"""Character creation's campfire in VR: its clouds off.

The scene's preset (data/hd/env/preset/ui/charactercreate.json) places a cloud
effect, FX_UICharacterCreate_clouds (data/hd/vfx/particles/env/UI/...). Its
cards are laid flat over the picture: in the headset they stayed fixed to the
eyes while the scene turned with the head (owner 2026-11-06, the dark squares
over the heroes' heads too). A copy of the preset without that one entity goes
into the mod's own data (D2R-PCVR.mpq), as prepare_kurast_dock does for the
docks; the sky, the mountains, the fire, the heroes and the light stay.

Read from the player's own game, never packaged. The installer calls prepare()
and check_existing(); run alone, it writes the preset and its receipt.

  python prepare_campfire_sky.py "<game folder>" --casclib "<...>/bin/CascLib.dll" [--write]
Undo: delete <game>/mods/D2R-PCVR/D2R-PCVR.mpq/data/hd/env/preset/ui/charactercreate.json
"""
import argparse
import copy
import hashlib
import json
import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))   # the embedded runtime adds no script folder
from game_assets import open_assets  # noqa: E402

PRESET = 'data/hd/env/preset/ui/charactercreate.json'
RECEIPT = 'pcvr-campfire-sky.json'
CLOUDS = 'fx_uicharactercreate_clouds'
# The town round the fire (Harrogath's walls, houses and siege props, all in the half of the scene the
# game's camera looks at): in VR you turn round, and behind you the ground just ended. A copy of that
# half, turned half a circle about the fire, stands behind and beside you instead. Models only - no
# lights, effects or sounds twice.
FIRE = (1060.67, 123.94)        # rogue_bonfire01_01, x and z
SURROUND_MIN = 12.0             # the fire's own stones and logs stay single
MIRROR = '_pcvr_behind'


def turned_half(entity, ids):
    """The entity turned 180 degrees about the fire's vertical axis, with a new id and name."""
    e = copy.deepcopy(entity)
    e['name'] = f'{e.get("name", "entity")}{MIRROR}'
    new = (int(e.get('id', 0)) * 2654435761 + 0x9E3779B9) & 0xFFFFFFFF
    while new in ids or new == 0:
        new = (new + 0x01000193) & 0xFFFFFFFF
    ids.add(new)
    e['id'] = new
    for c in e.get('components', []):
        if c.get('type') != 'TransformDefinitionComponent':
            continue
        p, q = c.get('position'), c.get('orientation')
        if isinstance(p, dict):
            p['x'] = 2 * FIRE[0] - float(p.get('x', 0.0))
            p['z'] = 2 * FIRE[1] - float(p.get('z', 0.0))
        if isinstance(q, dict):   # (0, 1, 0, 0) * q: half a turn about +y
            x, y, z, w = (float(q.get(k, 0.0)) for k in 'xyzw')
            q['x'], q['y'], q['z'], q['w'] = z, w, -x, -y
    return e


def placed_model(entity):
    comps = entity.get('components', []) if isinstance(entity, dict) else []
    kinds = {str(c.get('type', '')) for c in comps}
    if any('Light' in k or 'Vfx' in k or 'Sound' in k or 'Audio' in k for k in kinds):
        return None
    pos = next((c.get('position') for c in comps if c.get('type') == 'TransformDefinitionComponent'), None)
    if not isinstance(pos, dict) or not any('Model' in k for k in kinds):
        return None
    x, z = float(pos.get('x', 0.0)), float(pos.get('z', 0.0))
    return ((x - FIRE[0]) ** 2 + (z - FIRE[1]) ** 2) ** 0.5


TOWN = 'data/hd/env/preset/expansion/town/townwest.json'   # Harrogath, the west half (2832 entities)
# The scene's portcullis (ui_wall_gate01_01, x z) and Harrogath's own west gate (wall_gate.model in
# townwest): the town is laid so that its gate stands where the scene's does - the first thing a player
# knows here (owner 2026-11-06: "aaa, no tak, to tu"). The town's west wall then runs on from the
# scene's gate behind and beside the player; the town lies round them, inside the walls.
SCENE_GATE = (1002.2, 107.6)
TOWN_GATE = (49.2, 190.0)
TOWN_AT_GATE = (TOWN_GATE[0] + FIRE[0] - SCENE_GATE[0], TOWN_GATE[1] + FIRE[1] - SCENE_GATE[1])
# The scene's own half of the town: the directions from the fire it already fills (degrees, atan2(z, x)).
SCENE_SECTOR = (-180.0, -55.0)


# Kept clear beside the heroes: the town's nearest house (structure03, left of the Amazon) and its yard
# stood right at the hero row (owner 2026-11-06: "nie może być aż tak blisko"). Directions and radius.
HERO_SIDE = (100.0, 180.0)
HERO_SIDE_CLEAR = 55.0
# The town's own ground (the preset's terrain), sunk a little so that the scene's ground wins where both lie.
TOWN_GROUND_DROP = 0.12


# Harrogath's west wall, as the town lays it (x of its line in the town, models from its walls folder):
# the town shows it to the isometric camera from the east, so its far side was never finished. The
# scene's own gate stands turned half round against the town's; the town's wall here is turned the same.
WALL_LINE_X = 49.2
WALLS = 'expansion_town_walls'
# Off: with the town's lists merged its textures, and the scene's own wall, came out blurrier still
# (owner 2026-11-06) - the streaming budget spread over two thousand more textures.
TOWN_DEPENDENCIES = ()
# What of a town entity is copied: where it stands and what it shows. Physics bodies, walls that fade
# round the hero (WallTransparency, FadeTarget) stay in the town; cloth and ragdolls (skeletons) whole.
TOWN_SHADOWS = False
KEPT_COMPONENTS = ('TransformDefinitionComponent', 'ModelDefinitionComponent', 'ModelPlatformTierComponent',
                   'ModelVariationDefinitionComponent')
ANIMATED = ('SkeletonDefinitionComponent', 'PhysicsClothDefinitionComponent', 'PhysicsPartialRagdollDefinitionComponent')
# The town's ground clutter (snow piles, grass, weeds, corn, shrubs, grunge decals) and its small props:
# hundreds of models and textures that the scene's texture budget then shares with everything else -
# the whole scene, its own wall too, went blurry with them in (owner 2026-11-06). Houses, walls, trees,
# fences and the larger yard pieces stay.
CLUTTER = ('snowprops', 'grass', 'weeds', 'corn_stalks', 'shrubs', 'bushes', '/grunge/', '/decal/',
           'bottles', 'pottery', 'tavern_props', 'vases', 'urns', 'tableware', 'pebbles', 'rags', 'rugs',
           'planks', 'debris', 'weapons', 'tools', 'shop_props', 'docktown', 'jungle', 'tomb', 'barracks',
           'hanging_props', 'decorations', 'cottage_props', 'furniture', 'chests', 'lights', 'cloth')


# The town's own wall (its walls folder) where it runs on into the scene's built half: there the scene's
# wall stands only in places (its gate, two lengths of wall) and the town's was all taken out with the half -
# holes in the wall left of the gate and behind the player (owner 2026-11-07). Its pieces come back unless
# one of the scene's own walls or its gate stands within WALL_FILL_CLEAR of them (no wall twice).
WALL_FILL_CLEAR = 10.0
WALL_FILL = True                 # --no-wall-fill: the town's wall out of the whole scene half, as before


def scene_walls(preset):
    """Where the scene's own walls and gate stand, (x, z) from the fire."""
    out = []
    for e in preset.get('entities', []):
        comps = e.get('components', []) if isinstance(e, dict) else []
        model = str(next((c.get('filename', '') for c in comps if c.get('type') == 'ModelDefinitionComponent'), '')).lower()
        name = model.rsplit('/', 1)[-1]
        if 'wall' not in name and 'gate' not in name:
            continue
        tr = next((c for c in comps if c.get('type') == 'TransformDefinitionComponent' and isinstance(c.get('position'), dict)), None)
        if tr is not None:
            out.append((float(tr['position'].get('x', 0.0)) - FIRE[0], float(tr['position'].get('z', 0.0)) - FIRE[1]))
    return out


