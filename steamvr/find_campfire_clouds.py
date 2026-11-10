"""Diagnostics: where character creation's campfire scene takes its clouds from.

Lists the game's assets whose names mention the character screen, and inside
each JSON among them every path that mentions clouds, sky or weather. Reads the
player's own archives (the mod's CascLib), writes nothing.

  python find_campfire_clouds.py "<game folder>" --casclib "<...>/bin/CascLib.dll"
"""
import argparse
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from game_assets import open_assets  # noqa: E402

KEYS = ('characterselect', 'charactercreate', 'char_select', 'charselect', 'frontend')
WORDS = re.compile(r'cloud|sky|weather|horizon|backdrop|matte', re.I)


def strings(node):
    if isinstance(node, str):
        yield node
    elif isinstance(node, dict):
        for k, v in node.items():
            yield from strings(k)
            yield from strings(v)
    elif isinstance(node, list):
        for v in node:
            yield from strings(v)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('game', type=Path)
    ap.add_argument('--casclib', type=Path, default=None)
    args = ap.parse_args()
    with open_assets(args.game, args.casclib) as assets:
        names = sorted(n for n in assets.files if any(k in n.lower() for k in KEYS))
        print(f'{len(names)} assets name the character screen:')
        for n in names:
            print(' ', n)
        print()
        print('inside them - paths with cloud / sky / weather / horizon / backdrop / matte:')
        for n in names:
            if not n.endswith('.json'):
                continue
            try:
                data = json.loads(assets.read(n))
            except Exception as error:   # noqa: BLE001 - listing only
                print(f'  {n}: unreadable ({error})')
                continue
            hits = sorted({s for s in strings(data) if WORDS.search(s)})
            if hits:
                print(f'  {n}:')
                for h in hits[:40]:
                    print(f'      {h}')
        clouds = sorted(n for n in assets.files if 'cloud' in n.lower())
        print()
        print(f'{len(clouds)} assets in the whole game with "cloud" in the name (first 80):')
        for n in clouds[:80]:
            print(' ', n)
    return 0


if __name__ == '__main__':
    sys.exit(main())
