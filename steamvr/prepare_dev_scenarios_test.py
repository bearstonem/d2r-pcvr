import tempfile
import unittest
from pathlib import Path
import prepare_dev_scenarios as dev


class PrepareTests(unittest.TestCase):
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