# --town-keep A1:B2 ...: rectangles of the owner's grid map (columns A-J, rows 1-10, 40 units a cell,
# screen right = -z, screen down = +x from the fire) where the town's own pieces stay even inside the
# scene's built half - Harrogath's raised yard with its stairs (structure09_platform) and the house on it
# (owner 2026-11-08: "brakuje górki. Całego terenu ze schodami").
TOWN_KEEP = ()
# --hide NAME...: entities taken out last, by their exact name - the town's pieces too (NAME_pcvr_behind):
# the town wall's corner at B4, whose unfinished back faced the player once its wall was turned (2026-11-08).
HIDE = ()
# --town-biome: the scene takes Harrogath's biome (expansion_town). The town's ground (terrain.model) is
# painted by blend layers whose textures its biome names; under the scene's own biome the same layers came
# out as one terracotta all over (owner 2026-11-08: "tej terakoty w grze nie ma praktycznie w ogóle").
TOWN_BIOME = False
# --pave: the town's flat ground under copies of the scene's road to the gate (terrain_characterCreate01,
# "gładkie kamienie", with its own gentle swells - owner 2026-11-08), laid as bricks over the whole map
# (x, z from the fire), a little under the road itself so that the road and its two copies by the wall win.
PAVE = False
PAVE_AREA = (-205.0, 225.0, -210.0, 240.0)
PAVE_STEP = (100.0, 26.0)        # along its length (x) and across (z); the piece is about 114 x 31
PAVE_HULL = (-29.0, 83.0)        # the piece's near corner from its origin (x, z)
PAVE_DY = (-0.03, -0.05, -0.07)
# --pave-skip X:Z ...: no road piece over these points (the fire's own pebbles; owner 2026-11-08: "ta
# tekstura, na której stoję, zasłania ognisko ... tak samo 1 na północ ... oraz jedna na południe").
PAVE_SKIP = ()
# --pave-tilt A1:B2:DROP: the pieces whose middle lies in those cells tipped about their own length, the
# edge towards the map's right (smaller z) DROP lower and the other edge where it was - their ends bent
# down to the ground beside the tower (D5-D6), not the whole floor tipped.
PAVE_TILT = ()
# --pave-hole X0:X1:Z0:Z1: the fire's own ground (its gravel squares) kept clear of the road: the rows over it
# are laid without their half-step, their pieces starting at X1 and on (towards the bottom of the map) only,
# so that no piece of a row reaches into it and every row beside it stays whole (owner 2026-11-08: the
# whole left and bottom of the town in the road's stones, the fire's squares as they are).
PAVE_HOLE = None
# --move NAME:DX:DY:DZ: an entity (the town's too, NAME_pcvr_behind) moved; --lift X:Z:R:DY: everything
# but the ground within R of (X, Z) from the fire raised by DY (the well and what leans on it, standing in
# the new road's swells - owner 2026-11-08).
MOVES = ()
LIFTS = ()
# --light X:Z:Y:R:G:B:POWER:RADIUS ...: a point light of its own (from the scene's fire light, its
# settings replaced): X, Z from the fire, Y the height (owner 2026-11-09: a light in the room behind
# the slit over the stairs).
LIGHTS = ()
LIGHT_TEMPLATE = 'charactercreate_fill_point01_04'


def with_lights(preset):
    entities = preset['entities']
    src = next((e for e in entities if isinstance(e, dict) and e.get('name') == LIGHT_TEMPLATE), None)
    if src is None:
        return preset, ['light: the fire light not found']
    ids = {int(e.get('id', 0)) for e in entities if isinstance(e, dict)}
    for k, spec in enumerate(LIGHTS):
        x, z, y, r, g, b, power, radius = (float(v) for v in spec.lstrip('@').split(':'))
        e = copy.deepcopy(src)
        e['name'] = f'pcvr_light{k:02d}'
        new = (0x51A7E000 + k * 7919) & 0xFFFFFFFF
        while new in ids:
            new = (new + 0x01000193) & 0xFFFFFFFF
        ids.add(new); e['id'] = new
        for c in e.get('components', []):
            if c.get('type') == 'TransformDefinitionComponent':
                c['position'] = {'x': FIRE[0] + x, 'y': y, 'z': FIRE[1] + z}
            elif c.get('type') == 'PointLightDefinitionComponent':
                c['color'] = {'x': r, 'y': g, 'z': b}
                c['power'] = power
                c['radius'] = radius
        entities.append(e)
    return preset, [f'+{len(LIGHTS)} lights']


FLOORS = ('terrain_charactercreate', 'ui_skirt_mud', 'ui_rock_pile', 'town_ground')


def with_pave(preset):
    entities = preset['entities']
    src = next((e for e in entities if isinstance(e, dict) and str(e.get('name', '')).lower() == 'terrain_charactercreate01_01'), None)
    if src is None:
        return preset, ['pave: road not found']
    ids = {int(e.get('id', 0)) for e in entities if isinstance(e, dict)}
    x0, x1, z0, z1 = PAVE_AREA
    n = 0
    row = 0
    z = z0
    while z < z1:
        x = x0 - (PAVE_STEP[0] / 2 if row % 2 else 0.0)
        holed = PAVE_HOLE is not None and min(z + 31.0, PAVE_HOLE[3]) - max(z, PAVE_HOLE[2]) > 2.0
        if holed:
            x = PAVE_HOLE[1]
        col = 0
        while x < x1:
            hit = any(x <= px_ < x + 114.0 and z <= pz_ < z + 31.0 for px_, pz_ in PAVE_SKIP)
            if hit:
                x += PAVE_STEP[0]; col += 1
                continue
            e = copy.deepcopy(src)
            e['name'] = f'{src.get("name")}_pave{n:03d}'
            new = (int(src.get('id', 0)) * 2654435761 + (n + 1) * 0x9E3779B1) & 0xFFFFFFFF
            while new in ids or new == 0:
                new = (new + 0x01000193) & 0xFFFFFFFF
            ids.add(new); e['id'] = new
            for c in e.get('components', []):
                if c.get('type') == 'TransformDefinitionComponent' and isinstance(c.get('position'), dict):
                    p = c['position']
                    p['x'] = FIRE[0] + x - PAVE_HULL[0]
                    p['z'] = FIRE[1] + z - PAVE_HULL[1]
                    p['y'] = float(p.get('y', 0.0)) + PAVE_DY[(row + col) % len(PAVE_DY)]
                    for (a, b, drop) in PAVE_TILT:
                        # the cells' x, z (columns run to smaller z, rows to larger x), and the piece over
                        # them by 10 or more each way
                        cz1, cz0 = 200.0 - 40.0 * a[0], 200.0 - 40.0 * (b[0] + 1)
                        cx0, cx1 = (a[1] - 1) * 40.0 - 200.0, b[1] * 40.0 - 200.0
                        ox = min(x + 114.0, cx1) - max(x, cx0)
                        oz = min(z + 31.0, cz1) - max(z, cz0)
                        if ox >= 10.0 and oz >= 10.0:
                            # about local x through the origin (the piece lies at local z 83..114 from
                            # it): the near edge (83) DROP lower, the far edge (114) put back where it was
                            th = math.asin(max(-1.0, min(1.0, -drop / 31.0)))
                            p['y'] -= (PAVE_HULL[1] + 31.0) * drop / 31.0
                            h = th / 2
                            q = c.setdefault('orientation', {'x': 0.0, 'y': 0.0, 'z': 0.0, 'w': 1.0})
                            x0_, y0_, z0_, w0_ = (float(q.get(k, 0.0)) for k in 'xyzw')
                            bx, bw = math.sin(h), math.cos(h)
                            q['x'] = bw * x0_ + bx * w0_
                            q['y'] = bw * y0_ - bx * z0_
                            q['z'] = bw * z0_ + bx * y0_
                            q['w'] = bw * w0_ - bx * x0_
            entities.append(e)
            n += 1; col += 1
            x += PAVE_STEP[0]
        row += 1
        z += PAVE_STEP[1]
    return preset, [f'+{n} pieces of the road over the town ground']


