"""Migrate historical portable commands without adopting unrelated shortcuts."""
import copy
import json
from pathlib import Path, PureWindowsPath
import tempfile
import unittest

import steam_config as config

ROOT = Path(__file__).resolve().parent
FIXTURE = ROOT / 'test_fixtures/portable_battlenet_shortcuts.json'
STEAM = PureWindowsPath('C:/Program Files (x86)/Steam')
GAME = PureWindowsPath('D:/Games/Diablo II Resurrected')
PORTABLE = PureWindowsPath('C:/Users/Player/Downloads/D2R PCVR')
MANAGED = PureWindowsPath('C:/Users/Player/AppData/Local/Programs/D2R PCVR')
RUNTIME = PureWindowsPath('E:/VR Library/SteamVR/steamxr_win64.json')


class ShortcutMigrationTests(unittest.TestCase):
    def setUp(self):
        # Frozen output of the actual pre-artwork update_shortcuts generator at
        # 9683da9744e6a78153c02b95672c167aca6068f1, using Windows paths.
        self.entries = json.loads(FIXTURE.read_text())['shortcuts']
        self.routing = dict(store='battlenet', python=MANAGED / '.runtime/python/python.exe',
                            wrapper=MANAGED / 'steamvr/launch_windows.py', runtime=RUNTIME)

    def migrate(self, entries=None, routing=None):
        original = config.encode_binary({'shortcuts': self.entries if entries is None else entries})
        return config.update_shortcuts(original, STEAM, GAME, ['steamvr', 'vdxr'],
                                       **(self.routing if routing is None else routing))

    def assert_migration(self, routing):
        data = self.migrate(routing=routing)
        entries = config.decode_binary(data)['shortcuts']
        self.assertEqual(len(entries), 2)
        specs = config.shortcut_specs(STEAM, GAME, **routing)
        for key, connection in [('0', 'steamvr'), ('1', 'vdxr')]:
            name, options, _, executable, directory = specs[connection]
            self.assertEqual(entries[key]['appid'], self.entries[key]['appid'])
            self.assertEqual(entries[key]['appname'], name)
            self.assertEqual(entries[key]['LaunchOptions'], options)
            self.assertEqual(entries[key]['Exe'], '"' + str(executable) + '"')
            self.assertEqual(entries[key]['StartDir'], '"' + str(directory) + '"')
        self.assertEqual(config.update_shortcuts(data, STEAM, GAME, ['steamvr', 'vdxr'],
                                                **routing), data)

    def test_portable_to_managed_preserves_ids_and_repeat_install(self):
        self.assert_migration(self.routing)

    def test_runtime_only_move_with_legacy_and_current_names(self):
        routing = dict(self.routing, python=PORTABLE / 'deps/python/python.exe',
                       wrapper=PORTABLE / 'steamvr/launch_windows.py')
        for current_names in (False, True):
            with self.subTest(current_names=current_names):
                if current_names:
                    for entry in self.entries.values():
                        entry['appname'] = entry['appname'].replace(
                            'Diablo II: Resurrected VR', 'D2R VR Unleashed')
                self.assert_migration(routing)

    def test_migration_keeps_artwork_slots_and_other_entries(self):
        with tempfile.TemporaryDirectory() as tmp:
            profile = Path(tmp) / 'userdata/123'
            folder = profile / 'config'
            folder.mkdir(parents=True)
            unrelated = {'appid': 9, 'appname': 'Another game', 'icon': 'custom.png'}
            self.entries['2'] = unrelated
            (folder / 'shortcuts.vdf').write_bytes(config.encode_binary({'shortcuts': self.entries}))
            def install(routing):
                config.configure(profile, STEAM, GAME, None, ['steamvr', 'vdxr'],
                                 Path(tmp) / 'backups', lambda: None,
                                 artwork_root=ROOT.parent / 'artwork', **routing)
            install(self.routing)
            before = {p.name: p.read_bytes() for p in (folder / 'grid').iterdir()}
            install(dict(self.routing, runtime=PureWindowsPath('F:/SteamVR/steamxr_win64.json')))
            self.assertEqual({p.name: p.read_bytes() for p in (folder / 'grid').iterdir()}, before)
            expected = {f"{self.entries[key]['appid']}{suffix}.png"
                        for key in ('0', '1') for suffix in config.ARTWORK_FILES.values()}
            self.assertEqual(set(before), expected)
            self.assertEqual(config.decode_binary((folder / 'shortcuts.vdf').read_bytes())
                             ['shortcuts']['2'], unrelated)

    def test_unrelated_or_malformed_commands_are_refused(self):
        old = self.entries['0']
        options = old['LaunchOptions']
        invalid = [
            dict(old, Exe='"C:\\ThirdParty\\launcher.exe"'),
            dict(old, LaunchOptions=options.replace('launch_windows.py', 'other.py')),
            dict(old, LaunchOptions=options.replace('\\steamvr\\', '\\thirdparty\\')),
            dict(old, LaunchOptions=options.replace(str(GAME), str(GAME) + '-other')),
            dict(old, LaunchOptions=options + ' --extra'),
            dict(old, LaunchOptions=options + ' "--battlenet-game=' + str(GAME) + '"'),
            dict(old, LaunchOptions=options.replace('--experimental-battlenet ', '')),
            dict(old, LaunchOptions=options.replace('--frame-vr ', '')),
            dict(old, LaunchOptions=options[:options.index(' "--frame-openxr-runtime=')] + ' --frame-vdxr'),
        ]
        for entry in invalid:
            with self.subTest(entry=entry):
                entries = copy.deepcopy(self.entries)
                entries['0'] = entry
                with self.assertRaisesRegex(ValueError, 'different shortcut'):
                    self.migrate(entries)

    def test_collision_and_duplicate_names_still_refused(self):
        for duplicate_name in (False, True):
            with self.subTest(duplicate_name=duplicate_name):
                entries = copy.deepcopy(self.entries)
                entries['2'] = dict(entries['0'])
                if not duplicate_name:
                    entries['2']['appname'] = 'Another game'
                with self.assertRaisesRegex(ValueError, 'duplicate|collision'):
                    self.migrate(entries)


if __name__ == '__main__':
    unittest.main()
