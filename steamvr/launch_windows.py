#!/usr/bin/env python3
"""Native Windows stock/VR launches and explicit Battle.net offline previews."""
import ntpath
import os
import base64
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import time

from mod_identity import MOD_NAME

RUNTIME_OPTION = '--frame-openxr-runtime='
VDXR_OPTION = '--frame-vdxr'
BATTLENET_OPTION = '--battlenet-game='
EXPERIMENTAL_OPTION = '--experimental-battlenet'
DESKTOP_REQUEST = '--frame-desktop-request='
DEV_OPTION = '--frame-dev'


def battlenet_game(arguments):
    games = [arg[len(BATTLENET_OPTION):] for arg in arguments if arg.startswith(BATTLENET_OPTION)]
    if len(games) > 1:
        raise SystemExit('Expected at most one --battlenet-game=PATH')
    return Path(games[0]).resolve() if games else None


def prepare(arguments):
    vr = '--frame-vr' in arguments
    developer = DEV_OPTION in arguments
    if developer and not vr:
        raise SystemExit('--frame-dev requires --frame-vr and an offline test character.')
    game = battlenet_game(arguments)
    if game is not None:
        from game_install import validate_game
        if not vr:
            raise SystemExit('Battle.net preview shortcuts require --frame-vr')
        if any(arg not in ('--frame-vr', VDXR_OPTION, EXPERIMENTAL_OPTION, DEV_OPTION)
               and not arg.startswith((BATTLENET_OPTION, RUNTIME_OPTION)) for arg in arguments):
            raise SystemExit('Unexpected argument in Battle.net preview shortcut')
        try:
            validate_game(game, 'battlenet', EXPERIMENTAL_OPTION in arguments)
        except ValueError as error:
            raise SystemExit(str(error)) from error
        loader = game / 'D2RLoader.exe'
        if not loader.is_file():
            raise SystemExit('D2RLoader is not installed beside the Battle.net D2R.exe')
        # Wrapper-only flags are never passed to Blizzard's executable or loader. D2RLoader runs this mod.
        if developer:
            from prepare_dev_scenarios import stage, MOD
            stage(game, experimental_battlenet=EXPERIMENTAL_OPTION in arguments)
        return [str(loader), '-mod', MOD if developer else MOD_NAME], str(game)
    command = [arg for arg in arguments
               if arg not in ('--frame-vr', VDXR_OPTION, DEV_OPTION) and not arg.startswith(RUNTIME_OPTION)]
    cwd = None
    if not command:
        raise SystemExit('Expected the Steam launch command containing D2R.exe')
    if vr:
        found = False
        for index, arg in enumerate(command):
            if ntpath.basename(arg).lower() == 'd2r.exe':
                folder = ntpath.dirname(arg)
                loader = ntpath.join(folder, 'D2RLoader.exe')
                if not Path(loader).is_file():
                    raise SystemExit('D2RLoader is not installed beside D2R.exe')
                command[index] = loader
                if developer:
                    from prepare_dev_scenarios import stage, MOD
                    mods = [i for i, item in enumerate(command) if item.lower() == '-mod']
                    if len(mods) > 1 or any(i + 1 >= len(command) or command[i + 1] != MOD for i in mods):
                        raise SystemExit('--frame-dev cannot be combined with a different -mod selection.')
                    if any(item.lower() in ('-tcp', '-join', '-server') or item.lower().startswith(('-mod=', '-savepath')) for item in command):
                        raise SystemExit('--frame-dev uses its own offline mod and save path.')
                    stage(Path(folder))
                # D2RLoader runs this mod (mods\D2R-PCVR), unless the launch options already name one.
                if not any(item.lower() == '-mod' for item in command):
                    command[index + 1:index + 1] = ['-mod', MOD if developer else MOD_NAME]
                cwd = folder
                found = True
                break
        if not found:
            raise SystemExit('Expected the Steam launch command containing D2R.exe')
    return command, cwd


def vdxr_manifest():
    return Path(os.environ.get('ProgramFiles', 'C:/Program Files')) / 'Virtual Desktop Streamer/OpenXR/virtualdesktop-openxr.json'