def grid_cell(dx, dz):
    return (200.0 - dz) / 40.0, (dx + 200.0) / 40.0 + 1.0


def in_town_keep(dx, dz):
    c, r = grid_cell(dx, dz)
    for a, b in TOWN_KEEP:
        if a[0] <= c < b[0] + 1 and a[1] <= r < b[1] + 1:
            return True
    return False


def parse_cell(text):
    text = text.strip().upper()
    return 'ABCDEFGHIJ'.index(text[0]), int(text[1:])


def in_scene_half(dx, dz):
    a = math.degrees(math.atan2(dz, dx))
    return SCENE_SECTOR[0] <= a <= SCENE_SECTOR[1]


def beside_heroes(dx, dz):
    a = math.degrees(math.atan2(dz, dx))
    return HERO_SIDE[0] <= a <= HERO_SIDE[1] and math.hypot(dx, dz) < HERO_SIDE_CLEAR


def town_ground(town, at, turn_deg, ids):
    """The town's ground mesh (its preset's terrain) as a plain model at the same place as the houses:
    without it Harrogath stands over the void wherever the scene's own ground ends."""
    source = town.get('terrain')
    comps = source.get('components', []) if isinstance(source, dict) else []
    model = next((c for c in comps if c.get('type') == 'ModelDefinitionComponent' and c.get('filename')), None)
    tr = next((c for c in comps if c.get('type') == 'TransformDefinitionComponent' and isinstance(c.get('position'), dict)), None)
    if model is None or tr is None:
        return None
    t = math.radians(turn_deg)
    ct, st = math.cos(t), math.sin(t)
    x = float(tr['position'].get('x', 0.0)) - at[0]
    z = float(tr['position'].get('z', 0.0)) - at[1]
    new = 0x5CA1AB1E
    while new in ids:
        new = (new + 0x01000193) & 0xFFFFFFFF
    ids.add(new)
    return {'type': 'Entity', 'name': f'town_ground{MIRROR}', 'id': new, 'components': [
        {'type': 'TransformDefinitionComponent', 'name': f'town_ground{MIRROR}_TransformDefinitionComponent',
         'position': {'x': FIRE[0] + ct * x + st * z, 'y': float(tr['position'].get('y', 0.0)) - TOWN_GROUND_DROP,
                      'z': FIRE[1] - st * x + ct * z},
         'orientation': {'x': 0.0, 'y': math.sin(t * 0.5), 'z': 0.0, 'w': math.cos(t * 0.5)},
         'scale': {'x': 1.0, 'y': 1.0, 'z': 1.0}, 'inheritOnlyPosition': False},
        dict(copy.deepcopy(model), name=f'town_ground{MIRROR}_ModelDefinitionComponent', shadowMask=0)]}


def with_harrogath_behind(preset, town, at, turn_deg, near=14.0, far=400.0, ground=True, flip_wall=True, clutter=False,
                          wall_fill=True):
    """Harrogath's own houses, walls and props round the player: the town preset moved so that its
    point `at` (x, z) lies on the fire, turned by `turn_deg` about it, and only what lands outside the
    scene's own built half, between `near` and `far` from the fire. Models only, as above."""
    result = copy.deepcopy(preset)
    entities = result['entities']
    if any(MIRROR in str(e.get('name', '')) for e in entities):
        return result, 0
    ids = {int(e.get('id', 0)) for e in entities if isinstance(e, dict)}
    t = math.radians(turn_deg)
    ct, st = math.cos(t), math.sin(t)
    qy = (0.0, math.sin(t * 0.5), 0.0, math.cos(t * 0.5))
    added = 0
    own_walls = scene_walls(preset) if wall_fill else []
    for source in town.get('entities', []):
        comps = source.get('components', []) if isinstance(source, dict) else []
        kinds = {str(c.get('type', '')) for c in comps}
        if not any('Model' in k for k in kinds) or any('Light' in k or 'Vfx' in k or 'Sound' in k or 'Audio' in k for k in kinds):
            continue
        tr = next((c for c in comps if c.get('type') == 'TransformDefinitionComponent' and isinstance(c.get('position'), dict)), None)
        if tr is None:
            continue
        if not clutter:
            model_name = str(next((c.get('filename', '') for c in comps if c.get('type') == 'ModelDefinitionComponent'), '')).lower()
            if any(word in model_name for word in CLUTTER):
                continue
        x = float(tr['position'].get('x', 0.0)) - at[0]
        z = float(tr['position'].get('z', 0.0)) - at[1]
        dx, dz = ct * x + st * z, -st * x + ct * z   # turned about +y
        d = math.hypot(dx, dz)
        if d < near or d > far or beside_heroes(dx, dz):
            continue
        if in_scene_half(dx, dz) and not (in_town_keep(dx, dz) and WALLS not in str(next((c.get('filename', '') for c in comps if c.get('type') == 'ModelDefinitionComponent'), '')).lower()):
            model_name = str(next((c.get('filename', '') for c in comps if c.get('type') == 'ModelDefinitionComponent'), '')).lower()
            if not (wall_fill and WALLS in model_name and
                    all(math.hypot(dx - wx, dz - wz) >= WALL_FILL_CLEAR for wx, wz in own_walls)):
                continue
        if any(k in kinds for k in ANIMATED):
            continue
        e = copy.deepcopy(source)
        e['name'] = f'{e.get("name", "entity")}{MIRROR}'
        # the picture only: no bodies, no fading walls (the town fades its walls round the hero - here
        # that cut squares round the heroes' faces, owner 2026-11-06)
        e['components'] = [c for c in e.get('components', []) if c.get('type') in KEPT_COMPONENTS]
        if not TOWN_SHADOWS:
            # the town in the picture, not in the lights' shadow maps (owner 2026-11-07: dark blocks on
            # the heroes' faces and pauldrons came with the town)
            for c in e['components']:
                if c.get('type') == 'ModelDefinitionComponent':
                    c['shadowMask'] = 0
        model = next((c.get('filename', '') for c in comps if c.get('type') == 'ModelDefinitionComponent'), '')
        spin = (flip_wall and WALLS in str(model).lower()
                and abs(float(tr['position'].get('x', 0.0)) - WALL_LINE_X) < 4.0
                and (FLIP_SIDE == 'all' or (FLIP_SIDE == 'north') == (dz > 0.0)))
        # --flip-far-wall: the town's north wall, across the square from the gate (z over 120 from the
        # fire), turned too - so that its columned face, like the west wall's, is the one seen
        # (only its straight lengths: an end, corner or crenellation turned about its own origin leaves its place)
        spin = spin or (FLIP_FAR and WALLS in str(model).lower() and dz > 120.0 and 'segment' in str(model).lower())
        far_length = FLIP_FAR and WALLS in str(model).lower() and dz > 120.0 and 'segment' in str(model).lower()
        new = (int(e.get('id', 0)) * 2246822519 + 0x27D4EB2F) & 0xFFFFFFFF
        while new in ids or new == 0:
            new = (new + 0x01000193) & 0xFFFFFFFF
        ids.add(new)
        e['id'] = new
        for c in e.get('components', []):
            if c.get('type') != 'TransformDefinitionComponent':
                continue
            c['position']['x'] = FIRE[0] + dx + (FAR_SHIFT if far_length else 0.0)
            c['position']['z'] = FIRE[1] + dz
            q = c.get('orientation')
            if isinstance(q, dict) and spin:   # half a turn about its own +y first
                x0, y0, z0, w0 = (float(q.get(k, 0.0)) for k in 'xyzw')
                q['x'], q['y'], q['z'], q['w'] = z0, w0, -x0, -y0
            if isinstance(q, dict):   # qy * q
                x0, y0, z0, w0 = (float(q.get(k, 0.0)) for k in 'xyzw')
                bx, by, bz, bw = qy
                q['x'] = bw * x0 + bx * w0 + by * z0 - bz * y0
                q['y'] = bw * y0 - bx * z0 + by * w0 + bz * x0
                q['z'] = bw * z0 + bx * y0 - by * x0 + bz * w0
                q['w'] = bw * w0 - bx * x0 - by * y0 - bz * z0
        entities.append(e)
        added += 1
    # the town's own model, texture and skeleton lists, so that its textures load as they do in the town.
    # Only these: its json list names the town's biomes and vis box, which bring Harrogath's screen-space
    # weather into the scene (the dark squares over the heroes' faces again, 2026-11-06).
    deps = result.setdefault('dependencies', {})
    if isinstance(deps, dict) and isinstance(town.get('dependencies'), dict):
        for kind, items in town['dependencies'].items():
            if kind not in TOWN_DEPENDENCIES:
                continue
            mine = deps.setdefault(kind, [])
            if not isinstance(mine, list) or not isinstance(items, list):
                continue
            seen = {json.dumps(i, sort_keys=True) for i in mine}
            for item in items:
                key = json.dumps(item, sort_keys=True)
                if key not in seen:
                    seen.add(key)
                    mine.append(copy.deepcopy(item))
    if ground:
        floor = town_ground(town, at, turn_deg, ids)
        if floor is not None:
            entities.append(floor)
            added += 1
    return result, added


