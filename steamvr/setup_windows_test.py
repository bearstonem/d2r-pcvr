"""Setup safety and routing fixtures. Never read/write the user's Steam profile."""
import contextlib
import io
import hashlib
import json
import shutil
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import textwrap
import unittest
from unittest.mock import patch

import setup_windows as setup
import steam_config as config


LOCAL = '"UserLocalConfigStore"\r\n{\r\n// untouched comment\r\n"friends" { "PersonaName" "Player" }\r\n"Software" { "Valve" { "Steam" { "apps" { "2536520" { "LaunchOptions" "old option" "Other" "keep" } "123" { "LaunchOptions" "unrelated" } } } } }\r\n"PrivateData" "fixture only"\r\n}\r\n'


class SteamSetupTests(unittest.TestCase):
    def test_release_integrity_rejects_changed_file_and_escaping_path(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            names = ('bin/d2rl-pcvr.dll', 'bin/openxr_loader.dll', 'steamvr/install.py', 'steamvr/mod_identity.py',
                     'steamvr/setup_windows.py', 'steamvr/steam_config.py', 'steamvr/launch_windows.py', 'steamvr/prepare_dev_scenarios.py',
                     'steamvr/game_install.py', 'steamvr/game_assets.py', 'steamvr/casc_assets.py',
                     'steamvr/prepare_sewer_backdrop.py',
                     'Uninstall-Windows.cmd', 'steamvr/uninstall_windows.py')
            hashes = {}
            for name in names:
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b'fixture')
                hashes[name] = hashlib.sha256(b'fixture').hexdigest()
            manifest = {'platform': 'native-windows', 'sha256': hashes}
            source = root / 'release-manifest.json'
            source.write_text(json.dumps(manifest))
            self.assertEqual(setup.verify_release(root), manifest)
            (root / names[0]).write_bytes(b'changed')
            with self.assertRaisesRegex(ValueError, 'missing or changed'):
                setup.verify_release(root)
            (root / names[0]).write_bytes(b'fixture')
            hashes['../outside'] = hashes[names[0]]
            source.write_text(json.dumps(manifest))
            with self.assertRaisesRegex(ValueError, 'Invalid path'):
                setup.verify_release(root)

    def test_launch_options_change_only_target_span(self):
        options = setup.launch_options(Path('C:/Mods with spaces/python.exe'), Path('C:/Mods with spaces/launch_windows.py'), Path('D:/Steam VR/steamxr_win64.json'))
        updated = config.set_launch_options(LOCAL, options)
        self.assertEqual(updated, LOCAL.replace('"old option"', config.quote(options)))
        self.assertEqual(config.text_value(config.parse_text(updated), 'UserLocalConfigStore', 'Software', 'Valve', 'Steam', 'apps', '2536520', 'LaunchOptions'), options)
        self.assertEqual(config.set_launch_options(updated, options), updated)
        self.assertEqual(options.count('%command%'), 1)
        self.assertNotIn('--frame-vr', options)

    def test_missing_app_and_ancestors_and_bom(self):
        for local in ('"UserLocalConfigStore" {}', '"UserLocalConfigStore" { "Software" { "Valve" { "Steam" { "apps" {} } } } }', '\ufeff' + LOCAL):
            updated = config.set_launch_options(local, '"C:\\mod\\python.exe" %command%')
            value = config.text_value(config.parse_text(updated.lstrip('\ufeff')), 'UserLocalConfigStore', 'Software', 'Valve', 'Steam', 'apps', '2536520', 'LaunchOptions')
            self.assertEqual(value, '"C:\\mod\\python.exe" %command%')
            self.assertEqual(updated.startswith('\ufeff'), local.startswith('\ufeff'))

    def test_ambiguous_or_malformed_configuration_rejected(self):
        for local in ('"UserLocalConfigStore" { "Software" {} "software" {} }', '"UserLocalConfigStore" {', '"OtherRoot" {}'):
            with self.assertRaises(ValueError):
                config.set_launch_options(local, 'new')
        for data in (b'\x00shortcuts\0', b'\x07bad\0\x08', b'\x08trailing'):
            with self.assertRaises(ValueError):
                config.update_shortcuts(data, Path('steam'), Path('game'), ['steamvr'])

    def test_shortcuts_idempotent_preserve_other_entries_and_select_runtime(self):
        other = {'appid': 0xfedcba98, 'appname': 'Other Game', 'Exe': '"other.exe"', 'tags': {'0': 'favorites'}, 'UnknownField': 'retained'}
        original = config.encode_binary({'shortcuts': {'0': other}})
        updated = config.update_shortcuts(original, Path('C:/Steam'), Path('D:/D2R'), ['steamvr', 'vdxr'])
        self.assertEqual(config.update_shortcuts(updated, Path('C:/Steam'), Path('D:/D2R'), ['steamvr', 'vdxr']), updated)
        entries = config.decode_binary(updated)['shortcuts']
        self.assertEqual(entries['0'], other)
        self.assertEqual(entries['1']['OpenVR'], 1)
        self.assertEqual(entries['2']['OpenVR'], 0)
        self.assertEqual(entries['1']['LaunchOptions'], '-applaunch 2536520 --frame-vr')
        self.assertEqual(entries['2']['LaunchOptions'], '-applaunch 2536520 --frame-vr --frame-vdxr')
        only_vd = config.decode_binary(config.update_shortcuts(None, Path('Steam'), Path('Game'), ['vdxr']))['shortcuts']
        self.assertEqual(len(only_vd), 1)
        self.assertEqual(only_vd['0']['OpenVR'], 0)

    def fixture(self, root):
        profile = root / 'userdata/123'
        directory = profile / 'config'
        directory.mkdir(parents=True)
        (directory / 'localconfig.vdf').write_bytes(LOCAL.encode())
        return profile, directory

    def test_transaction_backup_and_absent_shortcuts(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            profile, directory = self.fixture(root)
            backup = config.configure(profile, root, root, 'new', ['vdxr'], root / 'backups', lambda: None)
            self.assertEqual((backup / 'localconfig.vdf').read_bytes(), LOCAL.encode())
            self.assertFalse((backup / 'shortcuts.vdf').exists())
            self.assertEqual((directory / 'localconfig.vdf').read_bytes(), LOCAL.replace('"old option"', '"new"').encode())
            self.assertEqual(config.decode_binary((directory / 'shortcuts.vdf').read_bytes())['shortcuts']['0']['OpenVR'], 0)
            self.assertFalse(list(directory.glob('d2r-setup-*')))

    def test_second_file_write_failure_rolls_back_first(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            profile, directory = self.fixture(root)
            old_shortcuts = config.encode_binary({'shortcuts': {}})
            (directory / 'shortcuts.vdf').write_bytes(old_shortcuts)
            replace = config.os.replace
            calls = 0
            def fail_second(source, target):
                nonlocal calls
                calls += 1
                if calls == 2:
                    raise PermissionError('fixture blocked write')
                return replace(source, target)
            with patch.object(config.os, 'replace', side_effect=fail_second), self.assertRaises(PermissionError):
                config.configure(profile, root, root, 'new', ['steamvr'], root / 'backups', lambda: None)
            self.assertEqual((directory / 'localconfig.vdf').read_bytes(), LOCAL.encode())
            self.assertEqual((directory / 'shortcuts.vdf').read_bytes(), old_shortcuts)
            self.assertFalse(list(directory.glob('d2r-setup-*')))

    def test_steam_reopening_or_external_change_prevents_commit(self):
        for race in ('steam', 'config'):
            with self.subTest(race=race), tempfile.TemporaryDirectory() as temp:
                root = Path(temp)
                profile, directory = self.fixture(root)
                calls = 0
                def check():
                    nonlocal calls
                    calls += 1
                    if calls == 2:
                        if race == 'steam':
                            raise ValueError('Steam reopened')
                        (directory / 'localconfig.vdf').write_text('changed externally')
                with self.assertRaises(ValueError):
                    config.configure(profile, root, root, 'new', ['steamvr'], root / 'backups', check)
                expected = LOCAL.encode() if race == 'steam' else b'changed externally'
                self.assertEqual((directory / 'localconfig.vdf').read_bytes(), expected)
                self.assertFalse((directory / 'shortcuts.vdf').exists())

    def test_discovery_uses_separate_libraries_and_limits_manifest_path(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            steam = root / 'Steam'
            library = root / 'Second library'
            (steam / 'steamapps').mkdir(parents=True)
            (library / 'steamapps/common/D2R').mkdir(parents=True)
            (steam / 'steamapps/libraryfolders.vdf').write_text('"libraryfolders" { "0" { "path" ' + config.quote(str(steam)) + ' } "1" { "path" ' + config.quote(str(library)) + ' } }')
            manifest = library / 'steamapps/appmanifest_2536520.acf'
            manifest.write_text('"AppState" { "installdir" "D2R" }')
            roots = setup.libraries(steam)
            self.assertEqual(roots, [steam, library])
            self.assertEqual(setup.app_folder(roots, '2536520'), library / 'steamapps/common/D2R')
            manifest.write_text('"AppState" { "installdir" "../../../escape" }')
            self.assertIsNone(setup.app_folder(roots, '2536520'))

    def test_wizard_quest_vdxr_shortcuts_only_and_frame_install_routing(self):
        for quest, battlenet_in_package in ((True, False), (False, False), (True, True), (False, True)):
            with self.subTest(quest=quest, battlenet_in_package=battlenet_in_package), tempfile.TemporaryDirectory() as temp:
                root = Path(temp)
                shutil.copytree(Path(__file__).resolve().parent.parent / "artwork", root / "artwork")
                steam = root / 'steam'
                game = root / 'game'
                saves = root / 'saves'
                steam.mkdir()
                game.mkdir()
                saves.mkdir()
                profile, _ = self.fixture(steam)
                (game / 'D2R.exe').write_bytes('3.3.93787'.encode('utf-16-le'))
                (game / 'Data').mkdir()
                (game / 'Data/.build.config').write_text('fixture')
                for name in ('D2RLoader.exe', 'mods/D2R-PCVR/reshade/ReShade64.dll', 'mods/D2R-PCVR/d2rloader/plugins/openxr_loader.dll',
                             'mods/D2R-PCVR/d2rloader/plugins/d2rl-pcvr.dll', 'mods/D2R-PCVR/d2rloader/plugins/d2r_pcvr.ini'):
                    file = game / name
                    file.parent.mkdir(parents=True, exist_ok=True)
                    file.write_bytes(b'fixture')
                # Quest: headset 2, VD 2, shortcut-only 3, continue 1, open later 2.
                # Frame: headset 1, preserve upgrade 1, continue 1, open later 2.
                answers = ['2', '2', '3', '1', '2'] if quest else ['1', '1', '1', '2']
                if battlenet_in_package:
                    answers.insert(0, '1')  # Select Steam in a package that offers both editions.
                manifest = {'release_tag': 'fixture', 'sha256': {'bin/CascLib.dll': 'fixture'} if battlenet_in_package else {}}
                folders = [steam, game, saves] if quest else [steam, game, root, saves]
                with patch.object(setup, 'ROOT', root), patch.object(setup, 'verify_release', return_value=manifest), \
                        patch.object(setup, 'folder', side_effect=folders), patch.object(setup, 'steam_location', return_value=steam), \
                        patch.object(setup, 'saved_games', return_value=saves), patch.object(setup, 'wait_for_closed'), \
                        patch.object(setup, 'require_closed'), patch.object(setup.subprocess, 'run') as run, \
                        patch.object(setup.subprocess, 'Popen') as popen, patch('builtins.input', side_effect=answers), \
                        patch('launch_windows.vdxr_manifest', return_value=game / 'mods/D2R-PCVR/d2rloader/plugins/openxr_loader.dll'), \
                        patch.object(setup.sys, 'argv', ['setup_windows.py']), contextlib.redirect_stdout(io.StringIO()) as output:
                    setup.main()
                result = json.loads((root / 'setup-result.json').read_text())
                self.assertEqual(result['store'], 'steam')
                self.assertFalse(result['experimental'])
                state = setup.install.read_record(game)['steam_profiles'][str(profile.resolve())]
                self.assertEqual(state['previous_launch_options'], 'old option')
                self.assertEqual(state['installed_launch_options'], result['launch_options'])
                self.assertEqual('Which game installation?' in output.getvalue(), battlenet_in_package)
                self.assertEqual(result['connections'], ['vdxr'] if quest else ['steamvr'])
                entries = config.decode_binary((profile / 'config/shortcuts.vdf').read_bytes())['shortcuts']
                self.assertEqual(entries['0']['OpenVR'], 0 if quest else 1)
                if quest:
                    run.assert_not_called()
                    self.assertNotIn('--frame-openxr-runtime=', result['launch_options'])
                else:
                    self.assertIn('--preserve-settings', run.call_args.args[0])
                    self.assertNotIn('--experimental-battlenet', run.call_args.args[0])
                    self.assertIn('--frame-openxr-runtime=', result['launch_options'])
                popen.assert_not_called()


class InstallerOutputTests(unittest.TestCase):
    def test_security_recovery_message_reaches_wizard_without_losing_start_or_reminder(self):
        import install
        error = OSError(22, 'Invalid argument')
        error.winerror = 225
        long_path = Path('C:/fixture') / ('nested-folder/' * 50) / 'D2RLoader.zip'
        message = str(install.dependency_io_error(long_path, 'read', error))
        self.assertGreater(len(message), 800)
        with tempfile.TemporaryDirectory() as directory, contextlib.redirect_stdout(io.StringIO()):
            root = Path(directory)
            child = 'import sys; raise SystemExit(sys.argv[1])'
            with self.assertRaises(ValueError) as caught:
                setup.run_installer([sys.executable, '-c', child, message], root, hide_console=True)
            self.assertIn(message, str(caught.exception))
            self.assertIn(message, (root / 'setup-install.log').read_text(encoding='utf-8'))

    @unittest.skipUnless(os.name == 'nt', 'Windows hidden-process pipe inheritance')
    def test_hidden_child_failure_reaches_wizard_output_and_persistent_log(self):
        with tempfile.TemporaryDirectory(prefix='setup with spaces ') as directory:
            root = Path(directory)
            parent = textwrap.dedent('''
                import sys
                from pathlib import Path
                sys.path.insert(0, sys.argv[1])
                import setup_windows
                try:
                    setup_windows.run_installer([sys.executable, '-c', sys.argv[3], sys.argv[2]],
                                                Path(sys.argv[2]), hide_console=True)
                except ValueError as error:
                    print(error, file=sys.stderr, flush=True)
                    raise SystemExit(1)
            ''')
            child = textwrap.dedent('''
                import sys
                from pathlib import Path
                marker = Path(sys.argv[1]) / 'attempts.txt'
                with marker.open('a') as output:
                    output.write('one attempt\\n')
                print('Preparing installation...', flush=True)
                print('PermissionError: fixture locked file \\u65e5\\u672c', file=sys.stderr, flush=True)
                raise SystemExit(1)
            ''')
            completed = subprocess.run([sys.executable, '-X', 'utf8', '-u', '-c', parent,
                str(Path(setup.__file__).parent), str(root), child], capture_output=True,
                text=True, encoding='utf-8', timeout=15, creationflags=0x08000000)
            self.assertEqual(completed.returncode, 1)
            self.assertIn('Preparing installation...', completed.stdout)
            self.assertIn('PermissionError: fixture locked file \u65e5\u672c', completed.stdout)
            self.assertIn('PermissionError: fixture locked file \u65e5\u672c', completed.stderr)
            self.assertIn(str(root / 'setup-install.log'), completed.stderr)
            self.assertNotIn('returned non-zero exit status', completed.stderr)
            self.assertEqual((root / 'attempts.txt').read_text(), 'one attempt\n')
            log = (root / 'setup-install.log').read_text(encoding='utf-8')
            self.assertIn('PermissionError: fixture locked file \u65e5\u672c', log)
            self.assertIn('Installer exit code: 1 (0x00000001)', log)

    def test_hidden_success_relays_both_streams_and_preserves_parent_environment(self):
        with tempfile.TemporaryDirectory() as directory, \
                patch.dict(os.environ, {'PYTHONIOENCODING': 'ascii', 'D2R_SETUP_FIXTURE': 'retained'}), \
                contextlib.redirect_stdout(io.StringIO()) as output:
            root = Path(directory)
            before = dict(os.environ)
            child = ('import os,sys; print("Ready \\u65e5\\u672c"); '
                     'print(os.environ["D2R_SETUP_FIXTURE"]); '
                     'print("Warning from stderr", file=sys.stderr)')
            setup.run_installer([sys.executable, '-c', child], root, hide_console=True)
            self.assertEqual(dict(os.environ), before)
            log = (root / 'setup-install.log').read_text(encoding='utf-8')
            for message in ('Ready \u65e5\u672c', 'retained', 'Warning from stderr'):
                self.assertIn(message, output.getvalue())
                self.assertIn(message, log)
            self.assertIn('Installer exit code: 0 (0x00000000)', log)

    def test_child_exit_without_output_still_reports_code_and_log(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with self.assertRaisesRegex(ValueError, 'exit code 7.*no diagnostic output.*setup-install.log'):
                setup.run_installer([sys.executable, '-c', 'raise SystemExit(7)'], root, hide_console=True)
            self.assertIn('Installer exit code: 7', (root / 'setup-install.log').read_text())


if __name__ == '__main__':
    unittest.main()
