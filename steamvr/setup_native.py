#!/usr/bin/env python3
"""Noninteractive backend for the D2R PCVR graphical Windows installer."""
import argparse
import configparser
import hashlib
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

import install
import setup_windows as setup
import steam_config
import uninstall_windows as uninstall
from game_install import storage_config, validate_game

ROOT = Path(__file__).resolve().parent.parent
STATE = 'managed-installations.json'
HEADSETS = ('Steam Frame', 'Quest 3', 'Quest 2', 'Other SteamVR headset')
MODES = {'repair': 0, 'full': 1, 'shortcuts': 2}


def atomic_json(path, value):
    temporary = path.with_suffix('.tmp')
    try:
        temporary.write_text(json.dumps(value, indent=2) + '\n', encoding='utf-8')
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def managed_state(root):
    path = root / STATE
    result = json.loads(path.read_text(encoding='utf-8')) if path.is_file() else {'installations': []}
    if not isinstance(result, dict) or not isinstance(result.get('installations'), list):
        raise ValueError('The managed installation record is invalid. Keep the backups and contact the maintainer.')
    for entry in result['installations']:
        if (not isinstance(entry, dict) or entry.get('store') not in ('steam', 'battlenet')
                or any(not isinstance(entry.get(key), str) or not entry[key] for key in ('game', 'steam', 'saves'))):
            raise ValueError('The managed installation record contains an invalid game entry.')
    return result


def read_choices(path):
    data = path.read_bytes()
    parser = configparser.ConfigParser(interpolation=None)
    parser.read_string(data.decode('utf-16' if data.startswith((b'\xff\xfe', b'\xfe\xff')) else 'utf-8-sig'))
    return dict(parser['setup'])


def write_report(path, section, values):
    parser = configparser.ConfigParser(interpolation=None)
    # Win32 profile APIs used by Inno Setup read UTF-16, single-line values.
    parser[section] = {key: str(value).replace('\r', ' ').replace('\n', ' ') for key, value in values.items()}
    output = io.StringIO()
    parser.write(output)
    path.write_text(output.getvalue(), encoding='utf-16')


def write_probe(path, root, steam=None, game=None, managed_root=None):
    steam = steam or setup.steam_location()
    libraries = setup.libraries(steam) if steam else []
    accounts = setup.profiles(steam) if steam else []
    manifest = setup.verify_release(root)
    values = {
        'steam': str(steam or ''), 'steam_game': str(setup.app_folder(libraries, steam_config.APP_ID) or ''),
        'battlenet_game': str(setup.battlenet_location() or ''),
        'steamvr': str(setup.app_folder(libraries, '250820') or ''), 'saves': str(setup.saved_games()),
        'battlenet_available': str(int('bin/CascLib.dll' in manifest['sha256'])),
        'profiles': str(len(accounts)),
        'backup_required': '0', 'backup_message': '', 'backup_path': '',
    }
    if game is not None:
        try:
            check_backups(game.resolve(), Path(managed_root or root))
        except (OSError, ValueError, KeyError) as error:
            values.update(backup_required='1', backup_message=str(error))
            try:
                recorded = install.read_record(game).get('snapshots', [])
                if recorded:
                    values['backup_path'] = recorded[0]
            except (OSError, ValueError, KeyError):
                pass  # Keep the original diagnostic if the receipt is unreadable.
    for index, (profile, name) in enumerate(accounts):
        values[f'profile_{index}'] = profile.name
        values[f'profile_name_{index}'] = name
    write_report(path, 'detected', values)