SKY = 'characterselect_sky.model'
# The scene's sky (mountains and all) is one band: an arc of 120 degrees, u 0..1 across, about a centre
# on its local +x (its Granny mesh, 2026-11-06: radius 1.155 of a VertexScale of 172.86 model units,
# which is 174.7 in the scene at the band's scale - short of the fire). Two copies turned 120 and 240
# degrees about that centre close the ring edge to edge. Each copy is a hair
# wider (its local z, the chord) so that the edges overlap instead of leaving a slit, and a hair farther
# out, so that where they overlap the scene's own sky - the mountains the player knows - is the one seen.
SKY_TURNS = (120.0, 240.0)
SKY_GROW = 0.004
SKY_WIDEN = 1.02
SKY_RADIUS = 1.1547 * 172.8617   # model units, times the band's own scale
SKY_TOP = 0.8466 * 172.8617      # the band's top edge above its origin, model units
CAP_FACE = 1.0                   # -1 turns the cap the other way up
POINTS = False                   # --spots-as-points (testing)
SPOT_SPECULAR = None             # --spot-specular X (testing)


def sky_centre(sky):
    """Where the band's arc is centred (x, z): its local +x, turned and scaled as the scene places it."""
    tr = next(c for c in sky.get('components', []) if c.get('type') == 'TransformDefinitionComponent')
    p, q = tr['position'], tr.get('orientation') or {}
    yaw = 2.0 * math.atan2(float(q.get('y', 0.0)), float(q.get('w', 1.0)))
    r = SKY_RADIUS * float((tr.get('scale') or {}).get('x', 1.0))
    # +x turned by yaw about +y: (cos, -sin) in x, z
    return float(p.get('x', 0.0)) + r * math.cos(yaw), float(p.get('z', 0.0)) - r * math.sin(yaw)


def with_sky_round(preset, turns=SKY_TURNS):
    result = copy.deepcopy(preset)
    entities = result['entities']
    sky = next((e for e in entities if isinstance(e, dict) and SKY in json.dumps(e).lower()
                and MIRROR not in str(e.get('name', ''))), None)
    if sky is None:
        return result, 0
    ids = {int(e.get('id', 0)) for e in entities if isinstance(e, dict)}
    added = 0
    for i, turn in enumerate(turns, 1):
        e = copy.deepcopy(sky)
        e['name'] = f'{e.get("name", "sky")}_{int(turn)}{MIRROR}'
        new = (int(e.get('id', 0)) + 0x9E3779B9 * i) & 0xFFFFFFFF
        while new in ids or new == 0:
            new = (new + 0x01000193) & 0xFFFFFFFF
        ids.add(new)
        e['id'] = new
        k = 1.0 + SKY_GROW * i
        t = math.radians(turn)
        centre = sky_centre(sky)
        ct, st = math.cos(t), math.sin(t)
        qy = (0.0, math.sin(t * 0.5), 0.0, math.cos(t * 0.5))
        for c in e.get('components', []):
            if c.get('type') != 'TransformDefinitionComponent':
                continue
            p = c['position']
            x, z = float(p.get('x', 0.0)) - centre[0], float(p.get('z', 0.0)) - centre[1]
            p['x'] = centre[0] + k * (ct * x + st * z)
            p['z'] = centre[1] + k * (-st * x + ct * z)
            sc = c.get('scale')
            if isinstance(sc, dict):
                for a in 'xyz':
                    sc[a] = float(sc.get(a, 1.0)) * k * (SKY_WIDEN if a == 'z' else 1.0)
            q = c.get('orientation')
            if isinstance(q, dict):   # qy * q
                x0, y0, z0, w0 = (float(q.get(a, 0.0)) for a in 'xyzw')
                bx, by, bz, bw = qy
                q['x'] = bw * x0 + bx * w0 + by * z0 - bz * y0
                q['y'] = bw * y0 - bx * z0 + by * w0 + bz * x0
                q['z'] = bw * z0 + bx * y0 - by * x0 + bz * w0
                q['w'] = bw * w0 - bx * x0 - by * y0 - bz * z0
        entities.append(e)
        added += 1
    return result, added


CAP = 'data/hd/env/model/expansion/mountaintop/expansion_mountaintop_skybox/skybox01.model'
CAP_SIZE = 326.12   # its quad's half-side (Granny VertexScale), in the x = 0 plane, facing +x
CAP_COVER = 1.35    # half-side over the sky band's radius: the corners reach well past the band


def with_sky_cap(preset):
    """Over the player: the Arreat Summit's sky card (Act 5's own), laid flat at the height of the top of
    the scene's sky band, facing down, wide enough to close the ring of bands from above."""
    result = copy.deepcopy(preset)
    entities = result['entities']
    if any(f'sky_cap{MIRROR}' == e.get('name') for e in entities if isinstance(e, dict)):
        return result, 0
    sky = next((e for e in entities if isinstance(e, dict) and SKY in json.dumps(e).lower()
                and MIRROR not in str(e.get('name', ''))), None)
    if sky is None:
        return result, 0
    tr = next(c for c in sky.get('components', []) if c.get('type') == 'TransformDefinitionComponent')
    band = float((tr.get('scale') or {}).get('x', 1.0))
    centre = sky_centre(sky)
    top = float(tr['position'].get('y', 0.0)) + SKY_TOP * band
    k = CAP_COVER * SKY_RADIUS * band / CAP_SIZE
    ids = {int(e.get('id', 0)) for e in entities if isinstance(e, dict)}
    new = 0x5CA1AB1F
    while new in ids:
        new = (new + 0x01000193) & 0xFFFFFFFF
    h = math.sqrt(0.5)
    entities.append({'type': 'Entity', 'name': f'sky_cap{MIRROR}', 'id': new, 'components': [
        {'type': 'TransformDefinitionComponent', 'name': f'sky_cap{MIRROR}_TransformDefinitionComponent',
         'position': {'x': centre[0], 'y': top, 'z': centre[1]},
         # a quarter turn about -z: its +x face turned to -y, down towards the player
         'orientation': {'x': 0.0, 'y': 0.0, 'z': -h * CAP_FACE, 'w': h},
         'scale': {'x': k, 'y': k, 'z': k}, 'inheritOnlyPosition': False},
        {'type': 'ModelDefinitionComponent', 'name': f'sky_cap{MIRROR}_ModelDefinitionComponent',
         'filename': CAP, 'visibleLayers': 1, 'lightMask': 19, 'shadowMask': 0, 'ghostShadows': False,
         'floorModel': False, 'terrainBlendEnableYUpBlend': False, 'terrainBlendMode': 1}]})
    return result, 1


