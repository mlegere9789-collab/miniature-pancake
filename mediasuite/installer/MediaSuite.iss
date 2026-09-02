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
#define MyToolsStagedDir "..\tools-staged"
#define MyAppIcon "..\src\MediaSuite.App\Assets\MediaSuite.ico"

[Setup]
; Generated once for this app and never reused elsewhere — Inno Setup uses it to
; recognise an upgrade of the same product rather than a fresh install.
AppId={{1EA2B3B5-BDE9-499E-9FA8-372CADD2BC2D}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
; The real GitHub repo this project lives in — not a placeholder domain. Same one
; GitHubReleaseUpdateChecker checks releases against and Settings' About card links to.
; Shows up in Windows' own "Programs and Features" list next to the uninstall button.
AppPublisherURL=https://github.com/mlegere9789-collab/miniature-pancake
AppSupportURL=https://github.com/mlegere9789-collab/miniature-pancake
AppUpdatesURL=https://github.com/mlegere9789-collab/miniature-pancake/releases
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
; The app's own exe already carries this icon (MediaSuite.App.csproj's ApplicationIcon),
; so Start Menu/desktop shortcuts and UninstallDisplayIcon above pick it up automatically
; by pointing at the exe — this line is only for the Setup.exe wizard itself, which
; otherwise shows Inno Setup's own generic icon instead of MediaSuite's.
SetupIconFile={#MyAppIcon}
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
; Third-party tool binaries fetched by fetch-tools.ps1 (see build.ps1, which runs it
; before this script) — MediaSuite ships self-contained, no separate download needed.
; A tool that fetch-tools.ps1 could not obtain simply has no folder here; ToolLocator
; already treats a missing folder as "not installed" rather than erroring, so a partial
; tools-staged directory still produces a working installer for whatever it did fetch.
Source: "{#MyToolsStagedDir}\*"; DestDir: "{app}\tools"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist
Source: "..\tools\README.md"; DestDir: "{app}\tools"; Flags: ignoreversion

[Dirs]
; In case fetch-tools.ps1 found nothing at all, {app}\tools should still exist so
; Settings has somewhere to point at. Not deleted on uninstall: see [UninstallDelete]
; below — never take a user's own downloaded tools, presets or Google Drive sign-in
; with it.
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
