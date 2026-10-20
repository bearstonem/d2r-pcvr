"""Co-op connection checks. No router/firewall edits or game protocol messages."""
import hashlib
import ipaddress
import json
import re
import socket

REPORT_SCHEMA = 'pcvr-coop-check/1'
FIELDS = {'game_version': 'Game build', 'mod': 'Selected mod',
          'plugin_sha256': 'Unleashed build', 'loader_sha256': 'D2RLoader files',
          'port': 'Game port', 'combat_policy': 'Combat rules'}


def port_number(value):
    if isinstance(value, bool) or not re.fullmatch(r'[0-9]{1,5}', str(value)):
        raise ValueError('Use a TCP port from 1 to 65535.')
    value = int(value)
    if not 1 <= value <= 65535:
        raise ValueError('Use a TCP port from 1 to 65535.')
    return value


def endpoint(text, default_port):
    """Accept a direct IPv4/DNS endpoint, not a URL or loader Game ID."""
    host = text.strip()
    port = port_number(default_port)
    if host.count(':') == 1:
        host, supplied = host.rsplit(':', 1)
        port = port_number(supplied)
    if len(host) > 253 or not re.fullmatch(r'[A-Za-z0-9](?:[A-Za-z0-9.-]*[A-Za-z0-9])?', host):
        raise ValueError('Enter the host IPv4 address or computer name, optionally followed by :port. Game IDs go in D2RLoader.')
    if all(c in '0123456789.' for c in host):
        address = ipaddress.IPv4Address(host)
        if address.is_unspecified or address.is_multicast or int(address) == 0xffffffff:
            raise ValueError('Enter a host address, not a wildcard or broadcast address.')
    elif any(not re.fullmatch(r'[A-Za-z0-9](?:[A-Za-z0-9-]*[A-Za-z0-9])?', label)
             or len(label) > 63 for label in host.split('.')):
        raise ValueError('Enter a valid IPv4 address or computer name.')
    return host, port


def probe(text, port=4000, timeout=3):
    host, port = endpoint(text, port)
    if not 0 < timeout <= 10:
        raise ValueError('Connection timeout must be between zero and ten seconds.')
    try:
        with socket.create_connection((host, port), timeout=timeout):
            pass
        status = 'reachable'
        detail = 'TCP port reachable. D2RLoader still needs to accept the game/mod; this is not a gameplay test.'
    except socket.gaierror:
        status = 'name_not_found'
        detail = 'Computer name not found. Check the address, or use the host IPv4 address.'
    except ConnectionRefusedError:
        status = 'refused'
        detail = 'Connection refused. Ask the host to create the TCP/IP game and check the game port and firewall permission.'
    except TimeoutError:
        status = 'timed_out'
        detail = 'No TCP response. Check the address, host game, firewall and router forwarding. For CGNAT, try a private VPN.'
    except OSError:
        status = 'unreachable'
        detail = 'Connection unavailable. Check both network connections, then the host game and firewall.'
    return dict(host=host, port=port, status=status, detail=detail)


def local_addresses():
    """Local adapter candidates only; no public-IP service is contacted."""
    try:
        addresses = {row[4][0] for row in socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET)}
        return sorted(address for address in addresses
                      if not ipaddress.IPv4Address(address).is_loopback
                      and not ipaddress.IPv4Address(address).is_link_local
                      and not ipaddress.IPv4Address(address).is_unspecified)
    except OSError:
        return []


def friend_report(report):
    """Exclude paths, IPs, character names, calibration and personal settings."""
    shared = {'schema': REPORT_SCHEMA, **{key: report[key] for key in FIELDS}}
    # Validate our own generated report by the same rules as a friend's input.
    validate_report(shared)
    return shared


def validate_report(report):
    if not isinstance(report, dict) or report.get('schema') != REPORT_SCHEMA:
        raise ValueError('Select a compatibility report saved by the co-op assistant.')
    for key in ('game_version', 'mod'):
        value = report.get(key)
        if not isinstance(value, str) or not re.fullmatch(r'[A-Za-z0-9_.-]{1,64}', value):
            raise ValueError('Invalid game build or mod name in compatibility report.')
    hashes = report.get('loader_sha256')
    if (not isinstance(hashes, dict) or set(hashes) != {'D2RLoader.exe', 'D2RCore.dll', 'd2rloader.mpq'}
            or any(not isinstance(h, str) or not re.fullmatch(r'[0-9a-f]{64}', h) for h in hashes.values())):
        raise ValueError('Invalid loader hashes in compatibility report.')
    if not isinstance(report.get('plugin_sha256'), str) or not re.fullmatch(r'[0-9a-f]{64}', report['plugin_sha256']):
        raise ValueError('Invalid Unleashed hash in compatibility report.')
    if type(report.get('port')) is not int:
        raise ValueError('Invalid numeric game port in compatibility report.')
    port_number(report['port'])
    policy = report.get('combat_policy')
    if (not isinstance(policy, dict) or set(policy) != {'melee_rules', 'contact_hits', 'enemies_always_hit'}
            or any(type(v) is not int or v not in (0, 1) for v in policy.values())):
        raise ValueError('Invalid combat rules in compatibility report.')


def compare_reports(ours, theirs):
    validate_report(ours)
    validate_report(theirs)
    return [label for key, label in FIELDS.items() if ours[key] != theirs[key]]


def load_friend_report(path):
    if path.stat().st_size > 16384:
        raise ValueError('Compatibility report is too large; ask for a fresh report from the assistant.')
    report = json.loads(path.read_text(encoding='utf-8-sig'))
    validate_report(report)
    return report


def check_code(report):
    data = json.dumps(friend_report(report), sort_keys=True, separators=(',', ':')).encode('utf-8')
    return hashlib.sha256(data).hexdigest()[:16]
