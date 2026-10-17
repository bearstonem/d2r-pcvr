#!/usr/bin/env python3
"""Installer reversibility and portable launch routing, using temporary fixtures."""
import contextlib
import configparser
import hashlib
import io
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile

import install
import mod_identity
import launch_windows
import uninstall_windows as uninstall


class InstallationTests(unittest.TestCase):
    def test_install_restore_keeps_progress_and_stock_executable(self):
        self.install_round_trip('steam')

    @unittest.skipUnless(os.name == 'nt', 'Battle.net preview targets native Windows')
    def test_battlenet_install_restore_keeps_progress_and_stock_executable(self):
        self.install_round_trip('battlenet')

    def install_round_trip(self, store):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            game = root / 'library/steamapps/common/Diablo II Resurrected'
            game.mkdir(parents=True)
            exe = ('3.3.93787' if store == 'steam' else '3.3.93847').encode('utf-16-le')
            (game / 'D2R.exe').write_bytes(exe)
            (game / 'Data').mkdir()
            (game / ('Data/.build.config' if store == 'steam' else '.build.info')).write_text('fixture')
            # A ReShade.ini beside the game (the player's, or another mod's): the mod's ReShade has its own.
            (game / 'ReShade.ini').write_text('existing config')
            mod = game / 'mods/D2R-PCVR'
            mod_plugins = mod / 'd2rloader/plugins'
            icon_dir = mod_plugins / 'd2r_vr_icons'
            icon_dir.mkdir(parents=True)
            (icon_dir / 'manifest.tsv').write_text('old local cache')
            potion_dir = mod_plugins / 'd2r_vr_potions'
            potion_dir.mkdir()
            (potion_dir / 'hp1.pvr').write_bytes(b'previous potion')
            # On Windows default_saves points at the user's REAL Saved Games.
            # Fixtures must never install into or restore that directory.
            saves = root / 'saves'
            saves.mkdir(parents=True)
            (saves / 'Settings.json').write_text('{"Resolution Scale": 80}')
            (saves / 'Friend.d2s').write_bytes(b'original progress')
            deps = root / 'deps'
            deps.mkdir()
            archive = deps / 'D2RLoader-1.3.1-beta.zip'
            with zipfile.ZipFile(archive, 'w') as z:
                z.writestr('D2RLoader.exe', b'fixture loader')
                z.writestr('d2rloader/config/d2rloader.toml', 'fixture config')
                z.comment = b'current loader archive fixture'
            current_archive = archive.read_bytes()
            historical_archive = io.BytesIO(current_archive)
            with zipfile.ZipFile(historical_archive, 'a') as z:
                z.comment = b'historical loader archive fixture'
            historical_archive = historical_archive.getvalue()
            # Distinct ZIP packaging with the same harmless install payload.
            # Both pins must pass final preflight, independently of downloading.
            loader_hashes = tuple(hashlib.sha256(data).hexdigest()
                                  for data in (current_archive, historical_archive))
            (deps / 'reshade-6.8').mkdir()
            (deps / 'reshade-6.8/ReShade64.dll').write_bytes(b'fixture reshade')
            compiler = deps / 'd3dcompiler47/modern/d3dcompiler_47.dll'
            compiler.parent.mkdir(parents=True)
            compiler.write_bytes(b'fixture compiler')
            plugin = root / 'plugin.dll'
            plugin.write_bytes(b'fixture plugin')
            loader = root / 'openxr_loader.dll'
            loader.write_bytes(b'fixture xr loader')
            common = ['install.py', '--game', str(game), '--deps', str(deps),
                      '--plugin', str(plugin), '--openxr-loader', str(loader),
                      '--saves', str(saves), '--backup-dir', str(root / 'backups')]
            if store == 'battlenet':
                casc = root / 'CascLib.dll'
                casc.write_bytes(b'fixture CASC library, cache decoding is mocked')
                common += ['--store', store, '--experimental-battlenet', '--casclib', str(casc)]
            def icons(game, output, **kwargs):
                out = Path(output)
                for name in ('fixture.png', 'manifest.tsv', 'cache.json'):
                    (out / name).write_bytes(b'local generated fixture')
                return {'images': ['fixture.png'], 'icons': 1}
            def potions(game, output, **kwargs):
                for name in [code + '.pvr' for code in install.POTION_CODES] + ['cache.json']:
                    (Path(output) / name).write_bytes(b'local potion fixture')
                return {'models': len(install.POTION_CODES)}
            arcane_name = 'D2R-PCVR.mpq/data/hd/env/vis/act2_arcane_day.json'
            arcane = {arcane_name: b'local visual fixture',
                      'reshade/reshade-shaders/Textures/PCVR_Arcane/stars.png': b'local star fixture'}
            arcane['pcvr-arcane-sky.json'] = json.dumps({'version': 1, 'sha256': {
                name: hashlib.sha256(data).hexdigest() for name, data in arcane.items()}}).encode()
            with patch.object(install, 'require_game_closed'), contextlib.redirect_stdout(io.StringIO()), \
                    patch.object(install, 'COMPILER_HASH', hashlib.sha256(compiler.read_bytes()).hexdigest()), \
                    patch.object(install, 'prepare_icons', side_effect=icons), \
                    patch.object(install, 'prepare_potions', side_effect=potions), \
                    patch.object(install.prepare_arcane_sky, 'prepare', return_value=arcane):
                unapproved_hashes = ('0' * 64, '1' * 64)
                with patch.object(sys, 'argv', common), patch.object(install, 'LOADER_HASHES', unapproved_hashes):
                    with self.assertRaises(SystemExit) as caught:
                        install.main()
                message = str(caught.exception)
                self.assertIn(str(archive), message)
                for digest in (*unapproved_hashes, loader_hashes[0]):
                    self.assertIn(digest, message)
                self.assertIn('No game files changed', message)
                self.assertNotIn('\n', message)
                self.assertFalse((root / 'backups').exists())
                read_bytes = Path.read_bytes
                def blocked_archive(path):
                    if path == archive:
                        raise OSError(22, 'Invalid argument', str(path))
                    return read_bytes(path)
                with patch.object(sys, 'argv', common), patch.object(install, 'LOADER_HASHES', loader_hashes), \
                        patch.object(Path, 'read_bytes', blocked_archive):
                    with self.assertRaisesRegex(SystemExit, 'Protection history'):
                        install.main()
                self.assertFalse((root / 'backups').exists())
                self.assertFalse((game / 'D2RLoader.exe').exists())
                self.assertEqual((saves / 'Settings.json').read_text(), '{"Resolution Scale": 80}')
                self.assertEqual((saves / 'Friend.d2s').read_bytes(), b'original progress')
                with patch.object(sys, 'argv', common), patch.object(install, 'LOADER_HASHES', loader_hashes):
                    install.main()
                snapshot = next((root / 'backups').iterdir())
                record = install.read_record(game)
                self.assertEqual(record['snapshots'], [str(snapshot.resolve())])
                self.assertEqual(record['store'], store)
                self.assertEqual((game / 'D2R.exe').read_bytes(), exe)
                self.assertFalse((game / 'dxgi.dll').exists())
                self.assertEqual((icon_dir / 'fixture.png').read_bytes(), b'local generated fixture')
                self.assertEqual((potion_dir / 'hp1.pvr').read_bytes(), b'local potion fixture')
                self.assertTrue((potion_dir / 'rvl.pvr').is_file())
                for name, data in arcane.items():
                    self.assertEqual((mod / name).read_bytes(), data)
                self.assertEqual((game / 'd2rloader/runtime/d3dcompiler_47.dll').read_bytes(), compiler.read_bytes())
                self.assertFalse((game / 'd3dcompiler_47.dll').exists())
                # Everything of the mod in mods\D2R-PCVR; nothing of it in the game folder or D2RLoader's global plugins.
                self.assertEqual(json.loads((mod / 'D2R-PCVR.mpq/modinfo.json').read_text())['name'], 'D2R-PCVR')
                self.assertEqual(json.loads((mod / 'D2R-PCVR.mpq/modinfo.json').read_text())['savepath'], '../')
                self.assertIn('D2R VR Unleashed', json.loads((mod / 'D2R-PCVR.mpq/modinfo.json').read_text())['description'])
                self.assertEqual((mod / 'reshade/ReShade64.dll').read_bytes(), b'fixture reshade')
                self.assertTrue((mod / 'reshade/ReShade.ini').is_file())
                self.assertTrue((mod / 'reshade/D2R_Frame_ReShade.ini').is_file())
                self.assertTrue((mod / 'reshade/reshade-shaders/Shaders/D2R_DepthFog.fx').is_file())
                self.assertEqual((mod_plugins / 'openxr_loader.dll').read_bytes(), b'fixture xr loader')
                self.assertEqual((mod_plugins / 'd2rl-pcvr.dll').read_bytes(), b'fixture plugin')
                self.assertEqual((game / 'ReShade.ini').read_text(), 'existing config')
                for name in ('ReShade64.dll', 'openxr_loader.dll', 'D2R_Frame_ReShade.ini', 'reshade-shaders', 'D2R_Sky_ours',
                             'd2rloader/plugins/d2rl-pcvr.dll', 'd2rloader/plugins/d2r_pcvr.ini'):
                    self.assertFalse((game / name).exists(), name)
                cfg=configparser.ConfigParser(allow_no_value=True)
                cfg.read(mod_plugins / 'd2r_pcvr.ini')
                # Python merges duplicate sections differently from Win32.
                # Check the actual API used by the mod, including the near plane.
                if os.name == 'nt':
                    import ctypes
                    read_profile = ctypes.WinDLL('kernel32').GetPrivateProfileStringW
                    read_profile.argtypes = [ctypes.c_wchar_p, ctypes.c_wchar_p, ctypes.c_wchar_p,
                                             ctypes.c_wchar_p, ctypes.c_uint, ctypes.c_wchar_p]
                    for key, expected in [('near', '0.2'), ('fov', '64'), ('height', '6.3')]:
                        value = ctypes.create_unicode_buffer(64)
                        read_profile('camera', key, 'missing', value, len(value),
                                     str(mod_plugins / 'd2r_pcvr.ini'))
                        self.assertEqual(value.value, expected)
                self.assertTrue(cfg.getboolean('render','symmetric_projection'))
                self.assertTrue(cfg.getboolean('hud','monster_draw_order'))
                self.assertFalse(cfg.getboolean('render','defer_gpu_wait'))
                self.assertEqual(json.loads((saves / 'Settings.json').read_text())['NVIDIA DLSS'],0)
                self.assertEqual(json.loads((saves / 'Settings.json').read_text())['Resolution Scale'], 100)
                # An upgrade refreshes files without resetting calibration/video.
                custom_ini = b'[body_calibration]\nheight=1.83\nspan=1.91\n'
                custom_video = b'{"Resolution Scale": 92, "custom": "preserved"}\r\n'
                (mod_plugins / 'd2r_pcvr.ini').write_bytes(custom_ini)
                (saves / 'Settings.json').write_bytes(custom_video)
                (mod / 'reshade/ReShade.ini').write_bytes(b'custom reshade settings')
                # D2RLoader's own settings (another mod made itself the default) stay as they are.
                (game / 'd2rloader/config/d2rloader.toml').write_text('default_mod = "D2R-VR"')
                plugin.write_bytes(b'updated fixture plugin')
                archive.write_bytes(historical_archive)
                with patch.object(sys, 'argv', common + ['--preserve-settings']), \
                        patch.object(install, 'LOADER_HASHES', loader_hashes):
                    install.main()
                self.assertEqual((mod_plugins / 'd2r_pcvr.ini').read_bytes(), custom_ini)
                self.assertEqual((saves / 'Settings.json').read_bytes(), custom_video)
                self.assertEqual((mod / 'reshade/ReShade.ini').read_bytes(), b'custom reshade settings')
                self.assertEqual((game / 'd2rloader/config/d2rloader.toml').read_text(), 'default_mod = "D2R-VR"')
                self.assertEqual((mod_plugins / 'd2rl-pcvr.dll').read_bytes(), b'updated fixture plugin')
                self.assertFalse((game / 'd2rloader/plugins/d2rl-vrcam.dll').exists())
                self.assertFalse((game / 'd2rloader/plugins/d2r_vr.ini').exists())
                record = install.read_record(game)
                self.assertEqual(len(record['snapshots']), 2)
                self.assertEqual(record['snapshots'][0], str(snapshot.resolve()))
                (saves / 'Friend.d2s').write_bytes(b'new progress')
                with patch.object(sys, 'argv', ['install.py', '--game', str(game), '--restore', str(snapshot)]):
                    install.main()
                self.assertEqual((saves / 'Friend.d2s').read_bytes(), b'new progress')
                self.assertEqual((game / 'ReShade.ini').read_text(), 'existing config')
                self.assertEqual((game / 'D2R.exe').read_bytes(), exe)
                self.assertFalse((game / 'D2RLoader.exe').exists())
                self.assertFalse((game / install.RECORD).exists())
                self.assertFalse((icon_dir / 'fixture.png').exists())
                self.assertEqual((icon_dir / 'manifest.tsv').read_text(), 'old local cache')
                self.assertEqual((potion_dir / 'hp1.pvr').read_bytes(), b'previous potion')
                self.assertFalse((potion_dir / 'rvl.pvr').exists())
                self.assertFalse((mod_plugins / 'openxr_loader.dll').exists())
                self.assertFalse((mod_plugins / 'd2rl-pcvr.dll').exists())
                self.assertFalse((mod / 'reshade/ReShade64.dll').exists())
                self.assertFalse((mod / 'D2R-PCVR.mpq/modinfo.json').exists())
                for name in arcane:
                    self.assertFalse((mod / name).exists(), name)
                self.assertFalse((game / 'd2rloader/runtime/d3dcompiler_47.dll').exists())
                self.assertEqual(json.loads((saves / 'Settings.json').read_text())['Resolution Scale'], 80)

    def test_archive_paths_reject_windows_and_linux_traversal(self):
        for path in ('../D2R.exe', '/tmp/file', 'C:\\file', 'd2rloader\\..\\D2R.exe'):
            with self.subTest(path=path), self.assertRaises(SystemExit):
                install.safe_relative(path)

    def test_windows_steam_arguments_and_paths_with_spaces(self):
        original = ['C:\\Steam Library\\D2R.exe', '-some-argument', 'a value']
        self.assertEqual(launch_windows.prepare(original), (original, None))
        with patch.object(Path, 'is_file', return_value=True):
            command, cwd = launch_windows.prepare(original + ['--frame-vr'])
        self.assertEqual(command, ['C:\\Steam Library\\D2RLoader.exe', '-mod', 'D2R-PCVR', '-some-argument', 'a value'])
        self.assertEqual(cwd, 'C:\\Steam Library')
        # Launch options that already name a mod are left to choose it.
        with patch.object(Path, 'is_file', return_value=True):
            command, _ = launch_windows.prepare(['C:\\Steam Library\\D2R.exe', '-mod', 'Other', '--frame-vr'])
        self.assertEqual(command, ['C:\\Steam Library\\D2RLoader.exe', '-mod', 'Other'])

    def test_windows_runtime_override_only_applies_to_explicit_vr(self):
        with tempfile.TemporaryDirectory() as temporary:
            runtime = Path(temporary) / 'steamxr_win64.json'
            runtime.write_text('{}')
            options = [str(runtime.parent / 'D2R.exe'),
                       launch_windows.RUNTIME_OPTION + str(runtime)]
            self.assertEqual(launch_windows.prepare(options), ([options[0]], None))
            self.assertIsNone(launch_windows.launch_environment(options))
            with patch.dict(os.environ, {'XR_RUNTIME_JSON': 'other-runtime', 'SteamAppId': '2536520'}):
                environment = launch_windows.launch_environment(options + ['--frame-vr'])
                self.assertEqual(environment['XR_RUNTIME_JSON'], str(runtime.resolve()))
                # Windows normalizes environment variable names to uppercase.
                self.assertEqual({k.upper(): v for k, v in environment.items()}['STEAMAPPID'], '2536520')
                self.assertEqual(os.environ['XR_RUNTIME_JSON'], 'other-runtime')

    def test_windows_missing_runtime_override_is_rejected_before_launch(self):
        with self.assertRaises(SystemExit):
            launch_windows.launch_environment(['--frame-vr',
                launch_windows.RUNTIME_OPTION + 'missing-runtime.json'])

    def test_windows_vr_skips_early_overlay_routing_but_preserves_steam_ownership(self):
        with tempfile.TemporaryDirectory() as temporary:
            runtime = Path(temporary) / 'steamxr_win64.json'
            runtime.write_text('{}')
            inherited = {'SteamAppId': '2536520', 'SteamGameId': '2536520',
                         'STEAMOVERLAYGAMEID': '2536520', 'STEAMNOOVERLAYUI': '1',
                         'XR_RUNTIME_JSON': 'previous-runtime', 'D2R_FIXTURE': 'keep'}
            with patch.dict(os.environ, inherited, clear=True), \
                    patch.object(launch_windows, 'vdxr_manifest', return_value=runtime):
                before = dict(os.environ)
                modes = [[], [launch_windows.RUNTIME_OPTION + str(runtime)],
                         [launch_windows.VDXR_OPTION]]
                for mode in modes:
                    with self.subTest(mode=mode):
                        environment = {k.lower(): v for k, v in
                                       launch_windows.launch_environment(['--frame-vr'] + mode).items()}
                        self.assertNotIn('steamoverlaygameid', environment)
                        self.assertEqual(environment['steamappid'], '2536520')
                        self.assertEqual(environment['steamgameid'], '2536520')
                        self.assertEqual(environment['steamnooverlayui'], '1')
                        self.assertEqual(environment['d2r_fixture'], 'keep')
                        self.assertEqual(environment['xr_runtime_json'],
                                         str(runtime.resolve()) if mode else 'previous-runtime')
                        self.assertEqual(dict(os.environ), before)
                self.assertIsNone(launch_windows.launch_environment(
                    ['D2R.exe', launch_windows.RUNTIME_OPTION + str(runtime)]))
                self.assertEqual(dict(os.environ), before)

    def test_explicit_vdxr_overrides_configured_steamvr_without_changing_stock_play(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            steamvr = root / 'Steam VR/steamxr_win64.json'
            vdxr = root / 'Virtual Desktop/OpenXR/virtualdesktop-openxr.json'
            for runtime in (steamvr, vdxr):
                runtime.parent.mkdir(parents=True)
                runtime.write_text('{}')
            original = ['C:\\Steam Library\\D2R.exe', '-some-argument', 'a value']
            options = original + [launch_windows.RUNTIME_OPTION + str(steamvr),
                                  launch_windows.VDXR_OPTION]
            self.assertEqual(launch_windows.prepare(options), (original, None))
            self.assertIsNone(launch_windows.launch_environment(options))
            with patch.object(launch_windows, 'vdxr_manifest', return_value=vdxr), \
                    patch.dict(os.environ, {'XR_RUNTIME_JSON': 'other-runtime', 'SteamAppId': '2536520'}), \
                    patch.object(Path, 'is_file', return_value=True):
                command, cwd = launch_windows.prepare(options + ['--frame-vr'])
                self.assertEqual(command, ['C:\\Steam Library\\D2RLoader.exe', '-mod', 'D2R-PCVR'] + original[1:])
                self.assertEqual(cwd, 'C:\\Steam Library')
                for arguments in (options + ['--frame-vr'], ['--frame-vr', launch_windows.VDXR_OPTION]):
                    environment = launch_windows.launch_environment(arguments)
                    self.assertEqual(environment['XR_RUNTIME_JSON'], str(vdxr.resolve()))
                    self.assertEqual({k.upper(): v for k, v in environment.items()}['STEAMAPPID'], '2536520')
                self.assertEqual(os.environ['XR_RUNTIME_JSON'], 'other-runtime')

    def test_missing_vdxr_does_not_fall_back_to_the_configured_steamvr(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            steamvr = root / 'steamxr_win64.json'
            steamvr.write_text('{}')
            with patch.object(launch_windows, 'vdxr_manifest', return_value=root / 'missing.json'):
                with self.assertRaisesRegex(SystemExit, 'manifest was not found'):
                    launch_windows.launch_environment(['--frame-vr', launch_windows.VDXR_OPTION,
                        launch_windows.RUNTIME_OPTION + str(steamvr)])

    def test_multiple_configured_runtime_manifests_remain_rejected(self):
        for extra in ([], [launch_windows.VDXR_OPTION]):
            with self.subTest(extra=extra), self.assertRaisesRegex(SystemExit, 'at most one'):
                launch_windows.launch_environment(['--frame-vr',
                    launch_windows.RUNTIME_OPTION + 'first.json',
                    launch_windows.RUNTIME_OPTION + 'second.json'] + extra)


class RenameMigrationTests(unittest.TestCase):
    """Releases up to 0.3.15 installed D2R VR's file names (d2rl-vrcam.dll, d2r_vr.ini).
    An update carries this mod's own settings over; D2R VR's files are never touched."""
    OLD_PLUGIN = b'MZ old D2R PCVR d2r-vr-vrcam ' + 'd2r_vr_icons'.encode('utf-16-le')
    D2R_VR_PLUGIN = b'MZ D2R VR BodyWalk Game Link ' + 'd2r_vr_skill_icons'.encode('utf-16-le')
    CALIBRATION = b'[body_calibration]\nheight=1.83\nspan=1.91\n[camera]\nnear=0.15\n'

    def put(self, path, data):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        return path

    def fixture(self, root):
        game, saves, deps = root / 'game', root / 'saves', root / 'deps'
        self.put(game / 'D2R.exe', '3.3.93787'.encode('utf-16-le'))
        self.put(game / 'Data/.build.config', b'fixture')
        self.put(saves / 'Settings.json', b'{"Resolution Scale": 80}')
        self.put(saves / 'Friend.d2s', b'progress')
        archive = deps / 'D2RLoader-1.3.1-beta.zip'
        archive.parent.mkdir(parents=True)
        with zipfile.ZipFile(archive, 'w') as z:
            z.writestr('D2RLoader.exe', b'fixture loader')
        self.put(deps / 'reshade-6.8/ReShade64.dll', b'fixture reshade')
        self.put(deps / 'd3dcompiler47/modern/d3dcompiler_47.dll', b'fixture compiler')
        plugin = self.put(root / 'bin/d2rl-pcvr.dll', b'MZ new D2R PCVR d2r-pcvr-vrcam')
        loader = self.put(root / 'bin/openxr_loader.dll', b'fixture xr loader')
        self.argv = ['install.py', '--game', str(game), '--deps', str(deps), '--plugin', str(plugin),
                     '--openxr-loader', str(loader), '--saves', str(saves), '--backup-dir', str(root / 'backups')]
        self.hashes = dict(LOADER_HASHES=(hashlib.sha256(archive.read_bytes()).hexdigest(),),
                           COMPILER_HASH=hashlib.sha256(b'fixture compiler').hexdigest())
        return game, saves

    def old_release(self, root, game, saves):
        """What 0.3.15's install.py left: its plugin and ini under D2R VR's names, its record and first snapshot."""
        snapshot = root / 'backups/20261001-120000-000000'
        self.put(snapshot / 'manifest.json', json.dumps({
            'game': str(game), 'saves': str(saves), 'previous_mod': False,
            'installed': [str(install.RECORD), 'd2rloader\\plugins\\d2rl-vrcam.dll', 'd2rloader\\plugins\\d2r_vr.ini']}).encode())
        self.put(snapshot / 'saves/Settings.json', b'{"Resolution Scale": 80}')
        self.put(game / 'd2rloader/plugins/d2rl-vrcam.dll', self.OLD_PLUGIN)
        self.put(game / 'd2rloader/plugins/d2r_vr.ini', self.CALIBRATION)
        install.write_record(game, {'game': str(game), 'snapshots': [str(snapshot.resolve())]})
        return snapshot

    def run_install(self, *extra):
        output = io.StringIO()
        with patch.object(install, 'require_game_closed'), patch.object(install, 'LOADER_HASHES', self.hashes['LOADER_HASHES']), \
                patch.object(install, 'COMPILER_HASH', self.hashes['COMPILER_HASH']), \
                patch.object(install, 'prepare_icons', side_effect=ValueError('fixture: no game archives')), \
                patch.object(install, 'prepare_potions', side_effect=ValueError('fixture: no game archives')), \
                patch.object(install.prepare_arcane_sky, 'prepare', side_effect=ValueError('fixture: no game archives')), \
                patch.object(sys, 'argv', self.argv + list(extra)), contextlib.redirect_stdout(output):
            install.main()
        return output.getvalue()

    def files(self, folder):
        return {path.relative_to(folder).as_posix(): path.read_bytes() for path in folder.rglob('*') if path.is_file()}

    def test_update_carries_settings_and_calibration_and_removes_the_old_files(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            game, saves = self.fixture(root)
            first = self.old_release(root, game, saves)
            output = self.run_install('--preserve-settings')
            plugins = game / 'd2rloader/plugins'
            mod = game / 'mods/D2R-PCVR/d2rloader/plugins'
            self.assertEqual((mod / 'd2r_pcvr.ini').read_bytes(), self.CALIBRATION)
            self.assertEqual((mod / 'd2rl-pcvr.dll').read_bytes(), b'MZ new D2R PCVR d2r-pcvr-vrcam')
            self.assertFalse((plugins / 'd2rl-vrcam.dll').exists())
            self.assertFalse((plugins / 'd2r_vr.ini').exists())
            self.assertIn('Carried your settings and body calibration over', output)
            record = install.read_record(game)
            self.assertEqual(record['snapshots'][0], str(first.resolve()))
            second = Path(record['snapshots'][1])
            manifest = json.loads((second / 'manifest.json').read_text())
            self.assertTrue(manifest['previous_mod'])
            self.assertEqual(manifest['mod'], 'd2r-pcvr')
            self.assertEqual(sorted(Path(name).name for name in manifest['removed']), ['d2r_vr.ini', 'd2rl-vrcam.dll'])
            # The old files stay in the snapshot: rolling this update back returns them.
            self.assertEqual((second / 'previous/d2rloader/plugins/d2rl-vrcam.dll').read_bytes(), self.OLD_PLUGIN)
            self.assertEqual((second / 'previous/d2rloader/plugins/d2r_vr.ini').read_bytes(), self.CALIBRATION)
            # A repeated repair keeps the carried settings and finds nothing old to remove.
            (mod / 'd2r_pcvr.ini').write_bytes(self.CALIBRATION + b'tuned=1\n')
            self.assertNotIn('Carried', self.run_install('--preserve-settings'))
            self.assertEqual((mod / 'd2r_pcvr.ini').read_bytes(), self.CALIBRATION + b'tuned=1\n')
            # Removal across the rename: back to the game as it was before the first install.
            chain = uninstall.snapshots(game, install.read_record(game), [])
            self.assertEqual([path for path, _ in chain][0], first.resolve())
            uninstall.apply_updates(uninstall.restore_plan(game, chain), root / 'removal', lambda: None)
            for name in ('d2rl-pcvr.dll', 'd2r_pcvr.ini', 'd2rl-vrcam.dll', 'd2r_vr.ini'):
                self.assertFalse((plugins / name).exists(), name)
                self.assertFalse((mod / name).exists(), name)
            self.assertFalse((game / install.RECORD).exists())
            self.assertEqual((saves / 'Friend.d2s').read_bytes(), b'progress')

    def test_restoring_the_update_snapshot_brings_the_old_release_back(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            game, saves = self.fixture(root)
            self.old_release(root, game, saves)
            before = self.files(game / 'd2rloader/plugins')
            self.run_install('--preserve-settings')
            second = Path(install.read_record(game)['snapshots'][1])
            with patch.object(install, 'require_game_closed'), \
                    patch.object(sys, 'argv', ['install.py', '--game', str(game), '--restore', str(second)]), \
                    contextlib.redirect_stdout(io.StringIO()):
                install.main()
            self.assertEqual(self.files(game / 'd2rloader/plugins'), before)
            self.assertEqual(len(install.read_record(game)['snapshots']), 1)

    def test_full_install_over_the_old_release_resets_settings_but_still_renames(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            game, saves = self.fixture(root)
            self.old_release(root, game, saves)
            self.run_install()
            plugins = game / 'd2rloader/plugins'
            cfg = configparser.ConfigParser(interpolation=None, strict=False, allow_no_value=True)
            cfg.read(game / mod_identity.SETTINGS)
            self.assertEqual(cfg.get('camera', 'near'), '0.2')
            self.assertFalse(cfg.has_section('body_calibration'))
            self.assertFalse((plugins / 'd2rl-vrcam.dll').exists())
            self.assertFalse((plugins / 'd2r_vr.ini').exists())

    def test_d2r_vr_files_are_left_alone_and_setup_stops(self):
        # D2R VR alone, and D2R VR installed after an old D2R PCVR (whose record is still there).
        for after_old_release in (False, True):
            with self.subTest(after_old_release=after_old_release), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                game, saves = self.fixture(root)
                if after_old_release:
                    self.old_release(root, game, saves)
                self.put(game / 'd2rloader/plugins/d2rl-vrcam.dll', self.D2R_VR_PLUGIN)
                self.put(game / 'd2rloader/plugins/d2r_vr.ini', b'[mode]\nplatform=1\n')
                game_before, saves_before = self.files(game), self.files(saves)
                backups_before = self.files(root / 'backups') if after_old_release else {}
                with self.assertRaises(SystemExit) as caught:
                    self.run_install('--preserve-settings')
                message = str(caught.exception)
                self.assertIn('D2R VR', message)
                self.assertIn('No game files changed', message)
                self.assertEqual(self.files(game), game_before)
                self.assertEqual(self.files(saves), saves_before)
                self.assertEqual(self.files(root / 'backups') if (root / 'backups').exists() else {}, backups_before)

    def test_unrecorded_old_name_plugin_is_left_alone_until_its_backup_is_found(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            game, saves = self.fixture(root)
            snapshot = self.old_release(root, game, saves)
            (game / install.RECORD).unlink()
            # Its backups are elsewhere: the plugin is not provably this mod's.
            snapshot.rename(root / 'elsewhere')
            game_before = self.files(game)
            with self.assertRaisesRegex(SystemExit, 'did not install'):
                self.run_install('--preserve-settings')
            self.assertEqual(self.files(game), game_before)
            # The backup folder it was installed with (an older ZIP's) says it is.
            (root / 'backups').mkdir(exist_ok=True)
            (root / 'elsewhere').rename(snapshot)
            self.run_install('--preserve-settings')
            self.assertEqual((game / mod_identity.SETTINGS).read_bytes(), self.CALIBRATION)
            self.assertFalse((game / 'd2rloader/plugins/d2rl-vrcam.dll').exists())

    def test_the_global_plugins_install_moves_into_the_mod_folder(self):
        # d2rl-pcvr.dll / d2r_pcvr.ini in D2RLoader's global plugins folder (loaded with every mod, D2R VR's too).
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            game, saves = self.fixture(root)
            self.old_release(root, game, saves)
            self.run_install('--preserve-settings')
            plugins = game / 'd2rloader/plugins'
            for name in ('d2rl-pcvr.dll', 'd2r_pcvr.ini'):
                (game / 'mods/D2R-PCVR/d2rloader/plugins' / name).replace(plugins / name)
            output = self.run_install('--preserve-settings')
            self.assertIn('D2RLoader mod D2R-PCVR', output)
            self.assertEqual((game / mod_identity.SETTINGS).read_bytes(), self.CALIBRATION)
            self.assertTrue((game / mod_identity.PLUGIN).is_file())
            self.assertFalse((plugins / 'd2rl-pcvr.dll').exists())
            self.assertFalse((plugins / 'd2r_pcvr.ini').exists())
            third = Path(install.read_record(game)['snapshots'][-1])
            self.assertEqual((third / 'previous/d2rloader/plugins/d2r_pcvr.ini').read_bytes(), self.CALIBRATION)

    def test_d2r_vr_as_its_own_mod_does_not_stop_setup(self):
        # D2R VR in mods\D2R-VR loads only when that mod runs: the two mods sit side by side.
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            game, saves = self.fixture(root)
            theirs = {name: self.put(game / 'mods/D2R-VR' / name, data) for name, data in (
                ('d2rloader/plugins/d2rl-vrcam.dll', self.D2R_VR_PLUGIN), ('d2rloader/plugins/d2r_vr.ini', b'[mode]\nplatform=1\n'),
                ('reshade/ReShade64.dll', b'their reshade'), ('D2R-VR.mpq/modinfo.json', b'{"name": "D2R-VR"}'))}
            before = {name: path.read_bytes() for name, path in theirs.items()}
            self.run_install()
            self.assertTrue((game / mod_identity.PLUGIN).is_file())
            self.assertEqual({name: path.read_bytes() for name, path in theirs.items()}, before)
            # Removing this mod takes only its own folder's files; D2RLoader stays for D2R VR.
            chain = uninstall.snapshots(game, install.read_record(game), [])
            uninstall.apply_updates(uninstall.restore_plan(game, chain), root / 'removal', lambda: None)
            self.assertFalse((game / mod_identity.PLUGIN).exists())
            self.assertFalse((game / mod_identity.RESHADE).exists())
            self.assertEqual((game / 'D2RLoader.exe').read_bytes(), b'fixture loader')
            self.assertEqual({name: path.read_bytes() for name, path in theirs.items()}, before)
            self.assertFalse((game / install.RECORD).exists())


class DependencyDownloadTests(unittest.TestCase):
    payload = b'fixture dependency; no executable code'
    digest = hashlib.sha256(payload).hexdigest()
    historical_payload = b'historical fixture dependency; no executable code'
    historical_digest = hashlib.sha256(historical_payload).hexdigest()
    approved_digests = (digest, historical_digest)
    url = 'https://example.invalid/fixture.zip'

    def test_verified_manual_cache_is_reused_without_network_or_replacement(self):
        with tempfile.TemporaryDirectory() as temporary:
            target = Path(temporary) / 'fixture.zip'
            target.write_bytes(self.payload)
            with patch.object(install.urllib.request, 'urlopen') as download, \
                    patch.object(Path, 'write_bytes') as write, \
                    contextlib.redirect_stdout(io.StringIO()) as output:
                install.download_verified(self.url, target, self.digest)
            download.assert_not_called()
            write.assert_not_called()
            self.assertIn('Using verified cached dependency', output.getvalue())
            self.assertEqual(target.read_bytes(), self.payload)

    def test_current_download_accepts_explicit_reviewed_hash_set(self):
        with tempfile.TemporaryDirectory() as temporary:
            target = Path(temporary) / 'fixture.zip'
            with patch.object(install.urllib.request, 'urlopen', return_value=io.BytesIO(self.payload)) as download, \
                    contextlib.redirect_stdout(io.StringIO()):
                install.download_verified(self.url, target, self.approved_digests)
            download.assert_called_once()
            self.assertEqual(target.read_bytes(), self.payload)

    def test_historical_cache_is_reused_from_reviewed_hash_set(self):
        with tempfile.TemporaryDirectory() as temporary:
            target = Path(temporary) / 'fixture.zip'
            target.write_bytes(self.historical_payload)
            with patch.object(install.urllib.request, 'urlopen') as download, \
                    patch.object(Path, 'write_bytes') as write, \
                    contextlib.redirect_stdout(io.StringIO()) as output:
                install.download_verified(self.url, target, self.approved_digests)
            download.assert_not_called()
            write.assert_not_called()
            self.assertIn('Using verified cached dependency', output.getvalue())
            self.assertEqual(target.read_bytes(), self.historical_payload)

    def test_unlisted_download_reports_hashes_and_preserves_existing_cache(self):
        with tempfile.TemporaryDirectory() as temporary:
            target = Path(temporary) / 'fixture.zip'
            original = b'old, unverified cache'
            unlisted = b'unreviewed replacement at the same URL'
            actual = hashlib.sha256(unlisted).hexdigest()
            target.write_bytes(original)
            with patch.object(install.urllib.request, 'urlopen', return_value=io.BytesIO(unlisted)), \
                    contextlib.redirect_stdout(io.StringIO()):
                with self.assertRaises(SystemExit) as caught:
                    install.download_verified(self.url, target, self.approved_digests)
            message = str(caught.exception)
            self.assertIn('Checksum mismatch', message)
            self.assertIn(self.url, message)
            for digest in (*self.approved_digests, actual):
                self.assertIn(digest, message)
            self.assertIn('No game files changed', message)
            self.assertNotIn('\n', message)
            self.assertEqual(target.read_bytes(), original)

    def test_bad_download_keeps_existing_cache_and_enforces_checksum(self):
        with tempfile.TemporaryDirectory() as temporary:
            target = Path(temporary) / 'fixture.zip'
            target.write_bytes(b'old, unverified cache')
            with patch.object(install.urllib.request, 'urlopen', return_value=io.BytesIO(b'wrong content')), \
                    contextlib.redirect_stdout(io.StringIO()):
                with self.assertRaisesRegex(SystemExit, 'Checksum mismatch'):
                    install.download_verified(self.url, target, self.digest)
            self.assertEqual(target.read_bytes(), b'old, unverified cache')

    def test_valid_download_replaces_invalid_cache(self):
        with tempfile.TemporaryDirectory() as temporary:
            target = Path(temporary) / 'fixture.zip'
            target.write_bytes(b'invalid cache')
            with patch.object(install.urllib.request, 'urlopen', return_value=io.BytesIO(self.payload)), \
                    contextlib.redirect_stdout(io.StringIO()):
                install.download_verified(self.url, target, self.digest)
            self.assertEqual(target.read_bytes(), self.payload)

    def test_unreadable_cache_reports_path_and_os_error_without_redownloading(self):
        with tempfile.TemporaryDirectory() as temporary:
            target = Path(temporary) / 'fixture.zip'
            target.write_bytes(self.payload)
            with patch.object(Path, 'read_bytes', side_effect=OSError(22, 'Invalid argument')), \
                    patch.object(install.urllib.request, 'urlopen') as download:
                with self.assertRaises(SystemExit) as caught:
                    install.download_verified(self.url, target, self.digest)
            message = str(caught.exception)
            self.assertIn(str(target), message)
            self.assertIn('[Errno 22]', message)
            self.assertIn('Protection history', message)
            download.assert_not_called()
            self.assertEqual(target.read_bytes(), self.payload)

    def test_security_block_preserves_checksums_and_gives_temporary_recovery_steps(self):
        for code in (225, 226):
            with self.subTest(code=code), tempfile.TemporaryDirectory() as temporary:
                target = Path(temporary) / 'fixture.zip'
                error = OSError(22, 'Invalid argument')
                error.winerror = code
                with patch.object(Path, 'read_bytes', side_effect=error), \
                        patch.object(install.urllib.request, 'urlopen') as download:
                    with self.assertRaises(SystemExit) as caught:
                        install.download_verified(self.url, target, self.digest)
                message = str(caught.exception)
                self.assertIn('[PCVR_SECURITY_BLOCK]', message)
                self.assertIn(str(target), message)
                self.assertIn('Real-time protection Off', message)
                self.assertIn('back On immediately', message)
                self.assertIn('even if setup does not complete', message)
                self.assertIn('Update / repair', message)
                self.assertNotIn('\n', message)
                download.assert_not_called()

    @unittest.skipUnless(os.name == 'nt', 'Win32 error recovery')
    def test_errno22_recovers_security_block_without_misdiagnosing_other_errors(self):
        target = Path('fixture.zip')
        for code in (225, 226, 2, 5, 32, 87, None):
            with self.subTest(code=code), patch.object(install, 'windows_read_error', return_value=code):
                message = str(install.dependency_io_error(target, 'read', OSError(22, 'Invalid argument')))
                self.assertEqual('[PCVR_SECURITY_BLOCK]' in message, code in (225, 226))
                self.assertEqual('Real-time protection Off' in message, code in (225, 226))

    @unittest.skipUnless(os.name == 'nt', 'Real Win32 file probe')
    def test_native_error_probe_leaves_readable_file_intact(self):
        with tempfile.TemporaryDirectory() as temporary:
            target = Path(temporary) / 'fixture.zip'
            target.write_bytes(self.payload)
            self.assertIsNone(install.windows_read_error(target))
            self.assertEqual(target.read_bytes(), self.payload)
            target.unlink()  # No leaked handle.
            self.assertEqual(install.windows_read_error(target), 2)

    def test_file_blocked_after_download_is_reported_immediately(self):
        with tempfile.TemporaryDirectory() as temporary:
            target = Path(temporary) / 'fixture.zip'
            with patch.object(Path, 'read_bytes', side_effect=[FileNotFoundError(), OSError(22, 'Invalid argument')]), \
                    patch.object(install.urllib.request, 'urlopen', return_value=io.BytesIO(self.payload)), \
                    contextlib.redirect_stdout(io.StringIO()):
                with self.assertRaisesRegex(SystemExit, 'Protection history'):
                    install.download_verified(self.url, target, self.digest)
            self.assertEqual(target.read_bytes(), self.payload)

    def test_file_removed_after_download_reports_missing_dependency(self):
        with tempfile.TemporaryDirectory() as temporary:
            target = Path(temporary) / 'fixture.zip'
            write_bytes = Path.write_bytes
            def removed_write(path, data):
                size = write_bytes(path, data)
                path.unlink()
                return size
            with patch.object(Path, 'write_bytes', removed_write), \
                    patch.object(install.urllib.request, 'urlopen', return_value=io.BytesIO(self.payload)), \
                    contextlib.redirect_stdout(io.StringIO()):
                with self.assertRaisesRegex(SystemExit, 'missing, blocked/quarantined'):
                    install.download_verified(self.url, target, self.digest)
            self.assertFalse(target.exists())

    def test_blocked_write_is_explained(self):
        with tempfile.TemporaryDirectory() as temporary:
            target = Path(temporary) / 'fixture.zip'
            with patch.object(Path, 'write_bytes', side_effect=PermissionError(13, 'Permission denied')), \
                    patch.object(install.urllib.request, 'urlopen', return_value=io.BytesIO(self.payload)), \
                    contextlib.redirect_stdout(io.StringIO()):
                with self.assertRaisesRegex(SystemExit, 'Could not save required dependency'):
                    install.download_verified(self.url, target, self.digest)
            self.assertFalse(target.exists())

    def test_changed_file_after_save_fails_checksum(self):
        with tempfile.TemporaryDirectory() as temporary:
            target = Path(temporary) / 'fixture.zip'
            write_bytes = Path.write_bytes
            def changed_write(path, data):
                return write_bytes(path, b'changed content')
            with patch.object(Path, 'write_bytes', changed_write), \
                    patch.object(install.urllib.request, 'urlopen', return_value=io.BytesIO(self.payload)), \
                    contextlib.redirect_stdout(io.StringIO()):
                with self.assertRaisesRegex(SystemExit, 'Checksum mismatch for saved dependency'):
                    install.download_verified(self.url, target, self.digest)

    def test_saved_file_must_match_download_even_when_replaced_by_other_approved_payload(self):
        with tempfile.TemporaryDirectory() as temporary:
            target = Path(temporary) / 'fixture.zip'
            write_bytes = Path.write_bytes
            def swapped_write(path, data):
                self.assertEqual(data, self.payload)
                return write_bytes(path, self.historical_payload)
            with patch.object(Path, 'write_bytes', swapped_write), \
                    patch.object(install.urllib.request, 'urlopen', return_value=io.BytesIO(self.payload)), \
                    contextlib.redirect_stdout(io.StringIO()):
                with self.assertRaises(SystemExit) as caught:
                    install.download_verified(self.url, target, self.approved_digests)
            message = str(caught.exception)
            self.assertIn('Checksum mismatch for saved dependency', message)
            self.assertIn(str(target), message)
            self.assertIn(f'Expected SHA-256: {self.digest}', message)
            self.assertIn(f'received: {self.historical_digest}', message)
            self.assertEqual(target.read_bytes(), self.historical_payload)


if __name__ == '__main__':
    unittest.main()
