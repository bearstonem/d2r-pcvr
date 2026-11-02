"""Narrow Steam configuration edits, with backups and an exact binary round trip."""
from dataclasses import dataclass
from datetime import datetime
import copy
import ntpath
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile
import zlib

APP_ID = '2536520'
SHORTCUTS = {
    'steamvr': ('D2R VR Unleashed - SteamVR', '-applaunch 2536520 --frame-vr', 1),
    'vdxr': ('D2R VR Unleashed - Virtual Desktop', '-applaunch 2536520 --frame-vr --frame-vdxr', 0),
}


def quote(value):
    return '"' + value.replace('\\', '\\\\').replace('"', '\\"').replace('\n', '\\n').replace('\r', '\\r').replace('\t', '\\t') + '"'


@dataclass
class Entry:
    key: str
    value: object
    start: int
    end: int
    close: int = 0


def parse_text(text):
    """Record spans so changing one launch option leaves all other text intact."""
    token = re.compile(r'\s+|//[^\r\n]*|"(?:\\.|[^"\\])*"|[{}]|[^\s{}"]+')
    tokens = []
    offset = 0
    for match in token.finditer(text):
        if match.start() != offset:
            raise ValueError('Unsupported Steam text configuration; use the manual install directions.')
        offset = match.end()
        value = match[0]
        if value.isspace() or value.startswith('//'):
            continue
        tokens.append((value, match.start(), match.end()))
    if offset != len(text):
        raise ValueError('Truncated Steam text configuration')
    index = 0

    def unquote(value):
        if not value.startswith('"'):
            return value
        return re.sub(r'\\(.)', lambda m: {'n': '\n', 'r': '\r', 't': '\t'}.get(m[1], m[1]), value[1:-1])

    def tree(nested=False):
        nonlocal index
        result = []
        while index < len(tokens):
            key, _, _ = tokens[index]
            index += 1
            if key == '}':
                if not nested:
                    raise ValueError('Unexpected Steam configuration closing brace')
                return result
            if key == '{' or index >= len(tokens):
                raise ValueError('Missing Steam configuration value')
            value, start, end = tokens[index]
            index += 1
            if value == '{':
                children = tree(True)
                result.append(Entry(unquote(key), children, start, tokens[index - 1][2], tokens[index - 1][1]))
            elif value == '}':
                raise ValueError('Missing Steam configuration value')
            else:
                result.append(Entry(unquote(key), unquote(value), start, end))
        if nested:
            raise ValueError('Unclosed Steam configuration block')
        return result
    return tree()


def child(entries, key):
    matches = [entry for entry in entries if entry.key.lower() == key.lower()]
    if len(matches) > 1:
        raise ValueError(f'Duplicate Steam configuration key: {key}; use the manual directions.')
    return matches[0] if matches else None


def text_value(entries, *keys):
    for key in keys:
        entry = child(entries, key)
        if entry is None:
            return None
        entries = entry.value
    return entries


def set_launch_options(text, options):
    keys = ['UserLocalConfigStore', 'Software', 'Valve', 'Steam', 'apps', APP_ID, 'LaunchOptions']
    entries = parse_text(text.lstrip('\ufeff'))
    # Retain a possible BOM in the original offsets.
    if text.startswith('\ufeff'):
        return '\ufeff' + set_launch_options(text[1:], options)
    parent = None
    for index, key in enumerate(keys):
        entry = child(entries, key)
        if entry is None:
            if parent is None:
                raise ValueError('Steam local profile is missing UserLocalConfigStore. Launch Steam once first.')
            fragment = quote(options)
            for missing in reversed(keys[index:]):
                fragment = quote(missing) + '\t' + fragment if missing == 'LaunchOptions' else quote(missing) + '\n{\n' + fragment + '\n}'
            return text[:parent.close] + '\n' + fragment + '\n' + text[parent.close:]
        if index == len(keys) - 1:
            if not isinstance(entry.value, str):
                raise ValueError('Invalid Steam launch options')
            return text[:entry.start] + quote(options) + text[entry.end:]
        if not isinstance(entry.value, list):
            raise ValueError(f'Invalid Steam configuration block: {key}')
        parent, entries = entry, entry.value
    raise AssertionError('Unreachable')


