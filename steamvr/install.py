#!/usr/bin/env python3
"""Local, reversible installation; does not change Steam's original executable."""
import argparse
import configparser
from datetime import datetime
import hashlib
import http.client
import io
import json
import os
from pathlib import Path
from pathlib import PurePosixPath
import shutil
import subprocess
import tempfile
import urllib.request
import zipfile
from prepare_icons import prepare as prepare_icons
from prepare_potions import prepare as prepare_potions, CODES as POTION_CODES, FILES as POTION_FILES
from prepare_spell_sprites import prepare as prepare_spell_sprites, FILE as SPELL_SPRITES
from prepare_props import prepare as prepare_props, FILES as PROP_FILES
import prepare_arcane_sky
import prepare_sewer_backdrop
import prepare_kurast_dock
from game_install import validate_game
import mod_identity

ROOT = Path(__file__).resolve().parent.parent
STEAM = Path.home() / '.local/share/Steam'
GAME = STEAM / 'steamapps/common/Diablo II Resurrected'
SAVES = STEAM / 'steamapps/compatdata/2536520/pfx/drive_c/users/steamuser/Saved Games/Diablo II Resurrected'
# Explicitly reviewed 1.3.1 archives from https://d2rloader.net/download.html.
# Upstream replaced the same URL; retain the historical pin for existing caches.
# Never accept a new digest automatically from the network.
LOADER_HASHES = (
    'f7bf0287398890b09bcc68b576327e727304bd88f7be80160f9676e8bd6871e2',
    '9286c6b5bff7f1043658411dbea305faa9698b6a455e6441668fc6d02c22aad7',
)
RESHADE_HASH = 'afe4c8f13048306307983b8b3d41d5bf00a86820440b0e57dea10950e1176445'
SDK_VERSION = '10.0.26100.9169'
SDK_HASH = '475269434dcd808a67853773272f972c3229c0e10c3ddc821290e70cc0f6904d'
COMPILER_HASH = '3bd6d7acfe8a0a84acd166fba1a537ce68bd21c8edce2bd9bb863cfa034136d4'
# OFXR Bridge by tig3rmast3r (LGPL-3.0-or-later), the experimental frame
# generation layer behind VR Settings > Perf > Frame generation. Fetched like
# D2RLoader and ReShade, never attached to our releases, and optional: without
# it the rest installs. Only its layer and licenses are installed, with a
# manifest and settings written here: no tray app and no registry entry, as
# the mod names the layer for its own process (steamvr/frame_generation.h).
OFXR_URL = 'https://github.com/tig3rmast3r/OFXR-Bridge/releases/download/0.2.1/OFXR-Bridge-v0.2.1-V116.zip'
OFXR_HASH = 'db224491cbc9553a51fe55d08623d25864c4ec0a3d49bd76e9d6c4d9b2410c67'
OFXR_ARCHIVE = 'OFXR-Bridge-v0.2.1-V116.zip'
OFXR_LAYER = 'XR_APILAYER_XRFrameBridge_diagnostic'
OFXR_BUILD = 116
OFXR_DIR = mod_identity.PLUGIN.parent / 'ofxr'
OFXR_SETTINGS = OFXR_DIR / 'ofxr_bridge.ini'
# The layer reads this beside its DLL. No control_event key: that is the tray's
# disarm switch, and without it the layer runs while the mod names it. The mod
# asks for its own backend at each start; the bridge's in-headset FPS number
# stays off.
OFXR_DEFAULT_SETTINGS = (b'[ofxr]\r\nenabled=1\r\nframe_generation=ofxr\r\nmotion_vectors=off\r\nbackend=fidelityfx\r\n'
                         b'nvidia_preset=medium\r\nnvidia_input_scale=50\r\nnvidia_bidirectional=0\r\n\r\n'
                         b'[diagnostics]\r\nlogging_enabled=0\r\nmax_file_mb=32\r\nflush_each_event=0\r\n\r\n'
                         b'[overlay]\r\nposition=off\r\n')
RECORD = mod_identity.RECORD


def check_launcher_icon(game, content):
    """A launcher can keep its card picture open without write sharing.

    Unchanged pictures need no write. If a changed picture is locked, stop
    before creating a snapshot or changing the game, with a useful retry step.
    Opening r+b checks access without truncating or modifying the picture.
    """
    icon = game / mod_identity.MOD_DIR / 'launcher_icon.png'
    try:
        if icon.is_file() and icon.read_bytes() != content:
            with icon.open('r+b'):
                pass
    except PermissionError as error:
        raise launcher_icon_write_error(icon, before_changes=True) from error


