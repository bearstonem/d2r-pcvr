"""Removal fixtures use temporary games, saves and Steam profiles only."""
import contextlib
import io
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

import install
import mod_identity
import steam_config as steam
import setup_windows as setup
import uninstall_windows as uninstall


LOCAL = ('"UserLocalConfigStore" { "Software" { "Valve" { "Steam" { "apps" { '
         '"2536520" { "LaunchOptions" "PLACEHOLDER" "Other" "kept" } '
         '"123" { "LaunchOptions" "other game" } } } } } }')


class UninstallTests(unittest.TestCase):
    def write(self, path, content):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(content)
        return path

    def snapshot(self, root, game, files, previous, saves, initial=False):
        root.mkdir(parents=True)
        manifest = {'game': str(game), 'saves': str(saves), 'installed': files,
                    'previous_mod': not initial}
        (root / 'manifest.json').write_text(json.dumps(manifest))
        for name, payload in previous.items():
            self.write(root / 'previous' / name, payload)
        self.write(root / 'saves/Settings.json', b'{"video": "original"}')
        return root

    def test_upgrades_across_release_folders_restore_originals_and_keep_progress(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            game, saves = root / 'game', root / 'saves'
            stock = self.write(game / 'D2R.exe', b'original game, updated since install')
            character = self.write(saves / 'Friend.d2s', b'latest progress')
            stash = self.write(saves / 'SharedStashSoftCoreV2.d2i', b'latest stash')
            video = self.write(saves / 'Settings.json', b'{"video": "current"}')
            first = self.snapshot(root / 'release-one/20260101', game,
                                  [str(uninstall.PLUGIN), 'ReShade.ini', str(install.RECORD)],
                                  {'ReShade.ini': b'original other mod configuration'}, saves, True)
            second = self.snapshot(root / 'release-two/20260201', game,
                                   [str(uninstall.PLUGIN), 'ReShade.ini', 'openxr_loader.dll', str(install.RECORD)],
                                   {str(uninstall.PLUGIN): b'old mod', 'ReShade.ini': b'old VR config',
                                    str(install.RECORD): b'old receipt'}, saves)
            record = {'game': str(game), 'snapshots': [str(first), str(second)]}
            for name in [str(uninstall.PLUGIN), 'ReShade.ini', 'openxr_loader.dll']:
                self.write(game / name, b'current mod')
            install.write_record(game, record)
            extra = self.write(game / 'd2rloader/plugins/another-mod.dll', b'keep this other mod')
            chain = uninstall.snapshots(game, record, [])
            plan = uninstall.restore_plan(game, chain)
            backup = uninstall.apply_updates(plan, root / 'backups', lambda: None)
            self.assertTrue((backup / 'manifest.json').is_file())
            # Another installed plugin can use these shared files. Do not put
            # an old snapshot's configuration back over its current setup.
            self.assertEqual((game / 'ReShade.ini').read_bytes(), b'current mod')
            self.assertEqual((game / 'openxr_loader.dll').read_bytes(), b'current mod')
            for path in (game / uninstall.PLUGIN, game / install.RECORD):
                self.assertFalse(path.exists())
            self.assertEqual(stock.read_bytes(), b'original game, updated since install')
            self.assertEqual(character.read_bytes(), b'latest progress')
            self.assertEqual(stash.read_bytes(), b'latest stash')
            self.assertEqual(video.read_bytes(), b'{"video": "current"}')
            self.assertEqual(extra.read_bytes(), b'keep this other mod')
            self.assertIsNone(uninstall.apply_updates(plan, root / 'backups', lambda: None))
            plan = uninstall.restore_plan(game, chain, restore_video=True)
            self.assertEqual(plan[video], b'{"video": "original"}')
            self.assertNotIn(character, plan)
            self.assertNotIn(stash, plan)

    def test_d2rloader_stays_while_another_mod_uses_it(self):
        for other in (None, 'mods/D2R-VR/d2rloader/plugins/d2rl-vrcam.dll', 'd2rloader/plugins/another-mod.dll'):
            with self.subTest(other=other), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                game, saves = root / 'game', root / 'saves'
                loader = ['D2RLoader.exe', 'D2RCore.dll', 'd2rloader.mpq', 'd2rloader/config/d2rloader.toml']
                files = [str(uninstall.PLUGIN), str(mod_identity.RESHADE), str(install.RECORD)] + loader
                first = self.snapshot(root / 'backups/20261016', game, files, {}, saves, True)
                for name in files:
                    self.write(game / name, b'installed')
                if other:
                    self.write(game / other, b'another mod')
                install.write_record(game, {'game': str(game), 'snapshots': [str(first)]})
                chain = uninstall.snapshots(game, install.read_record(game), [])
                uninstall.apply_updates(uninstall.restore_plan(game, chain), root / 'removal', lambda: None)
                for name in (str(uninstall.PLUGIN), str(mod_identity.RESHADE), str(install.RECORD)):
                    self.assertFalse((game / name).exists(), name)
                for name in loader:
                    self.assertEqual((game / name).exists(), other is not None, name)
                if other:
                    self.assertEqual((game / other).read_bytes(), b'another mod')

    def test_missing_original_backup_blocks_removal_and_can_be_relocated(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            game = root / 'game'
            current = self.write(game / uninstall.PLUGIN, b'current')
            first = self.snapshot(root / 'moved/20260101', game, [str(uninstall.PLUGIN)], {}, root / 'saves', True)
            record = {'snapshots': [str(root / 'old-location/20260101')]}
            with self.assertRaisesRegex(ValueError, 'missing'):
                uninstall.snapshots(game, record, [])
            self.assertEqual(current.read_bytes(), b'current')
            chain = uninstall.snapshots(game, record, [first.parent])
            self.assertEqual(chain[0][0], first)
            latest = self.snapshot(root / '20260201', game, [str(uninstall.PLUGIN)],
                                   {str(uninstall.PLUGIN): b'previous VR mod'}, root / 'saves')
            with self.assertRaisesRegex(ValueError, 'pre-mod backup'):
                uninstall.snapshots(game, {'snapshots': [str(latest)]}, [])

    def test_legacy_backups_ignore_other_game_and_select_latest_fresh_install(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            game, saves = root / 'game', root / 'saves'
            old = self.snapshot(root / 'backups/20260101', game, [str(uninstall.PLUGIN)], {}, saves, True)
            self.snapshot(root / 'backups/20260201', root / 'other game', [str(uninstall.PLUGIN)], {}, saves, True)
            fresh = self.snapshot(root / 'backups/20260301', game, [str(uninstall.PLUGIN)], {}, saves, True)
            self.assertEqual([p for p, _ in uninstall.snapshots(game, {}, [old.parent])], [fresh])

    def test_removal_leaves_d2r_vr_files_installed_after_this_mod(self):
        # D2R PCVR 0.3.15 (old names) was installed first; D2R VR was installed over it later.
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            game, saves = root / 'game', root / 'saves'
            legacy_plugin, legacy_ini = str(mod_identity.LEGACY_PLUGIN), str(mod_identity.LEGACY_SETTINGS)
            first = self.snapshot(root / 'backups/20261001', game,
                                  [legacy_plugin, legacy_ini, 'ReShade.ini', str(install.RECORD)],
                                  {'ReShade.ini': b'original'}, saves, True)
            record = {'game': str(game), 'snapshots': [str(first)]}
            install.write_record(game, record)
            plugin = self.write(game / legacy_plugin, b'MZ D2R VR BodyWalk Game Link')
            ini = self.write(game / legacy_ini, b'[mode]\nplatform=1\n')
            self.write(game / 'ReShade.ini', b'current')
            plan = uninstall.restore_plan(game, uninstall.snapshots(game, record, []))
            self.assertNotIn(plugin, plan)
            self.assertNotIn(ini, plan)
            self.assertNotIn(game / 'ReShade.ini', plan)
            uninstall.apply_updates(plan, root / 'removal', lambda: None)
            self.assertEqual(plugin.read_bytes(), b'MZ D2R VR BodyWalk Game Link')
            self.assertEqual(ini.read_bytes(), b'[mode]\nplatform=1\n')
            self.assertEqual((game / 'ReShade.ini').read_bytes(), b'current')
            self.assertFalse((game / install.RECORD).exists())
            # Without D2R VR there, the old names are this mod's leftovers and go.
            plugin.write_bytes(b'MZ old D2R PCVR ' + 'd2r_vr_icons'.encode('utf-16-le'))
            plan = uninstall.restore_plan(game, [(first, json.loads((first / 'manifest.json').read_text()))])
            self.assertIsNone(plan[plugin])
            self.assertIsNone(plan[ini])
            self.assertEqual(plan[game / 'ReShade.ini'], b'original')

    def test_chain_across_the_rename_keeps_the_first_install_as_original(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            game, saves = root / 'game', root / 'saves'
            legacy_plugin, legacy_ini = str(mod_identity.LEGACY_PLUGIN), str(mod_identity.LEGACY_SETTINGS)
            first = self.snapshot(root / 'backups/20261001', game, [legacy_plugin, legacy_ini], {}, saves, True)
            # The update that renamed: new files written, old ones journaled and removed.
            second = self.snapshot(root / 'backups/20261015', game,
                                   [str(mod_identity.SETTINGS), legacy_plugin, legacy_ini, str(uninstall.PLUGIN)],
                                   {legacy_plugin: b'old plugin', legacy_ini: b'calibration'}, saves)
            record = {'game': str(game), 'snapshots': [str(first), str(second)]}
            self.assertEqual([path for path, _ in uninstall.snapshots(game, record, [])], [first, second])
            # Rolling back only the update returns the old release's files.
            plan = uninstall.restore_plan(game, [(second, json.loads((second / 'manifest.json').read_text()))])
            self.assertEqual(plan[game / legacy_plugin], b'old plugin')
            self.assertEqual(plan[game / legacy_ini], b'calibration')
            self.assertIsNone(plan[game / uninstall.PLUGIN])

    def test_restore_rejects_original_game_paths_traversal_and_directory_targets(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            game = root / 'game'
            original = self.write(game / 'D2R.exe', b'original')
            for name in ('D2R.exe', '../outside.txt', 'C:\\outside.txt', 'Settings.json'):
                with self.subTest(name=name), self.assertRaises((ValueError, SystemExit)):
                    uninstall.restore_plan(game, [(root / 'snapshot', {'installed': [name]})])
            (game / 'openxr_loader.dll').mkdir()
            with self.assertRaisesRegex(ValueError, 'not a file'):
                uninstall.restore_plan(game, [(root / 'snapshot', {'installed': ['openxr_loader.dll']})])
            self.assertEqual(original.read_bytes(), b'original')

    def test_steam_cleanup_keeps_other_edition_and_unrelated_configuration(self):
        for store in ('steam', 'battlenet'):
            with self.subTest(store=store), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                game, other = root / 'game', root / 'other game'
                profile = root / 'userdata/123'
                wrapper = '"C:\\Mods\\python.exe" "C:\\Mods\\steamvr\\launch_windows.py" %command%'
                local = self.write(profile / 'config/localconfig.vdf', LOCAL.replace('"PLACEHOLDER"', steam.quote(wrapper)).encode())
                data = steam.update_shortcuts(None, root, game, ['steamvr', 'vdxr'])
                data = steam.update_shortcuts(data, root, other, ['steamvr', 'vdxr'], store='battlenet',
                                             python=root / 'python.exe', wrapper=root / 'steamvr/launch_windows.py', runtime=root / 'runtime.json')
                # A same-name entry for another game folder must remain.
                entries = steam.decode_binary(data)
                entries['shortcuts']['4'] = {'appname': 'Other Game', 'Exe': '"other.exe"'}
                collision = dict(entries['shortcuts']['0' if store == 'steam' else '2'])
                collision['icon'] = str(root / 'different installation/D2R.exe')
                entries['shortcuts']['5'] = collision
                self.write(profile / 'config/shortcuts.vdf', steam.encode_binary(entries))
                target = game if store == 'steam' else other
                state = {'previous_launch_options': '-my-old-option', 'installed_launch_options': wrapper}
                updates = steam.uninstall_updates(profile, target, store, state)
                remaining = steam.decode_binary(updates[profile / 'config/shortcuts.vdf'])['shortcuts']
                self.assertEqual(list(remaining), ['0', '1', '2', '3'])
                self.assertIn(entries['shortcuts']['4'], remaining.values())
                self.assertIn(collision, remaining.values())
                preserved = ('2', '3') if store == 'steam' else ('0', '1')
                for key in preserved:
                    self.assertIn(entries['shortcuts'][key], remaining.values())
                if store == 'battlenet':
                    self.assertNotIn(local, updates)
                else:
                    self.assertEqual(updates[local].decode(), LOCAL.replace('PLACEHOLDER', '-my-old-option'))
                    local.write_bytes(LOCAL.replace('PLACEHOLDER', '-new-user-option').encode())
                    self.assertNotIn(local, steam.uninstall_updates(profile, game, store, state))
                    local.write_bytes(LOCAL.replace('"PLACEHOLDER"', steam.quote(wrapper + ' --added-later')).encode())
                    self.assertNotIn(local, steam.uninstall_updates(profile, game, store, state))

    def test_partial_failure_rolls_back_removed_files_and_config(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            removed = self.write(root / 'plugin.dll', b'installed mod')
            config = self.write(root / 'localconfig.vdf', b'current config')
            with patch.object(uninstall.os, 'replace', side_effect=PermissionError('blocked')):
                with self.assertRaises(PermissionError):
                    uninstall.apply_updates({removed: None, config: b'restored'}, root / 'backups', lambda: None)
            self.assertEqual(removed.read_bytes(), b'installed mod')
            self.assertEqual(config.read_bytes(), b'current config')
            self.assertFalse(list(root.glob('d2r-uninstall-*')))

    def test_external_change_or_running_game_prevents_commit(self):
        for race in ('file', 'game'):
            with self.subTest(race=race), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                target = self.write(root / 'plugin.dll', b'current')
                calls = 0
                def check():
                    nonlocal calls
                    calls += 1
                    if calls == 2:
                        if race == 'game':
                            raise ValueError('game running')
                        target.write_bytes(b'changed externally')
                with self.assertRaises(ValueError):
                    uninstall.apply_updates({target: None}, root / 'backups', check)
                self.assertEqual(target.read_bytes(), b'changed externally' if race == 'file' else b'current')

    @unittest.skipUnless(os.name == 'nt', 'Windows removal wizard')
    def test_wizard_uses_steam_final_flush_and_keeps_unrelated_changes(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            game = root / 'game'
            self.write(game / 'D2R.exe', b'original')
            self.write(game / 'Data/.build.config', b'fixture')
            profile = root / 'steam/userdata/123'
            wrapper = '"C:\\Mods\\python.exe" "C:\\Mods\\steamvr\\launch_windows.py" %command%'
            original = LOCAL.replace('"PLACEHOLDER"', steam.quote(wrapper))
            local = self.write(profile / 'config/localconfig.vdf', original.encode())
            flushed = original + '\n// unrelated Steam changes written on exit\n'
            with patch.object(uninstall, 'ROOT', root), patch.object(setup, 'verify_release', return_value={}), \
                    patch.object(setup, 'steam_location', return_value=root / 'steam'), \
                    patch.object(setup, 'folder', side_effect=[root / 'steam', game]), \
                    patch.object(setup, 'profiles', return_value=[(profile, 'Player')]), \
                    patch.object(setup, 'wait_for_closed', side_effect=lambda: local.write_text(flushed, encoding='utf-8')), \
                    patch.object(setup, 'require_closed'), patch.object(sys, 'argv', ['uninstall_windows.py']), \
                    patch('builtins.input', side_effect=['1', '1', '2']), contextlib.redirect_stdout(io.StringIO()):
                uninstall.main()   # (the game is in a folder Steam does not list: "still installed", then its folder)
            self.assertEqual(local.read_text(encoding='utf-8'),
                             LOCAL.replace('PLACEHOLDER', '') + '\n// unrelated Steam changes written on exit\n')

    @unittest.skipUnless(os.name == 'nt', 'Windows removal wizard')
    def test_wizard_cancel_changes_nothing_and_both_editions_remove_safely(self):
        for store, cancel in (('steam', True), ('steam', False), ('battlenet', False)):
            with self.subTest(store=store, cancel=cancel), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                game, saves = root / 'game', root / 'saves'
                stock = self.write(game / 'D2R.exe', b'newer game build is allowed during removal')
                self.write(game / ('Data/.build.config' if store == 'steam' else '.build.info'), b'fixture')
                plugin = self.write(game / uninstall.PLUGIN, b'mod')
                character = self.write(saves / 'Friend.d2s', b'current progress')
                snapshot = self.snapshot(root / 'setup-backups/game/20260101', game,
                                         [str(uninstall.PLUGIN), str(install.RECORD)], {}, saves, True)
                install.write_record(game, {'game': str(game), 'snapshots': [str(snapshot)]})
                # Steam's fixture lists no game, so the wizard first asks whether it is still installed (yes).
                answers = (['1', '1'] if store == 'steam' else ['2']) + ['1', '1' if cancel else '2']
                with patch.object(uninstall, 'ROOT', root), patch.object(setup, 'verify_release', return_value={}), \
                        patch.object(setup, 'steam_location', return_value=root / 'steam'), \
                        patch.object(setup, 'battlenet_location', return_value=game), \
                        patch.object(setup, 'folder', side_effect=[root / 'steam', game]), \
                        patch.object(setup, 'profiles', return_value=[]), \
                        patch.object(setup, 'wait_for_closed') as wait, patch.object(setup, 'require_closed'), \
                        patch.object(sys, 'argv', ['uninstall_windows.py']), \
                        patch('builtins.input', side_effect=answers), contextlib.redirect_stdout(io.StringIO()):
                    uninstall.main()
                self.assertEqual(plugin.exists(), cancel)
                self.assertEqual((game / install.RECORD).exists(), cancel)
                self.assertEqual(character.read_bytes(), b'current progress')
                self.assertEqual(stock.read_bytes(), b'newer game build is allowed during removal')
                if cancel:
                    wait.assert_not_called()
                    self.assertFalse(list((root / 'setup-backups').glob('uninstall-*')))


    def gone_fixture(self, root, store='steam'):
        """A game the launcher has uninstalled: D2R.exe and the archives are gone, the mod's files are not."""
        game, saves, client = root / 'game', root / 'saves', root / 'steam'
        plugin = self.write(game / uninstall.PLUGIN, b'mod')
        own = self.write(game / 'screenshots/mine.png', b'the player\'s own file')
        character = self.write(saves / 'Friend.d2s', b'current progress')
        snapshot = self.snapshot(root / 'setup-backups/game/20260101', game,
                                 [str(uninstall.PLUGIN), str(install.RECORD)], {}, saves, True)
        install.write_record(game, {'game': str(game), 'snapshots': [str(snapshot)]})
        self.write(root / 'setup-result.json', json.dumps({'store': store, 'game': str(game)}).encode())
        profile = client / 'userdata/123'
        wrapper = '"C:\\Mods\\python.exe" "C:\\Mods\\steamvr\\launch_windows.py" %command%'
        local = self.write(profile / 'config/localconfig.vdf',
                           LOCAL.replace('"PLACEHOLDER"', steam.quote(wrapper)).encode())
        data = steam.encode_binary({'shortcuts': {'0': {'appname': 'Unrelated', 'Exe': 'other.exe'}}})
        routing = {} if store == 'steam' else dict(store='battlenet', python=Path('C:/Mods/python.exe'),
                                                   wrapper=Path('C:/Mods/steamvr/launch_windows.py'))
        shortcuts = self.write(profile / 'config/shortcuts.vdf',
                               steam.update_shortcuts(data, client, game, ['vdxr'], **routing))
        return game, saves, client, profile, plugin, own, character, local, shortcuts

    def run_gone_wizard(self, root, client, profile, answers):
        with patch.object(uninstall, 'ROOT', root), patch.object(setup, 'verify_release', return_value={}), \
                patch.object(setup, 'steam_location', return_value=client), \
                patch.object(setup, 'battlenet_location', return_value=None), \
                patch.object(setup, 'folder', side_effect=[client]), \
                patch.object(setup, 'profiles', return_value=[(profile, 'Player')]), \
                patch.object(setup, 'wait_for_closed'), patch.object(setup, 'require_closed'), \
                patch.object(sys, 'argv', ['uninstall_windows.py']), \
                patch('builtins.input', side_effect=answers), contextlib.redirect_stdout(io.StringIO()) as output:
            uninstall.main()
        return output.getvalue()

    @unittest.skipUnless(os.name == 'nt', 'Windows removal wizard')
    def test_removal_finishes_when_the_game_was_uninstalled_first(self):
        # #328: the wizard asked for a folder containing D2R.exe for ever. Edition, "no longer installed",
        # Enter for the recorded folder, remove.
        for store, left in (('steam', 'files'), ('battlenet', 'files'), ('steam', 'nothing'), ('steam', 'no backup')):
            with self.subTest(store=store, left=left), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                game, saves, client, profile, plugin, own, character, local, shortcuts = self.gone_fixture(root, store)
                if left == 'nothing':
                    for path in sorted(game.rglob('*'), reverse=True):
                        path.unlink() if path.is_file() else path.rmdir()
                    game.rmdir()
                if left == 'no backup':
                    for path in sorted((root / 'setup-backups').rglob('*'), reverse=True):
                        path.unlink() if path.is_file() else path.rmdir()
                answers = ['1' if store == 'steam' else '2', '2', '', '2']
                text = self.run_gone_wizard(root, client, profile, answers)
                self.assertIn('Removal complete.', text)
                self.assertIn('no longer' if left != 'nothing' else 'no longer exists', text)
                remaining = steam.decode_binary(shortcuts.read_bytes())['shortcuts']
                self.assertEqual([item['appname'] for item in remaining.values()], ['Unrelated'])
                options = steam.text_value(steam.parse_text(local.read_text(encoding='utf-8')), 'UserLocalConfigStore',
                                           'Software', 'Valve', 'Steam', 'apps', steam.APP_ID, 'LaunchOptions')
                # The Steam edition's launch options no longer start the helper; Battle.net removal leaves them alone.
                self.assertEqual(steam.is_vr_launch_options(options), store != 'steam')
                self.assertEqual(character.read_bytes(), b'current progress')
                if left == 'nothing':
                    self.assertFalse(game.exists())   # never made again
                else:
                    self.assertEqual(own.read_bytes(), b'the player\'s own file')
                    # Tracked files go when their backup says what they are; without it the folder is left alone.
                    self.assertEqual(plugin.exists(), left == 'no backup')
                    self.assertEqual((game / install.RECORD).exists(), left == 'no backup')
                    self.assertEqual('were not touched' in text, left == 'no backup')
                before = {path: path.read_bytes() for path in root.rglob('*') if path.is_file() and 'uninstall-' not in str(path)}
                text = self.run_gone_wizard(root, client, profile, answers)   # safe to run twice
                self.assertIn('Removal complete.', text)
                after = {path: path.read_bytes() for path in root.rglob('*') if path.is_file() and 'uninstall-' not in str(path)}
                self.assertEqual(after, before)

    @unittest.skipUnless(os.name == 'nt', 'Windows removal wizard')
    def test_gone_answer_with_the_game_still_there_removes_normally(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            game, saves, client, profile, plugin, own, character, local, shortcuts = self.gone_fixture(root)
            stock = self.write(game / 'D2R.exe', b'the game is here after all')
            self.write(game / 'Data/.build.config', b'fixture')
            # ... "no longer installed", the folder, then the usual video settings question and the confirmation.
            text = self.run_gone_wizard(root, client, profile, ['1', '2', str(game), '1', '2'])
            self.assertIn('D2R.exe is still in', text)
            self.assertIn('Use the original Steam or Battle.net Play button', text)
            self.assertFalse(plugin.exists())
            self.assertEqual(stock.read_bytes(), b'the game is here after all')
            self.assertEqual(own.read_bytes(), b'the player\'s own file')

    def test_leftover_plan_touches_only_tracked_files_and_never_stops_removal(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            game, saves, client, profile, plugin, own, character, local, shortcuts = self.gone_fixture(root)
            self.assertTrue(uninstall.game_gone(game))
            roots = [root / 'setup-backups/game']
            updates, note = uninstall.leftover_plan(game, install.read_record(game), roots)
            self.assertEqual(updates, {game / uninstall.PLUGIN: None, game / install.RECORD: None})
            self.assertIn('anything else in it is kept', note)
            # A backup that lists a file outside the mod is refused as ever - and that leaves the folder alone.
            manifest = root / 'setup-backups/game/20260101/manifest.json'
            data = json.loads(manifest.read_text())
            data['installed'].append('screenshots/mine.png')
            manifest.write_text(json.dumps(data))
            updates, note = uninstall.leftover_plan(game, install.read_record(game), roots)
            self.assertEqual(updates, {})
            self.assertIn('were not touched', note)
            self.write(game / 'D2R.exe', b'installed')
            self.assertFalse(uninstall.game_gone(game))
            self.assertEqual(uninstall.former_game_folder(root, 'steam'), game)
            self.assertIsNone(uninstall.former_game_folder(root, 'battlenet'))
            self.assertIsNone(uninstall.former_game_folder(root / 'missing', 'steam'))


if __name__ == '__main__':
    unittest.main()
