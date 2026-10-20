"""Opt-in TCP/IP setup and read-only compatibility report for co-op testing.

Uses offline characters and D2RLoader's own host/join UI. Never downloads a
loader, opens ports, starts a game or changes character files.
"""
import argparse
import configparser
import copy
from datetime import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import tempfile
import tomllib

from game_install import validate_game
from install import require_game_closed
from mod_identity import MOD_NAME
from coop_connection import port_number

ROOT = Path(__file__).resolve().parent.parent
CONFIG = Path('d2rloader/config/d2rloader.toml')
PLUGIN = Path('d2rloader/plugins/d2rl-pcvr.dll')
TEST_MOD = 'pcvr-coop'
TEST_INFO = b'{"name":"pcvr-coop","savepath":"pcvr-coop/"}\n'
CAPABILITY = b'Frame co-op: experimental transport diagnostics ready'


def checked_game(game):
    game = Path(game).absolute()
    if game.resolve() != game:
        raise ValueError('Select a game folder directly, rather than a linked path.')
    return game


def patch_button(original):
    """Preserve comments/newlines/other sections; validate the complete result."""
    text = original.decode('utf-8-sig')
    parsed = tomllib.loads(text)
    section = parsed.get('d2rloader', {})
    if not isinstance(section, dict):
        raise ValueError('The loader settings must contain a [d2rloader] table.')
    current = section.get('show_tcpip_button', False)
    if not isinstance(current, bool):
        raise ValueError('show_tcpip_button must be a TOML boolean.')
    if current:
        return original
    newline = '\r\n' if '\r\n' in text else '\n'
    lines = text.splitlines(keepends=True)
    start = next((i for i, line in enumerate(lines)
                  if re.fullmatch(r'\s*\[d2rloader\]\s*(?:#.*)?', line.rstrip('\r\n'))), None)
    if start is None:
        text += ('' if not text or text.endswith(('\n', '\r')) else newline)
        text += '[d2rloader]'+newline+'show_tcpip_button = true'+newline
    else:
        end = next((i for i in range(start+1, len(lines)) if lines[i].lstrip().startswith('[')), len(lines))
        found = False
        for i in range(start+1, end):
            match = re.fullmatch(r'(\s*show_tcpip_button\s*=\s*)false(\s*(?:#.*?)?)(\r?\n)?', lines[i])
            if match:
                lines[i] = match[1]+'true'+match[2]+(match[3] or '')
                found = True
                break
        if not found:
            if end and not lines[end-1].endswith(('\n', '\r')):
                lines[end-1] += newline
            lines.insert(end, 'show_tcpip_button = true'+newline)
        text = ''.join(lines)
    expected = copy.deepcopy(parsed)
    expected.setdefault('d2rloader', {})['show_tcpip_button'] = True
    if tomllib.loads(text) != expected:
        raise ValueError('Could not enable the button without changing another loader setting.')
    return (b'\xef\xbb\xbf' if original.startswith(b'\xef\xbb\xbf') else b'')+text.encode('utf-8')


def enable_button(game):
    game = checked_game(game)
    config = game/CONFIG
    if config.resolve() != config:
        raise ValueError('Loader settings must not be a linked file.')
    require_game_closed()
    original = config.read_bytes()
    updated = patch_button(original)
    if updated == original:
        return None
    backup = config.with_name(config.name+'.pcvr-coop-'+datetime.now().strftime('%Y%m%d-%H%M%S-%f')+'.bak')
    with backup.open('xb') as stream:
        stream.write(original)
    if backup.read_bytes() != original:
        raise OSError('Could not verify the loader settings backup.')
    # Recheck immediately before changing the global setting. No rollback can
    # overwrite settings that the running loader is concurrently changing.
    require_game_closed()
    if config.read_bytes() != original:
        raise ValueError('Loader settings changed during setup; close the game and retry.')
    fd, name = tempfile.mkstemp(prefix='.pcvr-coop-', suffix='.tmp', dir=config.parent)
    temporary = Path(name)
    try:
        with os.fdopen(fd, 'wb') as stream:
            stream.write(updated)
        os.replace(temporary, config)
        if config.read_bytes() != updated:
            raise OSError(f'Could not verify updated settings. Your original file is at {backup}')
    finally:
        temporary.unlink(missing_ok=True)
    return backup


