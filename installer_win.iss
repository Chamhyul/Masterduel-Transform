; Script generated for Inno Setup 6
; MasterDuel Transform Windows Installer Script

[Setup]
AppName=MasterDuel Transform
AppVersion=0.1.2
AppPublisher=참혈
AppPublisherURL=https://www.youtube.com/@참혈
AppSupportURL=https://github.com/Chamhyul/Masterduel-Transform
DefaultDirName={commonpf}\Adobe\Common\Plug-ins\7.0\MediaCore
DisableDirPage=yes
LicenseFile=LICENSE
OutputBaseFilename=win_x64_MD_Transform.v0.1.2
OutputDir=.
Compression=lzma2/ultra64
SolidCompression=yes
ArchitecturesInstallIn64BitMode=x64compatible
VersionInfoVersion=0.1.2.0
VersionInfoCompany=참혈
VersionInfoDescription=MasterDuel Transform for Adobe Premiere Pro
VersionInfoCopyright=Copyright (C) 2026 참혈. All rights reserved.

[Files]
; 빌드된 릴리즈 바이너리 참조
Source: "AutoTransform\build\Release\AutoTransform.aex"; DestDir: "{app}"; Flags: ignoreversion

[Messages]
WelcomeLabel1=MasterDuel Transform v0.1.2 (Windows) 설치를 시작합니다.
WelcomeLabel2=Adobe Premiere Pro용 키프레임 없는 자동 트랜스폼 플러그인입니다.%n%n• 개발자: 참혈%n• 유튜브: https://www.youtube.com/@참혈%n• GitHub: https://github.com/Chamhyul/Masterduel-Transform%n%n설치를 진행하시면 Adobe 공용 플러그인 폴더에 자동으로 등록됩니다.

[Code]
function InitializeSetup(): Boolean;
var
  InstalledFile: String;
  InstalledVer: String;
  InstalledMS, InstalledLS: Cardinal;
  CurrentMS, CurrentLS: Cardinal;
begin
  Result := True;
  InstalledFile := ExpandConstant('{commonpf}\Adobe\Common\Plug-ins\7.0\MediaCore\AutoTransform.aex');

  // 현재 인스톨러 버전: 0.1.2.0
  CurrentMS := (0 shl 16) or 1; // Major: 0, Minor: 1
  CurrentLS := (2 shl 16) or 0; // Build: 2, Revision: 0

  if FileExists(InstalledFile) and GetVersionNumbers(InstalledFile, InstalledMS, InstalledLS) then
  begin
    // 1. 기존 설치 파일이 현재 버전보다 높은 경우 (최신 버전이 이미 설치됨)
    if (InstalledMS > CurrentMS) or ((InstalledMS = CurrentMS) and (InstalledLS > CurrentLS)) then
    begin
      GetVersionNumbersString(InstalledFile, InstalledVer);
      MsgBox('이미 더 최신 버전(' + InstalledVer + ')이 설치되어 있습니다.' + #13#10 +
             '설치를 중단합니다.', mbInformation, MB_OK);
      Result := False;
      Exit;
    end
    // 2. 기존 설치 파일과 현재 버전이 동일한 경우 (동일 버전 재설치/덮어쓰기 여부 확인)
    else if (InstalledMS = CurrentMS) and (InstalledLS = CurrentLS) then
    begin
      if MsgBox('MasterDuel Transform v0.1.2 버전이 이미 설치되어 있습니다.' + #13#10 + #13#10 +
                '플러그인을 다시 설치(덮어쓰기)하시겠습니까?', mbConfirmation, MB_YESNO) = IDNO then
      begin
        Result := False;
        Exit;
      end;
    end;
    // 3. 기존 설치 파일이 구버전인 경우 -> 질문 없이 정상 업그레이드 진행 (Result := True)
  end;
end;
