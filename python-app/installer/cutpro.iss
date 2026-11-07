; Inno Setup script for Cut Pro
; Build with:  ISCC.exe installer\cutpro.iss
; (or open this file in the Inno Setup Compiler and choose Build > Compile)

#define MyAppName "Cut Pro"
; MUST match core::kVersion in src/core/version.h. The in-app updater compares
; the GitHub release tag against that compiled-in version, so each published
; release has to bump both this and version.h in lockstep or the update icon
; will never appear (or will re-offer a build the user already has).
#define MyAppVersion "0.1.4.1"
#define MyAppPublisher "Antigravity"
#define MyAppExeName "cutpro.exe"

; The already-deployed, runnable application folder. Everything under it ships,
; minus the excludes in [Files] (stale backups, debug logs, the test binary and
; Python bytecode caches).
#define MyDistDir "..\dist\Cut Pro 2026"
#define MyAssetsDir "..\assets"

[Setup]
; AppId uniquely identifies this application for install/upgrade/uninstall.
; Keep it fixed across versions so a new build upgrades the old one in place.
AppId={{3F99534F-E688-4A4A-9F51-99CAEDA45690}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
UninstallDisplayName={#MyAppName} {#MyAppVersion}
UninstallDisplayIcon={app}\{#MyAppExeName}
; 64-bit Qt 6 build: install into the real Program Files and mark the app 64-bit.
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; Per-machine install (Program Files). Requires elevation.
PrivilegesRequired=admin
Compression=lzma2/max
SolidCompression=yes
; The in-app updater launches this setup with /SILENT /CLOSEAPPLICATIONS
; /RESTARTAPPLICATIONS. Make Inno close a running Cut Pro that locks the files
; being replaced, then relaunch it after the upgrade.
; force (not just yes): Cut Pro runs as a main process PLUS windowless helper
; cutpro.exe worker processes. "yes" only closes apps that answer the Restart
; Manager politely (windowed ones); the helpers don't, so they keep cutpro.exe
; and its DLLs locked and the silent upgrade leaves old files behind (the
; "conflicts with the old version" bug). "force" terminates every process
; holding the files so the replace always succeeds.
CloseApplications=force
RestartApplications=yes
WizardStyle=modern
SetupIconFile={#MyAssetsDir}\cutpro.ico
OutputDir=out
OutputBaseFilename=CutPro-{#MyAppVersion}-Setup
DisableProgramGroupPage=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
; The whole deployed tree. Excludes keep build debris and non-shipped data out:
;   *.old                      - rename-aside backups from redeploys
;   *.log                      - debug/launch logs left in the run folder
;   cutpro_backend_tests.exe   - the test binary, not shipped to users
;   __pycache__                - Python bytecode caches under tools\
;   models\*                   - Whisper GGML weights are user-downloaded on
;                                demand from Settings into models\whisper; they
;                                must never be baked into the installer.
;   license_gen*.exe           - the internal license-key generator. A customer
;                                who received it could mint their own licenses,
;                                so it must never ship in the installer.
Source: "{#MyDistDir}\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion; Excludes: "*.old,*.log,cutpro_backend_tests.exe,license_gen*.exe,*\__pycache__\*,__pycache__\*,models\*"
; Speech cleanup has a small bundled model and must work offline after install.
Source: "{#MyDistDir}\models\noise-removal\*"; DestDir: "{app}\models\noise-removal"; Flags: ignoreversion

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\Uninstall {#MyAppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent
