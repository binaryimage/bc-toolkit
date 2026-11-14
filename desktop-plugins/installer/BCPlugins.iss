; binaryimage MPC plugins for the PC: 9 VST3s in one installer.
; Built by CI:  ISCC /DStage=<staging folder> /DAppVersion=1.0.0 /O<out> BCPlugins.iss
; The staging folder has VST3\<Name>.vst3 bundles and README.txt.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef Stage
  #define Stage "stage"
#endif

[Setup]
AppId={{4E6B1D3A-8F27-4C59-A0D2-7B91E5C3F604}
AppName=binaryimage Plugins
AppVersion={#AppVersion}
AppVerName=binaryimage Plugins {#AppVersion}
AppPublisher=binaryimage
DefaultDirName={commoncf64}\VST3\binaryimage
DisableDirPage=yes
DisableProgramGroupPage=yes
OutputBaseFilename=binaryimage-Plugins-Setup-{#AppVersion}
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
WizardStyle=modern
Compression=lzma2
SolidCompression=yes
UninstallDisplayName=binaryimage Plugins (Janus, Maggot, Legion and 6 effects)
InfoBeforeFile={#Stage}\README.txt

[Types]
Name: "full"; Description: "All 9 plugins"
Name: "custom"; Description: "Pick which"; Flags: iscustom

[Components]
Name: "janus";     Description: "Janus (synth)";     Types: full
Name: "maggot";    Description: "Maggot (synth)";    Types: full
Name: "legion";    Description: "Legion (choir synth)"; Types: full
Name: "grendel";   Description: "Grendel (effect)";  Types: full
Name: "ouroboros"; Description: "Ouroboros (effect)"; Types: full
Name: "fenrir";    Description: "Fenrir (effect)";   Types: full
Name: "moloch";    Description: "Moloch (effect)";   Types: full
Name: "mare";      Description: "Mare (effect)";     Types: full
Name: "banshee";   Description: "Banshee (effect)";  Types: full

[Files]
Source: "{#Stage}\VST3\Janus.vst3\*";     DestDir: "{app}\Janus.vst3";     Components: janus;     Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#Stage}\VST3\Maggot.vst3\*";    DestDir: "{app}\Maggot.vst3";    Components: maggot;    Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#Stage}\VST3\Legion.vst3\*";    DestDir: "{app}\Legion.vst3";    Components: legion;    Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#Stage}\VST3\Grendel.vst3\*";   DestDir: "{app}\Grendel.vst3";   Components: grendel;   Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#Stage}\VST3\Ouroboros.vst3\*"; DestDir: "{app}\Ouroboros.vst3"; Components: ouroboros; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#Stage}\VST3\Fenrir.vst3\*";    DestDir: "{app}\Fenrir.vst3";    Components: fenrir;    Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#Stage}\VST3\Moloch.vst3\*";    DestDir: "{app}\Moloch.vst3";    Components: moloch;    Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#Stage}\VST3\Mare.vst3\*";      DestDir: "{app}\Mare.vst3";      Components: mare;      Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#Stage}\VST3\Banshee.vst3\*";   DestDir: "{app}\Banshee.vst3";   Components: banshee;   Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#Stage}\README.txt"; DestDir: "{app}"; Flags: ignoreversion
