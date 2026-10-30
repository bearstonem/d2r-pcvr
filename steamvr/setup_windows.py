#!/usr/bin/env python3
"""Player-facing Windows setup; all prompts run before any game/config writes."""
import argparse
import csv
import hashlib
import json
import os
import hdr_check
from pathlib import Path, PurePosixPath
import subprocess
import sys

import install
import mod_identity
import steam_config
from game_install import validate_game, battlenet_location, STEAM_VERSION, BATTLENET_VERSION

ROOT = Path(__file__).resolve().parent.parent


def choose(title, options, default=0):
    print('\n' + title)
    for index, option in enumerate(options):
        print(f'  {index + 1}. {option}' + (' [default]' if index == default else ''))
    while True:
        answer = input('Choose a number (Enter for default): ').strip()
        if not answer:
            return default
        if answer.isdecimal() and 1 <= int(answer) <= len(options):
            return int(answer) - 1
        print('Enter one of the numbers above.')


def folder(title, default=None, required=None):
    while True:
        answer = input(f'{title}' + (f' [{default}]' if default else '') + ': ').strip().strip('"')
        if not answer and default is not None:
            path = default
        elif not answer:
            continue
        else:
            path = Path(os.path.expandvars(answer)).expanduser().resolve()
        if path.is_dir() and (required is None or (path / required).is_file()):
            return path
        print(f'Folder not found' + (f' or missing {required}' if required else '') + '. Please try again.')


def steam_location():
    import winreg
    for hive, key, name in [(winreg.HKEY_CURRENT_USER, r'Software\Valve\Steam', 'SteamPath'),
                            (winreg.HKEY_LOCAL_MACHINE, r'SOFTWARE\WOW6432Node\Valve\Steam', 'InstallPath')]:
        try:
            with winreg.OpenKey(hive, key) as handle:
                candidate = Path(winreg.QueryValueEx(handle, name)[0])
            if (candidate / 'steam.exe').is_file():
                return candidate.resolve()
        except OSError:
            pass
    return None


def libraries(steam):
    result = [steam]
    source = steam / 'steamapps/libraryfolders.vdf'
    if source.is_file():
        tree = steam_config.text_value(steam_config.parse_text(source.read_text(encoding='utf-8-sig')), 'libraryfolders')
        for entry in tree or []:
            if entry.key.isdecimal():
                value = steam_config.text_value(entry.value, 'path') if isinstance(entry.value, list) else entry.value
                if isinstance(value, str):
                    path = Path(value)
                    if path.is_dir() and path not in result:
                        result.append(path)
    return result


def app_folder(library_roots, app_id):
    for library in library_roots:
        source = library / f'steamapps/appmanifest_{app_id}.acf'
        if source.is_file():
            name = steam_config.text_value(steam_config.parse_text(source.read_text(encoding='utf-8-sig')), 'AppState', 'installdir')
            if isinstance(name, str):
                candidate = (library / 'steamapps/common' / name).resolve()
                common = (library / 'steamapps/common').resolve()
                if candidate.is_relative_to(common) and candidate.is_dir():
                    return candidate
    return None


def profiles(steam):
    result = []
    for profile in sorted((steam / 'userdata').glob('*')):
        source = profile / 'config/localconfig.vdf'
        if profile.name.isdecimal() and profile.name != '0' and source.is_file():
            tree = steam_config.parse_text(source.read_text(encoding='utf-8-sig'))
            name = steam_config.text_value(tree, 'UserLocalConfigStore', 'friends', 'PersonaName')
            result.append((profile, name if isinstance(name, str) else 'Steam account'))
    return result


