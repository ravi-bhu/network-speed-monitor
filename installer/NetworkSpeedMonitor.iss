; Network Speed Monitor - per-user installer (Inno Setup 6.3 or newer).
;
; Build one installer per architecture from the repository root (SourceExe is relative to this installer folder, or use an absolute path):
;   ISCC.exe /DMyAppVersion=0.1.0 /DMyArch=x64   /DSourceExe=..\build\src\ui\Release\NetworkSpeedMonitor.exe installer\NetworkSpeedMonitor.iss
;   ISCC.exe /DMyAppVersion=0.1.0 /DMyArch=arm64 /DSourceExe=..\build-arm64\src\ui\Release\NetworkSpeedMonitor.exe installer\NetworkSpeedMonitor.iss
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
; File metadata of the setup exe. Code signing (docs/signing.md) checks the product name and version.
VersionInfoVersion={#MyAppVersion}
VersionInfoProductName={#MyAppName}
DefaultDirName={localappdata}\Programs\NetworkSpeedMonitor
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
DisableDirPage=auto
PrivilegesRequired=lowest
; A running app is closed gracefully by [Code] (CloseRunningApp) before anything else happens, in
; interactive and silent runs alike. There is deliberately no AppMutex: its check shows a "please close
; the app" message box that a silent winget upgrade or uninstall cannot answer.
; CloseApplications stays as the fallback: if the app is somehow still running when files are replaced,
; Restart Manager asks it to close (WM_QUERYENDSESSION/WM_ENDSESSION, which the app handles by saving
; and exiting). "yes", not "force", so nothing is ever killed. The app is started again by [Run].
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
; Starts the app after every install, silent ones too (no skipifsilent): a winget install or upgrade then
; shows the readout at once, and the app's first run writes its own autostart Run value (Start with
; Windows is on by default, spec 8.2). An interactive install shows this as the checked "Launch" box on
; the last page.
Filename: "{app}\{#MyAppExe}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall

[Code]
const
  // kAppWindowClass in src/ui/app.h: the app's hidden top-level window. Only the app registers it.
  AppWindowClass = 'NetworkSpeedMonitor.AppWindow';
  WmClose = $0010;          // WM_CLOSE: the app saves its usage, restores the Win10 task list and exits
  CloseTimeoutMs = 10000;   // how long to wait for it to go
  ClosePollMs = 250;

var
  // Whether setup found the app running (and asked it to close). Written to the setup log (/LOG).
  AppWasRunning: Boolean;

// Asks a running copy of the app to exit, exactly as its own Exit menu item does, then waits up to
// CloseTimeoutMs for its window to disappear. Never kills it: if it is still there after the wait
// (hung, or elevated so Windows drops the message), setup carries on and Restart Manager
// (CloseApplications) gets its turn. Returns whether the app was running.
function CloseRunningApp(): Boolean;
var
  Wnd: HWND;
  Waited: Integer;
begin
  Wnd := FindWindowByClassName(AppWindowClass);
  Result := Wnd <> 0;
  if not Result then
    Exit;
  PostMessage(Wnd, WmClose, 0, 0);
  Waited := 0;
  while (FindWindowByClassName(AppWindowClass) <> 0) and (Waited < CloseTimeoutMs) do
  begin
    Sleep(ClosePollMs);
    Waited := Waited + ClosePollMs;
  end;
  if FindWindowByClassName(AppWindowClass) <> 0 then
    Log('Network Speed Monitor did not exit within the wait; leaving it to Restart Manager.');
end;

// Setup (install or upgrade, interactive or silent): close the app first, so its files can be replaced
// and it cannot write to its database mid-upgrade.
function InitializeSetup(): Boolean;
begin
  AppWasRunning := CloseRunningApp();
  if AppWasRunning then
    Log('Network Speed Monitor was running; asked it to close.');
  Result := True;
end;

// Uninstall (interactive or silent): close the app first, so its exe can be removed and the history
// question below never races a running app. It is not restarted.
function InitializeUninstall(): Boolean;
begin
  if CloseRunningApp() then
    Log('Network Speed Monitor was running; asked it to close.');
  Result := True;
end;

// Usage history lives in %LOCALAPPDATA%\NetworkSpeedMonitor (usage.db, settings.ini).
// An interactive uninstall asks before deleting it; a silent one (winget, /VERYSILENT) always keeps it.
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if (CurUninstallStep = usPostUninstall) and (not UninstallSilent) then
    if MsgBox('Also delete your usage history?', mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES then
      DelTree(ExpandConstant('{localappdata}\NetworkSpeedMonitor'), True, True, True);
end;