def validate_choices(values, root, managed_root=None):
    manifest = setup.verify_release(root)
    store, headset = values.get('store'), values.get('headset')
    if store not in ('steam', 'battlenet') or headset not in HEADSETS:
        raise ValueError('Choose a supported game edition and headset.')
    connections = values.get('connections', '').split(',')
    if (not connections or len(set(connections)) != len(connections)
            or any(name not in ('steamvr', 'vdxr') for name in connections)
            or headset not in ('Quest 2', 'Quest 3') and connections != ['steamvr']):
        raise ValueError('Choose SteamVR for Steam Frame, or SteamVR / Virtual Desktop for Quest.')
    if store == 'battlenet' and 'bin/CascLib.dll' not in manifest['sha256']:
        raise ValueError('This package does not include Battle.net support.')
    paths = {}
    for key in ('steam', 'game', 'saves'):
        if not values.get(key):
            raise ValueError(f'Select the {key} folder.')
        paths[key] = Path(values[key]).expanduser().resolve()
    steam, game, saves = paths['steam'], paths['game'], paths['saves']
    target = Path(managed_root or root).resolve()
    for path in paths.values():
        if path.is_relative_to(target) or target.is_relative_to(path):
            raise ValueError('The mod helper folder must be separate from the game, Steam and Saved Games folders.')
    if not (steam / 'steam.exe').is_file():
        raise ValueError('Select the Steam folder containing steam.exe.')
    _, version = validate_game(game, store, experimental_battlenet=store == 'battlenet')
    if not (saves / 'Settings.json').is_file():
        raise ValueError('Launch the original game once first, finish account prompts, and exit. Then select its Saved Games folder.')
    json.loads((saves / 'Settings.json').read_text(encoding='utf-8-sig'))
    accounts = {profile.name: profile for profile, _ in setup.profiles(steam)}
    profile = accounts.get(values.get('profile'))
    if profile is None:
        raise ValueError('Sign into Steam once and select a detected Steam account.')
    runtime = None
    if 'steamvr' in connections:
        runtime = Path(values.get('steamvr', '')) / 'steamxr_win64.json'
        if not runtime.is_file():
            raise ValueError('Install SteamVR, or select its folder containing steamxr_win64.json.')
        runtime = runtime.resolve()
    if 'vdxr' in connections:
        from launch_windows import vdxr_manifest
        if not vdxr_manifest().is_file():
            raise ValueError('Install Virtual Desktop Streamer first, then rerun setup. See https://www.vrdesktop.net/.')
    mode = MODES.get(values.get('mode'))
    if mode is None:
        raise ValueError('Choose Update / repair, Full install or Shortcuts only.')
    if mode == 2 and not all((game / name).is_file() for name in (
            'D2RLoader.exe', 'ReShade64.dll', 'openxr_loader.dll', 'd2rloader/plugins/d2rl-vrcam.dll',
            'd2rloader/plugins/d2r_vr.ini')):
        raise ValueError('Shortcuts only requires a complete installed mod. Choose Update / repair instead.')
    options = setup.launch_options(target / '.runtime/python/python.exe', target / 'steamvr/launch_windows.py', runtime) if store == 'steam' else None
    routing = {} if store == 'steam' else {'store': store, 'python': target / '.runtime/python/python.exe',
                                          'wrapper': target / 'steamvr/launch_windows.py', 'runtime': runtime}
    setup.preflight_steam(profile, steam, game, options, connections, **routing)
    legacy = Path(values['backups']).expanduser().resolve() if values.get('backups') else None
    check_backups(game, target, legacy)
    managed_state(target)
    return dict(manifest=manifest, store=store, version=version, headset=headset, connections=connections,
                steam=steam, game=game, profile=profile, saves=saves, runtime=runtime, mode=mode)


def check_backups(game, root, legacy=None):
    """Fail before helper files are copied if an existing install cannot be removed."""
    record = install.read_record(game)
    if record.get('snapshots') or (game / uninstall.PLUGIN).is_file():
        roots = [root / 'setup-backups/game'] + [Path(name).parent for name in record.get('snapshots', [])]
        if legacy is not None:
            # Accept the old extracted ZIP folder as well as a snapshot/parent.
            # Lookup stays bounded; manifests must still match this game.
            roots.extend([legacy, legacy / 'setup-backups/game', legacy / 'build-frame/backups', legacy / 'game'])
        chain = uninstall.snapshots(game, record, roots)
        uninstall.restore_plan(game, chain)
        return chain
    return []


