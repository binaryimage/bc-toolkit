; BC Visualizer: audio-reactive music videos for YouTube.
; Built by CI:  ISCC /DStage=<staging folder> /DAppVersion=1.0.0 /O<out> Visualizer.iss

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef Stage
  #define Stage "stage"
#endif

[Setup]
AppId={{8C1F4E27-93B5-4D0A-A6E2-5F7B19D3C4A8}
AppName=BC Visualizer
AppVersion={#AppVersion}
AppVerName=BC Visualizer {#AppVersion}
AppPublisher=BC
DefaultDirName={autopf}\BC Visualizer
DefaultGroupName=BC Visualizer
DisableProgramGroupPage=yes
OutputBaseFilename=BC-Visualizer-Setup-{#AppVersion}
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
WizardStyle=modern
Compression=lzma2
SolidCompression=yes
CloseApplications=yes
UninstallDisplayIcon={app}\BC Visualizer.exe
UninstallDisplayName=BC Visualizer

[Files]
Source: "{#Stage}\BC Visualizer.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Stage}\Visualizer README.txt"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\BC Visualizer"; Filename: "{app}\BC Visualizer.exe"
Name: "{autodesktop}\BC Visualizer"; Filename: "{app}\BC Visualizer.exe"; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; Flags: unchecked

[Run]
Filename: "{app}\BC Visualizer.exe"; Description: "Open BC Visualizer"; Flags: nowait postinstall skipifsilent
