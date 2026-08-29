; Inno Setup script for MediaSuite.
;
; Expects a self-contained win-x64 publish of MediaSuite.App already sitting in
; ..\publish\MediaSuite — see build.ps1, which runs the publish and then this script in
; the right order. Keep MyAppVersion in step with <Version> in ..\Directory.Build.props;
; there is no automated link between the two for a project this size.

#define MyAppName "MediaSuite"
#define MyAppVersion "1.0.0"
#define MyAppPublisher "MediaSuite"
#define MyAppExeName "MediaSuite.exe"
#define MyPublishDir "..\publish\MediaSuite"

[Setup]
; Generated once for this app and never reused elsewhere — Inno Setup uses it to
; recognise an upgrade of the same product rather than a fresh install.
AppId={{1EA2B3B5-BDE9-499E-9FA8-372CADD2BC2D}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
UninstallDisplayIcon={app}\{#MyAppExeName}
OutputDir=..\dist
OutputBaseFilename=MediaSuiteSetup-{#MyAppVersion}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
; A personal single-user build, but a traditional Program Files install still needs
; elevation to write there — this is the classic installer behaviour, not the
; per-user/sandboxed alternative Inno Setup also supports.
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; GroupDescription: "Additional shortcuts:"; Flags: unchecked

[Files]
; Everything dotnet publish produced for the self-contained win-x64 build.
Source: "{#MyPublishDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
; So the empty tools folder this installer creates below isn't a mystery the first time
; someone looks inside it.
Source: "..\tools\README.md"; DestDir: "{app}\tools"; Flags: ignoreversion

[Dirs]
; Created empty on purpose — the bundled tools are third-party downloads the user adds
; themselves (see tools\README.md), never shipped in the installer. Not deleted on
; uninstall: see [UninstallDelete] below.
Name: "{app}\tools"

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\Uninstall {#MyAppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "Launch {#MyAppName}"; Flags: nowait postinstall skipifsilent

; No [UninstallDelete] section on purpose: Inno Setup's default uninstall already
; leaves {app}\tools and the user's settings folder under %AppData% alone, and that is
; exactly what should happen — uninstalling must never take a user's downloaded tool
; binaries, presets or Google Drive sign-in with it.
