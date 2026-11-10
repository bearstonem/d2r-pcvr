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


# What the helper is doing, in the words a failure is reported with (#321:
# "Setup could not finish: 2" named neither the step nor a file).
_step = 'starting'


def step(name):
    global _step
    _step = name


class SetupArguments(argparse.ArgumentParser):
    """Report unreadable options in words. argparse exits with status 2, and
    that bare number was all the wizard showed (#321)."""

    def error(self, message):
        raise SystemExit(
            f'the wizard handed its helper options it could not read ({message}). '
            'This happens when a folder is given with a backslash at its end, or when Setup is started from the top '
            'folder of a drive such as D:\\. Move D2R-PCVR-Setup.exe into an ordinary folder such as Downloads, '
            'remove any backslash from the end of the folders you typed, and run Setup again.')


def folder_argument(value):
    """A folder from the wizard. `"D:\\"` reaches a program as `D:"` (the
    backslash escapes the quote); a quote cannot be part of a Windows path,
    so one at the end stands for that lost backslash."""
    if value.endswith('"'):
        value = value.rstrip('"') + '\\'
    return Path(value)


def read_json(path, what):
    """JSON from a named file; a damaged one is reported by its path."""
    try:
        return json.loads(Path(path).read_text(encoding='utf-8-sig'))
    except ValueError as error:
        raise ValueError(f'{what} could not be read: {path} is damaged or incomplete ({error}).') from error


def failure_message(error, doing=None):
    """One line a player can act on: the step, and the file or folder involved.
    The exception itself goes to the setup log (failure_detail)."""
    doing = doing or _step
    if isinstance(error, OSError) and (error.filename or error.errno is not None):
        import errno
        name = str(error.filename) if error.filename else 'a file or folder Setup needs'
        code, windows = error.errno, getattr(error, 'winerror', None)
        if windows == 32:
            text = (f'{name} is open in another program. Close D2R, Steam, D2R VR Launcher and any window '
                    'showing that file, then run Setup again.')
        elif code == errno.ENOENT:
            text = (f'{name} was not found. If Windows Security removed it, see Protection History; '
                    'otherwise check that the folder still exists, then run Setup again.')
        elif code in (errno.EACCES, errno.EPERM):
            text = (f'Windows did not allow {name} to be read or changed. Close programs using it and check '
                    'Windows Security Protection History (Controlled folder access blocks changes to game and '
                    'Saved Games folders), then run Setup again.')
        elif code == errno.ENOSPC:
            text = f'there is not enough free disk space to write {name}.'
        else:
            text = f'Windows reported a problem with {name}: {error.strerror or error}.'
        if error.filename2:
            text += f' (The other file involved: {error.filename2}.)'
    elif isinstance(error, KeyError):
        text = (f'a setup or backup file is missing its entry {error.args[0]!r}. Keep the installation backups '
                'and attach the setup logs to a bug report.') if error.args else 'a setup or backup file is incomplete.'
    else:
        text = str(error).strip()
        if not text or text.lstrip('-').isdigit():
            # Never a bare status: say that nothing better is known.
            text = (f'the helper stopped with code {text or "unknown"} and gave no reason. '
                    'Attach the setup logs to a bug report.')
    return f'Setup could not finish while {doing}: {text}'


def failure_detail(error, doing, logs):
    """Append the exception to the first of `logs` whose folder exists (never
    creating one), and return that file. None: nowhere to write."""
    import time
    import traceback
    lines = [time.strftime('%Y-%m-%d %H:%M:%S ') + f'Setup helper failed while {doing}',
             'Options: ' + json.dumps(sys.argv[1:]),
             *''.join(traceback.format_exception(type(error), error, error.__traceback__)).rstrip().splitlines()]
    for log in logs:
        try:
            if log is None or not Path(log).parent.is_dir():
                continue
            with Path(log).open('a', encoding='utf-8') as output:
                output.write('\n'.join(lines) + '\n')
            return Path(log)
        except OSError:
            continue
    return None


def detail_logs(arguments):
    """Where a failure's detail is kept: the helper folder's setup-last.log once
    there is one (beside setup-install.log), else the user's temporary folder -
    the wizard's own files are deleted when it closes."""
    import tempfile
    managed = None
    for index, value in enumerate(arguments):
        if value == '--managed-root' and index + 1 < len(arguments):
            managed = folder_argument(arguments[index + 1])
        elif value.startswith('--managed-root='):
            managed = folder_argument(value.split('=', 1)[1])
    # Installing and removing run from the helper folder itself; the earlier steps run from the wizard's copy.
    installed = ROOT if any(value.split('=', 1)[0] in ('--install', '--uninstall', '--uninstall-check')
                            for value in arguments) else None
    return [folder / 'setup-last.log' for folder in (managed, installed) if folder is not None] + [
        Path(tempfile.gettempdir()) / 'D2R-PCVR-setup-last.log']