def decode_binary(data):
    offset = 0

    def string():
        nonlocal offset
        end = data.index(0, offset)
        result = data[offset:end].decode('utf-8', errors='surrogateescape')
        offset = end + 1
        return result

    def tree():
        nonlocal offset
        result = {}
        while True:
            kind = data[offset]
            offset += 1
            if kind == 8:
                return result
            key = string()
            if key in result:
                raise ValueError('Duplicate Steam shortcut key; refusing to rewrite')
            if kind == 0:
                value = tree()
            elif kind == 1:
                value = string()
            elif kind == 2:
                value = struct.unpack_from('<I', data, offset)[0]
                offset += 4
            else:
                raise ValueError(f'Unsupported Steam shortcut type {kind}; use the manual directions.')
            result[key] = value

    try:
        result = tree()
    except (IndexError, struct.error) as error:
        raise ValueError('Truncated Steam shortcut configuration') from error
    if offset != len(data):
        raise ValueError('Unexpected trailing Steam shortcut data')
    return result


def encode_binary(tree):
    result = bytearray()
    for key, value in tree.items():
        kind = 0 if isinstance(value, dict) else 1 if isinstance(value, str) else 2
        result.append(kind)
        result.extend(key.encode('utf-8', errors='surrogateescape') + b'\0')
        if kind == 0:
            result.extend(encode_binary(value))
        elif kind == 1:
            result.extend(value.encode('utf-8', errors='surrogateescape') + b'\0')
        else:
            result.extend(struct.pack('<I', value))
    result.append(8)
    return bytes(result)


def shortcut_specs(steam, game, store='steam', python=None, wrapper=None, runtime=None):
    if store == 'steam':
        return {connection: (name, options, openvr, steam / 'steam.exe', steam)
                for connection, (name, options, openvr) in SHORTCUTS.items()}
    if store != 'battlenet' or python is None or wrapper is None:
        raise ValueError('Battle.net shortcuts require the Python interpreter and launch wrapper')
    common = [str(wrapper), '--battlenet-game=' + str(game),
              '--experimental-battlenet', '--frame-vr']
    result = {}
    for connection, suffix in [('steamvr', 'SteamVR'), ('vdxr', 'Virtual Desktop')]:
        if connection == 'steamvr' and runtime is None:
            continue
        arguments = common + (['--frame-openxr-runtime=' + str(runtime)]
                              if connection == 'steamvr' else ['--frame-vdxr'])
        name = 'D2R VR Unleashed (Battle.net) - ' + suffix
        result[connection] = (name, subprocess.list2cmdline(arguments),
                              int(connection == 'steamvr'), python, wrapper.parent)
    return result


def is_same_battlenet_shortcut(item, game, connection):
    """Recognize generated routing for the same Battle.net game."""
    exe = item.get('Exe', '')
    options = item.get('LaunchOptions', '')
    if not isinstance(exe, str) or not isinstance(options, str):
        return False
    executable = re.fullmatch(r'"([^"\r\n]+)"', exe)
    if not executable or ntpath.basename(executable[1]).lower() not in {
            'python.exe', 'pythonw.exe'}:
        return False
    token = r'(?:"[^"\r\n]+"|[^\s"]+)'
    game_arg = subprocess.list2cmdline(['--battlenet-game=' + str(game)])
    command = re.fullmatch(
        rf'(?P<wrapper>{token}) {re.escape(game_arg)} '
        rf'--experimental-battlenet --frame-vr (?P<runtime>{token})',
        options)
    if not command:
        return False
    wrapper = command['wrapper'].strip('"')
    if (ntpath.basename(wrapper).lower() != 'launch_windows.py'
            or ntpath.basename(ntpath.dirname(wrapper)).lower() != 'steamvr'):
        return False
    runtime = command['runtime'].strip('"')
    if connection == 'vdxr':
        return runtime == '--frame-vdxr'
    prefix = '--frame-openxr-runtime='
    return (connection == 'steamvr' and runtime.startswith(prefix)
            and len(runtime) > len(prefix))