def launcher_icon_write_error(path, before_changes=False):
    # A launcher may reopen the picture after preflight. Setup's normal
    # rollback still handles any changes made before this late failure.
    message = (f'Cannot update the launcher icon: {path}. Close D2R VR Launcher '
               'and any image viewer using this file, then retry Update / repair.')
    return SystemExit(message + (' No game files changed.' if before_changes else ''))


def read_record(game):
    source = game / RECORD
    if not source.resolve().is_relative_to(game.resolve()):
        raise ValueError('Mod installation record points outside the game folder.')
    if not source.is_file():
        return {}
    record = json.loads(source.read_text(encoding='utf-8'))
    if Path(record['game']).resolve() != game.resolve():
        raise ValueError('Mod installation record belongs to a different game folder.')
    if (not isinstance(record.get('snapshots', []), list)
            or any(not isinstance(name, str) or not name for name in record.get('snapshots', []))
            or not isinstance(record.get('steam_profiles', {}), dict)
            or any(not isinstance(state, dict) for state in record.get('steam_profiles', {}).values())):
        raise ValueError('Invalid mod installation record.')
    return record


def write_record(game, record):
    target = game / RECORD
    if not target.resolve().is_relative_to(game.resolve()):
        raise ValueError('Mod installation record points outside the game folder.')
    target.parent.mkdir(parents=True, exist_ok=True)
    temporary = target.with_suffix('.tmp')
    try:
        temporary.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
        os.replace(temporary, target)
    finally:
        temporary.unlink(missing_ok=True)


def require_game_closed():
    if os.name == 'nt':
        import csv
        rows = csv.reader(subprocess.check_output(
            ['tasklist', '/FO', 'CSV', '/NH'], text=True, creationflags=0x08000000).splitlines())
        names = [row[0].lower() for row in rows if row]
    else:
        names = []
        for cmd in Path('/proc').glob('[0-9]*/cmdline'):
            try:
                first = cmd.read_bytes().split(b'\0', 1)[0].decode(errors='replace')
                names.append(first.replace('\\', '/').rsplit('/', 1)[-1].lower())
            except OSError:
                continue
    if any(name in ('d2r.exe', 'd2rloader.exe') for name in names):
        raise SystemExit('Save and exit D2R before installing or restoring the mod.')


def windows_read_error(path):
    """Recover Win32's reason when Python maps a Defender block to errno 22.

    Only opens an existing file for reading, then closes it. No security policy
    or file contents are changed. 225/226 are ERROR_VIRUS_INFECTED/DELETED.
    """
    if os.name != 'nt':
        return None
    import ctypes
    from ctypes import wintypes
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
        ctypes.c_void_p, wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
    kernel.CreateFileW.restype = wintypes.HANDLE
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    kernel.CloseHandle.restype = wintypes.BOOL
    handle = kernel.CreateFileW(str(path), 0x80000000, 7, None, 3, 0x80, None)
    if handle == ctypes.c_void_p(-1).value:
        return ctypes.get_last_error()
    kernel.CloseHandle(handle)
    return None


def dependency_io_error(path, action, error):
    code = getattr(error, 'winerror', None)
    if code is None and os.name == 'nt':
        try:
            code = windows_read_error(path)
        except OSError:
            pass  # Keep the original I/O diagnosis if probing also fails.
    if code in (225, 226):
        return SystemExit(
            '[PCVR_SECURITY_BLOCK] Windows Security interrupted setup. '
            '1) Open Windows Security > Virus & threat protection > Manage settings. '
            '2) Temporarily turn Real-time protection Off. '
            '3) Run this installer again and choose Update / repair. '
            '4) Turn Real-time protection back On immediately afterward, even if setup does not complete. '
            f'Details: required dependency "{path}", Windows error {code}. No game files changed.')
    return SystemExit(
        f'Could not {action} required dependency "{path}": {error}. '
        'The file may be missing, blocked/quarantined by security software, or inaccessible. '
        'Check file permissions and your security software\'s history. On Windows, check '
        'Windows Security > Virus & threat protection > Protection history; '
        'include any detection name and affected file path in your bug report. '
        'No game files changed.')


def read_dependency(path, missing_ok=False):
    try:
        return path.read_bytes()
    except FileNotFoundError as error:
        if missing_ok:
            return None
        raise dependency_io_error(path, 'read', error) from error
    except OSError as error:
        raise dependency_io_error(path, 'read', error) from error


def dependency_checksum_error(source, expected, actual):
    # Keep this on one line: the graphical setup surfaces the last child line.
    return SystemExit(
        f'Checksum mismatch for {source}. Expected SHA-256: {", ".join(expected)}; '
        f'received: {actual}. The download may have changed upstream or been corrupted. '
        'Use the latest D2R PCVR installer; if it still fails, report these hashes. '
        'No game files changed.')


