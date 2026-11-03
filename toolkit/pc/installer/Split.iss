; BC Split on its own: the ASIO sharing driver (64- and 32-bit) and the Split Host.
; Built by CI:  ISCC /DStage=<staging folder> /DAppVersion=1.0.0 /O<out> Split.iss

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef Stage
  #define Stage "stage"
#endif

[Setup]
AppId={{3E9D7B21-6C4A-4F85-B0D2-91A7C5E3F648}
AppName=BC Split
AppVersion={#AppVersion}
AppVerName=BC Split {#AppVersion}
AppPublisher=BC
DefaultDirName={autopf}\BC Split
DefaultGroupName=BC Split
DisableProgramGroupPage=yes
OutputBaseFilename=BC-Split-Setup-{#AppVersion}
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
WizardStyle=modern
Compression=lzma2
SolidCompression=yes
CloseApplications=yes
UninstallDisplayIcon={app}\BC Split Host.exe
UninstallDisplayName=BC Split

[Files]
Source: "{#Stage}\Split\BC Split Host.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Stage}\Split\BC Split x64.dll"; DestDir: "{app}"; Flags: ignoreversion restartreplace uninsrestartdelete regserver 64bit
Source: "{#Stage}\Split\BC Split x86.dll"; DestDir: "{app}"; Flags: ignoreversion restartreplace uninsrestartdelete regserver 32bit
Source: "{#Stage}\Split README.txt"; DestDir: "{app}"; Flags: ignoreversion isreadme

[Icons]
Name: "{group}\BC Split"; Filename: "{app}\BC Split Host.exe"; Parameters: "--show"
Name: "{group}\Split README"; Filename: "{app}\Split README.txt"

[Run]
Filename: "{app}\BC Split Host.exe"; Parameters: "--show"; Description: "Open BC Split to pick your interface"; Flags: nowait postinstall skipifsilent

[UninstallRun]
Filename: "{sys}\taskkill.exe"; Parameters: "/f /im ""BC Split Host.exe"""; Flags: runhidden; RunOnceId: "StopSplit"

[Code]
// a running Split host would lock its own files: stop it first (apps using Split will be asked to reset)
function PrepareToInstall (var NeedsRestart: Boolean): String;
var
  Code: Integer;
begin
  Exec (ExpandConstant ('{sys}\taskkill.exe'), '/f /im "BC Split Host.exe"', '', SW_HIDE, ewWaitUntilTerminated, Code);
  Result := '';
end;
