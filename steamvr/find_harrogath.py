"""Diagnostics: Harrogath's own scene presets in the game's archives.

Lists the presets of Act 5's town (and anything else that names it), with how many
entities each places and the area they cover, and which of the campfire scene's
models each one also uses - to find the real town the character screen was cut
from. Reads the player's own archives, writes nothing.

  python find_harrogath.py "<game folder>" --casclib "<...>/bin/CascLib.dll"
"""
import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from game_assets import open_assets  # noqa: E402

CAMPFIRE = 'data/hd/env/preset/ui/charactercreate.json'


def models(preset):
    found = set()
    def walk(node):
        if isinstance(node, str):
            if node.lower().endswith(('.model', '.json')) and node.lower().startswith('data/'):
                found.add(node.lower())
        elif isinstance(node, dict):
            for v in node.values():
                walk(v)
        elif isinstance(node, list):
            for v in node:
                walk(v)
    walk(preset.get('entities', []))
    return found


def extent(preset):
    xs, zs = [], []
    for e in preset.get('entities', []):
        for c in e.get('components', []) if isinstance(e, dict) else []:
            if c.get('type') == 'TransformDefinitionComponent' and isinstance(c.get('position'), dict):
                xs.append(float(c['position'].get('x', 0)))
                zs.append(float(c['position'].get('z', 0)))
    if not xs:
        return '-'
    return f'x {min(xs):8.1f}..{max(xs):8.1f}  z {min(zs):8.1f}..{max(zs):8.1f}'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('game', type=Path)
    ap.add_argument('--casclib', type=Path, default=None)
    args = ap.parse_args()
    with open_assets(args.game, args.casclib) as assets:
        campfire = models(json.loads(assets.read(CAMPFIRE)))
        names = sorted(n for n in assets.files if n.startswith('data/hd/env/preset/') and n.endswith('.json')
                       and ('expansion' in n or 'act5' in n or 'town' in n or 'harrogath' in n))
        print(f'{len(names)} candidate presets; the campfire scene uses {len(campfire)} distinct models/prefabs')
        rows = []
        for n in names:
            try:
                p = json.loads(assets.read(n))
            except Exception as error:   # noqa: BLE001 - listing only
                print(f'  {n}: unreadable ({error})')
                continue
            m = models(p)
            rows.append((len(m & campfire), len(p.get('entities', [])), n, extent(p)))
        rows.sort(reverse=True)
        print('shared-models | entities | preset | area')
        for shared, count, n, area in rows:
            print(f'  {shared:4d} | {count:5d} | {n} | {area}')
        lvl = sorted(n for n in assets.files if 'lvlprest' in n or n.endswith('.ds1'))
        print(f'\n{len(lvl)} level files (.ds1 / lvlprest) found; first 40:')
        for n in lvl[:40]:
            print(' ', n)
    return 0


if __name__ == '__main__':
    sys.exit(main())