def inspect(game, mod=MOD_NAME, experimental_battlenet=False):
    game = checked_game(game)
    if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_-]{0,63}', mod):
        raise ValueError('Select a plain D2RLoader mod folder name.')
    store, version = validate_game(game, experimental_battlenet=experimental_battlenet)
    folder = game/'mods'/mod
    plugin = folder/PLUGIN
    if folder.resolve() != folder or plugin.resolve() != plugin or not plugin.is_file():
        raise ValueError('Install Unleashed in the selected mod folder first.')
    manifest = json.loads((ROOT/'vrmod-manifest.json').read_text(encoding='utf-8'))
    loader = next(r for r in manifest['requires'] if r['id']=='d2rloader')
    loader_hashes = {}
    for entry in loader['files']:
        path = game/entry['path'].removeprefix('{game}/')
        digest = hashlib.sha256(path.read_bytes()).hexdigest() if path.is_file() else ''
        if digest not in entry['sha256_any_of']:
            raise ValueError('D2RLoader files do not match this Unleashed build. Repair with our installer first.')
        loader_hashes[path.name] = digest
    settings = tomllib.loads((game/CONFIG).read_text(encoding='utf-8-sig'))
    section = settings.get('d2rloader', {})
    if not isinstance(section, dict) or not isinstance(section.get('advanced', {}), dict):
        raise ValueError('Expected D2RLoader settings tables.')
    advanced = section.get('advanced', {})
    extensions = advanced.get('allow_mod_extensions', True)
    if extensions is not True:
        raise ValueError('Enable Allow Mod Extensions in D2RLoader General settings before co-op testing.')
    for key, source in (('show_tcpip_button', section), ('automatic_port_mapping', advanced), ('hosting_check', advanced)):
        if type(source.get(key, key != 'show_tcpip_button')) is not bool:
            raise ValueError(f'{key} must be a TOML boolean.')
    ini = configparser.ConfigParser(strict=False, interpolation=None, inline_comment_prefixes=('#', ';'))
    preferences = folder/'d2rloader/plugins/d2r_pcvr.ini'
    if preferences.is_file():
        data = preferences.read_bytes()
        ini.read_string(data.decode('utf-16' if data.startswith((b'\xff\xfe', b'\xfe\xff')) else 'utf-8-sig'))
    policy = {}
    for key, default in (('melee_rules', 0), ('contact_hits', 1), ('enemies_always_hit', 1)):
        value = ini.get('gameplay', key, fallback=str(default)).strip()
        # Match the native diagnostic adapter's integer policy reads.
        if not re.fullmatch(r'-?\d+', value):
            raise ValueError(f'Use a numeric {key} value in VR Settings before comparing co-op rules.')
        policy[key] = int(int(value) == 1) if key == 'melee_rules' else int(int(value) != 0)
    payload = plugin.read_bytes()
    return dict(store=store, game_version=version, mod=mod, helper_version=manifest['version'],
                plugin_sha256=hashlib.sha256(payload).hexdigest(), loader_sha256=loader_hashes,
                combat_policy=policy, candidate_has_diagnostics=CAPABILITY in payload,
                multiplayer_button=section.get('show_tcpip_button', False),
                port=port_number(advanced.get('game_port', 4000)),
                automatic_port_mapping=advanced.get('automatic_port_mapping', True),
                hosting_check=advanced.get('hosting_check', True),
                status='community-reported co-op; remote VR movement is not replicated; full host/guest feature testing pending')


def stage_test(game, plugin, experimental_battlenet=False):
    """Fresh isolated mod/save list; never update a player's existing folder."""
    from prepare_dev_scenarios import FILES, TREES
    game = checked_game(game)
    require_game_closed()
    validate_game(game, experimental_battlenet=experimental_battlenet)
    plugin = Path(plugin).resolve()
    if CAPABILITY not in plugin.read_bytes():
        raise ValueError('Select a DLL built from the co-op diagnostic branch.')
    source = game/'mods'/MOD_NAME
    destination = game/'mods'/TEST_MOD
    mods = destination.parent
    if mods.resolve() != mods or destination.exists():
        raise ValueError('The pcvr-coop test folder already exists or the mods folder is linked; leaving it unchanged.')
    candidates = {p: source/p for p in FILES}
    candidates[PLUGIN] = plugin
    trees = [(tree, tree) for tree in TREES]
    if (source/f'{MOD_NAME}.mpq/data').is_dir():
        trees.append((f'{MOD_NAME}.mpq/data', f'{TEST_MOD}.mpq/data'))
    for tree,target in trees:
        folder = source/tree
        if folder.resolve()!=folder or not folder.is_dir():
            raise ValueError(f'Complete the normal Unleashed installation first; missing {folder}')
        for path in folder.rglob('*'):
            if path.resolve()!=path:
                raise ValueError('Test preparation refuses linked source files.')
            if path.is_file():
                candidates[Path(target)/path.relative_to(folder)] = path
    for relative,path in candidates.items():
        if path.resolve()!=path or not path.is_file() or relative.is_absolute() or '..' in relative.parts:
            raise ValueError(f'Missing or linked test source: {path}')
    # Only the freshly allocated temporary folder can be cleaned on failure.
    # Resolve it before using TemporaryDirectory's recursive cleanup.
    temporary = tempfile.TemporaryDirectory(prefix='.pcvr-coop-',dir=mods)
    stage = Path(temporary.name)
    if stage.resolve().parent != mods.resolve():
        raise ValueError('Unexpected test staging directory.')
    with temporary:
        for relative,path in candidates.items():
            target = stage/relative
            target.parent.mkdir(parents=True,exist_ok=True)
            shutil.copy2(path,target)
            if target.read_bytes()!=path.read_bytes():
                raise OSError('Test file verification failed.')
        info = stage/f'{TEST_MOD}.mpq/modinfo.json'
        info.parent.mkdir(parents=True,exist_ok=True)
        info.write_bytes(TEST_INFO)
        require_game_closed()
        if destination.exists():
            raise ValueError('Another process created pcvr-coop; leaving it unchanged.')
        stage.rename(destination)
    return destination


