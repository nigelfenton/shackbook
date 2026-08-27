; Inno Setup script for ShackBook Windows installer.
;
; Built by GitHub Actions on tag push (see .github/workflows/release.yml).
; Locally: install Inno Setup 6, then from a CMD prompt:
;   set SHACKBOOK_VERSION=0.1.0
;   set SHACKBOOK_STAGING=C:\path\to\staged\ShackBook-v0.1.0-windows-x64
;   "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" installer\shackbook.iss
;
; The staging directory must already contain ShackBook.exe + the Qt DLLs +
; plugin subdirs (platforms, sqldrivers, …) — i.e. exactly what
; windeployqt produces in build/.

#define MyAppName        "ShackBook"
#define MyAppVersion     GetEnv("SHACKBOOK_VERSION")
#define MyAppPublisher   "Nigel Fenton (G0JKN / W3)"
#define MyAppURL         "https://github.com/nigelfenton/shackbook"
#define MyAppExeName     "ShackBook.exe"
#define StagingDir       GetEnv("SHACKBOOK_STAGING")

#if MyAppVersion == ""
  #error SHACKBOOK_VERSION must be set before compiling
#endif
#if StagingDir == ""
  #error SHACKBOOK_STAGING must point at a windeployqt'd staging directory
#endif

[Setup]
; AppId is a fixed GUID — keep this stable forever so upgrades replace
; the previous install instead of installing alongside it.
AppId={{C8D7E1A3-5F92-4A18-8B6D-4E3A6F9D0B27}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}/issues
AppUpdatesURL={#MyAppURL}/releases
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
AllowNoIcons=yes
LicenseFile={#StagingDir}\LICENSE
OutputDir=installer-output
OutputBaseFilename=ShackBook-Setup-{#MyAppVersion}-windows-x64
SetupIconFile=..\src\logbook.ico
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; Per-user install by default (no admin rights), but allow admin upgrade.
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; \
    GroupDescription: "Additional icons:"; Flags: unchecked

[Files]
; Recurse the entire windeployqt staging directory into {app}.  Skip the
; bundled VC redist installer — Inno Setup itself handles redist via the
; [Run] section below if we ever need it; bundling the .exe inside the
; install dir just clutters Program Files.
Source: "{#StagingDir}\*"; DestDir: "{app}"; \
    Excludes: "vc_redist.x64.exe"; \
    Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{userdesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; \
    Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#MyAppName}}"; \
    Flags: nowait postinstall skipifsilent