def validate_backups(values, root):
    if not values.get('game') or not values.get('backups'):
        raise ValueError('Select the original extracted mod folder or its backup folder.')
    legacy = Path(values['backups']).expanduser().resolve()
    if not legacy.is_dir():
        raise ValueError('The selected backup folder does not exist. Check the Recycle Bin if it was deleted.')
    return check_backups(Path(values['game']).expanduser().resolve(), root, legacy)


def runtime_check(values):
    from launch_windows import vdxr_manifest
    connections = values.get('connections', '').split(',')
    if not connections or any(name not in ('steamvr', 'vdxr') for name in connections):
        return False, 'Choose SteamVR or Virtual Desktop / VDXR.'
    messages, ready = [], True
    for connection in connections:
        path = (Path(values.get('steamvr', '')) / 'steamxr_win64.json'
                if connection == 'steamvr' else vdxr_manifest())
        name = 'SteamVR' if connection == 'steamvr' else 'Virtual Desktop / VDXR'
        try:
            with path.open('rb') as source:
                source.read(1)
            messages.append(f'{name}: available')
        except OSError as error:
            ready = False
            messages.append(f'{name}: cannot read {path} ({error})')
    return ready, ' | '.join(messages)


def readiness(values, root, managed_root=None):
    result, closed = {}, False
    try:
        running = setup.running_apps()
        result['steam'] = 'Running - choose Steam > Exit; closing its window leaves it running.' if 'steam.exe' in running else 'Closed'
        result['game'] = 'Running - Save & Exit D2R and close the game.' if running & {'d2r.exe', 'd2rloader.exe'} else 'Closed'
        closed = not running & {'steam.exe', 'd2r.exe', 'd2rloader.exe'}
    except (OSError, subprocess.SubprocessError) as error:
        result['steam'] = result['game'] = f'Could not check running applications: {error}'
    runtime_ready, result['runtime'] = runtime_check(values)
    selected = False
    try:
        validate_choices(values, root, managed_root)
        result['selections'] = 'Verified'
        selected = True
    except (OSError, ValueError, KeyError) as error:
        result['selections'] = str(error)
    result['ready'] = str(int(closed and runtime_ready and selected))
    return result


def selected_shortcuts(values, root):
    client, game = Path(values['steam']), Path(values['game'])
    routing = {} if values['store'] == 'steam' else dict(store='battlenet',
        python=root / '.runtime/python/python.exe', wrapper=root / 'steamvr/launch_windows.py',
        runtime=Path(values['steamvr']) / 'steamxr_win64.json' if 'steamvr' in values['connections'].split(',') else None)
    specs = steam_config.shortcut_specs(client, game, **routing)
    return [specs[name] for name in values['connections'].split(',')]


