"""Validate and stage a native release plus the pinned private Python runtime."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import uuid
import zipfile

PYTHON_SHA256 = '4acbed6dd1c744b0376e3b1cf57ce906f9dc9e95e68824584c8099a63025a3c3'
# The D2R VR Launcher's own Setup, carried unmodified (https://github.com/natpoh/d2r-vr-launcher/releases): this one name only.
LAUNCHER_SETUP = re.compile(r'D2R_VR_Launcher_Setup_(\d+(?:\.\d+){1,3})\.exe')
ROOT_FILES = {
    'INSTALL.md', 'INSTALL-WINDOWS.md', 'README.md', 'CONTROLS.md', 'RELEASE_NOTES.md',
    'Launch-VR-SteamVR.cmd', 'Launch-VR-VDXR.cmd', 'Launch-Dev-SteamVR.cmd', 'Install-Windows.cmd',
    'steamvr/DEVELOPMENT_SCENARIOS.md',
    'Coop-Setup.cmd', 'docs/MULTIPLAYER.md',
    'Install-BattleNet-Preview.cmd', 'Uninstall-Windows.cmd', 'THIRD_PARTY.md', 'LICENSE', 'd2r_pcvr.ini',
    'vrmod-manifest.json', 'launcher_icon.png',
    'reshade/D2R_DepthFog.fx', 'third_party/minhook/LICENSE.txt', 'external/reshade/LICENSE.md',
    'bin/d2rl-pcvr.dll', 'bin/openxr_loader.dll', 'bin/CascLib.dll', 'steamvr/vendor/bitknit.py',
}
SCRIPTS = {'setup_windows.ps1', 'setup_windows.py', 'setup_native.py', 'uninstall_windows.py', 'hdr_check.py',
           'steam_config.py', 'install.py', 'mod_identity.py', 'launch_windows.py', 'prepare_icons.py',
           'steam_assets.py', 'game_assets.py', 'game_install.py', 'casc_assets.py',
           'prepare_potions.py', 'potion_assets.py', 'prepare_spell_sprites.py', 'prepare_arcane_sky.py', 'prepare_dev_scenarios.py',
           'prepare_sewer_backdrop.py', 'prepare_kurast_dock.py', 'prepare_props.py',
           'coop_assistant.py', 'coop_setup.py', 'coop_connection.py', 'coop_wizard.py'}


def safe_name(name):
    path = PurePosixPath(name)
    if not path.parts or path.is_absolute() or '..' in path.parts or ':' in name or '\\' in name:
        raise ValueError(f'Unsafe archive path: {name}')
    return path


def allowed(name, source):
    path = safe_name(name)
    return (name in ROOT_FILES or name in {'steamvr/' + item for item in SCRIPTS}
            or (len(path.parts) == 3 and path.parts[:2] in (('steamvr', 'licenses'), ('reshade', 'sky'))
                and path.suffix == ('.txt' if path.parts[0] == 'steamvr' else '.png')
                and (source / name).is_file()))


def read_launcher_setup(path, sha256):
    """The launcher's Setup as published (its name, its SHA-256 pinned by the build), or an error before anything is staged."""
    match = LAUNCHER_SETUP.fullmatch(Path(path).name)
    if not match or not re.fullmatch(r'[0-9a-f]{64}', sha256 or ''):
        raise ValueError('The launcher Setup must keep its published name, D2R_VR_Launcher_Setup_<version>.exe, and come with its SHA-256.')
    data = Path(path).read_bytes()
    if hashlib.sha256(data).hexdigest() != sha256:
        raise ValueError('Launcher Setup checksum mismatch.')
    return Path(path).name, match.group(1), data


