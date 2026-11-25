"""Recognition payload shared by full and local branch installers."""
from pathlib import Path

def payload(build, plugin_directory):
    root = Path(build) / "voice"
    # Older build fixtures and branches have no integrated helper.
    if not (root / "voice-recognition.exe").exists():
        return {}
    required = ["libvosk.dll", "libgcc_s_seh-1.dll", "libstdc++-6.dll", "libwinpthread-1.dll", "VOSK-COPYING.txt",
                "vosk-model-small-en-us-0.15/am/final.mdl", "vosk-model-small-en-us-0.15/conf/model.conf"]
    for name in required:
        if not (root / name).is_file():
            raise RuntimeError("Incomplete speech build: " + name)
    return {Path(plugin_directory) / "voice" / p.relative_to(root): p.read_bytes()
            for p in root.rglob("*") if p.is_file()}
