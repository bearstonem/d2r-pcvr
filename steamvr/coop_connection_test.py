from copy import deepcopy
import configparser
import json
from pathlib import Path
import socket
import tempfile
import unittest
from unittest.mock import patch, MagicMock

import coop_connection as connection
import coop_wizard as wizard


def report():
    return dict(game_version='3.3.93787', mod='D2R-PCVR', plugin_sha256='a'*64,
                loader_sha256={name:'b'*64 for name in ('D2RLoader.exe','D2RCore.dll','d2rloader.mpq')},
                port=4000, combat_policy=dict(melee_rules=0, contact_hits=1, enemies_always_hit=1),
                store='steam', multiplayer_button=True, automatic_port_mapping=True, hosting_check=True,
                helper_version='0.3.23', local_ip='192.168.1.5', character='private', calibration=[1,2], path='private')


class ConnectionTests(unittest.TestCase):
    def test_valid_ports_and_endpoints(self):
        self.assertEqual(connection.endpoint(' example.test:4123 ',4000),('example.test',4123))
        self.assertEqual(connection.endpoint('192.168.1.2',65535),('192.168.1.2',65535))
        self.assertEqual(connection.endpoint('gaming-pc',4000),('gaming-pc',4000))
        self.assertEqual(connection.port_number('1'),1)

    def test_invalid_endpoints_never_open_a_socket(self):
        for value in ('', 'https://example.test', 'example.test/path', 'bad host', '-bad', 'bad..name',
                      '127.0.0.1:0', '127.0.0.1:65536', '0.0.0.0', '255.255.255.255', '224.0.0.1',
                      '999.1.1.1', '[::1]:4000', 'a'*254, 'host:4;echo'):
            with self.subTest(value=value), patch.object(connection.socket,'create_connection') as create:
                with self.assertRaises(ValueError):connection.probe(value)
                create.assert_not_called()
        for port in (True,False,0,-1,65536,'abc',4.5,None):
            with self.subTest(port=port), self.assertRaises(ValueError):connection.port_number(port)

    def test_probe_has_bounded_timeout_and_no_protocol_payload(self):
        with patch.object(connection.socket,'create_connection') as create:
            result=connection.probe('example.test:4123',4000)
            self.assertEqual(result['status'],'reachable')
            self.assertIn('not a gameplay test',result['detail'])
            create.assert_called_once_with(('example.test',4123),timeout=3)
            create.return_value.__enter__.return_value.send.assert_not_called()
        with self.assertRaises(ValueError):connection.probe('host',4000,11)

    def test_different_failures_have_specific_help(self):
        for error,status in ((socket.gaierror(),'name_not_found'),(ConnectionRefusedError(),'refused'),
                             (TimeoutError(),'timed_out'),(OSError(),'unreachable')):
            with patch.object(connection.socket,'create_connection',side_effect=error):
                self.assertEqual(connection.probe('example.test')['status'],status)

    def test_only_local_usable_addresses_are_displayed(self):
        data=[(0,0,0,'',(ip,0)) for ip in ('192.168.1.8','100.64.2.3','127.0.0.1','169.254.1.1','0.0.0.0','192.168.1.8')]
        with patch.object(connection.socket,'getaddrinfo',return_value=data):
            self.assertEqual(connection.local_addresses(),['100.64.2.3','192.168.1.8'])
        with patch.object(connection.socket,'getaddrinfo',side_effect=OSError):
            self.assertEqual(connection.local_addresses(),[])

    def test_real_loopback_probe_only_opens_a_tcp_connection(self):
        with socket.socket(socket.AF_INET,socket.SOCK_STREAM) as listener:
            listener.bind(('127.0.0.1',0));listener.listen(1)
            self.assertEqual(connection.probe('127.0.0.1',listener.getsockname()[1])['status'],'reachable')
            peer,_=listener.accept()
            with peer:
                self.assertEqual(peer.recv(1),b'')


