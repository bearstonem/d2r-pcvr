# Adapted from natpoh/diablo2-vr-port (MIT, Copyright (c) 2026 natpoh) at 91cb1e8; see THIRD_PARTY.md.
# Port of its tools/settings/dome_assets.cpp PrepareCeiling and Measure (the walls' top), with the tables of its
# shared/game_ceiling.h and shared/ceiling_measures.h. The barracks' beams and the domed vaults are left out.
"""Prepare the game-drawn dungeon ceilings from the player's own game.

Each kind of ceiling is a slab made from the game's own lit act 1 cave rock model (StandardMaterial): our slab's
mesh in it (granny_write.py), its three maps renamed to the room's own tiling stone or rock. Five LOD files and a
unit JSON a kind, written over the game developers' unused test models (the game makes a model only from a path it
knows; nothing in play names these), plus the caves' rim walls and measures.txt (each area's walls' top). Generated
files stay in the mod's own .mpq folder; nothing of the game's is shipped, and the game's archives are only read.
"""
import argparse
import hashlib
import json
import os
import re
import struct
import tempfile
from pathlib import Path

import granny_write
from game_assets import open_assets

# ---- the slab (gameceiling::kTile ...) ----------------------------------------------------------------------------------------
TILE, TILES_HALF, CELLS, RELIEF = 40.0, 6, 6, 6.0
WALL_RADIUS = TILE * TILES_HALF - TILE * 0.5 - RELIEF   # under the slab wherever its middle is (214)
WALL_HEIGHT, WALL_SEGMENTS, WALL_ROWS = TILE, 96, 4
SOURCE = 'data/hd/env/model/act1/caves/act1_caves_rocks/rocks03_lod1.model'
SOURCE_MAPS = ('data/hd/env/texture/act1/caves/act1_caves_rocks_rocks03_alb.texture',
               'data/hd/env/texture/act1/caves/act1_caves_rocks_rocks03_nrm.texture',
               'data/hd/env/texture/act1/caves/act1_caves_rocks_rocks03_orm.texture')
UNIT_DIR = 'data/hd/env/model/d2r_vr/ceiling/'
MEASURES = UNIT_DIR + 'measures.txt'
MEASURES_VERSION = 5
RECEIPT = 'pcvr-game-ceiling.json'
RECEIPT_VERSION = 1
LODS = 5