def with_town_behind(preset):
    result = copy.deepcopy(preset)
    entities = result['entities']
    if any(MIRROR in str(e.get('name', '')) for e in entities):
        return result, 0
    ids = {int(e.get('id', 0)) for e in entities if isinstance(e, dict)}
    added = [turned_half(e, ids) for e in entities
             if (d := placed_model(e)) is not None and d >= SURROUND_MIN]
    entities.extend(added)
    return result, len(added)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def without_clouds(preset):
    if not isinstance(preset, dict) or not isinstance(preset.get('entities'), list):
        raise ValueError('The campfire preset has no entity list; left as it is')
    result = copy.deepcopy(preset)
    entities = result['entities']
    def cloud(e):
        return isinstance(e, dict) and CLOUDS in str(e.get('name', '')).lower()
    removed = [e.get('name') for e in entities if cloud(e)]
    result['entities'] = [e for e in entities if not cloud(e)]
    return result, removed


def without_light_shadows(preset):
    """The scene's lights kept, their shadows not: the three shadow-casting spot lights round the heroes
    (fill_spot99_01/02, rim_spot_98_02) fit their shadow maps to the menu's fixed camera, and seen from
    the headset they cut dark blocks into the heroes' faces that came and went with the head (owner
    2026-11-06: gone with the spot lights taken out - and the Necromancer's hair with them)."""
    turned = []
    for e in preset.get('entities', []):
        for c in e.get('components', []) if isinstance(e, dict) else []:
            if 'Light' in str(c.get('type', '')) and c.get('shadowCaster') is not False:
                # point lights carry no shadowCaster at all - written out so they cast none either
                # (owner 2026-11-07: blocks stayed with the spots' shadows off)
                c['shadowCaster'] = False
                turned.append(f'{e.get("name")} (shadow off)')
    return preset, turned


def spots_as_points(preset):
    """The scene's spot lights turned into point lights of the same colour, power and reach (testing: the
    spot lights' shading cut screen-aligned dark blocks into the heroes' faces in the headset)."""
    turned = []
    for e in preset.get('entities', []):
        for i, c in enumerate(e.get('components', []) if isinstance(e, dict) else []):
            if c.get('type') != 'SpotLightDefinitionComponent':
                continue
            e['components'][i] = {
                'type': 'PointLightDefinitionComponent', 'name': str(c.get('name', '')).replace('Spot', 'Point'),
                'color': c.get('color'), 'power': c.get('power'), 'radius': c.get('distance'),
                'attenuation': c.get('attenuation'), 'lightMask': c.get('lightMask'), 'isLocalLight': c.get('isLocalLight'),
                'diffuseContribution': c.get('diffuseContribution'), 'specularContribution': c.get('specularContribution')}
            turned.append(f'{e.get("name")} (as a point light)')
    return preset, turned


def without(preset, names):
    """The scene's own entities whose names hold any of `names` (lower case) taken out too."""
    if not names:
        return preset, []
    def hit(e):
        n = str(e.get('name', '')).lower()
        return isinstance(e, dict) and MIRROR not in n and any(x.lower() in n for x in names)
    removed = [e.get('name') for e in preset['entities'] if hit(e)]
    preset['entities'] = [e for e in preset['entities'] if not hit(e)]
    return preset, removed


def with_marks(preset, names, step=20.0):
    """Testing: which model is which in the headset. The k-th of `names` (an entity name, or a part of
    one) gets k copies of itself floating one over the other, `step` world units apart - count them."""
    ids = {int(e.get('id', 0)) for e in preset['entities'] if isinstance(e, dict)}
    marked = []
    for k, name in enumerate(names, 1):
        src = next((e for e in preset['entities'] if isinstance(e, dict) and name.lower() in str(e.get('name', '')).lower()), None)
        if src is None:
            marked.append(f'{name}: not found')
            continue
        for j in range(1, k + 1):
            e = copy.deepcopy(src)
            e['name'] = f'{src.get("name")}_mark{j}'
            new = (int(src.get('id', 0)) * 2246822519 + j * 0x9E3779B9) & 0xFFFFFFFF
            while new in ids or new == 0:
                new = (new + 0x01000193) & 0xFFFFFFFF
            ids.add(new)
            e['id'] = new
            for c in e.get('components', []):
                if c.get('type') == 'TransformDefinitionComponent' and isinstance(c.get('position'), dict):
                    c['position']['y'] = float(c['position'].get('y', 0.0)) + step * j
            preset['entities'].append(e)
        marked.append(f'{src.get("name")}: {k} copies over it')
    return preset, marked


MARKS = ()                      # --mark NAME... (testing)
UI_WALLS = False                # --ui-walls
SPIN = ()                       # --spin NAME...: entities turned half round about their own place
GROUND_TURNS = ()               # --ground-turns DEG...: copies of the scene's own ground turned about the fire
GROUND_SINK = 0.06              # the copies a hair under the scene's own ground, so that it wins where both lie


COPIES = ()                     # --copy NAME:DX:DZ / --copy-at NAME:X:Z:DY


def with_copies(preset, specs):
    """Copies of entities: NAME:DX:DZ moved by (DX, DZ) from the original; @NAME:X:Z:DY with their origin
    at (X, Z) from the fire and DY over the original's height (then :YAW, :SCALE). (Rocks lengthened, a patch of ground.)"""
    entities = preset['entities']
    ids = {int(e.get('id', 0)) for e in entities if isinstance(e, dict)}
    done = []
    for k, spec in enumerate(specs):
        at = spec.startswith('@')
        parts = spec.lstrip('@').split(':')
        name, nums = parts[0], [float(v) for v in parts[1:]]
        src = next((e for e in entities if isinstance(e, dict) and str(e.get('name', '')).lower() == name.lower()), None)
        if src is None:
            done.append(f'{spec}: not found')
            continue
        e = copy.deepcopy(src)
        e['name'] = f'{src.get("name")}_copy{k:02d}'
        new = (int(src.get('id', 0)) * 2246822519 + (k + 1) * 0x85EBCA6B) & 0xFFFFFFFF
        while new in ids or new == 0:
            new = (new + 0x01000193) & 0xFFFFFFFF
        ids.add(new); e['id'] = new
        for c in e.get('components', []):
            if c.get('type') == 'TransformDefinitionComponent' and isinstance(c.get('position'), dict):
                p = c['position']
                if at:
                    p['x'], p['z'] = FIRE[0] + nums[0], FIRE[1] + nums[1]
                    p['y'] = float(p.get('y', 0.0)) + (nums[2] if len(nums) > 2 else 0.0)
                    q = c.get('orientation')
                    if len(nums) > 3 and isinstance(q, dict):   # :YAW - turned about its origin, qy * q
                        h = math.radians(nums[3]) / 2
                        bx, by, bz, bw = 0.0, math.sin(h), 0.0, math.cos(h)
                        x0, y0, z0, w0 = (float(q.get(k, 0.0)) for k in 'xyzw')
                        q['x'] = bw * x0 + bx * w0 + by * z0 - bz * y0
                        q['y'] = bw * y0 - bx * z0 + by * w0 + bz * x0
                        q['z'] = bw * z0 + bx * y0 - by * x0 + bz * w0
                        q['w'] = bw * w0 - bx * x0 - by * y0 - bz * z0
                    if len(nums) > 4:   # :SCALE - the copy made SCALE times its size, about its origin
                        sc = c.setdefault('scale', {'x': 1.0, 'y': 1.0, 'z': 1.0})
                        for k in 'xyz':
                            sc[k] = float(sc.get(k, 1.0)) * nums[4]
                else:
                    p['x'] = float(p.get('x', 0.0)) + nums[0]
                    p['z'] = float(p.get('z', 0.0)) + nums[1]
        entities.append(e)
        done.append(f'{spec}: copied')
    return preset, done