def launch_environment(arguments):
    """Allow a per-game runtime choice without changing other PCVR apps."""
    if '--frame-vr' not in arguments:
        return None
    runtimes = [arg[len(RUNTIME_OPTION):] for arg in arguments
                if arg.startswith(RUNTIME_OPTION)]
    if len(runtimes) > 1:
        raise SystemExit('Expected at most one --frame-openxr-runtime=PATH manifest')
    # The explicit launch flag overrides the normal SteamVR launch-option
    # manifest. Stock Play ignores it, and a missing VDXR install fails closed.
    environment = os.environ.copy()
    # Overlay UI settings and this routing hint alone do not stop Steam's early
    # injection. The desktop handoff below also starts a clean launcher chain.
    for key in list(environment):
        if key.lower() == 'steamoverlaygameid':
            del environment[key]
    if battlenet_game(arguments) is not None:
        # Non-Steam shortcuts inherit Steam IDs too. They must not select Steam
        # ownership/authentication for a legitimately owned Battle.net copy.
        for key in list(environment):
            if key.lower() in ('steamappid', 'steamgameid'):
                del environment[key]
    else:
        # A direct non-Steam library shortcut inherits its own synthetic IDs.
        # D2R's Steam edition must initialize against the owned game's app ID.
        for key in list(environment):
            if key.lower() in ('steamappid', 'steamgameid'):
                del environment[key]
        environment['SteamAppId'] = environment['SteamGameId'] = '2536520'
    if VDXR_OPTION in arguments:
        runtime = vdxr_manifest()
    elif runtimes:
        runtime = Path(runtimes[0])
    else:
        return environment
    if not runtime.is_file():
        raise SystemExit(f'OpenXR runtime manifest was not found: {runtime}')
    environment['XR_RUNTIME_JSON'] = str(runtime.resolve())
    return environment


def launch_log(cwd, message):
    if cwd is None:
        return
    try:
        path = Path(cwd) / 'd2rloader/logs/pcvr-launch.log'
        path.parent.mkdir(parents=True, exist_ok=True)
        with path.open('a', encoding='utf-8') as output:
            output.write(time.strftime('%Y-%m-%d %H:%M:%S ') + message + '\n')
    except OSError:
        pass  # A read-only log directory must not prevent a game launch.


def publish_status(path, **status):
    temporary = path.with_suffix('.tmp')
    temporary.write_text(json.dumps(status), encoding='utf-8')
    os.replace(temporary, path)


def desktop_child(request_path):
    status_path = request_path.with_name('status.json')
    cwd = None
    try:
        request = json.loads(request_path.read_text(encoding='utf-8'))
        arguments = request['arguments']
        if '--frame-vr' not in arguments or battlenet_game(arguments) is not None:
            raise ValueError('Desktop launch handoff requires the Steam VR edition.')
        command, cwd = prepare(arguments)
        environment = launch_environment(arguments)
        if request.get('runtime'):
            environment['XR_RUNTIME_JSON'] = request['runtime']
        publish_status(status_path, state='running', pid=os.getpid())
        # This helper belongs to Explorer, not Steam's injected launcher chain.
        # Wait for exactly one game; never retry or terminate an active game.
        process = subprocess.Popen(command, cwd=cwd, env=environment)
        launch_log(cwd, f'Desktop handoff created loader pid={process.pid}')
        result = process.wait()
        launch_log(cwd, f'Loader exited code=0x{result & 0xffffffff:08x}')
        publish_status(status_path, state='finished', result=result)
        return result
    except (OSError, ValueError, KeyError, SystemExit) as error:
        launch_log(cwd, f'Desktop handoff failed: {error}')
        publish_status(status_path, state='finished', result=1, error=str(error))
        return 1


