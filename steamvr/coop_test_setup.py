"""One-click bootstrap for the experimental connection test kit.

Reuse the official install's private Python and verified setup helpers, rather
than distributing a game installation or downloading dependencies again.
"""
import os
import json
from pathlib import Path
import runpy
import sys


def main():
    root = Path(__file__).resolve().parent.parent
    installed = Path(os.environ.get('LOCALAPPDATA', ''))/'Programs/D2R PCVR'
    if not (installed/'steamvr/prepare_dev_scenarios.py').is_file():
        raise SystemExit('Install Unleashed with the official installer before using this test kit.')
    expected=json.loads((root/'vrmod-manifest.json').read_text(encoding='utf-8'))['version']
    current=json.loads((installed/'release-manifest.json').read_text(encoding='utf-8-sig'))['release_tag']
    if current!=f'v{expected}-windows-preview':
        raise SystemExit(f'Install Unleashed v{expected} with the official installer before using this test kit.')
    sys.path.insert(0, str(root/'steamvr'))
    sys.path.insert(1, str(installed/'steamvr'))
    sys.argv[1:1] = ['--stage-test', '--plugin', str(root/'bin/d2rl-pcvr.dll'), '--enable-tcpip']
    runpy.run_path(str(root/'steamvr/coop_setup.py'),run_name='__main__')


if __name__=='__main__':main()
