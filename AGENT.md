# Premiere Pro Auto Transform 플러그인 UI 및 파라미터 사양

현재 렌더링 파이프라인 교체 및 모션 블러를 구현 중인 에이전트에게 전달하는 UI 및 파라미터 연동 가이드라인입니다. 
사용자의 키보드 매크로 호환성 및 Premiere Pro 호스트의 특수성을 고려하여 아래 명시된 UI 구조와 규칙을 100% 준수하여 구현해야 합니다.

---

## 1. Effect Controls 패널 전체 UI 배치 구조 (순서 엄수)

키보드 매크로 호환성을 위해 **하단 4개 항목(Start/End Position, Start/End Scale)은 어떠한 그룹에도 포함되지 않으며, 반드시 최하단에 순서대로 영구 고정**되어야 합니다.

```text
▾ Auto Transform
▾ Timing & Easing           (접이식 토글 그룹: PF_ADD_TOPIC)
    Duration (frames)       10 frames (기본값: 10.0)
    Easing Preset           [ Linear ▾ ] (드롭다운 기본값: Linear)
                            (선택지: Linear | Ease In | Ease Out | Ease In & Out | Custom)
    Ease In (%)             0% (기본값: 0.0, 범위: 0 ~ 100%)
    Ease Out (%)            0% (기본값: 0.0, 범위: 0 ~ 100%)
(토글 그룹 종료: PF_END_TOPIC)

▾ Motion Blur               (접이식 토글 그룹: PF_ADD_TOPIC)
    Shutter Angle           0.0° (기본값: 0.0° = Off, 범위: 0.0° ~ 720.0°)
    Samples                 8 (기본값: 8, 범위: 2 ~ 32)
(토글 그룹 종료: PF_END_TOPIC)

────────────────────────────────────────────────────────
Start Position              (그룹 밖 최하단 고정, 기본 50% = 4K: 1920, 1080 / FHD: 960, 540)
End Position                (그룹 밖 최하단 고정, 기본 50% = 4K: 1920, 1080 / FHD: 960, 540)
Start Scale                 100% (그룹 밖 최하단 고정)
End Scale                   100% (그룹 밖 최하단 고정)
```

---

## 2. Parameter Disk ID 정책 (하위 호환성 유지)

기존 프로젝트 및 프리셋과의 호환을 위해 Disk ID 번호는 변경하지 않고 아래 순서대로 고정 부여합니다:

- `DURATION_DISK_ID = 1`
- `START_POSITION_DISK_ID = 2`
- `END_POSITION_DISK_ID = 3`
- `START_SCALE_DISK_ID = 4`
- `END_SCALE_DISK_ID = 5`
- `EASING_PRESET_DISK_ID = 6`
- `EASE_IN_DISK_ID = 7`
- `GROUP_TIMING_START_DISK_ID = 8`
- `EASE_OUT_DISK_ID = 9`
- `GROUP_TIMING_END_DISK_ID = 10`
- `GROUP_BLUR_START_DISK_ID = 11`
- `SHUTTER_ANGLE_DISK_ID = 12`
- `SAMPLES_DISK_ID = 13`
- `GROUP_BLUR_END_DISK_ID = 14`

---

## 3. 핵심 동작 및 상호작용 규칙 (검증 완료된 필수 사항)

### 1) Point 파라미터 기본값(Default) 등록 규칙 (오버플로우 방지)
- Premiere Pro에서 `PF_PointDef`의 실제 값(`x_value`, `y_value`)은 **절대 픽셀 좌표**입니다.
- 단, `ParamsSetup`에서 등록하는 초기 기본값(`x_dephault`, `y_dephault`)은 반드시 **퍼센트(`FLOAT2FIX(50)`)**로 전달해야 합니다.
- 만약 기본값 자리에 픽셀(예: 960, 1920)을 넣으면 너비의 960%로 인식되어 `32767.0`으로 오버플로우가 발생합니다. `50%`로 등록해야 Premiere Pro가 클립 해상도를 인식하여 4K는 `(1920, 1080)`, FHD는 `(960, 540)`으로 정확히 띄워줍니다.
- **다운샘플링 수동 보정 금지**: Premiere Pro는 프로그램 모니터가 1/4 해상도일 때 파라미터 좌표를 이미 1/4로 다운샘플링하여 넘겨주므로, 플러그인 코드에서 `downsample_x/y`를 곱하면 이중 축소(1/16)가 발생합니다. `x_value`를 그대로 읽어 사용하십시오.

