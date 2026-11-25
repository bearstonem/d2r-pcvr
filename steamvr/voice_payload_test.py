import tempfile, unittest
from pathlib import Path
from voice_payload import payload
class PayloadTests(unittest.TestCase):
    def test_only_executable_is_shipped(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d); (root / "voice").mkdir()
            (root / "voice/voice-recognition.exe").write_bytes(b"fixture")
            (root / "voice/private-sample.wav").write_bytes(b"private")
            (root / "voice/libvosk.dll").write_bytes(b"obsolete")
            self.assertEqual(payload(root, Path("plugins")), {Path("plugins/voice/voice-recognition.exe"): b"fixture"})
    def test_old_build_has_no_payload(self):
        with tempfile.TemporaryDirectory() as d: self.assertEqual(payload(d, Path("plugins")), {})
if __name__ == "__main__": unittest.main()