def inspect_installation(values, root):
    """Read-only startup diagnostics; never launch a game or edit Steam/security."""
    game = Path(values['game'])
    manifest = setup.verify_release(root)
    problems = []
    hashes = {'d2rloader/plugins/d2rl-vrcam.dll': 'bin/d2rl-vrcam.dll', 'openxr_loader.dll': 'bin/openxr_loader.dll'}
    for name in ('D2RLoader.exe', 'D2RCore.dll', 'D2RLoader.mpq', 'ReShade64.dll', 'openxr_loader.dll',
                 'd2rloader/plugins/d2rl-vrcam.dll', 'd2rloader/plugins/d2r_vr.ini'):
        path = game / name
        try:
            with path.open('rb') as source:
                expected = manifest['sha256'].get(hashes.get(name))
                if expected and hashlib.file_digest(source, 'sha256').hexdigest() != expected:
                    problems.append(f'Checksum mismatch: {path}')
                elif not expected:
                    source.read(1)
        except OSError as error:
            problems.append(f'Cannot read {path} ({error})')
    runtime_ready, runtime = runtime_check(values)
    specs = selected_shortcuts(values, root)
    names = [spec[0] for spec in specs]
    shortcut_problems = []
    profile = Path(values['steam']) / 'userdata' / values['profile']
    try:
        entries = steam_config.decode_binary((profile / 'config/shortcuts.vdf').read_bytes())['shortcuts']
        for name, options, openvr, executable, directory in specs:
            matching = [item for item in entries.values() if item.get('appname', item.get('AppName')) == name]
            expected = dict(Exe='"' + str(executable) + '"', StartDir='"' + str(directory) + '"',
                            LaunchOptions=options, OpenVR=openvr)
            if len(matching) != 1 or any(matching[0].get(key) != value for key, value in expected.items()):
                shortcut_problems.append(f'Missing, duplicated or outdated: {name}')
        if values['store'] == 'steam':
            local = steam_config.parse_text((profile / 'config/localconfig.vdf').read_text(encoding='utf-8-sig'))
            options = steam_config.text_value(local, 'UserLocalConfigStore', 'Software', 'Valve', 'Steam', 'apps', steam_config.APP_ID, 'LaunchOptions')
            runtime_path = Path(values['steamvr']) / 'steamxr_win64.json' if 'steamvr' in values['connections'].split(',') else None
            if options != setup.launch_options(root / '.runtime/python/python.exe', root / 'steamvr/launch_windows.py', runtime_path):
                shortcut_problems.append('Steam game launch options are outdated; run Update / repair.')
    except (OSError, ValueError, KeyError, TypeError, AttributeError) as error:
        shortcut_problems.append(f'Cannot check Steam shortcuts: {error}')
    return dict(ready=str(int(not problems and not shortcut_problems and runtime_ready)),
                files=' | '.join(problems) if problems else 'Required mod files are readable; packaged DLL hashes match.',
                runtime=runtime, shortcuts=' | '.join(shortcut_problems) if shortcut_problems else 'Verified',
                launch=' | '.join(names), game_logs=str(game / 'd2rloader/logs'))


def adopt_backups(game, root, legacy=None):
    """Move a ZIP installation's backup dependency into the managed folder."""
    record = install.read_record(game)
    copied = []
    destination_root = (root / 'setup-backups/game').resolve()
    for original, _ in check_backups(game, root, legacy):
        source = original.resolve()
        if not (source / 'manifest.json').is_file():
            raise ValueError(f'An existing installation backup is missing: {source}. Restore it before upgrading.')
        manifest = json.loads((source / 'manifest.json').read_text(encoding='utf-8'))
        if Path(manifest['game']).resolve() != game.resolve():
            raise ValueError('An existing backup belongs to a different game installation.')
        if source.is_relative_to(destination_root):
            copied.append(str(source))
            continue
        suffix = hashlib.sha256(str(source).encode()).hexdigest()[:10]
        destination = destination_root / (source.name + '-' + suffix)
        if not destination.is_dir():
            for item in [source, *source.rglob('*')]:
                if item.is_symlink() or getattr(item, 'is_junction', lambda: False)():
                    raise ValueError('Installation backups must not contain links to other folders.')
            shutil.copytree(source, destination)
        def contents(directory):
            result = {}
            for path in directory.rglob('*'):
                if path.is_symlink() or getattr(path, 'is_junction', lambda: False)():
                    raise ValueError('Installation backups must not contain links to other folders.')
                if path.is_file():
                    result[path.relative_to(directory).as_posix()] = hashlib.sha256(path.read_bytes()).hexdigest()
            return result
        if contents(destination) != contents(source):
            raise ValueError('A copied backup conflicts with the managed backup. Keep both and contact the maintainer.')
        copied.append(str(destination))
    if copied and copied != record.get('snapshots', []):
        setup.require_closed()
        record['game'] = str(game)
        record['snapshots'] = copied
        install.write_record(game, record)