### 2) Easing Preset ↔ 슬라이더 실시간 양방향 동기화 (`PF_Cmd_USER_CHANGED_PARAM`)
Premiere Pro에서 다른 파라미터의 UI 값을 실시간 갱신하기 위해서는 다음 3가지 플래그가 필수입니다:
1. `GlobalSetup`에 `PF_OutFlag_SEND_UPDATE_PARAMS_UI` 추가.
2. `UserChangedParam` 이벤트 발생 시 대상 파라미터의 내부 값 수정 후 **`params[target]->uu.change_flags = PF_ChangeFlag_CHANGED_VALUE;`** 지정.
3. 호스트에게 패널을 즉시 다시 그리도록 **`out_data->out_flags |= PF_OutFlag_REFRESH_UI | PF_OutFlag_FORCE_RERENDER;`** 설정.

- **드롭다운 선택 시 슬라이더 변경 수치**:
  - `Linear`: In 0%, Out 0%
  - `Ease In`: In 70%, Out 0%
  - `Ease Out`: In 0%, Out 70%
  - `Ease In & Out`: In 70%, Out 70%
- **슬라이더 조작 시**:
  - 사용자가 `Ease In` 또는 `Ease Out` 슬라이더를 마우스나 키보드로 변경하는 즉시 드롭다운이 **`Custom`**으로 자동 전환되어야 함.
- **렌더링 직결**:
  - 드롭다운이 `Linear`, `Ease In`, `Ease Out`, `Ease In & Out`일 때는 드롭다운 프리셋 값(0/0, 70/0, 0/70, 70/70)으로 즉시 렌더링하고, `Custom`일 때만 슬라이더 수치를 읽어 렌더링해야 프리셋 변경이 실시간으로 100% 반영됩니다.

### 3) 조정 레이어(Synthetic)와 일반 클립 시간 계산 분기
- Premiere Pro에서 일반 비디오 클립은 `in_data->current_time`이 원본 미디어 시작점 기준이므로, `PF_UtilitySuite`의 `GetClipStart`를 차감하여 컷편집 시작점부터 0프레임이 되도록 해야 합니다.
- **하지만 조정 레이어(Adjustment Layer)는 파일이 없는 Synthetic 아이템**이므로 처음부터 `current_time`이 레이어 시작점 기준 0입니다. 여기에 `GetClipStart`를 빼면 음수가 되어 0프레임에서 멈추는 버그가 발생합니다.
- 반드시 `utilSuite->IsTrackItemEffectAppliedToSynthetic(in_data->effect_ref, &isSynthetic)`을 호출하여, **`isSynthetic == false`인 일반 클립일 때만 `GetClipStart`를 차감**하십시오.

### 4) Motion Blur 제어 방식
- 별도의 On/Off 체크박스 없이 `Shutter Angle`이 `0.0°`이면 모션 블러를 끄고(단일 패스 렌더링), `0.0°` 초과 시 설정된 샘플 수(`Samples`)만큼 다중 샘플링 및 누적 합성을 수행합니다.

---

## 4. [반복 금지 원칙] Metal GPU 파이프라인 시행착오 및 금지 규칙 (Never Repeat)

> [!CAUTION]
> **에이전트 필독**: 아래 명시된 항목들은 이미 수차례 시도하여 실패했거나 사용자에게 심각한 시간 낭비를 초래한 잘못된 접근법입니다. **어떠한 경우에도 아래 방식을 다시 시도하거나 추측성 코드를 작성하지 마십시오.**

### ❌ 절대 반복 금지 목록 (Forbidden Approaches)

