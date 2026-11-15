"""Floors of ours for the main menu's 3D scenes: the game's own terrain textures on meshes of ours.

Kurast's paving and mud are painted on the town's own ground (one model, its materials blended per tile), which the
menu cannot borrow, and the game draws no copy of a game model we write (tried 2026-11-14 with the menu's own ground,
cut and whole: nothing drawn). So, as the game-drawn ceilings (prepare_game_ceiling.py): the game's lit act 1 cave rock
model takes a mesh of ours (granny_write.py), its three maps renamed to a terrain texture's, written over a developers'
test model nobody plays (the game makes a model only from a path it knows). Two kinds:
- plates (FLOORS, SHAPES): flat, square or cut to a shape - Act 3's stone under the crossing behind you;
- grounds (GROUNDS): a game model's shape (its meshes read, triangles left out) painted with a terrain texture, the
  town's flat stones laid on it - Act 3's ground before the temple, the menu's own terrain_act3 made mud.

Writes the five LOD files of each into the mod's own .mpq folder and steamvr/frontend_floors.json (which floor is which
test model, a ground's stones; prepare_frontend_scene.py and the scene editor read it; this machine's, not in git).
The test models are picked once (the first free ones, by name) and kept in that file; a floor no longer made has its
files taken out of the mod folder.

  python prepare_frontend_floor.py "<game folder>" --casclib "<...>/CascLib.dll" [--mod D2R-PCVR]
"""
import argparse
import json
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import granny_write  # noqa: E402
import prepare_game_ceiling as ceiling  # noqa: E402
from game_assets import open_assets  # noqa: E402

# (name, the terrain texture without "_ALB.texture") - the scene editor lists the plates as "podłoga"
FLOORS = (('act3_stone', 'data/hd/env/texture/terrain/act3/docktown/act3_docktown_stone_small02'),
          ('act3_ground', 'data/hd/env/texture/terrain/act3/docktown/act3_docktown_mud01'))
HALF = 40.0          # a plate: 80 x 80 round its middle, flat at y 0, seen from above
TILE = 40.0          # one picture each 40 (as the ceilings')
CELLS = 2            # cells a picture
LODS = 5
# Plates cut to the land under them: {name: (half, cells a picture, rows of '#' kept / '.' left out)}; a row is the
# plate's z (from -half), a column its x (from -half). Act 3's stone lies at right -15, ahead -70, yaw 80 in the menu
# (prepare_frontend_scene.TOWN_PLATES): a cross, as the town's ground lies (two bars in the plate's x / z: x -17..15 by
# z -30..42, x -55..30 by z -22..12), its corners between the arms water, the arm nearest you short of the pier.
SHAPES = {'act3_stone': (60.0, 8, (
    '........................',
    '........................',
    '........................',
    '........................',
    '........................',
    '........................',
    '.........######.........',
    '.........######.........',
    '.#################......',
    '.#################......',
    '.#################......',
    '.#################......',
    '.#################......',
    '.#################......',
    '.........######.........',
    '.........######.........',
    '.........######.........',
    '.........######.........',
    '.........######.........',
    '.........######.........',
    '........................',
    '........................',
    '........................',
    '........................',
))}
# Grounds of ours shaped as a game model (owner 2026-11-15): its meshes read (a LOD our reader unpacks - not terrain_act3's
# first two), the triangles named left out, painted with the floor's maps (one picture each `tile`, seen from above), the
# town's flat stones laid on it on a jittered grid, some left out (a gap of mud between). Act 3: the menu's ground
# terrain_act3 (prepare_frontend_scene.GROUND_SWAP) as mud - its fine ground before the temple (trimmed_terrainShape) and
# the coarse U round it (polySurfaceShape1) but the U's two arms, over the water at the sides (its triangles 0-3, 19-26,
# 32-42, 54-56 left; 4-18, 27-31, 43-53, 57 right); the stones on the fine ground.
GROUNDS = {'act3_ground': {
    'source': 'data/hd/env/model/ui/ui_groundplane/terrain_act3', 'lod': 3, 'tile': 20.0,
    'meshes': {'trimmed_terrainShape': (), 'polySurfaceShape1': tuple(list(range(0, 4)) + list(range(19, 27)) + list(range(32, 43))
                                                                      + [54, 55, 56] + list(range(4, 19)) + list(range(27, 32))
                                                                      + list(range(43, 54)) + [57])},
    'pavers_on': 'trimmed_terrainShape', 'paver_step': 2.8, 'paver_keep': 0.6, 'paver_scale': (0.9, 1.3),
    'pavers': tuple(f'data/hd/env/model/global/prop/act3/docktown/act3_docktown_rubble/paver0{k}.model' for k in range(1, 6))}}
REGISTRY = HERE / 'frontend_floors.json'
TESTS = 'data/hd/env/model/tests/'


