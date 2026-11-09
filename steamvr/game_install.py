"""Identify local game storage before installing any files.

No game build or version is refused here. A game update does not by itself
break D2RLoader or this plugin: the plugin checks the game code it hooks by its
bytes when the game starts (cleanroom/sigscan) and leaves a feature off, with a
log line, when they differ. The version found is only recorded, for bug reports.
"""
from pathlib import Path
import re


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
    exe = game / 'D2R.exe'
    if not exe.is_file():
        raise ValueError(f'D2R.exe was not found in {game}. Select the folder that contains it.')
    if detected == 'battlenet' and not experimental_battlenet:
        raise ValueError('Battle.net support is experimental. Use --experimental-battlenet for local offline compatibility testing.')
    return detected, file_version(exe.read_bytes()) or 'unknown'


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
