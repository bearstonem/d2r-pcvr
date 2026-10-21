from pathlib import Path
import contextlib
import io
import sys
import tempfile
import unittest
from unittest.mock import patch
import hashlib
import json
import tomllib

import coop_setup as coop


class ButtonSettingsTests(unittest.TestCase):
    def test_changes_only_button_preserving_comments_crlf_and_bom(self):
        before=b'\xef\xbb\xbf# keep\r\n[d2rloader]\r\nshow_tcpip_button = false # keep me\r\ncheck_for_updates=false\r\n[d2rloader.advanced]\r\ngame_port=4123\r\n'
        after=coop.patch_button(before)
        self.assertEqual(after,before.replace(b'button = false',b'button = true'))
        expected=tomllib.loads(before.decode('utf-8-sig'))
        expected['d2rloader']['show_tcpip_button']=True
        self.assertEqual(tomllib.loads(after.decode('utf-8-sig')),expected)

    def test_idempotent_and_missing_table_or_key(self):
        for before in (b'# only\n',b'[d2rloader]\ncheck_for_updates=false\n[d2rloader.advanced]\ngame_port=4000',b'[d2rloader.advanced]\ngame_port=4000'):
            with self.subTest(before=before):
                after=coop.patch_button(before)
                self.assertTrue(tomllib.loads(after.decode())['d2rloader']['show_tcpip_button'])
                self.assertEqual(coop.patch_button(after),after)

    def test_wrong_types_duplicate_keys_and_ambiguous_multiline_text_rejected(self):
        for before in (b'[d2rloader]\nshow_tcpip_button="false"', b'[d2rloader]\nshow_tcpip_button=false\nshow_tcpip_button=true',b'text="""\n[d2rloader]\nshow_tcpip_button = false\n"""\n[d2rloader]\nshow_tcpip_button = false\n'):
            with self.subTest(before=before),self.assertRaises(ValueError):
                coop.patch_button(before)

    def test_backup_matches_original_and_second_run_does_not_write(self):
        with tempfile.TemporaryDirectory() as folder,patch.object(coop,'require_game_closed') as closed:
            game=Path(folder).resolve();config=game/coop.CONFIG;config.parent.mkdir(parents=True)
            before=b'[d2rloader]\nshow_tcpip_button=false\n';config.write_bytes(before)
            backup=coop.enable_button(game)
            self.assertEqual(backup.read_bytes(),before)
            self.assertTrue(tomllib.loads(config.read_text())['d2rloader']['show_tcpip_button'])
            self.assertIsNone(coop.enable_button(game))
            self.assertEqual(len(list(config.parent.glob('*.bak'))),1)
            self.assertEqual(closed.call_count,3)

    def test_running_game_refuses_before_any_write(self):
        with tempfile.TemporaryDirectory() as folder,patch.object(coop,'require_game_closed',side_effect=ValueError('game open')):
            game=Path(folder).resolve();config=game/coop.CONFIG;config.parent.mkdir(parents=True)
            before=b'[d2rloader]\n';config.write_bytes(before)
            with self.assertRaises(ValueError):coop.enable_button(game)
            self.assertEqual(config.read_bytes(),before)
            self.assertEqual(list(config.parent.glob('*.bak')),[])

    def test_concurrent_setting_edit_is_preserved(self):
        with tempfile.TemporaryDirectory() as folder:
            game=Path(folder).resolve();config=game/coop.CONFIG;config.parent.mkdir(parents=True)
            config.write_bytes(b'[d2rloader]\nshow_tcpip_button=false\n')
            calls=0
            def closed():
                nonlocal calls
                calls+=1
                if calls==2:config.write_bytes(b'[d2rloader]\ncheck_for_updates=false\n')
            with patch.object(coop,'require_game_closed',side_effect=closed),self.assertRaises(ValueError):
                coop.enable_button(game)
            self.assertEqual(config.read_bytes(),b'[d2rloader]\ncheck_for_updates=false\n')