GROWS = ()                      # --grow NAME:F:CX:CZ


def with_grows(preset, specs):
    """NAME:F:CX:CZ - an entity made F times larger about the point (CX, CZ) from the fire (its middle):
    the ground under the fire grown over the seam to the next ground."""
    done = []
    for spec in specs:
        name, f, cx, cz = spec.split(':')
        f, cx, cz = float(f), float(cx), float(cz)
        e = next((e for e in preset['entities'] if isinstance(e, dict) and str(e.get('name', '')).lower() == name.lower()), None)
        if e is None:
            done.append(f'{spec}: not found')
            continue
        for c in e.get('components', []):
            if c.get('type') == 'TransformDefinitionComponent' and isinstance(c.get('position'), dict):
                p = c['position']
                px, pz = float(p['x']) - FIRE[0], float(p['z']) - FIRE[1]
                p['x'] = FIRE[0] + cx + (px - cx) * f
                p['z'] = FIRE[1] + cz + (pz - cz) * f
                sc = c.setdefault('scale', {'x': 1.0, 'y': 1.0, 'z': 1.0})
                for k in 'xz':
                    sc[k] = float(sc.get(k, 1.0)) * f
        done.append(f'{spec}: grown')
    return preset, done


def with_ground_round(preset, turns):
    """The scene's own ground (its ui_groundplane terrain pieces) copied round the fire by `turns` degrees:
    the floor the player stands on, where the scene ended in the town's own ground."""
    entities = preset['entities']
    sources = [e for e in entities if isinstance(e, dict) and 'ui_groundplane' in json.dumps(e.get('components', [])).lower()
               and MIRROR not in str(e.get('name', ''))]
    ids = {int(e.get('id', 0)) for e in entities if isinstance(e, dict)}
    made = 0
    for turn in turns:
        t = math.radians(turn)
        ct, st = math.cos(t), math.sin(t)
        qy = (0.0, math.sin(t * 0.5), 0.0, math.cos(t * 0.5))
        for src in sources:
            e = copy.deepcopy(src)
            e['name'] = f'{src.get("name")}_ground{int(turn)}{MIRROR}'
            new = (int(src.get('id', 0)) * 2654435761 + int(turn) * 0x9E3779B9 + 7) & 0xFFFFFFFF
            while new in ids or new == 0:
                new = (new + 0x01000193) & 0xFFFFFFFF
            ids.add(new); e['id'] = new
            for c in e.get('components', []):
                if c.get('type') != 'TransformDefinitionComponent' or not isinstance(c.get('position'), dict):
                    continue
                p = c['position']
                x, z = float(p.get('x', 0.0)) - FIRE[0], float(p.get('z', 0.0)) - FIRE[1]
                p['x'] = FIRE[0] + ct * x + st * z
                p['z'] = FIRE[1] - st * x + ct * z
                p['y'] = float(p.get('y', 0.0)) - GROUND_SINK
                q = c.get('orientation')
                if isinstance(q, dict):   # qy * q
                    x0, y0, z0, w0 = (float(q.get(k, 0.0)) for k in 'xyzw')
                    bx, by, bz, bw = qy
                    q['x'] = bw * x0 + bx * w0 + by * z0 - bz * y0
                    q['y'] = bw * y0 - bx * z0 + by * w0 + bz * x0
                    q['z'] = bw * z0 + bx * y0 - by * x0 + bz * w0
                    q['w'] = bw * w0 - bx * x0 - by * y0 - bz * z0
            entities.append(e)
            made += 1
    return preset, [f'+{made} copies of the scene ground turned {list(turns)}']


def with_spins(preset, names):
    """Entities (by name or part of one) turned 180 degrees about their own vertical axis, where they stand:
    the town's models built for its own camera show the player their other side."""
    done = []
    for spec in names:
        name, _, deg = spec.partition(':')
        deg = float(deg) if deg else 180.0   # NAME[:DEG], about +y (on the map: + counterclockwise)
        h = math.radians(deg) / 2
        bx, by, bz, bw = 0.0, math.sin(h), 0.0, math.cos(h)
        hit = [e for e in preset['entities'] if isinstance(e, dict) and name.lower() == str(e.get('name', '')).lower()]
        if not hit:
            hit = [e for e in preset['entities'] if isinstance(e, dict) and name.lower() in str(e.get('name', '')).lower()]
        for e in hit:
            for c in e.get('components', []):
                q = c.get('orientation') if c.get('type') == 'TransformDefinitionComponent' else None
                if isinstance(q, dict):   # qy * q
                    x0, y0, z0, w0 = (float(q.get(k, 0.0)) for k in 'xyzw')
                    q['x'] = bw * x0 + bx * w0 + by * z0 - bz * y0
                    q['y'] = bw * y0 - bx * z0 + by * w0 + bz * x0
                    q['z'] = bw * z0 + bx * y0 - by * x0 + bz * w0
                    q['w'] = bw * w0 - bx * x0 - by * y0 - bz * z0
        done.append(f'{name}: {len(hit)} turned {deg:g}')
    return preset, done
UI_WALL_LENGTH = 26.8           # ui_wall01's length along its line, world units (from the scene's own row)
UI_WALL_FAR_YAW = -90.0         # its turn on the north line, degrees (its face toward the fire)


def with_ui_walls(preset):
    """The town's west and north walls round the square as the scene's own brick-and-stone wall
    (ui_wall01, the gate's set) instead of the town's columned pieces: those taken out, copies of the
    scene's own ui_wall01 laid end to end where they stood."""
    entities = preset['entities']
    src = next((e for e in entities if isinstance(e, dict) and e.get('name') == 'ui_wall01_01'), None)
    if src is None:
        return preset, ['ui_wall01_01 not found: town walls kept']
    def where(e):
        tr = next((c for c in e.get('components', []) if c.get('type') == 'TransformDefinitionComponent'
                   and isinstance(c.get('position'), dict)), None)
        return (float(tr['position']['x']) - FIRE[0], float(tr['position']['z']) - FIRE[1]) if tr else None
    def town_wall(e):
        if not isinstance(e, dict) or MIRROR not in str(e.get('name', '')):
            return False
        model = str(next((c.get('filename', '') for c in e.get('components', []) if c.get('type') == 'ModelDefinitionComponent'), '')).lower()
        at = where(e)
        if WALLS not in model or at is None:
            return False
        x, z = at
        return (abs(x + 59.0) < 5.0 and -130.0 < z < 125.0) or (z > 120.0 and x < 150.0)
    taken = sum(1 for e in entities if town_wall(e))
    preset['entities'] = entities = [e for e in entities if not town_wall(e)]
    ids = {int(e.get('id', 0)) for e in entities if isinstance(e, dict)}
    sx, sz = where(src)
    L = UI_WALL_LENGTH
    spots = []   # (x, z, yaw) from the fire
    z = -16.4 + 11.0 + L / 2           # north of the gate (its half width ~11)
    while z - L / 2 < 132.0:
        spots.append((sx, z, None)); z += L
    for z in (-50.0, -96.8 - L / 4 - L / 2):   # between the stone fence and the scene's own wall, and past its half wall
        spots.append((sx, z, None))
    x = -59.0 + L / 2
    while x + L / 2 <= 150.0:
        spots.append((x, 137.0, UI_WALL_FAR_YAW)); x += L
    for k, (x, z, yaw) in enumerate(spots):
        e = copy.deepcopy(src)
        e['name'] = f'ui_wall01_pcvr_{k:02d}'
        new = (0x51A11000 + k * 0x01000193) & 0xFFFFFFFF
        while new in ids or new == 0:
            new = (new + 0x01000193) & 0xFFFFFFFF
        ids.add(new); e['id'] = new
        for c in e.get('components', []):
            if c.get('type') == 'TransformDefinitionComponent':
                c['position']['x'] = FIRE[0] + x
                c['position']['z'] = FIRE[1] + z
                if yaw is not None:
                    h = math.radians(yaw) / 2
                    c['orientation'] = {'x': 0.0, 'y': math.sin(h), 'z': 0.0, 'w': math.cos(h)}
        entities.append(e)
    return preset, [f'-{taken} town wall pieces, +{len(spots)} ui_wall01 round the square']


