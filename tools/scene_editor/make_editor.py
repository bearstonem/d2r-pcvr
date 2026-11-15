"""The scene editor's page for one act of the main menu (tools/scene_editor): the act's pieces from a built preset
(prepare_frontend_scene.py's output) in its camera's frame, ready to drag, turn, copy and remove in a browser; its
'Zapisz' writes the owner's changes (akt<N>_zmiany.json) - as steamvr/frontend_edits_act<N>.json they are applied by
prepare_frontend_scene.py over what it lays.

  python make_editor.py <built frontendact2.json> <out.html> [--act 2] [--version v19] [--transw2 <preset json>]
         [--game "<folder gry>" --casclib "<...>/CascLib.dll"]

With --game the page also carries the models themselves (light LODs, small textures, read from the player's archives by
steamvr/podglad_modeli.py): a 3D preview of the selected / chosen piece and true outlines on the map from each model's
bounds. The page then holds game data - keep it out of git (it is generated next to the template only on the owner's PC).
"""
import argparse
import base64
import importlib.util
import json
import math
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
FOOT = {'ui_wall01': (20, 2.5), 'ui_wall01_cheap': (20, 2.5), 'ui_entrance01': (18, 6), 'border02': (31, 62),
        'border01': (40, 30), 'tower01': (10, 10), 'ui_act2_vignette_terrain': (150, 150), 'act2_groundplane': (300, 300)}
DEFAULT = {'palmy': (6, 6), 'skały': (8, 8), 'rośliny i piasek': (2.5, 2.5), 'miasto (karty)': (0.5, 0.15), 'budowle': (4, 4)}


# what the owner can add (2026-11-14: the desert's heads and statues, the Lost City's tablets, grass, barrels...)
ADD_FOLDERS = ('data/hd/env/model/act2/ruin/act2_ruin_statues/', 'data/hd/env/model/act2/ruin/act2_ruin_tablets/',
               'data/hd/env/model/act2/ruin/act2_ruin_columns/', 'data/hd/env/model/act2/ruin/act2_ruin_pillars/',
               'data/hd/env/model/act2/ruin/act2_ruin_stone_fences/', 'data/hd/env/model/act2/ruin/act2_ruin_wagon_destroyed/',
               'data/hd/env/model/act2/arcane/act2_arcane_statues/',
               'data/hd/env/model/act2/outdoors/', 'data/hd/env/model/global/prop/act2/outdoors/',
               'data/hd/env/model/global/prop/act2/town/act2_town_shop_props/',
               'data/hd/env/model/global/prop/act2/town/act2_town_tavern_props/',
               'data/hd/env/model/global/prop/act2/town/act2_town_bottles/',
               'data/hd/env/model/global/prop/act2/town/act2_town_wood_props/',
               'data/hd/env/model/global/prop/act2/town/act2_town_plank_props/',
               'data/hd/env/model/global/prop/act2/tomb/act2_tomb_sand_piles/',
               'data/hd/env/foliage/act2/model/', 'data/hd/env/foliage/global/model/act2_',
               # floors (2026-11-14: "only two floor textures?"): tomb stone tiles, patterns, carpets, the menu's grounds
               'data/hd/env/model/global/prop/act2/tomb/act2_tomb_stone_tiles',
               'data/hd/env/model/act2/arcane/act2_arcane_floor_patterns/',
               'data/hd/env/model/act2/palace/harem/act2_palace_harem_floor_patterns/',
               'data/hd/env/model/act2/palace/harem/act2_palace_harem_carpets/',
               'data/hd/env/model/act2/town/act2_town_palace_interior/floor',
               'data/hd/env/model/act2/tomb/act2_tomb_tyrael_tiles/',
               'data/hd/env/model/ui/ui_groundplane/',
               # large sand to lay as floor (Lut Gholein's sand skirts and piles)
               'data/hd/env/model/global/prop/act2/town/act2_town_sandskirts/',
               'data/hd/env/model/global/prop/act2/town/act2_town_sand_piles/')
