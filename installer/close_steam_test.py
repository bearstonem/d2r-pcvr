"""The wizard's Close Steam step (installer/close_steam.inc), run for real in a fixture installer.

The fixture compiles a tiny Inno Setup script that includes close_steam.inc and calls it against
a stand-in process: a copy of the private Python under a unique name. The real Steam, steam.exe
and its -shutdown switch are never used, and no game or Steam file is read or written.
"""
import configparser
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import textwrap
import time
import unittest
import uuid

ROOT = Path(__file__).resolve().parent.parent
INCLUDE = ROOT / 'installer/close_steam.inc'
SETUP = ROOT / 'installer/D2R_PCVR_Setup.iss'


def compiler():
    for candidate in (os.environ.get('ISCC'), ROOT / 'deps/inno-7.1.0/ISCC.exe', shutil.which('ISCC.exe'),
                      Path(os.environ.get('ProgramFiles(x86)', r'C:\Program Files (x86)')) / 'Inno Setup 6/ISCC.exe'):
        if candidate and Path(candidate).is_file():
            return Path(candidate)
    return None


FIXTURE = r'''
[Setup]
AppId=D2R-PCVR-Close-Steam-Fixture
AppName=D2R PCVR Close Steam fixture
AppVersion=0.0.1
CreateAppDir=no
Uninstallable=no
PrivilegesRequired=lowest
OutputDir={output}
OutputBaseFilename=close-steam-fixture

[Code]
#include "{include}"

// Runs one scenario from /SCENARIO=<ini> and writes [result]; never installs anything.
// "Page refresh" (the game check the page makes), then optionally the game starting, then "the click".
function InitializeSetup: Boolean;
var
  Scenario, Game: String;
  Blocked: Boolean;
  Code, Waited: Integer;
begin
  Scenario := ExpandConstant('{{param:SCENARIO|}}');
  Game := GetIniString('run', 'game', '', Scenario);
  SetIniString('result', 'running_before', IntToStr(Ord(ProcessRunning(GetIniString('run', 'process', '', Scenario)))), Scenario);
  SetIniString('result', 'game_at_refresh', IntToStr(Ord(AnyRunning(Game))), Scenario);
  if GetIniString('run', 'start_game', '', Scenario) <> '' then begin
    Exec(GetIniString('run', 'start_game', '', Scenario), GetIniString('run', 'start_game_args', '', Scenario), '',
      SW_HIDE, ewNoWait, Code);
    Waited := 0;
    while not AnyRunning(Game) and (Waited < 10000) do begin Sleep(100); Waited := Waited + 100; end;
  end;
  SetIniString('result', 'closed', IntToStr(Ord(CloseGracefully(Game, GetIniString('run', 'exe', '', Scenario),
    GetIniString('run', 'args', '', Scenario), GetIniString('run', 'process', '', Scenario),
    GetIniInt('run', 'timeout', 0, 0, 600000, Scenario), nil, Blocked))), Scenario);
  SetIniString('result', 'blocked', IntToStr(Ord(Blocked)), Scenario);
  SetIniString('result', 'steam_exe', SteamExeInFolder(GetIniString('run', 'steam_folder', '', Scenario)), Scenario);
  SetIniString('result', 'done', '1', Scenario);
  Result := False;
end;
'''

# The stand-in "Steam": runs until the shutdown request writes its flag file (or ignores it when stubborn).
LOOP = textwrap.dedent('''
    import sys, time
    from pathlib import Path
    flag, stubborn = Path(sys.argv[1]), sys.argv[2] == 'stubborn'
    end = time.monotonic() + 60
    while time.monotonic() < end:
        if flag.exists() and not stubborn:
            sys.exit(0)
        time.sleep(0.1)
    sys.exit(3)
''')
STOP = 'import sys\nfrom pathlib import Path\nPath(sys.argv[1]).write_text("shutdown requested")\n'


