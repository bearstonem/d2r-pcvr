#!/usr/bin/env python3
"""Catalog the installed game's small level props (cups, plates, vases...) that
weapons can knock about.

Standard-library Python; Battle.net also needs CascLib on Windows. The catalog
holds only preset entity ids, positions and kinds read from the user's own game
files; it stays in the user's installation, never in the mod's release.

Every HD level preset (data/hd/env/preset/**.json) lists its entities with an
`id`. At run time the renderer keeps that id for each placed entity, and the
entity's local transform is the preset's TransformDefinitionComponent. The
plugin matches both (vr/prop_scan.cpp), so only these props can be moved.
"""
import argparse
import hashlib
import json
import os
import re
import struct
from pathlib import Path
from game_assets import open_assets

VERSION = 4   # the cache; 2 adds wall-faded props, static native bodies, furniture and cloth; 3 barrels, shovels, hay;
              # 4 loose weapons, shields, helms, tools, broken pottery, bones, more barrels, baskets and bags
FORMAT = 1    # props.bin layout, read by steamvr/props.h
MAGIC = 0x31505250   # 'PRP1'
FILES = ('props.bin',)
PRESETS = 'data/hd/env/preset/'
# A prop is placed on its own: a transform and a model, nothing that lights,
# emits or animates, so moving its matrix moves all of it. Fading with walls
# that cut away is kept. A static native body is allowed: only the HD
# renderer's own debris and ragdolls touch it, and it stays where it was.
PLAIN = {'TransformDefinitionComponent', 'ModelPlatformTierComponent', 'ModelDefinitionComponent'}
ALLOWED = PLAIN | {'WallTransparencyComponent'}
SHAPES = ('round', 'box', 'ball')
MATERIALS = ('ceramic', 'glass', 'metal', 'wood', 'paper', 'bone', 'wicker', 'food', 'cloth', 'furniture', 'stone', 'sack')
# Model name without its number -> collision shape and material. Candles,
# lanterns and lamps are left out: their flames are separate entities.
KINDS = {
    **{name: ('round', 'ceramic') for name in (
        'cup', 'mug', 'plate', 'plate_small', 'act1_plate', 'act2_plate', 'offering_plate', 'serving_platter',
        'plate_stack', 'bowl_set', 'bowl_stack', 'bowl_stack_small', 'stack_o_bowls', 'fruit_bowl',
        'bowl', 'bowl_angled', 'dish', 'jug', 'stoneware_jug', 'pitcher', 'jar', 'canopic_jar',
        'urn', 'act2_urn', 'expansion_urn', 'offering_urn', 'pot', 'town_pot', 'chamber_pot',
        'vase', 'vase_small', 'vase_white', 'vase_purple', 'vase_red', 'vase_brown', 'vase_blue')},
    **{name: ('round', 'glass') for name in (
        'bottle', 'bottle_small', 'bottle_green', 'bottle_brown', 'bottle_glass', 'bottle_flower')},
    **{name: ('round', 'metal') for name in ('pail', 'bucket', 'pan', 'kettle')},
    **{name: ('box', 'metal') for name in ('knife', 'tool_knife', 'tool_spoon', 'hammer', 'shovel')},
    'barrel': ('round', 'wood'),
    **{name: ('box', 'paper') for name in (
        'book', 'book_green', 'book_red', 'book_orange', 'book_open', 'arcane_book', 'scrolls', 'book_stack',
        'package_brown', 'package_yellow', 'package_white')},
    **{name: ('box', 'bone') for name in ('skull', 'skull_single', 'animal_skull', 'skull_animal', 'skull_ornate', 'bone', 'shinbone')},
    **{name: ('round', 'wicker') for name in ('basket', 'basket_woven', 'basket_dirty', 'act3_basket', 'plate_woven', 'basket_stack')},
    **{name: ('ball', 'food') for name in ('apple', 'potato')},
    'fish': ('box', 'food'),
    **{name: ('box', 'cloth') for name in (
        'pillow', 'pillow_blue', 'pillow_red', 'pillow_green', 'pillow_purple', 'cloth_folded', 'crumpled_cloth', 'rag')},
    **{name: ('box', 'wood') for name in ('crate', 'container', 'figurine_box', 'firewood', 'firewood_log')},
    **{name: ('box', 'furniture') for name in ('chair', 'stool', 'act1_stool', 'barstool')},
    **{name: ('box', 'stone') for name in ('tablet', 'tablet_small')},
    **{name: ('box', 'sack') for name in ('sack', 'sack_small', 'hay_bale')},
    # 4: what lies about in houses, barns, camps and dungeons (owner 2026-10-25, in an Act 1
    # house: "we should probably map them"). Groups (crates, sacks, barrels as one model),
    # wall-mounted pieces, candles and lanterns stay out; anything over MaxExtent (1.2 m, a
    # long spear or a wagon wheel) is left where it is at run time.
    **{name: ('round', 'wood') for name in (
        'l_barrel', 'r_barrel', 'global_barrel', 'global_barrel01_open', 'global_barrel02_open', 'burned_barrel',
        'barrel_broken', 'cell_barrel', 'drum', 'wheel', 'wheel_broken', 'wagonwheel')},
    **{name: ('box', 'furniture') for name in ('chair_broken', 'stoolframe', 'stool_frame', 'bench', 'act1_bench')},
    **{name: ('box', 'wood') for name in (
        'wood_board_broken', 'burned_board', 'firewood_a', 'wood_log', 'log', 'burnt_log', 'cutting_board',
        'sticks_bundled', 'crate01_open', 'crate02_open', 'quiver', 'bow', 'crossbow', 'arrows', 'arrow',
        'club_devilkin', 'axe_handle', 'shield_wood')},
    **{name: ('box', 'metal') for name in (
        'sword', 'sword_rusty', 'sword_corrupted', 'sword_bare', 'short_sword', 'bastard_sword', 'claymore_sword',
        'dagger', 'axe', 'axe_crude', 'battle_axe', 'great_axe', 'large_axe', 'spear', 'spear_corrupted',
        'fishing_spear', 'mace', 'shield', 'shield_rusty', 'shield_corrupted', 'shield_fallen', 'kite_shield',
        'buckler_shield', 'body_shield', 'armor_shoulderpad_fallen', 'tongs', 'tools', 'tool', 'tool_cleaver',
        'tool_sharpener', 'mining_tools', 'rake', 'pitchfork')},
    **{name: ('round', 'metal') for name in ('armor_helm', 'armor_helm_cap', 'overseer_helmet')},
    **{name: ('round', 'ceramic') for name in (
        'bowl_broken', 'plate_broken', 'vase_brown_broken', 'vase_blue_broken', 'vase_red_broken', 'canopic_jar_broken')},
    **{name: ('box', 'bone') for name in (
        'skull_fragment', 'long_skull', 'short_skull', 'baboon_skull', 'skull_mandible', 'skull_animal_mandible',
        'rib_cage', 'horn')},
    'onion': ('ball', 'food'),
    **{name: ('box', 'food') for name in ('meat', 'fish_bunch')},
    **{name: ('box', 'sack') for name in ('bag', 'sack_striped')},
    **{name: ('round', 'wicker') for name in (
        'act3_basket01_open', 'act3_basket02_open', 'act3_basket03_open', 'lobster_pot', 'fishing_trap')},
    'figurine': ('box', 'stone'),
}
NUMBER = re.compile(r'_?\d+$')


