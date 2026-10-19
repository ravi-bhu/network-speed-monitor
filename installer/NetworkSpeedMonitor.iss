; Network Speed Monitor - per-user installer (Inno Setup 6.3 or newer).
;
; Build one installer per architecture from the repository root:
;   ISCC.exe /DMyAppVersion=0.1.0 /DMyArch=x64   /DSourceExe=build\src\ui\Release\NetworkSpeedMonitor.exe installer\NetworkSpeedMonitor.iss
;   ISCC.exe /DMyAppVersion=0.1.0 /DMyArch=arm64 /DSourceExe=build-arm64\src\ui\Release\NetworkSpeedMonitor.exe installer\NetworkSpeedMonitor.iss
; The result is dist\NetworkSpeedMonitor-<version>-<arch>-setup.exe.

#ifndef MyAppVersion
  #define MyAppVersion "0.1.0"
#endif
#ifndef MyArch
  #define MyArch "x64"
#endif
#ifndef SourceExe
  #define SourceExe "..\build\src\ui\Release\NetworkSpeedMonitor.exe"
#endif

#define MyAppName "Network Speed Monitor"
#define MyAppExe "NetworkSpeedMonitor.exe"

[Setup]
; AppId identifies this product to Windows and to winget. It was generated once and MUST NEVER CHANGE,
; or upgrades would install side by side and the old copy could not be uninstalled.
; (The doubled opening brace is Inno's escape for a literal brace.)
AppId={{A5C137BB-8BC5-49E5-A9AE-ABE0DCCB2EDF}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher=Ravi Bhushan
DefaultDirName={localappdata}\Programs\NetworkSpeedMonitor
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
DisableDirPage=auto
PrivilegesRequired=lowest
; Same string as kSingleInstanceMutex in src/ui/app.h. While the app runs, setup and uninstall
; ask the user to close it (CloseApplications offers to close it for them); nothing is force-killed.
AppMutex=Local\NetworkSpeedMonitor.SingleInstance.7d3c6a52-0c8e-4f0f-9b1e-5a7f2c1d9e43
CloseApplications=yes
RestartApplications=no
UninstallDisplayIcon={app}\{#MyAppExe}
UninstallDisplayName={#MyAppName}
OutputDir=..\dist
OutputBaseFilename=NetworkSpeedMonitor-{#MyAppVersion}-{#MyArch}-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
LicenseFile=..\LICENSE
MinVersion=10.0
#if MyArch == "arm64"
ArchitecturesAllowed=arm64
ArchitecturesInstallIn64BitMode=arm64
#else
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
#endif

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
Source: "{#SourceExe}"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\THIRD_PARTY_NOTICES.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\PRIVACY.md"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\{#MyAppName}"; Filename: "{app}\{#MyAppExe}"

[Registry]
; The app writes this value itself (Settings > Start with Windows). Declaring it here, without creating it,
; makes the uninstaller remove it.
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: none; ValueName: "NetworkSpeedMonitor"; Flags: uninsdeletevalue dontcreatekey

[Run]
Filename: "{app}\{#MyAppExe}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent

[Code]
// Usage history lives in %LOCALAPPDATA%\NetworkSpeedMonitor (usage.db, settings.ini).
// An interactive uninstall asks before deleting it; a silent one (winget, /VERYSILENT) always keeps it.
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if (CurUninstallStep = usPostUninstall) and (not UninstallSilent) then
    if MsgBox('Also delete your usage history?', mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES then
      DelTree(ExpandConstant('{localappdata}\NetworkSpeedMonitor'), True, True, True);
end;
