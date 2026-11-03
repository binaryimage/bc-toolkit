; BC Toolkit installer: the app and the BC Link VST3. BC Split has its own installer.
; Built by CI:  ISCC /DStage=<staging folder> /DAppVersion=1.1.0 /O<out> Toolkit.iss

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef Stage
  #define Stage "stage"
#endif

[Setup]
AppId={{8C1E5A3B-2F4D-4B7E-9A61-3D5C7E9F1A20}
AppName=BC Toolkit
AppVersion={#AppVersion}
AppVerName=BC Toolkit {#AppVersion}
AppPublisher=BC
DefaultDirName={autopf}\BC Toolkit
DefaultGroupName=BC Toolkit
DisableProgramGroupPage=yes
UsePreviousAppDir=no
UsePreviousGroup=no
OutputBaseFilename=BC-Toolkit-Setup-{#AppVersion}
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
WizardStyle=modern
Compression=lzma2
SolidCompression=yes
CloseApplications=yes
UninstallDisplayIcon={app}\BC Toolkit.exe
UninstallDisplayName=BC Toolkit

[Tasks]
Name: "vst3"; Description: "BC Link VST3 for Ableton (streams tracks to Toolkit)"
Name: "desktopicon"; Description: "Desktop shortcut"; Flags: unchecked

[Files]
Source: "{#Stage}\BC Toolkit.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Stage}\Toolkit Tutorial.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Stage}\INSTALL.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Stage}\BC Link.vst3\*"; DestDir: "{commoncf64}\VST3\BC Link.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs; Tasks: vst3

[Icons]
Name: "{group}\BC Toolkit"; Filename: "{app}\BC Toolkit.exe"
Name: "{group}\Toolkit Tutorial"; Filename: "{app}\Toolkit Tutorial.md"
Name: "{autodesktop}\BC Toolkit"; Filename: "{app}\BC Toolkit.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\BC Toolkit.exe"; Description: "Open BC Toolkit"; Flags: nowait postinstall skipifsilent

[InstallDelete]
; version 1.1 was called binaryimage Toolkit: remove its plug-in and program folder (settings are kept)
Type: filesandordirs; Name: "{commoncf64}\VST3\Toolkit Link.vst3"
Type: filesandordirs; Name: "{autopf}\binaryimage Toolkit"

[Code]
// 1.1 installed Split inside the old binaryimage Toolkit folder. Split has its own installer now (BC Split):
// unregister and remove that old copy before its folder is deleted.
procedure RemoveOldSplit;
var
  Code: Integer;
  Dir: String;
begin
  Dir := ExpandConstant ('{autopf}\binaryimage Toolkit\Split');
  if not DirExists (Dir) then Exit;
  Exec (ExpandConstant ('{sys}\taskkill.exe'), '/f /im "binaryimage Split Host.exe"', '', SW_HIDE, ewWaitUntilTerminated, Code);
  if FileExists (Dir + '\binaryimage Split x64.dll') then
    Exec (ExpandConstant ('{sys}\regsvr32.exe'), '/u /s "' + Dir + '\binaryimage Split x64.dll"', '', SW_HIDE, ewWaitUntilTerminated, Code);
  if FileExists (Dir + '\binaryimage Split x86.dll') then
    Exec (ExpandConstant ('{syswow64}\regsvr32.exe'), '/u /s "' + Dir + '\binaryimage Split x86.dll"', '', SW_HIDE, ewWaitUntilTerminated, Code);
  DelTree (Dir, True, True, True);
end;

// runs before anything is copied or deleted, so the old drivers can still be unregistered
function PrepareToInstall (var NeedsRestart: Boolean): String;
begin
  RemoveOldSplit;
  Result := '';
end;