def rollback_snapshot(game, saves, snapshot):
    manifest = json.loads((snapshot / 'manifest.json').read_text(encoding='utf-8'))
    if Path(manifest['game']).resolve() != game.resolve():
        raise ValueError('Rollback backup belongs to another game.')
    if Path(manifest['saves']).resolve() != saves.resolve():
        raise ValueError('Rollback backup belongs to another Saved Games folder.')
    return uninstall.restore_plan(game, [(snapshot, manifest)], restore_video=True)


def install_selected(values, root):
    setup.require_closed()
    selection = validate_choices(values, root)
    game = selection['game']
    state = managed_state(root)
    before_state = json.loads(json.dumps(state))
    print('Preserving existing installation backups...', flush=True)
    adopt_backups(game, root, Path(values['backups']).expanduser().resolve() if values.get('backups') else None)
    existing = [entry for entry in state['installations'] if Path(entry['game']).resolve() != game]
    entry = {'game': str(game), 'store': selection['store'], 'steam': str(selection['steam']),
             'saves': str(selection['saves']), 'status': 'installing', 'receipt': install.read_record(game)}
    state['installations'] = existing + [entry]
    originals = {}
    for path in (selection['profile'] / 'config/localconfig.vdf', selection['profile'] / 'config/shortcuts.vdf', game / install.RECORD):
        originals[path] = path.read_bytes() if path.is_file() else None
    backups = root / 'setup-backups/game'
    before = set(backups.glob('*/manifest.json'))
    atomic_json(root / STATE, state)
    committed_steam = {}
    def steam_committed():
        for path in originals:
            if path != game / install.RECORD:
                committed_steam[path] = path.read_bytes() if path.is_file() else None
    try:
        print('Installing the mod and configuring Steam shortcuts...', flush=True)
        result = setup.apply_setup(**selection, root=root, on_steam_configured=steam_committed, hide_console=True)
        entry.update(status='installed', receipt=install.read_record(game))
        atomic_json(root / STATE, state)
        print('Setup complete. Start Steam and use the matching VR shortcut with an offline character.', flush=True)
        return result
    except BaseException:
        print('Setup stopped. Restoring changes from this attempt...', flush=True)
        try:
            updates = {}
            for snapshot in sorted(set(backups.glob('*/manifest.json')) - before, reverse=True):
                updates.update(rollback_snapshot(game, selection['saves'], snapshot.parent))
            updates[game / install.RECORD] = originals[game / install.RECORD]
            for path, expected in committed_steam.items():
                current = path.read_bytes() if path.is_file() else None
                if current != expected:
                    raise ValueError('Steam configuration was edited after setup wrote it; that external edit was preserved.')
                updates[path] = originals[path]
            uninstall.apply_updates(updates, root / 'setup-backups', setup.require_closed)
            atomic_json(root / STATE, before_state)
            print('Changes from this attempt were restored. Existing characters and backups were kept.', flush=True)
        except BaseException as recovery_error:
            print(f'Automatic recovery could not finish: {recovery_error}. Keep this installation folder and its backups; rerun setup after closing Steam and D2R.', flush=True)
        raise


