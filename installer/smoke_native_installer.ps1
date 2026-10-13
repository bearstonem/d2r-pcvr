param(
    [string]$ISCCPath,
    [string]$PythonDirectory
)
# Exercise Inno's real lifecycle with a stub backend and a distinct app ID.
# This does not install the mod or touch any real game/Steam configuration.
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path $PSScriptRoot -Parent
if (!$ISCCPath) { $ISCCPath = Join-Path $repoRoot 'deps/inno-7.1.0/ISCC.exe' }
if (!$PythonDirectory) { $PythonDirectory = Join-Path $repoRoot 'deps/python' }
if (!(Test-Path -LiteralPath $ISCCPath) -or !(Test-Path -LiteralPath (Join-Path $PythonDirectory 'python.exe'))) {
    throw 'Provide Inno Setup 7.1+ and the official embeddable Python directory.'
}
$qaRoot = Join-Path $repoRoot ('build-frame/installer-qa-' + [guid]::NewGuid().ToString('N'))
$payload = Join-Path $qaRoot 'payload'
$target = Join-Path $qaRoot 'installed'
New-Item -ItemType Directory -Path "$payload/steamvr","$payload/.runtime" -Force | Out-Null
Copy-Item -LiteralPath $PythonDirectory -Destination "$payload/.runtime/python" -Recurse
foreach ($name in @('LICENSE','INSTALL-WINDOWS.md','CONTROLS.md')) {
    Copy-Item -LiteralPath (Join-Path $repoRoot $name) -Destination (Join-Path $payload $name)
}
$backend = @'
import argparse, configparser
from pathlib import Path
root = Path(__file__).resolve().parent.parent
p = argparse.ArgumentParser()
for option in ('probe','validate','validate-backups','readiness','inspect','report','install','managed-root','steam','game'):
    p.add_argument('--' + option)
for option in ('check','uninstall-check','uninstall'):
    p.add_argument('--' + option, action='store_true')
a = p.parse_args()
if a.probe:
    c = configparser.ConfigParser()
    c['detected'] = dict(profiles='1', profile_0='123', profile_name_0='Fixture only',
                        battlenet_available='1', backup_required='0')
    with Path(a.probe).open('w', encoding='utf-16') as f:
        c.write(f)
elif a.install:
    attempts = root / 'fixture-install-attempts.txt'
    attempts.write_text(str(int(attempts.read_text()) + 1 if attempts.exists() else 1))
    (root / 'fixture-installed.txt').write_text('stub installed; no game changes')
    c = configparser.ConfigParser()
    c.read(a.install)
    if c['setup']['mode'] == 'fail-install':
        raise SystemExit('Fixture: required dependency file is unreadable. Check Protection history.')
elif a.readiness or a.inspect:
    c = configparser.ConfigParser()
    c['checks'] = dict(ready='1', game='Closed', steam='Closed', runtime='Fixture runtime available',
                       selections='Verified', files='Fixture mod files readable', shortcuts='Verified', launch='Fixture VR shortcut')
    with Path(a.report).open('w', encoding='utf-16') as f:
        c.write(f)
elif a.uninstall:
    if (root / 'fixture-block-removal.txt').exists():
        raise SystemExit('Fixture: restoration could not finish; helpers must remain.')
    assert (root / '.runtime/python/python.exe').is_file(), 'Helpers removed too early'
    (root.parent / 'removal-ran-before-helper-deletion.txt').write_text('passed')
    (root / 'fixture-installed.txt').unlink()
print('Installer lifecycle fixture passed; no game or Steam configuration touched.')
'@
[IO.File]::WriteAllText((Join-Path $payload 'steamvr/setup_native.py'), $backend)
$script = [IO.File]::ReadAllText((Join-Path $PSScriptRoot 'D2R_PCVR_Setup.iss'))
$script = $script.Replace('AppId=D2R-PCVR-Native-Windows','AppId=D2R-PCVR-Installer-Fixture')
$script = $script.Replace('DefaultGroupName=D2R PCVR','DefaultGroupName=D2R PCVR Installer Fixture')
$script = $script.Replace('AppName=D2R PCVR','AppName=D2R PCVR Installer Fixture')
$script = $script.Replace('UninstallDisplayName=D2R PCVR (Windows VR mod)','UninstallDisplayName=D2R PCVR Installer Fixture')
# Exercise readiness before Inno initializes {app}; silent install alone skips it.
$script = $script.Replace("(ExpandConstant('{param:PAYLOADCHECK|}') = '') then begin", "(ExpandConstant('{param:PAYLOADCHECK|}') = '') and (ExpandConstant('{param:GUIDANCECHECK|}') = '') then begin")
$guidanceHook = @'
  PreviousEdition := 'steam';
  if ExpandConstant('{param:GUIDANCECHECK|}') <> '' then begin
    if not RefreshReadiness then RaiseException('Fixture readiness failed before Install: ' + LastOutput);
    Log('D2R PCVR GUIDANCECHECK PASSED before Install; no permanent files changed.');
    Abort;
  end;
