"""Prepare the immunity wisps' Feral Rage glow from the player's own game.

Only the sprite artwork is reused; the renderer owns motion and colors. No
native gameplay states, overlay tables or skill assets are replaced. Generated
artwork stays local and must never be included in source or release packages.
"""
import argparse
import hashlib
import json
from pathlib import Path

from game_assets import open_assets
from prepare_arcane_sky import bc4_texture
from prepare_icons import png

OVERLAY = 'data/hd/overlays/druid/feralrage_1.json'
PARTICLES = 'data/hd/vfx/particles/overlays/druid/feralrage_1/vfx_druid_feralrage1.particles'
TEXTURE = 'data/hd/vfx/textures/light/t_fx_light_dot_glow_gritty.texture'
IMAGE = 'reshade/reshade-shaders/Textures/PCVR_Immunity/feral_glow.png'
RECEIPT = 'pcvr-monster-immunity.json'


def generate(assets):
    overlay = json.loads(assets.read(OVERLAY).decode('utf-8-sig'))
    if overlay.get('type') != 'OverlayDefinition' or not any(
            p.get('path', '').lower() == PARTICLES for p in overlay.get('dependencies', {}).get('particles', [])):
        raise ValueError('Feral Rage overlay changed')
    particles = assets.read(PARTICLES)
    if len(particles) > 4 * 1024 * 1024 or TEXTURE.encode() not in particles.lower():
        raise ValueError('Feral Rage no longer references the expected glow')
    width, height, pixels = bc4_texture(assets.read(TEXTURE))
    if (width, height) != (256, 256):
        raise ValueError('Unexpected Feral Rage glow dimensions')
    rgba = bytearray(width * height * 4)
    for channel in range(3):
        rgba[channel::4] = pixels
    rgba[3::4] = b'\xff' * (width * height)
    data = png(width, height, rgba)
    receipt = {'version': 1, 'build': assets.build_hash, 'source': TEXTURE,
               'effect': PARTICLES, 'sha256': {IMAGE: hashlib.sha256(data).hexdigest()}}
    return {IMAGE: data, RECEIPT: (json.dumps(receipt, indent=2) + '\n').encode()}


def prepare(game, casclib=None):
    with open_assets(Path(game), casclib) as assets:
        return generate(assets)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True, help='Staging mod root; does not install or launch')
    parser.add_argument('--casclib', type=Path)
    args = parser.parse_args()
    payload = prepare(args.game, args.casclib)
    for name, data in payload.items():
        target = args.output / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
    print('Prepared Feral Rage glow in ' + str(args.output))