def kind_of(model):
    """The KINDS key for a model path, or None."""
    stem = str(model).replace('\\', '/').rsplit('/', 1)[-1].lower()
    if not stem.endswith('.model'):
        return None
    stem = NUMBER.sub('', stem[:-len('.model')])
    return stem if stem in KINDS else None


def static_body(component):
    return component.get('type') == 'PhysicsBodyDefinitionComponent' and component.get('bodytype') == 'static'


def entities(preset):
    """(id, kind, position) for each plain catalogued prop in one preset."""
    for entity in preset.get('entities', []) if isinstance(preset, dict) else []:
        if not isinstance(entity, dict):
            continue
        components = [c for c in entity.get('components', []) if isinstance(c, dict)]
        if not components or any(c.get('type') not in ALLOWED and not static_body(c) for c in components):
            continue
        model = next((c.get('filename') for c in components if c.get('type') == 'ModelDefinitionComponent'), None)
        transform = next((c for c in components if c.get('type') == 'TransformDefinitionComponent'), None)
        kind = kind_of(model) if model else None
        identifier = entity.get('id')
        if not kind or not transform or not isinstance(identifier, int) or not 0 < identifier < 2**32:
            continue
        try:
            position = tuple(float(transform['position'][axis]) for axis in 'xyz')
        except (KeyError, TypeError, ValueError):
            continue
        if all(abs(v) < 1e6 for v in position):
            yield identifier, kind, position


