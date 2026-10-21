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
    parser.add_argument('--plugin',type=Path, help='Required for the optional diagnostic-DLL kit')
    parser.add_argument('--assistant-only', action='store_true', help='Setup/checks for the existing release; no DLL or separate mod')
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    if subprocess.check_output(['git','status','--porcelain'],cwd=ROOT).strip():
        parser.error('Commit the test candidate before packaging.')
    if not args.assistant_only and (args.plugin is None or b'Frame co-op: experimental transport diagnostics ready' not in args.plugin.read_bytes()):
        parser.error('Select the co-op diagnostic candidate DLL.')
    paths=['Coop-Setup.cmd', 'steamvr/coop_assistant.py', 'steamvr/coop_connection.py', 'steamvr/coop_wizard.py',
           'steamvr/coop_setup.py','docs/MULTIPLAYER.md',
           'vrmod-manifest.json','LICENSE','THIRD_PARTY.md','third_party/minhook/LICENSE.txt']
    if not args.assistant_only:
        paths += ['steamvr/coop_test_setup.py']
    paths += [p.relative_to(ROOT).as_posix() for p in sorted((ROOT/'steamvr/licenses').glob('*.txt'))]
    files={name:(ROOT/name).read_bytes() for name in paths}
    if not args.assistant_only:
        files['bin/d2rl-pcvr.dll']=args.plugin.read_bytes()
    for name,extra in ([] if args.assistant_only else [('Launch-Coop-SteamVR.cmd',''),('Launch-Coop-VDXR.cmd','%%20--frame-vdxr')]):
        files[name]=('@echo off\r\nstart "" "steam://run/2536520//--frame-vr'+extra+'%%20-mod%%20pcvr-coop/"\r\n').encode('ascii')
    files['Setup-Coop-Test.cmd']=b'''@echo off\r
setlocal\r
set "coop_python=%LOCALAPPDATA%\\Programs\\D2R PCVR\\.runtime\\python\\python.exe"\r
if not exist "%coop_python%" (\r
    echo Install the matching Unleashed release with our official installer first.\r
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
Both PCs: install the matching Unleashed release, exit D2R/D2RLoader, extract this kit,\r
then run Setup-Coop-Test.cmd. Use the SAME kit on both PCs.\r
Use a Launch-Coop CMD (Steam edition), or select pcvr-coop under Other\r
mods in the shared launcher. Create an\r
offline test character. Choose Multiplayer beside Play; host LAN Only\r
or Unlisted, then have the other PC join. See docs/MULTIPLAYER.md.\r
Guest physical swings, shield defense and custom projectiles still need\r
host/guest verification. This kit adds connection diagnostics, not an action bridge.\r
Normal Unleashed, pcvr-dev and existing saves are preserved.\r
No game assets, extracted caches, character files or loader are included.\r
'''
    if args.assistant_only:
        files.pop('Setup-Coop-Test.cmd')
        files['README.txt']=('Unleashed CO-OP SETUP ASSISTANT\r\n'
            'Install Unleashed with the official Windows installer on both PCs first.\r\n'
            'Extract this kit and double-click Coop-Setup.cmd. Select Host or Join.\r\n'
            'It uses the normal installed mod; no DLL, separate mod or game assets are included.\r\n'
            'Compare friend reports and test the host port after the host creates a game.\r\n'
            'Only enabling the Multiplayer button changes loader settings (with an exact backup).\r\n'
            'No game is launched and no router/firewall rules or saves are changed.\r\n'
            'Community reports confirm basic play; remote VR movements are not replicated.\r\n'
            'See docs/MULTIPLAYER.md for LAN, internet, forwarding and private VPN steps.\r\n').encode('utf-8')
    report=dict(source_commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
                purpose='setup assistant for the installed release' if args.assistant_only else 'optional connection diagnostic build; no remote action dispatch added',
                sha256={name:hashlib.sha256(data).hexdigest() for name,data in files.items()})
    files['test-kit-manifest.json']=(json.dumps(report,indent=2)+'\n').encode()
    args.output.parent.mkdir(parents=True,exist_ok=True)
    with zipfile.ZipFile(args.output,'w',zipfile.ZIP_DEFLATED) as archive:
        for name,data in files.items():archive.writestr(name,data)
    print(f'{args.output}: {len(files)} allowlisted files; SHA256 {hashlib.sha256(args.output.read_bytes()).hexdigest()}')


if __name__=='__main__':main()