def saved_games():
    # SHGetKnownFolderPath handles redirected Saved Games folders on Windows.
    import ctypes
    import uuid
    guid = (ctypes.c_ubyte * 16).from_buffer_copy(uuid.UUID('4c5c32ff-bb9d-43b0-b5b4-2d72e54eaaa4').bytes_le)
    value = ctypes.c_wchar_p()
    shell = ctypes.WinDLL('shell32')
    shell.SHGetKnownFolderPath.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_void_p, ctypes.POINTER(ctypes.c_wchar_p)]
    shell.SHGetKnownFolderPath.restype = ctypes.c_long
    if shell.SHGetKnownFolderPath(ctypes.byref(guid), 0, None, ctypes.byref(value)) == 0:
        try:
            return Path(value.value) / 'Diablo II Resurrected'
        finally:
            ole = ctypes.WinDLL('ole32')
            ole.CoTaskMemFree.argtypes = [ctypes.c_void_p]
            ole.CoTaskMemFree(ctypes.cast(value, ctypes.c_void_p))
    return Path.home() / 'Saved Games/Diablo II Resurrected'


def require_closed():
    rows = csv.reader(subprocess.check_output(['tasklist', '/FO', 'CSV', '/NH'], text=True,
        creationflags=0x08000000 if os.name == 'nt' else 0).splitlines())
    if any(row and row[0].lower() in ('steam.exe', 'd2r.exe', 'd2rloader.exe') for row in rows):
        raise ValueError('Save & Exit D2R, then choose Steam > Exit on the PC. Do not just close its window.')


def wait_for_closed():
    while True:
        try:
            require_closed()
            return
        except ValueError as error:
            print('\n' + str(error))
            input('Press Enter after both are fully closed (Ctrl+C to cancel): ')


def verify_release(root):
    source = root / 'release-manifest.json'
    if not source.is_file():
        raise ValueError('Extract the complete Windows release ZIP first, then run Install-Windows.cmd from that folder.')
    manifest = json.loads(source.read_text(encoding='utf-8'))
    if manifest.get('platform') != 'native-windows':
        raise ValueError('This setup needs the native Windows release ZIP.')
    hashes = manifest['sha256']
    required = {'bin/d2rl-pcvr.dll', 'bin/openxr_loader.dll', 'steamvr/install.py', 'steamvr/mod_identity.py',
                'steamvr/setup_windows.py', 'steamvr/steam_config.py', 'steamvr/launch_windows.py', 'steamvr/prepare_dev_scenarios.py',
                'steamvr/game_install.py', 'steamvr/game_assets.py', 'steamvr/casc_assets.py',
                'steamvr/prepare_sewer_backdrop.py',
                'Uninstall-Windows.cmd', 'steamvr/uninstall_windows.py'}
    if not required.issubset(hashes):
        raise ValueError('Incomplete Windows setup package')
    for name, digest in hashes.items():
        relative = PurePosixPath(name.replace('\\', '/'))
        if relative.is_absolute() or '..' in relative.parts or ':' in name:
            raise ValueError('Invalid path in release manifest')
        path = root / relative
        if not path.resolve().is_relative_to(root.resolve()) or not path.is_file() or hashlib.sha256(path.read_bytes()).hexdigest() != digest:
            raise ValueError(f'Release file is missing or changed: {name}. Extract a fresh ZIP.')
    return manifest


def launch_options(python, wrapper, runtime):
    parts = [str(python), str(wrapper)]
    if runtime is not None:
        parts.append('--frame-openxr-runtime=' + str(runtime))
    if any('"' in part or '\n' in part or '\r' in part or '%' in part for part in parts):
        raise ValueError('Move the extracted mod to a folder without quotes, percent signs or line breaks.')
    return ' '.join('"' + part + '"' for part in parts) + ' %command%'


def preflight_steam(profile, steam, game, options, connections, **routing):
    """Validate proposed edits without writing, using the current profile bytes."""
    old_options = None
    if options is not None:
        source = (profile / 'config/localconfig.vdf').read_text(encoding='utf-8')
        old_options = steam_config.text_value(steam_config.parse_text(source.lstrip('\ufeff')),
            'UserLocalConfigStore', 'Software', 'Valve', 'Steam', 'apps', steam_config.APP_ID, 'LaunchOptions')
        steam_config.set_launch_options(source, options)
    shortcuts = profile / 'config/shortcuts.vdf'
    steam_config.update_shortcuts(shortcuts.read_bytes() if shortcuts.exists() else None,
                                  steam, game, connections, **routing)
    return old_options


