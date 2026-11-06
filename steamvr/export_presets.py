"""Diagnostics: copy scene presets out of the game's archives, as they are, to a folder.

For analysis away from the game (which part of Harrogath the campfire scene was
cut from). Reads the player's own archives; writes only into --out.

  python export_presets.py "<game folder>" --casclib "<...>/bin/CascLib.dll" --out <folder> <preset> [<preset> ...]
"""
import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from game_assets import open_assets  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('game', type=Path)
    ap.add_argument('presets', nargs='*')
    ap.add_argument('--casclib', type=Path, default=None)
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--list', action='append', default=[], metavar='TEXT',
                    help='only list the archive names holding TEXT (models first), export nothing')
    ap.add_argument('--refs', action='append', default=[], metavar='TEXT',
                    help='list the scene presets that name TEXT, with where they place it')
    args = ap.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    with open_assets(args.game, args.casclib) as assets:
        if args.refs:
            import json
            presets = sorted(n for n in assets.files if n.startswith('data/hd/env/preset/') and n.endswith('.json'))
            for text in args.refs:
                print(f'presets naming "{text}":')
                for n in presets:
                    raw = assets.read(n)
                    if text.lower().encode() not in raw.lower():
                        continue
                    try:
                        entities = json.loads(raw.decode('utf-8-sig')).get('entities', [])
                    except Exception:   # noqa: BLE001 - listing only
                        entities = []
                    for e in entities:
                        if text.lower() not in json.dumps(e).lower():
                            continue
                        tr = next((c for c in e.get('components', []) if c.get('type') == 'TransformDefinitionComponent'), {})
                        print(f'    {n} | {e.get("name")} | pos {tr.get("position")} | rot {tr.get("orientation")} | scale {tr.get("scale")}')
        if args.list:
            for text in args.list:
                found = sorted((not n.endswith('.model'), n) for n in assets.files if text.lower() in n)
                found = [n for _, n in found if '_lod' not in n or '_lod0' in n]
                print(f'{len(found)} names hold "{text}" (lod0 only):')
                for n in found[:300]:
                    print(f'    {n}')
            return 0
        for name in args.presets:
            key = name.lower().replace('\\', '/')
            if key not in assets.files:
                # not under that name: everything whose name holds the file's stem instead
                stem = Path(key).stem
                found = sorted(n for n in assets.files if stem in n)
                print(f'{name}: not in the archive index; {len(found)} names hold "{stem}":')
                for n in found[:40]:
                    print(f'    {n}')
                names = found[:10]
            else:
                names = [key]
            for n in names:
                data = assets.read(n)
                target = args.out / Path(n).name
                target.write_bytes(data)
                print(f'{n}: {len(data)} bytes -> {target}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
