"""Diagnostics: what stands in character creation's campfire scene, and where.

Every entity of data/hd/env/preset/ui/charactercreate.json with its position and
whatever model, particle effect, prefab or light it places, plus the preset's
dependency lists - to see what the scene is built from (which act's pieces) and
to place anything new round the player. Reads the player's own archives, writes
nothing.

  python dump_campfire_scene.py "<game folder>" --casclib "<...>/bin/CascLib.dll"
"""
import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from game_assets import open_assets  # noqa: E402

PRESET = 'data/hd/env/preset/ui/charactercreate.json'


def paths(node):
    if isinstance(node, str):
        if node.lower().startswith('data/'):
            yield node
    elif isinstance(node, dict):
        for v in node.values():
            yield from paths(v)
    elif isinstance(node, list):
        for v in node:
            yield from paths(v)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('game', type=Path)
    ap.add_argument('--casclib', type=Path, default=None)
    args = ap.parse_args()
    with open_assets(args.game, args.casclib) as assets:
        preset = json.loads(assets.read(PRESET))
    print('dependencies:')
    for kind, items in (preset.get('dependencies') or {}).items():
        for item in items or []:
            print(f'  {kind}: {item.get("path") if isinstance(item, dict) else item}')
    print()
    print(f'{len(preset.get("entities", []))} entities (name | position | what it places):')
    for e in preset.get('entities', []):
        pos = None
        placed = []
        for c in e.get('components', []):
            if c.get('type') == 'TransformDefinitionComponent' and isinstance(c.get('position'), dict):
                p = c['position']
                pos = f'{p.get("x", 0):9.2f} {p.get("y", 0):8.2f} {p.get("z", 0):9.2f}'
            for path in paths(c):
                placed.append(path)
            if 'Light' in c.get('type', ''):
                placed.append(c.get('type'))
        print(f'  {e.get("name")} | {pos or "-"} | {", ".join(sorted(set(placed)))[:200]}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
