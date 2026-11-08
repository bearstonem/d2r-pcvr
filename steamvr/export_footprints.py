#!/usr/bin/env python3
"""Diagnostics: the outline seen from above (x, z hull of its vertices, the model's own units) of every
model the prepared campfire scene places, for a map of the scene. Numbers only - no Blizzard art leaves
the game; written under extracted/ (git ignores it).

  python export_footprints.py GAME [--casclib CascLib.dll] [--mod D2R-PCVR]
"""
import argparse
import json
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from game_assets import open_assets  # noqa: E402
from potion_assets import Granny  # noqa: E402

PRESET = 'data/hd/env/preset/ui/charactercreate.json'


def hull(points):
    pts = sorted(set(points))
    if len(pts) < 3:
        return pts
    def cross(o, a, b):
        return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])
    lower, upper = [], []
    for p in pts:
        while len(lower) >= 2 and cross(lower[-2], lower[-1], p) <= 0:
            lower.pop()
        lower.append(p)
    for p in reversed(pts):
        while len(upper) >= 2 and cross(upper[-2], upper[-1], p) <= 0:
            upper.pop()
        upper.append(p)
    return lower[:-1] + upper[:-1]


def outline(data):
    g = Granny(data)
    points, top = [], -1e9
    for mesh in g.array(g.root['Meshes']):
        for v in g.array(g.reference(mesh['PrimaryVertexData'])['Vertices']):
            pos = g.scalar(v['Position'])
            x, y, z = pos[:3]
            if len(pos) > 3 and abs(x) <= 1.0001 and abs(z) <= 1.0001:
                # D2R packs a position as int16 / 32767 times VertexScale, and the scale's log2 / 13 in the
                # fourth component (granny_write: _w) - unpacked here into model units
                s = 2.0 ** (pos[3] * 13.0)
                x, y, z = x * s, y * s, z * s
            points.append((round(x, 2), round(z, 2)))
            top = max(top, y)
    return hull(points), round(top, 2)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('game', type=Path)
    ap.add_argument('--casclib', type=Path, default=None)
    ap.add_argument('--mod', default='D2R-PCVR')
    args = ap.parse_args()
    preset = json.loads((args.game / 'mods' / args.mod / f'{args.mod}.mpq' / PRESET).read_text(encoding='utf-8-sig'))
    models = sorted({str(c.get('filename', '')) for e in preset['entities'] if isinstance(e, dict)
                     for c in e.get('components', []) if c.get('type') == 'ModelDefinitionComponent' and c.get('filename')})
    out, bad = {}, []
    with open_assets(args.game, args.casclib) as assets:
        for m in models:
            base = m[:-len('.model')] if m.lower().endswith('.model') else m
            for path in (base + '_lod0.model', base + '_lod1.model', m):
                try:
                    shape, top = outline(assets.read(path))
                    out[m] = {'hull': shape, 'top': top}
                    break
                except Exception as e:   # noqa: BLE001 - a map: take what reads
                    err = f'{path}: {e}'
            else:
                bad.append(err)
    target = Path(__file__).resolve().parent.parent / 'extracted' / 'footprints.json'
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(json.dumps({'models': out, 'failed': bad}), encoding='utf-8')
    print(f'written: {target} ({len(out)} models, {len(bad)} not read)')
    for b in bad[:10]:
        print('  not read:', b)
    return 0


if __name__ == '__main__':
    sys.exit(main())