FLIP_FAR = False                # --flip-far-wall
FAR_SHIFT = 0.0                 # --far-wall-shift X: the turned far wall moved along x (its pieces turn about one end)
FLIP_SIDE = 'all'               # --wall-flip-side north|south: only that part of the west wall turned


def generate(assets, mod_name='D2R-PCVR', surround=True, town_at=TOWN_AT_GATE, town_turn=0.0, ground=True, sky_turns=SKY_TURNS,
             flip_wall=True, cap=False, clutter=False, drop=(), shadows_off=True):
    if mod_name not in ('D2R-PCVR', 'pcvr-dev'):
        raise ValueError('Unsupported destination mod')
    raw = assets.read(PRESET)
    if len(raw) > 16 * 1024 * 1024:
        raise ValueError('Unexpectedly large preset')
    result, removed = without_clouds(json.loads(raw))
    if not removed:
        raise ValueError('Campfire preset changed: no cloud effect found; native preset retained')
    result, dropped = without(result, drop)
    removed = removed + dropped
    if shadows_off:
        result, turned = without_light_shadows(result)
        removed = removed + turned
    if POINTS:
        result, turned = spots_as_points(result)
        removed = removed + turned
    if SPOT_SPECULAR is not None:
        for e in result.get('entities', []):
            for c in e.get('components', []) if isinstance(e, dict) else []:
                if c.get('type') == 'SpotLightDefinitionComponent':
                    c['specularContribution'] = SPOT_SPECULAR
        removed = removed + [f'spot lights specular x{SPOT_SPECULAR}']
    if surround and town_at is not None:
        town = json.loads(assets.read(TOWN))
        result, placed = with_harrogath_behind(result, town, town_at, town_turn, ground=ground, flip_wall=flip_wall, clutter=clutter,
                                               wall_fill=WALL_FILL)
        removed = removed + [f'+{placed} Harrogath models round the player (town point {town_at}, turned {town_turn})']
    elif surround:
        result, mirrored = with_town_behind(result)
        removed = removed + [f'+{mirrored} town models behind the player']
    if surround and sky_turns:
        result, skies = with_sky_round(result, sky_turns)
        removed = removed + [f'+{skies} copies of the sky turned {list(sky_turns)}']
    if surround and cap:
        result, capped = with_sky_cap(result)
        removed = removed + [f'+{capped} sky over the player']
    if surround and UI_WALLS:
        result, walls = with_ui_walls(result)
        removed = removed + walls
    if GROWS:
        result, grown = with_grows(result, GROWS)
        removed = removed + grown
    if COPIES:
        result, copied = with_copies(result, COPIES)
        removed = removed + copied
    if GROUND_TURNS:
        result, grounds = with_ground_round(result, GROUND_TURNS)
        removed = removed + grounds
    if SPIN:
        result, spun = with_spins(result, SPIN)
        removed = removed + spun
    if PAVE:
        result, paved = with_pave(result)
        removed = removed + paved
    if TOWN_BIOME and surround:
        town_biome = json.loads(assets.read(TOWN)).get('biomeFilename')
        if town_biome:
            removed = removed + [f'biome {result.get("biomeFilename")} -> {town_biome}']
            result['biomeFilename'] = town_biome
    if LIGHTS:
        result, lit = with_lights(result)
        removed = removed + lit
    if MOVES or LIFTS:
        for e in result['entities']:
            if not isinstance(e, dict):
                continue
            name = str(e.get('name', '')).lower()
            tr = next((c for c in e.get('components', []) if c.get('type') == 'TransformDefinitionComponent'
                       and isinstance(c.get('position'), dict)), None)
            if tr is None:
                continue
            p = tr['position']
            for spec in MOVES:
                n_, dx, dy, dz = spec.split(':')
                if name == n_.lower():
                    p['x'] = float(p.get('x', 0.0)) + float(dx)
                    p['y'] = float(p.get('y', 0.0)) + float(dy)
                    p['z'] = float(p.get('z', 0.0)) + float(dz)
                    removed = removed + [f'moved: {spec}']
            if any(f in name for f in FLOORS):
                continue
            for spec in LIFTS:
                lx, lz, r, dy = (float(v) for v in spec.split(':'))
                if math.hypot(float(p.get('x', 0.0)) - FIRE[0] - lx, float(p.get('z', 0.0)) - FIRE[1] - lz) <= r:
                    p['y'] = float(p.get('y', 0.0)) + dy
                    removed = removed + [f'lifted {dy}: {e.get("name")}']
    if HIDE:
        names = {n.lower() for n in HIDE}
        gone = [e.get('name') for e in result['entities'] if isinstance(e, dict) and str(e.get('name', '')).lower() in names]
        result['entities'] = [e for e in result['entities'] if not (isinstance(e, dict) and str(e.get('name', '')).lower() in names)]
        removed = removed + [f'hidden: {n}' for n in gone]
    if MARKS:
        result, marked = with_marks(result, MARKS)
        removed = removed + marked
    output = {f'{mod_name}.mpq/' + PRESET: (json.dumps(result, ensure_ascii=False, indent=2) + '\n').encode('utf-8')}
    record = {'version': 1, 'build': getattr(assets, 'build_hash', ''), 'removed_entities': removed,
              'sha256': {name: digest(data) for name, data in output.items()}}
    output[RECEIPT] = (json.dumps(record, indent=2) + '\n').encode('utf-8')
    return output


def prepare(game, mod_name='D2R-PCVR', casclib=None, surround=True, town_at=TOWN_AT_GATE, town_turn=0.0, ground=True,
            sky_turns=SKY_TURNS, flip_wall=True, cap=False, clutter=False, drop=(), shadows_off=True):
    """Return a complete payload; the installer's transaction performs writes."""
    with open_assets(Path(game), casclib) as assets:
        return generate(assets, mod_name, surround, town_at, town_turn, ground, sky_turns, flip_wall, cap, clutter, drop, shadows_off)