def download_verified(url, path, digest, max_bytes=100 * 1024 * 1024):
    expected = (digest,) if isinstance(digest, str) else tuple(digest)
    cached = read_dependency(path, missing_ok=True)
    if cached is not None and hashlib.sha256(cached).hexdigest() in expected:
        print(f'Using verified cached dependency: {path.name}')
        return
    print(f'Downloading {url}')
    request = urllib.request.Request(url, headers={'User-Agent': 'D2R-PCVR-installer/0.1'})
    with urllib.request.urlopen(request, timeout=60) as response:
        data = response.read(max_bytes + 1)
    received = hashlib.sha256(data).hexdigest()
    if received not in expected:
        raise dependency_checksum_error(url, expected, received)
    try:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
    except OSError as error:
        raise dependency_io_error(path, 'save', error) from error
    # Security software can remove/block a downloaded file even after writing
    # succeeds. Verify disk access before fetching further dependencies.
    saved = hashlib.sha256(read_dependency(path)).hexdigest()
    if saved != received:
        raise dependency_checksum_error(f'saved dependency {path}', (received,), saved)


def frame_generation_manifest():
    """An explicit OpenXR layer manifest beside the layer: the mod adds its
    folder to XR_API_LAYER_PATH and names the layer in xrCreateInstance."""
    return (json.dumps({'file_format_version': '1.0.0', 'api_layer': {
        'name': OFXR_LAYER, 'library_path': f'./{OFXR_LAYER}.dll', 'api_version': '1.0',
        'implementation_version': str(OFXR_BUILD),
        'description': f'OFXR Bridge V{OFXR_BUILD}, enabled by D2R PCVR for its own process'}}, indent=2) + '\n').encode('utf-8')


def download_frame_generation(path):
    """Optional: a failed download (network, checksum or disk, the last two
    raised as SystemExit) leaves frame generation unavailable and installs the
    rest. Returns whether the pinned archive is in the cache."""
    try:
        download_verified(OFXR_URL, path, OFXR_HASH)
    except (OSError, SystemExit, http.client.HTTPException) as error:
        print(f'OFXR Bridge download failed; frame generation will be unavailable: {error}')
        return False
    return True


def frame_generation_files(archive):
    """OFXR Bridge's layer, licenses and manifest for OFXR_DIR; {} unless the
    pinned archive is in the dependency cache."""
    data = read_dependency(archive, missing_ok=True)
    if data is None:
        return {}
    digest = hashlib.sha256(data).hexdigest()
    if digest != OFXR_HASH:
        print(f'OFXR Bridge archive checksum mismatch ({digest}); frame generation is not installed.')
        return {}
    files = {}
    try:
        with zipfile.ZipFile(io.BytesIO(data)) as z:
            files[OFXR_DIR / f'{OFXR_LAYER}.dll'] = z.read(f'ofxr/{OFXR_LAYER}.dll')
            for name in z.namelist():
                if name in ('LICENSE', 'THIRD_PARTY.md') or name.startswith('licenses/') and not name.endswith('/'):
                    files[OFXR_DIR / safe_relative(name)] = z.read(name)
    except (zipfile.BadZipFile, KeyError) as error:
        print(f'OFXR Bridge archive unusable ({error}); frame generation is not installed.')
        return {}
    files[OFXR_DIR / f'{OFXR_LAYER}.json'] = frame_generation_manifest()
    return files


def safe_relative(name):
    # Apply Windows path rules even when preparing/installing on Linux.
    path = PurePosixPath(name.replace('\\', '/'))
    if path.is_absolute() or '..' in path.parts or ':' in str(path) or not path.parts:
        raise SystemExit('Unsafe path in loader archive or backup manifest')
    return Path(*path.parts)


def default_saves(game):
    if os.name == 'nt':
        return Path.home() / 'Saved Games/Diablo II Resurrected'
    # Steam creates the Proton prefix in the library containing the game.
    return game.parent.parent / 'compatdata/2536520/pfx/drive_c/users/steamuser/Saved Games/Diablo II Resurrected'


LOADER_CONFIG = Path('d2rloader/config/d2rloader.toml')