# Act 3 (2026-11-14, Kurast Docks behind you): the docks town's own folders, the jungle's plants, the menu's Act 3 pieces
ADD_FOLDERS_ACT = {3: ('data/hd/env/model/act3/docktown/', 'data/hd/env/model/global/prop/act3/docktown/',
                       'data/hd/env/model/global/prop/act3/jungle/', 'data/hd/env/model/act3/jungle/act3_jungle_riverbanks/',
                       'data/hd/env/foliage/act3/model/', 'data/hd/env/foliage/global/model/act3_',
                       'data/hd/env/model/ui/ui_act3_vignette/', 'data/hd/env/model/ui/ui_groundplane/')}
CUTS = {2: [-115, 211], 3: [-220, 220]}
UI_GROUNDS = 'data/hd/env/model/ui/'   # + every act's vignette ground (ui_actN_vignette*/...terrain...)
ADD_STATUES = ('act2_statue0', )   # data/hd/env/model/global/prop/objects/statues/act2_statue0N
PREVIEW_VERTS, PREVIEW_TEX = 6000, 128


def floor_plates():
    """Our floor plates (steamvr/prepare_frontend_floor.py): {model path, lower case: (maps, half size)} - not its
    grounds (a game model's shape, put in the menu model's place by the scene script, not by the editor)."""
    f = HERE.parent.parent / 'steamvr' / 'frontend_floors.json'
    try:
        return {v['model'].lower(): (v['maps'], float(v['half'])) for v in json.loads(f.read_text(encoding='utf-8')).values()
                if 'maps' in v and 'half' in v and not v.get('ground')}
    except (OSError, ValueError, KeyError, TypeError, AttributeError):
        return {}


FLOORS = floor_plates()


