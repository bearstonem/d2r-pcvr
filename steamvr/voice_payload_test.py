import tempfile, unittest
from pathlib import Path
from voice_payload import payload
class PayloadTests(unittest.TestCase):
    def test_missing_runtime_is_rejected(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d); (root / "voice").mkdir()
            (root / "voice/voice-recognition.exe").write_bytes(b"fixture")
            with self.assertRaisesRegex(RuntimeError, "Incomplete speech build"):
                payload(root, Path("plugins"))
    def test_old_build_has_no_payload(self):
        with tempfile.TemporaryDirectory() as d: self.assertEqual(payload(d, Path("plugins")), {})
if __name__ == "__main__": unittest.main()
