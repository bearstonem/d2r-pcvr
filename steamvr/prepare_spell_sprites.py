#!/usr/bin/env python3
"""Cache the Sorceress hand spells' own effect textures from the installed game.

Elemental hand effects draw a few small sprites around each hand with the
textures the game's own Fire Bolt, Fireball, Charged Bolt, Ice Bolt and Ice
Blast effects use. They are read locally from the user's installation and
stay there, never in the mod's release. Native BC payloads are kept as they
are for direct GPU sampling.
"""
import argparse
import hashlib
import json
import os
import struct
from pathlib import Path
from game_assets import open_assets

VERSION = 1
MAGIC = 0x31505353  # 'SSP1'
FILE = 'sprites.ssp'
# The renderer's texture slots, in order: each texture, the spell effect that
# names it (checked against that effect's own particle file) and its use.
TEXTURES = (
    ('pyro/T_FX_fire_plate_med_04', 'firebolt', 'mask'),
    ('pyro/T_FX_fire_plate_med_03', 'fireball', 'mask'),
    ('mask/T_FX_softFlame_mask_', 'firebolt', 'mask'),
    ('gradient/T_FX_flame_gradient_02', 'firebolt', 'gradient'),
    ('mask/T_FX_Flash_Center', 'charged_bolt', 'mask'),
    ('mask/T_FX_soft_glowcard', 'charged_bolt', 'mask'),
    ('gradient/T_FX_lightning_Grad_04', 'charged_bolt', 'gradient'),
    ('mask/T_FX_Flash_Center2', 'ice_bolt', 'mask'),
    ('gradient/T_FX_IceBolt_gradient', 'ice_bolt', 'gradient'),
    ('geo/T_FX_FrozenOrb', 'ice_blast', 'mask'),
    ('gradient/T_FX_bluegreen_white_gradient_02', 'ice_blast', 'gradient'),
)
# D2R texture formats to DXGI: BC4 masks, BC3 read as linear masks, RGBA8
# colour ramps (sRGB, decoded during sampling).
FORMATS = {'mask': {63: 80, 61: 77, 62: 77}, 'gradient': {31: 29}}


def texture(data, use):
    """Return (width, height, DXGI format, mips) of one D2R texture.

    Header and mip offsets as in potion_assets.texture; offsets are relative
    to their own field. Every declared mip is validated before any is used.
    """
    if len(data) < 44:
        raise ValueError('Truncated sprite texture')
    magic, fmt, _, width, height = struct.unpack_from('<IHHII', data)
    mips, = struct.unpack_from('<I', data, 28)
    if magic != 0x2845443c or fmt not in FORMATS[use] or not 1 <= mips <= 14:
        raise ValueError('Unsupported sprite texture')
    if not 4 <= width <= 2048 or not 4 <= height <= 2048:
        raise ValueError('Invalid sprite texture size')
    payloads = []
    for level in range(mips):
        if 44 + level * 8 > len(data):
            raise ValueError('Truncated sprite mip table')
        size, relative = struct.unpack_from('<II', data, 36 + level * 8)
        w, h = max(1, width >> level), max(1, height >> level)
        expected = w * h * 4 if fmt == 31 else ((w + 3) // 4) * ((h + 3) // 4) * (8 if fmt == 63 else 16)
        offset = 40 + level * 8 + relative
        if size != expected or offset < 36 + mips * 8 or offset + size > len(data):
            raise ValueError('Invalid sprite texture mip')
        payloads.append(data[offset:offset + size])
    return width, height, FORMATS[use][fmt], payloads


def encode(textures):
    result = bytearray(struct.pack('<3I', MAGIC, VERSION, len(textures)))
    for index, (width, height, fmt, mips) in enumerate(textures):
        result += struct.pack('<5I', index, width, height, fmt, len(mips))
        for mip in mips:
            result += struct.pack('<I', len(mip)) + mip
    return bytes(result)


def particles(assets, missile):
    unit = json.loads(assets.read(f'data/hd/missiles/{missile}.json').decode('utf-8-sig'))
    return b''.join(assets.read(p['path']) for p in unit['dependencies']['particles']).lower()


def prepare(game, destination=None, force=False, casclib=None):
    game = Path(game)
    destination = Path(destination) if destination else game / 'd2rloader/plugins/d2r_vr_spell_sprites'
    destination.mkdir(parents=True, exist_ok=True)
    metadata = destination / 'cache.json'
    with open_assets(game, casclib) as assets:
        if not force and metadata.is_file():
            try:
                previous = json.loads(metadata.read_text())
                if previous['version'] == VERSION and previous['build'] == assets.build_hash and \
                        hashlib.sha256((destination / FILE).read_bytes()).hexdigest() == previous['sha256']:
                    return previous
            except (ValueError, KeyError, OSError):
                pass
        effects, textures, sources = {}, [], []
        for name, missile, use in TEXTURES:
            path = f'data/hd/vfx/textures/{name}.texture'
            if missile not in effects:
                effects[missile] = particles(assets, missile)
            if path.lower().encode() not in effects[missile]:
                raise ValueError(f'{missile} no longer uses {path}')
            textures.append(texture(assets.read(path), use))
            sources.append(path)
        # Decode/validate every texture before replacing the live cache.
        data = encode(textures)
        result = {'version': VERSION, 'build': assets.build_hash, 'textures': len(textures),
                  'sources': sources, 'sha256': hashlib.sha256(data).hexdigest()}
        temp = destination / (FILE + '.tmp')
        temp.write_bytes(data)
        os.replace(temp, destination / FILE)
        temp = destination / 'cache.json.tmp'
        temp.write_text(json.dumps(result, indent=2) + '\n')
        os.replace(temp, metadata)
        return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game', required=True, type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--casclib', type=Path)
    parser.add_argument('--force', action='store_true')
    args = parser.parse_args()
    result = prepare(args.game, args.output, args.force, args.casclib)
    print(f"Prepared {result['textures']} spell effect textures from your installed game.")