def removal_plan(root):
    updates = {}
    state = managed_state(root)
    for entry in state['installations']:
        game, steam = Path(entry['game']).resolve(), Path(entry['steam']).resolve()
        if game.is_relative_to(root.resolve()) or root.resolve().is_relative_to(game):
            raise ValueError('The managed game path overlaps the helper folder.')
        record = install.read_record(game) or entry.get('receipt', {})
        if (game / install.RECORD).exists() or (game / uninstall.PLUGIN).exists():
            detected, _ = storage_config(game)
            if detected != entry['store'] or not (game / 'D2R.exe').is_file():
                raise ValueError('The recorded game folder no longer matches its installation. Keep the backups.')
            chain = uninstall.snapshots(game, record, [root / 'setup-backups/game'])
            updates.update(uninstall.restore_plan(game, chain, restore_video=False))
        for profile, _ in setup.profiles(steam):
            profile_state = record.get('steam_profiles', {}).get(str(profile.resolve()), {})
            # Apply each edition sequentially to the same virtual Steam bytes.
            for path, payload in steam_config.uninstall_updates(profile, game, entry['store'], profile_state).items():
                if path not in updates:
                    updates[path] = payload
                else:
                    if updates[path] == payload:
                        continue
                    # Combine shortcut removals when both editions use one profile.
                    if path.name != 'shortcuts.vdf':
                        raise ValueError('Two managed installations conflict on the same Steam launch options.')
                    current = steam_config.decode_binary(updates[path])
                    retained = steam_config.decode_binary(payload)['shortcuts']
                    original = steam_config.decode_binary(path.read_bytes())['shortcuts']
                    removed = [item for item in original.values() if item not in retained.values()]
                    current['shortcuts'] = {str(i): item for i, item in enumerate(
                        item for item in current['shortcuts'].values() if item not in removed)}
                    updates[path] = steam_config.encode_binary(current)
    return updates


def remove_managed(root):
    setup.require_closed()
    updates = removal_plan(root)
    uninstall.apply_updates(updates, root / 'setup-backups', setup.require_closed)
    atomic_json(root / STATE, {'installations': []})
    print('The VR mod and its matching Steam shortcuts were removed. Characters, shared stash, current video settings and backups were kept.', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    action = parser.add_mutually_exclusive_group(required=True)
    action.add_argument('--probe', type=Path)
    action.add_argument('--validate', type=Path)
    action.add_argument('--validate-backups', type=Path)
    action.add_argument('--readiness', type=Path)
    action.add_argument('--inspect', type=Path)
    action.add_argument('--install', type=Path)
    action.add_argument('--uninstall', action='store_true')
    action.add_argument('--uninstall-check', action='store_true')
    action.add_argument('--check', action='store_true')
    parser.add_argument('--managed-root', type=Path)
    parser.add_argument('--report', type=Path, help='Read-only check results for the graphical wizard')
    parser.add_argument('--steam', type=Path, help='Steam folder selected in the graphical wizard (discovery only)')
    parser.add_argument('--game', type=Path, help='Selected game for read-only backup discovery')
    args = parser.parse_args()
    setup.verify_release(ROOT)
    if args.check:
        print('Graphical installer payload verified. No game files were changed.')
        return
    if os.name != 'nt':
        raise ValueError('This installer requires Windows.')
    if args.probe:
        write_probe(args.probe, ROOT, args.steam, args.game, args.managed_root)
    elif args.validate:
        validate_choices(read_choices(args.validate), ROOT, args.managed_root)
        setup.require_closed()
        print('Selections verified. Ready to install.')
    elif args.validate_backups:
        validate_backups(read_choices(args.validate_backups), args.managed_root or ROOT)
        print('Original installation backups verified. No game or Steam files were changed.')
    elif args.readiness or args.inspect:
        if args.report is None:
            parser.error('--report is required for readiness and installation checks')
        values = read_choices(args.readiness or args.inspect)
        result = readiness(values, ROOT, args.managed_root) if args.readiness else inspect_installation(values, ROOT)
        write_report(args.report, 'checks', result)
        for key, value in result.items():
            print(f'{key}: {value}', flush=True)
        print('Read-only check complete. No game or Steam files were changed.')
    elif args.install:
        install_selected(read_choices(args.install), ROOT)
    elif args.uninstall_check:
        setup.require_closed()
        removal_plan(ROOT)
        print('Ready to remove the VR mod from the managed game installations.')
    else:
        remove_managed(ROOT)


if __name__ == '__main__':
    try:
        main()
    except (Exception, SystemExit) as error:
        if isinstance(error, SystemExit) and error.code in (None, 0):
            raise
        print(f'Setup could not finish: {error}', file=sys.stderr, flush=True)
        raise SystemExit(1)
