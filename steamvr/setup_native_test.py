"""Graphical setup fixtures; never touch a real game, Steam profile or registry."""
import contextlib
import configparser
import io
import json
from pathlib import Path
import shutil
import sys
import tempfile
import textwrap
import unittest
from unittest.mock import patch

import setup_native as native
import setup_windows as setup
import steam_config as steam
import install
import mod_identity
import uninstall_windows as uninstall

LOCAL = '"UserLocalConfigStore" { "friends" { "PersonaName" "Player" } "Software" { "Valve" { "Steam" { "apps" { "2536520" { "LaunchOptions" "-original-option" } } } } } }'


class NativeSetupTests(unittest.TestCase):
    def put(self, path, data=b'fixture'):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        return path

    def fixture(self, root, store='steam'):
        app, game, client, saves = [root / name for name in ('helper', 'Game-' + store, 'Steam', 'Saved Games')]
        app.mkdir(exist_ok=True)
        self.put(client / 'steam.exe')
        self.put(client / 'steamapps/common/SteamVR/steamxr_win64.json', b'{}')
        self.put(client / 'steamapps/appmanifest_250820.acf', b'"AppState" { "installdir" "SteamVR" }')
        profile = client / 'userdata/123'
        self.put(profile / 'config/localconfig.vdf', LOCAL.encode())
        self.put(game / 'D2R.exe', ('3.3.93787' if store == 'steam' else '3.3.93847').encode('utf-16-le'))
        self.put(game / ('Data/.build.config' if store == 'steam' else '.build.info'))
        self.put(saves / 'Settings.json', b'{"Anti Aliasing": 2}')
        self.put(saves / 'Character.d2s', b'current character')
        values = dict(store=store, headset='Steam Frame', connections='steamvr', steam=str(client),
                      game=str(game), saves=str(saves), steamvr=str(client / 'steamapps/common/SteamVR'),
                      profile='123', mode='full')
        return app, game, client, saves, profile, values

    def snapshot(self, root, game, saves, previous=None):
        previous = previous or {}
        self.put(root / 'manifest.json', json.dumps(dict(game=str(game), saves=str(saves),
            installed=[str(uninstall.PLUGIN), str(install.RECORD)], previous_mod=False)).encode())
        self.put(root / 'saves/Settings.json', b'{"original": true}')
        self.put(root / 'saves/Character.d2s', b'old character must not be restored')
        for name, data in previous.items():
            self.put(root / 'previous' / name, data)
        return root

    def steam_game(self, game, client):
        target = client / 'steamapps/common/Diablo II Resurrected'
        game.rename(target)
        self.put(client / 'steamapps/appmanifest_2536520.acf',
                 b'"AppState" { "installdir" "Diablo II Resurrected" }')
        return target

    def steam_wrapper(self, profile, helper):
        self.put(helper / 'steamvr/launch_windows.py', b'fixture only; never executed')
        source = profile / 'config/localconfig.vdf'
        options = setup.launch_options(helper / 'python/python.exe', helper / 'steamvr/launch_windows.py', None)
        source.write_text(steam.set_launch_options(source.read_text(), options), encoding='utf-8')

    def probe(self, root, app, game, client, saves, **kwargs):
        output = root / 'probe.ini'
        before = {path: path.read_bytes() for path in root.rglob('*')
                  if path.is_file() and path != output}
        with patch.object(setup, 'verify_release', return_value={'sha256': {}}), \
                patch.object(setup, 'saved_games', return_value=saves), \
                patch.object(setup, 'battlenet_location', return_value=None):
            native.write_probe(output, app, client, game, **kwargs)
        after = {path: path.read_bytes() for path in root.rglob('*')
                 if path.is_file() and path != output}
        self.assertEqual(after, before, 'Backup discovery must write only its output INI')
        parser = configparser.ConfigParser(interpolation=None)
        parser.read(output, encoding='utf-16')
        return parser['detected']

    def test_probe_clean_game_always_outputs_empty_backup_folder(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            app, game, client, saves, profile, values = self.fixture(root)
            result = self.probe(root, app, game, client, saves)
            self.assertEqual(result['backup_required'], '0')
            self.assertEqual(result['backup_folder'], '')

    def test_probe_reports_hdr_advice_without_changing_game_settings(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            app, game, client, saves, profile, values = self.fixture(root)
            with patch.object(setup.hdr_check, 'query', return_value=[setup.hdr_check.Display('Fixture HDR', True)]):
                result = self.probe(root, app, game, client, saves)
            self.assertEqual(result['hdr_status'], 'on')
            self.assertIn('Fixture HDR', result['hdr_warning'])
            self.assertIn('Use HDR Off', result['hdr_warning'])
            self.assertIn('continue setup', result['hdr_warning'])

    def test_probe_finds_complete_legacy_chain_from_steam_wrapper_with_unicode_and_spaces(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            app, game, client, saves, profile, values = self.fixture(root)
            game = self.steam_game(game, client)
            helper = root / 'old extracted 日本語 with spaces'
            folder = helper / 'build-frame/backups'
            original = self.snapshot(folder / '20260101', game, saves)
            upgrade = self.snapshot(folder / '20260102', game, saves,
                                    {str(uninstall.PLUGIN): b'previous mod'})
            manifest = json.loads((upgrade / 'manifest.json').read_text())
            manifest.update(previous_mod=True, installed=manifest['installed'] + ['ReShade.ini'])
            (upgrade / 'manifest.json').write_text(json.dumps(manifest))
            self.put(game / uninstall.PLUGIN, b'legacy mod')
            self.steam_wrapper(profile, helper)
            result = self.probe(root, app, game, client, saves)
            self.assertEqual(result['backup_required'], '1')
            self.assertEqual(result['backup_folder'], str(folder.resolve()))
            chain = native.check_backups(game, app, Path(result['backup_folder']))
            self.assertEqual([path for path, _ in chain], [original, upgrade])
            self.assertIn(game / 'ReShade.ini', uninstall.restore_plan(game, chain))
            self.assertFalse((game / install.RECORD).exists())

    def test_probe_finds_source_preview_and_managed_development_backup_roots(self):
        for location in ('source-preview', 'managed-development'):
            with self.subTest(location=location), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                app, game, client, saves, profile, values = self.fixture(root)
                source = root / 'downloaded installer'
                folder = (source / 'd2r-pcvr-preview/setup-backups/game' if location == 'source-preview'
                          else app / 'build-frame/backups')
                self.snapshot(folder / '20260101', game, saves)
                self.put(game / uninstall.PLUGIN, b'legacy mod')
                result = self.probe(root, app, game, client, saves, source_root=source)
                self.assertEqual(result['backup_required'], '1')
                self.assertEqual(result['backup_folder'], str(folder.resolve()))

    def test_explicit_snapshot_hint_includes_later_upgrades_and_beats_other_candidates(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            app, game, client, saves, profile, values = self.fixture(root)
            folder = root / 'chosen backup'
            original = self.snapshot(folder / '20260101', game, saves)
            upgrade = self.snapshot(folder / '20260102', game, saves,
                                    {str(uninstall.PLUGIN): b'previous mod'})
            manifest = json.loads((upgrade / 'manifest.json').read_text())
            manifest['previous_mod'] = True
            (upgrade / 'manifest.json').write_text(json.dumps(manifest))
            source = root / 'other helper'
            self.snapshot(source / 'setup-backups/game/20260103', game, saves)
            self.put(game / uninstall.PLUGIN, b'legacy mod')
            result = self.probe(root, app, game, client, saves, source_root=source, backup_hint=original)
            self.assertEqual(result['backup_folder'], str(folder.resolve()))
            self.assertEqual(len(native.check_backups(game, app, Path(result['backup_folder']))), 2)

    def test_invalid_explicit_hints_do_not_silently_switch_to_a_valid_source_backup(self):
        malformed = [[], {}, {'game': None, 'installed': []}, {'game': 'SELECTED'},
                     {'game': 'SELECTED', 'installed': None},
                     {'game': 'SELECTED', 'installed': [], 'previous_mod': False},
                     {'game': 'SELECTED', 'installed': [str(install.RECORD)], 'previous_mod': False},
                     {'game': 'SELECTED', 'installed': ['../D2R.exe'], 'previous_mod': False}]
        cases = [('stale', None), ('wrong-game', None), ('corrupt-json', None)]
        cases += [('malformed-' + str(index), value) for index, value in enumerate(malformed)]
        for label, value in cases:
            with self.subTest(case=label), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                app, game, client, saves, profile, values = self.fixture(root)
                source = root / 'valid fallback'
                self.snapshot(source / 'setup-backups/game/20260101', game, saves)
                self.put(game / uninstall.PLUGIN, b'legacy mod')
                hint = root / 'bad candidate'
                if label == 'wrong-game':
                    self.snapshot(hint / '20260101', root / 'another game', saves)
                elif label == 'corrupt-json':
                    self.put(hint / '20260101/manifest.json', b'{bad JSON')
                elif value is not None:
                    if isinstance(value, dict) and value.get('game') == 'SELECTED':
                        value = {**value, 'game': str(game)}
                    self.put(hint / '20260101/manifest.json', json.dumps(value).encode())
                result = self.probe(root, app, game, client, saves, source_root=source, backup_hint=hint)
                self.assertEqual(result['backup_required'], '1')
                self.assertEqual(result['backup_folder'], '')
                self.assertTrue(result['backup_message'])

    def test_probe_ignores_corrupt_candidates_and_rejects_distinct_valid_chains(self):
        for corrupt in (True, False):
            with self.subTest(corrupt=corrupt), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                app, game, client, saves, profile, values = self.fixture(root)
                game = self.steam_game(game, client)
                source, old_helper = root / 'source', root / 'old helper'
                folder = source / 'setup-backups/game'
                self.snapshot(folder / '20260101', game, saves)
                candidate = old_helper / 'build-frame/backups/20260101'
                if corrupt:
                    self.put(candidate / 'manifest.json', b'[]')
                else:
                    self.snapshot(candidate, game, saves)
                self.put(game / uninstall.PLUGIN, b'legacy mod')
                self.steam_wrapper(profile, old_helper)
                result = self.probe(root, app, game, client, saves, source_root=source)
                self.assertEqual(result['backup_required'], '1')
                if corrupt:
                    self.assertEqual(result['backup_folder'], str(folder.resolve()))
                else:
                    self.assertEqual(result['backup_folder'], '')
                    self.assertIn('More than one', result['backup_message'])

    def test_probe_deduplicates_same_chain_discovered_by_source_and_launch_routing(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            app, game, client, saves, profile, values = self.fixture(root)
            game = self.steam_game(game, client)
            helper = root / 'old helper'
            folder = helper / 'setup-backups/game'
            self.snapshot(folder / '20260101', game, saves)
            self.put(game / uninstall.PLUGIN, b'legacy mod')
            self.steam_wrapper(profile, helper)
            result = self.probe(root, app, game, client, saves, source_root=helper)
            self.assertEqual(result['backup_folder'], str(folder.resolve()))

    def test_probe_recovers_original_without_losing_a_recorded_upgrade_in_another_folder(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            app, game, client, saves, profile, values = self.fixture(root)
            game = self.steam_game(game, client)
            helper = root / 'original helper'
            folder = helper / 'build-frame/backups'
            original = self.snapshot(folder / '20260101', game, saves)
            upgrade = self.snapshot(root / 'later helper/backups/20260102', game, saves,
                                    {str(uninstall.PLUGIN): b'previous mod'})
            manifest = json.loads((upgrade / 'manifest.json').read_text())
            manifest.update(previous_mod=True, installed=manifest['installed'] + ['ReShade.ini'])
            (upgrade / 'manifest.json').write_text(json.dumps(manifest))
            self.put(game / uninstall.PLUGIN, b'current mod')
            install.write_record(game, {'game': str(game), 'snapshots': [str(upgrade)]})
            self.steam_wrapper(profile, helper)
            result = self.probe(root, app, game, client, saves)
            self.assertEqual(result['backup_required'], '1')
            self.assertEqual(result['backup_folder'], str(folder.resolve()))
            chain = native.check_backups(game, app, Path(result['backup_folder']))
            self.assertEqual([path for path, _ in chain], [original, upgrade])
            self.assertIn(game / 'ReShade.ini', uninstall.restore_plan(game, chain))

    def test_steam_wrapper_for_another_game_copy_is_not_used(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            app, game, client, saves, profile, values = self.fixture(root)
            self.steam_game(game, client)
            other = root / 'another Steam copy'
            self.put(other / uninstall.PLUGIN, b'legacy mod')
            helper = root / 'other helper'
            self.snapshot(helper / 'setup-backups/game/20260101', other, saves)
            self.steam_wrapper(profile, helper)
            result = self.probe(root, app, other, client, saves)
            self.assertEqual(result['backup_required'], '1')
            self.assertEqual(result['backup_folder'], '')

    def test_battlenet_shortcut_discovery_requires_matching_icon_game_and_wrapper(self):
        for mismatch in ('none', 'legacy', 'icon', 'game', 'wrapper', 'truncated'):
            with self.subTest(mismatch=mismatch), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                app, game, client, saves, profile, values = self.fixture(root, 'battlenet')
                helper = root / 'old Battle.net 日本語 helper'
                folder = helper / 'setup-backups/game'
                self.snapshot(folder / '20260101', game, saves)
                self.put(game / uninstall.PLUGIN, b'legacy mod')
                shortcut = steam.update_shortcuts(None, client, game, ['steamvr'], store='battlenet',
                    python=helper / 'python/python.exe', wrapper=helper / 'steamvr/launch_windows.py',
                    runtime=client / 'steamapps/common/SteamVR/steamxr_win64.json')
                entries = steam.decode_binary(shortcut)
                item = entries['shortcuts']['0']
                if mismatch == 'icon':
                    item['icon'] = str(root / 'other game/D2R.exe')
                elif mismatch == 'game':
                    item['LaunchOptions'] = item['LaunchOptions'].replace(str(game), str(root / 'other game'))
                elif mismatch == 'wrapper':
                    item['StartDir'] = item['StartDir'].replace('steamvr', 'unrelated')
                elif mismatch == 'legacy':
                    # Shortcuts from 0.3.19 and earlier name the wrapper by its full path.
                    wrapper = '"' + str(helper / 'steamvr/launch_windows.py') + '"'
                    item['LaunchOptions'] = item['LaunchOptions'].replace('launch_windows.py', wrapper, 1)
                    item['StartDir'] = '"' + str(root) + '"'
                self.put(profile / 'config/shortcuts.vdf',
                         b'\x00shortcuts\x00\x02bad\x00' if mismatch == 'truncated' else steam.encode_binary(entries))
                result = self.probe(root, app, game, client, saves)
                self.assertEqual(result['backup_required'], '1')
                self.assertEqual(result['backup_folder'], str(folder.resolve()) if mismatch in ('none', 'legacy') else '')

    def test_adopted_backups_are_automatically_remembered_by_receipt_in_new_helper(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            app, game, client, saves, profile, values = self.fixture(root)
            source = root / 'old helper'
            folder = source / 'build-frame/backups'
            self.snapshot(folder / '20260101', game, saves)
            self.put(game / uninstall.PLUGIN, b'legacy mod')
            result = self.probe(root, app, game, client, saves, source_root=source)
            with patch.object(setup, 'require_closed'):
                native.adopt_backups(game, app, Path(result['backup_folder']))
            destination = app / 'setup-backups/game'
            new_helper = root / 'new installer helper'
            new_helper.mkdir()
            result = self.probe(root, new_helper, game, client, saves,
                                backup_hint=root / 'stale hint', source_root=source)
            self.assertEqual(result['backup_required'], '0')
            self.assertEqual(result['backup_folder'], str(destination.resolve()))
            self.assertEqual(len(install.read_record(game)['snapshots']), 1)
            self.assertEqual((saves / 'Character.d2s').read_bytes(), b'current character')

    def test_managed_receipts_keep_two_game_copies_and_same_named_snapshots_separate(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            app, game, client, saves, profile, values = self.fixture(root)
            other = root / 'another game copy'
            for index, target in enumerate((game, other)):
                self.put(target / uninstall.PLUGIN, b'legacy mod')
                folder = root / ('legacy-' + str(index))
                self.snapshot(folder / '20260101', target, saves)
                with patch.object(setup, 'require_closed'):
                    native.adopt_backups(target, app, folder)
            first = install.read_record(game)['snapshots']
            second = install.read_record(other)['snapshots']
            self.assertNotEqual(first, second)
            for target, expected in ((game, first), (other, second)):
                result = self.probe(root, app, target, client, saves)
                self.assertEqual(result['backup_required'], '0')
                chain = native.check_backups(target, app)
                self.assertEqual([str(path) for path, _ in chain], expected)
                self.assertEqual(Path(chain[0][1]['game']), target)

    def test_d2r_vr_in_the_game_folder_stops_the_wizard_without_touching_it(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            app, game, client, saves, profile, values = self.fixture(root)
            plugin = self.put(game / mod_identity.LEGACY_PLUGIN, b'MZ D2R VR BodyWalk Game Link')
            ini = self.put(game / mod_identity.LEGACY_SETTINGS, b'[mode]\nplatform=1\n')
            result = self.probe(root, app, game, client, saves)
            self.assertIn('D2R VR', result['other_mod'])
            self.assertEqual(result['installed_mod'], '')
            self.assertEqual(result['backup_required'], '0')
            for mode in ('repair', 'full'):
                with self.subTest(mode=mode), patch.object(setup, 'verify_release', return_value={'sha256': {}}):
                    with self.assertRaisesRegex(ValueError, 'Remove D2R VR with its own uninstaller'):
                        native.validate_choices({**values, 'mode': mode}, app)
            self.assertEqual(plugin.read_bytes(), b'MZ D2R VR BodyWalk Game Link')
            self.assertEqual(ini.read_bytes(), b'[mode]\nplatform=1\n')
            self.assertEqual(list(app.iterdir()), [])

    def test_old_release_under_old_names_is_offered_update_and_validates(self):
        for recorded in (True, False):
            with self.subTest(recorded=recorded), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                app, game, client, saves, profile, values = self.fixture(root)
                folder = root / 'old-zip/build-frame/backups'
                original = folder / '20261001'
                self.put(original / 'manifest.json', json.dumps(dict(game=str(game), saves=str(saves), previous_mod=False,
                    installed=[str(mod_identity.LEGACY_PLUGIN), str(mod_identity.LEGACY_SETTINGS), str(install.RECORD)])).encode())
                self.put(original / 'saves/Settings.json', b'{}')
                self.put(game / mod_identity.LEGACY_PLUGIN, b'MZ old D2R PCVR ' + 'd2r_vr_icons'.encode('utf-16-le'))
                self.put(game / mod_identity.LEGACY_SETTINGS, b'[body_calibration]\nheight=1.83\n')
                if recorded:
                    install.write_record(game, {'game': str(game), 'snapshots': [str(original)]})
                hint = None if recorded else folder
                result = self.probe(root, app, game, client, saves, backup_hint=hint)
                self.assertEqual(result['other_mod'], '')
                self.assertEqual(result['installed_mod'], 'd2r-pcvr')
                values = {**values, 'mode': 'repair', 'backups': '' if recorded else str(folder)}
                with patch.object(setup, 'verify_release', return_value={'sha256': {}}):
                    self.assertEqual(native.validate_choices(values, app)['mode'], 0)
                    if not recorded:
                        # Without the old backup the plugin is not provably this mod's.
                        with self.assertRaisesRegex(ValueError, 'did not install'):
                            native.validate_choices({**values, 'backups': ''}, app)

    def test_probe_only_arguments_are_rejected_for_install_actions(self):
        with patch.object(sys, 'argv', ['setup_native.py', '--install', 'fixture.ini', '--backup-hint', 'backup']), \
                contextlib.redirect_stderr(io.StringIO()), patch.object(setup, 'verify_release') as verify:
            with self.assertRaises(SystemExit):
                native.main()
        verify.assert_not_called()

    def test_selection_validates_both_editions_profiles_and_headset_connections(self):
        for store in ('steam', 'battlenet'):
            with self.subTest(store=store), tempfile.TemporaryDirectory() as directory:
                app, game, client, saves, profile, values = self.fixture(Path(directory), store)
                with patch.object(setup, 'verify_release', return_value={'sha256': {'bin/CascLib.dll': 'fixture'}}):
                    selected = native.validate_choices(values, app)
                    self.assertEqual(selected['store'], store)
                    self.assertEqual(selected['profile'], profile)
                    for edits, message in [({'profile': '../other'}, 'Steam account'),
                                           ({'connections': 'vdxr'}, 'SteamVR for Steam Frame'),
                                           ({'game': str(app)}, 'separate'),
                                           ({'mode': 'shortcuts'}, 'complete installed mod')]:
                        with self.assertRaisesRegex(ValueError, message):
                            native.validate_choices({**values, **edits}, app)
                    with patch('launch_windows.vdxr_manifest', return_value=client / 'missing.json'):
                        with self.assertRaisesRegex(ValueError, 'Virtual Desktop Streamer'):
                            native.validate_choices({**values, 'headset': 'Quest 3', 'connections': 'vdxr'}, app)
                    with patch('launch_windows.vdxr_manifest', return_value=client / 'steamapps/common/SteamVR/steamxr_win64.json'):
                        selected = native.validate_choices({**values, 'headset': 'Quest 3', 'connections': 'steamvr,vdxr'}, app)
                        self.assertEqual(selected['connections'], ['steamvr', 'vdxr'])

    def test_rejects_wrong_game_build_and_missing_battlenet_payload_before_writes(self):
        with tempfile.TemporaryDirectory() as directory:
            app, game, client, saves, profile, values = self.fixture(Path(directory), 'battlenet')
            with patch.object(setup, 'verify_release', return_value={'sha256': {}}):
                with self.assertRaisesRegex(ValueError, 'does not include Battle.net'):
                    native.validate_choices(values, app)
            self.put(game / 'D2R.exe', b'new unsupported version')
            with patch.object(setup, 'verify_release', return_value={'sha256': {'bin/CascLib.dll': 'fixture'}}):
                with self.assertRaisesRegex(ValueError, 'does not match'):
                    native.validate_choices(values, app)
            self.assertEqual(list(app.iterdir()), [])

    def test_utf8_and_utf16_choices_preserve_unicode_and_spaces(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'choices.ini'
            text = '[setup]\ngame=C:\\Games\\Diablo 日本語\nheadset=Quest 3\n'
            for encoding in ('utf-8-sig', 'utf-16'):
                path.write_text(text, encoding=encoding)
                self.assertEqual(native.read_choices(path)['game'], r'C:\Games\Diablo 日本語')

    def test_zip_backup_migration_survives_removal_of_original_folder_and_preserves_progress(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            app, game, client, saves, profile, values = self.fixture(root)
            old = self.snapshot(root / 'old-zip/setup-backups/game/20260101', game, saves)
            self.put(game / uninstall.PLUGIN, b'old mod')
            install.write_record(game, {'game': str(game), 'snapshots': [str(old)]})
            with patch.object(setup, 'require_closed'):
                native.adopt_backups(game, app)
            record = install.read_record(game)
            destination = Path(record['snapshots'][0])
            self.assertTrue(destination.is_relative_to(app))
            # Only a named fixture folder within the temporary workspace is removed.
            legacy = (root / 'old-zip').resolve()
            self.assertTrue(legacy.is_relative_to(root.resolve()))
            shutil.rmtree(legacy)
            chain = uninstall.snapshots(game, record, [app / 'setup-backups/game'])
            with patch.object(setup, 'require_closed'):
                uninstall.apply_updates(uninstall.restore_plan(game, chain), app / 'setup-backups', setup.require_closed)
            self.assertFalse((game / uninstall.PLUGIN).exists())
            self.assertEqual((saves / 'Character.d2s').read_bytes(), b'current character')
            self.assertEqual((saves / 'Settings.json').read_bytes(), b'{"Anti Aliasing": 2}')

    def test_corrupt_or_missing_imported_backup_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            app, game, client, saves, profile, values = self.fixture(Path(directory))
            old = self.snapshot(Path(directory) / 'zip/backups/20260101', game, saves)
            record = {'game': str(game), 'snapshots': [str(old)]}
            install.write_record(game, record)
            with patch.object(setup, 'require_closed'):
                native.adopt_backups(game, app)
            destination = Path(install.read_record(game)['snapshots'][0])
            (destination / 'saves/Settings.json').write_bytes(b'corrupt')
            install.write_record(game, record)
            with patch.object(setup, 'require_closed'), self.assertRaisesRegex(ValueError, 'conflicts'):
                native.adopt_backups(game, app)
            record['snapshots'] = [str(old / 'missing')]
            install.write_record(game, record)
            with self.assertRaisesRegex(ValueError, 'missing'):
                native.adopt_backups(game, app)

    def test_failed_install_restores_game_and_preserves_external_steam_edit(self):
        with tempfile.TemporaryDirectory() as directory:
            app, game, client, saves, profile, values = self.fixture(Path(directory))
            selection = dict(manifest={}, store='steam', version='3.3.93787', headset='Steam Frame',
                connections=['steamvr'], steam=client, game=game, profile=profile, saves=saves,
                runtime=client / 'steamapps/common/SteamVR/steamxr_win64.json', mode=1)
            def fail(**kwargs):
                self.snapshot(app / 'setup-backups/game/20260101', game, saves)
                self.put(game / uninstall.PLUGIN, b'partially installed mod')
                self.put(saves / 'Settings.json', b'{"changed": true}')
                self.put(profile / 'config/localconfig.vdf', (LOCAL + '\n// external edit').encode())
                raise OSError('fixture failure before Steam commit')
            with patch.object(setup, 'require_closed'), patch.object(native, 'validate_choices', return_value=selection), \
                    patch.object(setup, 'apply_setup', side_effect=fail), contextlib.redirect_stdout(io.StringIO()):
                with self.assertRaisesRegex(OSError, 'fixture failure'):
                    native.install_selected(values, app)
            self.assertFalse((game / uninstall.PLUGIN).exists())
            self.assertEqual((saves / 'Settings.json').read_bytes(), b'{"original": true}')
            self.assertEqual((saves / 'Character.d2s').read_bytes(), b'current character')
            self.assertTrue((profile / 'config/localconfig.vdf').read_text().endswith('// external edit'))
            self.assertEqual(native.managed_state(app)['installations'], [])

    def test_removal_combines_both_editions_and_keeps_other_shortcuts(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            app, game, client, saves, profile, values = self.fixture(root)
            other = root / 'battlenet'
            self.put(other / 'D2R.exe', b'updated game version allowed during removal')
            self.put(other / '.build.info')
            entries = []
            for target, store in ((game, 'steam'), (other, 'battlenet')):
                snapshot = self.snapshot(app / f'setup-backups/game/{store}', target, saves)
                self.put(target / uninstall.PLUGIN, b'mod')
                install.write_record(target, {'game': str(target), 'snapshots': [str(snapshot)]})
                entries.append({'game': str(target), 'store': store, 'steam': str(client), 'saves': str(saves)})
            options = setup.launch_options(app / '.runtime/python/python.exe', app / 'steamvr/launch_windows.py', None)
            self.put(profile / 'config/localconfig.vdf', steam.set_launch_options(LOCAL, options).encode())
            data = steam.encode_binary({'shortcuts': {'0': {'appname': 'Unrelated', 'Exe': 'other.exe'}}})
            data = steam.update_shortcuts(data, client, game, ['steamvr', 'vdxr'])
            data = steam.update_shortcuts(data, client, other, ['steamvr', 'vdxr'], store='battlenet',
                                         python=app / '.runtime/python/python.exe', wrapper=app / 'steamvr/launch_windows.py',
                                         runtime=client / 'steamapps/common/SteamVR/steamxr_win64.json')
            self.put(profile / 'config/shortcuts.vdf', data)
            native.atomic_json(app / native.STATE, {'installations': entries})
            with patch.object(setup, 'require_closed'), contextlib.redirect_stdout(io.StringIO()):
                native.remove_managed(app)
                native.remove_managed(app)  # Safe to rerun after removal.
            remaining = steam.decode_binary((profile / 'config/shortcuts.vdf').read_bytes())['shortcuts']
            self.assertEqual([item['appname'] for item in remaining.values()], ['Unrelated'])
            self.assertFalse((game / uninstall.PLUGIN).exists())
            self.assertFalse((other / uninstall.PLUGIN).exists())
            self.assertEqual((saves / 'Character.d2s').read_bytes(), b'current character')
            self.assertEqual((saves / 'Settings.json').read_bytes(), b'{"Anti Aliasing": 2}')
            self.assertEqual(native.managed_state(app)['installations'], [])

    def test_running_process_blocks_removal_without_any_mutation(self):
        with tempfile.TemporaryDirectory() as directory:
            app, game, client, saves, profile, values = self.fixture(Path(directory))
            with patch.object(setup, 'require_closed', side_effect=ValueError('Steam running')):
                with self.assertRaisesRegex(ValueError, 'Steam running'):
                    native.remove_managed(app)
            self.assertEqual(list(app.iterdir()), [])

    def test_preflight_rejects_missing_original_backup_before_helper_writes(self):
        with tempfile.TemporaryDirectory() as directory:
            app, game, client, saves, profile, values = self.fixture(Path(directory))
            self.put(game / uninstall.PLUGIN, b'installed mod')
            install.write_record(game, {'game': str(game), 'snapshots': [str(app / 'missing')]})
            with patch.object(setup, 'verify_release', return_value={'sha256': {}}):
                with self.assertRaisesRegex(ValueError, 'backup is missing'):
                    native.validate_choices(values, app)
            self.assertEqual(list(app.iterdir()), [])

    def test_failed_install_after_steam_commit_restores_original_configuration(self):
        with tempfile.TemporaryDirectory() as directory:
            app, game, client, saves, profile, values = self.fixture(Path(directory))
            localconfig, shortcuts = profile / 'config/localconfig.vdf', profile / 'config/shortcuts.vdf'
            original = localconfig.read_bytes()
            selection = dict(manifest={}, store='steam', version='3.3.93787', headset='Steam Frame',
                connections=['steamvr'], steam=client, game=game, profile=profile, saves=saves,
                runtime=client / 'steamapps/common/SteamVR/steamxr_win64.json', mode=0)
            def fail(**kwargs):
                self.snapshot(app / 'setup-backups/game/20260101', game, saves)
                self.put(game / uninstall.PLUGIN, b'new mod')
                self.put(localconfig, (LOCAL + '\n// committed by setup').encode())
                self.put(shortcuts, b'committed shortcuts')
                kwargs['on_steam_configured']()
                raise OSError('fixture failure after Steam commit')
            with patch.object(setup, 'require_closed'), patch.object(native, 'validate_choices', return_value=selection), \
                    patch.object(setup, 'apply_setup', side_effect=fail), contextlib.redirect_stdout(io.StringIO()):
                with self.assertRaisesRegex(OSError, 'after Steam commit'):
                    native.install_selected(values, app)
            self.assertEqual(localconfig.read_bytes(), original)
            self.assertFalse(shortcuts.exists())
            self.assertFalse((game / uninstall.PLUGIN).exists())
            self.assertEqual((saves / 'Character.d2s').read_bytes(), b'current character')

    def test_repeated_repair_preserves_calibration_and_keeps_one_managed_entry(self):
        with tempfile.TemporaryDirectory() as directory:
            app, game, client, saves, profile, values = self.fixture(Path(directory))
            calibration = self.put(game / mod_identity.SETTINGS, b'calibrated player')
            values['mode'] = 'repair'
            selection = dict(manifest={}, store='steam', version='3.3.93787', headset='Steam Frame',
                connections=['steamvr'], steam=client, game=game, profile=profile, saves=saves,
                runtime=client / 'steamapps/common/SteamVR/steamxr_win64.json', mode=0)
            def apply(**kwargs):
                self.assertEqual(kwargs['mode'], 0)
                self.assertTrue(kwargs['hide_console'])
                self.put(game / uninstall.PLUGIN, b'new plugin')
                if not install.read_record(game).get('snapshots'):
                    snapshot = self.snapshot(app / 'setup-backups/game/20260101', game, saves)
                    install.write_record(game, {'game': str(game), 'snapshots': [str(snapshot)]})
                kwargs['on_steam_configured']()
                return {'fixture': True}
            with patch.object(setup, 'require_closed'), patch.object(native, 'validate_choices', return_value=selection), \
                    patch.object(setup, 'apply_setup', side_effect=apply), contextlib.redirect_stdout(io.StringIO()):
                for _ in range(2):
                    self.assertEqual(native.install_selected(values, app), {'fixture': True})
            self.assertEqual(calibration.read_bytes(), b'calibrated player')
            self.assertEqual((saves / 'Settings.json').read_bytes(), b'{"Anti Aliasing": 2}')
            entries = native.managed_state(app)['installations']
            self.assertEqual(len(entries), 1)
            self.assertEqual(entries[0]['status'], 'installed')

    def test_shared_execution_configures_real_fixture_steam_and_removes_cleanly(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            app, game, client, saves, profile, values = self.fixture(root)
            runtime_python = app / '.runtime/python/python.exe'
            def install_files(command, helper_root, **kwargs):
                self.assertIn('--download-deps', command)
                self.assertIn(str(game), command)
                snapshot = self.snapshot(app / 'setup-backups/game/20260101', game, saves)
                self.put(game / uninstall.PLUGIN, b'new plugin')
                install.write_record(game, {'game': str(game), 'snapshots': [str(snapshot)]})
            with patch.object(setup, 'require_closed'), patch.object(setup, 'verify_release', return_value={'sha256': {}}), \
                    patch.object(setup.sys, 'executable', str(runtime_python)), \
                    patch.object(setup, 'run_installer', side_effect=install_files), \
                    contextlib.redirect_stdout(io.StringIO()):
                result = native.install_selected(values, app)
                self.assertIn(str(runtime_python), result['launch_options'])
                record = install.read_record(game)
                self.assertEqual(record['steam_profiles'][str(profile.resolve())]['previous_launch_options'], '-original-option')
                entries = steam.decode_binary((profile / 'config/shortcuts.vdf').read_bytes())['shortcuts']
                self.assertEqual(len(entries), 1)
                self.assertEqual(entries['0']['OpenVR'], 1)
                native.remove_managed(app)
            self.assertEqual((profile / 'config/localconfig.vdf').read_text(), LOCAL)
            self.assertEqual(steam.decode_binary((profile / 'config/shortcuts.vdf').read_bytes())['shortcuts'], {})
            self.assertFalse((game / uninstall.PLUGIN).exists())
            self.assertEqual((saves / 'Character.d2s').read_bytes(), b'current character')

    def test_hidden_battlenet_child_error_survives_rollback_with_saves_and_config_kept(self):
        with tempfile.TemporaryDirectory() as directory:
            app, game, client, saves, profile, values = self.fixture(Path(directory), 'battlenet')
            original_settings = (saves / 'Settings.json').read_bytes()
            original_config = (profile / 'config/localconfig.vdf').read_bytes()
            selection = dict(manifest={}, store='battlenet', version='3.3.93847', headset='Quest 3',
                connections=['steamvr'], steam=client, game=game, profile=profile, saves=saves,
                runtime=client / 'steamapps/common/SteamVR/steamxr_win64.json', mode=1)
            # This real child changes only the temporary fixture. It journals
            # its partial write just as install.py does, then reports a failure.
            child = textwrap.dedent('''
                import argparse, json, shutil, sys
                from pathlib import Path
                parser = argparse.ArgumentParser()
                for name in ('game', 'saves', 'backup-dir'):
                    parser.add_argument('--' + name, type=Path)
                args, _ = parser.parse_known_args()
                snapshot = args.backup_dir / 'fixture-failed-attempt'
                (snapshot / 'saves').mkdir(parents=True)
                shutil.copy2(args.saves / 'Settings.json', snapshot / 'saves/Settings.json')
                plugin = Path('mods/D2R-PCVR/d2rloader/plugins/d2rl-pcvr.dll')
                (snapshot / 'manifest.json').write_text(json.dumps(dict(game=str(args.game),
                    saves=str(args.saves), installed=[str(plugin)], previous_mod=False)))
                (args.game / plugin).parent.mkdir(parents=True, exist_ok=True)
                (args.game / plugin).write_bytes(b'partial fixture install')
                (args.saves / 'Settings.json').write_text('{"changed": true}')
                print('Preparing fixture Battle.net installation...', flush=True)
                raise SystemExit('PermissionError: fixture blocked mod file')
            ''')
            self.put(app / 'steamvr/install.py', child.encode('utf-8'))
            with patch.object(setup, 'require_closed'), \
                    patch.object(native, 'validate_choices', return_value=selection), \
                    contextlib.redirect_stdout(io.StringIO()) as output:
                with self.assertRaisesRegex(ValueError, 'PermissionError: fixture blocked mod file'):
                    native.install_selected(values, app)
            self.assertIn('PermissionError: fixture blocked mod file', output.getvalue())
            self.assertIn('Changes from this attempt were restored.', output.getvalue())
            log = (app / 'setup-install.log').read_text(encoding='utf-8')
            self.assertIn('PermissionError: fixture blocked mod file', log)
            self.assertIn('Installer exit code: 1', log)
            self.assertFalse((game / uninstall.PLUGIN).exists())
            self.assertFalse((game / install.RECORD).exists())
            self.assertEqual((saves / 'Settings.json').read_bytes(), original_settings)
            self.assertEqual((saves / 'Character.d2s').read_bytes(), b'current character')
            self.assertEqual((profile / 'config/localconfig.vdf').read_bytes(), original_config)
            self.assertFalse((profile / 'config/shortcuts.vdf').exists())
            self.assertEqual(native.managed_state(app)['installations'], [])

    def test_checksum_hashes_reach_wizard_and_log_without_game_changes(self):
        with tempfile.TemporaryDirectory() as directory:
            app, game, client, saves, profile, values = self.fixture(Path(directory))
            before = {path: path.read_bytes() for folder in (game, saves, profile)
                      for path in folder.rglob('*') if path.is_file()}
            selection = dict(manifest={}, store='steam', version='3.3.93787', headset='Quest 3',
                connections=['steamvr'], steam=client, game=game, profile=profile, saves=saves,
                runtime=client / 'steamapps/common/SteamVR/steamxr_win64.json', mode=1)
            received = '0' * 64
            message = str(install.dependency_checksum_error(
                'https://d2rloader.net/downloads/D2RLoader-1.3.1-beta.zip',
                install.LOADER_HASHES, received))
            self.put(app / 'steamvr/install.py', ('raise SystemExit(' + repr(message) + ')').encode('utf-8'))
            with patch.object(setup, 'require_closed'), \
                    patch.object(native, 'validate_choices', return_value=selection), \
                    contextlib.redirect_stdout(io.StringIO()):
                with self.assertRaises(ValueError) as caught:
                    native.install_selected(values, app)
            self.assertIn(message, str(caught.exception))
            log = (app / 'setup-install.log').read_text(encoding='utf-8')
            self.assertIn(message, log)
            self.assertIn('Installer exit code: 1', log)
            after = {path: path.read_bytes() for folder in (game, saves, profile)
                     for path in folder.rglob('*') if path.is_file()}
            self.assertEqual(after, before)
            self.assertEqual(native.managed_state(app)['installations'], [])

    def test_imports_original_legacy_backup_with_or_without_newer_receipt(self):
        for receipt in (False, True):
            with self.subTest(receipt=receipt), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                app, game, client, saves, profile, values = self.fixture(root)
                folder = root / 'old-zip/backups'
                original = self.snapshot(folder / '20260101', game, saves)
                self.put(game / uninstall.PLUGIN, b'legacy mod')
                if receipt:
                    upgrade = self.snapshot(folder / '20260102', game, saves, {str(uninstall.PLUGIN): b'legacy mod'})
                    manifest = json.loads((upgrade / 'manifest.json').read_text())
                    manifest['previous_mod'] = True
                    (upgrade / 'manifest.json').write_text(json.dumps(manifest))
                    install.write_record(game, {'game': str(game), 'snapshots': [str(upgrade)]})
                values['backups'] = str(folder)
                with patch.object(setup, 'verify_release', return_value={'sha256': {}}), patch.object(setup, 'require_closed'):
                    native.validate_choices(values, app)
                    native.adopt_backups(game, app, folder if not receipt else None)
                chain = uninstall.snapshots(game, install.read_record(game), [app / 'setup-backups/game'])
                self.assertEqual(len(chain), 2 if receipt else 1)
                self.assertTrue(all(path.is_relative_to(app) for path, _ in chain))
                self.assertEqual(uninstall.restore_plan(game, chain)[game / uninstall.PLUGIN], None)


if __name__ == '__main__':
    unittest.main()
