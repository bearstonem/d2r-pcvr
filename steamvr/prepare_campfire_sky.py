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
        if in_scene_half(dx, dz):
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
                and abs(float(tr['position'].get('x', 0.0)) - WALL_LINE_X) < 4.0)
        new = (int(e.get('id', 0)) * 2246822519 + 0x27D4EB2F) & 0xFFFFFFFF
        while new in ids or new == 0:
            new = (new + 0x01000193) & 0xFFFFFFFF
        ids.add(new)
        e['id'] = new
        for c in e.get('components', []):
            if c.get('type') != 'TransformDefinitionComponent':
                continue
            c['position']['x'] = FIRE[0] + dx
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
    ap.add_argument('--clutter', action='store_true', help="the town's grass, snow piles and small props too")
    ap.add_argument('--sky', nargs='*', type=float, default=list(SKY_TURNS), metavar='DEG',
                    help='turns (degrees) of the copies of the sky round the fire; --sky alone: none')
    args = ap.parse_args()
    global CAP_FACE, POINTS, SPOT_SPECULAR, TOWN_SHADOWS, WALL_FILL
    TOWN_SHADOWS = args.town_shadows
    WALL_FILL = not args.no_wall_fill
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
