"""Download pinned local speech runtime/model; no game build or installation."""
import argparse, hashlib, pathlib, urllib.request, zipfile
ROOT = pathlib.Path(__file__).resolve().parent.parent
DEST = ROOT / "build-frame" / "voice-recognition"
CACHE = ROOT / "deps" / "voice-recognition"
WHEEL = "https://files.pythonhosted.org/packages/c0/4c/deb0861f7da9696f8a255f1731bb73e9412cca29c4b3888a3fcb2a930a59/vosk-0.3.45-py3-none-win_amd64.whl"
SHA = "6994ddc68556c7e5730c3b6f6bad13320e3519b13ce3ed2aa25a86724e7c10ac"
MODEL = "https://alphacephei.com/vosk/models/vosk-model-small-en-us-0.15.zip"
MODEL_SHA = "30f26242c4eb449f948e42cb302dd7a686cb29a3423a8367f99ff41780942498"
def fetch(url, name, expected):
    CACHE.mkdir(parents=True, exist_ok=True)
    path = CACHE / name
    if not path.exists():
        print("Downloading", name, flush=True)
        temp = path.with_suffix(".part")
        with urllib.request.urlopen(url, timeout=120) as src, temp.open("wb") as dst:
            while data := src.read(1024*1024): dst.write(data)
        temp.replace(path)
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    if expected and digest != expected: raise RuntimeError("Checksum mismatch: " + name)
    print(name, digest, flush=True)
    return path

def setup():
    DEST.mkdir(parents=True, exist_ok=True)
    wheel = fetch(WHEEL, "vosk-0.3.45-win64.whl", SHA)
    with zipfile.ZipFile(wheel) as z:
        for name in z.namelist():
            if name.endswith(".dll") or "LICENSE" in name.upper() or "COPYING" in name.upper():
                (DEST / pathlib.PurePosixPath(name).name).write_bytes(z.read(name))
    notice = fetch("https://raw.githubusercontent.com/alphacep/vosk-api/v0.3.45/COPYING", "VOSK-COPYING", "bb618af833c817ab01c1564bd3ec75ba4b4f9cf30910a4a2168e30a674ee8cc6")
    (DEST / "VOSK-COPYING.txt").write_bytes(notice.read_bytes())
    model = fetch(MODEL, "vosk-model-small-en-us-0.15.zip", MODEL_SHA)
    with zipfile.ZipFile(model) as z:
        for info in z.infolist():
            path = (DEST / info.filename).resolve()
            if not path.is_relative_to(DEST.resolve()): raise RuntimeError("Unsafe archive path")
            if info.is_dir(): path.mkdir(parents=True, exist_ok=True)
            else:
                path.parent.mkdir(parents=True, exist_ok=True)
                if not path.exists() or path.stat().st_size != info.file_size: path.write_bytes(z.read(info))
    print("Speech runtime ready:", DEST)
if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--destination", type=pathlib.Path)
    args = parser.parse_args()
    if args.destination: DEST = args.destination
    setup()
