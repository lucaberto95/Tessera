; Inno Setup script for Tessera. Build with: deploy.cmd installer (needs Inno Setup 6, ISCC.exe).
; Per-user install (no administrator rights), .tsra association written under HKCU.
#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif

[Setup]
AppId={{6E2B1D54-7A3C-4C1B-9D0F-5A7E3F2C8B91}
AppName=Tessera
AppVersion={#AppVersion}
AppPublisher=Tessera
DefaultDirName={autopf}\Tessera
DefaultGroupName=Tessera
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
ChangesAssociations=yes
DisableProgramGroupPage=yes
UninstallDisplayIcon={app}\Tessera.exe
UninstallDisplayName=Tessera
OutputDir=..\dist
OutputBaseFilename=Tessera-{#AppVersion}-setup
SetupIconFile=tessera.ico
Compression=lzma2
SolidCompression=yes
WizardStyle=modern

[Languages]
Name: "italian"; MessagesFile: "compiler:Languages\Italian.isl"

[Tasks]
Name: "desktopicon"; Description: "Crea un collegamento sul desktop"; Flags: unchecked

[Files]
Source: "..\dist\Tessera\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

[Icons]
Name: "{autoprograms}\Tessera"; Filename: "{app}\Tessera.exe"
Name: "{autodesktop}\Tessera"; Filename: "{app}\Tessera.exe"; Tasks: desktopicon

[Registry]
; Root HKA = current user's hive for a per-user install (all-users only if chosen in the dialog).
Root: HKA; Subkey: "Software\Classes\.tsra"; ValueType: string; ValueData: "Tessera.Project"; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\Tessera.Project"; ValueType: string; ValueData: "Progetto Tessera"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\Tessera.Project\DefaultIcon"; ValueType: string; ValueData: "{app}\Tessera.exe,1"
Root: HKA; Subkey: "Software\Classes\Tessera.Project\shell\open\command"; ValueType: string; ValueData: """{app}\Tessera.exe"" ""%1"""

[Run]
Filename: "{app}\Tessera.exe"; Description: "Avvia Tessera"; Flags: nowait postinstall skipifsilent

; Projects (.tsra files) live outside {app} and are never touched by the uninstaller.