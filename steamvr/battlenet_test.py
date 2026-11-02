"""Store/build gates and Battle.net launch isolation, using temporary fixtures."""
import ctypes
import contextlib
import io
import json
import os
import shutil
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import casc_assets
import game_assets
import game_install
import launch_windows as launch
import setup_windows as setup
import steam_config as steam


class BattleNetTests(unittest.TestCase):
    def game(self, root, store='battlenet', version='3.3.93847'):
        path = root / store
        path.mkdir()
        (path / 'D2R.exe').write_bytes(version.encode('utf-16-le'))
        (path / 'D2RLoader.exe').write_bytes(b'fixture loader')
        if store == 'steam':
            (path / 'Data').mkdir()
            (path / 'Data/.build.config').write_text('fixture steam config')
        else:
            (path / '.build.info').write_text('fixture local build config')
        return path

    def test_store_version_and_experimental_gates(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            bnet = self.game(root)
            stock = self.game(root, 'steam', '3.3.93787')
            self.assertEqual(game_install.validate_game(stock), ('steam', '3.3.93787'))
            with self.assertRaisesRegex(ValueError, 'experimental'):
                game_install.validate_game(bnet)
            self.assertEqual(game_install.validate_game(bnet, experimental_battlenet=True), ('battlenet', '3.3.93847'))
            with self.assertRaisesRegex(ValueError, 'selected'):
                game_install.validate_game(bnet, 'steam', True)
            (bnet / 'D2R.exe').write_bytes('3.3.99999'.encode('utf-16-le'))
            with self.assertRaisesRegex(ValueError, 'does not match'):
                game_install.validate_game(bnet, experimental_battlenet=True)

    def test_steam_accepts_the_new_build_and_the_previous_one(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            stock = self.game(root, 'steam', '3.3.94995')
            self.assertEqual(game_install.STEAM_VERSION, '3.3.94995')
            self.assertEqual(game_install.validate_game(stock), ('steam', '3.3.94995'))
            (stock / 'D2R.exe').write_bytes('3.3.93787'.encode('utf-16-le'))
            self.assertEqual(game_install.validate_game(stock), ('steam', '3.3.93787'))
            # Another build is refused, naming the one found in the version resource.
            resource = b'\0\0'.join(['FileVersion'.encode('utf-16-le'), '3.3.99999'.encode('utf-16-le'), b''])
            (stock / 'D2R.exe').write_bytes(b'MZ' + resource)
            with self.assertRaisesRegex(ValueError, r'3\.3\.94995 or 3\.3\.93787; your game is 3\.3\.99999\. Your game version does not match'):
                game_install.validate_game(stock)
            self.assertEqual(game_install.file_version(b'MZ'), None)

    def test_asset_reader_selection_and_cache_invalidation(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            bnet = self.game(root)
            stock = self.game(root, 'steam', '3.3.93787')
            before = game_assets.build_hash(bnet)
            (bnet / '.build.info').write_text('new build')
            self.assertNotEqual(game_assets.build_hash(bnet), before)
            with patch.object(game_assets, 'SteamAssets') as static, patch.object(casc_assets, 'CascAssets') as casc:
                game_assets.open_assets(stock)
                static.assert_called_once_with(stock)
                casc.assert_not_called()
                library = root / 'CascLib.dll'
                game_assets.open_assets(bnet, library)
                casc.assert_called_once_with(bnet, library)

    def test_explicit_battlenet_launch_strips_wrapper_flags_and_steam_ids(self):
        with tempfile.TemporaryDirectory(prefix='mod with spaces ') as tmp:
            root = Path(tmp)
            bnet = self.game(root)
            runtime = root / 'Steam VR/steamxr_win64.json'
            runtime.parent.mkdir()
            runtime.write_text('{}')
            args = [launch.BATTLENET_OPTION + str(bnet), launch.EXPERIMENTAL_OPTION,
                    '--frame-vr', launch.RUNTIME_OPTION + str(runtime)]
            self.assertEqual(launch.prepare(args), ([str(bnet / 'D2RLoader.exe'), '-mod', 'D2R-PCVR'], str(bnet)))
            with patch.dict(os.environ, {'SteamAppId': '2536520', 'SteamGameId': '999', 'SteamOverlayGameId': '888', 'D2R_FIXTURE': 'keep'}):
                environment = launch.launch_environment(args)
                self.assertFalse(any(k.lower() in ('steamappid', 'steamgameid', 'steamoverlaygameid') for k in environment))
                self.assertEqual(environment['D2R_FIXTURE'], 'keep')
                self.assertEqual(environment['XR_RUNTIME_JSON'], str(runtime.resolve()))
                self.assertEqual(os.environ['SteamAppId'], '2536520')
            for invalid in (args + ['-online'], args + [launch.BATTLENET_OPTION + str(bnet)],
                            [a for a in args if a != launch.EXPERIMENTAL_OPTION],
                            [a for a in args if a != '--frame-vr']):
                with self.subTest(invalid=invalid), self.assertRaises(SystemExit):
                    launch.prepare(invalid)

    def test_shortcuts_coexist_and_preserve_steam_launch_options(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            bnet = self.game(root)
            config = root / 'userdata/123/config'
            config.mkdir(parents=True)
            local = b'fixture Steam launch options must stay byte-identical'
            (config / 'localconfig.vdf').write_bytes(local)
            original = steam.update_shortcuts(None, root, root / 'steam game', ['steamvr', 'vdxr'])
            (config / 'shortcuts.vdf').write_bytes(original)
            routing = {'store': 'battlenet', 'python': root / 'python.exe',
                       'wrapper': root / 'mod with spaces/launch_windows.py',
                       'runtime': root / 'Steam VR/steamxr_win64.json'}
            backup = steam.configure(config.parent, root, bnet, None, ['steamvr', 'vdxr'], root / 'backups', lambda: None, **routing)
            self.assertEqual((config / 'localconfig.vdf').read_bytes(), local)
            self.assertFalse((backup / 'localconfig.vdf').exists())
            updated = (config / 'shortcuts.vdf').read_bytes()
            self.assertEqual(steam.update_shortcuts(updated, root, bnet, ['steamvr', 'vdxr'], **routing), updated)
            entries = steam.decode_binary(updated)['shortcuts']
            originals = steam.decode_binary(original)['shortcuts']
            self.assertEqual(entries['0'], originals['0'])
            self.assertEqual(entries['1'], originals['1'])
            self.assertEqual(len(entries), 4)
            for key in ('2', '3'):
                self.assertIn('(Battle.net)', entries[key]['appname'])
                self.assertNotIn('-applaunch', entries[key]['LaunchOptions'])
                self.assertIn('--battlenet-game=', entries[key]['LaunchOptions'])
            self.assertIn('--frame-openxr-runtime=', entries['2']['LaunchOptions'])
            self.assertIn('--frame-vdxr', entries['3']['LaunchOptions'])
            vd_only = {**routing, 'runtime': None}
            steam.update_shortcuts(None, root, bnet, ['vdxr'], **vd_only)
            with self.assertRaisesRegex(ValueError, 'explicit SteamVR runtime'):
                steam.update_shortcuts(None, root, bnet, ['steamvr'], **vd_only)

    def test_pinned_casc_abi_layout(self):
        self.assertEqual(ctypes.sizeof(casc_assets.FindData), 344)
        self.assertEqual(casc_assets.FindData.size.offset, 304)
        self.assertEqual(casc_assets.FindData.name_type.offset, 340)

    @unittest.skipUnless(os.name == 'nt', 'Windows setup wizard')
    def test_battlenet_wizard_installs_and_creates_vdxr_shortcut_only(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            game = self.game(root)
            config = root / 'steam/userdata/123/config'
            config.mkdir(parents=True)
            # Battle.net setup must not even parse or rewrite this file.
            original = b'fixture Steam settings, untouched'
            (config / 'localconfig.vdf').write_bytes(original)
            saves = root / 'saves'
            saves.mkdir()
            (saves / 'Settings.json').write_text('{}')
            mod = root / 'mod'
            mod.mkdir()
            shutil.copytree(Path(__file__).resolve().parent.parent / 'artwork', mod / 'artwork')
            with patch.object(setup, 'ROOT', mod), \
                    patch.object(setup, 'verify_release', return_value={'sha256': {'bin/CascLib.dll': 'fixture'}}), \
                    patch.object(setup, 'steam_location', return_value=root / 'steam'), \
                    patch.object(setup, 'battlenet_location', return_value=game), \
                    patch.object(setup, 'saved_games', return_value=saves), \
                    patch.object(setup, 'folder', side_effect=[root / 'steam', game, saves]), \
                    patch.object(setup, 'wait_for_closed'), patch.object(setup, 'require_closed'), \
                    patch.object(setup.subprocess, 'run') as run, patch.object(setup.subprocess, 'Popen') as popen, \
                    patch('launch_windows.vdxr_manifest', return_value=game / 'D2R.exe'), \
                    patch.object(setup.sys, 'argv', ['setup_windows.py']), \
                    patch('builtins.input', side_effect=['2', '2', '2', '1', '2']), \
                    contextlib.redirect_stdout(io.StringIO()) as output:
                setup.main()
            self.assertIn('Which game installation?', output.getvalue())
            self.assertIn('Battle.net (experimental), D2R 3.3.93847', output.getvalue())
            self.assertEqual((config / 'localconfig.vdf').read_bytes(), original)
            entry = steam.decode_binary((config / 'shortcuts.vdf').read_bytes())['shortcuts']['0']
            self.assertIn('(Battle.net)', entry['appname'])
            self.assertIn('--frame-vdxr', entry['LaunchOptions'])
            self.assertEqual(entry['OpenVR'], 0)
            command = run.call_args.args[0]
            self.assertEqual(command[command.index('--store') + 1], 'battlenet')
            self.assertIn('--experimental-battlenet', command)
            self.assertIn('--casclib', command)
            popen.assert_not_called()
            result = json.loads((mod / 'setup-result.json').read_text())
            self.assertTrue(result['experimental'])
            self.assertIsNone(result['launch_options'])

    @unittest.skipUnless(os.name == 'nt', 'Windows setup wizard')
    def test_steam_only_package_rejects_battlenet_entry_before_prompts(self):
        with patch.object(setup, 'verify_release', return_value={'sha256': {}}), \
                patch.object(setup.sys, 'argv', ['setup_windows.py', '--experimental-battlenet']), \
                patch('builtins.input') as prompt, patch.object(setup.subprocess, 'run') as run:
            with self.assertRaisesRegex(ValueError, 'does not include.*Battle.net'):
                setup.main()
            prompt.assert_not_called()
            run.assert_not_called()

    def test_live_audit_guard_parser_and_masked_match(self):
        import audit_live_game as audit
        entries = list(audit.guards())
        self.assertGreater(len(entries), 100)
        self.assertEqual(audit.check(b'\x48\x8b\x10', [0x48, 0x8b, None]), 'matches')
        self.assertEqual(audit.check(None, [0x48]), 'unreadable')
        self.assertEqual(audit.check(b'\x90', [0x48]), 'different_at_baseline_address')


if __name__ == '__main__':
    unittest.main()