# Every room its own kind: (name, maps without "_ALB.texture" ..., relief, slab model, rim wall model or None). The
# models are the game developers' test models (under data/hd/env/model/Tests/; "@": under data/hd/env/model/).
KINDS = (
    ('cave', 'data/hd/env/texture/terrain/act1/caves/act1_caves_rock01', RELIEF, 'test_detailnormal/cliffside01', 'test_detailnormal/decal01'),
    ('jungle_cave', 'data/hd/env/texture/terrain/act3/jungle/act3_jungle_dungeon_rocky01', RELIEF, 'test_cloth/cloth02', 'test_cloth/cloth04'),
    ('ice_cave', 'data/hd/env/texture/terrain/expansion/icecave/Act5_Rocky_Floor', RELIEF, 'test_cloth/rope_test01/rope_test01', 'testy'),
    ('crypt', 'data/hd/env/texture/terrain/act4/mesa/act4_mesa_stonetile01', 0.0, 'vertblend_house', None),
    ('smooth_dark', 'data/hd/env/texture/terrain/act2/arcane/act2_arcane_grunge01', 0.0, 'test_terrainblending/cliff01', None),
    ('smooth_light', 'data/hd/env/texture/terrain/act2/arcane/act2_arcane_grunge02', 0.0, 'test_terrainblending/ramp01', None),
    ('palace', 'data/hd/env/texture/terrain/act2/palace/act2_palace_cell_marble_tile01', 0.0, 'TextureExportTesting/TestCube', None),
    ('sewer', 'data/hd/env/texture/terrain/act2/sewer/act2_sewer_cobblestone01', 0.0, 'test_cube_tex', None),
    ('harem', 'data/hd/env/texture/terrain/act2/palace/act2_palace_marble01', 0.0, 'test_cube_foliage', None),
    ('barracks_court', 'data/hd/env/texture/terrain/act1/barracks/act1_barracks_cobblestone03', 0.0, '@expansion/siege/expansion_siege_craters/test_asset', None),
    ('act3_sewer', 'data/hd/env/texture/terrain/act3/jungle/act3_jungle_dungeon_rocky01', 0.0, '@act1/catacomb/act1_catacomb_props/_pit02', None),
    ('temple', 'data/hd/env/texture/terrain/act3/temple/act3_temple_stone01', 0.0, 'test_cube', None),
    ('durance', 'data/hd/env/texture/terrain/act3/travincal/act3_travincal_stone01', 0.0, 'test_cloth/hangingRug', None),
    ('wildtemple', 'data/hd/env/texture/terrain/expansion/wildtemple/expansion_wildtemple_caverock', 0.0, 'test_decal/steppe01', None),
    ('baallair', 'data/hd/env/texture/terrain/expansion/baallair/expansion_baallair_floor01', 0.0, 'test_light/test_material_spheres01', None),
)
# Which kind each area gets (the game's biome names); an area not here keeps the shader's vault
BIOME_KINDS = (
    ('act1_caves', 0), ('act2_maggot', 1), ('act3_spider', 1), ('act3_jungle_dungeon', 1), ('expansion_icecave', 2),
    ('act1_crypt', 3), ('act1_barracks', 4), ('act1_catacombs', 4), ('act1_cathedral', 5), ('act2_palace_clean', 6),
    ('act2_sewer', 7), ('act2_palace', 8), ('act2_palace_cells', 4), ('act2_tomb', 4),
    ('act1_court_to_barracks', 9), ('act3_sewer', 10), ('act3_temple', 11), ('act3_travincal', 12),
    ('expansion_wildtemple', 13), ('expansion_baallair', 14), ('expansion_baallair_throneroom', 14),
)
# Which presets build each area (the start of their paths under data/hd/env/preset/): the walls' top is measured there
AREAS = (
    ('act1_caves', 'act1/caves/'), ('act1_crypt', 'act1/crypt/'), ('act1_barracks', 'act1/barracks/'), ('act1_catacombs', 'act1/catacomb/'),
    ('act1_cathedral', 'act1/cathedrl/'), ('act2_sewer', 'act2/sewer/'), ('act2_tomb', 'act2/tomb/'), ('act2_palace', 'act2/palace/'),
    ('act2_palace_clean', 'act2/palace/'), ('act2_palace_cells', 'act2/palace/'), ('act2_maggot', 'act2/maggot/'), ('act3_sewer', 'act3/sewer/'),
    ('act3_temple', 'act3/temple/'), ('act3_spider', 'act3/spider/'), ('act3_jungle_dungeon', 'act3/jungle/dung'),
    ('expansion_icecave', 'expansion/icecave/'), ('expansion_baallair', 'expansion/baallair/'), ('expansion_wildtemple', 'expansion/wildtemple/'),
    ('act1_court_to_barracks', 'act1/barracks/'), ('act3_travincal', 'act3/travincal/'), ('expansion_baallair_throneroom', 'expansion/baallair/'),
)
PRESETS = 'data/hd/env/preset/'
WALL_MIN_HEIGHT = 6.0   # taller than this as placed: a wall, not its cap, trim or rubble
# Who may name a test model we write over (the collision guard): the game's LOD table, and the developers' test presets
# and the editor's prop warehouses, which no level loads (data/global/excel/lvlprest.txt names none of them).
LOD_TABLE = 'data/hd/model_lod_desc.json'
UNPLAYED = ('data/hd/env/preset/test/', 'data/hd/env/preset/prop_warehouse/')
LEVEL_PRESETS = 'data/global/excel/lvlprest.txt'


def digest(data):
    return hashlib.sha256(data).hexdigest()


