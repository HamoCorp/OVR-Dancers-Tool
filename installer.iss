; OVR Dancers Tool Installer
; Compile with Inno Setup: right-click > Compile (or iscc installer.iss)

#define MyAppName "OVR Dancers Tool"
#define MyAppVersion "1.0.0"
#define MyAppPublisher "HamoCorp"
#define MyAppURL "https://github.com/HamoCorp/OVR-Dancers-Tool"

[Setup]
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
UninstallDisplayIcon={app}\OVRDancersTool.exe
OutputDir=.
OutputBaseFilename=OVR-Dancers-Tool-{#MyAppVersion}-Setup
PrivilegesRequired=admin
Compression=lzma2
SolidCompression=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
Source: "build\overlay\Release\OVRDancersTool.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "build\overlay\Release\openvr_api.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "build\overlay\Release\manifest.vrmanifest"; DestDir: "{app}"; Flags: ignoreversion
Source: "build\overlay\Release\resources\*"; DestDir: "{app}\resources"; Flags: ignoreversion recursesubdirs
Source: "build\driver\driver.vrdrivermanifest"; DestDir: "{code:GetSteamVRDrivers}\00ovrdancers"; Flags: ignoreversion
Source: "build\driver\bin\win64\driver_ovrdancers.dll"; DestDir: "{code:GetSteamVRDrivers}\00ovrdancers\bin\win64"; Flags: ignoreversion

[Icons]
Name: "{group}\OVR Dancers Tool"; Filename: "{app}\OVRDancersTool.exe"; WorkingDir: "{app}"
Name: "{group}\Uninstall OVR Dancers Tool"; Filename: "{uninstallexe}"

[Code]
function GetSteamVRDrivers(Param: string): string;
var
  SteamPath: string;
begin
  if RegQueryStringValue(HKCU, 'Software\Valve\Steam', 'SteamPath', SteamPath) then
    Result := SteamPath + '\steamapps\common\SteamVR\drivers'
  else
    Result := 'C:\Program Files (x86)\Steam\steamapps\common\SteamVR\drivers';
  if not DirExists(Result) then
    Result := 'C:\Program Files (x86)\Steam\steamapps\common\SteamVR\drivers';
end;
