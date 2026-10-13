; Native OpenXR installer. Build with build_native_installer.ps1.
#ifndef PayloadDir
  #error PayloadDir must point to a verified native Windows package with its private Python runtime.
#endif
#ifndef ModVersion
  #define ModVersion "0.3.10"
#endif
#ifndef InstallerOutput
  #define InstallerOutput "Output"
#endif

[Setup]
AppId=D2R-PCVR-Native-Windows
AppName=D2R PCVR
AppVersion={#ModVersion}
AppPublisher=D2R PCVR contributors
AppPublisherURL=https://github.com/bearstonem/d2r-pcvr
AppSupportURL=https://github.com/bearstonem/d2r-pcvr/issues
DefaultDirName={localappdata}\Programs\D2R PCVR
DefaultGroupName=D2R PCVR
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
UninstallDisplayName=D2R PCVR (Windows VR mod)
UninstallDisplayIcon={app}\.runtime\python\python.exe

[Files]
Source: "{#PayloadDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\Installation help"; Filename: "{sys}\notepad.exe"; Parameters: """{app}\INSTALL-WINDOWS.md"""
Name: "{group}\Controller mappings"; Filename: "{sys}\notepad.exe"; Parameters: """{app}\CONTROLS.md"""
Name: "{group}\Uninstall D2R PCVR"; Filename: "{uninstallexe}"

[Run]
Filename: "{code:SteamExecutable}"; Description: "Open Steam to use your new VR shortcuts"; Flags: nowait postinstall skipifsilent; Check: SetupSucceeded

[Code]
const
  SteamExitHelp = 'Save and exit Diablo II. In Steam, choose Steam > Exit. Closing Steam''s window leaves it running. This temporarily disconnects SteamVR streaming; reopen Steam after setup finishes.';
  SecurityHelp = 'If a required file is missing or unreadable, check Windows Security > Virus & threat protection > Protection history (or your other antivirus history). Note the detection name and affected filename. Keep protection enabled while the detection is investigated.';
  DeletedBackupHelp = 'Check the Recycle Bin or another backup for the original extracted mod folder. Downloading the ZIP again does not recreate the backups made during installation.'#13#10#13#10 +
    'If the original backups are permanently gone, cancel setup and back up your entire Diablo II Resurrected Saved Games folder. You can prepare a fresh mod installation by moving the old mod components into a separate folder, then rerunning Setup. You do not need to download the game again.'#13#10#13#10 +
    'See "Original backups permanently deleted" in INSTALL-WINDOWS.md for the exact files to move. Keep game data, saves and unrelated mods. Full install alone does not bypass the backup check.';
var
  EditionPage, HeadsetPage, ConnectionPage, AccountPage, ModePage: TInputOptionWizardPage;
  PathsPage, RuntimePage, BackupPage: TInputDirWizardPage;
  ReadinessPage: TWizardPage;
  BackupDetails, ReadinessDetails: TNewMemo;
  FinishedCheck, FinishedSetupLogs, FinishedGameLogs, FinishedSecurity: TNewButton;
  ProfileIDs: TStringList;
  PayloadRoot, ProbeFile, ChoicesFile, CheckFile, LastOutput, PreviousEdition, BackendLogRoot: String;
  SetupFailed, BackendBusy: Boolean;

function Q(const Value: String): String;
begin
  Result := '"' + Value + '"';
end;

procedure BackendOutput(const S: String; const Error, FirstLine: Boolean);
begin
  Log(S);
  if Trim(S) <> '' then LastOutput := S;
  if BackendLogRoot <> '' then SaveStringToFile(BackendLogRoot + '\setup-last.log', S + #13#10, True);
  if BackendBusy then begin
    WizardForm.StatusLabel.Caption := Copy(S, 1, 180);
    WizardForm.StatusLabel.Update;
  end;
end;

function RunBackend(const Root, Arguments: String): Boolean;
var
  ExitCode: Integer;
begin
  LastOutput := '';
  BackendLogRoot := Root;
  ExitCode := -1;
  Result := ExecAndLogOutput(Root + '\.runtime\python\python.exe',
    '-X utf8 -u ' + Q(Root + '\steamvr\setup_native.py') + ' ' + Arguments,
    Root, SW_SHOWNORMAL, ewWaitUntilTerminated, ExitCode, @BackendOutput);
  Result := Result and (ExitCode = 0);
  if not Result and (LastOutput = '') then
    LastOutput := 'Could not start the setup helper: ' + Root + '\.runtime\python\python.exe. Check Windows Security Protection History and keep your installation backups.';
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
  I, Count: Integer;
  ID, Name: String;
begin
  ProfileIDs.Clear;
  AccountPage.CheckListBox.Items.Clear;
  Count := GetIniInt('detected', 'profiles', 0, 0, 1000, ProbeFile);
  for I := 0 to Count - 1 do begin
    ID := GetIniString('detected', 'profile_' + IntToStr(I), '', ProbeFile);
    Name := GetIniString('detected', 'profile_name_' + IntToStr(I), 'Steam account', ProbeFile);
    ProfileIDs.Add(ID);
    AccountPage.Add(Name + ' (local ID ' + ID + ')');
  end;
  if Count > 0 then AccountPage.SelectedValueIndex := 0;
end;

function RefreshProbe(const Steam: String): Boolean;
var
  Arguments: String;
begin
  Arguments := '--probe ' + Q(ProbeFile);
  if Steam <> '' then begin
    Arguments := Arguments + ' --steam ' + Q(Steam) + ' --managed-root ' + Q(WizardDirValue);
    if PathsPage.Values[1] <> '' then Arguments := Arguments + ' --game ' + Q(PathsPage.Values[1]);
  end;
  Result := RunBackend(PayloadRoot, Arguments);
  if Result then LoadProfiles;
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

procedure OpenLocation(const Location: String);
var
  ErrorCode: Integer;
begin
  if not ShellExec('open', Location, '', '', SW_SHOWNORMAL, ewNoWait, ErrorCode) then
    MsgBox('Could not open ' + Location + '. Windows error: ' + IntToStr(ErrorCode), mbError, MB_OK);
end;

procedure SecurityClick(Sender: TObject);
begin
  MsgBox(SecurityHelp, mbInformation, MB_OK);
  OpenLocation('ms-settings:windowsdefender');
end;

procedure SetupLogsClick(Sender: TObject);
begin
  if DirExists(BackendLogRoot) then OpenLocation(BackendLogRoot)
  else if DirExists(WizardDirValue) then OpenLocation(WizardDirValue)
  else OpenLocation(PayloadRoot);
end;

procedure GameLogsClick(Sender: TObject);
var
  Folder: String;
begin
  Folder := GetIniString('checks', 'game_logs', PathsPage.Values[1] + '\d2rloader\logs', CheckFile);
  if DirExists(Folder) then OpenLocation(Folder)
  else MsgBox('No game startup logs exist yet. After trying the VR shortcut, check ' + Folder + '. Setup logs are available with Open setup logs.', mbInformation, MB_OK);
end;

procedure DeletedBackupClick(Sender: TObject);
var
  ErrorCode: Integer;
begin
  MsgBox(DeletedBackupHelp, mbInformation, MB_OK);
  if not ShellExec('open', ExpandConstant('{sys}\notepad.exe'), Q(PayloadRoot + '\INSTALL-WINDOWS.md'), '', SW_SHOWNORMAL, ewNoWait, ErrorCode) then
    MsgBox('Read Original backups permanently deleted in ' + PayloadRoot + '\INSTALL-WINDOWS.md', mbInformation, MB_OK);
end;

function AddButton(Parent: TWinControl; const Caption: String; Left, Top, Width: Integer; Click: TNotifyEvent): TNewButton;
begin
  Result := TNewButton.Create(Parent);
  Result.Parent := Parent;
  Result.Caption := Caption;
  Result.SetBounds(Left, Top, Width, ScaleY(25));
  Result.OnClick := Click;
end;

function RefreshReadiness: Boolean;
begin
  WriteChoices;
  Result := RunBackend(PayloadRoot, '--readiness ' + Q(ChoicesFile) + ' --report ' + Q(CheckFile) + ' --managed-root ' + Q(WizardDirValue));
  if Result then begin
    ReadinessDetails.Text := 'Diablo II / D2RLoader: ' + GetIniString('checks', 'game', 'Unknown', CheckFile) + #13#10#13#10 +
      'Steam: ' + GetIniString('checks', 'steam', 'Unknown', CheckFile) + #13#10#13#10 +
      'VR runtime: ' + GetIniString('checks', 'runtime', 'Unknown', CheckFile) + #13#10#13#10 +
      'Setup choices and backups: ' + GetIniString('checks', 'selections', 'Unknown', CheckFile);
    Result := GetIniInt('checks', 'ready', 0, 0, 1, CheckFile) = 1;
  end else ReadinessDetails.Text := LastOutput + #13#10#13#10 + SecurityHelp;
end;

procedure CheckAgainClick(Sender: TObject);
begin
  RefreshReadiness;
end;

function CheckInstalled(const ShowResult: Boolean): Boolean;
var
  Detail: String;
begin
  Result := RunBackend(ExpandConstant('{app}'), '--inspect ' + Q(ChoicesFile) + ' --report ' + Q(CheckFile));
  if Result then begin
    Detail := 'Mod files: ' + GetIniString('checks', 'files', 'Unknown', CheckFile) + #13#10#13#10 +
      'VR runtime: ' + GetIniString('checks', 'runtime', 'Unknown', CheckFile) + #13#10#13#10 +
      'Steam shortcuts: ' + GetIniString('checks', 'shortcuts', 'Unknown', CheckFile);
    Result := GetIniInt('checks', 'ready', 0, 0, 1, CheckFile) = 1;
  end else Detail := LastOutput;
  FinishedSecurity.Visible := not Result;
  if ShowResult then begin
    if Result then MsgBox(Detail + #13#10#13#10 + 'These file checks do not test a headset launch.', mbInformation, MB_OK)
    else MsgBox(Detail + #13#10#13#10 + SecurityHelp + #13#10#13#10 + 'Use Open setup logs and Open game logs to collect diagnostics. Run Setup again and choose Update / repair after resolving the error.', mbError, MB_OK);
  end;
end;

procedure CheckInstalledClick(Sender: TObject);
begin
  CheckInstalled(True);
end;

procedure LayoutFinishedPage;
var
  ButtonTop: Integer;
begin
  WizardForm.AdjustLabelHeight(WizardForm.FinishedLabel);
  ButtonTop := WizardForm.FinishedLabel.Top + WizardForm.FinishedLabel.Height + ScaleY(6);
  FinishedCheck.Top := ButtonTop;
  FinishedSetupLogs.Top := ButtonTop;
  FinishedGameLogs.Top := ButtonTop + ScaleY(33);
  FinishedSecurity.Top := ButtonTop + ScaleY(33);
  WizardForm.RunList.Top := ButtonTop + ScaleY(69);
  WizardForm.RunList.Height := WizardForm.FinishedPage.ClientHeight - WizardForm.RunList.Top - ScaleY(8);
end;

function RetryInstallation: Boolean;
var
  Form: TSetupForm;
  Details: TNewMemo;
  RetryButton, StopButton: TNewButton;
begin
  Result := False;
  if WizardSilent then Exit;
  Form := CreateCustomForm(520, 270, False, False);
  try
    Form.Caption := 'D2R PCVR setup needs attention';
    Details := TNewMemo.Create(Form);
    Details.Parent := Form;
    Details.SetBounds(ScaleX(12), ScaleY(12), Form.ClientWidth - ScaleX(24), Form.ClientHeight - ScaleY(60));
    Details.ReadOnly := True;
    Details.ScrollBars := ssVertical;
    Details.Text := LastOutput + #13#10#13#10 + SecurityHelp + #13#10#13#10 +
      'Your helper files and backups have been kept. Resolve the reported error, then choose Retry to run setup again. ' + SteamExitHelp;
    AddButton(Form, 'Windows Security', ScaleX(12), Form.ClientHeight - ScaleY(37), ScaleX(130), @SecurityClick);
    AddButton(Form, 'Open logs', ScaleX(150), Form.ClientHeight - ScaleY(37), ScaleX(100), @SetupLogsClick);
    RetryButton := AddButton(Form, 'Retry', Form.ClientWidth - ScaleX(206), Form.ClientHeight - ScaleY(37), ScaleX(90), nil);
    RetryButton.ModalResult := mrRetry;
    RetryButton.Default := True;
    StopButton := AddButton(Form, 'Stop setup', Form.ClientWidth - ScaleX(108), Form.ClientHeight - ScaleY(37), ScaleX(96), nil);
    StopButton.ModalResult := mrCancel;
    StopButton.Cancel := True;
    Result := Form.ShowModal = mrRetry;
  finally
    Form.Free;
  end;
end;

procedure InitializeWizard;
var
  HelpLabel: TNewStaticText;
  ButtonWidth, ButtonTop: Integer;
begin
  WizardForm.WelcomeLabel2.Caption :=
    'Play your own Diablo II: Resurrected in first-person VR with motion controls.'#13#10#13#10 +
    'Supports SteamVR and Virtual Desktop on Windows. Steam Frame uses SteamVR.'#13#10#13#10 +
    'You must purchase and install the original game. No Blizzard game assets are included. Use an OFFLINE local character with this preview.'#13#10#13#10 +
    'Launch the original game once and complete its account prompts. Save and exit D2R, then choose Steam > Exit before installing. Closing Steam''s window leaves it running.';
  PayloadRoot := ExpandConstant('{tmp}\') + '{app}';
  ProbeFile := ExpandConstant('{tmp}\detected.ini');
  ChoicesFile := ExpandConstant('{tmp}\choices.ini');
  CheckFile := ExpandConstant('{tmp}\checks.ini');
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
  BackupPage := CreateInputDirPage(ModePage.ID, 'Locate an older installation backup',
    'Keep the original files available for removal',
    'Select the folder where you originally extracted the mod ZIP, or its setup-backups\game or build-frame\backups folder. Setup needs these original backups to preserve removal and will copy and verify them before updating.', False, '');
  BackupPage.Add('Original installation backup folder:');
  BackupPage.SubCaptionLabel.Height := ScaleY(54);
  BackupPage.PromptLabels[0].Top := ScaleY(62);
  BackupPage.Edits[0].Top := ScaleY(80);
  BackupPage.Buttons[0].Top := ScaleY(80);
  BackupDetails := TNewMemo.Create(WizardForm);
  BackupDetails.Parent := BackupPage.Surface;
  BackupDetails.SetBounds(0, ScaleY(117), BackupPage.SurfaceWidth, BackupPage.SurfaceHeight - ScaleY(155));
  BackupDetails.ReadOnly := True;
  BackupDetails.ScrollBars := ssVertical;
  AddButton(BackupPage.Surface, 'Deleted the old folder?', 0, BackupPage.SurfaceHeight - ScaleY(30), ScaleX(175), @DeletedBackupClick);
  ReadinessPage := CreateCustomPage(BackupPage.ID, 'Check before installing', 'Close the game and Steam, then check again');
  HelpLabel := TNewStaticText.Create(WizardForm);
  HelpLabel.Parent := ReadinessPage.Surface;
  HelpLabel.SetBounds(0, 0, ReadinessPage.SurfaceWidth, ScaleY(72));
  HelpLabel.WordWrap := True;
  HelpLabel.Caption := SteamExitHelp;
  ReadinessDetails := TNewMemo.Create(WizardForm);
  ReadinessDetails.Parent := ReadinessPage.Surface;
  ReadinessDetails.SetBounds(0, ScaleY(82), ReadinessPage.SurfaceWidth, ReadinessPage.SurfaceHeight - ScaleY(122));
  ReadinessDetails.ReadOnly := True;
  ReadinessDetails.ScrollBars := ssVertical;
  AddButton(ReadinessPage.Surface, 'Check again', 0, ReadinessPage.SurfaceHeight - ScaleY(30), ScaleX(110), @CheckAgainClick);
  AddButton(ReadinessPage.Surface, 'Windows Security', ScaleX(118), ReadinessPage.SurfaceHeight - ScaleY(30), ScaleX(140), @SecurityClick);
  AddButton(ReadinessPage.Surface, 'Open logs', ScaleX(266), ReadinessPage.SurfaceHeight - ScaleY(30), ScaleX(110), @SetupLogsClick);
  WizardForm.FinishedLabel.AutoSize := False;
  WizardForm.FinishedLabel.Height := ScaleY(132);
  ButtonWidth := (WizardForm.FinishedLabel.Width - ScaleX(8)) div 2;
  ButtonTop := WizardForm.FinishedLabel.Top + WizardForm.FinishedLabel.Height + ScaleY(6);
  FinishedCheck := AddButton(WizardForm.FinishedPage, 'Check installation', WizardForm.FinishedLabel.Left, ButtonTop, ButtonWidth, @CheckInstalledClick);
  FinishedSetupLogs := AddButton(WizardForm.FinishedPage, 'Open setup logs', WizardForm.FinishedLabel.Left + ButtonWidth + ScaleX(8), ButtonTop, ButtonWidth, @SetupLogsClick);
  FinishedGameLogs := AddButton(WizardForm.FinishedPage, 'Open game logs', WizardForm.FinishedLabel.Left, ButtonTop + ScaleY(33), ButtonWidth, @GameLogsClick);
  FinishedSecurity := AddButton(WizardForm.FinishedPage, 'Windows Security', WizardForm.FinishedLabel.Left + ButtonWidth + ScaleX(8), ButtonTop + ScaleY(33), ButtonWidth, @SecurityClick);
  FinishedSecurity.Visible := False;
  WizardForm.RunList.Top := ButtonTop + ScaleY(69);
  WizardForm.RunList.Height := WizardForm.FinishedPage.ClientHeight - WizardForm.RunList.Top - ScaleY(8);
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
    Result := (PageID >= EditionPage.ID) and (PageID <= ReadinessPage.ID);
    Exit;
  end;
  Result := ((PageID = ConnectionPage.ID) and (HeadsetPage.SelectedValueIndex in [0, 3])) or
            ((PageID = RuntimePage.ID) and not UsesSteamVR) or
            ((PageID = AccountPage.ID) and (ProfileIDs.Count = 1)) or
            ((PageID = BackupPage.ID) and (GetIniInt('detected', 'backup_required', 0, 0, 1, ProbeFile) = 0));
end;

function NextButtonClick(CurPageID: Integer): Boolean;
var
  Edition, Key: String;
begin
  Result := True;
  if ExpandConstant('{param:CONFIG|}') <> '' then Exit;
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
      BackupPage.Values[0] := '';
      BackupDetails.Text := 'Last recorded backup: ' + GetIniString('detected', 'backup_path', '', ProbeFile) + #13#10#13#10 +
        GetIniString('detected', 'backup_message', '', ProbeFile) + #13#10#13#10 +
        'If you deleted the original folder, use Deleted the old folder? for recovery steps. Do not select an empty replacement folder.';
      if FileExists(PathsPage.Values[1] + '\d2rloader\plugins\d2rl-vrcam.dll') then ModePage.SelectedValueIndex := 0
      else ModePage.SelectedValueIndex := 1;
    end;
  end;
  if (CurPageID = ModePage.ID) and (ModePage.SelectedValueIndex = 1) and
      FileExists(PathsPage.Values[1] + '\d2rloader\plugins\d2rl-vrcam.dll') then
    Result := MsgBox('Full install resets body calibration and applies recommended video settings. Continue?', mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES;
  if CurPageID = BackupPage.ID then begin
    if not DirExists(BackupPage.Values[0]) then begin
      MsgBox('Select the existing original installation backup folder, or cancel to keep the current mod.', mbError, MB_OK);
      Result := False;
    end else begin
      WriteChoices;
      Result := RunBackend(PayloadRoot, '--validate-backups ' + Q(ChoicesFile) + ' --managed-root ' + Q(WizardDirValue));
      if not Result then MsgBox(LastOutput + #13#10#13#10 + 'Select the original extracted mod folder or its backups. If the folder was deleted, use Deleted the old folder? for help.', mbError, MB_OK);
    end;
  end;
  if CurPageID = ReadinessPage.ID then begin
    Result := RefreshReadiness;
    if not Result then MsgBox('Setup is not ready. Resolve the items shown, then click Check again. No game or Steam files were changed.', mbInformation, MB_OK);
  end;
end;

function UpdateReadyMemo(Space, NewLine, MemoUserInfoInfo, MemoDirInfo,
  MemoTypeInfo, MemoComponentsInfo, MemoGroupInfo, MemoTasksInfo: String): String;
var
  Connection: String;
begin
  if ExpandConstant('{param:CONFIG|}') <> '' then begin
    Result := 'Using the supplied setup choices: ' + ExpandConstant('{param:CONFIG|}');
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
    'Helpers and backups: ' + WizardDirValue + NewLine + NewLine +
    SteamExitHelp + NewLine + 'Use an offline character in VR.';
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  Result := '';
  if ExpandConstant('{param:CONFIG|}') <> '' then ChoicesFile := ExpandConstant('{param:CONFIG|}')
  else WriteChoices;
  if not RunBackend(PayloadRoot, '--validate ' + Q(ChoicesFile) + ' --managed-root ' + Q(ExpandConstant('{app}'))) then
    Result := LastOutput + #13#10#13#10 + 'No game or Steam files were changed. Correct the selections, or Save & Exit D2R and fully exit Steam, then retry.';
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then begin
    BackendBusy := True;
    try
      repeat
        SetupFailed := not RunBackend(ExpandConstant('{app}'), '--install ' + Q(ChoicesFile));
        if not SetupFailed then Break;
        if not RetryInstallation then Break;
      until False;
    finally
      BackendBusy := False;
    end;
    if SetupFailed then begin
      Log('Game configuration did not complete. Helper files and backups are retained for repair.');
    end;
  end;
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  if CurPageID = ReadinessPage.ID then RefreshReadiness;
  if CurPageID = wpFinished then begin
    if SetupFailed then begin
      WizardForm.FinishedHeadingLabel.Caption := 'D2R PCVR setup needs attention';
      WizardForm.FinishedLabel.Caption := 'Game configuration did not complete. Your backups and helper files were kept. Check setup-last.log in ' + ExpandConstant('{app}') + ' and rerun Setup to repair.';
      FinishedSecurity.Visible := True;
    end else begin
      if not CheckInstalled(False) then
        WizardForm.FinishedHeadingLabel.Caption := 'D2R PCVR installation check needs attention';
      WizardForm.FinishedLabel.Caption :=
        'In Steam, launch: ' + GetIniString('checks', 'launch', 'the matching D2R VR shortcut', CheckFile) + #13#10#13#10 +
        'Connect your headset and use an OFFLINE character. Keep SteamVR closed for Virtual Desktop / VDXR. First VR load asks for body calibration.'#13#10#13#10 +
        'For a launch problem, use Check installation and the log buttons below. Complete any Battle.net account prompts through normal Play before trying VR. Normal desktop Play stays stock.';
    end;
    LayoutFinishedPage;
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