class CompatibilityTests(unittest.TestCase):
    def test_share_report_omits_private_fields(self):
        shared=connection.friend_report(report())
        self.assertEqual(set(shared),set(connection.FIELDS)|{'schema'})
        text=json.dumps(shared)
        self.assertNotIn('192.168',text);self.assertNotIn('private',text);self.assertNotIn('calibration',text)
        self.assertEqual(connection.check_code(report()),connection.check_code(dict(report(),local_ip='other',calibration=[])))

    def test_every_compatibility_difference_is_identified(self):
        ours=connection.friend_report(report())
        for key,value in (('game_version','3.3.93847'),('mod','pcvr-coop'),('plugin_sha256','c'*64),
                          ('loader_sha256',{name:'d'*64 for name in ours['loader_sha256']}),('port',4123),
                          ('combat_policy',dict(melee_rules=1,contact_hits=1,enemies_always_hit=1))):
            theirs=deepcopy(ours);theirs[key]=value
            self.assertEqual(connection.compare_reports(ours,theirs),[connection.FIELDS[key]])
        self.assertEqual(connection.compare_reports(ours,deepcopy(ours)),[])

    def test_invalid_friend_reports_are_rejected(self):
        base=connection.friend_report(report())
        for key,value in (('schema','other'),('game_version','bad\x1b[2J'),('mod','../other'),
                          ('plugin_sha256','not hash'),('loader_sha256',{}),('port',True),
                          ('combat_policy',dict(melee_rules=0,contact_hits=True,enemies_always_hit=1))):
            bad=deepcopy(base);bad[key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):connection.validate_report(bad)

    def test_report_read_is_bounded_and_accepts_utf8_bom(self):
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'friend.json';expected=connection.friend_report(report())
            path.write_text(json.dumps(expected),encoding='utf-8-sig')
            self.assertEqual(connection.load_friend_report(path),expected)
            path.write_bytes(b' '*16385)
            with self.assertRaisesRegex(ValueError,'too large'):connection.load_friend_report(path)

    def test_comparison_shows_port_and_melee_values_with_their_remedies(self):
        ours=connection.friend_report(report());theirs=deepcopy(ours)
        theirs['port']=4123;theirs['combat_policy']['melee_rules']=1
        text=connection.comparison_text(ours,theirs)
        self.assertIn('this PC TCP 4000; friend TCP 4123',text)
        self.assertIn('game_port in D2RLoader Advanced',text)
        self.assertIn('this PC Unleashed; friend Classic',text)
        self.assertIn('VR Settings > Combat > Melee rules',text)
        self.assertNotIn('DLLs differ',text)

    def test_comparison_covers_build_mod_and_shared_ini_options(self):
        ours=connection.friend_report(report());theirs=deepcopy(ours)
        theirs.update(game_version='3.3.93847',mod='pcvr-coop',plugin_sha256='c'*64)
        theirs['combat_policy'].update(contact_hits=0,enemies_always_hit=0)
        text=connection.comparison_text(ours,theirs)
        for part in ('3.3.93787; friend 3.3.93847','D2R-PCVR; friend pcvr-coop',
                     'same release','contact_hits: this PC 1; friend 0',
                     'enemies_always_hit: this PC 1; friend 0','d2r_pcvr.ini'):
            self.assertIn(part,text)
        self.assertNotIn('192.168',text)
        self.assertNotIn('calibration',text)

    def test_comparison_names_only_the_loader_files_that_differ(self):
        ours=connection.friend_report(report());theirs=deepcopy(ours)
        theirs['loader_sha256']['D2RLoader.exe']='d'*64
        text=connection.comparison_text(ours,theirs)
        self.assertIn('D2RLoader files differ: D2RLoader.exe.',text)
        self.assertNotIn('D2RCore.dll',text)

    def test_matching_reports_still_explain_loader_checks_and_reject_invalid_input(self):
        ours=connection.friend_report(report())
        self.assertIn('D2RLoader still checks game data',connection.comparison_text(ours,deepcopy(ours)))
        theirs=deepcopy(ours);theirs['mod']='bad\x1b[2J'
        with self.assertRaises(ValueError):connection.comparison_text(ours,theirs)