def lookup(assets, wanted):
    """Preset entities by id (pcvr-dev's "not catalogued in reach" log): file, model and components."""
    found = []
    for name in sorted(n for n in assets.files if n.startswith(PRESETS) and n.endswith('.json')):
        try:
            preset = json.loads(assets.read(name))
        except ValueError:
            continue
        for entity in preset.get('entities', []) if isinstance(preset, dict) else []:
            if not isinstance(entity, dict) or entity.get('id') not in wanted:
                continue
            components = [c for c in entity.get('components', []) if isinstance(c, dict)]
            model = next((c.get('filename') for c in components if c.get('type') == 'ModelDefinitionComponent'), None)
            transform = next((c for c in components if c.get('type') == 'TransformDefinitionComponent'), {})
            found.append({'id': entity['id'], 'preset': name[len(PRESETS):], 'model': model, 'kind': kind_of(model) if model else None,
                          'position': transform.get('position'), 'components': sorted({c.get('type') for c in components})})
    return found


def encode(rows):
    """props.bin from (id, kind, position) rows: header, kinds, entries sorted by id."""
    kinds = sorted({kind for _, kind, _ in rows})
    index = {kind: i for i, kind in enumerate(kinds)}
    unique = {}
    for identifier, kind, position in rows:
        key = (identifier, *(round(v, 4) for v in position))
        unique.setdefault(key, (identifier, kind, position))
    entries = sorted(unique.values(), key=lambda r: (r[0], r[2]))
    if len(entries) > 1 << 20 or len(kinds) > 255:
        raise ValueError('Unexpected prop catalog size')
    out = bytearray(struct.pack('<4I', MAGIC, FORMAT, len(kinds), len(entries)))
    for kind in kinds:
        shape, material = KINDS[kind]
        out += struct.pack('<28sBBH', kind.encode('ascii'), SHAPES.index(shape), MATERIALS.index(material), 0)
    for identifier, kind, position in entries:
        out += struct.pack('<I3fHH', identifier, *position, index[kind], 0)
    return bytes(out), len(entries), kinds


def prepare(game, destination=None, force=False, casclib=None):
    game = Path(game)
    destination = Path(destination) if destination else game / 'd2rloader/plugins/d2r_vr_props'
    destination.mkdir(parents=True, exist_ok=True)
    metadata = destination / 'cache.json'
    with open_assets(game, casclib) as assets:
        if not force and metadata.is_file():
            try:
                previous = json.loads(metadata.read_text())
                if previous['version'] == VERSION and previous['build'] == assets.build_hash and \
                        hashlib.sha256((destination / 'props.bin').read_bytes()).hexdigest() == previous['sha256']:
                    return previous
            except (ValueError, KeyError, OSError):
                pass
        rows, presets = [], 0
        for name in sorted(n for n in assets.files if n.startswith(PRESETS) and n.endswith('.json')):
            try:
                preset = json.loads(assets.read(name))
            except ValueError:
                continue
            presets += 1
            rows.extend(entities(preset))
        if not presets or not rows:
            raise ValueError('No level presets with catalogued props in this installation')
        data, count, kinds = encode(rows)
        result = {'version': VERSION, 'build': assets.build_hash, 'presets': presets, 'props': count,
                  'kinds': len(kinds), 'sha256': hashlib.sha256(data).hexdigest()}
        temp = destination / 'props.bin.tmp'
        temp.write_bytes(data)
        os.replace(temp, destination / 'props.bin')
        temp = destination / 'cache.json.tmp'
        temp.write_text(json.dumps(result, indent=2) + '\n')
        os.replace(temp, metadata)
        return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game', required=True, type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--force', action='store_true')
    parser.add_argument('--casclib', type=Path, help='Packaged Unicode x64 CascLib 3.0 (Battle.net only)')
    parser.add_argument('--lookup', type=int, nargs='+', metavar='ID', help='Show these preset entities instead (no catalog written)')
    args = parser.parse_args()
    if args.lookup:
        with open_assets(args.game, args.casclib) as assets:
            for row in lookup(assets, set(args.lookup)):
                print(json.dumps(row))
        raise SystemExit(0)
    result = prepare(args.game, args.output, args.force, args.casclib)
    print(f"Knockable prop catalog: {result['props']} props of {result['kinds']} kinds from {result['presets']} level presets")