1. **실측(실제 로그) 없는 가설 기반 코딩 절대 금지**:
   - SDK 문서나 디스어셈블리에서 본 문자열이 Premiere Pro 런타임 노드에 당연히 존재할 것이라고 가정하고 코드를 작성하는 행위 절대 금지.
   - 반드시 **로그로 해당 노드에 해당 키와 유효한 값이 찍히는 것을 직접 확인한 뒤에만** 그 키를 사용하는 코드를 작성할 것.

2. **비공개 / 미존재 프로퍼티 키 사용 금지**:
   - `EffectiveTrackItemStartAsTicks`: Premiere Pro 내부에 존재하지 않는 키임 (로그 실측으로 확인됨). 사용 금지.
   - `EffectNode::RuntimeInstanceID` (또는 `TransitionNode::...`): `mNodeID` 노드 프로퍼티에 존재하지 않는 키임. 임의 조회 금지.

3. **세그먼트 `outStartTime` 단순 차감 방식 금지**:
   - `inSequenceTime - outStartTime`을 바로 프레임으로 쓰는 방식 금지.
   - **이유**: 비디오 세그먼트는 타임라인 전체의 절단선이므로, 다른 비디오 트랙(V1 등)에 컷이 있으면 V2의 통 조정 레이어가 잘리지 않았음에도 세그먼트가 분할되어 효과가 중간에 강제 리셋되는 치명적 버그를 유발함.

4. **진단 로그에 불필요한 조건문(`ownerNodeID != 0`)을 걸어 출력을 차단하는 행위 금지**:
   - 조정 레이어에서는 `ownerNodeID`가 `0`이거나 노드 구조가 다름.
   - 로그 코드에 `ownerNodeID != 0`을 걸면 파일이 0바이트로 생성되어 닫히므로, 실측 기회를 스스로 날려버림. 로그는 항상 무조건 찍히도록 방어적으로 작성할 것.

---

## 5. [검증 완료 및 영구 보존] 클립 분할 시작점(0프레임) 통합 아키텍처

> [!IMPORTANT]
> **검증 완료 (2026-09-19)**: 일반 비디오 클립뿐만 아니라 조정 레이어에서도 칼날(Razor)로 분할했을 때 분할된 조각의 시작점에서 정확하게 0프레임부터 효과가 시작되도록 하는 **통합 InPoint 아키텍처**가 최종 검증되었습니다. 향후 어떠한 작업에서도 이 타이밍 파이프라인을 훼손하거나 임의 변경하지 마십시오.

### 1) 기술적 배경 및 규명된 사실
* **CPU 렌더링**: `in_data->effect_ref`를 통해 `PF_UtilitySuite::GetTrackItemStart` 한 줄로 호스트가 클립 시작점을 돌려줌.
* **Metal GPU 렌더링 (`PrGPUFilterBase`)**:
  * 호스트가 `effect_ref`를 제공하지 않고 `mNodeID`, `mTimelineID`, `inSequenceTime`, `inClipTime`만 전달함.
  * **일반 비디오 클립**: `AcquireOperatorOwnerNodeID(mNodeID, &ownerNodeID)`가 성공하며, `ownerNodeID`의 직속 자식에 `RenderableNodeMediaImpl`이 존재함.
  * **조정 레이어 (Adjustment Layer)**: `AcquireOperatorOwnerNodeID`가 항상 `0`을 반환함!

### 2) 통합 InPoint 아키텍처 구조 (ImpactFX 바이너리 100% 동일 구현)
Premiere Pro 내부에서 일반 비디오 클립과 조정 레이어는 모두 분할 지점의 인포인트를 **`MediaNode::InPointMediaTimeAsTicks`**라는 동일한 프로퍼티에 저장합니다:

