#!/usr/bin/env python3
"""Package only distributable preview files; exclude local logs, saves and dumps."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parent.parent


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--build', type=Path, default=ROOT / 'build-frame')
    parser.add_argument('--openxr-loader', type=Path, required=True)
    parser.add_argument('--casclib', type=Path, help='Include pinned Unicode x64 CascLib 3.0 for experimental Battle.net testing')
    parser.add_argument('--output', type=Path, default=ROOT / 'build-frame/releases/d2r-pcvr-preview.zip')
    parser.add_argument('--variant', choices=['current-source', 'linux-baseline'], default='current-source')
    parser.add_argument('--plugin-source-note', help='Required provenance for an archived baseline DLL')
    parser.add_argument('--plugin-source-commit', help='Original binary source when repackaging documentation without rebuilding')
    parser.add_argument('--release-tag', help='Published release tag recorded in the manifest')
    parser.add_argument('--platform', choices=['native-windows', 'linux-proton'], default='native-windows')
    args = parser.parse_args()
    if args.variant == 'linux-baseline' and not args.plugin_source_note:
        parser.error('--variant linux-baseline requires --plugin-source-note')
    if subprocess.check_output(['git', 'status', '--porcelain'], cwd=ROOT).strip():
        raise SystemExit('Commit source before packaging a release')
    commit = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    plugin_commit = subprocess.check_output(
        ['git', 'rev-parse', '--verify', (args.plugin_source_commit or commit) + '^{commit}'],
        cwd=ROOT, text=True).strip() if args.variant == 'current-source' else None
    # Explicit allowlist. Never glob an install, backup, build or diagnostic tree.
    paths = ['INSTALL.md', 'INSTALL-WINDOWS.md', 'README.md', 'CONTROLS.md', 'RELEASE_NOTES.md',
             'Launch-VR-SteamVR.cmd', 'Launch-VR-VDXR.cmd',
             'Install-Windows.cmd', 'Uninstall-Windows.cmd', 'steamvr/setup_windows.ps1',
             'steamvr/uninstall_windows.py',
             'steamvr/setup_windows.py', 'steamvr/steam_config.py', 'steamvr/hdr_check.py',
             'THIRD_PARTY.md', 'LICENSE', 'd2r_pcvr.ini',
             'reshade/D2R_DepthFog.fx', 'third_party/minhook/LICENSE.txt',
             'external/reshade/LICENSE.md', 'steamvr/install.py', 'steamvr/mod_identity.py',
             'steamvr/launch_windows.py']
    if args.platform == 'linux-proton':
        paths += ['INSTALL-LINUX.md', 'steamvr/launch.sh', 'steamvr/run-proton.sh', 'steamvr/session.py']
    else:
        paths += ['steamvr/setup_native.py']
    paths += ['steamvr/prepare_icons.py', 'steamvr/steam_assets.py', 'steamvr/game_assets.py',
              'steamvr/game_install.py', 'steamvr/casc_assets.py']
    paths += ['steamvr/prepare_potions.py', 'steamvr/potion_assets.py', 'steamvr/vendor/bitknit.py',
              'steamvr/prepare_arcane_sky.py']
    files = {path: ROOT / path for path in paths}
    for folder, pattern in [('reshade/sky', '*.png'), ('steamvr/licenses', '*.txt')]:
        for path in sorted((ROOT / folder).glob(pattern)):
            files[path.relative_to(ROOT).as_posix()] = path
    files['bin/d2rl-pcvr.dll'] = args.build / 'd2rl-pcvr.dll'
    if args.platform == 'linux-proton':
        files['bin/openxr-probe.exe'] = args.build / 'openxr-probe.exe'
    files['bin/openxr_loader.dll'] = args.openxr_loader
    if args.casclib:
        if args.platform != 'native-windows':
            raise SystemExit('Experimental Battle.net packaging currently requires native Windows')
        files['bin/CascLib.dll'] = args.casclib
        files['Install-BattleNet-Preview.cmd'] = ROOT / 'Install-BattleNet-Preview.cmd'
    hashes = {name: hashlib.sha256(path.read_bytes()).hexdigest() for name, path in files.items()}
    manifest = {'source_commit': commit, 'game_version': '3.3.93787',
                'created_utc': datetime.now(timezone.utc).isoformat(),
                'release_tag': args.release_tag,
                'platform': args.platform,
                'storefronts': {'steam': {'version': '3.3.93787', 'status': 'verified'},
                               **({'battlenet': {'version': '3.3.93847', 'status': 'experimental'}} if args.casclib else {})},
                'installation_guide': 'INSTALL-WINDOWS.md' if args.platform == 'native-windows' else 'INSTALL-LINUX.md',
                'variant': args.variant,
                'plugin_source_commit': plugin_commit,
                'plugin_source_note': args.plugin_source_note,
                'connections': ['SteamVR', 'Virtual Desktop (VDXR)'] if args.platform == 'native-windows' else [],
                'play_mode': 'local offline single-player',
                'sha256': hashes}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(args.output, 'w', zipfile.ZIP_DEFLATED) as z:
        for name, path in files.items():
            z.write(path, f'd2r-pcvr-preview/{name}')
        z.writestr('d2r-pcvr-preview/release-manifest.json', json.dumps(manifest, indent=2) + '\n')
    digest = hashlib.sha256(args.output.read_bytes()).hexdigest()
    args.output.with_suffix('.zip.sha256').write_text(f'{digest}  {args.output.name}\n')
    print(f'{args.output} ({args.output.stat().st_size} bytes), source {commit}')


if __name__ == '__main__':
    main()