def update_shortcuts(original, steam, game, connections, **routing):
    specs = shortcut_specs(steam, game, **routing)
    if not set(connections).issubset(specs):
        raise ValueError('Battle.net SteamVR shortcuts require an explicit SteamVR runtime manifest')
    root = decode_binary(original) if original is not None else {'shortcuts': {}}
    if original is not None and encode_binary(root) != original:
        raise ValueError('Steam shortcuts do not round-trip exactly; use the manual directions.')
    entries = root.get('shortcuts')
    if not isinstance(entries, dict) or any(not key.isdecimal() or not isinstance(value, dict) for key, value in entries.items()):
        raise ValueError('Unexpected Steam shortcut structure')
    previous = copy.deepcopy(entries)
    for connection in connections:
        name, options, openvr, executable, directory = specs[connection]
        aliases = {name, name.replace('D2R VR Unleashed', 'Diablo II: Resurrected VR')}
        matches = [key for key, item in entries.items() if item.get('appname', item.get('AppName')) in aliases]
        if len(matches) > 1:
            raise ValueError(f'Remove duplicate library entries named {name} in Steam, then run setup again.')
        key = matches[0] if matches else str(max(map(int, entries), default=-1) + 1)
        item = entries.setdefault(key, {})
        exe = '"' + str(executable) + '"'
        same_command = (item.get('Exe') == exe
                        and item.get('LaunchOptions') == options)
        same_game = (routing.get('store') == 'battlenet'
                     and is_same_battlenet_shortcut(item, game, connection))
        if item and not (same_command or same_game):
            raise ValueError(f'A different shortcut already uses the name {name}. Rename it in Steam before setup.')
        appid = item.get('appid', zlib.crc32((exe + name).encode('utf-8')) | 0x80000000)
        if not isinstance(appid, int) or not 0x80000000 <= appid <= 0xffffffff:
            raise ValueError('Invalid non-Steam shortcut appid')
        if any(other != key and value.get('appid') == appid for other, value in entries.items()):
            raise ValueError('Steam shortcut appid collision; no files were changed')
        item.pop('AppName', None)
        item.update({
            'appid': appid,
            'appname': name, 'Exe': exe, 'StartDir': '"' + str(directory) + '"',
            'icon': str(game / 'D2R.exe'), 'ShortcutPath': '', 'LaunchOptions': options,
            'IsHidden': 0, 'AllowDesktopConfig': 0, 'AllowOverlay': 0,
            'OpenVR': openvr, 'Devkit': 0, 'DevkitGameID': '', 'DevkitOverrideAppID': 0, 'FlatpakAppID': '',
        })
        item.setdefault('LastPlayTime', 0)
        item.setdefault('tags', {'0': 'D2R PCVR'})
    targets = {name for connection in connections for name in
               (specs[connection][0], specs[connection][0].replace('D2R VR Unleashed', 'Diablo II: Resurrected VR'))}
    for key, value in previous.items():
        if value.get('appname', value.get('AppName')) not in targets and entries[key] != value:
            raise ValueError('An unrelated Steam shortcut changed')
    result = encode_binary(root)
    if decode_binary(result) != root:
        raise ValueError('Steam shortcut verification failed')
    return result


def is_vr_launch_options(options):
    return isinstance(options, str) and '%command%' in options and bool(
        re.search(r'steamvr[\\/]launch_windows\.py(?:"|\s)', options, re.IGNORECASE))


