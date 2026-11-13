"""Exercise the subscription INI control against temporary profiles only."""
import configparser
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


@unittest.skipUnless(os.name == 'nt', 'Windows INI API integration')
class SubscriptionControlTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.ini = Path(self.directory.name) / 'fixture.ini'
        self.original = ('; keep this comment\r\n[render]\r\nrings=6\r\n'
                         'model_radius=210\r\nsubscription_rings=8\r\n'
                         '[unrelated]\r\nkeep=42\r\n')
        self.ini.write_bytes(self.original.encode('ascii'))

    def run_control(self, rings, ini=None):
        script = Path(__file__).with_name('Set-Subscriptions.ps1')
        return subprocess.run(['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass',
                               '-File', str(script), '-Ini', str(ini or self.ini),
                               '-Rings', str(rings)], capture_output=True, text=True,
                              creationflags=subprocess.CREATE_NO_WINDOW)

    def test_toggle_updates_only_render_subscription_preference(self):
        for rings in (0, 8):
            result = self.run_control(rings)
            self.assertEqual(result.returncode, 0, result.stderr)
            text = self.ini.read_text()
            settings = configparser.ConfigParser()
            settings.read_string(text)
            self.assertEqual(settings.getint('render', 'subscription_rings'), rings)
            self.assertEqual(settings.getint('render', 'rings'), 6)
            self.assertEqual(settings.getint('render', 'model_radius'), 210)
            self.assertEqual(settings.getint('unrelated', 'keep'), 42)
            self.assertIn('; keep this comment', text)
            self.assertEqual(set(settings.sections()), {'render', 'unrelated'})

    def test_invalid_depth_preserves_profile(self):
        self.assertNotEqual(self.run_control(4).returncode, 0)
        self.assertEqual(self.ini.read_bytes(), self.original.encode('ascii'))

    def test_missing_profile_is_not_created(self):
        missing = self.ini.with_name('missing.ini')
        self.assertNotEqual(self.run_control(8, missing).returncode, 0)
        self.assertFalse(missing.exists())


if __name__ == '__main__':
    unittest.main()