def model_base(file):
    return 'data/hd/env/model/' + file[1:] if file.startswith('@') else 'data/hd/env/model/Tests/' + file


def model_path(file):
    return model_base(file) + '.model'


def lod_path(file, lod):
    return f'{model_base(file)}_lod{lod}.model'


def map_path(kind, which):
    return f'{KINDS[kind][1]}_{which}.texture'


def unit_path(kind, wall):
    if kind == 0:
        return UNIT_DIR + ('cave_ceiling_wall_unit.json' if wall else 'cave_ceiling_unit.json')
    return UNIT_DIR + KINDS[kind][0] + ('_wall_unit.json' if wall else '_ceiling_unit.json')


def salt_of(kind, wall):
    """Each piece's mesh its own bytes: kind k's slab 1 + 2k, its wall 2 + 2k."""
    return 1 + 2 * kind + (1 if wall else 0)


def unit_json_of(name, model, maps):
    """A unit (lightMask 19 and shadowMask 0, as the game's own cave pieces that cast none): its lights reach it, it
    throws no shadow. Word for word as the source's UnitJsonOf."""
    return ('{"dependencies":{"particles":[],"models":[{"path":"' + model + '"}],'
            '"skeletons":[],"animations":[],"textures":[{"path":"' + maps + '_ALB.texture"},'
            '{"path":"' + maps + '_NRM.texture"},'
            '{"path":"' + maps + '_ORM.texture"}],"physics":[],"json":[],"variantdata":[],'
            '"objecteffects":[],"other":[]},"type":"UnitDefinition","name":"d2r_vr_' + name + '","entities":[{"type":"Entity",'
            '"name":"entity_root","id":1000000003,"components":[{"type":"UnitRootComponent","name":"component_root",'
            '"state_machine_filename":"","doNotInheritRotation":false,"rotationOverride":{"x":0.0,"y":0.0,"z":0.0,"w":1.0},'
            '"doNotUseHDHeight":true,"hideAllMeshWhenInOpenedMode":false,"onCreateEventName":"","animations":[]},'
            '{"type":"TransformDefinitionComponent","name":"entity_root_TransformDefinition","position":{"x":0.0,"y":0.0,"z":0.0},'
            '"orientation":{"x":0.0,"y":0.0,"z":0.0,"w":1.0},"scale":{"x":1.0,"y":1.0,"z":1.0},"inheritOnlyPosition":false}]},'
            '{"type":"Entity","name":"entity_model","id":1000000004,"components":[{"type":"ModelDefinitionComponent",'
            '"name":"component_model_' + name + '","filename":"' + model + '",'
            '"visibleLayers":1,"lightMask":19,"shadowMask":0,"ghostShadows":false,"floorModel":false,"terrainBlendEnableYUpBlend":false,'
            '"terrainBlendMode":1}]}]}')


def unit_json(kind, wall):
    name, maps, _, slab, wall_file = KINDS[kind]
    unit = (('cave_ceiling_wall' if wall else 'cave_ceiling') if kind == 0 else name + ('_wall' if wall else '_ceiling'))
    return unit_json_of(unit, model_path(wall_file if wall else slab), maps)


# ---- the walls' top (ceilmeasure) ---------------------------------------------------------------------------------------------

f32 = granny_write.f32


def biome_of(text):
    """The biome a preset names ("biomeFilename": ".../<biome>.json"), or ''."""
    key = text.find('"biomeFilename"')
    if key < 0:
        return ''
    colon = text.find(':', key)
    q1 = -1 if colon < 0 else text.find('"', colon + 1)
    q2 = -1 if q1 < 0 else text.find('"', q1 + 1)
    if q2 < 0:
        return ''
    b = text[q1 + 1:q2]
    b = b[b.rfind('/') + 1:]
    if len(b) > 5 and b.endswith('.json'):
        b = b[:-5]
    return b.lower()


def _num(obj, key, fallback):
    v = obj.get(key) if isinstance(obj, dict) else None
    return float(v) if isinstance(v, (int, float)) and not isinstance(v, bool) else fallback