@unittest.skipUnless(os.name == 'nt' and compiler() is not None, 'needs Windows and Inno Setup (ISCC)')
class CloseSteamFixtureTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix='pcvr-close-steam-')
        root = Path(cls.temporary.name)
        script = root / 'fixture.iss'
        script.write_text(FIXTURE.format(output=root, include=INCLUDE), encoding='utf-8')
        subprocess.run([str(compiler()), '/Qp', str(script)], check=True, capture_output=True)
        cls.setup = root / 'close-steam-fixture.exe'
        python = Path(sys.executable).parent
        if not (python / 'python312._pth').is_file() and (ROOT / 'deps/python/python.exe').is_file():
            python = ROOT / 'deps/python'
        # A unique process name, so neither a real program nor another test can match it.
        cls.name = 'pcvr-fake-steam-' + uuid.uuid4().hex[:8] + '.exe'
        shutil.copytree(python, root / 'runtime')
        cls.exe = root / 'runtime' / cls.name
        (root / 'runtime/python.exe').rename(cls.exe)
        # The stand-in game, with a unique name too: never the real D2R.exe / D2RLoader.exe.
        cls.game_name = 'pcvr-fake-game-' + uuid.uuid4().hex[:8] + '.exe'
        cls.game_exe = root / 'runtime' / cls.game_name
        shutil.copy2(cls.exe, cls.game_exe)
        (root / 'loop.py').write_text(LOOP)
        (root / 'stop.py').write_text(STOP)
        cls.root = root

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def start(self, stubborn=False):
        flag = self.root / ('flag-' + uuid.uuid4().hex)
        process = subprocess.Popen([str(self.exe), str(self.root / 'loop.py'), str(flag), 'stubborn' if stubborn else 'normal'],
                                   creationflags=0x08000000)
        def stop():   # the test's own stand-in, never Steam
            if process.poll() is None:
                process.kill()
            process.wait(10)
        self.addCleanup(stop)
        time.sleep(0.5)
        self.assertIsNone(process.poll())
        return process, flag

    def scenario(self, exe, args, timeout, process=None, **extra):
        path = self.root / ('scenario-' + uuid.uuid4().hex + '.ini')
        # Win32 INI reads drop one pair of enclosing quotes: keep the command line's own.
        path.write_text('[run]\nexe={}\nargs="{}"\nprocess={}\ntimeout={}\nsteam_folder=c:/Program Files (x86)/Steam\n'.format(
            exe, args, process or self.name, timeout) + ''.join(f'{key}="{value}"\n' for key, value in extra.items()),
            encoding='utf-8')
        started = time.monotonic()
        subprocess.run([str(self.setup), '/VERYSILENT', '/SUPPRESSMSGBOXES', '/SCENARIO=' + str(path),
                        '/LOG=' + str(path.with_suffix('.log'))], timeout=120)
        result = configparser.ConfigParser()
        result.read(path, encoding='utf-8')
        self.assertEqual(result.get('result', 'done', fallback=''), '1', path.with_suffix('.log').read_text(errors='replace'))
        return result['result'], time.monotonic() - started

    def stop_command(self, flag):
        return f'"{self.root / "stop.py"}" "{flag}"'

    def test_asks_the_program_to_close_and_waits_for_it(self):
        process, flag = self.start()
        result, _ = self.scenario(self.exe, self.stop_command(flag), 15000)
        self.assertEqual((result['running_before'], result['closed']), ('1', '1'))
        self.assertEqual(process.wait(5), 0, 'the program closed itself; nothing was terminated')
        self.assertTrue(flag.is_file())

    def test_a_program_that_stays_open_is_never_terminated(self):
        process, flag = self.start(stubborn=True)
        result, elapsed = self.scenario(self.exe, self.stop_command(flag), 2000)
        self.assertEqual((result['running_before'], result['closed']), ('1', '0'))
        self.assertGreaterEqual(elapsed, 2)
        self.assertTrue(flag.is_file(), 'the close request was sent')
        self.assertIsNone(process.poll(), 'still running: Setup gives up and asks the player instead')

    def test_game_started_after_the_page_refresh_stops_the_request(self):
        # The page saw no game, then the game started before the click: Steam must not be asked to close.
        process, flag = self.start()
        game_flag = self.root / ('game-flag-' + uuid.uuid4().hex)
        self.addCleanup(lambda: game_flag.write_text('test over'))   # the stand-in game then exits by itself
        result, _ = self.scenario(self.exe, self.stop_command(flag), 15000, game=f'D2R-not-this.exe,{self.game_name}',
                                  start_game=self.game_exe, start_game_args=f'"{self.root / "loop.py"}" "{game_flag}" normal')
        self.assertEqual((result['running_before'], result['game_at_refresh']), ('1', '0'))
        self.assertEqual((result['blocked'], result['closed']), ('1', '0'))
        self.assertFalse(flag.exists(), 'no shutdown request was sent')
        self.assertIsNone(process.poll(), 'the stand-in Steam is still running')

    def test_game_already_running_is_reported_the_same_way(self):
        process, flag = self.start()
        game_flag = self.root / ('game-flag-' + uuid.uuid4().hex)
        game = subprocess.Popen([str(self.game_exe), str(self.root / 'loop.py'), str(game_flag), 'normal'], creationflags=0x08000000)
        self.addCleanup(game.wait, 10)
        self.addCleanup(lambda: game_flag.write_text('test over'))
        time.sleep(0.5)
        result, _ = self.scenario(self.exe, self.stop_command(flag), 15000, game=self.game_name)
        self.assertEqual((result['game_at_refresh'], result['blocked'], result['closed']), ('1', '1', '0'))
        self.assertFalse(flag.exists())
        self.assertIsNone(process.poll())

    def test_nothing_to_close_runs_nothing(self):
        result, _ = self.scenario(self.root / 'missing.exe', '', 15000, process='pcvr-not-running-' + uuid.uuid4().hex + '.exe')
        self.assertEqual((result['running_before'], result['closed']), ('0', '1'))

    def test_missing_steam_exe_leaves_it_running(self):
        process, _ = self.start()
        result, _ = self.scenario(self.root / 'missing.exe', '', 15000)
        self.assertEqual(result['closed'], '0')
        self.assertIsNone(process.poll())

    def test_steam_path_from_the_registry_form(self):
        result, _ = self.scenario(self.root / 'missing.exe', '', 0, process='pcvr-not-running-' + uuid.uuid4().hex + '.exe')
        self.assertEqual(result['steam_exe'], r'c:\Program Files (x86)\Steam\steam.exe')