def uninstall_updates(profile, game, store, state=None):
    """Remove this edition's entries without replacing a whole Steam profile."""
    state = state or {}
    config = profile / 'config'
    updates = {}
    shortcuts = config / 'shortcuts.vdf'
    if shortcuts.is_file():
        original = shortcuts.read_bytes()
        root = decode_binary(original)
        if encode_binary(root) != original or not isinstance(root.get('shortcuts'), dict):
            raise ValueError('Steam shortcuts cannot be safely edited. No files were removed.')
        names = {name for name, _, _ in SHORTCUTS.values()} if store == 'steam' else {
            'Diablo II: Resurrected VR (Battle.net) - SteamVR',
            'Diablo II: Resurrected VR (Battle.net) - Virtual Desktop'}
        names |= {name.replace('D2R VR Unleashed', 'Diablo II: Resurrected VR') for name in names}
        names |= {name.replace('Diablo II: Resurrected VR', 'D2R VR Unleashed') for name in names}
        removed = False
        for key, entry in list(root['shortcuts'].items()):
            if not isinstance(entry, dict):
                raise ValueError('Invalid Steam shortcut entry.')
            options = entry.get('LaunchOptions', '')
            marker = '-applaunch ' + APP_ID if store == 'steam' else '--battlenet-game='
            icon = entry.get('icon', '')
            if (entry.get('appname', entry.get('AppName')) in names and isinstance(options, str)
                    and marker in options and '--frame-vr' in options and isinstance(icon, str)
                    and (Path(icon).resolve() == (game / 'D2R.exe').resolve()
                         or (Path(icon).resolve() == (config / 'grid' / f"{entry.get('appid')}_icon.png").resolve()
                             and (store == 'steam' or (' ' + subprocess.list2cmdline(['--battlenet-game=' + str(game)]) + ' ') in (' ' + options + ' '))))):
                # Only remove images still identical to our bundled files; keep
                # any artwork the player replaced after installation.
                connection = 'vdxr' if '--frame-vdxr' in options else 'steamvr'
                for name, suffix in ARTWORK_FILES.items():
                    target = config / 'grid' / f"{entry.get('appid')}{suffix}.png"
                    bundled = Path(__file__).resolve().parent.parent / 'artwork' / ARTWORK_FOLDERS[connection] / name
                    if (not any(other != key and item.get('appid') == entry.get('appid')
                                for other, item in root['shortcuts'].items())
                            and not target.is_symlink() and target.is_file() and bundled.is_file()
                            and target.read_bytes() == bundled.read_bytes()):
                        updates[target] = None
                del root['shortcuts'][key]
                removed = True
        if removed:
            root['shortcuts'] = {str(index): entry for index, entry in enumerate(root['shortcuts'].values())}
        payload = encode_binary(root)
        if payload != original:
            updates[shortcuts] = payload
    if store == 'steam':
        local = config / 'localconfig.vdf'
        if local.is_file():
            original = local.read_bytes()
            source = original.decode('utf-8')
            options = text_value(parse_text(source.lstrip('\ufeff')), 'UserLocalConfigStore',
                                 'Software', 'Valve', 'Steam', 'apps', APP_ID, 'LaunchOptions')
            expected = state.get('installed_launch_options')
            if is_vr_launch_options(options) and (expected is None or options == expected):
                previous = state.get('previous_launch_options', '')
                if not isinstance(previous, str) or is_vr_launch_options(previous):
                    previous = ''
                updates[local] = set_launch_options(source, previous).encode('utf-8')
    return updates


ARTWORK_FILES = {'Background.png': '_hero', 'Cover.png': 'p',
                 'Wide-Cover.png': '', 'Logo.png': '_logo', 'Icon.png': '_icon'}
ARTWORK_FOLDERS = {'steamvr': 'SteamVR', 'vdxr': 'Virtual-Desktop'}


