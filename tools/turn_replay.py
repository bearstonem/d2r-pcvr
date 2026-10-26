"""Replay one frame of a By turns capture through the warp again (development).

A capture (capture_pair with Stereo rendering By turns) leaves, for frames 0-3,
Frame-turns-N-F-<eye>-kept.png / -depth.bin and Frame-turns-N-F.txt. Frame F
(1-3) turned one eye from the other eye's picture of frame F and its own of
frame F-1; this writes those, and the sidecar's warp, as turn-replay's raw
inputs, runs turn-replay (a developer build's tool) and saves its picture.

    python tools/turn_replay.py <capture folder> <N> <F> <out folder> <turn-replay.exe> [rims fill sharp]

rims, fill and sharp are 0 or 1 (default 1 1 1: [stereo] turns_rims,
turns_fill, turns_sharp). Needs numpy and Pillow.
"""
import subprocess
import sys
from pathlib import Path

import numpy as np
from PIL import Image


def prepare(folder, serial, frame, out):
    side = (folder / f"Frame-turns-{serial}-{frame}.txt").read_text().splitlines()
    first = side[0].split()
    drawn, turned = first[first.index("drawn") + 1], first[first.index("turned") + 1]
    params = []
    for line in side:
        f = line.split()
        if not f:
            continue
        if f[0] == "size":
            params.append(f"size {f[1]} {f[2]}")
        elif f[:2] == ["turn", "homography"]:
            params.append("homography " + " ".join(f[2:11]))
        elif f[:2] == ["drawn", "near"]:
            v = dict(zip(f[1::2], f[2::2]))
            params += [f"drawn.near {v['near']}", f"drawn.apart {v['apart']}",
                       f"drawn.keep {v['keep']} {v['keepNear']} {v['keepReach']} {v['keepEffects']}"]
        elif f[0] in ("drawn", "own") and f[1] in ("move", "carry", "drawnFov", "nowFov"):
            params.append(f"{f[0]}.{f[1]} " + " ".join(f[2:]))
        elif f[:2] == ["own", "near"]:
            params += [f"own.near {f[2]}", f"own.reach {f[4]} {f[5]}"]
    (out / "params.txt").write_text("\n".join(params) + "\n")
    for name, f, eye in (("drawn", frame, drawn), ("own", frame - 1, turned)):
        picture = np.asarray(Image.open(folder / f"Frame-turns-{serial}-{f}-{eye}-kept.png").convert("RGBA"), dtype=np.uint8).copy()
        picture[..., 3] = 255
        picture.tofile(out / f"{name}.rgba")
        (out / f"{name}.depth").write_bytes((folder / f"Frame-turns-{serial}-{f}-{eye}-depth.bin").read_bytes())
    width, height = (int(v) for v in params[0].split()[1:3])
    return turned, width, height


def main():
    if len(sys.argv) not in (6, 9):
        sys.exit(__doc__)
    folder, serial, frame, out = Path(sys.argv[1]), int(sys.argv[2]), int(sys.argv[3]), Path(sys.argv[4])
    tool = str(Path(sys.argv[5]).resolve())   # CreateProcess does not take a relative path with forward slashes
    switches = sys.argv[6:9] if len(sys.argv) == 9 else ["1", "1", "1"]
    if frame < 1:
        sys.exit("frame 0's own picture is from before the capture: replay 1, 2 or 3")
    out.mkdir(parents=True, exist_ok=True)
    turned, width, height = prepare(folder, serial, frame, out)
    name = f"replay-{serial}-{frame}-{turned}-rims{switches[0]}-fill{switches[1]}-sharp{switches[2]}"
    subprocess.run([tool, str(out), *switches, name + ".rgba"], check=True)
    picture = np.fromfile(out / (name + ".rgba"), dtype=np.uint8).reshape(height, width, 4)
    Image.fromarray(picture[..., :3]).save(out / (name + ".png"))
    print(out / (name + ".png"), "- compare with", folder / f"Frame-turns-{serial}-{frame}-{turned}-turned.png")


if __name__ == "__main__":
    main()
