#!/usr/bin/env python3
"""Noninteractive backend for the D2R PCVR graphical Windows installer."""
import argparse
import configparser
import hashlib
import io
import json
import os
from pathlib import Path
import shlex
import shutil
import sys

import install
import mod_identity
import setup_windows as setup
import steam_config
import uninstall_windows as uninstall
from game_install import storage_config, validate_game

ROOT = Path(__file__).resolve().parent.parent
STATE = 'managed-installations.json'
HEADSETS = ('Steam Frame', 'Quest 3', 'Quest 2', 'Other SteamVR headset')
MODES = {'repair': 0, 'full': 1, 'shortcuts': 2}
BACKUP_ERRORS = (OSError, ValueError, KeyError, TypeError, AttributeError)
PROBE_BACKUP_ERRORS = BACKUP_ERRORS + (SystemExit,)


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


def command_arguments(command):
    """Read the narrow Windows command format emitted by our launch helpers."""
    if not isinstance(command, str) or any(char in command for char in '\r\n'):
        return []
    try:
        parts = shlex.split(command, posix=False)
    except ValueError:
        return []
    result = [part[1:-1] if part.startswith('"') and part.endswith('"') else part
              for part in parts]
    return [] if any('"' in part for part in result) else result


def wrapper_root(argument):
    wrapper = Path(argument)
    if (wrapper.is_absolute() and wrapper.name.lower() == 'launch_windows.py'
            and wrapper.parent.name.lower() == 'steamvr'):
        return wrapper.parent.parent.resolve()
    return None


def launch_helper_roots(game, accounts, steam_game):
    """Read existing D2R launch routing; never run a discovered command."""
    roots = set()
    for profile, _ in accounts:
        if steam_game is not None and game == steam_game.resolve():
            try:
                source = (profile / 'config/localconfig.vdf').read_text(encoding='utf-8-sig')
                options = steam_config.text_value(steam_config.parse_text(source),
                    'UserLocalConfigStore', 'Software', 'Valve', 'Steam', 'apps',
                    steam_config.APP_ID, 'LaunchOptions')
                parts = command_arguments(options)
                if len(parts) >= 3 and '%command%' in parts:
                    helper = wrapper_root(parts[1])
                    if helper is not None:
                        roots.add(helper)
            except BACKUP_ERRORS:
                pass
        try:
            source = profile / 'config/shortcuts.vdf'
            if not source.is_file():
                continue
            entries = steam_config.decode_binary(source.read_bytes()).get('shortcuts', {})
            if not isinstance(entries, dict):
                continue
            for entry in entries.values():
                if not isinstance(entry, dict) or not isinstance(entry.get('icon'), str):
                    continue
                if Path(entry['icon']).resolve() != (game / 'D2R.exe').resolve():
                    continue
                parts = command_arguments(entry.get('LaunchOptions'))
                games = [part.split('=', 1)[1] for part in parts
                         if part.startswith('--battlenet-game=')]
                if (not parts or len(games) != 1 or '--frame-vr' not in parts
                        or Path(games[0]).resolve() != game):
                    continue
                # Current shortcuts name the wrapper relative to their start folder.
                start = entry.get('StartDir')
                wrapper = Path(parts[0])
                if not wrapper.is_absolute() and isinstance(start, str) and len(start) > 2 and start[0] == start[-1] == '"':
                    wrapper = Path(start[1:-1]) / wrapper
                helper = wrapper_root(str(wrapper))
                if helper is not None:
                    roots.add(helper)
        except BACKUP_ERRORS:
            pass
    return sorted(roots)


def probe_backup_chain(game, root, folder=None):
    chain = check_backups(game, root, folder)
    for _, manifest in chain:
        installed = manifest.get('installed')
        if (not isinstance(installed, list) or not installed
                or any(not isinstance(name, str) or not name for name in installed)):
            raise ValueError('An installation backup has an invalid or incomplete installed-file list.')
    if chain:
        baseline = {str(install.safe_relative(name)).lower() for name in chain[0][1]['installed']}
        if not any(str(plugin).lower() in baseline for plugin in uninstall.PLUGINS):
            raise ValueError('The original installation backup does not track the VR plugin. '
                             'Select a complete installation backup.')
    return chain


