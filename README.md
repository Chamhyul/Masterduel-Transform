# MasterDuel Transform (AutoTransform)

Adobe Premiere Pro 및 After Effects용 고성능 모션 변환 및 GPU 가속 플러그인입니다.  
수동 키프레임 작업 없이 컷편집 지점 및 클립 타이밍에 맞추어 직관적인 이동/크기 조절과 모션 블러를 자동으로 렌더링합니다.

---

## ✨ 주요 기능 (Features)

1. **키프레임 없는 자동 트랜스폼 (No-Keyframe Auto Transform)**:
   - 클립의 컷편집 시작점(In-point) 및 지속 시간(Duration)을 감지하여 시작 위치/크기에서 목표 위치/크기로 부드러운 애니메이션 자동 생성.
   - 일반 비디오 클립뿐만 아니라 **조정 레이어(Adjustment Layer)**에서도 완벽한 0프레임 동기화 지원.

2. **Apple Metal GPU 가속 및 서브샘플링 모션 블러 (Metal MPE & Motion Blur)**:
   - Apple Silicon Mac을 위한 네이티브 Metal GPU 파이프라인 탑재.
   - 셔터각(Shutter Angle, 0° ~ 720°) 및 다중 서브샘플링(Samples, 2 ~ 32)을 통한 고품질 광학 모션 블러 렌더링.

3. **이징 프리셋 및 실시간 양방향 UI 바인딩**:
   - `Linear`, `Ease In`, `Ease Out`, `Ease In & Out`, `Custom` 프리셋 제공.
   - 슬라이더 조작 시 드롭다운이 즉시 동기화되는 실시간 반응형 파라미터 UI.

4. **프리셋 분할 및 모디파이어 화면 분할 투명화**:
   - 화면 분할 모드(`<`, `>`)에 따른 가시 영역 절삭 및 마진 블리딩 자동 처리.

---

## 🛠 빌드 요구사항 (Build Prerequisites)

본 프로젝트는 Adobe 사유 SDK를 포함하지 않습니다. 로컬에서 직접 빌드하려면 다음 공식 SDK가 상위 또는 지정 경로에 준비되어 있어야 합니다:

- **운영체제**: macOS 11.0 Big Sur 이상 (Apple Silicon / Intel)
- **도구체인**: Clang (Xcode Command Line Tools), `metal`, `Rez`
- **Adobe SDK**:
  - `Adobe After Effects SDK 26.5` (또는 호환 버전)
  - `Adobe Premiere Pro 26.0 C++ SDK` (또는 호환 버전)

---

## 📦 빌드 방법 (Building on macOS)

```bash
cd AutoTransform/Mac
./build.sh
```

빌드가 완료되면 `AutoTransform/build/AutoTransform.plugin` 번들이 생성됩니다.

---

## 📥 설치 (Installation)

### 간편 설치 (배포 패키지)
릴리즈(Releases) 탭에서 제공되는 `MasterDuel Transform v0.1.0.pkg` 파일을 다운로드하여 실행하면 Premiere Pro 공용 플러그인 폴더에 자동 설치됩니다.

### 수동 설치 경로
```bash
sudo cp -R "AutoTransform/build/AutoTransform.plugin" "/Library/Application Support/Adobe/Common/Plug-ins/7.0/MediaCore/"
```

---

## 📄 라이선스 (License)

This project is licensed under the MIT License - see the LICENSE file for details.
