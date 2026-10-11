// Test harness for steamvr_appconfig.iss. Run via run_appconfig_tests.py, which writes
// the fixture files, compiles this script, runs it, and validates the results.
// The setup exits immediately from InitializeSetup; nothing is installed.

[Setup]
AppName=AppConfig Test
AppVersion=0
CreateAppDir=no
PrivilegesRequired=lowest
Uninstallable=no
OutputBaseFilename=test_appconfig

[Code]
#include "..\steamvr_appconfig.iss"

function InitializeSetup(): Boolean;
var
  Dir, Entry: string;
begin
  Dir := ExpandConstant('{param:fixtures}');
  Entry := '"C:\\Test Dir\\manifest.vrmanifest"';

  AddManifestPath(Dir + '\add_empty.json', Entry);
  AddManifestPath(Dir + '\add_multiline.json', Entry);
  AddManifestPath(Dir + '\add_inline.json', Entry);
  AddManifestPath(Dir + '\add_bracket_in_path.json', Entry);
  AddManifestPath(Dir + '\add_already_present.json', Entry);
  AddManifestPath(Dir + '\add_no_key.json', Entry);

  RemoveManifestPath(Dir + '\remove_last.json', Entry);
  RemoveManifestPath(Dir + '\remove_first.json', Entry);
  RemoveManifestPath(Dir + '\remove_middle.json', Entry);
  RemoveManifestPath(Dir + '\remove_only.json', Entry);

  AddManifestPath(Dir + '\roundtrip.json', Entry);
  RemoveManifestPath(Dir + '\roundtrip.json', Entry);

  Result := False;
end;