def migrate_reshade_paths(content, game):
    """Retain user values/comments and rebase paths when moving ReShade's base.

    Owned shaders/textures are copied into the mod. Keep the old search folder
    as a fallback for unrelated/custom files that are deliberately left alone.
    """
    encoding = 'utf-16' if content.startswith((b'\xff\xfe', b'\xfe\xff')) else 'utf-8-sig'
    text = content.decode(encoding)
    general = False
    lines = []
    for line in text.splitlines(keepends=True):
        stripped = line.strip()
        if stripped.startswith('['):
            general = stripped.lower() == '[general]'
        key, separator, value = line.partition('=')
        kind = key.strip().lower()
        if general and separator and kind in ('effectsearchpaths', 'texturesearchpaths', 'presetpath'):
            paths = []
            for raw in value.strip().split(','):
                if not raw.strip():
                    continue
                p = Path(raw.strip().replace('\\', '/'))
                old = p if p.is_absolute() else game / p
                old = old.resolve()
                relative = old.relative_to(game) if old.is_relative_to(game) else None
                mapped = relative is not None and bool(relative.parts) and (relative.parts[0].lower() == 'reshade-shaders'
                    or relative.as_posix().lower() == 'd2r_frame_reshade.ini')
                paths.append(str(game / mod_identity.RESHADE_DIR / relative) if mapped else str(old))
                if mapped and kind != 'presetpath':
                    paths.append(str(old))
            ending = '\r\n' if line.endswith('\r\n') else '\n' if line.endswith('\n') else ''
            line = key + '=' + ','.join(dict.fromkeys(paths)) + ending
        lines.append(line)
    return ''.join(lines).encode(encoding)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--game', type=Path, default=GAME)
    ap.add_argument('--deps', type=Path, default=Path.home() / 'vrdeps')
    ap.add_argument('--saves', type=Path, help='Saved Games folder (for custom prefixes/redirected Windows folders)')
    ap.add_argument('--plugin', type=Path, default=ROOT / 'build-frame/windows-native/d2rl-pcvr.dll')
    ap.add_argument('--openxr-loader', type=Path, help='Windows x64 openxr_loader.dll; required on native Windows')
    ap.add_argument('--backup-dir', type=Path, default=ROOT / 'build-frame/backups')
    ap.add_argument('--download-deps', action='store_true', help='Fetch pinned D2RLoader, ReShade and (Windows) OFXR Bridge from their official sites')
    ap.add_argument('--store', choices=['auto', 'steam', 'battlenet'], default='auto')
    ap.add_argument('--experimental-battlenet', action='store_true', help='Enable Battle.net 3.3.93847 for local offline compatibility testing')
    ap.add_argument('--casclib', type=Path, default=ROOT / 'bin/CascLib.dll', help='Pinned Unicode x64 CascLib 3.0 for Battle.net asset reading')
    ap.add_argument('--preserve-settings', action='store_true', help='Keep existing calibration, mod/ReShade configuration and video settings when upgrading')
    ap.add_argument('--restore', type=Path, help='Restore the installation snapshot made by this script')
    args = ap.parse_args()
    game = args.game.resolve()
    require_game_closed()
    saves = (args.saves or default_saves(game)).resolve()
    if args.restore:
        snapshot = args.restore.resolve()
        m = json.loads((snapshot / 'manifest.json').read_text())
        if str(game) != m['game']:
            raise SystemExit('Snapshot belongs to a different game folder')
        # D2R VR's d2rl-vrcam.dll / d2r_vr.ini, installed after this snapshot: never touched.
        keep = ({str(name).lower() for name in mod_identity.LEGACY_FILES}
                if mod_identity.plugin_owner(game / mod_identity.LEGACY_PLUGIN) == mod_identity.OTHER_MOD else set())
        shared = bool(mod_identity.other_loader_users(game))
        for rel in m['installed']:
            rel = safe_relative(rel)
            if str(rel).lower() in keep or shared and mod_identity.shared_dependency(rel):
                continue
            target = game / rel
            old = snapshot / 'previous' / rel
            if old.exists():
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(old, target)
            elif target.is_file():
                target.unlink()
        saves = Path(m.get('saves', str(saves)))
        saves.mkdir(parents=True, exist_ok=True)
        shutil.copy2(snapshot / 'saves/Settings.json', saves / 'Settings.json')
        print('Mod files and video settings restored. Character progress was kept.')
        return
    try:
        store, version = validate_game(game, args.store, args.experimental_battlenet)
    except ValueError as error:
        raise SystemExit(str(error)) from error
    if store == 'battlenet':
        if os.name != 'nt':
            raise SystemExit('Experimental Battle.net support currently requires native Windows.')
        if not args.casclib.is_file():
            raise SystemExit('Battle.net installation requires the packaged Unicode x64 CascLib.dll.')
        print('Experimental Battle.net build: use a local OFFLINE character for compatibility testing.')
    if not args.plugin.is_file():
        raise SystemExit(f'Mod DLL not found: {args.plugin}. Extract the complete release ZIP and follow INSTALL-WINDOWS.md.')
    if os.name == 'nt' and not args.openxr_loader:
        raise SystemExit('Native Windows requires --openxr-loader pointing to the release bin/openxr_loader.dll')
    if args.openxr_loader and not args.openxr_loader.is_file():
        raise SystemExit('OpenXR loader DLL was not found')
    if not (saves / 'Settings.json').is_file():
        raise SystemExit(f'Launch stock D2R once first, or pass --saves; Settings.json not found in {saves}')
    record = read_record(game)
    # Another VR mod's plugin in this folder (D2R VR's d2rl-vrcam.dll): leave it
    # alone and stop, as D2RLoader would load both. An earlier D2R PCVR under the
    # old names is carried over below (steamvr/mod_identity.py).
    found = mod_identity.survey(game, [args.backup_dir])
    if found.other:
        raise SystemExit(found.message + ' No game files changed.')
    legacy_effects = (mod_identity.legacy_effect_files(game, [args.backup_dir])
                      if found.legacy or found.moved else [])
    effect_configs = {}
    if args.preserve_settings:
        for rel in legacy_effects:
            if rel.as_posix().lower() in ('reshade.ini', 'd2r_frame_reshade.ini'):
                content = (game / rel).read_bytes()
                if rel.name.lower() == 'reshade.ini':
                    content = migrate_reshade_paths(content, game)
                effect_configs[mod_identity.RESHADE_DIR / rel.name] = content
    # Read and validate all inputs before changing any game files.
    settings = json.loads((saves / 'Settings.json').read_text(encoding='utf-8-sig'))
    archive = args.deps / 'D2RLoader-1.3.1-beta.zip'
    reshade = args.deps / 'reshade-6.8/ReShade64.dll'
    compiler = args.deps / 'd3dcompiler47/modern/d3dcompiler_47.dll'
    ofxr = args.deps / OFXR_ARCHIVE
    if args.download_deps:
        download_verified('https://d2rloader.net/downloads/D2RLoader-1.3.1-beta.zip', archive, LOADER_HASHES)
        setup = args.deps / 'ReShade_Setup_6.8.0_Addon.exe'
        download_verified('https://reshade.me/downloads/ReShade_Setup_6.8.0_Addon.exe', setup, RESHADE_HASH)
        # Official setup executable contains an appended ZIP. Extract the DLL
        # locally; do not run setup or create an automatically loaded DXGI proxy.
        with zipfile.ZipFile(setup) as z:
            reshade.parent.mkdir(parents=True, exist_ok=True)
            reshade.write_bytes(z.read('ReShade64.dll'))
        if os.name == 'nt':
            # Frame generation needs the Windows OpenXR loader (Proton has none).
            download_frame_generation(ofxr)
        if os.name != 'nt':
            # Wine's compiler stayed busy throughout multi-minute tests of the
            # effect. Use Microsoft's current SDK redist privately under the
            # mod; the Windows 8.1 Winetricks version lacks shader model 5.1.
            sdk = args.deps / f'd3dcompiler47/microsoft.windows.sdk.cpp.{SDK_VERSION}.nupkg'
            download_verified(f'https://api.nuget.org/v3-flatcontainer/microsoft.windows.sdk.cpp/{SDK_VERSION}/{sdk.name}',
                              sdk, SDK_HASH, max_bytes=200 * 1024 * 1024)
            with zipfile.ZipFile(sdk) as z:
                compiler.parent.mkdir(parents=True, exist_ok=True)
                compiler.write_bytes(z.read('c/Redist/D3D/x64/d3dcompiler_47.dll'))
    if os.name != 'nt' and not compiler.is_file():
        raise SystemExit('Proton needs the SDK shader compiler; run with --download-deps.')
    if compiler.is_file() and hashlib.sha256(read_dependency(compiler)).hexdigest() != COMPILER_HASH:
        raise SystemExit('SDK shader compiler checksum mismatch; run with --download-deps.')
    loader_digest = hashlib.sha256(read_dependency(archive)).hexdigest()
    if loader_digest not in LOADER_HASHES:
        raise dependency_checksum_error(f'D2RLoader archive {archive}', LOADER_HASHES, loader_digest)
    payload = {}
    shared_loader = bool(mod_identity.other_loader_users(game))
    try:
        with zipfile.ZipFile(archive) as z:
            for name in z.namelist():
                if not name.endswith('/'):
                    rel = safe_relative(name)
                    if rel.parts[0].lower() not in ('mods', 'd2rloader', 'd2rloader.exe', 'd2rcore.dll', 'd2rloader.mpq'):
                        raise SystemExit(f'Unexpected file in loader archive: {name}')
                    # D2RLoader's settings (default_mod, extensions) are the player's and other mods' too: an existing
                    # d2rloader.toml stays as it is.
                    if rel == LOADER_CONFIG and (game / rel).is_file():
                        continue
                    data = z.read(name)
                    if shared_loader and mod_identity.loader_file(rel) and (game / rel).is_file():
                        if rel.as_posix().lower() in mod_identity.LOADER_NAMES and (game / rel).read_bytes() != data:
                            raise SystemExit('Another mod uses a different installed D2RLoader. '
                                             'Select a compatible shared loader before updating PCVR. No game files changed.')
                        # Preserve the other mod's existing shared configuration/resources.
                        continue
                    payload[rel] = data
    except OSError as error:
        raise dependency_io_error(archive, 'read', error) from error
    payload[mod_identity.RESHADE] = read_dependency(reshade)
    payload[mod_identity.PLUGIN] = args.plugin.read_bytes()
    if compiler.is_file():
        payload[Path('d2rloader/runtime/d3dcompiler_47.dll')] = compiler.read_bytes()
    if args.openxr_loader:
        payload[mod_identity.OPENXR_LOADER] = args.openxr_loader.read_bytes()
    frame_generation = frame_generation_files(ofxr)
    if frame_generation:
        payload.update(frame_generation)
        print('Prepared OFXR Bridge 0.2.1 for VR Settings > Perf > Frame generation (experimental, off by default).')
    # The launcher's descriptor of this mod (the D2R VR Launcher lists mods\*\vrmod-manifest.json). It is journaled like
    # every other file, so a restore or removal takes it away with the mod.
    payload[mod_identity.MOD_DIR / 'vrmod-manifest.json'] = (ROOT / 'vrmod-manifest.json').read_bytes()
    payload[mod_identity.MOD_DIR / 'launcher_icon.png'] = (ROOT / 'launcher_icon.png').read_bytes()
    # Native artwork is read locally, never downloaded/bundled with the mod.
    # Prepare before mutating the install and journal it with all other files.
    with tempfile.TemporaryDirectory(prefix='d2r-vr-icons-') as temporary:
        try:
            result = prepare_icons(game, temporary, casclib=args.casclib)
        except (OSError, ValueError, KeyError, IndexError) as error:
            print(f'Native icon cache unavailable; wheel labels still work: {error}')
        else:
            for name in result['images'] + ['manifest.tsv', 'cache.json']:
                payload[mod_identity.PLUGIN.parent / 'd2r_vr_icons' / safe_relative(name)] = (Path(temporary) / name).read_bytes()
            print(f"Prepared {result['icons']} native icon bindings from your installed game.")
    with tempfile.TemporaryDirectory(prefix='d2r-vr-potions-') as temporary:
        try:
            result = prepare_potions(game, temporary, casclib=args.casclib)
        except (OSError, ValueError, KeyError, IndexError) as error:
            print(f'Native potion models unavailable; retaining icon fallback: {error}')
        else:
            for name in list(POTION_FILES) + ['cache.json']:
                payload[mod_identity.PLUGIN.parent / 'd2r_vr_potions' / safe_relative(name)] = (Path(temporary) / name).read_bytes()
            print(f"Prepared {result['models']} original 3D potion models from your installed game.")
    with tempfile.TemporaryDirectory(prefix='d2r-vr-props-') as temporary:
        try:
            result = prepare_props(game, temporary, casclib=args.casclib)
        except (OSError, ValueError, KeyError, IndexError) as error:
            print(f'Knockable prop catalog unavailable; level props stay where they are: {error}')
        else:
            for name in list(PROP_FILES) + ['cache.json']:
                payload[mod_identity.PLUGIN.parent / 'd2r_vr_props' / safe_relative(name)] = (Path(temporary) / name).read_bytes()
            print(f"Catalogued {result['props']} knockable props from your installed game's levels.")
    with tempfile.TemporaryDirectory(prefix='d2r-vr-spell-sprites-') as temporary:
        try:
            result = prepare_spell_sprites(game, temporary, casclib=args.casclib)
        except (OSError, ValueError, KeyError, IndexError) as error:
            print(f'Spell effect textures unavailable; elemental hands keep their procedural aura: {error}')
        else:
            for name in (SPELL_SPRITES, 'cache.json'):
                payload[mod_identity.PLUGIN.parent / 'd2r_vr_spell_sprites' / safe_relative(name)] = (Path(temporary) / name).read_bytes()
            print(f"Prepared {result['textures']} spell effect textures from your installed game.")
    try:
        arcane = prepare_arcane_sky.prepare(game, casclib=args.casclib)
        prepare_arcane_sky.check_existing(game / mod_identity.MOD_DIR, arcane)
    except (OSError, ValueError, KeyError, IndexError) as error:
        print(f'Arcane sky preparation unavailable; existing background retained: {error}')
    else:
        payload.update({mod_identity.MOD_DIR / name: data for name, data in arcane.items()})
        print('Prepared surrounding Arcane Sanctuary sky from your installed game.')
    try:
        sewers = prepare_sewer_backdrop.prepare(game, casclib=args.casclib)
        prepare_sewer_backdrop.check_existing(game / mod_identity.MOD_DIR, sewers)
    except (OSError, ValueError, KeyError, IndexError) as error:
        print(f'Kurast sewer backdrop preparation unavailable; existing surfaces retained: {error}')
    else:
        payload.update({mod_identity.MOD_DIR / name: data for name, data in sewers.items()})
        print('Prepared dark Kurast sewer entrances from your installed game.')
    try:
        docktown = prepare_kurast_dock.prepare(game, casclib=args.casclib)
        prepare_kurast_dock.check_existing(game / mod_identity.MOD_DIR, docktown)
    except (OSError, ValueError, KeyError, IndexError) as error:
        print(f'Kurast Docks flying canoe fix unavailable; existing preset retained: {error}')
    else:
        payload.update({mod_identity.MOD_DIR / name: data for name, data in docktown.items()})
        print('Prepared Kurast Docks preset (removed flying canoe) from your installed game.')
    require_game_closed()
    check_launcher_icon(game, payload[mod_identity.MOD_DIR / 'launcher_icon.png'])
    snapshot = args.backup_dir / datetime.now().strftime('%Y%m%d-%H%M%S-%f')
    snapshot.mkdir(parents=True)
    shutil.copytree(saves, snapshot / 'saves')
    installed = []
    manifest = {'game': str(game), 'store': store, 'game_version': version,
                'mod': mod_identity.MOD_ID, 'previous_mod': found.ours,
                'saves': str(saves), 'installed': installed}
    (snapshot / 'manifest.json').write_text(json.dumps(manifest, indent=2))

    journaled = set()

    def journal(rel):
        key = str(rel).casefold()
        if key in journaled:
            return
        dest = game / rel
        if dest.exists():
            old = snapshot / 'previous' / rel
            old.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(dest, old)
        installed.append(str(rel))
        # Journal before replacing so an interrupted install can be restored.
        (snapshot / 'manifest.json').write_text(json.dumps(manifest, indent=2))
        journaled.add(key)

    def put(rel, content):
        journal(rel)
        dest = game / rel
        # Still journal unchanged files so rollback and removal retain the
        # complete ownership chain. Avoid write access (and file locks) when
        # the bytes already match, notably the launcher's open card icon.
        if dest.is_file() and dest.read_bytes() == content:
            return
        dest.parent.mkdir(parents=True, exist_ok=True)
        try:
            dest.write_bytes(content)
        except PermissionError as error:
            if rel == mod_identity.MOD_DIR / 'launcher_icon.png':
                raise launcher_icon_write_error(dest) from error
            raise

    def remove(rel):
        """Journaled removal: the snapshot keeps the file, so --restore and a
        failed setup put it back, and removal restores the pre-mod state."""
        dest = game / rel
        if not dest.is_file():
            return
        journal(rel)
        manifest.setdefault('removed', []).append(str(rel))
        (snapshot / 'manifest.json').write_text(json.dumps(manifest, indent=2))
        dest.unlink()

    record.update({'game': str(game), 'store': store, 'saves': str(saves),
                   'snapshots': record.get('snapshots', []) + [str(snapshot.resolve())]})
    # Track the chain before other writes, including interrupted installs.
    put(RECORD, (json.dumps(record, indent=2) + '\n').encode('utf-8'))
    if found.legacy:
        # Up to 0.3.16 D2R PCVR was d2rl-vrcam.dll with d2r_vr.ini (D2R VR's names).
        # Update / repair keeps the player's settings and calibration under the new
        # name; then the old files go, before the new plugin is written, so D2RLoader
        # never sees two copies of this mod.
        legacy_settings = game / mod_identity.LEGACY_SETTINGS
        if args.preserve_settings and legacy_settings.is_file() and not (game / mod_identity.SETTINGS).is_file():
            put(mod_identity.SETTINGS, legacy_settings.read_bytes())
            print('Carried your settings and body calibration over from d2r_vr.ini to d2r_pcvr.ini.')
        for rel in mod_identity.LEGACY_FILES:
            remove(rel)
        print('Removed the old D2R PCVR plugin files (d2rl-vrcam.dll, d2r_vr.ini); '
              'D2R PCVR is now d2rl-pcvr.dll with d2r_pcvr.ini. They are kept in the backup.')
    if found.moved:
        # D2R PCVR's plugin in D2RLoader's global plugins folder would load with every mod (D2R VR's too): it moves
        # into the mod folder, the settings and calibration with it.
        global_settings = game / mod_identity.GLOBAL_SETTINGS
        if args.preserve_settings and global_settings.is_file() and not (game / mod_identity.SETTINGS).is_file():
            put(mod_identity.SETTINGS, global_settings.read_bytes())
            print(f'Carried your settings and body calibration over into {mod_identity.SETTINGS}.')
        for rel in mod_identity.GLOBAL_FILES:
            remove(rel)
        print(f'D2R PCVR is now the D2RLoader mod {mod_identity.MOD_NAME} ({mod_identity.MOD_DIR}); '
              'its files left the global plugins folder. They are kept in the backup.')
    # Copy only our recorded legacy files. Existing mod-folder settings win on
    # repair; shared roots stay intact while another mod uses this loader.
    for dest, content in effect_configs.items():
        if not (game / dest).exists():
            put(dest, content)
    shared = bool(mod_identity.other_loader_users(game))
    for rel in legacy_effects:
        dest = mod_identity.RESHADE_DIR / rel
        if rel.name.lower() not in ('reshade64.dll', 'reshade.ini', 'd2r_frame_reshade.ini'):
            if not (game / dest).exists():
                put(dest, (game / rel).read_bytes())
        if not shared:
            remove(rel)
    for rel, content in payload.items():
        put(rel, content)
    cfg = configparser.ConfigParser(interpolation=None, strict=False, allow_no_value=True)
    # The introductory comment also mentions [camera]. Splitting that comment
    # produced a malformed first camera section which Win32 profile reads used
    # instead of the real one, silently restoring the large default near plane.
    lines = (ROOT / 'd2r_pcvr.ini').read_text(encoding='utf-8').splitlines()
    start = next(i for i, line in enumerate(lines) if line.strip() == '[camera]')
    cfg.read_string('\n'.join(lines[start:]))
    changes = {
        'camera': {'fov_from_flatvr': '0', 'neck_model': '0', 'near': '0.2'},
        'stereo': {'afr': '1', 'pair_per_tick': '1', 'true_scale': '0', 'convergence': '0', 'swap': '0'},
        'turn': {'snap': '30'},
        'mode': {'platform': '1', 'vr_mode': '4', 'vr_mode_order': '2'},
        'hud': {'hide': '0', 'map': '0', 'stereo_labels': '1', 'monster_draw_order': '1'},
        'update': {'check': '0'},
        'gameplay': {'motion_controls': '1', 'arrow_speed': '2', 'arrow_direct_release': '1', 'contact_hits': '1'},
        'render': {'game_height_fog': '1', 'fixed_foveation': '1', 'foveation_inner': '0.55',
                   'symmetric_projection': '1', 'defer_gpu_wait': '1',
                   'eye_tracked_foveation': '0', 'gaze_foveation_inner': '0.35'},
    }
    for section, values in changes.items():
        if not cfg.has_section(section):
            cfg.add_section(section)
        for key, value in values.items():
            cfg.set(section, key, value)
    import io
    out = io.StringIO(); cfg.write(out)
    def put_config(rel, content):
        if not args.preserve_settings or not (game / rel).is_file():
            put(rel, content)

    put_config(mod_identity.SETTINGS, out.getvalue().encode())
    put(mod_identity.MODINFO, mod_identity.modinfo_bytes())
    # ReShade's paths are relative to its base, the mod's reshade\ folder (the plugin sets it).
    put_config(mod_identity.RESHADE_DIR / 'ReShade.ini', b'[GENERAL]\nEffectSearchPaths=.\\reshade-shaders\\Shaders\nTextureSearchPaths=.\\reshade-shaders\\Textures\nPresetPath=.\\D2R_Frame_ReShade.ini\n[INPUT]\nKeyOverlay=36,0,0,0\n')
    put_config(mod_identity.RESHADE_DIR / 'D2R_Frame_ReShade.ini', b'Techniques=D2R_DepthFog@D2R_DepthFog.fx\n')
    if frame_generation:
        put_config(OFXR_SETTINGS, OFXR_DEFAULT_SETTINGS)
    put(mod_identity.RESHADE_DIR / 'reshade-shaders/Shaders/D2R_DepthFog.fx', (ROOT / 'reshade/D2R_DepthFog.fx').read_bytes())
    for texture in (ROOT / 'reshade/sky').glob('*.png'):
        put(mod_identity.RESHADE_DIR / 'D2R_Sky_ours' / texture.name, texture.read_bytes())
        put(mod_identity.RESHADE_DIR / 'reshade-shaders/Textures/D2R_Sky_ours' / texture.name, texture.read_bytes())
    settings.update({'VSync': 0, 'NVIDIA DLSS': 0, 'Anti Aliasing': 1, 'Framerate Cap': 0,
                     'Dynamic Resolution Scaling': 0, 'Camera Shake': 0,
                     'Resolution Scale': 100, 'Ambient Occlusion Quality': 2,
                     'Atmospherics Quality': 2, 'Shadow Quality': 2})
    if not args.preserve_settings:
        (saves / 'Settings.json').write_text(json.dumps(settings, indent=4))
    print(f'Installed offline VR mod. Save and file backup: {snapshot}')


if __name__ == '__main__':
    main()