class WizardTests(unittest.TestCase):
    def test_guidance_distinguishes_host_guest_lan_internet_and_vpn(self):
        host=wizard.instructions('host','internet',4123,['192.168.1.8'])
        self.assertIn('TCP 4123',host);self.assertIn('CGNAT',host);self.assertIn('192.168.1.8',host)
        self.assertIn('Joining normally needs no forwarding',wizard.instructions('guest','internet',4000,[]))
        self.assertIn('LAN Only',wizard.instructions('host','lan',4000,[]))
        self.assertIn('Tailscale',wizard.instructions('guest','vpn',4000,[]))

    def test_default_wizard_does_not_change_settings_or_connect(self):
        answers=iter(['2','2','q']);messages=[]
        with patch('coop_setup.enable_button') as enable,patch.object(wizard,'probe') as probe:
            wizard.run(Path('game'),report(),lambda _:next(answers),messages.append)
        enable.assert_not_called();probe.assert_not_called()
        self.assertTrue(any('No game was launched' in line for line in messages))

    def test_hidden_button_requires_explicit_opt_in(self):
        answers=iter(['2','2','n','q'])
        with patch('coop_setup.enable_button') as enable:
            wizard.run(Path('game'),dict(report(),multiplayer_button=False),lambda _:next(answers),lambda _:None)
        enable.assert_not_called()
        answers=iter(['2','2','y','q'])
        with patch('coop_setup.enable_button',return_value=None) as enable:
            wizard.run(Path('game'),dict(report(),multiplayer_button=False),lambda _:next(answers),lambda _:None)
        enable.assert_called_once_with(Path('game'))

    def test_guest_probe_only_runs_for_explicit_address(self):
        answers=iter(['2','2','1','example.test:4123','q']);messages=[]
        with patch('coop_setup.inspect',return_value=report()),patch.object(wizard,'probe',return_value=dict(port=4123,detail='result')) as probe:
            wizard.run(Path('game'),report(),lambda _:next(answers),messages.append)
        probe.assert_called_once_with('example.test:4123',4000)
        self.assertTrue(any('your loader still uses 4000' in line for line in messages))

    def test_export_never_overwrites_an_existing_file(self):
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'friend.json';path.write_bytes(b'keep')
            answers=iter(['2','2','3',str(path),'q'])
            with patch('coop_setup.inspect',return_value=report()):
                wizard.run(Path('game'),report(),lambda _:next(answers),lambda _:None)
            self.assertEqual(path.read_bytes(),b'keep')

    def test_friend_file_comparison_provides_the_setting_and_values(self):
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'friend.json';theirs=connection.friend_report(report())
            theirs['port']=4123;path.write_text(json.dumps(theirs))
            answers=iter(['2','2','4',str(path),'q']);messages=[]
            with patch('coop_setup.inspect',return_value=report()):
                wizard.run(Path('game'),report(),lambda _:next(answers),messages.append)
            self.assertTrue(any('this PC TCP 4000; friend TCP 4123' in line for line in messages))

    def test_settings_parse_error_returns_to_menu_instead_of_ending_assistant(self):
        answers=iter(['2','2','5','5','q']);messages=[]
        with patch('coop_setup.inspect',side_effect=[configparser.MissingSectionHeaderError('ini',1,'bad'),report()]):
            wizard.run(Path('game'),report(),lambda _:next(answers),messages.append)
        self.assertTrue(any('Co-op check:' in line for line in messages))
        self.assertIn('No game was launched',messages[-1])

    def test_input_end_is_a_clean_exit(self):
        messages=[]
        wizard.run(Path('game'),report(),MagicMock(side_effect=EOFError),messages.append)
        self.assertIn('\nCo-op assistant closed.',messages)


if __name__=='__main__':unittest.main()
