; StereoFlow - Inno Setup 6 script. Builds BOTH outputs from the same file:
;   iscc installer\StereoFlow.iss /DMyVersion=1.0.0                -> dist\StereoFlow-1.0.0-win64-setup.exe     (installer)
;   iscc installer\StereoFlow.iss /DMyVersion=1.0.0 /DPORTABLE=1   -> dist\StereoFlow-1.0.0-win64-portable.exe  (extract-only, no install)
; Normally run via scripts\package.bat, which builds first: the files are taken from build\vs2022\bin\Release (Qt runtime already deployed there).

#ifndef MyVersion
  #define MyVersion "1.0.0"
#endif
#define MyAppName "StereoFlow"
#define MyPublisher "DRwander"
#define MyURL "https://github.com/DRwander84/StereoFlow"
#define MyExe "StereoFlow.exe"
#ifndef BinDir
  #define BinDir "..\build\vs2022\bin\Release"
#endif

[Setup]
AppId={{6F1D3C52-8B7A-4E39-9A4C-5D2E7B0C9F41}
AppName={#MyAppName}
AppVersion={#MyVersion}
AppVerName={#MyAppName} {#MyVersion}
AppPublisher={#MyPublisher}
AppPublisherURL={#MyURL}
AppSupportURL={#MyURL}/issues
AppUpdatesURL={#MyURL}/releases
VersionInfoVersion={#MyVersion}
VersionInfoCompany={#MyPublisher}
VersionInfoDescription={#MyAppName} setup
AppCopyright=Copyright 2026 DRwander (@DRwander84)
LicenseFile=..\LICENSE
SetupIconFile=..\assets\icon\StereoFlow.ico
OutputDir=..\dist
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.19041
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
DisableProgramGroupPage=yes
; StereoFlow never needs administrator rights: per-user install by default, the user may still pick "all users".
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
; Refuse to install/update/uninstall while StereoFlow is running (same mutex the app uses for "single instance").
AppMutex=Local\StereoFlow.SingleInstance
CloseApplications=no

#ifdef PORTABLE
; ---- portable: only unpacks the files into a folder the user picks. No uninstaller, no registry, no shortcuts.
OutputBaseFilename=StereoFlow-{#MyVersion}-win64-portable
DefaultDirName={userdesktop}\StereoFlow
DisableDirPage=no
UsePreviousAppDir=no
Uninstallable=no
CreateUninstallRegKey=no
DisableReadyPage=yes
DisableFinishedPage=no
#else
; ---- setup
OutputBaseFilename=StereoFlow-{#MyVersion}-win64-setup
DefaultDirName={autopf}\{#MyAppName}
UninstallDisplayIcon={app}\{#MyExe}
UninstallDisplayName={#MyAppName}
#endif

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

#ifndef PORTABLE
[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
#endif

[Files]
Source: "{#BinDir}\*"; DestDir: "{app}"; Excludes: "*.pdb,*.ilk,*.lib,*.exp,*.obj"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\NOTICE"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\README.md"; DestDir: "{app}"; Flags: ignoreversion

#ifndef PORTABLE
[Icons]
Name: "{autoprograms}\{#MyAppName}"; Filename: "{app}\{#MyExe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyExe}"; Tasks: desktopicon
#endif

[Run]
Filename: "{app}\{#MyExe}"; Description: "{cm:LaunchProgram,{#MyAppName}}"; Flags: nowait postinstall skipifsilent

[Code]
// When StereoFlow quits it may leave a tiny headless copy of itself ("--restore-watch", up to 30 minutes) that gives
// hushed apps their volume back. It holds StereoFlow.exe open, so end it before files are replaced or removed.
// (AppMutex guarantees the real, visible StereoFlow is not running at this point.)
procedure StopRestoreWatch();
var
  rc: Integer;
begin
  Exec(ExpandConstant('{sys}\taskkill.exe'), '/F /IM {#MyExe}', '', SW_HIDE, ewWaitUntilTerminated, rc);
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  StopRestoreWatch();
  Result := '';
end;

#ifndef PORTABLE
function InitializeUninstall(): Boolean;
begin
  StopRestoreWatch();
  Result := True;
end;
#endif
