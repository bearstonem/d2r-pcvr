"""Identify local game storage and gate builds before installing any files."""
from pathlib import Path
import re

# The game builds this release supports, newest first. Setup names the first;
# the older ones stay accepted for players whose game has not updated yet.
STEAM_VERSIONS = ('3.3.94995', '3.3.93787')
BATTLENET_VERSIONS = ('3.3.93847',)
STEAM_VERSION = STEAM_VERSIONS[0]
BATTLENET_VERSION = BATTLENET_VERSIONS[0]


def storage_config(game):
    game = Path(game)
    for directory in ('Data', 'data'):
        config = game / directory / '.build.config'
        if config.is_file():
            return 'steam', config
    if (game / '.build.info').is_file():
        return 'battlenet', game / '.build.info'
    raise ValueError('Game archive configuration not found (.build.config or .build.info).')


def validate_game(game, store='auto', experimental_battlenet=False):
    game = Path(game)
    detected, _ = storage_config(game)
    if store != 'auto' and detected != store:
        raise ValueError(f'This is a {detected} installation, but {store} was selected.')
    supported = STEAM_VERSIONS if detected == 'steam' else BATTLENET_VERSIONS
    exe = game / 'D2R.exe'
    data = exe.read_bytes() if exe.is_file() else b''
    expected = next((v for v in supported if v.encode('utf-16-le') in data), None)
    if not expected:
        found = file_version(data)
        raise ValueError(f'This build requires {detected} D2R {" or ".join(supported)}'
                         + (f'; your game is {found}.' if found else '.') + ' Your game version does not match.')
    if detected == 'battlenet' and not experimental_battlenet:
        raise ValueError('Battle.net support is experimental. Use --experimental-battlenet for local offline compatibility testing.')
    return detected, expected


def file_version(data):
    """The FileVersion string in the executable's version resource, or None."""
    key = 'FileVersion'.encode('utf-16-le') + b'\0\0'
    at = data.find(key)
    while at >= 0:
        match = re.match(rb'(?:\0\0)*((?:[0-9.]\0){3,32})\0\0', data[at + len(key):at + len(key) + 80])
        if match:
            return match.group(1).decode('utf-16-le')
        at = data.find(key, at + 1)
    return None


def battlenet_location():
    """Read the game's uninstall registration, never Battle.net account data."""
    import winreg
    for hive in (winreg.HKEY_LOCAL_MACHINE, winreg.HKEY_CURRENT_USER):
        for base in (r'SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall',
                     r'SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall'):
            try:
                with winreg.OpenKey(hive, base + r'\Diablo II Resurrected') as handle:
                    path = Path(winreg.QueryValueEx(handle, 'InstallLocation')[0])
                if (path / 'D2R.exe').is_file():
                    return path.resolve()
            except OSError:
                pass
    return None