def floor_mesh(half, tile, cells, mask=None):
    """A square plate round the origin, flat at y 0, seen from above (normals up, both windings), one picture a tile;
    with a mask only its '#' cells (rows z, columns x, from -half)."""
    half, tile = granny_write.f32(half), granny_write.f32(tile)
    m = granny_write.Mesh()
    m.scale = half
    w = granny_write._w(m.scale)
    n = max(1, int(round(2 * half / tile * cells)))
    step = 2 * half / n
    row = n + 1
    for i in range(n + 1):
        for j in range(n + 1):
            x, z = -half + j * step, -half + i * step
            p = (x, 0.0, z)
            pos = [granny_write.lround(p[k] / m.scale * 32767.0) for k in range(3)] + [w]
            granny_write._bound(m, p)
            m.v.append([pos, [0, 127, 0, 0], [127, 0, 0, -127],
                        [granny_write.half(granny_write.f32(x / tile)), granny_write.half(granny_write.f32(-z / tile))]])
    m.mn[1], m.mx[1] = -0.01, 0.01   # (a box, not a sheet)
    for i in range(n):
        for j in range(n):
            if mask is not None and (i >= len(mask) or j >= len(mask[i]) or mask[i][j] != '#'):
                continue
            a, b, c, d = i * row + j, i * row + j + 1, (i + 1) * row + j, (i + 1) * row + j + 1
            for t in ((a, b, c), (b, d, c)):
                m.idx += [t[0], t[1], t[2], t[0], t[2], t[1]]
    return m


