#!/usr/bin/env python3
"""Diagnostics: a game .texture (its full-size level) out of the installed game as a PNG, for this
machine only (Blizzard art: written under extracted/, which git ignores; never published).

  python export_texture.py GAME data/hd/env/texture/.../name.texture [--casclib CascLib.dll] [--out DIR]
"""
import argparse
import struct
import sys
import zlib
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from game_assets import open_assets  # noqa: E402
from prepare_leather import decode  # noqa: E402


def png(width, height, rgba):
    rows = b''.join(b'\0' + rgba[y * width * 4:(y + 1) * width * 4] for y in range(height))
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data) & 0xFFFFFFFF)
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>2I5B', width, height, 8, 6, 0, 0, 0)) +
            chunk(b'IDAT', zlib.compress(rows, 6)) + chunk(b'IEND', b''))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('game', type=Path)
    ap.add_argument('texture')
    ap.add_argument('--casclib', type=Path, default=None)
    ap.add_argument('--out', type=Path, default=Path(__file__).resolve().parent.parent / 'extracted')
    args = ap.parse_args()
    with open_assets(args.game, args.casclib) as assets:
        width, height, rgba = decode(assets.read(args.texture))
    args.out.mkdir(parents=True, exist_ok=True)
    target = args.out / (Path(args.texture).stem + '.png')
    target.write_bytes(png(width, height, bytes(rgba)))
    print(f'written: {target} ({width}x{height})')
    return 0


if __name__ == '__main__':
    sys.exit(main())
