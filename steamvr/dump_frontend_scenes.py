"""Diagnostics: the main menu's (character select's) act scenes in the game's archives.

The screen the game opens on stands one of five 3D scenes, one per act (the biome is
'act<N>_frontend_biome', as the log shows: 'act2_frontend_biome'). This lists every
file that names 'frontend' and every UI preset, copies the presets (raw JSON) into
an output folder, and writes for each a summary of its entities (name, position,
what it places) - to see what the scene is built from before it is made into a
place to stand in, as the campfire was. Reads the player's own archives; writes
only into the output folder (keep it out of git: extracted/ is the game's).

  python dump_frontend_scenes.py "<game folder>" --casclib "<...>/bin/CascLib.dll" --out "<folder>"
"""
import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from game_assets import open_assets  # noqa: E402


def paths(node):
    if isinstance(node, str):
        if node.lower().startswith('data/') or node.lower().endswith(('.model', '.particles', '.json', '.texture')):
            yield node
    elif isinstance(node, dict):
        for v in node.values():
            yield from paths(v)
    elif isinstance(node, list):
        for v in node:
            yield from paths(v)


def summary(name, preset):
    out = [f'# {name}']
    for kind, items in (preset.get('dependencies') or {}).items():
        out.append(f'dependencies {kind}: {len(items or [])}')
        for item in (items or [])[:400]:
            out.append(f'  {item.get("path") if isinstance(item, dict) else item}')
    ents = preset.get('entities', []) or []
    out.append(f'\n{len(ents)} entities (name | x y z | component types | what it places):')
    xs, zs = [], []
    for e in ents:
        pos, placed, kinds = '-', set(), []
        for c in e.get('components', []) if isinstance(e, dict) else []:
            t = str(c.get('type', ''))
            kinds.append(t.replace('DefinitionComponent', '').replace('Component', ''))
            if t == 'TransformDefinitionComponent' and isinstance(c.get('position'), dict):
                p = c['position']
                x, y, z = float(p.get('x', 0)), float(p.get('y', 0)), float(p.get('z', 0))
                xs.append(x); zs.append(z)
                pos = f'{x:9.2f} {y:8.2f} {z:9.2f}'
            placed.update(paths(c))
        out.append(f'  {e.get("name")} | {pos} | {",".join(kinds)} | {", ".join(sorted(placed))[:300]}')
    if xs:
        out.append(f'\narea: x {min(xs):.1f}..{max(xs):.1f}  z {min(zs):.1f}..{max(zs):.1f}')
    return '\n'.join(out) + '\n'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('game', type=Path)
    ap.add_argument('--casclib', type=Path, default=None)
    ap.add_argument('--out', type=Path, required=True)
    args = ap.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    report = []
    with open_assets(args.game, args.casclib) as assets:
        names = sorted(assets.files)
        front = [n for n in names if 'frontend' in n or 'characterselect' in n or 'charselect' in n]
        ui = [n for n in names if n.startswith('data/hd/env/preset/ui/')]
        report.append(f'{len(front)} files naming frontend / characterselect:')
        report += [f'  {n}' for n in front]
        report.append(f'\n{len(ui)} UI presets (data/hd/env/preset/ui/):')
        report += [f'  {n}' for n in ui]
        wanted = sorted({n for n in front + ui if n.endswith('.json')})
        report.append(f'\n{len(wanted)} JSON copied to {args.out}:')
        for n in wanted:
            try:
                raw = assets.read(n)
            except Exception as error:   # noqa: BLE001 - listing only
                report.append(f'  {n}: unreadable ({error})')
                continue
            flat = n.replace('data/hd/', '').replace('/', '__')
            (args.out / flat).write_bytes(raw)
            try:
                preset = json.loads(raw)
            except Exception:   # noqa: BLE001 - not JSON we know
                report.append(f'  {n}: copied (not parsed)')
                continue
            if isinstance(preset, dict) and 'entities' in preset:
                (args.out / (flat[:-5] + '.summary.txt')).write_text(summary(n, preset), encoding='utf-8')
                report.append(f'  {n}: {len(preset.get("entities") or [])} entities')
            else:
                report.append(f'  {n}: copied')
    text = '\n'.join(report) + '\n'
    (args.out / '_report.txt').write_text(text, encoding='utf-8')
    print(text)
    return 0


if __name__ == '__main__':
    sys.exit(main())
