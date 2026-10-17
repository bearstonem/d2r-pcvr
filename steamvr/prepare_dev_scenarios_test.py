import tempfile
import unittest
from unittest.mock import patch
from pathlib import Path
import prepare_dev_scenarios as dev


class PrepareTests(unittest.TestCase):
    def fixture(self, directory):
        game = Path(directory).resolve()
        for name in ('D2R.exe', 'D2RLoader.exe'):
            (game / name).write_bytes(b'fixture')
        normal = game / 'mods/D2R-PCVR'
        for relative in dev.FILES:
            path = normal / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(dev.CAPABILITY if relative == dev.PLUGIN else b'normal settings/calibration')
        for tree in dev.TREES:
            path = normal / tree / 'fixture.bin'
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b'locally prepared fixture')
        return game, normal

    @patch.object(dev, 'require_game_closed')
    @patch.object(dev, 'validate_game')
    def test_full_stage_keeps_settings_saves_and_other_mods(self, validate, closed):
        with tempfile.TemporaryDirectory() as directory:
            game, normal = self.fixture(directory)
            native = normal / 'D2R-PCVR.mpq/data/hd/env/fixture.json'
            native.parent.mkdir(parents=True); native.write_bytes(b'local native override')
            (normal / 'D2R-PCVR.mpq/modinfo.json').write_bytes(b'normal save path')
            save = game / 'Hero.d2s'; save.write_bytes(b'normal progress')
            root = dev.stage(game)
            self.assertEqual((root / 'pcvr-dev.mpq/data/hd/env/fixture.json').read_bytes(), b'local native override')
            self.assertEqual((root / 'pcvr-dev.mpq/modinfo.json').read_bytes(), dev.MOD_INFO.encode())
            ini = root / 'd2rloader/plugins/d2r_pcvr.ini'
            self.assertEqual(ini.read_bytes(), b'normal settings/calibration')
            ini.write_bytes(b'developer calibration')
            (normal / dev.PLUGIN).write_bytes(b'new normal version')
            self.assertEqual(dev.stage(game), root)  # existing candidate never replaced on launch
            self.assertEqual((root / dev.PLUGIN).read_bytes(), dev.CAPABILITY)
            self.assertEqual(ini.read_bytes(), b'developer calibration')
            self.assertEqual(save.read_bytes(), b'normal progress')
            self.assertEqual((normal / dev.PLUGIN).read_bytes(), b'new normal version')

    @patch.object(dev, 'require_game_closed')
    @patch.object(dev, 'validate_game')
    def test_explicit_dll_update_backs_up_only_dll(self, validate, closed):
        with tempfile.TemporaryDirectory() as directory:
            game, normal = self.fixture(directory); root = dev.stage(game)
            update = game / 'built.dll'; update.write_bytes(dev.CAPABILITY + b' new')
            dev.stage(game, update)
            self.assertEqual((root / dev.PLUGIN).read_bytes(), update.read_bytes())
            backups = list((root / dev.PLUGIN).parent.glob('*.before-dev-*'))
            self.assertEqual(len(backups), 1)
            self.assertEqual(backups[0].read_bytes(), dev.CAPABILITY)
            self.assertEqual((normal / dev.PLUGIN).read_bytes(), dev.CAPABILITY)
            self.assertEqual((root / 'd2rloader/plugins/d2r_pcvr.ini').read_bytes(), b'normal settings/calibration')

    @patch.object(dev, 'require_game_closed')
    @patch.object(dev, 'validate_game')
    def test_failures_do_not_touch_existing_content(self, validate, closed):
        with tempfile.TemporaryDirectory() as directory:
            game, normal = self.fixture(directory)
            (normal / dev.PLUGIN).write_bytes(b'old release without dev support')
            with self.assertRaisesRegex(ValueError, 'no developer tools'):
                dev.stage(game)
            self.assertFalse((game / 'mods/pcvr-dev').exists())
            (normal / dev.PLUGIN).write_bytes(dev.CAPABILITY)
            global_plugin = game / 'd2rloader/plugins/d2rl-vrcam.dll'
            global_plugin.parent.mkdir(parents=True); global_plugin.write_bytes(b'other mod')
            with self.assertRaisesRegex(ValueError, 'global VR plugin'):
                dev.stage(game)
            self.assertEqual(global_plugin.read_bytes(), b'other mod')
            global_plugin.unlink()
            root = dev.prepare(game)
            with patch.object(dev.os, 'replace', side_effect=OSError('fixture copy failure')):
                with self.assertRaises(OSError):
                    dev.stage(game)
            self.assertFalse((root / dev.PLUGIN).exists())
            self.assertEqual((root / 'pcvr-dev.mpq/modinfo.json').read_bytes(), dev.MOD_INFO.encode())
            self.assertEqual(dev.stage(game), root)  # interrupted setup can be retried
            closed.side_effect = SystemExit('game running')
            with self.assertRaises(SystemExit):
                dev.stage(game)

    @patch.object(dev, 'require_game_closed')
    @patch.object(dev, 'validate_game')
    def test_linked_source_is_rejected_before_writes(self, validate, closed):
        with tempfile.TemporaryDirectory() as directory:
            game, normal = self.fixture(directory)
            link = normal / dev.TREES[0] / 'linked'
            try:
                link.symlink_to(game / 'D2R.exe')
            except OSError:
                self.skipTest('Symlink creation is unavailable')
            with self.assertRaisesRegex(ValueError, 'linked paths'):
                dev.stage(game)
            self.assertFalse((game / 'mods/pcvr-dev').exists())

    def test_isolation_idempotence_and_existing_content(self):
        with tempfile.TemporaryDirectory() as directory:
            game = Path(directory).resolve()
            for name in ('D2R.exe', 'D2RLoader.exe'):
                (game / name).touch()
            save = game / 'saves' / 'Hero.d2s'
            save.parent.mkdir()
            save.write_bytes(b'player progress')
            root = dev.prepare(game)
            info = root / 'pcvr-dev.mpq' / 'modinfo.json'
            self.assertEqual(info.read_bytes(), dev.MOD_INFO.encode())
            marker = root / 'keep.txt'
            marker.write_text('existing')
            self.assertEqual(dev.prepare(game), root)
            self.assertEqual(marker.read_text(), 'existing')
            self.assertEqual(save.read_bytes(), b'player progress')
            info.write_text('{"name":"pcvr-dev","savepath":""}')
            with self.assertRaises(ValueError):
                dev.prepare(game)
            self.assertEqual(info.read_text(), '{"name":"pcvr-dev","savepath":""}')

    def test_missing_install_and_conflicting_directory(self):
        with tempfile.TemporaryDirectory() as directory:
            game = Path(directory)
            with self.assertRaises(ValueError):
                dev.prepare(game)
            (game / 'D2R.exe').touch()
            (game / 'D2RLoader.exe').touch()
            (game / 'mods' / dev.MOD).mkdir(parents=True)
            with self.assertRaises(ValueError):
                dev.prepare(game)


if __name__ == '__main__':
    unittest.main()
