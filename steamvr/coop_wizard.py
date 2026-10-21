"""Double-click co-op assistant using the install's existing private Python."""
from datetime import datetime
import configparser
import json
import os
from pathlib import Path
import subprocess

from coop_connection import check_code, comparison_text, friend_report, load_friend_report, local_addresses, probe


def choose(prompt, options, default, ask=input, say=print):
    while True:
        say(prompt)
        for key, label in options.items():
            say(f'  {key}. {label}')
        answer = ask(f'Choice [{default}]: ').strip().lower() or default
        if answer in options:
            return answer
        say('Choose one of the listed options.')


def instructions(role, network, port, addresses):
    lines = [f'Game port: TCP {port}. Both players need the same port and Unleashed build.']
    if role == 'host':
        lines += ['Launch your usual Unleashed VR shortcut, select an offline character, then Multiplayer beside Play.']
        if network == 'internet':
            lines += ['Create an Unlisted game and let D2RLoader finish its Hosting status check.',
                      'If reachable, share the Game ID from D2RLoader with your friend.',
                      f'If not reachable: allow D2RLoader.exe through Windows Firewall, then forward TCP {port}',
                      f'from your router to this PC on TCP {port}. Use its LAN address below, not a VPN adapter.',
                      'Keep that LAN address reserved in your router so forwarding still works after a restart.',
                      'With two routers, forwarding may be needed on both. A private/100.64-100.127 WAN address',
                      'can mean double NAT or CGNAT: ask your ISP about a public IPv4 address, or use a private VPN.']
        else:
            lines += ['Create a LAN Only game. Your friend uses Direct Join with this PC address.',
                      'Use the LAN address on the same home network, or the VPN address on your private VPN.']
        lines += ['Local IPv4 candidates: '+(', '.join(addresses) if addresses else 'not detected; check Windows Network settings.'),
                  'Local addresses are for LAN, VPN or router forwarding. An internet friend needs the Game ID or public IPv4.']
    else:
        lines += ['Ask the host to create the game first, then launch your usual Unleashed VR shortcut.',
                  'Select an offline character, choose Multiplayer, then join with the host Game ID or Direct Join.']
        if network == 'internet':
            lines += ['Use the host Game ID or public IPv4. A home-network 192.168.x.x / 10.x.x address will not work over the internet.',
                      'The host handles router forwarding. Joining normally needs no forwarding rule on your router.']
        else:
            lines += ['Use the host LAN IPv4 on the same network, or the host VPN IPv4 when using a private VPN.']
    if network == 'vpn':
        lines += ['Connect both PCs through the same private VPN first, then use LAN Only / Direct Join.',
                  'A private VPN such as Tailscale can avoid router forwarding, including on many CGNAT connections.',
                  'It is optional, installed separately, and has not been verified with this co-op assistant.']
    lines += ['Keep the firewall enabled. Permit the game/port on the network you are using.',
              'If the connection works but joining fails, compare setup reports and check the loader compatibility message.',
              'Remote VR hand/body movements are not replicated yet. Test both players attacking, looting, portals and reconnecting.']
    return '\n'.join(lines)


def copy_report(text):
    if os.name != 'nt':
        raise ValueError('Clipboard copy is available on Windows; use Save report here.')
    clip = Path(os.environ['SystemRoot'])/'System32/clip.exe'
    subprocess.run([str(clip)], input=text.encode('utf-16le'), check=True, timeout=3, creationflags=0x08000000)


def run(game, report, ask=input, say=print):
    from coop_setup import enable_button, inspect
    say('\nD2R VR Unleashed - Co-op setup')
    say(f'Game build {report["game_version"]} | mod {report["mod"]} | check code {check_code(report)}')
    say('Basic co-op is community-reported working. Remote VR movements are not shared yet.')
    say('This assistant uses your installed mod and existing offline characters; no test DLL is required.')
    try:
        role = choose('What are you doing?', {'1': 'Host a game', '2': 'Join a friend'}, '1', ask, say)
        network = choose('How will you connect?', {'1': 'Same home network (LAN)', '2': 'Internet',
                         '3': 'Private VPN already set up on both PCs'}, '2', ask, say)
        role = {'1': 'host', '2': 'guest'}[role]
        network = {'1': 'lan', '2': 'internet', '3': 'vpn'}[network]
        addresses = local_addresses() if role == 'host' else []
        say('\n'+instructions(role, network, report['port'], addresses))
        say('Automatic router mapping: '+('On' if report['automatic_port_mapping'] else 'Off; enable it in D2RLoader Advanced if wanted.'))
        say('Loader internet Hosting check: '+('On' if report['hosting_check'] else 'Off; enable it in D2RLoader Advanced for an external reachability check.'))
        if not report['multiplayer_button']:
            say('\nThe Multiplayer button is currently hidden. Enabling it requires D2R/D2RLoader closed.')
            if ask('Show the Multiplayer button now? [y/N]: ').strip().lower() == 'y':
                backup = enable_button(game)
                report['multiplayer_button'] = True
                say('Multiplayer enabled.'+(f' Original loader settings: {backup}' if backup else ''))
        while True:
            action = choose('\nNext step', {'1': 'Test the host address (after the host creates a game)',
                            '2': 'Copy compatibility report for a friend', '3': 'Save compatibility report',
                            '4': 'Compare a friend report file', '5': 'Show hosting/joining steps again',
                            '6': 'Enable the Multiplayer button', 'q': 'Finish'}, 'q', ask, say)
            if action == 'q':
                break
            try:
                # Refresh the compatibility data if settings changed while this assistant was open.
                report = inspect(game, report['mod'], report['store'] == 'battlenet')
                shared = friend_report(report)
                if action == '1':
                    host = ask('Host IPv4/name[:port], not Game ID (blank cancels): ').strip()
                    if host:
                        result = probe(host, report['port'])
                        say(result['detail'])
                        if result['port'] != report['port']:
                            say(f'The tested port is {result["port"]}; your loader still uses {report["port"]}. Both PCs must match in Advanced > game_port.')
                elif action == '2':
                    copy_report(json.dumps(shared, indent=2))
                    say('Compatibility report copied. It contains no IPs, local paths or character data.')
                elif action == '3':
                    folder = Path.home()/'Downloads'
                    if not folder.is_dir():
                        folder = Path.home()
                    default = folder/('Unleashed-coop-check-'+datetime.now().strftime('%Y%m%d-%H%M%S')+'.json')
                    path = Path(ask(f'Save report [{default}]: ').strip().strip('"') or default).expanduser()
                    with path.open('x', encoding='utf-8') as output:
                        json.dump(shared, output, indent=2)
                        output.write('\n')
                    say(f'Saved {path}. Send this small file to your friend.')
                elif action == '4':
                    name = ask('Friend report file (blank cancels): ').strip().strip('"')
                    if name:
                        say(comparison_text(shared, load_friend_report(Path(name).expanduser())))
                elif action == '5':
                    say(instructions(role, network, report['port'], addresses))
                elif action == '6':
                    say('Close D2R/D2RLoader first. Only the Multiplayer button setting changes.')
                    if ask('Enable Multiplayer? [y/N]: ').strip().lower() == 'y':
                        backup = enable_button(game)
                        say('Multiplayer enabled.'+(f' Original loader settings: {backup}' if backup else ''))
            except (OSError, ValueError, KeyError, configparser.Error, subprocess.SubprocessError) as error:
                say(f'Co-op check: {error}')
    except (EOFError, KeyboardInterrupt):
        say('\nCo-op assistant closed.')
    say('When ready, start Unleashed with your usual VR shortcut. No game was launched and no router/firewall rules or saves were changed.')