def cat(m):
    if m.lower() in FLOORS:
        return 'podłoga'
    s = m.rsplit('/', 1)[-1]
    if 'water' in s and ('ui_act3_vignette' in m or 'scum' in m):
        return 'teren'
    if any(k in m for k in ('docktown_docks/', 'dock_pieces/', 'meshif_dock', 'ui_act3_vignette/dock', 'ui_act3_vignette/broken_dock',
                            'ui_act3_vignette/plank', 'ui_act3_vignette/stairs')):
        return 'pomosty'
    if 'act3' in m and any(k in m for k in ('_huts', 'hut_pieces', 'walls_', 'pillars', 'meshif_hut', '_gate', 'stone_wall', 'pyramid')):
        return 'budowle'
    if 'act3' in m and any(k in m for k in ('ivy', 'roots', 'fern', 'weeds', 'cinchona', 'vines', 'epiphyte', 'elephant', 'banyan',
                                            'cypress', 'treeline', 'sapling', 'clover', 'grass', 'bushes', 'leaves', 'cluster')):
        return 'rośliny i piasek'
    if 'act3' in m and any(k in m for k in ('rocks', 'rubble', 'mound', 'boulder')):
        return 'skały'
    if any(k in m for k in ('ruin_statues', 'arcane_statues', 'objects/statues', 'ruin_tablets', 'ruin_columns',
                            'ruin_pillars', 'stone_fences', 'wagon_destroyed')):
        return 'posągi i ruiny'
    if any(k in m for k in ('stone_tiles', 'floor_pattern', 'carpets', 'tyrael_tiles', 'palace_interior/floor', 'sand_tile')):
        return 'podłoga'
    if 'terrain' in s or 'groundplane' in s:
        return 'teren'
    if any(k in s for k in ('wall01', 'entrance', 'pillar', 'tower', 'spire', 'window')):
        return 'budowle'
    if 'plate0' in s:
        return 'miasto (karty)'
    if any(k in s for k in ('border', 'cliff', 'boulder', 'rock', 'large0', 'medium0', 'small0')):
        return 'skały'
    if 'palm' in s and 'bush' not in s:
        return 'palmy'
    if any(k in s for k in ('grass', 'bush', 'tulip', 'cact', 'mound', 'sand_pile', 'dune')):
        return 'rośliny i piasek'
    if 'trim' in s or 'cobble' in s:
        return 'kamienie bruku'
    return 'rzeczy'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('built', type=Path, help="a built preset WITHOUT the owner's changes; with --game: ignored, the act is built afresh")
    ap.add_argument('out', type=Path)
    ap.add_argument('--act', type=int, default=2)
    ap.add_argument('--version', default='')
    ap.add_argument('--transw2', type=Path)
    ap.add_argument('--game', type=Path)
    ap.add_argument('--casclib', type=Path)
    ap.add_argument('--edits', type=Path, default=None,
                    help="the owner's changes to start from (default: steamvr/frontend_edits_act<N>.json, if there)")
    a = ap.parse_args()
    spec = importlib.util.spec_from_file_location('pfs', HERE.parent.parent / 'steamvr' / 'prepare_frontend_scene.py')
    pfs = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(pfs)
    cx, cy, cz = pfs.CAMERAS[a.act]
    if a.game:
        # The act as the script lays it WITHOUT the owner's changes (2026-11-14: the game's preset already held them
        # once the owner built the game, so the editor took them for the base and its changes file doubled them).
        sys.path.insert(0, str(HERE.parent.parent / 'steamvr'))
        from game_assets import open_assets
        with open_assets(a.game, a.casclib) as assets:
            raw = assets.read(pfs.PRESET)
            sources = {w: assets.read(w) for w in pfs.source_presets()}
            built, _ = pfs.prepare(raw, sources=sources, edits_files={act: HERE / '__no_edits__' for act in pfs.ACTS},
                                   assets=assets)
        b = json.loads(built)
        print("the act built afresh from the game's archives, without the owner's changes")
    else:
        b = json.loads(a.built.read_text(encoding='utf-8'))
        if any(str(e.get('name', '')).startswith('pcvr_ed_') for e in b['entities']):
            sys.exit("this preset already holds the editor's changes (built with them) - give --game, or a preset built "
                     "without them (prepare_frontend_scene.py --edits <a missing file>)")
    items, models = [], set()
    for e in b['entities']:
        t, m, n = pfs.transform(e), pfs.model_of(e), e.get('name', '')
        if not t or not m:
            continue
        x, z = float(t['position']['x']), float(t['position']['z'])
        r, ah = cz - z, cx - x
        if not (-300 <= r <= 300 and -420 <= ah <= 260) or abs(z - cz) > 125 + 300 and False:
            continue
        if z > cz + 125 or n.startswith('pcvr_tile_'):
            continue   # (another act's; the road's curbs: laid by the script along the road)
        s = m.rsplit('/', 1)[-1].replace('.model', '')
        if s in ('terrain', 'act5_frontend_terrain', 'terrain_act1_path01', 'blocker_disk01'):
            continue
        q = t.get('orientation') or {}
        c = cat(m)
        w, d = FOOT.get(s, DEFAULT.get(c, (2, 2)))
        items.append(dict(name=n, model=m, short=s, cat=c, right=round(r, 2), ahead=round(ah, 2),
                          y=round(float(t['position']['y']), 2),
                          yaw=round(math.degrees(2 * math.atan2(float(q.get('y', 0)), float(q.get('w', 1)))), 1),
                          scale=round(float((t.get('scale') or {}).get('x', 1)), 3), w=w, d=d))
        models.add(m)
    if a.transw2 and a.transw2.exists():
        tw = json.loads(a.transw2.read_text(encoding='utf-8'))
        models |= {c['filename'] for e in tw['entities'] for c in e['components']
                   if c.get('type') == 'ModelDefinitionComponent' and 'terrain' not in c['filename']}
    if a.act == 2:
        bc = 'data/hd/env/model/act2/bigcliff/'
        models |= {f'{bc}act2_bigcliff_cliffs/{k}.model' for k in ['border01', 'border02'] + [f'cliff0{i}' for i in range(1, 9)]}
        models |= {f'{bc}act2_bigcliff_cliff_edges/cliff_edge0{i}.model' for i in range(1, 6)}
        models |= {f'{bc}act2_bigcliff_boulders/boulder0{i}.model' for i in range(1, 5)}
    models |= {v for v in FLOORS}   # our floor plates, in every act's list
    folders = ADD_FOLDERS_ACT.get(a.act, ADD_FOLDERS)
    geo, assets_models = None, set()
    if a.game:
        sys.path.insert(0, str(HERE.parent.parent / 'steamvr'))
        from game_assets import open_assets
        import potion_assets
        import podglad_modeli
        potion_assets.LIMIT = 256 * 1024 * 1024
        with open_assets(a.game, a.casclib) as assets:
            names = {n.lower(): n for n in assets.files}
            albs = [n for n in names if n.endswith('_alb.texture')]
            for n in names:
                if n.endswith('.model') and '_lod' not in n.rsplit('/', 1)[-1] or n.endswith('_lod0.model'):
                    pass
                else:
                    continue
                if n.endswith('.model') and (n.startswith(folders) or
                                             (n.startswith(UI_GROUNDS) and 'terrain' in n.rsplit('/', 1)[-1]) or
                                             (a.act == 2 and 'objects/statues/' in n and n.rsplit('/', 1)[-1].startswith(ADD_STATUES))):
                    assets_models.add(podglad_modeli.strip_lod(n))
            models |= assets_models
            want = {m.lower() for m in models} | {i['model'].lower() for i in items}
            want = {m for m in want if m.rsplit('/', 1)[-1] not in ('terrain.model', 'act5_frontend_terrain.model')
                    and m not in FLOORS}
            print(f'{len(want)} models to read for the preview...', flush=True)
            report = []
            geo_models, textures = podglad_modeli.extract(assets, names, albs, sorted(want), report,
                                                          max_verts=PREVIEW_VERTS, tex_size=PREVIEW_TEX,
                                                          log=lambda t: print(t, flush=True))
            for m, (maps, h) in FLOORS.items():   # a plate: two triangles, its terrain picture once a 40
                try:
                    w_, h_, rgba = podglad_modeli.decode_texture(assets.read(names[f'{maps}_alb.texture'.lower()]), PREVIEW_TEX)
                    textures.append('data:image/png;base64,' + base64.b64encode(podglad_modeli.png(w_, h_, rgba)).decode())
                    t = len(textures) - 1
                except Exception:   # noqa: BLE001
                    t = None
                r = h / 40.0
                v = [(-h, 0, -h, 0, 1, 0, 0, r), (h, 0, -h, 0, 1, 0, 2 * r, r), (h, 0, h, 0, 1, 0, 2 * r, -r), (-h, 0, h, 0, 1, 0, 0, -r)]
                vb = base64.b64encode(struct.pack(f'<{len(v) * 8}f', *[x for p in v for x in p])).decode()
                ib = base64.b64encode(struct.pack('<12I', 0, 1, 2, 0, 2, 3, 0, 2, 1, 0, 3, 2)).decode()
                geo_models[m] = {'parts': [{'v': vb, 'i': ib, 't': t, 'mesh': 'plate', 'mat': maps.rsplit('/', 1)[-1]}],
                                 'lo': [-h, 0.0, -h], 'hi': [h, 0.05, h], 'lod': 'plate', 'from': [], 'tex': [maps.rsplit('/', 1)[-1]]}
            geo = {'models': geo_models, 'textures': textures}
            (a.out.with_suffix('.txt')).write_text('\n'.join(report) + '\n', encoding='utf-8')
    models = sorted(models, key=lambda m: (cat(m), m.rsplit('/', 1)[-1]))
    ef = a.edits or (HERE.parent.parent / 'steamvr' / f'frontend_edits_act{a.act}.json')
    preload = json.loads(ef.read_text(encoding='utf-8')) if ef.exists() else None
    if preload:
        print(f"starting from the owner's changes: {ef}")
    half = round(pfs.ROAD_HALF * pfs.ROAD_NARROW.get(a.act, 1.0), 2)
    data = dict(act=a.act, version=a.version, camera=[cx, cz], items=items, models=models,
                cats={m: cat(m) for m in models}, geo=geo, preload=preload,
                road=[list(p) for p in pfs.ROAD_PATH.get(a.act, ())], half=half,
                cuts=CUTS.get(a.act, [-115, 211]))
    page = (HERE / 'edytor_template.html').read_text(encoding='utf-8')
    if a.act != 2:   # the page's own words and keys for this act
        n = a.act
        for o, w in (('Akt 2', f'Akt {n}'), ('akt2', f'akt{n}'), ('scene_editor_act2_', f'scene_editor_act{n}_'),
                     ('frontend_edits_act2', f'frontend_edits_act{n}'), ("'pcvr_ed_2_'", f"'pcvr_ed_{n}_'"),
                     (r' --version v19 --transw2 extracted\act2\town__transw2.json', f' --act {n} --version {a.version or "v1"}')):
            page = page.replace(o, w)
    page = page.replace('/*DATA*/null', json.dumps(data, ensure_ascii=False))
    a.out.write_text(page, encoding='utf-8')
    print(f'{len(items)} pieces, {len(models)} models -> {a.out}')


if __name__ == '__main__':
    main()
