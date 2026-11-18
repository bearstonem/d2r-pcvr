"""Diagnostics: a few of Act 2's presets as they are, and the size of some models, for the main menu's Act 2 scene.

For prepare_frontend_scene.py (owner 2026-11-14): the road out of Lut Gholein (the town's west and north ways out,
transw2 / transn1, with their curbs), the town itself (lutw / lutn), the Lost City's ruins and the Far Oasis's cliffs:
their presets copied raw (to read how the game lays its roads, curbs and cliffs), and for every model they place,
plus their own terrain, its extent (the bounding box of its first level of detail) - to lay ours at the right size.
Reads the player's own archives; writes only into the output folder (keep it out of git: extracted/ is the game's).

  python dump_act2_presets.py "<game folder>" --casclib "<...>/CascLib.dll" --out "<folder>"
"""
import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from game_assets import open_assets  # noqa: E402
import potion_assets  # noqa: E402

PRESETS = ('town/transw2', 'town/transn1', 'town/lutw', 'town/lutn', 'outdoors/ruin1', 'outdoors/ruin3',
           'outdoors/oasis5', 'outdoors/oasis10', 'outdoors/edge1', 'outdoors/edge12', 'outdoors/mesarm')
EXTRA = ('data/hd/env/model/act2/town/act2_town_palace_walls/tower01.model',
         'data/hd/env/model/act2/town/act2_town_palace_structures/spire01.model',
         'data/hd/env/model/ui/ui_act2_vignette/spire01_low.model',
         'data/hd/env/model/ui/ui_act2_vignette/ui_act2_vignette_terrain.model',
         'data/hd/env/model/ui/ui_act2_vignette/mound01_low.model',
         'data/hd/env/model/ui/ui_act2_vignette/architecture/ui_wall01.model',
         'data/hd/env/model/global/prop/act2/town/act2_town_stone_trims/cobblestone_single01.model',
         'data/hd/env/model/global/prop/act2/town/act2_town_stone_trims/cobblestone_pile01.model',
         'data/hd/env/model/global/prop/act2/town/act2_town_stone_trims/cobblestone_pile02.model')


class Big(potion_assets.Granny):
    """The potion reader, without its potion-sized limits (terrain is large); positions only."""

    def array(self, field):
        kind, sub, _, ref = field
        if kind == 7:
            sub = self.ptr(ref)
            ref = (ref[0], ref[1] + 8)
        elif kind not in (3, 4):
            raise ValueError('Expected Granny array')
        count, = self.read(ref, '<I')
        base = self.ptr(ref, 4)
        if count and base is None:
            raise ValueError('Missing Granny array data')
        stride = 8 if kind == 4 else self.definition(sub)[1]
        refs = [(base[0], base[1] + i * stride) for i in range(count)]
        return [self.fields(self.ptr(r) if kind == 4 else r, sub) for r in refs]

    def bounds(self):
        lo, hi, n = [1e30] * 3, [-1e30] * 3, 0
        for mesh in self.array(self.root['Meshes']):
            for v in self.array(self.reference(mesh['PrimaryVertexData'])['Vertices']):
                p = self.scalar(v['Position'])[:3]
                for k in range(3):
                    lo[k], hi[k] = min(lo[k], p[k]), max(hi[k], p[k])
                n += 1
        return lo, hi, n


def model_files(node):
    if isinstance(node, dict):
        f = node.get('filename')
        if isinstance(f, str) and f.lower().endswith('.model'):
            yield f.lower()
        for v in node.values():
            yield from model_files(v)
    elif isinstance(node, list):
        for v in node:
            yield from model_files(v)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('game', type=Path)
    ap.add_argument('--casclib', type=Path, default=None)
    ap.add_argument('--out', type=Path, required=True)
    args = ap.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    potion_assets.LIMIT = 256 * 1024 * 1024
    models, report = set(EXTRA), []
    with open_assets(args.game, args.casclib) as assets:
        names = set(assets.files)
        for p in PRESETS:
            n = f'data/hd/env/preset/act2/{p}.json'
            try:
                raw = assets.read(n)
            except Exception as e:   # noqa: BLE001
                report.append(f'{n}: not read ({e})')
                continue
            (args.out / (p.replace('/', '__') + '.json')).write_bytes(raw)
            preset = json.loads(raw)
            found = set(model_files(preset))
            models |= found
            report.append(f'{n}: {len(preset.get("entities", []))} entities, {len(found)} models')
            print(f'{n} copied', flush=True)
        report.append('\nModels (min x y z | max x y z | vertices):')
        for i, m in enumerate(sorted(models), 1):
            lod0 = m[:-len('.model')] + '_lod0.model'
            f = lod0 if lod0 in names else m
            try:
                lo, hi, n = Big(assets.read(f)).bounds()
                report.append(f'  {m} | {lo[0]:.2f} {lo[1]:.2f} {lo[2]:.2f} | {hi[0]:.2f} {hi[1]:.2f} {hi[2]:.2f} | {n}')
            except Exception as e:   # noqa: BLE001 - listing only
                report.append(f'  {m} | not measured ({type(e).__name__}: {e})')
            if i % 25 == 0:
                print(f'  {i}/{len(models)} models measured', flush=True)
    (args.out / '_sizes.txt').write_text('\n'.join(report) + '\n', encoding='utf-8')
    print(f'done - {args.out}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