```text
[일반 비디오 클립 (ownerNodeID != 0)]
ownerNodeID (RenderableNodeClipImpl)
└── 자식 노드 (RenderableNodeMediaImpl)
    └── MediaNode::InPointMediaTimeAsTicks

[조정 레이어 (ownerNodeID == 0)]
AcquireNodeForTime(inSequenceTime)
└── segNodeID (RenderableNodeCompositorImpl)
    └── Layer[0] (RenderableNode_AdjustmentImpl)
        ├── LayerSubIn[0] (RenderableNodeCompositorImpl)  ← 🚨 아래 깔린 V1 배경 영상 (탐색 제외 필수!)
        ├── LayerSubIn[1] (RenderableNodeClipImpl)
        └── LayerSubIn[2] (RenderableNodeMediaImpl)       ← 🎯 조정 레이어 자신의 미디어 노드!
            └── MediaNode::InPointMediaTimeAsTicks
```

### 3) 핵심 구현 규칙 (ImpactFX 역순 탐색 기법)
1. **조정 레이어 탐색 시 역순 순회 필수**:
   - `Layer[0]` (`RenderableNode_AdjustmentImpl`)의 서브 입력을 순회할 때, 반드시 **마지막 인덱스(`subCount - 1`)부터 0번으로 역순 순회**해야 함.
   - `0번` 서브 입력은 항상 아래 비디오 트랙(컴포지터)이므로, 순방향으로 탐색하면 배경 영상의 거대한 InPoint를 오탐하여 시간 계산이 음수로 실패함.
   - 역순으로 순회하면 맨 마지막에 위치한 조정 레이어 자체의 `RenderableNodeMediaImpl`을 오탐 없이 즉시 획득함.
2. **통합 시간 계산 공식**:
   $$\text{elapsedTicks} = \text{inClipTime} - \text{InPointMediaTimeAsTicks}$$
   $$\text{currentFrame} = \frac{\text{elapsedTicks}}{\text{ticksPerFrame}}$$
   - 분할 지점(In-point)에서는 $\text{inClipTime} = \text{InPointMediaTimeAsTicks}$ 이므로 **정확하게 0프레임** 도출.

---

## 6. [지속 수정 및 의사결정 기록 (Decision & Change Log)]

모든 에이전트는 타이밍 로직 및 파이프라인 수정 시 아래 로그에 작업 내역, 결과, 교훈을 반드시 기록해야 합니다.

### [2026-09-19 15:30]
- **작업 내용**: Film Impact 디스어셈블리 분석 후 `EffectNode::RuntimeInstanceID` 및 세그먼트 매칭 코드 구현 시도.
- **결과**: **실패**. 조정 레이어가 여전히 원본 시작점에서 시작함.
- **실패 원인 분석**:
  1. `mNodeID`에 `EffectNode::RuntimeInstanceID` 키가 없어 ID가 `0`으로 조회됨.
  2. 세그먼트 트리 매칭이 `0`과 비교하여 전부 실패하고 fallback으로 떨어짐.
  3. 로그 코드에 `ownerNodeID != 0` 검사가 있어 조정 레이어 렌더 시 로그가 0바이트로 닫힘.
- **교훈**: 가설 기반 코딩 금지. 실측 로그 우선 원칙 확립.

### [2026-09-19 15:40] (결정적 돌파구: 전수 실측 로그 확보)
- **실측 결과**:
  1. `AcquireOperatorOwnerNodeID result: ownerNodeID=0` 확인. 조정 레이어는 `ownerNodeID`가 없음을 규명.
  2. 세그먼트 노드 하위 `Layer[0]` (`RenderableNode_AdjustmentImpl`) -> `LayerSubIn[2]` (`RenderableNodeMediaImpl`)에 `MediaNode::InPointMediaTimeAsTicks`가 실제로 존재함을 100% 실측 확인.

### [2026-09-19 15:45]
- **작업 내용**: 재귀 탐색(`FindInPointTicksInHierarchy`)으로 `MediaNode::InPointMediaTimeAsTicks`를 조회하도록 구현.
- **결과**: 일반 비디오 정상, **조정 레이어 실패**.
- **원인**: DFS 깊이 우선 탐색이 `0번` 서브 입력인 `LayerSubIn[0]`(V1 배경 영상 컴포지터)으로 먼저 들어가 배경 영상의 수조 단위 InPoint를 가져와 음수 에러 발생.

