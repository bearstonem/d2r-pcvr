"""Diagnostics: Kurast Docks (Act 3's town) as the game lays it, for the main menu's Act 3 scene.

For prepare_frontend_scene.py (owner 2026-11-14, "Pelne 3D dla scen: Akt 3"): the menu's Act 3 hero stands on a broken
dock over the water; behind the player goes a piece of Kurast Docks - the pier on the town map's left side that you run
along from the ship to Cain. This copies, raw:
  - every preset of the docks town (data/hd/env/preset/act3/docktown/...), and the prefabs they place;
  - a list of every Act 3 preset (to pick more later: the jungle's edge, the river);
  - every Act 3 / docktown model folder with the models in it (what can be added in the editor);
  - for every model those presets and the menu's Act 3 place: its extent (the bounding box of its first LOD).
Reads the player's own archives; writes only into the output folder (keep it out of git: extracted/ is the game's).

  python dump_act3_presets.py "<game folder>" --casclib "<...>/CascLib.dll" --out extracted\\act3
"""
import argparse
import collections
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from game_assets import open_assets  # noqa: E402
import potion_assets  # noqa: E402

PRESET_DIR = 'data/hd/env/preset/act3/'
TOWN = ('data/hd/env/preset/act3/docktown/',)
MENU = 'data/hd/env/preset/ui/frontendact2.json'
MENU_ACT3 = ('/ui_act3_vignette/', '/act3', 'act3_')
MODEL_LIST = ('data/hd/env/model/act3/', 'data/hd/env/model/global/prop/act3/', 'data/hd/env/foliage/act3/',
              'data/hd/env/foliage/global/model/act3_', 'data/hd/env/model/ui/ui_act3_vignette/')


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


def files_in(node, ext):
    if isinstance(node, str):
        if node.lower().startswith('data/') and node.lower().endswith(ext):
            yield node.lower()
    elif isinstance(node, dict):
        for v in node.values():
            yield from files_in(v, ext)
    elif isinstance(node, list):
        for v in node:
            yield from files_in(v, ext)


def flat(name):
    return name.split('/preset/act3/', 1)[-1].split('/env/', 1)[-1].replace('/', '__')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('game', type=Path)
    ap.add_argument('--casclib', type=Path, default=None)
    ap.add_argument('--out', type=Path, required=True)
    args = ap.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    (args.out / 'prefabs').mkdir(exist_ok=True)
    potion_assets.LIMIT = 256 * 1024 * 1024
    report = []
    with open_assets(args.game, args.casclib) as assets:
        names = {n.lower(): n for n in assets.files}

        def read(n):
            return assets.read(names.get(n.lower(), n))

        presets = sorted(n for n in names if n.startswith(PRESET_DIR) and n.endswith('.json'))
        (args.out / '_presets.txt').write_text('\n'.join(presets) + '\n', encoding='utf-8')
        report.append(f'{len(presets)} Act 3 presets (all listed in _presets.txt)')
        models, prefabs, seen = set(), [], set()
        for n in presets:
            if not n.startswith(TOWN):
                continue
            raw = read(n)
            (args.out / (flat(n))).write_bytes(raw)
            p = json.loads(raw)
            found = set(files_in(p, '.model'))
            models |= found
            prefabs += [f for f in files_in(p, '.json') if f not in seen]
            seen |= set(prefabs)
            report.append(f'{n}: {len(p.get("entities", []))} entities, {len(found)} models')
            print(f'{n} copied', flush=True)
        menu = json.loads(read(MENU))
        mm = {m for m in files_in(menu, '.model') if any(k in m for k in MENU_ACT3)}
        models |= mm
        prefabs += [f for f in files_in(menu, '.json') if 'act3' in f and f not in seen]
        seen |= set(prefabs)
        report.append(f'the menu: {len(mm)} Act 3 models')
        while prefabs:   # prefabs (torches, boats, grass at the water...) and the prefabs in them
            f = prefabs.pop()
            if f not in names:
                report.append(f'  prefab {f}: not in the archives')
                continue
            try:
                raw = read(f)
                (args.out / 'prefabs' / flat(f)).write_bytes(raw)
                p = json.loads(raw)
            except Exception as e:   # noqa: BLE001 - listing only
                report.append(f'  prefab {f}: not read ({e})')
                continue
            models |= set(files_in(p, '.model'))
            new = [g for g in files_in(p, '.json') if g not in seen]
            seen |= set(new)
            prefabs += new
        report.append(f'{len(seen)} prefabs copied (prefabs/)')
        listed = sorted(n for n in names if n.endswith('.model') and n.startswith(MODEL_LIST) and '_lod' not in n)
        folders = collections.defaultdict(list)
        for n in listed:
            folders[n.rsplit('/', 1)[0]].append(n.rsplit('/', 1)[1][:-len('.model')])
        report.append(f'\n{len(listed)} Act 3 models in {len(folders)} folders:')
        for k in sorted(folders):
            report.append(f'  {k}/ ({len(folders[k])}): {", ".join(folders[k])}')
        report.append('\nModels placed (min x y z | max x y z | vertices):')
        for i, m in enumerate(sorted(models), 1):
            lod0 = m[:-len('.model')] + '_lod0.model'
            f = lod0 if lod0 in names else m
            try:
                lo, hi, n = Big(read(f)).bounds()
                report.append(f'  {m} | {lo[0]:.2f} {lo[1]:.2f} {lo[2]:.2f} | {hi[0]:.2f} {hi[1]:.2f} {hi[2]:.2f} | {n}')
            except Exception as e:   # noqa: BLE001 - listing only
                report.append(f'  {m} | not measured ({type(e).__name__}: {e})')
            if i % 50 == 0:
                print(f'  {i}/{len(models)} models measured', flush=True)
    (args.out / '_sizes.txt').write_text('\n'.join(report) + '\n', encoding='utf-8')
    print(f'done - {args.out}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