def artwork_updates(payload, profile, steam, game, connections, artwork_root, **routing):
    """Plan exact per-account grid files and the separate shortcut icon field.

    Steam uses the unsigned 32-bit shortcut appid here, not its 64-bit launch ID.
    PNGs are copied byte-for-byte. Alternate formats for these exact slots are
    backed up and removed transactionally so Steam cannot choose stale artwork.
    """
    root = decode_binary(payload)
    specs = shortcut_specs(steam, game, **routing)
    grid = profile / 'config/grid'
    if not grid.resolve().is_relative_to((profile / 'config').resolve()):
        raise ValueError('Steam artwork directory points outside the selected profile')
    updates = {}
    for connection in connections:
        entry = next(item for item in root['shortcuts'].values() if item.get('appname') == specs[connection][0])
        appid = entry['appid']
        for name, suffix in ARTWORK_FILES.items():
            target = grid / f'{appid}{suffix}.png'
            if target.is_symlink():
                raise ValueError('Steam artwork target is a symbolic link')
            data = (artwork_root / ARTWORK_FOLDERS[connection] / name).read_bytes()
            if not data.startswith(b'\x89PNG\r\n\x1a\n'):
                raise ValueError(f'Invalid bundled Steam artwork: {name}')
            updates[target] = data
            for existing in grid.glob(f'{appid}{suffix}.*'):
                if existing != target and existing.suffix.lower() in {'.png', '.jpg', '.jpeg', '.webp', '.tga'}:
                    if existing.is_symlink():
                        raise ValueError('Steam artwork target is a symbolic link')
                    updates[existing] = None
        entry['icon'] = str(grid / f'{appid}_icon.png')
    return encode_binary(root), updates


def configure(profile, steam, game, options, connections, backups, require_closed, artwork_root=None, **routing):
    """Stage Steam edits and roll back a partial write; never edit a live Steam client."""
    config = profile / 'config'
    local = config / 'localconfig.vdf'
    shortcuts = config / 'shortcuts.vdf'
    require_closed()
    # Battle.net shortcuts never modify the Steam game's launch options.
    originals = {shortcuts: shortcuts.read_bytes() if shortcuts.exists() else None}
    updates = {shortcuts: update_shortcuts(originals[shortcuts], steam, game, connections, **routing)}
    if artwork_root is not None:
        payload, artwork = artwork_updates(updates[shortcuts], profile, steam, game, connections, artwork_root, **routing)
        updates = {**artwork, shortcuts: payload}
        originals.update({path: path.read_bytes() if path.exists() else None for path in artwork})
    if options is not None:
        originals = {local: local.read_bytes(), **originals}
        updates = {local: set_launch_options(originals[local].decode('utf-8'), options).encode('utf-8'), **updates}
    backup = backups / ('steam-' + datetime.now().strftime('%Y%m%d-%H%M%S-%f'))
    backup.mkdir(parents=True)
    for path, original in originals.items():
        if original is not None:
            destination = backup / path.relative_to(config)
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, destination)
    staged, replaced = {}, []
    try:
        for path, payload in updates.items():
            path.parent.mkdir(parents=True, exist_ok=True)
            if payload is None:
                staged[path] = None
                continue
            with tempfile.NamedTemporaryFile(dir=path.parent, prefix='d2r-setup-', delete=False) as output:
                staged[path] = Path(output.name)
                output.write(payload)
        require_closed()
        for path, original in originals.items():
            current = path.read_bytes() if path.exists() else None
            if current != original:
                raise ValueError('Steam configuration changed during setup. Close Steam and try again.')
        for path, temporary in staged.items():
            if temporary is None:
                path.unlink(missing_ok=True)
            else:
                os.replace(temporary, path)
            replaced.append(path)
        if any((path.read_bytes() if path.exists() else None) != payload for path, payload in updates.items()):
            raise OSError('Steam configuration verification failed')
    except BaseException:
        for path in reversed(replaced):
            if originals[path] is None:
                path.unlink(missing_ok=True)
            else:
                path.write_bytes(originals[path])
        raise
    finally:
        for temporary in staged.values():
            if temporary is not None:
                temporary.unlink(missing_ok=True)
    return backup
