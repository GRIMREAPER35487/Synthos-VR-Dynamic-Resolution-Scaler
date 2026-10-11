[Setup]
AppId={{8F5B8F9A-4C2E-4D1E-8B3F-9A0B1C2D3E4F}}
AppName=Syn Dynamic Res Scale
AppVersion=1.0.0
AppPublisher=Syn Software
DefaultDirName={autopf}\Syn Dynamic Res Scale
DefaultGroupName=Syn Dynamic Res Scale
UninstallDisplayIcon={app}\Syn_Dynamic_Res_Scale.exe
Compression=lzma2/ultra64
SolidCompression=yes
OutputDir=..\installer_output
OutputBaseFilename=Syn_Dynamic_Res_Scale_Setup
CloseApplications=force
CloseApplicationsFilter=*Syn_Dynamic_Res_Scale*
UsePreviousAppDir=yes

[Files]
Source: "..\build\Release\Release\Syn_Dynamic_Res_Scale.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\build\_deps\openvr-src\bin\win64\openvr_api.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\manifest.vrmanifest"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\settings.ini"; DestDir: "{app}"; Flags: onlyifdoesntexist

[Icons]
Name: "{autoprograms}\Syn Dynamic Res Scale"; Filename: "{app}\Syn_Dynamic_Res_Scale.exe"
Name: "{autodesktop}\Syn Dynamic Res Scale"; Filename: "{app}\Syn_Dynamic_Res_Scale.exe"

[Run]
Filename: "{app}\Syn_Dynamic_Res_Scale.exe"; Description: "Launch Syn Dynamic Res Scale"; Flags: postinstall nowait skipifsilent

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

procedure RegisterSteamVR();
var
  AppConfigPath, ManifestPath, EscapedPath: string;
  Lines: TArrayOfString;
  I, BracketPos: Integer;
  Found: Boolean;
begin
  AppConfigPath := GetSteamAppConfigPath();
  ManifestPath := ExpandConstant('{app}\manifest.vrmanifest');
  EscapedPath := ManifestPath;
  StringChange(EscapedPath, '\', '\\');

  if FileExists(AppConfigPath) then
  begin
    if LoadStringsFromFile(AppConfigPath, Lines) then
    begin
      Found := False;
      for I := 0 to GetArrayLength(Lines) - 1 do
      begin
        if Pos(EscapedPath, Lines[I]) > 0 then
        begin
          Found := True;
          Break;
        end;
      end;
      
      if not Found then
      begin
        for I := 0 to GetArrayLength(Lines) - 1 do
        begin
          BracketPos := Pos(']', Lines[I]);
          if BracketPos > 0 then
          begin
            Lines[I] := '     ,"' + EscapedPath + '"' + #13#10 + '  ]';
            SaveStringsToFile(AppConfigPath, Lines, False);
            Break;
          end;
        end;
      end;
    end;
  end;
end;

procedure UnregisterSteamVR();
var
  AppConfigPath, ManifestPath, EscapedPath: string;
  Lines: TArrayOfString;
  I: Integer;
begin
  AppConfigPath := GetSteamAppConfigPath();
  ManifestPath := ExpandConstant('{app}\manifest.vrmanifest');
  EscapedPath := ManifestPath;
  StringChange(EscapedPath, '\', '\\');

  if FileExists(AppConfigPath) then
  begin
    if LoadStringsFromFile(AppConfigPath, Lines) then
    begin
      for I := 0 to GetArrayLength(Lines) - 1 do
      begin
        if Pos(EscapedPath, Lines[I]) > 0 then
        begin
          Lines[I] := '';
        end;
      end;
      SaveStringsToFile(AppConfigPath, Lines, False);
    end;
  end;
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
