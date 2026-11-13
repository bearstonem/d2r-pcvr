import ctypes
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

import render_distance_status as runtime


@unittest.skipUnless(os.name == 'nt', 'Windows shared-memory integration')
class RuntimeStatusTests(unittest.TestCase):
    def setUp(self):
        helper = Path(__file__).resolve().parent.parent / 'build-frame/windows-native/render-distance-status-test.exe'
        if not helper.exists():
            self.skipTest('Build render-distance-status-test first.')
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.ini = Path(self.directory.name) / 'fixture.ini'
        self.process = subprocess.Popen([str(helper), str(self.ini)], stdin=subprocess.PIPE,
                                        stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                        text=True, creationflags=subprocess.CREATE_NO_WINDOW)
        self.addCleanup(self.stop_fixture)
        self.assertEqual(self.process.stdout.readline().strip(), 'READY')

    def stop_fixture(self):
        if self.process.poll() is None:
            self.process.communicate('\n', timeout=5)

    def test_cpp_publisher_and_python_reader_agree(self):
        status = runtime.read_status(self.ini)
        self.assertEqual(status['pid'], self.process.pid)
        self.assertEqual(status['requested_rings'], 8)
        self.assertEqual(status['furthest_room'], 4)
        self.assertEqual(status['active_model_radius'], 3260)
        text = runtime.format_status(status)
        self.assertIn('requested rings 8', text)
        self.assertIn('furthest room depth 4', text)
        self.assertIn('tiler ready', text)
        self.assertIn('recalculate waiting', text)
        self.assertEqual(runtime.mapping_name(self.ini), runtime.mapping_name(str(self.ini).upper()))

    def test_other_profile_and_closed_writer_have_no_status(self):
        with self.assertRaisesRegex(OSError, 'No live status'):
            runtime.read_status(self.ini.with_name('other.ini'))
        self.stop_fixture()
        with self.assertRaisesRegex(OSError, 'No live status'):
            runtime.read_status(self.ini)

    def test_reader_rejects_incomplete_stale_or_unsupported_snapshot(self):
        # Mutate only this test's own published fixture mapping. This verifies
        # torn-write/staleness behavior without touching a game mapping.
        api = ctypes.WinDLL('kernel32', use_last_error=True)
        api.OpenFileMappingW.argtypes = (ctypes.c_uint32, ctypes.c_int, ctypes.c_wchar_p)
        api.OpenFileMappingW.restype = ctypes.c_void_p
        api.MapViewOfFile.argtypes = (ctypes.c_void_p, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_size_t)
        api.MapViewOfFile.restype = ctypes.c_void_p
        api.UnmapViewOfFile.argtypes = (ctypes.c_void_p,)
        api.CloseHandle.argtypes = (ctypes.c_void_p,)
        handle = api.OpenFileMappingW(2, False, runtime.mapping_name(self.ini))
        self.assertTrue(handle)
        view = api.MapViewOfFile(handle, 2, 0, 0, 4096)
        self.assertTrue(view)
        try:
            status = runtime.read_status(self.ini)
            ctypes.c_uint32.from_address(view).value = 3
            with self.assertRaisesRegex(ValueError, 'updating'):
                runtime.read_status(self.ini)
            status['tick_ms'] = 0
            raw = json.dumps(status).encode() + b'\0'
            ctypes.memmove(view + 4, raw, len(raw))
            ctypes.c_uint32.from_address(view).value = 4
            with self.assertRaisesRegex(ValueError, 'stale'):
                runtime.read_status(self.ini)
            status['schema'] = 2
            raw = json.dumps(status).encode() + b'\0'
            ctypes.memmove(view + 4, raw, len(raw))
            with self.assertRaisesRegex(ValueError, 'Unsupported'):
                runtime.read_status(self.ini)
        finally:
            api.UnmapViewOfFile(view)
            api.CloseHandle(handle)


if __name__ == '__main__':
    unittest.main()