### [2026-09-19 15:55 ~ 16:10] (최종 해결 및 사용자 검증 완료)
- **작업 내용**:
  - `FindAdjustmentLayerInPoint` 전용 헬퍼 구현: ImpactFX 바이너리(`RecursiveFindMediaNode`, `0x23128`)와 100% 동일하게 서브 입력을 **마지막 인덱스부터 역순(`subCount - 1` -> `0`)으로 탐색**하여 0번 배경 컴포지터를 원천 배제하고 직속 `MediaNode`(`LayerSubIn[2]`)를 정확히 타겟팅.
  - `inClipTime - InPointMediaTimeAsTicks` 공식을 일반 비디오와 조정 레이어 양쪽에 동일 적용.
- **검증 결과**:
  - **사용자 검증 완료**: 일반 비디오 클립 및 조정 레이어 모두 칼날로 분할된 지점에서 **정확히 0프레임부터 효과 시작 확인 ("문제 해결 됐어")**.
- **최종 상태**: **해결 완료 및 아키텍처 영구 보존**.

---

## 7. Windows 포팅 및 설치 파일 제작 가이드라인 (Windows 에이전트 필독)

> [!IMPORTANT]
> **Windows 개발 환경 안내**: 본 프로젝트는 macOS(Apple Silicon / Metal)에서 완벽히 검증 및 첫 배포(v0.1.0)를 완료한 상태입니다. Windows 데스크톱에서 NVIDIA(CUDA) 및 AMD(OpenCL) GPU 가속을 완성하고 Windows 전용 설치 파일(`.exe`)을 제작하기 위한 지침입니다.

### 1) 필수 개발 환경 및 준비 사항
1. **Visual Studio 2022**:
   - 워크로드: **C++를 사용한 데스크톱 개발** 필수 설치.
2. **NVIDIA CUDA Toolkit (12.x)**:
   - 설치 시 Visual Studio와의 빌드 커스터마이징(Build Customizations)이 자동 연동됨.
3. **Windows용 Adobe SDK**:
   - `Premiere Pro 26.0 C++ SDK (Windows)`
   - `Adobe After Effects SDK 26.5 (Windows)`
   - *주의*: 저장소에는 라이선스 보호를 위해 SDK가 제외되어 있으므로 로컬 프로젝트 상위 또는 동일 경로에 배치해야 함.
4. **Inno Setup 6 (설치 프로그램 제작 도구)**:
   - 공식 사이트(jrsoftware.org)에서 무료 다운로드 설치.

---

### 2) Visual Studio 프로젝트 구성 팁 (Fast-Track)
* **빈 프로젝트를 처음부터 만들지 말 것**:
  - Windows SDK 내의 **`Premiere Pro 26.0 C++ SDK/Examples/Projects/GPUVideoFilter/Vignette/Win/Vignette.vcxproj`** (또는 `SDK_ProcAmp.vcxproj`)를 복사하여 프로젝트 템플릿으로 사용할 것.
  - 이 프로젝트 파일에는 이미 **CUDA 빌드 규칙(CUDA 12.x.props)**, Premiere Pro MPE 헤더 인클루드 경로, 라이브러리 링크 및 리소스 컴파일러(`.rc`) 설정이 완벽하게 갖추어져 있음.
* **출력 바이너리 파일명**:
  - Windows 환경의 Adobe 플러그인은 DLL 바이너리이며 확장자만 `.aex`임: **`AutoTransform.aex`**

---

### 3) GPU 파이프라인 포팅 핵심 규칙 (Never Repeat)

1. **타이밍 및 세그먼트 탐색 로직 100% 무결성 유지**:
   - `FindAdjustmentLayerInPoint`, `ExtractFromAdjustmentNode`, `FindInPointTicksInHierarchy` 등 `AGENT.md 5장`에 명시된 **통합 InPoint 탐색 아키텍처는 단 1글자도 변경하지 말고 공통으로 사용할 것**.
   - `currentFrame`, `duration`, 셔터각 서브샘플 적분(`subSamples`), 화면 분할 경계(`cropLeft`, `cropRight`) 계산은 모든 플랫폼이 동일하게 공유함.

