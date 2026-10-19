// Included by D2R_PCVR_Setup.iss inside [Code], and by installer/close_steam_test.py's
// fixture installer, which runs these functions against a stand-in process.
//
// Setup edits Steam's localconfig.vdf and shortcuts.vdf, and Steam rewrites both when it
// exits, so it must be closed first. Setup can ask Steam to close itself with its own
// `steam.exe -shutdown` (the same as Steam > Exit; SteamVR closes with it) and waits for it.
// Steam is never killed: if it does not exit in time the player is asked to close it.
// D2R / D2RLoader are never closed by Setup: the player must Save & Exit (progress).

const
  SteamProcess = 'steam.exe';
  SteamExitTimeoutMs = 30000;

function ProcessRunning(const Name: String): Boolean;
var
  Locator, Service, Items: Variant;
begin
  Result := False;
  try
    Locator := CreateOleObject('WbemScripting.SWbemLocator');
    Service := Locator.ConnectServer('.', 'root\CIMV2');
    Items := Service.ExecQuery('SELECT ProcessId FROM Win32_Process WHERE Name = ''' + Name + '''');
    Result := Items.Count > 0;
  except
    // Unknown: the backend's own process check still stops before any change.
    Log('Could not list running processes: ' + GetExceptionMessage);
  end;
end;

// True when any of the comma-separated process names is running.
function AnyRunning(Names: String): Boolean;
var
  Comma: Integer;
begin
  Result := False;
  while (Names <> '') and not Result do begin
    Comma := Pos(',', Names);
    if Comma = 0 then Comma := Length(Names) + 1;
    if Trim(Copy(Names, 1, Comma - 1)) <> '' then
      Result := ProcessRunning(Trim(Copy(Names, 1, Comma - 1)));
    Delete(Names, 1, Comma);
  end;
end;

const
  GameProcesses = 'D2R.exe,D2RLoader.exe';

function GameRunning: Boolean;
begin
  Result := AnyRunning(GameProcesses);
end;

function SteamRunning: Boolean;
begin
  Result := ProcessRunning(SteamProcess);
end;

// Steam's own folder as Steam records it (forward slashes, any case) -> its steam.exe.
function SteamExeInFolder(Folder: String): String;
begin
  StringChangeEx(Folder, '/', '\', True);
  Result := RemoveBackslashUnlessRoot(Folder) + '\steam.exe';
end;

// HKCU\Software\Valve\Steam SteamPath, else the Steam folder the wizard found.
function SteamShutdownExe(const FallbackFolder: String): String;
var
  Folder: String;
begin
  Result := '';
  if RegQueryStringValue(HKEY_CURRENT_USER, 'Software\Valve\Steam', 'SteamPath', Folder) and
      FileExists(SteamExeInFolder(Folder)) then
    Result := SteamExeInFolder(Folder)
  else if (FallbackFolder <> '') and FileExists(SteamExeInFolder(FallbackFolder)) then
    Result := SteamExeInFolder(FallbackFolder);
end;

procedure ShowCloseStatus(Status: TNewStaticText; const Text: String);
begin
  Log(Text);
  if Status <> nil then begin
    Status.Caption := Text;
    Status.Update;
  end;
end;

// Waits up to TimeoutMs for every ProcessName to be gone. True when it is.
function WaitForProcessExit(const ProcessName: String; TimeoutMs: Integer; Status: TNewStaticText): Boolean;
var
  Waited: Integer;
begin
  Waited := 0;
  Result := not ProcessRunning(ProcessName);
  while not Result and (Waited < TimeoutMs) do begin
    ShowCloseStatus(Status, Format('Waiting for Steam to close... %d of %d seconds', [Waited div 1000, TimeoutMs div 1000]));
    Sleep(500);
    Waited := Waited + 500;
    Result := not ProcessRunning(ProcessName);
  end;
end;

// Asks the running program to close itself (Exe Args, e.g. steam.exe -shutdown) and waits.
// True when ProcessName is no longer running. Never terminates anything.
// BlockingProcesses (the game) are checked again immediately before the request: the page
// may have been drawn before the game started. While one runs nothing is requested and
// Blocked is True.
function CloseGracefully(const BlockingProcesses, Exe, Args, ProcessName: String; TimeoutMs: Integer;
  Status: TNewStaticText; var Blocked: Boolean): Boolean;
var
  Code: Integer;
begin
  Blocked := False;
  Result := not ProcessRunning(ProcessName);
  if Result then Exit;
  if (Exe = '') or not FileExists(Exe) then begin
    ShowCloseStatus(Status, 'Setup could not find steam.exe to ask Steam to close. Choose Steam > Exit yourself.');
    Exit;
  end;
  Blocked := AnyRunning(BlockingProcesses);
  if Blocked then begin
    ShowCloseStatus(Status, 'Diablo II: Resurrected is running. Save & Exit the game first; Setup did not ask Steam to close.');
    Exit;
  end;
  ShowCloseStatus(Status, 'Asking Steam to close (' + Exe + ' ' + Args + ')...');
  if not Exec(Exe, Args, ExtractFileDir(Exe), SW_SHOWNORMAL, ewNoWait, Code) then begin
    ShowCloseStatus(Status, 'Setup could not ask Steam to close (' + SysErrorMessage(Code) + '). Choose Steam > Exit yourself.');
    Exit;
  end;
  Result := WaitForProcessExit(ProcessName, TimeoutMs, Status);
  if Result then
    ShowCloseStatus(Status, 'Steam has closed. Click Next to continue.')
  else
    ShowCloseStatus(Status, 'Steam is still running. Choose Steam > Exit on the PC (do not just close its window), then click Next.');
end;

// GameStarted: the game was running at the moment of the request, so Steam was not asked.
function CloseSteam(const FallbackFolder: String; Status: TNewStaticText; var GameStarted: Boolean): Boolean;
begin
  Result := CloseGracefully(GameProcesses, SteamShutdownExe(FallbackFolder), '-shutdown', SteamProcess,
    SteamExitTimeoutMs, Status, GameStarted);
end;