'@
$script = $script.Replace("  PreviousEdition := 'steam';", $guidanceHook)
$scriptPath = Join-Path $qaRoot 'Fixture.iss'
[IO.File]::WriteAllText($scriptPath, $script)
& $ISCCPath /Qp "/DPayloadDir=$payload" /DModVersion=0.0.1 "/DInstallerOutput=$qaRoot" $scriptPath
if ($LASTEXITCODE -ne 0) { throw 'Fixture compilation failed' }
$choices = Join-Path $qaRoot 'choices.ini'
[IO.File]::WriteAllText($choices,"[setup]`r`nmode=repair`r`n")
$registryKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\D2R-PCVR-Installer-Fixture_is1'
if (Test-Path -LiteralPath $registryKey) { throw 'A previous fixture remains registered; remove it before testing.' }
$guidanceLog = Join-Path $qaRoot 'guidance-check.log'
$guidance = Start-Process -FilePath (Join-Path $qaRoot 'D2R-PCVR-Setup-0.0.1.exe') -ArgumentList @(
    '/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART','/GUIDANCECHECK=1',('/DIR="{0}"' -f $target), ('/LOG="{0}"' -f $guidanceLog)
) -WindowStyle Hidden -PassThru -Wait
if (!(Select-String -LiteralPath $guidanceLog -SimpleMatch 'D2R PCVR GUIDANCECHECK PASSED before Install' -Quiet) -or
    (Test-Path -LiteralPath $target) -or (Test-Path -LiteralPath $registryKey)) {
    throw "Readiness did not complete before Install without permanent writes. Logs: $qaRoot"
}
$setup = Start-Process -FilePath (Join-Path $qaRoot 'D2R-PCVR-Setup-0.0.1.exe') -ArgumentList @(
    '/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART',('/DIR="{0}"' -f $target), ('/CONFIG="{0}"' -f $choices), ('/LOG="{0}/setup.log"' -f $qaRoot)
) -WindowStyle Hidden -PassThru -Wait
if ($setup.ExitCode -ne 0 -or !(Test-Path -LiteralPath $registryKey) -or !(Test-Path -LiteralPath "$target/fixture-installed.txt")) {
    throw "Fixture install failed (exit $($setup.ExitCode)). Logs: $qaRoot"
}
$block = Join-Path $target 'fixture-block-removal.txt'
[IO.File]::WriteAllText($block, 'Simulate a restoration failure before Inno deletes helpers.')
$blocked = Start-Process -FilePath "$target/unins000.exe" -ArgumentList @(
    '/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART',('/LOG="{0}/blocked-removal.log"' -f $qaRoot)
) -WindowStyle Hidden -PassThru -Wait
if ($blocked.ExitCode -eq 0 -or !(Test-Path -LiteralPath $registryKey) -or
    !(Test-Path -LiteralPath "$target/.runtime/python/python.exe") -or
    !(Test-Path -LiteralPath "$target/fixture-installed.txt")) {
    throw "Blocked removal did not preserve helpers. Logs: $qaRoot"
}
Remove-Item -LiteralPath $block
$removal = Start-Process -FilePath "$target/unins000.exe" -ArgumentList @(
    '/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART',('/LOG="{0}/uninstall.log"' -f $qaRoot)
) -WindowStyle Hidden -PassThru -Wait
if ($removal.ExitCode -ne 0 -or (Test-Path -LiteralPath $registryKey) -or
    (Test-Path -LiteralPath "$target/.runtime/python/python.exe") -or
    !(Test-Path -LiteralPath "$qaRoot/removal-ran-before-helper-deletion.txt")) {
    throw "Fixture removal failed (exit $($removal.ExitCode)). Logs: $qaRoot"
}
# A silent failure must retain helpers and return, without prompting/retrying.
Remove-Item -LiteralPath "$target/fixture-install-attempts.txt"
[IO.File]::WriteAllText($choices,"[setup]`r`nmode=fail-install`r`n")
$failedSetup = Start-Process -FilePath (Join-Path $qaRoot 'D2R-PCVR-Setup-0.0.1.exe') -ArgumentList @(
    '/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART',('/DIR="{0}"' -f $target), ('/CONFIG="{0}"' -f $choices), ('/LOG="{0}/failed-setup.log"' -f $qaRoot)
) -WindowStyle Hidden -PassThru -Wait
if ($failedSetup.ExitCode -ne 1 -or !(Test-Path -LiteralPath $registryKey) -or
    !(Test-Path -LiteralPath "$target/.runtime/python/python.exe") -or
    [IO.File]::ReadAllText("$target/fixture-install-attempts.txt") -ne '1') {
    throw "Silent failure did not retain helpers and stop after one attempt. Logs: $qaRoot"
}
$failedRemoval = Start-Process -FilePath "$target/unins000.exe" -ArgumentList @(
    '/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART',('/LOG="{0}/failed-setup-removal.log"' -f $qaRoot)
) -WindowStyle Hidden -PassThru -Wait
if ($failedRemoval.ExitCode -ne 0 -or (Test-Path -LiteralPath $registryKey)) {
    throw "Failed setup fixture could not be removed. Logs: $qaRoot"
}
[PSCustomObject]@{Result='Passed'; Scope='Readiness before Install, Inno file install, Windows Apps registration, blocked removal retains helpers, successful removal, silent install failure stops after one attempt and retains helpers';
    Backend='Stub only; real game/Steam transactions are covered separately'; Logs=$qaRoot} | ConvertTo-Json
