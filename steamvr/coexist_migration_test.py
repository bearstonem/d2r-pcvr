"""Migration and shared-dependency regressions; temporary fake installs only."""
import configparser
import json
from pathlib import Path
import tempfile
import unittest

import install
import install_test
import mod_identity as ids
import uninstall_windows as uninstall


class CoexistMigrationTests(unittest.TestCase):
    def fixture(self, root, effects=False):
        helper = install_test.RenameMigrationTests()
        game, saves = helper.fixture(root)
        first = helper.old_release(root, game, saves)
        if effects:
            data = json.loads((first / 'manifest.json').read_text())
            files = {
                'ReShade.ini': b'[GENERAL]\nEffectSearchPaths=.\\reshade-shaders\\Shaders\n'
                    b'TextureSearchPaths=.\\reshade-shaders\\Textures,.\\custom-textures\n'
                    b'PresetPath=.\\D2R_Frame_ReShade.ini\n[INPUT]\nKeyOverlay=42,1,0,0\n',
                'D2R_Frame_ReShade.ini': b'Techniques=D2R_DepthFog@D2R_DepthFog.fx\n[Custom.fx]\nStrength=0.42\n',
                'ReShade64.dll': b'old fixture reshade',
                'reshade-shaders/Textures/my-custom.png': b'fixture image',
                'reshade-shaders/Shaders/D2R_DepthFog.fx': b'old stock shader',
            }
            for name, value in files.items():
                helper.put(game / name, value)
                data['installed'].append(name)
            (first / 'manifest.json').write_text(json.dumps(data))
        return helper, game, saves

    def test_settings_assets_cleanup_repair_and_restore(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            h, game, saves = self.fixture(root, effects=True)
            old_ini = (game / 'ReShade.ini').read_bytes()
            old_preset = (game / 'D2R_Frame_ReShade.ini').read_bytes()
            unrelated = h.put(game / 'reshade-shaders/Textures/unrelated.png', b'leave alone')
            h.run_install('--preserve-settings')
            effect = game / ids.RESHADE_DIR
            cfg = configparser.ConfigParser()
            cfg.read(effect / 'ReShade.ini', encoding='utf-8-sig')
            self.assertEqual(cfg['INPUT']['KeyOverlay'], '42,1,0,0')
            self.assertEqual(Path(cfg['GENERAL']['PresetPath']), effect / 'D2R_Frame_ReShade.ini')
            self.assertIn(str(game / 'custom-textures'), cfg['GENERAL']['TextureSearchPaths'])
            self.assertIn(str(game / 'reshade-shaders/Textures'), cfg['GENERAL']['TextureSearchPaths'])
            self.assertEqual((effect / 'D2R_Frame_ReShade.ini').read_bytes(), old_preset)
            for shader in sorted(p.name for pattern in ('D2R_*.fx', 'D2R_*.fxh') for p in (install.ROOT / 'reshade').glob(pattern) if p.name != 'D2R_DepthFog.fx'):   # a stock install never had the feature headers; they arrive beside the effect
                self.assertTrue((effect / 'reshade-shaders/Shaders' / shader).exists(), shader)
            self.assertEqual((effect / 'reshade-shaders/Textures/my-custom.png').read_bytes(), b'fixture image')
            for name in ('ReShade.ini', 'D2R_Frame_ReShade.ini', 'ReShade64.dll', 'reshade-shaders/Textures/my-custom.png'):
                self.assertFalse((game / name).exists(), name)
            self.assertEqual(unrelated.read_bytes(), b'leave alone')
            second = Path(install.read_record(game)['snapshots'][-1])
            journal = json.loads((second / 'manifest.json').read_text())['installed']
            self.assertEqual(len(journal), len({name.lower() for name in journal}))
            current_ini = (effect / 'ReShade.ini').read_bytes() + b'\n; user edit after migration\n'
            (effect / 'ReShade.ini').write_bytes(current_ini)
            h.run_install('--preserve-settings')
            self.assertEqual((effect / 'ReShade.ini').read_bytes(), current_ini)
            h.run_install('--restore', str(second))
            self.assertEqual((game / 'ReShade.ini').read_bytes(), old_ini)
            self.assertEqual((game / 'D2R_Frame_ReShade.ini').read_bytes(), old_preset)
            self.assertEqual((game / 'reshade-shaders/Shaders/D2R_DepthFog.fx').read_bytes(), b'old stock shader')
            self.assertFalse((effect / 'reshade-shaders/Shaders/D2R_DepthFog.fx').exists())
            for shader in sorted(p.name for pattern in ('D2R_*.fx', 'D2R_*.fxh') for p in (install.ROOT / 'reshade').glob(pattern) if p.name != 'D2R_DepthFog.fx'):
                self.assertFalse((effect / 'reshade-shaders/Shaders' / shader).exists(), shader)
                self.assertFalse((game / 'reshade-shaders/Shaders' / shader).exists(), shader)
            self.assertEqual((saves / 'Friend.d2s').read_bytes(), b'progress')

    def test_unrelated_root_configuration_is_not_adopted_or_deleted(self):
        with tempfile.TemporaryDirectory() as tmp:
            h, game, _ = self.fixture(Path(tmp))
            original = h.put(game / 'ReShade.ini', b'unrelated root config')
            h.run_install('--preserve-settings')
            self.assertEqual(original.read_bytes(), b'unrelated root config')
            self.assertNotEqual((game / ids.RESHADE_DIR / 'ReShade.ini').read_bytes(), original.read_bytes())

    def test_shared_loader_survives_uninstall_and_snapshot_restore(self):
        for kind in ('modinfo', 'plugin-mod'):
            with self.subTest(kind=kind), tempfile.TemporaryDirectory() as tmp:
                h, game, _ = self.fixture(Path(tmp), effects=True)
                h.run_install('--preserve-settings')
                snapshot = Path(install.read_record(game)['snapshots'][-1])
                other = h.put(game / 'mods/Other/Other.mpq/modinfo.json', b'{"name":"Other"}')
                if kind == 'plugin-mod':
                    h.put(game / 'mods/Other/d2rloader/plugins/other.dll', b'other mod')
                loader = game / 'D2RLoader.exe'
                old = loader.read_bytes()
                self.assertIn('Other', ids.other_loader_users(game))
                chain = uninstall.snapshots(game, install.read_record(game), [Path(tmp) / 'backups'])
                updates = uninstall.restore_plan(game, chain)
                self.assertNotIn(loader, updates)
                self.assertNotIn(other, updates)
                h.run_install('--restore', str(snapshot))
                self.assertEqual(loader.read_bytes(), old)
                self.assertTrue(other.is_file())

    def test_migration_keeps_shared_roots_and_existing_destination_settings(self):
        with tempfile.TemporaryDirectory() as tmp:
            h, game, _ = self.fixture(Path(tmp), effects=True)
            h.put(game / 'mods/Other/Other.mpq/modinfo.json', b'{}')
            source = (game / 'ReShade.ini').read_bytes()
            dest = h.put(game / ids.RESHADE_DIR / 'ReShade.ini', b'already configured mod')
            h.run_install('--preserve-settings')
            self.assertEqual(dest.read_bytes(), b'already configured mod')
            self.assertEqual((game / 'ReShade.ini').read_bytes(), source)

    def test_root_and_external_search_paths_are_rebased_without_losing_values(self):
        with tempfile.TemporaryDirectory() as tmp:
            game = Path(tmp).resolve()
            data = '[GENERAL]\nEffectSearchPaths=.\n[OTHER]\nValue=custom\n'.encode('utf-16')
            result = install.migrate_reshade_paths(data, game).decode('utf-16')
            self.assertIn('EffectSearchPaths=' + str(game), result)
            self.assertIn('[OTHER]\nValue=custom', result)

    def test_existing_other_mod_loader_is_not_silently_replaced(self):
        with tempfile.TemporaryDirectory() as tmp:
            h, game, _ = self.fixture(Path(tmp))
            h.put(game / 'mods/Other/Other.mpq/modinfo.json', b'{}')
            h.put(game / 'D2RLoader.exe', b'another loader version')
            before = h.files(game)
            with self.assertRaisesRegex(SystemExit, 'different installed D2RLoader'):
                h.run_install('--preserve-settings')
            self.assertEqual(h.files(game), before)


if __name__ == '__main__':
    unittest.main()
