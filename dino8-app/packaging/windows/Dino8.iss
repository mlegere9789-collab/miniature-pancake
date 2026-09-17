; Inno Setup script for Dino 8 (same layout as the MediaSuite installer).
; Expects the CMake install tree in ..\..\build\install (see the
; dino8-app workflow), i.e. Dino8.exe plus data\commands.json.

#define MyAppName "Dino 8"
#define MyAppVersion "0.1.0"
#define MyAppPublisher "Dino 8 Project"
#define MyAppExeName "Dino8.exe"
#define MyInstallTree "..\..\build\install"

[Setup]
AppId={{7D1E4C0A-5B2F-4B7E-9C3D-2A6F8E1B9D40}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL=https://github.com/mlegere9789-collab/miniature-pancake
AppSupportURL=https://github.com/mlegere9789-collab/miniature-pancake/issues
AppUpdatesURL=https://github.com/mlegere9789-collab/miniature-pancake/releases
DefaultDirName={autopf}\Dino8
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
SetupIconFile=..\..\resources\dino8.ico
UninstallDisplayIcon={app}\{#MyAppExeName}
OutputDir=..\..\dist
OutputBaseFilename=Dino8Setup-{#MyAppVersion}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
LicenseFile=..\..\..\LICENSE

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; GroupDescription: "Additional shortcuts:"
Name: "assoc3dm"; Description: "Open .3dm files with Dino 8"; GroupDescription: "File associations:"

[Files]
Source: "{#MyInstallTree}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\Uninstall {#MyAppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Registry]
Root: HKA; Subkey: "Software\Classes\.3dm\OpenWithProgids"; ValueType: string; ValueName: "Dino8.3dm"; ValueData: ""; Flags: uninsdeletevalue; Tasks: assoc3dm
Root: HKA; Subkey: "Software\Classes\Dino8.3dm"; ValueType: string; ValueName: ""; ValueData: "Rhino/Dino 3D Model"; Flags: uninsdeletekey; Tasks: assoc3dm
Root: HKA; Subkey: "Software\Classes\Dino8.3dm\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\{#MyAppExeName},0"; Tasks: assoc3dm
Root: HKA; Subkey: "Software\Classes\Dino8.3dm\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#MyAppExeName}"" ""%1"""; Tasks: assoc3dm

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "Launch {#MyAppName}"; Flags: nowait postinstall skipifsilent
