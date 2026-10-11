// Safe editing of the "manifest_paths" array in SteamVR's appconfig.json.
// Included by setup.iss (and by tests\test_appconfig.iss).

function IsWhitespace(C: Char): Boolean;
begin
  Result := (C = ' ') or (C = #9) or (C = #13) or (C = #10);
end;

// Returns the position of the ']' that closes the JSON array opened at OpenPos,
// skipping over any brackets that appear inside quoted strings. 0 if not found.
function FindArrayClose(const S: string; OpenPos: Integer): Integer;
var
  I: Integer;
  InString: Boolean;
begin
  Result := 0;
  InString := False;
  I := OpenPos + 1;
  while I <= Length(S) do
  begin
    if InString then
    begin
      if S[I] = '\' then
        I := I + 1
      else if S[I] = '"' then
        InString := False;
    end
    else if S[I] = '"' then
      InString := True
    else if S[I] = ']' then
    begin
      Result := I;
      Exit;
    end;
    I := I + 1;
  end;
end;

// Adds Entry (a quoted, JSON-escaped path) to the "manifest_paths" array.
// Leaves the file untouched if the entry is already present or the array can't be found.
procedure AddManifestPath(const AppConfigPath, Entry: string);
var
  S: string;
  Content: AnsiString;
  KeyPos, OpenPos, ClosePos, LastPos: Integer;
begin
  if not LoadStringFromFile(AppConfigPath, Content) then Exit;
  S := Content;
  if Pos(Entry, S) > 0 then Exit;

  KeyPos := Pos('"manifest_paths"', S);
  if KeyPos = 0 then Exit;
  OpenPos := Pos('[', Copy(S, KeyPos, Length(S)));
  if OpenPos = 0 then Exit;
  OpenPos := OpenPos + KeyPos - 1;
  ClosePos := FindArrayClose(S, OpenPos);
  if ClosePos = 0 then Exit;

  // Find the last non-whitespace character inside the array
  LastPos := ClosePos - 1;
  while (LastPos > OpenPos) and IsWhitespace(S[LastPos]) do
    LastPos := LastPos - 1;

  if LastPos = OpenPos then
  begin
    // Empty array: replace its contents with the single entry
    Delete(S, OpenPos + 1, ClosePos - OpenPos - 1);
    Insert(#13#10 + '    ' + Entry + #13#10 + '  ', S, OpenPos + 1);
  end
  else
    // Non-empty array: append after the last element
    Insert(',' + #13#10 + '    ' + Entry, S, LastPos + 1);

  FileCopy(AppConfigPath, AppConfigPath + '.syn_backup', False);
  SaveStringToFile(AppConfigPath, S, False);
end;

// Removes Entry from the "manifest_paths" array along with its separating comma, keeping the JSON valid.
procedure RemoveManifestPath(const AppConfigPath, Entry: string);
var
  S: string;
  Content: AnsiString;
  EntryPos, StartPos, EndPos, I: Integer;
begin
  if not LoadStringFromFile(AppConfigPath, Content) then Exit;
  S := Content;
  EntryPos := Pos(Entry, S);
  if EntryPos = 0 then Exit;

  StartPos := EntryPos;
  EndPos := EntryPos + Length(Entry); // exclusive

  // Prefer removing the comma before the entry; if it is the first element, remove the one after it.
  I := EntryPos - 1;
  while (I > 0) and IsWhitespace(S[I]) do
    I := I - 1;
  if (I > 0) and (S[I] = ',') then
    StartPos := I
  else
  begin
    I := EndPos;
    while (I <= Length(S)) and IsWhitespace(S[I]) do
      I := I + 1;
    if (I <= Length(S)) and (S[I] = ',') then
    begin
      I := I + 1;
      while (I <= Length(S)) and IsWhitespace(S[I]) do
        I := I + 1;
      EndPos := I;
    end;
  end;

  Delete(S, StartPos, EndPos - StartPos);
  FileCopy(AppConfigPath, AppConfigPath + '.syn_backup', False);
  SaveStringToFile(AppConfigPath, S, False);
end;
