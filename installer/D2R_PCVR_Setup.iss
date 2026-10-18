; Native OpenXR installer. Build with build_native_installer.ps1.
#ifndef PayloadDir
  #error PayloadDir must point to a verified native Windows package with its private Python runtime.
#endif
#ifndef ModVersion
  #define ModVersion "0.3.19"
#endif
#ifndef InstallerOutput
  #define InstallerOutput "Output"
#endif

[Setup]
AppId=D2R-PCVR-Native-Windows
; Players see "D2R VR Unleashed"; the ids stay D2R PCVR's (AppId, the program folder, the D2RLoader mod D2R-PCVR).
AppName=D2R VR Unleashed
AppVersion={#ModVersion}
AppPublisher=D2R PCVR contributors
AppPublisherURL=https://github.com/bearstonem/d2r-pcvr
AppSupportURL=https://github.com/bearstonem/d2r-pcvr/issues
DefaultDirName={localappdata}\Programs\D2R PCVR
DefaultGroupName=D2R VR Unleashed
DisableDirPage=yes
DisableProgramGroupPage=yes
DisableWelcomePage=no
DisableFinishedPage=no
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
LicenseFile={#PayloadDir}\LICENSE
OutputDir={#InstallerOutput}
OutputBaseFilename=D2R-PCVR-Setup-{#ModVersion}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
CloseApplications=no
RestartApplications=no
UninstallDisplayName=D2R VR Unleashed (Windows VR mod)
UninstallDisplayIcon={app}\.runtime\python\python.exe
; The setup program's own file properties: D2R VR Unleashed (D2R PCVR), not D2R VR.
VersionInfoVersion={#ModVersion}
VersionInfoProductName=D2R VR Unleashed
VersionInfoProductVersion={#ModVersion}
VersionInfoCompany=D2R PCVR contributors
VersionInfoDescription=D2R VR Unleashed setup (Diablo II reimagined for physical, immersive VR; D2R PCVR, based on D2R VR by BodyWalkVR, MIT)
VersionInfoCopyright=MIT licence. D2R PCVR contributors; D2R VR (c) 2026 BodyWalkVR

[Files]
Source: "{#PayloadDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\Installation help"; Filename: "{sys}\notepad.exe"; Parameters: """{app}\INSTALL-WINDOWS.md"""
Name: "{group}\Controller mappings"; Filename: "{sys}\notepad.exe"; Parameters: """{app}\CONTROLS.md"""
Name: "{group}\Uninstall D2R VR Unleashed"; Filename: "{uninstallexe}"

[Run]
Filename: "{code:SteamExecutable}"; Description: "Open Steam to use your new VR shortcuts"; Flags: nowait postinstall skipifsilent; Check: SetupSucceeded

[Code]
var
  EditionPage, HeadsetPage, ConnectionPage, AccountPage, ModePage: TInputOptionWizardPage;
  PathsPage, RuntimePage, BackupPage: TInputDirWizardPage;
  HDRPage: TOutputMsgWizardPage;
  CloseSteamPage: TWizardPage;
  CloseSteamText, CloseSteamStatus: TNewStaticText;
  CloseSteamButton: TNewButton;
  ProfileIDs: TStringList;
  PayloadRoot, ProbeFile, ChoicesFile, LastOutput, PreviousEdition, BackupGame: String;
  SetupFailed, BackendBusy: Boolean;

#include "close_steam.inc"

// A bare command-line switch such as /CLOSESTEAM.
function SwitchGiven(const Name: String): Boolean;
var
  I: Integer;
begin
  Result := False;
  for I := 1 to ParamCount do
    if CompareText(ParamStr(I), Name) = 0 then Result := True;
end;

function Q(const Value: String): String;
begin
  Result := '"' + Value + '"';
end;

function IsSecurityBlock: Boolean;
begin
  Result := Pos('[PCVR_SECURITY_BLOCK]', LastOutput) > 0;
end;

function SecurityBlockMessage: String;
begin
  Result := 'Windows Security interrupted setup. To finish installing:'#13#10#13#10 +
    '1. Open Windows Security > Virus & threat protection > Manage settings.'#13#10#13#10 +
    '2. Temporarily turn Real-time protection Off.'#13#10#13#10 +
    '3. Run this installer again and choose Update / repair.'#13#10#13#10 +
    '4. Turn Real-time protection back On immediately afterward, even if setup does not complete.'#13#10#13#10 +
    'Your saves and settings have been kept.';
end;

procedure BackendOutput(const S: String; const Error, FirstLine: Boolean);
begin
  Log(S);
  if Trim(S) <> '' then LastOutput := S;
  if BackendBusy then begin
    if IsSecurityBlock then
      WizardForm.StatusLabel.Caption := 'Windows Security interrupted setup. Follow the steps shown to retry.'
    else WizardForm.StatusLabel.Caption := Copy(S, 1, 180);
    WizardForm.StatusLabel.Update;
    SaveStringToFile(ExpandConstant('{app}\setup-last.log'), S + #13#10, True);
  end;
end;

function RunBackend(const Root, Arguments: String): Boolean;
var
  ExitCode: Integer;
begin
  LastOutput := '';
  ExitCode := -1;
  Result := ExecAndLogOutput(Root + '\.runtime\python\python.exe',
    '-u ' + Q(Root + '\steamvr\setup_native.py') + ' ' + Arguments,
    Root, SW_SHOWNORMAL, ewWaitUntilTerminated, ExitCode, @BackendOutput);
  Result := Result and (ExitCode = 0);
  if not Result and (LastOutput = '') then
    LastOutput := 'Could not start the setup helper. Check Windows Security Protection History and keep your installation backups.';
end;

function InitializeSetup: Boolean;
begin
  Result := not CheckForMutexes('D2R-PCVR-Managed-Operation');
  if Result then CreateMutex('D2R-PCVR-Managed-Operation')
  else MsgBox('Another D2R PCVR setup or removal is running. Close it first.', mbError, MB_OK);
  if WizardSilent and (ExpandConstant('{param:CONFIG|}') = '') and
      (ExpandConstant('{param:PAYLOADCHECK|}') = '') then begin
    Log('Silent setup requires /CONFIG="path to setup choices INI". No game files were changed.');
    Result := False;
  end;
end;

procedure LoadProfiles;
var
  I, Count, SelectedIndex: Integer;
  ID, Name, SelectedProfile: String;
begin
  SelectedProfile := '';
  if (AccountPage.SelectedValueIndex >= 0) and (AccountPage.SelectedValueIndex < ProfileIDs.Count) then
    SelectedProfile := ProfileIDs[AccountPage.SelectedValueIndex];
  SelectedIndex := 0;
  ProfileIDs.Clear;
  AccountPage.CheckListBox.Items.Clear;
  Count := GetIniInt('detected', 'profiles', 0, 0, 1000, ProbeFile);
  for I := 0 to Count - 1 do begin
    ID := GetIniString('detected', 'profile_' + IntToStr(I), '', ProbeFile);
    if ID = SelectedProfile then SelectedIndex := I;
    Name := GetIniString('detected', 'profile_name_' + IntToStr(I), 'Steam account', ProbeFile);
    ProfileIDs.Add(ID);
    AccountPage.Add(Name + ' (local ID ' + ID + ')');
  end;
  if Count > 0 then AccountPage.SelectedValueIndex := SelectedIndex;
end;

function RefreshProbe(const Steam: String): Boolean;
var
  Arguments, FoundBackup: String;
begin
  Arguments := '--probe ' + Q(ProbeFile) + ' --source-root ' + Q(ExpandConstant('{src}'));
  if Steam <> '' then begin
    Arguments := Arguments + ' --steam ' + Q(Steam) + ' --managed-root ' + Q(WizardDirValue);
    if CompareText(BackupGame, PathsPage.Values[1]) <> 0 then begin
      BackupPage.Values[0] := '';
      BackupGame := PathsPage.Values[1];
    end;
    if PathsPage.Values[1] <> '' then begin
      Arguments := Arguments + ' --game ' + Q(PathsPage.Values[1]);
      if BackupPage.Values[0] <> '' then Arguments := Arguments + ' --backup-hint ' + Q(BackupPage.Values[0]);
    end;
  end;
  Result := RunBackend(PayloadRoot, Arguments);
  if Result then begin
    LoadProfiles;
    FoundBackup := GetIniString('detected', 'backup_folder', '', ProbeFile);
    if (FoundBackup <> '') or (GetIniInt('detected', 'backup_required', 0, 0, 1, ProbeFile) = 0) then
      BackupPage.Values[0] := FoundBackup;
  end;
end;

// Shown only while Steam or the game is running: Steam rewrites the files Setup edits when it exits.
procedure RefreshCloseSteamPage;
var
  Game, Steam: Boolean;
begin
  Game := GameRunning;
  Steam := SteamRunning;
  if Game then
    CloseSteamText.Caption := 'Diablo II: Resurrected (D2R.exe or D2RLoader.exe) is running. Save & Exit the game first: ' +
      'Setup never closes the game, so your progress is not lost. Then click Next.'
  else if Steam then
    CloseSteamText.Caption := 'Steam is running. Setup changes Steam''s VR shortcuts and launch options, and Steam overwrites ' +
      'them when it exits, so Steam has to be closed first.'#13#10#13#10 +
      'Click Close Steam: Setup asks Steam to exit the normal way (the same as Steam > Exit; SteamVR closes with it) ' +
      'and waits for it. Steam is never forced to close. You can also choose Steam > Exit yourself, then click Next.'
  else
    CloseSteamText.Caption := 'Steam and the game are closed. Click Next to continue.';
  CloseSteamButton.Enabled := Steam and not Game;
  CloseSteamButton.Top := CloseSteamText.Top + CloseSteamText.Height + ScaleY(12);
  CloseSteamStatus.Top := CloseSteamButton.Top + CloseSteamButton.Height + ScaleY(12);
end;

procedure CloseSteamButtonClick(Sender: TObject);
begin
  CloseSteamButton.Enabled := False;
  WizardForm.NextButton.Enabled := False;
  WizardForm.BackButton.Enabled := False;
  try
    CloseSteam(PathsPage.Values[0], CloseSteamStatus);
  finally
    WizardForm.NextButton.Enabled := True;
    WizardForm.BackButton.Enabled := True;
    RefreshCloseSteamPage;
  end;
end;

procedure CreateCloseSteamPage;
begin
  CloseSteamPage := CreateCustomPage(BackupPage.ID, 'Close Steam',
    'Steam must be closed while Setup changes its shortcuts');
  CloseSteamText := TNewStaticText.Create(CloseSteamPage);
  CloseSteamText.Parent := CloseSteamPage.Surface;
  CloseSteamText.AutoSize := True;
  CloseSteamText.WordWrap := True;
  CloseSteamText.Width := CloseSteamPage.SurfaceWidth;
  CloseSteamButton := TNewButton.Create(CloseSteamPage);
  CloseSteamButton.Parent := CloseSteamPage.Surface;
  CloseSteamButton.Caption := 'Close Steam';
  CloseSteamButton.Width := ScaleX(120);
  CloseSteamButton.Height := ScaleY(26);
  CloseSteamButton.OnClick := @CloseSteamButtonClick;
  CloseSteamStatus := TNewStaticText.Create(CloseSteamPage);
  CloseSteamStatus.Parent := CloseSteamPage.Surface;
  CloseSteamStatus.AutoSize := True;
  CloseSteamStatus.WordWrap := True;
  CloseSteamStatus.Width := CloseSteamPage.SurfaceWidth;
  CloseSteamStatus.Caption := '';
end;

procedure InitializeWizard;
begin
  WizardForm.WelcomeLabel2.Caption :=
    'Play your own Diablo II: Resurrected in first-person VR with motion controls.'#13#10#13#10 +
    'Supports SteamVR and Virtual Desktop on Windows. Steam Frame uses SteamVR.'#13#10#13#10 +
    'You must purchase and install the original game. No Blizzard game assets are included. Use an OFFLINE local character with this preview.'#13#10#13#10 +
    'Launch the original game once and complete its account prompts before installing. Save and exit D2R before clicking Install; Setup can close Steam for you.';
  PayloadRoot := ExpandConstant('{tmp}\') + '{app}';
  ProbeFile := ExpandConstant('{tmp}\detected.ini');
  ChoicesFile := ExpandConstant('{tmp}\choices.ini');
  ExtractTemporaryFiles('{app}\*');
  if ExpandConstant('{param:PAYLOADCHECK|}') <> '' then begin
    if not RunBackend(PayloadRoot, '--check') then RaiseException(LastOutput);
    Log('D2R PCVR PAYLOADCHECK PASSED; no game or Steam files changed.');
    Abort;
  end;
  ProfileIDs := TStringList.Create;
  EditionPage := CreateInputOptionPage(wpLicense, 'Choose your game installation',
    'Steam or Battle.net', 'Select the copy you want to play in VR. Battle.net support is experimental.', True, False);
  EditionPage.Add('Steam');
  HeadsetPage := CreateInputOptionPage(EditionPage.ID, 'Choose your headset',
    'Controller layout and connection', 'Quest 2 uses Quest controls; physical Quest 2 testing is still pending.', True, False);
  HeadsetPage.Add('Steam Frame');
  HeadsetPage.Add('Quest 3');
  HeadsetPage.Add('Quest 2');
  HeadsetPage.Add('Other SteamVR headset');
  HeadsetPage.SelectedValueIndex := StrToIntDef(GetPreviousData('Headset', '0'), 0);
  ConnectionPage := CreateInputOptionPage(HeadsetPage.ID, 'Connect your Quest',
    'Choose the connection you use', 'Virtual Desktop requires its headset app and Windows Streamer. Battle.net with VDXR has not yet been confirmed.', True, False);
  ConnectionPage.Add('Steam Link / SteamVR');
  ConnectionPage.Add('Virtual Desktop / VDXR');
  ConnectionPage.Add('Both (create two Steam library shortcuts)');
  ConnectionPage.SelectedValueIndex := StrToIntDef(GetPreviousData('Connection', '0'), 0);
  PathsPage := CreateInputDirPage(ConnectionPage.ID, 'Locate the installed game',
    'Detected folders can be changed with Browse', 'Select existing folders. Saved Games must contain Settings.json from the original game''s first launch.', False, '');
  PathsPage.Add('Steam folder (contains steam.exe):');
  PathsPage.Add('Diablo II: Resurrected folder (contains D2R.exe):');
  PathsPage.Add('Diablo II: Resurrected Saved Games folder:');
  RuntimePage := CreateInputDirPage(PathsPage.ID, 'Locate SteamVR', 'Choose its installed folder',
    'The SteamVR folder contains steamxr_win64.json. Install SteamVR through Steam if it is missing.', False, '');
  RuntimePage.Add('SteamVR folder:');
  AccountPage := CreateInputOptionPage(RuntimePage.ID, 'Choose your Steam account',
    'Steam library shortcuts', 'VR shortcuts will be added to this local account. Other games and shortcuts are kept.', True, True);
  ModePage := CreateInputOptionPage(AccountPage.ID, 'Install or update',
    'Choose how to apply settings', 'Game files, settings and Steam configuration are backed up. Character progress is kept.', True, False);
  ModePage.Add('Update / repair and keep calibration and video settings');
  ModePage.Add('Full install with recommended settings (resets calibration)');
  ModePage.Add('Only configure Steam shortcuts (keep the installed mod)');
  ModePage.SelectedValueIndex := 1;
  HDRPage := CreateOutputMsgPage(ModePage.ID, 'Check HDR before playing',
    'HDR can affect VR visuals', '');
  BackupPage := CreateInputDirPage(HDRPage.ID, 'Locate an older installation backup',
    'Keep the original files available for removal',
    'Setup fills in a verified backup when it can find one. Otherwise select the original snapshot or its backup folder (usually setup-backups\game or build-frame\backups in the old mod folder). Setup will copy it into the helper folder and remember it for this game. If it is unavailable, cancel and keep the current installation.', False, '');
  BackupPage.Add('Original installation backup folder:');
  CreateCloseSteamPage;
  if not RefreshProbe('') then RaiseException(LastOutput);
  if GetIniInt('detected', 'battlenet_available', 0, 0, 1, ProbeFile) = 1 then
    EditionPage.Add('Battle.net (experimental)');
  EditionPage.SelectedValueIndex := 0;
  PathsPage.Values[0] := GetIniString('detected', 'steam', '', ProbeFile);
  PathsPage.Values[1] := GetIniString('detected', 'steam_game', '', ProbeFile);
  PathsPage.Values[2] := GetIniString('detected', 'saves', '', ProbeFile);
  RuntimePage.Values[0] := GetIniString('detected', 'steamvr', '', ProbeFile);
  PreviousEdition := 'steam';
end;

function UsesSteamVR: Boolean;
begin
  Result := (HeadsetPage.SelectedValueIndex in [0, 3]) or (ConnectionPage.SelectedValueIndex <> 1);
end;

function ShouldSkipPage(PageID: Integer): Boolean;
begin
  if ExpandConstant('{param:CONFIG|}') <> '' then begin
    Result := ((PageID >= EditionPage.ID) and (PageID <= BackupPage.ID)) or (PageID = CloseSteamPage.ID);
    Exit;
  end;
  if PageID = CloseSteamPage.ID then begin
    Result := not SteamRunning and not GameRunning;
    Exit;
  end;
  Result := ((PageID = ConnectionPage.ID) and (HeadsetPage.SelectedValueIndex in [0, 3])) or
            ((PageID = RuntimePage.ID) and not UsesSteamVR) or
            ((PageID = AccountPage.ID) and (ProfileIDs.Count = 1)) or
            ((PageID = HDRPage.ID) and (GetIniString('detected', 'hdr_warning', '', ProbeFile) = '')) or
            ((PageID = BackupPage.ID) and (GetIniInt('detected', 'backup_required', 0, 0, 1, ProbeFile) = 0));
end;

function InstalledHere: Boolean;
begin
  Result := GetIniString('detected', 'installed_mod', '', ProbeFile) = 'd2r-pcvr';
end;

function NextButtonClick(CurPageID: Integer): Boolean;
var
  Edition, Key: String;
begin
  Result := True;
  if ExpandConstant('{param:CONFIG|}') <> '' then Exit;
  if CurPageID = CloseSteamPage.ID then begin
    if GameRunning then begin
      MsgBox('Save & Exit D2R first (Setup does not close the game), then click Next.', mbError, MB_OK);
      Result := False;
    end else if SteamRunning then begin
      MsgBox('Steam is still running. Click Close Steam, or choose Steam > Exit on the PC (do not just close its window), then click Next.', mbError, MB_OK);
      Result := False;
    end;
    RefreshCloseSteamPage;
  end;
  if CurPageID = EditionPage.ID then begin
    Edition := 'steam';
    Key := 'steam_game';
    if EditionPage.SelectedValueIndex = 1 then begin Edition := 'battlenet'; Key := 'battlenet_game'; end;
    if Edition <> PreviousEdition then PathsPage.Values[1] := GetIniString('detected', Key, '', ProbeFile);
    PreviousEdition := Edition;
  end;
  if CurPageID = PathsPage.ID then begin
    if not FileExists(PathsPage.Values[0] + '\steam.exe') or
       not FileExists(PathsPage.Values[1] + '\D2R.exe') or
       not FileExists(PathsPage.Values[2] + '\Settings.json') then begin
      MsgBox('Select the existing Steam, game and Saved Games folders. Launch the original game once if Settings.json is missing.', mbError, MB_OK);
      Result := False;
    end else begin
      Result := RefreshProbe(PathsPage.Values[0]);
      if not Result then MsgBox(LastOutput, mbError, MB_OK);
      if ProfileIDs.Count = 0 then begin
        MsgBox('Sign into Steam once on this PC so setup can find your Steam account, then retry.', mbError, MB_OK);
        Result := False;
      end;
      if RuntimePage.Values[0] = '' then RuntimePage.Values[0] := GetIniString('detected', 'steamvr', '', ProbeFile);
      // Another VR mod (D2R VR) in this game folder: its files stay as they are and setup does not go on.
      if Result and (GetIniString('detected', 'other_mod', '', ProbeFile) <> '') then begin
        MsgBox(GetIniString('detected', 'other_mod', '', ProbeFile), mbError, MB_OK);
        Result := False;
      end;
      // D2R PCVR already here (d2rl-pcvr.dll, or d2rl-vrcam.dll of a release up to 0.3.16): offer Update / repair.
      if InstalledHere then ModePage.SelectedValueIndex := 0
      else ModePage.SelectedValueIndex := 1;
    end;
  end;
  if (CurPageID = ModePage.ID) and (ModePage.SelectedValueIndex = 1) and InstalledHere then
    Result := MsgBox('Full install resets body calibration and applies recommended video settings. Continue?', mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES;
  if CurPageID = BackupPage.ID then begin
    if not DirExists(BackupPage.Values[0]) then begin
      MsgBox('Select the existing original installation backup folder, or cancel to keep the current mod.', mbError, MB_OK);
      Result := False;
    end else begin
      Result := RefreshProbe(PathsPage.Values[0]);
      if not Result then MsgBox(LastOutput, mbError, MB_OK)
      else if (GetIniInt('detected', 'backup_required', 0, 0, 1, ProbeFile) = 1) and
          (GetIniString('detected', 'backup_folder', '', ProbeFile) = '') then begin
        MsgBox(GetIniString('detected', 'backup_message', 'The selected folder does not contain the original backup for this game.', ProbeFile), mbError, MB_OK);
        Result := False;
      end;
    end;
  end;
end;

procedure WriteChoices;
var
  Headsets, Connections, Modes, Lines: TArrayOfString;
  Connection: String;
begin
  Headsets := ['Steam Frame', 'Quest 3', 'Quest 2', 'Other SteamVR headset'];
  Connections := ['steamvr', 'vdxr', 'steamvr,vdxr'];
  Modes := ['repair', 'full', 'shortcuts'];
  Connection := 'steamvr';
  if not (HeadsetPage.SelectedValueIndex in [0, 3]) then Connection := Connections[ConnectionPage.SelectedValueIndex];
  Lines := ['[setup]', 'store=' + PreviousEdition,
    'headset=' + Headsets[HeadsetPage.SelectedValueIndex], 'connections=' + Connection,
    'steam=' + PathsPage.Values[0], 'game=' + PathsPage.Values[1], 'saves=' + PathsPage.Values[2],
    'steamvr=' + RuntimePage.Values[0], 'profile=' + ProfileIDs[AccountPage.SelectedValueIndex],
    'mode=' + Modes[ModePage.SelectedValueIndex], 'backups=' + BackupPage.Values[0]];
  if not SaveStringsToUTF8File(ChoicesFile, Lines, False) then RaiseException('Could not save the setup choices.');
end;

function UpdateReadyMemo(Space, NewLine, MemoUserInfoInfo, MemoDirInfo,
  MemoTypeInfo, MemoComponentsInfo, MemoGroupInfo, MemoTasksInfo: String): String;
var
  Connection: String;
begin
  if ExpandConstant('{param:CONFIG|}') <> '' then begin
    Result := 'Using the supplied setup choices: ' + ExpandConstant('{param:CONFIG|}') + NewLine +
      GetIniString('detected', 'hdr_warning', '', ProbeFile);
    Exit;
  end;
  Connection := 'SteamVR';
  if not (HeadsetPage.SelectedValueIndex in [0, 3]) then
    Connection := ConnectionPage.CheckListBox.Items[ConnectionPage.SelectedValueIndex];
  Result := 'Game: ' + EditionPage.CheckListBox.Items[EditionPage.SelectedValueIndex] + NewLine +
    'Headset: ' + HeadsetPage.CheckListBox.Items[HeadsetPage.SelectedValueIndex] + NewLine +
    'Connection: ' + Connection + NewLine +
    'Mode: ' + ModePage.CheckListBox.Items[ModePage.SelectedValueIndex] + NewLine + NewLine +
    'Game folder: ' + PathsPage.Values[1] + NewLine +
    'Saved Games: ' + PathsPage.Values[2] + NewLine +
    'Steam: ' + PathsPage.Values[0] + NewLine +
    'Steam account: ' + AccountPage.CheckListBox.Items[AccountPage.SelectedValueIndex] + NewLine +
    'Helpers and backups: ' + WizardDirValue + NewLine;
  if BackupPage.Values[0] <> '' then Result := Result + 'Original backup location: ' + BackupPage.Values[0] + NewLine;
  Result := Result + NewLine +
    'Save and exit D2R, then fully exit Steam before installing. Use an offline character in VR.';
  if GetIniString('detected', 'hdr_warning', '', ProbeFile) <> '' then
    Result := Result + NewLine + NewLine + GetIniString('detected', 'hdr_warning', '', ProbeFile);
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  Result := '';
  if ExpandConstant('{param:CONFIG|}') <> '' then ChoicesFile := ExpandConstant('{param:CONFIG|}')
  else WriteChoices;
  // Steam started again after the Close Steam page, or a /CONFIG install. The game is never closed here.
  if SteamRunning and not GameRunning then begin
    if ExpandConstant('{param:CONFIG|}') = '' then begin
      Result := 'Steam is running again. Click Back, then Close Steam (or choose Steam > Exit on the PC), and click Next.' + #13#10#13#10 +
        'No game or Steam files were changed.';
      Exit;
    end;
    // Silent and /CONFIG installs close Steam only when asked to with /CLOSESTEAM.
    if SwitchGiven('/CLOSESTEAM') then CloseSteam(GetIniString('setup', 'steam', '', ChoicesFile), nil);
  end;
  if not RunBackend(PayloadRoot, '--validate ' + Q(ChoicesFile) + ' --managed-root ' + Q(ExpandConstant('{app}'))) then begin
    if IsSecurityBlock then Result := SecurityBlockMessage
    else Result := LastOutput + #13#10#13#10 + 'No game or Steam files were changed. Correct the selections, or Save & Exit D2R and fully exit Steam, then retry.';
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then begin
    BackendBusy := True;
    try
      SetupFailed := not RunBackend(ExpandConstant('{app}'), '--install ' + Q(ChoicesFile));
    finally
      BackendBusy := False;
    end;
    if SetupFailed then begin
      Log('Game configuration did not complete. Helper files and backups are retained for repair.');
      if IsSecurityBlock then MsgBox(SecurityBlockMessage, mbInformation, MB_OK)
      else MsgBox(LastOutput + #13#10#13#10 + 'Setup did not complete. Your helper files and backups have been kept. See setup-last.log in ' + ExpandConstant('{app}') + ', then rerun Setup to repair.', mbError, MB_OK);
    end;
  end;
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  if CurPageID = HDRPage.ID then
    HDRPage.MsgLabel.Caption := GetIniString('detected', 'hdr_warning', '', ProbeFile);
  if CurPageID = CloseSteamPage.ID then begin
    CloseSteamStatus.Caption := '';
    RefreshCloseSteamPage;
  end;
  if CurPageID = wpFinished then begin
    if SetupFailed then begin
      if IsSecurityBlock then begin
        WizardForm.FinishedHeadingLabel.Caption := 'One more step to finish setup';
        WizardForm.FinishedLabel.Caption := SecurityBlockMessage;
      end else begin
        WizardForm.FinishedHeadingLabel.Caption := 'D2R VR Unleashed setup needs attention';
        WizardForm.FinishedLabel.Caption := 'Game configuration did not complete. Your backups and helper files were kept. Check setup-last.log in ' + ExpandConstant('{app}') + ' and rerun Setup to repair.';
      end;
    end else WizardForm.FinishedLabel.Caption :=
      'Your VR shortcuts are ready in Steam. Connect your headset and use the matching SteamVR or Virtual Desktop shortcut with an OFFLINE character.'#13#10#13#10 +
      'Normal Steam / Battle.net Play stays stock. First VR load asks for body calibration. Help and controller mappings are in the ' + ExpandConstant('{groupname}') + ' Start menu folder.';
  end;
end;

function GetCustomSetupExitCode: Integer;
begin
  if SetupFailed then Result := 1 else Result := 0;
end;

function SetupSucceeded: Boolean;
begin
  Result := not SetupFailed and (ExpandConstant('{param:CONFIG|}') = '');
end;

function SteamExecutable(Param: String): String;
begin
  Result := PathsPage.Values[0] + '\steam.exe';
end;

procedure RegisterPreviousData(PreviousDataKey: Integer);
begin
  if not SetupFailed and (ExpandConstant('{param:CONFIG|}') = '') then begin
    SetPreviousData(PreviousDataKey, 'Headset', IntToStr(HeadsetPage.SelectedValueIndex));
    SetPreviousData(PreviousDataKey, 'Connection', IntToStr(ConnectionPage.SelectedValueIndex));
  end;
end;

procedure CancelButtonClick(CurPageID: Integer; var Cancel, Confirm: Boolean);
begin
  if BackendBusy then begin Cancel := False; Confirm := False; end;
end;

function InitializeUninstall: Boolean;
begin
  Result := not CheckForMutexes('D2R-PCVR-Managed-Operation');
  if Result then CreateMutex('D2R-PCVR-Managed-Operation')
  else begin MsgBox('Another D2R PCVR operation is running. Close it first.', mbError, MB_OK); Exit; end;
  Result := RunBackend(ExpandConstant('{app}'), '--uninstall-check');
  if not Result then MsgBox(LastOutput + #13#10#13#10 + 'Removal stopped. The mod, helper files and backups were kept. Close D2R and fully exit Steam, then retry.', mbError, MB_OK);
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then begin
    UninstallProgressForm.StatusLabel.Caption := 'Restoring managed game installations and removing VR shortcuts...';
    if not RunBackend(ExpandConstant('{app}'), '--uninstall') then
      RaiseException(LastOutput + #13#10#13#10 + 'Removal stopped. Helper files and backups were kept.');
  end;
end;

[Messages]
ConfirmUninstall=Remove D2R PCVR from every Steam and Battle.net game copy configured by this installer?%n%nYour characters, shared stash, current video settings and installation backups will be kept.
