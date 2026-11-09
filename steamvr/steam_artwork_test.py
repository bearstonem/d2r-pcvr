"""Steam artwork fixtures: no Steam client or player files are used."""
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import steam_config as config

ART = Path(__file__).resolve().parent.parent / 'artwork'


class ArtworkTests(unittest.TestCase):
    def fixture(self, root):
        profile = root / 'userdata/123'
        (profile / 'config').mkdir(parents=True)
        (profile / 'config/localconfig.vdf').write_text('"UserLocalConfigStore" {}')
        return profile

    def install(self, root, profile, connections=('steamvr', 'vdxr'), closed=lambda: None):
        return config.configure(profile, root, root / 'game', None, connections,
                                root / 'backups', closed, artwork_root=ART)

    def snapshot(self, profile):
        return {p.relative_to(profile): p.read_bytes() for p in profile.rglob('*') if p.is_file()}

    def test_all_slots_icon_field_and_repeat_install(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            profile = self.fixture(root)
            self.install(root, profile)
            entries = config.decode_binary((profile / 'config/shortcuts.vdf').read_bytes())['shortcuts']
            self.assertEqual(len(entries), 2)
            for entry, runtime in zip(entries.values(), ('SteamVR', 'Virtual-Desktop')):
                for name, suffix in config.ARTWORK_FILES.items():
                    target = profile / 'config/grid' / f"{entry['appid']}{suffix}.png"
                    self.assertEqual(target.read_bytes(), (ART / runtime / name).read_bytes())
                self.assertEqual(entry['icon'], str(profile / 'config/grid' / f"{entry['appid']}_icon.png"))
            before = self.snapshot(profile)
            self.install(root, profile)
            self.assertEqual(self.snapshot(profile), before)

    def test_other_account_and_similar_shortcut_untouched(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            profile = self.fixture(root)
            other = profile.parent / '456/config/grid'
            other.mkdir(parents=True)
            (other / '7p.png').write_bytes(b'other account')
            unrelated = {'appid': 7, 'appname': 'D2R VR Unleashed - SteamVR custom', 'icon': 'custom.ico'}
            (profile / 'config/shortcuts.vdf').write_bytes(config.encode_binary({'shortcuts': {'0': unrelated}}))
            grid = profile / 'config/grid'
            grid.mkdir()
            (grid / '7p.png').write_bytes(b'custom art')
            self.install(root, profile)
            self.assertEqual((other / '7p.png').read_bytes(), b'other account')
            self.assertEqual((grid / '7p.png').read_bytes(), b'custom art')
            self.assertEqual(config.decode_binary((profile / 'config/shortcuts.vdf').read_bytes())['shortcuts']['0'], unrelated)

    def test_old_name_migrates_without_changing_appid(self):
        root = Path('Steam')
        data = config.update_shortcuts(None, root, Path('Game'), ['steamvr'])
        tree = config.decode_binary(data)
        tree['shortcuts']['0']['appname'] = 'Diablo II: Resurrected VR - SteamVR'
        tree['shortcuts']['0']['appid'] = 0xf1234567
        updated = config.decode_binary(config.update_shortcuts(config.encode_binary(tree), root, Path('Game'), ['steamvr']))
        self.assertEqual(len(updated['shortcuts']), 1)
        self.assertEqual(updated['shortcuts']['0']['appid'], 0xf1234567)
        self.assertEqual(updated['shortcuts']['0']['appname'], config.SHORTCUTS['steamvr'][0])

    def test_exact_name_conflict_and_id_collision_refused(self):
        original = config.update_shortcuts(None, Path('Steam'), Path('Game'), ['steamvr'])
        for collision in (False, True):
            tree = config.decode_binary(original)
            if collision:
                tree['shortcuts']['1'] = dict(tree['shortcuts']['0'], appname='Another game')
            else:
                tree['shortcuts']['0']['Exe'] = 'another.exe'
            with self.assertRaises(ValueError):
                config.update_shortcuts(config.encode_binary(tree), Path('Steam'), Path('Game'), ['steamvr'])

    def test_partial_copy_failure_restores_images_and_vdf(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            profile = self.fixture(root)
            self.install(root, profile)
            grid = profile / 'config/grid'
            target = next(grid.glob('*p.png'))
            target.write_bytes(b'previous portrait')
            alternate = target.with_suffix('.jpg')
            alternate.write_bytes(b'previous jpg')
            before = self.snapshot(profile)
            replace = config.os.replace
            calls = 0
            def fail(source, destination):
                nonlocal calls
                calls += 1
                if calls == 4:
                    raise OSError('simulated copy error')
                return replace(source, destination)
            with patch.object(config.os, 'replace', side_effect=fail), self.assertRaises(OSError):
                self.install(root, profile)
            self.assertEqual(self.snapshot(profile), before)
            self.install(root, profile)
            self.assertFalse(alternate.exists())

    def test_steam_reopened_during_staging_changes_no_files(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            profile = self.fixture(root)
            before = self.snapshot(profile)
            calls = 0
            def closed():
                nonlocal calls
                calls += 1
                if calls == 2:
                    raise ValueError('Close Steam')
            with self.assertRaisesRegex(ValueError, 'Close Steam'):
                self.install(root, profile, closed=closed)
            self.assertEqual(self.snapshot(profile), before)

    def test_uninstall_recognizes_artwork_icon(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            profile = self.fixture(root)
            self.install(root, profile)
            updates = config.uninstall_updates(profile, root / 'game', 'steam')
            self.assertEqual(config.decode_binary(updates[profile / 'config/shortcuts.vdf'])['shortcuts'], {})


if __name__ == '__main__':
    unittest.main()