def stage(package, python_archive, destination, source, expected_version=None, launcher_setup=None, launcher_sha256=None):
    if hashlib.sha256(python_archive.read_bytes()).hexdigest() != PYTHON_SHA256:
        raise ValueError('Private Python archive checksum mismatch.')
    launcher = read_launcher_setup(launcher_setup, launcher_sha256) if launcher_setup is not None else None
    if destination.exists():
        raise ValueError('Use a new staging directory; existing files are never recursively removed.')
    with zipfile.ZipFile(package) as archive:
        names = [item.filename for item in archive.infolist() if not item.is_dir()]
        if len({name.casefold() for name in names}) != len(names):
            raise ValueError('Package contains duplicate or case-colliding file names.')
        if sum(item.file_size for item in archive.infolist()) > 250 * 1024 * 1024:
            raise ValueError('Package exceeds the installer payload limit.')
        prefix = 'd2r-pcvr-preview/'
        if any(not name.startswith(prefix) for name in names):
            raise ValueError('Package has an unexpected root folder.')
        manifest = json.loads(archive.read(prefix + 'release-manifest.json'))
        if manifest.get('platform') != 'native-windows':
            raise ValueError('The graphical installer requires a native Windows package.')
        if expected_version is not None and manifest.get('release_tag') != f'v{expected_version}-windows-preview':
            raise ValueError('Installer version does not match the package release tag.')
        hashes = manifest['sha256']
        if not {'steamvr/setup_native.py', 'steamvr/mod_identity.py', 'bin/d2rl-pcvr.dll', 'bin/openxr_loader.dll'} <= set(hashes):
            raise ValueError('Repackage the current source; the native setup backend is missing.')
        if set(names) != {prefix + name for name in hashes} | {prefix + 'release-manifest.json'}:
            raise ValueError('Package contains files not covered by its release manifest.')
        for name, digest in hashes.items():
            if not allowed(name, source):
                raise ValueError(f'File is outside the distributable installer allowlist: {name}')
            payload = archive.read(prefix + name)
            if hashlib.sha256(payload).hexdigest() != digest:
                raise ValueError(f'Package file checksum mismatch: {name}')
        destination.mkdir(parents=True)
        for name in hashes:
            target = destination / safe_name(name)
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(archive.read(prefix + name))
    runtime = destination / '.runtime/python'
    runtime.mkdir(parents=True)
    with zipfile.ZipFile(python_archive) as archive:
        for info in archive.infolist():
            if info.is_dir():
                continue
            name = safe_name(info.filename)
            target = runtime / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(archive.read(info))
            hashes[target.relative_to(destination).as_posix()] = hashlib.sha256(target.read_bytes()).hexdigest()
    # This local import-path file is regenerated by the portable CMD bootstrap.
    # The official runtime binaries, stdlib archive and license remain hashed.
    (runtime / 'python312._pth').write_text('python312.zip\n.\n../../steamvr\n../..\n', encoding='utf-8')
    hashes.pop('.runtime/python/python312._pth', None)
    if launcher is not None:
        name, version, data = launcher
        (destination / 'launcher').mkdir()
        (destination / 'launcher' / name).write_bytes(data)
        hashes['launcher/' + name] = launcher_sha256
        manifest['carried_launcher'] = {'file': 'launcher/' + name, 'version': version, 'sha256': launcher_sha256}
    manifest['distribution'] = 'graphical-windows-installer'
    manifest['private_python'] = {'version': '3.12.10', 'archive_sha256': PYTHON_SHA256}
    (destination / 'release-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--package', type=Path, required=True)
    parser.add_argument('--python-archive', type=Path, required=True)
    parser.add_argument('--staging-parent', type=Path, required=True)
    parser.add_argument('--version', help='Require this installer version to match the packaged release tag')
    parser.add_argument('--launcher-setup', type=Path, help='Carry the D2R VR Launcher Setup (D2R_VR_Launcher_Setup_<version>.exe) unmodified')
    parser.add_argument('--launcher-sha256', help='The pinned SHA-256 of that file (required with --launcher-setup)')
    args = parser.parse_args()
    destination = args.staging_parent.resolve() / ('payload-' + uuid.uuid4().hex)
    manifest = stage(args.package, args.python_archive, destination, Path(__file__).resolve().parent.parent, args.version,
                     args.launcher_setup, args.launcher_sha256)
    print(json.dumps({'payload': str(destination), 'source_commit': manifest['source_commit'],
                      'plugin_source_commit': manifest.get('plugin_source_commit'),
                      'plugin_sha256': manifest['sha256']['bin/d2rl-pcvr.dll'],
                      'carried_launcher': manifest.get('carried_launcher')}))


if __name__ == '__main__':
    main()
