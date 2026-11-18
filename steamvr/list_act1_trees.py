"""Diagnostics: Act 1's trees in the game's archives - which tree models there are and which presets use them.

For the main menu's forest (prepare_frontend_scene.py): every tree model of Act 1's foliage (its first
level of detail), and for each how many times the presets of Act 1 place it, in which presets the most,
with the size they give it - to pick the Dark Wood's own leafy trees. Reads the player's own archives;
writes only the report.

  python list_act1_trees.py "<game folder>" --casclib "<...>/bin/CascLib.dll" --out "<file>"
"""
import argparse
import collections
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from game_assets import open_assets  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('game', type=Path)
    ap.add_argument('--casclib', type=Path, default=None)
    ap.add_argument('--out', type=Path, required=True)
    args = ap.parse_args()
    out = []
    with open_assets(args.game, args.casclib) as assets:
        names = sorted(assets.files)
        trees = [n for n in names if n.startswith('data/hd/env/') and 'tree' in n and n.endswith('_lod0.model')]
        out.append(f'{len(trees)} tree models (lod0):')
        out += [f'  {n}' for n in trees]
        presets = [n for n in names if n.startswith('data/hd/env/preset/act1/') and n.endswith('.json')]
        out.append(f'\n{len(presets)} Act 1 presets; those naming wood / forest / tree:')
        out += [f'  {n}' for n in presets if re.search('wood|forest|tree', n)]
        uses = collections.Counter()
        where = collections.defaultdict(collections.Counter)
        scales = collections.defaultdict(list)
        for n in presets:
            try:
                preset = json.loads(assets.read(n))
            except Exception:   # noqa: BLE001 - listing only
                continue
            for e in preset.get('entities', []) or []:
                comps = e.get('components', []) if isinstance(e, dict) else []
                model = next((str(c.get('filename', '')).lower() for c in comps if c.get('type') == 'ModelDefinitionComponent'), '')
                if 'tree' not in model:
                    continue
                uses[model] += 1
                where[model][n] += 1
                t = next((c for c in comps if c.get('type') == 'TransformDefinitionComponent'), None)
                if t and isinstance(t.get('scale'), dict):
                    scales[model].append(float(t['scale'].get('y', 1.0)))
        out.append('\nTree models placed in Act 1 presets (times | model | typical size | presets using it most):')
        for model, count in uses.most_common():
            s = sorted(scales[model]) or [1.0]
            top = ', '.join(f'{p.rsplit("/", 1)[-1]} {c}' for p, c in where[model].most_common(4))
            out.append(f'  {count:5d} | {model} | {s[len(s) // 2]:.2f} ({s[0]:.2f}-{s[-1]:.2f}) | {top}')
    text = '\n'.join(out) + '\n'
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(text, encoding='utf-8')
    print(f'{len(trees)} tree models, {len(uses)} placed in Act 1 - report: {args.out}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