def checked_backup_folder(game, root, folder):
    folder = Path(folder).expanduser().resolve()
    snapshot = folder if (folder / 'manifest.json').is_file() else None
    # Include later upgrades beside an explicitly selected original snapshot.
    if snapshot is not None:
        folder = folder.parent
    chain = probe_backup_chain(game, root, folder)
    if snapshot is not None and snapshot not in [path for path, _ in chain]:
        raise ValueError('The selected snapshot is not part of the current installation backup chain. '
                         'Select its installation backup folder.')
    return folder, chain


def probe_backups(game, root, accounts, steam_game, source_root=None, backup_hint=None):
    try:
        chain = probe_backup_chain(game, root)
    except PROBE_BACKUP_ERRORS as error:
        message = str(error)
    else:
        parents = {path.parent for path, _ in chain}
        return '0', '', str(next(iter(parents))) if len(parents) == 1 else ''
    if backup_hint is not None:
        try:
            folder, _ = checked_backup_folder(game, root, backup_hint)
        except PROBE_BACKUP_ERRORS as error:
            return '1', str(error), ''
        return '1', '', str(folder)
    helpers = [root]
    if source_root is not None:
        helpers.append(Path(source_root))
    helpers.extend(launch_helper_roots(game, accounts, steam_game))
    candidates = set()
    for helper in helpers:
        for base in (helper, helper / 'd2r-pcvr-preview'):
            candidates.update((base, base / 'setup-backups/game', base / 'build-frame/backups'))
    matches = {}
    for candidate in sorted(candidates):
        try:
            folder, chain = checked_backup_folder(game, root, candidate)
        except PROBE_BACKUP_ERRORS:
            continue
        signature = tuple(path for path, _ in chain)
        if signature:
            matches.setdefault(signature, folder)
    if len(matches) == 1:
        return '1', '', str(next(iter(matches.values())))
    if matches:
        message = ('More than one valid installation backup was found for this game. '
                   'Select the backup folder for the installation you want to keep.')
    return '1', message, ''