2. **NVIDIA (CUDA) 파이프라인 사양**:
   - 프레임워크: `PrGPUDeviceFramework_CUDA`
   - 커널 파일: `AutoTransform.cu`
   - 정밀도: FP32(`float4`) 및 FP16(`half4`, `#include <cuda_fp16.h>`, `__half2float`, `__float2half_rn`) 지원.
   - **In-place 원본 버퍼 오염 방지**: Premiere Pro의 출력 버퍼는 단일 공간이므로, 원본 버퍼를 직접 읽으면서 쓰면 픽셀 왜곡 발생. 필터 인스턴스 멤버(`void* mCUDATempBuffer`)로 임시 디바이스 버퍼를 캐싱하고, `cudaMemcpyAsync`로 복사 후 커널 실행 필수! (매 프레임 `cudaMalloc` 금지, 해상도 변경 시에만 재할당)

3. **AMD / Intel (OpenCL) 파이프라인 사양**:
   - 프레임워크: `PrGPUDeviceFramework_OpenCL`
   - 런타임 JIT 컴파일: 외부 파일 로드 실패 방지를 위해 OpenCL 커널 소스를 C++ 원시 문자열 리터럴(`R"===( ... )==="`)로 임베딩하여 `clCreateProgramWithSource` + `clBuildProgram` + `clCreateKernel` 수행.
   - 최초 1회만 컴파일하도록 `cl_kernel sOpenCLKernelCache[kMaxDevices]` 캐시 유지.
   - 정밀도: OpenCL 1.1+ 표준 내장 함수인 `vload_half4`, `vstore_half4_rtz`를 사용하여 하드웨어 익스텐션 종속성 없이 안전하게 지원.
   - In-place 복사: `clEnqueueCopyBuffer`를 통한 임시 버퍼(`mCLTempBuffer`) 백업 후 실행.

---

### 4) Inno Setup을 이용한 Windows 설치 프로그램 제작 스크립트 규격

* Windows에서 빌드된 `AutoTransform.aex`를 일반 사용자에게 배포하기 위해 Inno Setup 스크립트(`installer_win.iss`)를 작성하여 컴파일합니다.
* **설치 대상 공식 경로**: `C:\Program Files\Adobe\Common\Plug-ins\7.0\MediaCore\`

```ini
[Setup]
AppName=MasterDuel Transform
AppVersion=0.1.0
AppPublisher=참혈
AppPublisherURL=https://www.youtube.com/@참혈
AppSupportURL=https://github.com/Chamhyul/Masterduel-Transform
DefaultDirName={commonpf}\Adobe\Common\Plug-ins\7.0\MediaCore
DisableDirPage=yes
OutputBaseFilename=MasterDuel Transform v0.1.0 (Windows)
OutputDir=.
Compression=lzma2/ultra64
SolidCompression=yes
ArchitecturesInstallIn64BitMode=x64

[Files]
Source: "AutoTransform.aex"; DestDir: "{app}"; Flags: ignoreversion

[Messages]
WelcomeLabel1=MasterDuel Transform v0.1.0 설치를 시작합니다.
WelcomeLabel2=Adobe Premiere Pro용 키프레임 없는 자동 트랜스폼 플러그인입니다.%n%n설치를 진행하시면 Adobe 공용 플러그인 폴더에 자동으로 등록됩니다.
```

* 산출물: **`MasterDuel Transform v0.1.0 (Windows).exe`**

---

### 5) GitHub Releases 배포 동기화
* Windows 설치 프로그램 생성이 완료되면 GitHub CLI를 통해 기존 v0.1.0 릴리즈에 에셋을 추가 업로드합니다:
  ```bash
  gh release upload v0.1.0 "MasterDuel Transform v0.1.0 (Windows).exe"
  ```
* 릴리즈 제목 및 노트를 `MasterDuel Transform v0.1.0 (macOS 및 Windows)`로 갱신하여 멀티 플랫폼 배포를 완성합니다.








