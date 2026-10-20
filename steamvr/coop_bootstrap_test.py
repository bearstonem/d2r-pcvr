import contextlib
import hashlib
import io
import json
import os
from pathlib import Path
import sys
import tempfile
import types
import unittest
from unittest.mock import patch, Mock
import zipfile

import build_coop_test_kit as kit
import coop_assistant


class AssistantKitTests(unittest.TestCase):
    def test_assistant_archive_has_only_helpers_docs_licenses_and_no_dll(self):
        with tempfile.TemporaryDirectory() as tmp:
            archive=Path(tmp)/'assistant.zip'
            with patch.object(sys,'argv',['kit','--assistant-only','--output',str(archive)]), \
                    patch.object(kit.subprocess,'check_output',side_effect=[b'', 'a'*40]), \
                    contextlib.redirect_stdout(io.StringIO()):
                kit.main()
            with zipfile.ZipFile(archive) as z:
                names=set(z.namelist())
                self.assertIn('Coop-Setup.cmd',names)
                self.assertIn('steamvr/coop_assistant.py',names)
                self.assertNotIn('Setup-Coop-Test.cmd',names)
                self.assertFalse(any(name.lower().endswith(('.dll','.exe','.d2s')) for name in names))
                self.assertFalse(any('Launch-Coop' in name or 'cache' in name for name in names))
                manifest=json.loads(z.read('test-kit-manifest.json'))
                self.assertEqual(set(manifest['sha256']),names-{'test-kit-manifest.json'})
                for name,digest in manifest['sha256'].items():
                    self.assertEqual(hashlib.sha256(z.read(name)).hexdigest(),digest)
                self.assertIn(b'CO-OP SETUP ASSISTANT',z.read('README.txt'))
                self.assertNotIn(b'0.3.20',z.read('README.txt'))

    def test_diagnostic_kit_still_requires_a_candidate(self):
        with patch.object(sys,'argv',['kit','--output','unused.zip']), \
                patch.object(kit.subprocess,'check_output',return_value=b''), \
                contextlib.redirect_stderr(io.StringIO()),self.assertRaises(SystemExit):
            kit.main()

    def test_full_package_runs_its_own_helper_without_external_runtime_check(self):
        with tempfile.TemporaryDirectory() as tmp:
            here=Path(tmp)/'steamvr';here.mkdir();(here/'game_install.py').write_text('fixture')
            old_path=list(sys.path)
            try:
                with patch.object(coop_assistant,'__file__',str(here/'coop_assistant.py')), \
                        patch.object(coop_assistant.runpy,'run_path') as run:
                    coop_assistant.main()
                    self.assertEqual(sys.path[0],str(here))
                    run.assert_called_once_with(str(here/'coop_setup.py'),run_name='__main__')
            finally:sys.path[:]=old_path

    def test_standalone_verifies_installed_helpers_and_adds_its_own_module_path(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);here=root/'kit/steamvr';here.mkdir(parents=True)
            installed=root/'Programs/D2R PCVR';(installed/'steamvr').mkdir(parents=True)
            (installed/'steamvr/setup_windows.py').write_text('fixture')
            setup=types.ModuleType('setup_windows');setup.verify_release=Mock()
            old_path=list(sys.path)
            try:
                with patch.object(coop_assistant,'__file__',str(here/'coop_assistant.py')), \
                        patch.dict(os.environ,LOCALAPPDATA=str(root)),patch.dict(sys.modules,setup_windows=setup), \
                        patch.object(coop_assistant.runpy,'run_path') as run:
                    coop_assistant.main()
                    setup.verify_release.assert_called_once_with(installed)
                    self.assertEqual(sys.path[:2],[str(here),str(installed/'steamvr')])
                    run.assert_called_once()
            finally:sys.path[:]=old_path

    def test_standalone_refuses_missing_or_unverified_install(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);here=root/'kit/steamvr';here.mkdir(parents=True)
            old_path=list(sys.path)
            try:
                with patch.object(coop_assistant,'__file__',str(here/'coop_assistant.py')), \
                        patch.dict(os.environ,LOCALAPPDATA=str(root)), \
                        patch.object(coop_assistant.runpy,'run_path') as run,self.assertRaises(SystemExit):
                    coop_assistant.main()
                run.assert_not_called()
            finally:sys.path[:]=old_path


if __name__=='__main__':unittest.main()
