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

function GameRunning: Boolean;
begin
  Result := ProcessRunning('D2R.exe') or ProcessRunning('D2RLoader.exe');
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
function CloseGracefully(const Exe, Args, ProcessName: String; TimeoutMs: Integer; Status: TNewStaticText): Boolean;
var
  Code: Integer;
begin
  Result := not ProcessRunning(ProcessName);
  if Result then Exit;
  if (Exe = '') or not FileExists(Exe) then begin
    ShowCloseStatus(Status, 'Setup could not find steam.exe to ask Steam to close. Choose Steam > Exit yourself.');
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

function CloseSteam(const FallbackFolder: String; Status: TNewStaticText): Boolean;
begin
  Result := CloseGracefully(SteamShutdownExe(FallbackFolder), '-shutdown', SteamProcess, SteamExitTimeoutMs, Status);
end;