def write_probe(path, root, steam=None, game=None, managed_root=None, source_root=None, backup_hint=None):
    steam = steam or setup.steam_location()
    libraries = setup.libraries(steam) if steam else []
    accounts = setup.profiles(steam) if steam else []
    manifest = setup.verify_release(root)
    steam_game = setup.app_folder(libraries, steam_config.APP_ID)
    values = {
        'steam': str(steam or ''), 'steam_game': str(steam_game or ''),
        'battlenet_game': str(setup.battlenet_location() or ''),
        'steamvr': str(setup.app_folder(libraries, '250820') or ''), 'saves': str(setup.saved_games()),
        'battlenet_available': str(int('bin/CascLib.dll' in manifest['sha256'])),
        'profiles': str(len(accounts)),
        'backup_required': '0', 'backup_message': '', 'backup_folder': '',
        'installed_mod': '', 'other_mod': '',
    }
    values.update(setup.hdr_check.check())
    if game is not None:
        required, message, folder = probe_backups(game.resolve(), Path(managed_root or root),
            accounts, steam_game, source_root, backup_hint)
        values.update(backup_required=required, backup_message=message, backup_folder=folder)
        # Whose plugin is in the game folder. D2R VR's stops the wizard here; a
        # d2rl-vrcam.dll without a record of this mod may still be an old D2R
        # PCVR whose backup the wizard asks for next (validation decides).
        found = mod_identity.survey(game.resolve(), [Path(managed_root or root) / 'setup-backups/game',
                                                     backup_hint, folder or None])
        values.update(installed_mod=mod_identity.MOD_ID if found.ours else '',
                      other_mod=found.message if found.other == mod_identity.OTHER_MOD else '')
    for index, (profile, name) in enumerate(accounts):
        values[f'profile_{index}'] = profile.name
        values[f'profile_name_{index}'] = name
    parser = configparser.ConfigParser(interpolation=None)
    parser['detected'] = values
    output = io.StringIO()
    parser.write(output)
    # Win32 profile APIs used by Inno Setup read UTF-16 INI files reliably.
    path.write_text(output.getvalue(), encoding='utf-16')


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
    legacy = Path(values['backups']).expanduser().resolve() if values.get('backups') else None
    found = mod_identity.survey(game, [target / 'setup-backups/game', legacy])
    if found.other:
        raise ValueError(found.message + ' No game or Steam files were changed.')
    if mode == 2 and not all((game / name).is_file() for name in (
            'D2RLoader.exe', mod_identity.RESHADE, mod_identity.OPENXR_LOADER, mod_identity.PLUGIN, mod_identity.SETTINGS)):
        raise ValueError('Shortcuts only requires a complete installed mod. Choose Update / repair instead.')
    options = setup.launch_options(target / '.runtime/python/python.exe', target / 'steamvr/launch_windows.py', runtime) if store == 'steam' else None
    routing = {} if store == 'steam' else {'store': store, 'python': target / '.runtime/python/python.exe',
                                          'wrapper': target / 'steamvr/launch_windows.py', 'runtime': runtime}
    setup.preflight_steam(profile, steam, game, options, connections, **routing)
    check_backups(game, target, legacy)
    managed_state(target)
    return dict(manifest=manifest, store=store, version=version, headset=headset, connections=connections,
                steam=steam, game=game, profile=profile, saves=saves, runtime=runtime, mode=mode)


def check_backups(game, root, legacy=None):
    """Fail before helper files are copied if an existing install cannot be removed."""
    record = install.read_record(game)
    # This mod's plugin under either name; never D2R VR's d2rl-vrcam.dll.
    if (record.get('snapshots') or (game / uninstall.PLUGIN).is_file()
            or mod_identity.plugin_owner(game / mod_identity.LEGACY_PLUGIN) not in (None, mod_identity.OTHER_MOD)):
        roots = [root / 'setup-backups/game'] + [Path(name).parent for name in record.get('snapshots', [])]
        if legacy is not None:
            roots.append(legacy)
        chain = uninstall.snapshots(game, record, roots)
        uninstall.restore_plan(game, chain)
        return chain
    return []


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
        if (game / install.RECORD).exists() or mod_identity.survey(game, [root / 'setup-backups/game']).ours:
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
    action.add_argument('--install', type=Path)
    action.add_argument('--uninstall', action='store_true')
    action.add_argument('--uninstall-check', action='store_true')
    action.add_argument('--check', action='store_true')
    parser.add_argument('--managed-root', type=Path)
    parser.add_argument('--steam', type=Path, help='Steam folder selected in the graphical wizard (discovery only)')
    parser.add_argument('--game', type=Path, help='Selected game for read-only backup discovery')
    parser.add_argument('--source-root', type=Path, help='Installer source folder (probe only)')
    parser.add_argument('--backup-hint', type=Path, help='Backup folder selected in the wizard (probe only)')
    args = parser.parse_args()
    if not args.probe and (args.source_root is not None or args.backup_hint is not None):
        parser.error('--source-root and --backup-hint require --probe')
    setup.verify_release(ROOT)
    if args.check:
        print('Graphical installer payload verified. No game files were changed.')
        return
    if os.name != 'nt':
        raise ValueError('This installer requires Windows.')
    if args.probe:
        write_probe(args.probe, ROOT, args.steam, args.game, args.managed_root,
                    args.source_root, args.backup_hint)
    elif args.validate:
        validate_choices(read_choices(args.validate), ROOT, args.managed_root)
        setup.require_closed()
        print('Selections verified. Ready to install.')
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