def detect_game():
    managed = Path(os.environ.get('LOCALAPPDATA', ''))/'Programs/D2R PCVR/managed-installations.json'
    if managed.is_file():
        entries = json.loads(managed.read_text(encoding='utf-8-sig')).get('installations', [])
        games = {Path(e['game']) for e in entries if e.get('status')=='installed'}
        if len(games)==1:
            return games.pop()
    raise ValueError('Select your game folder with --game "C:\\path\\to\\Diablo II Resurrected".')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game', type=Path)
    parser.add_argument('--mod', default=MOD_NAME)
    parser.add_argument('--enable-tcpip', action='store_true', help='Show the Multiplayer button; close D2R/D2RLoader first')
    parser.add_argument('--experimental-battlenet', action='store_true')
    parser.add_argument('--json', action='store_true', help='Print a report to compare with the other player')
    parser.add_argument('--stage-test', action='store_true', help='Prepare isolated pcvr-coop mod and character list')
    parser.add_argument('--plugin', type=Path, help='Co-op diagnostic candidate DLL, required with --stage-test')
    parser.add_argument('--wizard', action='store_true', help='Guided host/join setup for the existing installed mod')
    parser.add_argument('--test-host', help='Test a host IPv4/name[:port] after the host creates a game')
    parser.add_argument('--save-report', type=Path, help='Save compatibility information without paths, IPs or characters')
    parser.add_argument('--compare-report', type=Path, help='Compare a friend compatibility JSON report')
    args = parser.parse_args()
    try:
        game = args.game or detect_game()
        report = inspect(game, args.mod, args.experimental_battlenet)
        if args.wizard:
            if args.stage_test or args.enable_tcpip or args.test_host or args.save_report or args.compare_report:
                raise ValueError('Use --wizard by itself, or use the non-interactive options separately.')
            from coop_wizard import run
            run(game, report)
            return
        if args.stage_test:
            if args.plugin is None:
                raise ValueError('--stage-test requires --plugin PATH to the candidate DLL.')
            stage_test(game,args.plugin,args.experimental_battlenet)
            args.mod = TEST_MOD
            report = inspect(game, args.mod, args.experimental_battlenet)
            print('Prepared pcvr-coop with a separate offline character list. Normal Unleashed and pcvr-dev are unchanged.')
        if args.enable_tcpip:
            backup = enable_button(game)
            report['multiplayer_button'] = True
            if backup:
                print(f'Enabled Multiplayer. Original loader settings: {backup}')
        if args.json:
            print(json.dumps(report, indent=2))
        else:
            print(f'Unleashed co-op test setup: {report["game_version"]} ({report["store"]})')
            print('Multiplayer button: '+('shown' if report['multiplayer_button'] else 'hidden; run with --enable-tcpip'))
            print(report['status'])
            print('Start Unleashed normally, select an offline character, then choose Multiplayer beside Play.')
            print('Host: choose LAN Only or Unlisted. Guest: use Direct Join or the shared Game ID.')
            print('Both PCs need the same test DLL, mod folder name and combat rules. Keep the same game port.')
            print('No game was launched, no firewall/router changes were made, and no saves were changed.')
        from coop_connection import friend_report, compare_reports, load_friend_report, probe
        if args.save_report:
            with args.save_report.open('x', encoding='utf-8') as output:
                json.dump(friend_report(report), output, indent=2)
                output.write('\n')
            print(f'Compatibility report saved: {args.save_report}')
        if args.compare_report:
            differences = compare_reports(friend_report(report), load_friend_report(args.compare_report))
            print('Differences: '+', '.join(differences) if differences else 'Compared setup fields match. The loader still checks game data and other extensions.')
        if args.test_host:
            print(json.dumps(probe(args.test_host, report['port']), indent=2))
    except (OSError, ValueError, KeyError, configparser.Error) as error:
        parser.exit(1, f'Co-op setup: {error}\n')


if __name__=='__main__':
    main()
