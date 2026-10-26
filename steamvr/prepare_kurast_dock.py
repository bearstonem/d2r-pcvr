"""Prepare Kurast Docks preset with the misplaced flying canoe removed.

Diablo II: Resurrected's native docktown preset (data/hd/env/preset/act3/docktown/docktown3.json)
contains a stray developer-placed canoe prefab (data/hd/env/model/act3/docktown/act3_docktown_boats/prefab/pf_canoe01_07_bob.json)
at world coordinate (0.0, 50.0, 0.0). In the original fixed isometric overhead view,
the coordinate is outside the camera frustum and completely invisible. In first-person
VR, the canoe is visible floating 50 meters in the sky and bobbing on invisible water.

This module extracts the preset locally from the player's own archives, removes only
the floating canoe entity, and stages the corrected preset in the mod directory.
No game assets, models, or textures are distributed.
"""
import argparse
import copy
import hashlib
import json
from pathlib import Path

from game_assets import open_assets

PRESET = 'data/hd/env/preset/act3/docktown/docktown3.json'
RECEIPT = 'pcvr-kurast-dock.json'
FLYING_CANOE_ID = 3418903870
CANOE_PREFAB = 'data/hd/env/model/act3/docktown/act3_docktown_boats/prefab/pf_canoe01_07_bob.json'


def digest(data):
    return hashlib.sha256(data).hexdigest()


def is_flying_canoe(entity):
    if not isinstance(entity, dict) or entity.get('type') != 'Entity':
        return False
    if entity.get('id') == FLYING_CANOE_ID:
        return True
    has_boat = False
    has_high_y = False
    for comp in entity.get('components', []):
        ctype = comp.get('type')
        if ctype == 'PrefabPlacementDefinitionComponent':
            prefab = str(comp.get('prefab', '')).replace('\\', '/').lower()
            if 'pf_canoe01_07_bob' in prefab or 'canoe' in prefab:
                has_boat = True
        elif ctype == 'TransformDefinitionComponent':
            pos = comp.get('position', {})
            if isinstance(pos, dict) and pos.get('y', 0) >= 25.0:
                has_high_y = True
    return has_boat and has_high_y


def remove_flying_canoe(source):
    if not isinstance(source, dict) or source.get('type') != 'Preset':
        raise ValueError('Expected an HD Preset object')
    entities = source.get('entities')
    if not isinstance(entities, list):
        raise ValueError('Invalid preset entity table')

    kept = []
    removed = 0
    for entity in entities:
        if is_flying_canoe(entity):
            removed += 1
        else:
            kept.append(entity)

    if removed != 1:
        raise ValueError(f'Expected exactly one flying canoe entity, found {removed}')

    result = copy.deepcopy(source)
    result['entities'] = kept
    return result, removed


def generate(assets, mod_name='D2R-PCVR'):
    if mod_name not in ('D2R-PCVR', 'pcvr-dev'):
        raise ValueError('Unsupported destination mod')
    raw = assets.read(PRESET)
    if len(raw) > 16 * 1024 * 1024:
        raise ValueError('Unexpectedly large preset')
    source = json.loads(raw)
    result, removed = remove_flying_canoe(source)

    output = {}
    preset_data = (json.dumps(result, ensure_ascii=False, indent=2) + '\n').encode('utf-8')
    output[f'{mod_name}.mpq/' + PRESET] = preset_data

    record = {
        'version': 1,
        'build': assets.build_hash,
        'removed_entities': removed,
        'sha256': {name: digest(data) for name, data in output.items()}
    }
    output[RECEIPT] = (json.dumps(record, indent=2) + '\n').encode('utf-8')
    return output


def prepare(game, mod_name='D2R-PCVR', casclib=None):
    """Return a complete payload; the installer's transaction performs writes."""
    with open_assets(Path(game), casclib) as assets:
        return generate(assets, mod_name)


def check_existing(mod, payload):
    """Never replace a player's untracked or edited preset override."""
    mod = Path(mod)
    if mod.resolve() != mod:
        raise ValueError('Kurast Docks mod folder must not redirect to another location')
    receipt = mod / RECEIPT
    if receipt.resolve() != receipt:
        raise ValueError('Kurast Docks preparation receipt points outside the mod')
    previous = {}
    if receipt.is_file():
        record = json.loads(receipt.read_text(encoding='utf-8'))
        if not isinstance(record, dict) or record.get('version') != 1 or not isinstance(record.get('sha256'), dict):
            raise ValueError('Unknown Kurast Docks preparation receipt')
        previous = record['sha256']
    for name, data in payload.items():
        path = mod / name
        if path.resolve() != path:
            raise ValueError('Kurast Docks destination must not contain links')
        if not path.exists() or name == RECEIPT:
            continue
        old = path.read_bytes()
        if old != data and digest(old) != previous.get(name):
            raise ValueError(f'Existing Kurast Docks override left unchanged: {name}')


def main():
    from game_install import validate_game
    from install import require_game_closed
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game', type=Path, required=True, help='Path to game directory')
    parser.add_argument('--mod', default='D2R-PCVR', choices=['D2R-PCVR', 'pcvr-dev'], help='Target mod name')
    parser.add_argument('--casclib', type=Path, default=None, help='Optional path to CascLib.dll')
    parser.add_argument('--experimental-battlenet', action='store_true', help='Allow experimental Battle.net validation')
    args = parser.parse_args()
    try:
        require_game_closed()
        validate_game(args.game, experimental_battlenet=args.experimental_battlenet)
        with open_assets(args.game, args.casclib) as assets:
            payload = generate(assets, args.mod)
        mod_root = args.game / 'mods' / args.mod
        check_existing(mod_root, payload)
        for name, data in payload.items():
            dest = mod_root / name
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_bytes(data)
        print(f'Prepared Kurast Docks preset (flying canoe removed) in {mod_root}.')
    except (OSError, ValueError, RuntimeError) as error:
        parser.exit(1, f'{error}\n')


if __name__ == '__main__':
    main()