def placements(value, out):
    """Every model the preset's entities place: (model lower case, y, scale y), raised by the TransformDefinitionComponent
    in the same list."""
    if isinstance(value, dict):
        comps = value.get('components')
        if isinstance(comps, list):
            tf = next((c for c in comps if isinstance(c, dict) and c.get('type') == 'TransformDefinitionComponent'), None)
            for c in comps:
                if not isinstance(c, dict) or c.get('type') != 'ModelDefinitionComponent':
                    continue
                f = c.get('filename')
                if not isinstance(f, str) or not f:
                    continue
                y, scale_y = 0.0, 1.0
                if tf is not None:
                    pos, sc = tf.get('position'), tf.get('scale')
                    if pos is not None:
                        y = f32(_num(pos, 'y', 0.0))
                    if sc is not None:
                        scale_y = f32(_num(sc, 'y', 1.0))
                out.append((f.lower(), y, scale_y))
        for v in value.values():
            placements(v, out)
    elif isinstance(value, list):
        for v in value:
            placements(v, out)
    return out


def is_wall_piece(model):
    m = model.lower()
    return any(w in m for w in ('wall', 'pillar', 'cliff', 'column'))


def wall_top(model, y, scale_y, mn_y, mx_y):
    """The top of a placed wall piece, or a negative number when it does not count."""
    if not is_wall_piece(model) or f32(f32(mx_y - mn_y) * scale_y) <= WALL_MIN_HEIGHT:
        return -1.0
    return f32(y + f32(scale_y * mx_y))


