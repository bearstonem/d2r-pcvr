"""Diagnostics: Act 2's buildings and small creatures in the game's archives, for the main menu's Act 2 scene.

For prepare_frontend_scene.py (owner 2026-11-14): a tower of Lut Gholein to stand behind the wall, and the act's
critters (beetles, scarabs, birds...) if any can be placed in a preset. Lists
  - every model of Act 2 (and the UI vignette) whose name speaks of a tower, minaret, dome, palace...;
  - the models Act 2's town presets place, the most used first, with their size;
  - every file whose name speaks of a small creature, and the presets placing it (with the component's JSON).
Reads the player's own archives; writes only the report.

  python list_act2_models.py "<game folder>" --casclib "<...>/bin/CascLib.dll" --out "<file>"
"""
import argparse
import collections
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from game_assets import open_assets  # noqa: E402

BUILDING = re.compile(r'tower|minaret|dome|spire|turret|palace|temple|mosque|keep|gate|arch|roof|building|house|wall|'
                      r'cliff|rock|mesa|ledge|ridge|canyon|plateau|boulder|stone|'
                      r'cobble|pave|road|path|street|tile|floor|brick|plaza|step|stair|curb|trim', re.I)
# the presets whose models are counted: Lut Gholein, the Far Oasis, the Lost City, the desert round them
PLACES = re.compile(r'town|lutgholein|lut_gholein|oasis|lost|city|desert|outdoor|valley', re.I)
CRITTER = re.compile(r'beetle|scarab|bug|insect|critter|lizard|snake|serpent|rat[_s0-9.]|crab|vulture|bird|butterfly|moth|'
                     r'roach|spider|worm|maggot|flies|fly[_0-9.]|swarm|locust|fish|seagull|gull|crow|bat[_s0-9.]|ambient', re.I)


def comps_of(e):
    return e.get('components', []) if isinstance(e, dict) else []


def paths(node):
    if isinstance(node, str):
        if node.lower().startswith('data/'):
            yield node.lower()
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
    ap.add_argument('--out', type=Path, required=True)
    args = ap.parse_args()
    out = []
    with open_assets(args.game, args.casclib) as assets:
        names = sorted(assets.files)
        act2 = [n for n in names if n.endswith('.model') and ('act2' in n or '/ui_act2' in n) and n.startswith('data/hd/env/')]
        out.append(f'{len(act2)} Act 2 models; those naming a building part, rock, cliff or paving:')
        out += [f'  {n}' for n in act2 if BUILDING.search(n.rsplit('/', 1)[-1])]
        out.append('\nAct 2 model folders (models in each):')
        folders = collections.Counter(n.rsplit('/', 1)[0] for n in act2)
        out += [f'  {c:4d} {f}' for f, c in sorted(folders.items())]

        presets = [n for n in names if n.startswith('data/hd/env/preset/act2/') and n.endswith('.json')]
        out.append(f'\n{len(presets)} Act 2 presets; the town, the oasis, the lost city and the desert:')
        town = [n for n in presets if PLACES.search(n)]
        out += [f'  {n}' for n in town]
        uses, scales, where = collections.Counter(), collections.defaultdict(list), collections.defaultdict(collections.Counter)
        critters = collections.defaultdict(list)
        critter_files = [n for n in names if CRITTER.search(n.rsplit('/', 1)[-1])]
        for n in presets + [p for p in names if p.startswith('data/hd/env/preset/') and p.endswith('.json') and p not in presets]:
            try:
                preset = json.loads(assets.read(n))
            except Exception:   # noqa: BLE001 - listing only
                continue
            for e in preset.get('entities', []) or []:
                for c in comps_of(e):
                    for p in paths(c):
                        if CRITTER.search(p.rsplit('/', 1)[-1]) and len(critters[p]) < 3:
                            critters[p].append((n, json.dumps(c)[:600]))
                if n not in presets or n not in town:
                    continue
                model = next((str(c.get('filename', '')).lower() for c in comps_of(e) if c.get('type') == 'ModelDefinitionComponent'), '')
                if not model:
                    continue
                uses[model] += 1
                where[model][n] += 1
                t = next((c for c in comps_of(e) if c.get('type') == 'TransformDefinitionComponent'), None)
                if t and isinstance(t.get('scale'), dict):
                    scales[model].append(float(t['scale'].get('y', 1.0)))
        decals = collections.Counter()
        for n in town:
            try:
                preset = json.loads(assets.read(n))
            except Exception:   # noqa: BLE001
                continue
            for e in preset.get('entities', []) or []:
                for c in comps_of(e):
                    if c.get('type') == 'DecalDefinitionComponent':
                        decals[str(c.get('diffuseMap', '')).lower()] += 1
        out.append('\nDecals placed in those presets (times | texture):')
        out += [f'  {c:5d} | {d}' for d, c in decals.most_common(150)]
        out.append('\nModels placed in those presets (times | model | typical size | presets using it most):')
        for model, count in uses.most_common(600):
            s = sorted(scales[model]) or [1.0]
            top = ', '.join(f'{p.rsplit("/", 1)[-1]} {c}' for p, c in where[model].most_common(3))
            out.append(f'  {count:5d} | {model} | {s[len(s) // 2]:.2f} ({s[0]:.2f}-{s[-1]:.2f}) | {top}')
        out.append(f'\n{len(critter_files)} files naming a small creature:')
        out += [f'  {n}' for n in critter_files[:1500]]
        out.append('\nCreature files placed by presets (file | preset | component):')
        for p, places in sorted(critters.items()):
            for n, c in places:
                out.append(f'  {p}\n      {n}\n      {c}')
    text = '\n'.join(out) + '\n'
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(text, encoding='utf-8')
    print(f'{len(act2)} Act 2 models, {len(uses)} placed in its town, {len(critter_files)} creature files - report: {args.out}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
