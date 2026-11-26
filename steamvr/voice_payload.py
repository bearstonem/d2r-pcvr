"""Only ship the recognizer executable, never players' recorded samples."""
from pathlib import Path

def payload(build, plugin_directory):
    exe = Path(build) / "voice" / "voice-recognition.exe"
    return {Path(plugin_directory) / "voice" / exe.name: exe.read_bytes()} if exe.is_file() else {}