def median(tops):
    if not tops:
        return 0.0
    tops = sorted(tops)
    n = len(tops)
    return tops[n // 2] if n % 2 else f32(0.5 * f32(tops[n // 2 - 1] + tops[n // 2]))


def format_measures(walls):
    s = ("# D2R VR - the game's rooms measured from its own models by D2R_VR_Settings "
         "([ceiling] height_from_walls, game_ceiling_beams, game_ceiling_dome)\n")
    s += f'version {MEASURES_VERSION}\n'
    for biome, h in walls:
        s += f'walls {biome} {h:.2f}\n'
    return s


def parse_measures(text):
    """The walls of a measures.txt of ours ([(biome, top)]), or None when it is not ours or of another version."""
    walls, version = [], False
    for line in text.splitlines():
        words = line.split()
        if not words or words[0].startswith('#'):
            continue
        if words[0] == 'version':
            if words[1:] != [str(MEASURES_VERSION)]:
                return None
            version = True
        elif words[0] == 'walls' and len(words) == 3:
            try:
                top = float(words[2])
            except ValueError:
                continue
            if top > 0.0:
                walls.append((words[1], top))
    return walls if version else None


def measure(assets, texts):
    """Each indoor area's walls' top: the median top of the wall pieces its own biome's presets place, each model's bounds
    read once from its own lod0 file. `texts`: preset name -> its text (already read). Areas with no wall piece are left out."""
    boxes, shapes = {}, {}

    def bounds_of(model):
        if len(model) < 7 or not model.endswith('.model'):
            return None
        if model not in boxes:
            box = None
            name = model[:-6] + '_lod0.model'
            if name in assets.files:
                try:
                    # (each struct's names learnt once, from the first file that has it: the rest decode only their start)
                    box = granny_write.bounds(granny_write.File(assets.read(name)), shapes)
                except (ValueError, KeyError, struct.error):
                    box = None
            boxes[model] = box
        return boxes[model]

    parsed = {}
    walls = []
    for biome, folder in AREAS:
        tops = []
        want = PRESETS + folder
        for name in sorted(n for n in texts if n.startswith(want)):
            text = texts[name]
            b = biome_of(text)
            if b and b != biome:
                continue
            if name not in parsed:
                try:
                    parsed[name] = placements(json.loads(text), [])
                except ValueError:
                    parsed[name] = None
            for model, y, scale_y in parsed[name] or ():
                if not is_wall_piece(model):
                    continue
                box = bounds_of(model)
                if box is None:
                    continue
                top = wall_top(model, y, scale_y, box[0][1], box[1][1])
                if top >= 0.0:
                    tops.append(top)
        h = median(tops)
        if h > 0.0:
            walls.append((biome, h))
    return walls


# ---- the collision guard ------------------------------------------------------------------------------------------------------

class Guard:
    """For every kind's slab and wall: the JSON files outside the LOD table, the test presets and the warehouses that
    name its model (an empty list: free to write over). A test preset or warehouse a level loads counts as in play."""

    def __init__(self, level_presets):
        loaded = level_presets.lower()
        self.unplayed = tuple(p for p in UNPLAYED
                              if not re.search(r'(^|[\\/\s])' + re.escape(p.rstrip('/').rsplit('/', 1)[1]) + r'[\\/]', loaded))
        self.patterns = {}
        for kind in KINDS:
            for f in (kind[3], kind[4]):
                if f:   # (as a preset or unit names it: "<path>.model" or "<path>_lod<n>.model", whatever comes before it)
                    self.patterns[f] = re.compile(re.escape(model_base(f).removeprefix('data/hd/env/model/').lower()) + r'(?:\.model|_lod\d)')
        self.hits = {f: [] for f in self.patterns}

    def scan(self, name, text):
        if name == LOD_TABLE or name.startswith(self.unplayed):
            return
        low = text.lower()
        for f, pattern in self.patterns.items():
            if pattern.search(low):
                self.hits[f].append(name)


def named_in_play(texts, level_presets):
    guard = Guard(level_presets)
    for name, text in texts.items():
        guard.scan(name, text)
    return guard.hits


# ---- the maker ----------------------------------------------------------------------------------------------------------------

def generate(assets, mod_name='D2R-PCVR', log=print, walls=None):
    """A complete payload: {name under the mod's folder: bytes}, the receipt included, and the walls' tops. `walls`:
    measured before for this very game build (kept), else measured now. Raises ValueError when the game's rock model
    is not as known (nothing made)."""
    if mod_name not in ('D2R-PCVR', 'pcvr-dev'):
        raise ValueError('Unsupported game ceiling destination')
    if SOURCE not in assets.files:
        raise ValueError("act 1's cave rock model is not in this game: no game-drawn ceiling")
    rock = assets.read(SOURCE)
    try:
        level_presets = assets.read(LEVEL_PRESETS).decode('latin-1') if LEVEL_PRESETS in assets.files else ''
    except (ValueError, KeyError, OSError):
        level_presets = ''
    guard = Guard(level_presets)
    areas = tuple(PRESETS + folder for _, folder in AREAS)
    texts = {}   # (the areas' presets, kept for the walls' top; every JSON is scanned by the guard as it is read)
    for name in sorted(n for n in assets.files if n.startswith('data/hd/') and n.endswith('.json')):
        try:
            data = assets.read(name)
        except (ValueError, KeyError, OSError):
            continue
        if len(data) > 16 * 1024 * 1024:
            continue
        text = data.decode('utf-8', 'replace')
        guard.scan(name, text)
        if name.startswith(areas):
            texts[name] = text
    hits = guard.hits
    made, skipped = [], []
    files = {}
    wall = granny_write.wall_mesh(WALL_RADIUS, WALL_HEIGHT, WALL_SEGMENTS, WALL_ROWS, TILE)
    for k, (name, maps, relief, slab_file, wall_file) in enumerate(KINDS):
        if not all(map_path(k, w).lower() in assets.files for w in ('ALB', 'NRM', 'ORM')):
            skipped.append((name, 'its maps are not in this game'))
            continue
        named = [f'{f}: {", ".join(hits[f])}' for f in (slab_file, wall_file) if f and hits[f]]
        if named:
            skipped.append((name, 'a level names its test model (' + '; '.join(named) + ')'))
            continue
        renames = [(SOURCE_MAPS[i], map_path(k, w)) for i, w in enumerate(('ALB', 'NRM', 'ORM'))]
        slab = granny_write.ceiling_mesh(TILE, TILES_HALF, CELLS, relief)
        granny_write.salt(slab, salt_of(k, False))
        pieces = [(slab_file, slab, False)]
        if wall_file:
            own = granny_write.Mesh()
            own.v = [[list(p), list(n), list(t), list(u)] for p, n, t, u in wall.v]
            own.idx, own.scale, own.mn, own.mx = list(wall.idx), wall.scale, list(wall.mn), list(wall.mx)
            granny_write.salt(own, salt_of(k, True))
            pieces.append((wall_file, own, True))
        for file, mesh, is_wall in pieces:
            for lod in range(LODS):
                f = granny_write.File(rock)
                granny_write.remesh(f, mesh, renames, lod)
                files[lod_path(file, lod)] = granny_write.write(f)
            files[unit_path(k, is_wall)] = unit_json(k, is_wall).encode('utf-8')
        made.append(name)
    if walls is None:
        walls = measure(assets, texts)
    files[MEASURES] = format_measures(walls).encode('utf-8')
    payload = {f'{mod_name}.mpq/{rel}': data for rel, data in files.items()}
    record = {'version': RECEIPT_VERSION, 'game_build': assets.build_hash, 'mpq': f'{mod_name}.mpq',
              'kinds': made, 'skipped': [f'{n}: {why}' for n, why in skipped],
              'files': {rel: digest(data) for rel, data in files.items()}}
    payload[RECEIPT] = (json.dumps(record, indent=2) + '\n').encode('utf-8')
    log(f'Game-drawn ceiling: {len(made)} kinds made' + (f', {len(skipped)} skipped' if skipped else '') +
        f', the walls\' top measured in {len(walls)} areas.')
    for n, why in skipped:
        log(f'  {n}: {why}')
    return payload, walls


def measured_before(mod, build):
    """The walls' tops of the last make, when it was of this game build and its measures.txt is still ours; else None
    (measuring reads some 600 wall models: once a game build)."""
    try:
        record = json.loads((Path(mod) / RECEIPT).read_text(encoding='utf-8'))
        data = (Path(mod) / record['mpq'] / MEASURES).read_bytes()
        if record.get('version') != RECEIPT_VERSION or record.get('game_build') != build or record['files'].get(MEASURES) != digest(data):
            return None
        return parse_measures(data.decode('utf-8'))
    except (OSError, ValueError, KeyError, TypeError, AttributeError):
        return None


def prepare(game, mod_name='D2R-PCVR', casclib=None, log=print):
    """Return a complete payload; the installer's transaction performs writes."""
    with open_assets(Path(game), casclib) as assets:
        try:
            walls = measured_before(Path(game) / 'mods' / mod_name, assets.build_hash)
            return generate(assets, mod_name, log, walls)[0]
        except struct.error as error:   # (a game file cut short)
            raise ValueError(f"the game's cave rock model is not as known ({error})") from error


def check_existing(mod, payload):
    """Never replace a player's untracked or edited file (prepare_arcane_sky's rule): ours by the receipt, or the
    same bytes."""
    mod = Path(mod)
    if mod.resolve() != mod:
        raise ValueError('Game ceiling mod folder must not redirect to another location')
    receipt = mod / RECEIPT
    if receipt.resolve() != receipt:
        raise ValueError('Game ceiling receipt points outside the mod')
    previous, mpq = {}, None
    if receipt.is_file():
        record = json.loads(receipt.read_text(encoding='utf-8'))
        if (not isinstance(record, dict) or record.get('version') != RECEIPT_VERSION or not isinstance(record.get('files'), dict)
                or not isinstance(record.get('mpq'), str)):
            raise ValueError('Unknown game ceiling receipt')
        previous, mpq = record['files'], record['mpq']
    for name, data in payload.items():
        path = mod / name
        if path.resolve() != path:
            raise ValueError('Game ceiling destination must not contain links')
        if not path.exists() or name == RECEIPT:
            continue
        rel = name.split('/', 1)[1] if mpq and name.startswith(mpq + '/') else None
        old = path.read_bytes()
        if old != data and digest(old) != previous.get(rel):
            raise ValueError(f'Existing game ceiling file left unchanged: {name}')


# ---- the dev preview: pcvr-dev's own copy -------------------------------------------------------------------------------------

def dev_root(game):
    from prepare_dev_scenarios import MOD_INFO
    root = Path(game).resolve() / 'mods/pcvr-dev'
    info = root / 'pcvr-dev.mpq/modinfo.json'
    if root.resolve() != root or info.resolve() != info or info.read_text(encoding='utf-8') != MOD_INFO:
        raise ValueError('Requires the existing isolated pcvr-dev metadata/save path')
    return root


def write_files(root, payload):
    """Every file written beside itself, then moved over the old one (a game reading meanwhile never meets half a file);
    the receipt last."""
    for name in sorted(payload, key=lambda n: n == RECEIPT):
        path = root / name
        if path.resolve() != path:
            raise ValueError('Game ceiling destination must not contain links')
        if path.is_file() and path.read_bytes() == payload[name]:
            continue
        path.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.NamedTemporaryFile(dir=path.parent, prefix='.pcvr-ceiling-', delete=False) as stream:
            temporary = Path(stream.name)
            stream.write(payload[name])
        try:
            os.replace(temporary, path)
        finally:
            temporary.unlink(missing_ok=True)


def remove_files(root):
    """Our files taken out again, by the receipt's hashes (a file the player changed stays); the count."""
    receipt = root / RECEIPT
    if not receipt.is_file():
        return 0
    record = json.loads(receipt.read_text(encoding='utf-8'))
    if record.get('version') != RECEIPT_VERSION or not isinstance(record.get('files'), dict):
        raise ValueError('Unknown game ceiling receipt')
    removed = 0
    for rel, expected in record['files'].items():
        path = root / record['mpq'] / rel
        if path.resolve() != path:
            raise ValueError('Game ceiling destination must not contain links')
        if path.is_file() and digest(path.read_bytes()) == expected:
            path.unlink()
            removed += 1
    receipt.unlink()
    return removed


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game', type=Path, required=True)
    parser.add_argument('--dev', action='store_true', help='write into the isolated pcvr-dev mod (game closed)')
    parser.add_argument('--remove', action='store_true', help='with --dev: take ours out of pcvr-dev again')
    parser.add_argument('--output', type=Path, help='write the files into this empty folder instead (a dry run)')
    parser.add_argument('--casclib', type=Path, help='Packaged Unicode x64 CascLib 3.0 (Battle.net only)')
    args = parser.parse_args()
    try:
        if args.output:
            if args.output.exists() and any(args.output.iterdir()):
                raise ValueError('--output must be a new or empty folder')
            with open_assets(args.game, args.casclib) as assets:
                payload, _ = generate(assets, 'D2R-PCVR')
            args.output.mkdir(parents=True, exist_ok=True)
            write_files(args.output.resolve(), payload)
            print(f'Wrote {len(payload)} files ({sum(map(len, payload.values())) / 1e6:.1f} MB) to {args.output}.')
            return
        if not args.dev:
            raise ValueError('Use --dev (the pcvr-dev mod) or --output; the installer prepares the release mod.')
        from install import require_game_closed
        from game_install import validate_game
        require_game_closed()
        root = dev_root(args.game)
        if args.remove:
            print(f'Removed {remove_files(root)} game ceiling files from pcvr-dev.')
            return
        validate_game(args.game)
        with open_assets(args.game, args.casclib) as assets:
            payload, _ = generate(assets, 'pcvr-dev', walls=measured_before(root, assets.build_hash))
        check_existing(root, payload)
        require_game_closed()
        write_files(dev_root(args.game), payload)
        print(f'Installed {len(payload)} game ceiling files in pcvr-dev. Restart pcvr-dev to test.')
    except (OSError, ValueError, RuntimeError, KeyError) as error:
        parser.exit(1, f'{error}\n')


if __name__ == '__main__':
    main()