def run_installer(command, root, hide_console=False):
    if not hide_console:
        return subprocess.run(command, check=True)
    # CREATE_NO_WINDOW drops implicit standard handles, including the pipes
    # inherited from Inno Setup. Connect the child explicitly and relay its
    # output so the wizard receives progress and the actual failure reason.
    log = root / 'setup-install.log'
    environment = os.environ.copy()
    environment['PYTHONIOENCODING'] = 'utf-8'
    last_line = ''
    with log.open('w', encoding='utf-8') as output:
        with subprocess.Popen([command[0], '-u', *command[1:]],
                stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                text=True, encoding='utf-8', errors='replace', env=environment,
                creationflags=0x08000000 if os.name == 'nt' else 0) as process:
            for line in process.stdout:
                output.write(line)
                output.flush()
                if sys.stdout is not None:
                    try:
                        print(line, end='', flush=True)
                    except UnicodeEncodeError:
                        print(line.encode('ascii', errors='backslashreplace').decode('ascii'), end='', flush=True)
                if line.strip():
                    last_line = line.strip()
            result = process.wait()
        output.write(f'Installer exit code: {result} (0x{result & 0xffffffff:08x})\n')
    if result:
        # Keep the diagnosis and recovery steps together, including the reminder
        # to re-enable protection. Paths and security guidance can exceed 800 chars.
        detail = last_line[:4096] or 'The installer produced no diagnostic output.'
        raise ValueError(f'Mod installation failed (exit code {result}): {detail} Full output: {log}')


