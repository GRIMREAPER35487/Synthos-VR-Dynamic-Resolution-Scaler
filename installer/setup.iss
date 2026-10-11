[Setup]
AppId={{8F5B8F9A-4C2E-4D1E-8B3F-9A0B1C2D3E4F}}
AppName=Synthos VR Dynamic Resolution Scaler
AppVersion=1.0.0
AppPublisher=Synthos
DefaultDirName={autopf}\Synthos VR Dynamic Resolution Scaler
DefaultGroupName=Synthos VR Dynamic Resolution Scaler
UninstallDisplayIcon={app}\Syn_Dynamic_Res_Scale.exe
Compression=lzma2/ultra64
SolidCompression=yes
OutputDir=..\installer_output
OutputBaseFilename=Synthos_VR_Dynamic_Resolution_Scaler_Setup
CloseApplications=force
CloseApplicationsFilter=*Syn_Dynamic_Res_Scale*
UsePreviousAppDir=yes

[Files]
Source: "..\build\Release\Release\Syn_Dynamic_Res_Scale.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\build\_deps\openvr-src\bin\win64\openvr_api.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\manifest.vrmanifest"; DestDir: "{app}"; Flags: ignoreversion

[InstallDelete]
; Shortcuts from before the rename to "Synthos VR Dynamic Resolution Scaler"
Type: files; Name: "{autoprograms}\Syn Dynamic Res Scale.lnk"
Type: files; Name: "{autodesktop}\Syn Dynamic Res Scale.lnk"

[Icons]
Name: "{autoprograms}\Synthos VR Dynamic Resolution Scaler"; Filename: "{app}\Syn_Dynamic_Res_Scale.exe"
Name: "{autodesktop}\Synthos VR Dynamic Resolution Scaler"; Filename: "{app}\Syn_Dynamic_Res_Scale.exe"

[Run]
Filename: "{app}\Syn_Dynamic_Res_Scale.exe"; Description: "Launch Synthos VR Dynamic Resolution Scaler"; Flags: postinstall nowait skipifsilent

[Code]
function GetSteamAppConfigPath(): string;
var
  SteamPath: string;
begin
  Result := 'C:\Program Files (x86)\Steam\config\appconfig.json';
  if RegQueryStringValue(HKEY_CURRENT_USER, 'Software\Valve\Steam', 'SteamPath', SteamPath) then
  begin
    Result := SteamPath + '\config\appconfig.json';
  end
  else if RegQueryStringValue(HKEY_LOCAL_MACHINE, 'SOFTWARE\WOW6432Node\Valve\Steam', 'InstallPath', SteamPath) then
  begin
    Result := SteamPath + '\config\appconfig.json';
  end;
end;

#include "steamvr_appconfig.iss"

function GetManifestEntry(): string;
var
  EscapedPath: string;
begin
  EscapedPath := ExpandConstant('{app}\manifest.vrmanifest');
  StringChange(EscapedPath, '\', '\\');
  Result := '"' + EscapedPath + '"';
end;

procedure RegisterSteamVR();
begin
  AddManifestPath(GetSteamAppConfigPath(), GetManifestEntry());
end;

procedure UnregisterSteamVR();
begin
  RemoveManifestPath(GetSteamAppConfigPath(), GetManifestEntry());
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
  begin
    RegisterSteamVR();
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usPostUninstall then
  begin
    UnregisterSteamVR();
  end;
end;