def check_existing(mod, payload):
    """Never replace a player's untracked or edited preset override."""
    mod = Path(mod)
    if mod.resolve() != mod:
        raise ValueError('Campfire mod folder must not redirect to another location')
    receipt = mod / RECEIPT
    previous = {}
    if receipt.is_file():
        record = json.loads(receipt.read_text(encoding='utf-8'))
        if not isinstance(record, dict) or record.get('version') != 1 or not isinstance(record.get('sha256'), dict):
            raise ValueError('Unknown campfire preparation receipt')
        previous = record['sha256']
    for name, data in payload.items():
        path = mod / name
        if path.resolve() != path:
            raise ValueError('Campfire destination must not contain links')
        if not path.exists() or name == RECEIPT:
            continue
        old = path.read_bytes()
        if old != data and digest(old) != previous.get(name):
            raise ValueError(f'Existing campfire override left unchanged: {name}')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('game', type=Path)
    ap.add_argument('--casclib', type=Path, default=None)
    ap.add_argument('--mod', default='D2R-PCVR', choices=['D2R-PCVR', 'pcvr-dev'])
    ap.add_argument('--write', action='store_true')
    ap.add_argument('--replace', action='store_true', help='write over an override made without a receipt')
    ap.add_argument('--no-surround', action='store_true', help='clouds out only, no town behind the player')
    ap.add_argument('--town', nargs=2, type=float, metavar=('X', 'Z'), default=list(TOWN_AT_GATE),
                    help="the point of Harrogath (townwest.json) put on the fire; by default its gate on the scene's")
    ap.add_argument('--mirror', action='store_true', help="the scene's own half turned behind the player instead of the town")
    ap.add_argument('--turn', type=float, default=0.0, help='the town turned about the fire, degrees')
    ap.add_argument('--no-ground', action='store_true', help="without the town's own ground under it")
    ap.add_argument('--no-wall-flip', action='store_true', help="the town's west wall as the town has it")
    ap.add_argument('--cap', action='store_true', help="the Arreat Summit's sky card over the player (off: too well known a\n"
                    "view; the mod's own sky, [campfire] sky, fills the void over the player instead)")
    ap.add_argument('--cap-flip', action='store_true', help='the sky over the player turned the other way up')
    ap.add_argument('--drop', nargs='*', default=[], metavar='NAME',
                    help="the scene's own entities whose names hold NAME taken out too (testing)")
    ap.add_argument('--light-shadows', action='store_true', help="the scene's spot lights keep their shadows")
    ap.add_argument('--spots-as-points', action='store_true', help="the scene's spot lights as point lights (testing)")
    ap.add_argument('--spot-specular', type=float, default=None, help="the spot lights' specular share (testing)")
    ap.add_argument('--town-shadows', action='store_true', help="the town's models cast shadows too (testing)")
    ap.add_argument('--no-wall-fill', action='store_true', help="the town's wall out of the scene's whole half (holes by the gate)")
    ap.add_argument('--grow', nargs='*', default=[], metavar='SPEC', help='NAME:F:CX:CZ - F times larger about (CX, CZ) from the fire')
    ap.add_argument('--copy', nargs='*', default=[], metavar='SPEC',
                    help='copies: NAME:DX:DZ (moved from the original) or @NAME:X:Z:DY (origin at X, Z from the fire)')
    ap.add_argument('--ground-turns', nargs='*', type=float, default=[], metavar='DEG',
                    help="copies of the scene's own ground turned about the fire, degrees")
    ap.add_argument('--spin', nargs='*', default=[], metavar='NAME', help='entities turned 180 degrees where they stand')
    ap.add_argument('--ui-walls', action='store_true', help="the square's walls as the scene's own ui_wall01 (the gate's brick and stone)")
    ap.add_argument('--ui-wall-length', type=float, default=None, help='ui_wall01 length, world units (testing)')
    ap.add_argument('--ui-wall-far-yaw', type=float, default=None, help='ui_wall01 turn on the north line, degrees')
    ap.add_argument('--pave-hole', default=None, metavar='@X0:X1:Z0:Z1', help='a rectangle the road keeps clear of')
    ap.add_argument('--pave-z0', type=float, default=None, help='where the road\'s rows start (z)')
    ap.add_argument('--pave-skip', nargs='+', default=None, metavar='@X:Z', help='no road piece over these points (@ in front, for a minus)')
    ap.add_argument('--pave-tilt', nargs='+', default=None, metavar='A1:B2:DROP', help="road pieces tipped, their right edge DROP lower")
    ap.add_argument('--light', nargs='+', default=None, metavar='@X:Z:Y:R:G:B:POWER:RADIUS', help='point lights of its own')
    ap.add_argument('--move', nargs='+', default=None, metavar='NAME:DX:DY:DZ', help='entities moved, exact names')
    ap.add_argument('--lift', nargs='+', default=None, metavar='X:Z:R:DY', help='all but the ground within R of a point raised')
    ap.add_argument('--pave', action='store_true', help="the town ground under copies of the road to the gate")
    ap.add_argument('--town-biome', action='store_true', help="the scene under Harrogath's biome (its ground's textures)")
    ap.add_argument('--hide', nargs='+', default=None, metavar='NAME', help='entities taken out last, exact names')
    ap.add_argument('--town-keep', nargs='+', default=None, metavar='A1:B2',
                    help="grid rectangles (owner's map) where the town's own pieces stay inside the scene half")
    ap.add_argument('--far-wall-shift', type=float, default=None, help='the turned far wall moved along x, world units')
    ap.add_argument('--flip-far-wall', action='store_true', help="the town's north wall across the square turned round too")
    ap.add_argument('--wall-flip-side', choices=['all', 'north', 'south'], default='all',
                    help="the west wall turned only north of the gate (z > 0 from the fire) or only south of it")
    ap.add_argument('--mark', nargs='*', default=[], metavar='NAME',
                    help="testing: the k-th named entity gets k copies floating over it, to tell models apart")
    ap.add_argument('--clutter', action='store_true', help="the town's grass, snow piles and small props too")
    ap.add_argument('--sky', nargs='*', type=float, default=list(SKY_TURNS), metavar='DEG',
                    help='turns (degrees) of the copies of the sky round the fire; --sky alone: none')
    args = ap.parse_args()
    global CAP_FACE, POINTS, SPOT_SPECULAR, TOWN_SHADOWS, WALL_FILL, TOWN_KEEP, HIDE, TOWN_BIOME, PAVE
    PAVE = args.pave
    global PAVE_SKIP, PAVE_TILT, MOVES, LIFTS
    if args.pave_skip:
        PAVE_SKIP = tuple(tuple(float(v) for v in t.lstrip('@').split(':')) for t in args.pave_skip)
    if args.pave_tilt:
        PAVE_TILT = tuple((parse_cell(t.split(':')[0]), parse_cell(t.split(':')[1]), float(t.split(':')[2])) for t in args.pave_tilt)
    global PAVE_HOLE, PAVE_AREA
    if args.pave_hole:
        PAVE_HOLE = tuple(float(v) for v in args.pave_hole.lstrip('@').split(':'))
    if args.pave_z0 is not None:
        PAVE_AREA = (PAVE_AREA[0], PAVE_AREA[1], args.pave_z0, PAVE_AREA[3])
    global LIGHTS
    LIGHTS = tuple(args.light or ())
    MOVES = tuple(args.move or ())
    LIFTS = tuple(t.lstrip('@') for t in (args.lift or ()))
    TOWN_BIOME = args.town_biome
    if args.hide:
        HIDE = tuple(args.hide)
    if args.town_keep:
        TOWN_KEEP = tuple(tuple(parse_cell(p) for p in r.split(':')) for r in args.town_keep)
    TOWN_SHADOWS = args.town_shadows
    WALL_FILL = not args.no_wall_fill
    global MARKS, FLIP_SIDE, FLIP_FAR, UI_WALLS, UI_WALL_LENGTH, UI_WALL_FAR_YAW
    UI_WALLS = args.ui_walls
    global SPIN, GROUND_TURNS, COPIES, GROWS
    GROWS = tuple(args.grow)
    COPIES = tuple(args.copy)
    SPIN = tuple(args.spin)
    GROUND_TURNS = tuple(args.ground_turns)
    if args.ui_wall_length is not None: UI_WALL_LENGTH = args.ui_wall_length
    if args.ui_wall_far_yaw is not None: UI_WALL_FAR_YAW = args.ui_wall_far_yaw
    FLIP_FAR = args.flip_far_wall
    global FAR_SHIFT
    if args.far_wall_shift is not None: FAR_SHIFT = args.far_wall_shift
    MARKS = tuple(args.mark)
    FLIP_SIDE = args.wall_flip_side
    CAP_FACE = -1.0 if args.cap_flip else 1.0
    POINTS = args.spots_as_points
    SPOT_SPECULAR = args.spot_specular
    payload = prepare(args.game, args.mod, args.casclib, not args.no_surround,
                      None if args.mirror else tuple(args.town), args.turn, not args.no_ground,
                      tuple(args.sky), not args.no_wall_flip, args.cap, args.clutter, tuple(args.drop), not args.light_shadows)
    record = json.loads(payload[RECEIPT])
    print(f'{PRESET}: taking out {record["removed_entities"]}')
    if not args.write:
        print('(--write to write it)')
        return 0
    mod = args.game / 'mods' / args.mod
    if not args.replace:
        check_existing(mod, payload)
    for name, data in payload.items():
        target = mod / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
        print(f'written: {target}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