def apply_setup(manifest, store, version, headset, connections, steam, game, profile,
                saves, runtime, mode, root=None, on_steam_configured=None, hide_console=False):
    """Shared console/graphical execution; callers collect and confirm choices first."""
    root = Path(root) if root is not None else ROOT
    require_closed()
    hdr = hdr_check.check()
    print('Windows HDR check: ' + hdr['hdr_status'], flush=True)
    if hdr['hdr_warning']:
        print(hdr['hdr_warning'], flush=True)
    routing = {} if store == 'steam' else {'store': store, 'python': Path(sys.executable).resolve(),
        'wrapper': root / 'steamvr/launch_windows.py', 'runtime': runtime}
    options = launch_options(Path(sys.executable).resolve(), root / 'steamvr/launch_windows.py', runtime) if store == 'steam' else None
    # Steam may have flushed new bytes while closing after the preview.
    old_options = preflight_steam(profile, steam, game, options, connections, **routing)
    preview = steam_config.update_shortcuts(
        (profile / 'config/shortcuts.vdf').read_bytes() if (profile / 'config/shortcuts.vdf').exists() else None,
        steam, game, connections, **routing)
    steam_config.artwork_updates(preview, profile, steam, game, connections, root / 'artwork', **routing)
    if mode != 2:
        command = [sys.executable, str(root / 'steamvr/install.py'), '--game', str(game),
                   '--saves', str(saves), '--plugin', str(root / 'bin/d2rl-pcvr.dll'),
                   '--openxr-loader', str(root / 'bin/openxr_loader.dll'), '--deps', str(root / 'deps'),
                   '--backup-dir', str(root / 'setup-backups/game'), '--download-deps', '--store', store]
        if store == 'battlenet':
            command += ['--experimental-battlenet', '--casclib', str(root / 'bin/CascLib.dll')]
        if mode == 0:
            command.append('--preserve-settings')
        run_installer(command, root, hide_console=hide_console)
    require_closed()
    backup = steam_config.configure(profile, steam, game, options, connections,
                                   root / 'setup-backups', require_closed, artwork_root=root / 'artwork', **routing)
    if on_steam_configured is not None:
        on_steam_configured()
    record = install.read_record(game)
    record.update({'game': str(game), 'store': store, 'saves': str(saves)})
    state = record.setdefault('steam_profiles', {}).setdefault(str(profile.resolve()), {})
    if store == 'steam' and 'previous_launch_options' not in state:
        state['previous_launch_options'] = '' if steam_config.is_vr_launch_options(old_options) else (old_options or '')
    state.update({'steam': str(steam), 'installed_launch_options': options})
    require_closed()
    install.write_record(game, record)
    result = {'release': manifest.get('release_tag'), 'store': store, 'game_version': version,
              'experimental': store == 'battlenet', 'headset': headset, 'connections': connections,
              'game': str(game), 'steam_profile': profile.name, 'steam_backup': str(backup),
              'game_backups': str(root / 'setup-backups/game'), 'launch_options': options}
    (root / 'setup-result.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true', help='Check extracted release files without installing')
    parser.add_argument('--experimental-battlenet', action='store_true', help='Legacy preview entry point; Battle.net is offered automatically when included in the ZIP')
    args = parser.parse_args()
    manifest = verify_release(ROOT)
    if args.check:
        print('Windows release files verified. No game or Steam configuration was changed.')
        return
    if os.name != 'nt':
        raise ValueError('Run Install-Windows.cmd on your Windows PC.')
    battlenet_available = 'bin/CascLib.dll' in manifest.get('sha256', {})
    if args.experimental_battlenet and not battlenet_available:
        raise ValueError('This ZIP does not include the experimental Battle.net asset reader. Use a Battle.net-capable package.')
    print('\nD2R PCVR - Windows setup')
    print('This mod requires your separately purchased, legitimate copy of Diablo II: Resurrected.')
    print('No game assets are supplied. Use a local OFFLINE character while playing this preview.')
    print('Launch stock D2R once and finish its account prompts before continuing.')
    print('Keep this extracted mod folder in a permanent location; Steam will use it to launch.')
    store = 'steam'
    if battlenet_available:
        store = ['steam', 'battlenet'][choose('Which game installation?', [f'Steam ({STEAM_VERSION})', f'Battle.net ({BATTLENET_VERSION}, experimental)'])]
        if store == 'battlenet':
            print('Battle.net compatibility is under validation. Use a local OFFLINE character.')
    else:
        print(f'This package supports the Steam edition ({STEAM_VERSION}) only.')
    headset = ['Steam Frame', 'Quest 3', 'Quest 2', 'Other SteamVR headset'][choose('Which headset will you use?', ['Steam Frame', 'Quest 3', 'Quest 2', 'Other SteamVR headset'])]
    connections = ['steamvr']
    if headset in ('Quest 2', 'Quest 3'):
        if headset == 'Quest 2':
            print('Quest 2 uses the Quest controls; compatibility is not yet confirmed.')
        connection = choose('How will the Quest connect to this PC?', ['Steam Link / SteamVR', 'Virtual Desktop / VDXR', 'Both (create two shortcuts)'])
        connections = [['steamvr'], ['vdxr'], ['steamvr', 'vdxr']][connection]
    steam = folder('Steam installation folder', steam_location(), 'steam.exe')
    roots = libraries(steam)
    default_game = app_folder(roots, steam_config.APP_ID) if store == 'steam' else battlenet_location()
    game = folder('D2R game folder (' + ('Steam > Properties > Installed Files > Browse' if store == 'steam'
                  else 'Battle.net > D2R > Show in Explorer') + ')', default_game, 'D2R.exe')
    _, version = validate_game(game, store, experimental_battlenet=store == 'battlenet')
    runtime = None
    if 'steamvr' in connections:
        runtime_folder = folder('SteamVR installation folder', app_folder(roots, '250820'), 'steamxr_win64.json')
        runtime = runtime_folder / 'steamxr_win64.json'
    if 'vdxr' in connections:
        from launch_windows import vdxr_manifest
        if not vdxr_manifest().is_file():
            raise ValueError('Install Virtual Desktop Streamer in its default Windows location first: https://www.vrdesktop.net/ . Then rerun setup.')
        print('Use Virtual Desktop on Quest with its Windows Streamer. Keep SteamVR closed for VDXR.')
    accounts = profiles(steam)
    if not accounts:
        raise ValueError('No Steam profile found. Sign into desktop Steam once, then rerun setup.')
    selected = choose('Add shortcuts for which Steam account?', [f'{name} (local ID {profile.name})' for profile, name in accounts]) if len(accounts) > 1 else 0
    profile = accounts[selected][0]
    saves = folder('D2R saved games folder', saved_games(), 'Settings.json')
    found = mod_identity.survey(game, [ROOT / 'setup-backups/game', ROOT / 'build-frame/backups'])
    if found.other:
        raise ValueError(found.message + ' No game or Steam configuration was changed.')
    existing = found.ours
    mode = choose('How should setup install the mod?', ['Update / repair and keep calibration and video settings', 'Full install with recommended settings (resets calibration)', 'Only configure Steam shortcuts (keep installed mod)']) if existing else 1
    if mode == 2 and not all((game / name).is_file() for name in ('D2RLoader.exe', mod_identity.RESHADE, mod_identity.OPENXR_LOADER, mod_identity.PLUGIN, mod_identity.SETTINGS)):
        raise ValueError('The existing mod installation is incomplete. Rerun setup and choose Update / repair.')
    routing = {} if store == 'steam' else {'store': store, 'python': Path(sys.executable).resolve(),
                                          'wrapper': ROOT / 'steamvr/launch_windows.py', 'runtime': runtime}
    options = launch_options(Path(sys.executable).resolve(), ROOT / 'steamvr/launch_windows.py', runtime) if store == 'steam' else None
    old_options = preflight_steam(profile, steam, game, options, connections, **routing)
    print('\nReady to set up:')
    hdr = hdr_check.check()
    if hdr['hdr_warning']:
        print(hdr['hdr_warning'])
    print(f'  Edition: {"Steam" if store == "steam" else "Battle.net (experimental)"}, D2R {version}')
    print(f'  Headset: {headset}\n  Game: {game}\n  Steam account: {accounts[selected][1]} (local ID {profile.name})')
    specs = steam_config.shortcut_specs(steam, game, **routing)
    print('  Shortcuts: ' + ', '.join(specs[connection][0] for connection in connections))
    print('  Original launcher Play: stock desktop game. VR shortcuts: local offline play.')
    print('  Game files, saves and Steam configuration are backed up before replacement.')
    if old_options and old_options != options:
        print('  Existing D2R launch options will be replaced; a backup will retain them.')
    preview = steam_config.update_shortcuts(
        (profile / 'config/shortcuts.vdf').read_bytes() if (profile / 'config/shortcuts.vdf').exists() else None,
        steam, game, connections, **routing)
    steam_config.artwork_updates(preview, profile, steam, game, connections, ROOT / 'artwork', **routing)
    if mode != 2:
        print('  Required loader / ReShade components are downloaded from their official sites and checksum-checked.')
    if choose('Continue with this setup?', ['Install and configure shortcuts', 'Cancel']) == 1:
        print('Setup cancelled. No game or Steam configuration was changed.')
        return
    wait_for_closed()
    apply_setup(manifest, store, version, headset, connections, steam, game, profile, saves, runtime, mode)
    print('\nSetup complete. Steam library shortcuts have been created.')
    print(f'Backups: {ROOT / "setup-backups"}')
    print('Start Steam, connect your headset and play the matching VR shortcut using an OFFLINE character.')
    print('Normal launcher Play stays stock. For multiplayer, fully exit VR and use normal Play.')
    print('First VR load asks for body calibration. Controls: CONTROLS.md. Help: INSTALL-WINDOWS.md.')
    print('To remove this mod later, run Uninstall-Windows.cmd. Keep setup-backups and the extracted folder.')
    if choose('Open Steam now?', ['Open Steam', 'Later']) == 0:
        subprocess.Popen([str(steam / 'steam.exe')], cwd=steam)


if __name__ == '__main__':
    try:
        main()
    except (KeyboardInterrupt, EOFError):
        print('\nSetup cancelled. Any completed installation backups remain in setup-backups.')
        raise SystemExit(1)
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        print(f'\nSetup could not finish: {error}')
        print('Keep setup-backups if it exists. See INSTALL-WINDOWS.md and Windows Security Protection History if a required file was blocked.')
        raise SystemExit(1)