class CloseSteamScriptTests(unittest.TestCase):
    """What the real wizard does with it, read from the scripts."""

    def test_steam_closes_itself_and_nothing_is_ever_killed(self):
        include, setup = INCLUDE.read_text(encoding='utf-8'), SETUP.read_text(encoding='utf-8')
        self.assertIn("'-shutdown', SteamProcess", include)
        self.assertIn("RegQueryStringValue(HKEY_CURRENT_USER, 'Software\\Valve\\Steam', 'SteamPath'", include)
        self.assertIn('SteamExitTimeoutMs = 30000', include)
        for text in (include, setup):
            for forbidden in ('taskkill', 'TerminateProcess', 'Terminate()', 'wmic process'):
                self.assertNotIn(forbidden.lower(), text.lower())

    def test_the_game_is_never_closed_and_silent_installs_need_the_switch(self):
        include, setup = INCLUDE.read_text(encoding='utf-8'), SETUP.read_text(encoding='utf-8')
        self.assertIn('#include "close_steam.inc"', setup)
        # CloseGracefully is only ever asked to close Steam.
        self.assertEqual(include.count('CloseGracefully('), 2)
        self.assertNotIn('CloseGracefully(', setup)
        self.assertIn("GameProcesses = 'D2R.exe,D2RLoader.exe';", include)
        # The game is checked again inside the close itself, right before the request is sent.
        request = include.index("Exec(Exe, Args")
        self.assertLess(include.index('Blocked := AnyRunning(BlockingProcesses);'), request)
        self.assertIn('CloseGracefully(GameProcesses, SteamShutdownExe(FallbackFolder)', include)
        self.assertIn('CloseSteamButton.Enabled := Steam and not Game;', setup)
        self.assertIn('if SteamRunning and not GameRunning then begin', setup)
        self.assertIn("if SwitchGiven('/CLOSESTEAM') then CloseSteam(", setup)
        self.assertEqual(setup.count('CloseSteam('), 2)  # the button and /CLOSESTEAM
        self.assertIn('Result := ((PageID >= EditionPage.ID) and (PageID <= BackupPage.ID)) or (PageID = CloseSteamPage.ID);', setup)


if __name__ == '__main__':
    unittest.main()