def wait_for_desktop(status_path, timeout=20):
    # A real process handle prevents a crashed helper or a reused PID from
    # leaving the Steam shortcut stuck as Running. Never kill the process.
    import ctypes
    from ctypes import wintypes
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    kernel.OpenProcess.restype = wintypes.HANDLE
    kernel.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
    kernel.WaitForSingleObject.restype = wintypes.DWORD
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    kernel.CloseHandle.restype = wintypes.BOOL
    handle = None
    deadline = time.monotonic() + timeout
    try:
        while True:
            status = json.loads(status_path.read_text(encoding='utf-8')) if status_path.is_file() else {}
            if status.get('state') == 'finished':
                if handle:
                    kernel.WaitForSingleObject(handle, 5000)
                if status.get('error'):
                    raise OSError(status['error'])
                return int(status['result'])
            if status.get('state') == 'running':
                if not handle:
                    handle = kernel.OpenProcess(0x100000, False, int(status['pid']))  # SYNCHRONIZE
                    if not handle:
                        # It may have finished between publishing and opening.
                        final = json.loads(status_path.read_text(encoding='utf-8'))
                        if final.get('state') == 'finished':
                            continue
                        raise OSError('Desktop launch helper exited without a result.')
                if kernel.WaitForSingleObject(handle, 100) != 258:  # WAIT_TIMEOUT
                    final = json.loads(status_path.read_text(encoding='utf-8'))
                    if final.get('state') != 'finished':
                        raise OSError('Desktop launch helper exited without a result.')
                continue
            if time.monotonic() >= deadline:
                raise OSError('Windows desktop did not acknowledge the VR launch.')
            time.sleep(.05)
    finally:
        if handle:
            kernel.CloseHandle(handle)


def launch_from_desktop(arguments, cwd, environment):
    launch_log(cwd, 'Requesting Windows desktop handoff for Steam VR loader')
    try:
        with tempfile.TemporaryDirectory(prefix='d2r-vr-launch-') as temporary:
            request = Path(temporary) / 'request.json'
            request.write_text(json.dumps({'arguments': arguments,
                'runtime': environment.get('XR_RUNTIME_JSON')}), encoding='utf-8')
            payload = base64.b64encode(json.dumps({
                'executable': str(Path(sys.executable).resolve()),
                'arguments': subprocess.list2cmdline([str(Path(__file__).resolve()), DESKTOP_REQUEST + str(request)]),
                'directory': str(Path(__file__).resolve().parent),
            }).encode('utf-8')).decode('ascii')
            # All paths/arguments travel as JSON data, never as shell syntax.
            script = """$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
try {
$launch = [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String('PAYLOAD')) | ConvertFrom-Json
$shell = New-Object -ComObject Shell.Application
$desktopHwnd = 0
$desktop = $shell.Windows().FindWindowSW(0,0,8,[ref]$desktopHwnd,1)
if (!$desktop) { throw 'Windows Explorer desktop is unavailable.' }
$desktop.Document.Application.ShellExecute($launch.executable,$launch.arguments,$launch.directory,'open',0)
} catch { [Console]::Error.WriteLine($_.Exception.Message); exit 1 }
""".replace('PAYLOAD', payload)
            powershell = Path(os.environ.get('SystemRoot', 'C:/Windows')) / 'System32/WindowsPowerShell/v1.0/powershell.exe'
            subprocess.run([str(powershell), '-NoLogo', '-NoProfile', '-NonInteractive', '-STA',
                '-EncodedCommand', base64.b64encode(script.encode('utf-16-le')).decode('ascii')],
                check=True, capture_output=True, timeout=20,
                creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
            return wait_for_desktop(request.with_name('status.json'))
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        if isinstance(error, subprocess.CalledProcessError):
            detail = (error.stderr or b'').decode('utf-8', errors='replace').strip()[:800]
            detail = detail or f'Windows desktop request exited with code {error.returncode}.'
        elif isinstance(error, subprocess.TimeoutExpired):
            detail = 'Windows desktop request timed out before acknowledging the VR launch.'
        else:
            detail = str(error)
        launch_log(cwd, f'Windows desktop launch failed: {detail}')
        print(f'VR launch failed: {detail}', file=sys.stderr)
        return 1


def main(arguments):
    requests = [arg for arg in arguments if arg.startswith(DESKTOP_REQUEST)]
    if requests:
        if len(arguments) != 1:
            raise SystemExit('Unexpected desktop handoff arguments.')
        return desktop_child(Path(requests[0][len(DESKTOP_REQUEST):]))
    command, cwd = prepare(arguments)
    environment = launch_environment(arguments)
    if os.name == 'nt' and '--frame-vr' in arguments and battlenet_game(arguments) is None:
        return launch_from_desktop(arguments, cwd, environment)
    return subprocess.run(command, cwd=cwd, env=environment).returncode


if __name__ == '__main__':
    raise SystemExit(main(sys.argv[1:]))
