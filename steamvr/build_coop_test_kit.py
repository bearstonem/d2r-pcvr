"""Package the diagnostic test DLL and source helpers only, never a game tree."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parent.parent


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--plugin',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    if subprocess.check_output(['git','status','--porcelain'],cwd=ROOT).strip():
        parser.error('Commit the test candidate before packaging.')
    if b'Frame co-op: experimental transport diagnostics ready' not in args.plugin.read_bytes():
        parser.error('Select the co-op diagnostic candidate DLL.')
    paths=['steamvr/coop_setup.py','steamvr/coop_test_setup.py','docs/MULTIPLAYER.md',
           'vrmod-manifest.json','LICENSE','THIRD_PARTY.md','third_party/minhook/LICENSE.txt']
    paths += [p.relative_to(ROOT).as_posix() for p in sorted((ROOT/'steamvr/licenses').glob('*.txt'))]
    files={name:(ROOT/name).read_bytes() for name in paths}
    files['bin/d2rl-pcvr.dll']=args.plugin.read_bytes()
    files['Setup-Coop-Test.cmd']=b'''@echo off\r
setlocal\r
set "coop_python=%LOCALAPPDATA%\\Programs\\D2R PCVR\\.runtime\\python\\python.exe"\r
if not exist "%coop_python%" (\r
    echo Install Unleashed v0.3.20 with our official installer first.\r
    pause\r
    exit /b 1\r
)\r
"%coop_python%" "%~dp0steamvr\\coop_test_setup.py" %*\r
set "coop_result=%errorlevel%"\r
echo.\r
pause\r
exit /b %coop_result%\r
'''
    files['README.txt']=b'''Unleashed TCP/IP CONNECTION TEST - not a multiplayer combat release.\r
Both PCs: install Unleashed v0.3.20, exit D2R/D2RLoader, extract this kit,\r
then run Setup-Coop-Test.cmd. Use the SAME kit on both PCs.\r
Launch pcvr-coop under Other mods in the shared launcher and create an\r
offline test character. Choose Multiplayer beside Play; host LAN Only\r
or Unlisted, then have the other PC join. See docs/MULTIPLAYER.md.\r
Physical guest swings, shield defense and custom projectiles still need\r
their host-side action bridge. This kit verifies the connection path.\r
Normal Unleashed, pcvr-dev and existing saves are preserved.\r
No game assets, extracted caches, character files or loader are included.\r
'''
    report=dict(source_commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
                purpose='connection diagnostics only; guest physical actions not implemented',
                sha256={name:hashlib.sha256(data).hexdigest() for name,data in files.items()})
    files['test-kit-manifest.json']=(json.dumps(report,indent=2)+'\n').encode()
    args.output.parent.mkdir(parents=True,exist_ok=True)
    with zipfile.ZipFile(args.output,'w',zipfile.ZIP_DEFLATED) as archive:
        for name,data in files.items():archive.writestr(name,data)
    print(f'{args.output}: {len(files)} allowlisted files; SHA256 {hashlib.sha256(args.output.read_bytes()).hexdigest()}')


if __name__=='__main__':main()