def ground_mesh(assets, names, spec, seed):
    """GROUNDS: the mesh (model space of the source) and its pavers [[x, y, z, yaw degrees, scale, model], ...]."""
    import random
    import podglad_modeli
    lod = spec['lod']
    data = assets.read(names[f"{spec['source']}_lod{lod}.model".lower()])
    meshes = {n: (gr, dd) for n, gr, dd in podglad_modeli.Big(data).all_meshes()}
    verts, tris = [], []
    for mname, drop in spec['meshes'].items():
        gr, dd = meshes[mname]
        ix = [i for _, l in gr for i in l]
        base = len(verts)
        verts += dd
        drop = set(drop)
        tris += [[base + i for i in ix[t * 3:t * 3 + 3]] for t in range(len(ix) // 3) if t not in drop]
    used = sorted({i for t in tris for i in t})
    remap = {o: k for k, o in enumerate(used)}
    tile = spec['tile']
    m = granny_write.Mesh()
    m.scale = granny_write.f32(max(abs(verts[i][c]) for i in used for c in range(3)) * 1.01)
    w = granny_write._w(m.scale)
    for o in used:
        x, y, z, nx, ny, nz = verts[o][:6]
        if ny < 0:
            nx, ny, nz = -nx, -ny, -nz
        p = (x, y, z)
        pos = [granny_write.lround(p[k] / m.scale * 32767.0) for k in range(3)] + [w]
        granny_write._bound(m, p)
        m.v.append([pos, [granny_write.snorm8(nx), granny_write.snorm8(ny), granny_write.snorm8(nz), 0], [127, 0, 0, -127],
                    [granny_write.half(granny_write.f32(x / tile)), granny_write.half(granny_write.f32(-z / tile))]])
    for t in tris:
        a, b, c = (remap[i] for i in t)
        m.idx += [a, b, c, a, c, b]
    # pavers: on a grid (jittered) over the fine ground, where it is, some left out
    rnd = random.Random(seed)
    gr, dd = meshes[spec['pavers_on']]
    bins = {}
    for v in dd:
        bins.setdefault((int(v[0] // 2), int(v[2] // 2)), []).append(v)
    xs, zs = [v[0] for v in dd], [v[2] for v in dd]
    step, pavers = spec['paver_step'], []
    gx = min(xs)
    while gx <= max(xs):
        gz = min(zs)
        while gz <= max(zs):
            px, pz = gx + rnd.uniform(-0.25, 0.25) * step, gz + rnd.uniform(-0.25, 0.25) * step
            near = [v for i in (-1, 0, 1) for j in (-1, 0, 1) for v in bins.get((int(px // 2) + i, int(pz // 2) + j), ())]
            best = min(near, key=lambda v: (v[0] - px) ** 2 + (v[2] - pz) ** 2, default=None)
            keep = rnd.random() < spec['paver_keep']
            if best is not None and (best[0] - px) ** 2 + (best[2] - pz) ** 2 < 1.5 ** 2 and keep:
                pavers.append([round(px, 3), round(best[1] + 0.03, 3), round(pz, 3), round(rnd.uniform(0, 360), 1),
                               round(rnd.uniform(*spec['paver_scale']), 2), rnd.choice(spec['pavers'])])
            gz += step
        gx += step
    return m, pavers, len(tris)


def used_test_models(assets, folder=TESTS):
    """Every model of `folder` a JSON of the game names (but the LOD table and the developers' unplayed warehouses): a
    set of their paths without .model / _lodN, lower case."""
    used = set()
    rel = folder[len('data/hd/env/model/'):]
    pat = re.compile(re.escape(rel) + r'[a-z0-9_./\-]+?(?=\.model|_lod\d)')
    for name in assets.files:
        low = name.lower()
        if not (low.startswith('data/hd/') and low.endswith('.json')) or low == ceiling.LOD_TABLE or low.startswith(ceiling.UNPLAYED):
            continue
        try:
            data = assets.read(name)
        except (ValueError, KeyError, OSError):
            continue
        if len(data) > 16 * 1024 * 1024:
            continue
        used |= {'data/hd/env/model/' + h for h in pat.findall(data.decode('utf-8', 'replace').lower())}
    return used


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('game', type=Path)
    ap.add_argument('--casclib', type=Path, default=None)
    ap.add_argument('--mod', default='D2R-PCVR')
    a = ap.parse_args()
    registry = json.loads(REGISTRY.read_text(encoding='utf-8')) if REGISTRY.exists() else {}
    root = a.game / 'mods' / a.mod / f'{a.mod}.mpq'
    with open_assets(a.game, a.casclib) as assets:
        names = {n.lower(): n for n in assets.files}
        if ceiling.SOURCE.lower() not in names:
            raise SystemExit("act 1's cave rock model is not in this game")
        rock = assets.read(names[ceiling.SOURCE.lower()])
        taken = {ceiling.model_base(k[3]).lower() for k in ceiling.KINDS} | \
                {ceiling.model_base(k[4]).lower() for k in ceiling.KINDS if k[4]} | \
                {v['base'].lower() for v in registry.values()}
        free = None
        for name, maps in FLOORS:
            if not all(f'{maps}_{w}.texture'.lower() in names for w in ('alb', 'nrm', 'orm')):
                print(f'{name}: its maps are not in this game - left out')
                continue
            if name not in registry:
                if free is None:
                    print('looking for free test models (reads every JSON of the game, a minute)...', flush=True)
                    used = used_test_models(assets)
                    bases = sorted({n[:-len('_lod0.model')] for n in names if n.startswith(TESTS) and n.endswith('_lod0.model')})
                    free = [b for b in bases if b not in used and b not in taken
                            and all(f'{b}_lod{k}.model' in names for k in range(LODS))]
                if not free:
                    raise SystemExit('no free test model left')
                base = names[free.pop(0) + '_lod0.model'][:-len('_lod0.model')]
                taken.add(base.lower())
                registry[name] = {'base': base, 'model': base + '.model', 'maps': maps}
            half, cells, mask = SHAPES.get(name, (HALF, CELLS, None))
            registry[name].update(half=half, tile=TILE)
            base = registry[name]['base']
            renames = [(ceiling.SOURCE_MAPS[i], f'{maps}_{w}.texture') for i, w in enumerate(('ALB', 'NRM', 'ORM'))]
            if name in GROUNDS:
                try:
                    mesh, pavers, ntri = ground_mesh(assets, names, GROUNDS[name], 7)
                except Exception as e:   # noqa: BLE001 - its source not readable here: left out
                    print(f'{name}: its source not read ({type(e).__name__}: {e}) - left out')
                    registry.pop(name, None)
                    continue
                registry[name].update(ground=True, tile=GROUNDS[name]['tile'], pavers=pavers)
                registry[name].pop('half', None)
                shape = f'{ntri} triangles of {GROUNDS[name]["source"].rsplit("/", 1)[-1]}, {len(pavers)} stones on it'
            else:
                mesh = floor_mesh(half, TILE, cells, mask)
                shape = f'{2 * half:.0f} x {2 * half:.0f}' + (', cut to its land' if mask else '')
            granny_write.salt(mesh, 101 + FLOORS.index((name, maps)))
            for lod in range(LODS):
                f = granny_write.File(rock)
                granny_write.remesh(f, mesh, renames, lod)
                out = root / f'{base}_lod{lod}.model'
                out.parent.mkdir(parents=True, exist_ok=True)
                out.write_bytes(granny_write.write(f))
            print(f'{name}: {maps.rsplit("/", 1)[-1]} -> {base}.model ({shape})')
        # floors no longer made (and earlier tries): their files out of the mod folder, out of the registry
        made = {n for n, _ in FLOORS}
        for name in [n for n in registry if n not in made]:
            base = registry.pop(name)['base']
            gone = 0
            for lod in range(LODS):
                old = root / f'{base}_lod{lod}.model'
                if old.is_file():
                    old.unlink()
                    gone += 1
            print(f'{name}: no longer made - {gone} files of {base.rsplit("/", 1)[-1]} taken out of the mod folder')
    REGISTRY.write_text(json.dumps(registry, indent=1) + '\n', encoding='utf-8')
    print(f'gotowe - {REGISTRY.name}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