class PreflightTests(unittest.TestCase):
    def fixture(self,root):
        game=root/'game';game.mkdir()
        entries=[]
        for name in ('D2RLoader.exe','D2RCore.dll','d2rloader.mpq'):
            data=name.encode();(game/name).write_bytes(data)
            entries.append(dict(path='{game}/'+name,sha256_any_of=[hashlib.sha256(data).hexdigest()]))
        (root/'vrmod-manifest.json').write_text(json.dumps(dict(version='0.3.23',requires=[dict(id='d2rloader',files=entries)])))
        plugin=game/'mods'/coop.MOD_NAME/coop.PLUGIN;plugin.parent.mkdir(parents=True);plugin.write_bytes(b'fixture')
        config=game/coop.CONFIG;config.parent.mkdir(parents=True,exist_ok=True)
        config.write_text('[d2rloader]\nshow_tcpip_button=true\n[d2rloader.advanced]\ngame_port=4123\n')
        return game,plugin,config

    def test_report_uses_actual_loader_plugin_port_and_saved_rules(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp).resolve();game,plugin,_=self.fixture(root)
            plugin.with_name('d2r_pcvr.ini').write_text('[gameplay]\nmelee_rules=1\ncontact_hits=0\n',encoding='utf-16')
            with patch.object(coop,'ROOT',root),patch.object(coop,'validate_game',return_value=('steam','3.3.93787')):
                report=coop.inspect(game)
            self.assertEqual(report['port'],4123)
            self.assertEqual(report['plugin_sha256'],hashlib.sha256(b'fixture').hexdigest())
            self.assertEqual(report['combat_policy'],dict(melee_rules=1,contact_hits=0,enemies_always_hit=1))
            self.assertEqual(len(report['loader_sha256']),3)
            self.assertFalse(report['candidate_has_diagnostics'])
            self.assertIn('community-reported',report['status'])

    def test_invalid_network_settings_fail_without_changes(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp).resolve();game,_,config=self.fixture(root)
            for value in ('game_port=0','game_port=true','automatic_port_mapping="yes"','hosting_check=1'):
                data=('[d2rloader]\n[d2rloader.advanced]\n'+value).encode();config.write_bytes(data)
                with patch.object(coop,'ROOT',root),patch.object(coop,'validate_game',return_value=('steam','3.3.93787')):
                    with self.subTest(value=value),self.assertRaises(ValueError):coop.inspect(game)
                self.assertEqual(config.read_bytes(),data)

    def test_unknown_loader_and_invalid_mod_refuse_without_changes(self):
        with tempfile.TemporaryDirectory() as folder,patch.object(coop,'validate_game',return_value=('steam','3.3.93787')):
            game=Path(folder).resolve();plugin=game/'mods'/coop.MOD_NAME/coop.PLUGIN
            plugin.parent.mkdir(parents=True);plugin.write_bytes(b'synthetic')
            with self.assertRaisesRegex(ValueError,'D2RLoader files'):coop.inspect(game)
            with self.assertRaisesRegex(ValueError,'plain D2RLoader'):coop.inspect(game,'../other')
            self.assertEqual(plugin.read_bytes(),b'synthetic')

    def test_command_line_friend_comparison_prints_actionable_port_difference(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp).resolve();game,_,_=self.fixture(root)
            with patch.object(coop,'ROOT',root),patch.object(coop,'validate_game',return_value=('steam','3.3.93787')):
                report=coop.inspect(game)
                from coop_connection import friend_report
                theirs=friend_report(report);theirs['port']=4000
                path=root/'friend.json';path.write_text(json.dumps(theirs))
                output=io.StringIO()
                with patch.object(sys,'argv',['coop','--game',str(game),'--compare-report',str(path)]),contextlib.redirect_stdout(output):
                    coop.main()
            self.assertIn('this PC TCP 4123; friend TCP 4000',output.getvalue())
            self.assertIn('game_port in D2RLoader Advanced',output.getvalue())


class IsolatedStagingTests(unittest.TestCase):
    def fixture(self,game):
        from prepare_dev_scenarios import FILES,TREES
        source=game/'mods'/coop.MOD_NAME
        for relative in FILES:
            path=source/relative;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(b'original')
        for tree in TREES:
            path=source/tree/'fixture';path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(b'locally prepared')
        data=source/f'{coop.MOD_NAME}.mpq/data/local.json';data.parent.mkdir(parents=True);data.write_bytes(b'local override')
        plugin=game/'candidate.dll';plugin.write_bytes(coop.CAPABILITY)
        return source,plugin

    def test_fresh_test_mod_preserves_normal_files_and_has_separate_saves(self):
        with tempfile.TemporaryDirectory() as folder,patch.object(coop,'validate_game'),patch.object(coop,'require_game_closed'):
            game=Path(folder).resolve();source,plugin=self.fixture(game)
            before={p:p.read_bytes() for p in source.rglob('*') if p.is_file()}
            target=coop.stage_test(game,plugin)
            self.assertEqual((target/coop.PLUGIN).read_bytes(),coop.CAPABILITY)
            self.assertEqual((target/f'{coop.TEST_MOD}.mpq/modinfo.json').read_bytes(),coop.TEST_INFO)
            self.assertEqual((target/f'{coop.TEST_MOD}.mpq/data/local.json').read_bytes(),b'local override')
            self.assertFalse((target/'vrmod-manifest.json').exists())
            self.assertEqual(before,{p:p.read_bytes() for p in source.rglob('*') if p.is_file()})
            self.assertEqual(list(target.parent.glob('.pcvr-coop-*')),[])

    def test_existing_folder_and_missing_inputs_are_not_overwritten(self):
        with tempfile.TemporaryDirectory() as folder,patch.object(coop,'validate_game'),patch.object(coop,'require_game_closed'):
            game=Path(folder).resolve();source,plugin=self.fixture(game)
            target=game/'mods'/coop.TEST_MOD;target.mkdir();sentinel=target/'keep';sentinel.write_bytes(b'keep')
            with self.assertRaises(ValueError):coop.stage_test(game,plugin)
            self.assertEqual(sentinel.read_bytes(),b'keep')
            sentinel.unlink();target.rmdir();(source/'reshade/ReShade64.dll').unlink()
            with self.assertRaises(ValueError):coop.stage_test(game,plugin)
            self.assertFalse(target.exists())
            self.assertEqual(list(target.parent.glob('.pcvr-coop-*')),[])


if __name__=='__main__':unittest.main()