def atomic_json(path, value):
    temporary = path.with_suffix('.tmp')
    try:
        temporary.write_text(json.dumps(value, indent=2) + '\n', encoding='utf-8')
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def managed_state(root):
    path = root / STATE
    result = read_json(path, 'The record of managed installations') if path.is_file() else {'installations': []}
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
                helper = wrapper_root(parts[0])
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
    read_json(saves / 'Settings.json', "The game's settings file")
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
    step('checking that D2R and Steam are closed')
    setup.require_closed()
    step('checking the folders and choices you selected')
    selection = validate_choices(values, root)
    game = selection['game']
    state = managed_state(root)
    before_state = json.loads(json.dumps(state))
    step(f'preserving the installation backups of {game}')
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
    step(f'recording this installation in {root / STATE}')
    atomic_json(root / STATE, state)
    committed_steam = {}
    def steam_committed():
        for path in originals:
            if path != game / install.RECORD:
                committed_steam[path] = path.read_bytes() if path.is_file() else None
    try:
        step(f'installing the mod into {game} and adding the Steam shortcuts')
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
    """The edits that remove the mod from every managed game copy, and notes
    for the player about copies whose game was uninstalled first (#328)."""
    updates, notes = {}, []
    state = managed_state(root)
    for entry in state['installations']:
        game, steam = Path(entry['game']).resolve(), Path(entry['steam']).resolve()
        if game.is_relative_to(root.resolve()) or root.resolve().is_relative_to(game):
            raise ValueError('The managed game path overlaps the helper folder.')
        step(f'reading what the mod installed in {game}')
        gone = uninstall.game_gone(game)
        if gone:
            # Nothing of a game is left to restore or to protect: what the backups list of the mod's own files
            # goes if it can, and the Steam shortcuts below go either way. Never a reason to stop.
            try:
                record = install.read_record(game) if game.is_dir() else {}
            except (OSError, ValueError, KeyError):
                record = {}
            record = record or entry.get('receipt', {})
            leftovers, note = uninstall.leftover_plan(game, record, [root / 'setup-backups/game'])
            updates.update(leftovers)
            notes.append(note)
        else:
            record = install.read_record(game) or entry.get('receipt', {})
        if not gone and ((game / install.RECORD).exists()
                         or mod_identity.survey(game, [root / 'setup-backups/game']).ours):
            detected, _ = storage_config(game)
            if detected != entry['store']:
                raise ValueError(f'The recorded game folder {game} now holds a {detected} installation, not the '
                                 f'{entry["store"]} one the mod was installed into. Keep the backups.')
            chain = uninstall.snapshots(game, record, [root / 'setup-backups/game'])
            updates.update(uninstall.restore_plan(game, chain, restore_video=False))
        step(f'reading the Steam shortcuts in {steam}')
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
    return updates, notes


def remove_managed(root):
    step('checking that D2R and Steam are closed')
    setup.require_closed()
    updates, notes = removal_plan(root)
    step('removing the mod\'s files and Steam shortcuts')
    uninstall.apply_updates(updates, root / 'setup-backups', setup.require_closed)
    step(f'clearing the record of managed installations in {root / STATE}')
    atomic_json(root / STATE, {'installations': []})
    for note in notes:
        print(note, flush=True)
    print('The VR mod and its matching Steam shortcuts were removed. Characters, shared stash, current video settings and backups were kept.', flush=True)


def main():
    step('reading the options the wizard passed')
    parser = SetupArguments(description=__doc__)
    action = parser.add_mutually_exclusive_group(required=True)
    action.add_argument('--probe', type=folder_argument)
    action.add_argument('--validate', type=folder_argument)
    action.add_argument('--install', type=folder_argument)
    action.add_argument('--uninstall', action='store_true')
    action.add_argument('--uninstall-check', action='store_true')
    action.add_argument('--check', action='store_true')
    parser.add_argument('--managed-root', type=folder_argument)
    parser.add_argument('--steam', type=folder_argument, help='Steam folder selected in the graphical wizard (discovery only)')
    parser.add_argument('--game', type=folder_argument, help='Selected game for read-only backup discovery')
    parser.add_argument('--source-root', type=folder_argument, help='Installer source folder (probe only)')
    parser.add_argument('--backup-hint', type=folder_argument, help='Backup folder selected in the wizard (probe only)')
    args = parser.parse_args()
    if not args.probe and (args.source_root is not None or args.backup_hint is not None):
        parser.error('--source-root and --backup-hint require --probe')
    step('checking the installer\'s own files')
    setup.verify_release(ROOT)
    if args.check:
        print('Graphical installer payload verified. No game files were changed.')
        return
    if os.name != 'nt':
        raise ValueError('This installer requires Windows.')
    if args.probe:
        step('looking for Steam, the game and earlier installations' if args.game is None
             else f'reading the game folder {args.game} and the Steam folder {args.steam}')
        write_probe(args.probe, ROOT, args.steam, args.game, args.managed_root,
                    args.source_root, args.backup_hint)
    elif args.validate:
        step('checking the folders and choices you selected')
        validate_choices(read_choices(args.validate), ROOT, args.managed_root)
        step('checking that D2R and Steam are closed')
        setup.require_closed()
        print('Selections verified. Ready to install.')
    elif args.install:
        step('reading your setup choices')
        install_selected(read_choices(args.install), ROOT)
    elif args.uninstall_check:
        step('checking that D2R and Steam are closed')
        setup.require_closed()
        step('checking what the mod installed, before removing it')
        for note in removal_plan(ROOT)[1]:
            print(note, flush=True)
        print('Ready to remove the VR mod from the managed game installations.')
    else:
        remove_managed(ROOT)


def run():
    """main() for the wizard: 0, or 1 after one plain last line (the wizard
    shows the last line it received) with the detail kept in a log."""
    try:
        main()
    except (Exception, SystemExit) as error:
        if isinstance(error, SystemExit) and error.code in (None, 0):
            return 0
        doing = _step
        message = failure_message(error, doing)
        log = failure_detail(error, doing, detail_logs(sys.argv[1:]))
        if log is not None:
            message += f' Technical details were saved in {log}.'
        print(message, file=sys.stderr, flush=True)
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(run())
