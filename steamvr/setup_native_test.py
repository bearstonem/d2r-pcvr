"""Graphical setup fixtures; never touch a real game, Steam profile or registry."""
import contextlib
import configparser
import hashlib
import io
import json
from pathlib import Path
import shutil
import tempfile
import textwrap
import unittest
from unittest.mock import patch

import setup_native as native
import setup_windows as setup
import steam_config as steam
import install
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
            calibration = self.put(game / 'd2rloader/plugins/d2r_vr.ini', b'calibrated player')
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
                plugin = Path('d2rloader/plugins/d2rl-vrcam.dll')
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


    def test_selected_extracted_folder_finds_only_valid_original_backups_without_writes(self):
        for layout in ('setup-backups/game', 'build-frame/backups'):
            with self.subTest(layout=layout), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                app, game, client, saves, profile, values = self.fixture(root)
                old = self.snapshot(root / 'moved ZIP' / layout / '20260101', game, saves)
                self.put(game / uninstall.PLUGIN, b'old mod')
                install.write_record(game, {'game': str(game), 'snapshots': [str(root / 'deleted ZIP' / layout / old.name)]})
                values['backups'] = str(root / 'moved ZIP')
                before = {str(path): path.read_bytes() for path in root.rglob('*') if path.is_file()}
                self.assertEqual(native.validate_backups(values, app)[0][0], old)
                after = {str(path): path.read_bytes() for path in root.rglob('*') if path.is_file()}
                self.assertEqual(before, after)
                with patch.object(setup, 'require_closed'):
                    native.adopt_backups(game, app, root / 'moved ZIP')
                self.assertTrue(Path(install.read_record(game)['snapshots'][0]).is_relative_to(app))

    def test_empty_wrong_game_and_deleted_backup_folders_are_rejected_before_install(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            app, game, client, saves, profile, values = self.fixture(root)
            self.put(game / uninstall.PLUGIN, b'old mod without a receipt')
            empty = root / 'empty'
            empty.mkdir()
            wrong = self.snapshot(root / 'other mod/setup-backups/game/20260101', root / 'other game', saves)
            for folder, message in [(empty, 'original pre-mod backup'), (wrong.parents[2], 'original pre-mod backup'),
                                    (root / 'deleted', 'Recycle Bin')]:
                with self.subTest(folder=folder), self.assertRaisesRegex(ValueError, message):
                    native.validate_backups({**values, 'backups': str(folder)}, app)
            self.assertEqual(list(app.iterdir()), [])
            self.assertEqual((game / uninstall.PLUGIN).read_bytes(), b'old mod without a receipt')
            self.assertEqual((saves / 'Character.d2s').read_bytes(), b'current character')

    def test_probe_reports_missing_backup_path_and_reason_for_wizard(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            app, game, client, saves, profile, values = self.fixture(root)
            missing = root / 'deleted ZIP/setup-backups/game/20260101'
            install.write_record(game, {'game': str(game), 'snapshots': [str(missing)]})
            report = root / 'detected.ini'
            with patch.object(setup, 'verify_release', return_value={'sha256': {}}), \
                    patch.object(setup, 'saved_games', return_value=saves), patch.object(setup, 'battlenet_location', return_value=None):
                native.write_probe(report, app, client, game, app)
            data = configparser.ConfigParser(interpolation=None)
            data.read(report, encoding='utf-16')
            self.assertEqual(data['detected']['backup_required'], '1')
            self.assertEqual(data['detected']['backup_path'], str(missing))
            self.assertIn(str(missing), data['detected']['backup_message'])
            self.assertEqual(list(app.iterdir()), [])

    def test_readiness_reports_live_processes_runtime_and_invalid_selections_without_mutation(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            app, game, client, saves, profile, values = self.fixture(root)
            before = {str(path): path.read_bytes() for path in root.rglob('*') if path.is_file()}
            with patch.object(setup, 'verify_release', return_value={'sha256': {}}), \
                    patch.object(setup, 'running_apps', return_value={'steam.exe', 'd2rloader.exe'}) as processes:
                report = native.readiness(values, app)
                self.assertEqual(report['ready'], '0')
                self.assertIn('Steam > Exit', report['steam'])
                self.assertIn('Save & Exit', report['game'])
                processes.return_value = set()
                self.assertEqual(native.readiness(values, app)['ready'], '1')
                report = native.readiness({**values, 'steamvr': str(root / 'missing runtime'), 'profile': 'wrong'}, app)
                self.assertEqual(report['ready'], '0')
                self.assertIn('cannot read', report['runtime'])
                self.assertIn('Steam account', report['selections'])
                processes.side_effect = OSError('tasklist unavailable')
                report = native.readiness(values, app)
                self.assertEqual(report['ready'], '0')
                self.assertIn('tasklist unavailable', report['steam'])
            after = {str(path): path.read_bytes() for path in root.rglob('*') if path.is_file()}
            self.assertEqual(before, after)

    def test_installation_check_detects_missing_changed_files_and_stale_shortcuts_for_both_editions(self):
        for store in ('steam', 'battlenet'):
            with self.subTest(store=store), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                app, game, client, saves, profile, values = self.fixture(root, store)
                values.update(headset='Quest 3', connections='steamvr,vdxr')
                for name in ('D2RLoader.exe', 'D2RCore.dll', 'D2RLoader.mpq', 'ReShade64.dll', 'openxr_loader.dll',
                             str(uninstall.PLUGIN), 'd2rloader/plugins/d2r_vr.ini'):
                    self.put(game / name)
                runtime = client / 'steamapps/common/SteamVR/steamxr_win64.json'
                routing = {} if store == 'steam' else dict(store=store, python=app / '.runtime/python/python.exe',
                    wrapper=app / 'steamvr/launch_windows.py', runtime=runtime)
                self.put(profile / 'config/shortcuts.vdf', steam.update_shortcuts(None, client, game, ['steamvr', 'vdxr'], **routing))
                if store == 'steam':
                    options = setup.launch_options(app / '.runtime/python/python.exe', app / 'steamvr/launch_windows.py', runtime)
                    self.put(profile / 'config/localconfig.vdf', steam.set_launch_options(LOCAL, options).encode())
                manifest = {'sha256': {'bin/d2rl-vrcam.dll': hashlib.sha256(b'fixture').hexdigest(),
                                       'bin/openxr_loader.dll': hashlib.sha256(b'fixture').hexdigest()}}
                with patch.object(setup, 'verify_release', return_value=manifest), \
                        patch('launch_windows.vdxr_manifest', return_value=runtime):
                    report = native.inspect_installation(values, app)
                    self.assertEqual(report['ready'], '1', report)
                    self.assertIn('Virtual Desktop', report['launch'])
                    self.assertEqual('(Battle.net)' in report['launch'], store == 'battlenet')
                    (game / 'openxr_loader.dll').unlink()
                    self.put(game / uninstall.PLUGIN, b'corrupt DLL')
                    entries = steam.decode_binary((profile / 'config/shortcuts.vdf').read_bytes())
                    entries['shortcuts']['0']['LaunchOptions'] = 'deleted ZIP wrapper'
                    self.put(profile / 'config/shortcuts.vdf', steam.encode_binary(entries))
                    before = {str(path): path.read_bytes() for path in root.rglob('*') if path.is_file()}
                    report = native.inspect_installation(values, app)
                    self.assertEqual(report['ready'], '0')
                    self.assertIn('openxr_loader.dll', report['files'])
                    self.assertIn('Checksum mismatch', report['files'])
                    self.assertIn('outdated', report['shortcuts'])
                    self.assertEqual(before, {str(path): path.read_bytes() for path in root.rglob('*') if path.is_file()})

    def test_report_preserves_unicode_percent_and_flattens_multiline_errors_for_inno(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'checks.ini'
            native.write_report(path, 'checks', dict(files='C:\\Games\\日本語 100%\\file.dll\nPermission denied'))
            parser = configparser.ConfigParser(interpolation=None)
            parser.read(path, encoding='utf-16')
            self.assertEqual(parser['checks']['files'], 'C:\\Games\\日本語 100%\\file.dll Permission denied')

    def test_readiness_cli_writes_report_and_still_blocks_changed_process_state_at_install(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            app, game, client, saves, profile, values = self.fixture(root)
            choices, report = root / 'choices.ini', root / 'checks.ini'
            parser = configparser.ConfigParser(interpolation=None)
            parser['setup'] = values
            with choices.open('w', encoding='utf-16') as output:
                parser.write(output)
            with patch.object(native, 'ROOT', app), patch.object(setup, 'verify_release', return_value={'sha256': {}}), \
                    patch.object(setup, 'running_apps', return_value=set()) as processes, \
                    patch.object(native.sys, 'argv', ['setup_native.py', '--readiness', str(choices), '--report', str(report)]), \
                    contextlib.redirect_stdout(io.StringIO()):
                native.main()
                result = configparser.ConfigParser(interpolation=None)
                result.read(report, encoding='utf-16')
                self.assertEqual(result['checks']['ready'], '1')
                processes.return_value = {'steam.exe'}  # Steam reopened after the readiness check.
                with patch.object(native.sys, 'argv', ['setup_native.py', '--install', str(choices)]), \
                        self.assertRaisesRegex(ValueError, 'Steam > Exit'):
                    native.main()
            self.assertEqual(list(app.iterdir()), [])
            self.assertEqual((saves / 'Character.d2s').read_bytes(), b'current character')

    def test_archiving_old_mod_allows_fresh_selection_without_lost_backups_or_save_changes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            app, game, client, saves, profile, values = self.fixture(root, 'battlenet')
            self.put(game / uninstall.PLUGIN, b'old mod')
            self.put(game / 'D2RLoader.exe', b'old loader')
            self.put(game / 'Data/untouched-game-data', b'original game data')
            self.put(game / 'mods/unrelated-mod', b'other mod')
            install.write_record(game, {'game': str(game), 'snapshots': [str(root / 'permanently deleted backup')]})
            with patch.object(setup, 'verify_release', return_value={'sha256': {'bin/CascLib.dll': 'fixture'}}):
                with self.assertRaisesRegex(ValueError, 'backup is missing'):
                    native.validate_choices(values, app)
                archive = root / 'archived mod'
                archive.mkdir()
                for name in ('d2rloader', 'D2RLoader.exe'):
                    source, target = (game / name).resolve(), (archive / name).resolve()
                    self.assertTrue(source.is_relative_to(root.resolve()))
                    self.assertTrue(target.is_relative_to(root.resolve()))
                    shutil.move(source, target)
                self.assertEqual(native.validate_choices(values, app)['mode'], 1)
            self.assertEqual(install.read_record(game), {})
            self.assertEqual((game / 'Data/untouched-game-data').read_bytes(), b'original game data')
            self.assertEqual((game / 'mods/unrelated-mod').read_bytes(), b'other mod')
            self.assertEqual((saves / 'Character.d2s').read_bytes(), b'current character')
            self.assertTrue((archive / 'd2rloader' / install.RECORD.relative_to('d2rloader')).is_file())


if __name__ == '__main__':
    unittest.main()
