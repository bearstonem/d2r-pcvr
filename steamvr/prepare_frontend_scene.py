#!/usr/bin/env python3
"""The character select screen's scene for VR ([campfire] select): its screen-fixed clouds and flat sky out.

The main menu's scene (data/hd/env/preset/ui/frontendact2.json) holds all five acts' sets side by side;
the game's camera stands before the one of the hero's last act. Each set has a cloud effect
(FX_UIFrontEndActN_clouds): sprites that face the camera, so in the headset they turn and tilt with the
head. Each also has a flat sky card (actN_sky_01) whose edges show once you look round, and which is not
the sky the mod draws in the void beyond it ([campfire] select_sky). This takes both out, so there is one
sky all round: the mod's, of the hero's act.

A first step only: the sets themselves are still built for one camera, with nothing behind you.

Prepared from the player's own game, never distributed. Not yet called by the installer.
Undo: delete <game>/mods/<mod>/<mod>.mpq/data/hd/env/preset/ui/frontendact2.json and pcvr-frontend-scene.json.

usage: prepare_frontend_scene.py <game> [--mod pcvr-dev] [--keep-sky] [--write]
"""
import argparse
import copy
import hashlib
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))   # the embedded runtime adds no script folder
from game_assets import open_assets  # noqa: E402

PRESET = 'data/hd/env/preset/ui/frontendact2.json'
RECEIPT = 'pcvr-frontend-scene.json'
CLOUDS = re.compile(r'^fx_uifrontendact[1-5]_clouds_\d+$')
SKY = re.compile(r'^act[1-5]_sky_\d+$')


def digest(data):
    return hashlib.sha256(data).hexdigest()


def without(preset, patterns):
    """The preset without the entities whose whole name (lower case) matches any of `patterns`."""
    if not isinstance(preset, dict) or not isinstance(preset.get('entities'), list):
        raise ValueError('The front-end preset has no entity list; left as it is')
    result = copy.deepcopy(preset)
    def hit(e):
        return isinstance(e, dict) and any(p.match(str(e.get('name', '')).lower()) for p in patterns)
    removed = [e.get('name') for e in result['entities'] if hit(e)]
    result['entities'] = [e for e in result['entities'] if not hit(e)]
    return result, removed


def generate(raw, mod_name='D2R-PCVR', keep_sky=False, build=''):
    if mod_name not in ('D2R-PCVR', 'pcvr-dev'):
        raise ValueError('Unsupported destination mod')
    if len(raw) > 32 * 1024 * 1024:
        raise ValueError('Unexpectedly large preset')
    result, removed = without(json.loads(raw), [CLOUDS] if keep_sky else [CLOUDS, SKY])
    if not any(CLOUDS.match(str(n).lower()) for n in removed):
        raise ValueError('Front-end preset changed: no cloud effect found; native preset retained')
    output = {f'{mod_name}.mpq/' + PRESET: (json.dumps(result, ensure_ascii=False, indent=2) + '\n').encode('utf-8')}
    record = {'version': 1, 'build': build, 'removed_entities': removed,
              'sha256': {name: digest(data) for name, data in output.items()}}
    output[RECEIPT] = (json.dumps(record, indent=2) + '\n').encode('utf-8')
    return output


def prepare(game, mod_name='D2R-PCVR', casclib=None, keep_sky=False):
    """Return a complete payload; the caller writes it."""
    with open_assets(Path(game), casclib) as assets:
        return generate(assets.read(PRESET), mod_name, keep_sky, getattr(assets, 'build_hash', ''))


def check_existing(mod, payload):
    """Never replace a player's untracked or edited preset override."""
    mod = Path(mod)
    receipt = mod / RECEIPT
    previous = {}
    if receipt.is_file():
        record = json.loads(receipt.read_text(encoding='utf-8'))
        if not isinstance(record, dict) or record.get('version') != 1 or not isinstance(record.get('sha256'), dict):
            raise ValueError('Unknown front-end preparation receipt')
        previous = record['sha256']
    for name, data in payload.items():
        path = mod / name
        if path.resolve() != path:
            raise ValueError('Front-end destination must not contain links')
        if not path.exists() or name == RECEIPT:
            continue
        old = path.read_bytes()
        if old != data and digest(old) != previous.get(name):
            raise ValueError(f'Existing front-end override left unchanged: {name}')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('game', type=Path)
    ap.add_argument('--casclib', type=Path, default=None)
    ap.add_argument('--mod', default='D2R-PCVR', choices=['D2R-PCVR', 'pcvr-dev'])
    ap.add_argument('--keep-sky', action='store_true', help="the sets' flat sky cards stay (the clouds alone go)")
    ap.add_argument('--write', action='store_true')
    args = ap.parse_args()
    payload = prepare(args.game, args.mod, args.casclib, args.keep_sky)
    print(f'{PRESET}: taking out {json.loads(payload[RECEIPT])["removed_entities"]}')
    if not args.write:
        print('(--write to write it)')
        return 0
    mod = args.game / 'mods' / args.mod
    check_existing(mod, payload)
    for name, data in payload.items():
        target = mod / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
        print(f'written: {target}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
