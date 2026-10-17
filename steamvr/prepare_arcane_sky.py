"""Prepare a surrounding Arcane sky from the player's own game assets.

Generated textures and visual definitions remain local; never package them.
The original screen-space weather cards are removed only from Arcane's visual
definitions. All lighting, fog, terrain and other effects remain unchanged.
"""
import copy
import hashlib
import json
import struct
from pathlib import Path

from game_assets import open_assets
from prepare_icons import png

VIS = 'data/hd/env/vis/'
PERIODS = ('dawn1', 'dawn2', 'day', 'dusk1', 'dusk2', 'night', 'desecrated')
WEATHER = 'data/hd/vfx/particles/env/common/weather/stars.particles'
TEXTURES = 'reshade/reshade-shaders/Textures/PCVR_Arcane/'
RECEIPT = 'pcvr-arcane-sky.json'


def bc4_texture(data):
    """Decode D2R's single-channel BC4 (format 63), retaining native texels."""
    if len(data) < 44:
        raise ValueError('Truncated Arcane texture')
    magic, fmt, _, width, height = struct.unpack_from('<IHHII', data)
    mips, = struct.unpack_from('<I', data, 28)
    if (magic != 0x2845443c or fmt != 63 or not 1 <= mips <= 14
            or not 4 <= width <= 2048 or not 4 <= height <= 2048
            or width % 4 or height % 4):
        raise ValueError('Unsupported Arcane texture layout')
    # Validate every declared mip before accepting even the first one.
    payloads = []
    for level in range(mips):
        if 44 + level * 8 > len(data):
            raise ValueError('Truncated Arcane mip table')
        size, relative = struct.unpack_from('<II', data, 36 + level * 8)
        offset = 40 + level * 8 + relative
        expected = ((max(1, width >> level) + 3) // 4) * ((max(1, height >> level) + 3) // 4) * 8
        if size != expected or offset < 36 + mips * 8 or offset + size > len(data):
            raise ValueError('Invalid Arcane texture mip')
        payloads.append(data[offset:offset + size])
    pixels = bytearray(width * height)
    for block in range(len(payloads[0]) // 8):
        raw = payloads[0][block * 8:block * 8 + 8]
        a, b = raw[:2]
        table = ([a, b] + [((7 - i) * a + i * b) // 7 for i in range(1, 7)] if a > b
                 else [a, b] + [((5 - i) * a + i * b) // 5 for i in range(1, 5)] + [0, 255])
        indices = int.from_bytes(raw[2:], 'little')
        bx, by = block % (width // 4) * 4, block // (width // 4) * 4
        for i in range(16):
            pixels[(by + i // 4) * width + bx + i % 4] = table[(indices >> (3 * i)) & 7]
    return width, height, bytes(pixels)


def replace_weather(source):
    if not isinstance(source, dict):
        raise ValueError('Expected an Arcane visual definition')
    result = copy.deepcopy(source)
    if result.get('type') != 'VisualDataDefinition':
        raise ValueError('Expected an Arcane visual definition')
    try:
        components = [c for entity in result.get('entities', []) for c in entity.get('components', [])
                      if c.get('type') == 'VisualDataComponent']
    except (TypeError, AttributeError) as error:
        raise ValueError('Invalid Arcane visual components') from error
    weather = components[0].get('screenspaceVfx') if len(components) == 1 else None
    if not isinstance(weather, str) or weather.replace('\\', '/').lower() != WEATHER:
        raise ValueError('Arcane weather definition changed; retaining native background')
    components[0]['screenspaceVfx'] = ''
    return result


def generate(assets, mod_name='D2R-PCVR'):
    if mod_name not in ('D2R-PCVR', 'pcvr-dev'):
        raise ValueError('Unsupported Arcane destination')
    output = {}
    stars = [bc4_texture(assets.read(f'data/hd/vfx/textures/noise/t_fx_stars_{layer}.texture'))
             for layer in ('far', 'mid', 'near')]
    if any((w, h) != (1024, 512) for w, h, _ in stars):
        raise ValueError('Unexpected Arcane star map dimensions')
    rgba = bytearray(1024 * 512 * 4)
    for channel, (_, _, pixels) in enumerate(stars):
        rgba[channel::4] = pixels
    rgba[3::4] = b'\xff' * (1024 * 512)
    output[TEXTURES + 'stars.png'] = png(1024, 512, rgba)
    width, height, cloud = bc4_texture(assets.read('data/hd/vfx/textures/noise/t_fx_cloud_noise_03.texture'))
    if (width, height) != (512, 512):
        raise ValueError('Unexpected Arcane cloud map dimensions')
    rgba = bytearray(width * height * 4)
    for channel in range(3):
        rgba[channel::4] = cloud
    rgba[3::4] = b'\xff' * (width * height)
    output[TEXTURES + 'clouds.png'] = png(width, height, rgba)
    for period in PERIODS:
        name = VIS + f'act2_arcane_{period}.json'
        data = assets.read(name)
        if len(data) > 1024 * 1024:
            raise ValueError('Oversized Arcane visual definition')
        output[f'{mod_name}.mpq/' + name] = (json.dumps(replace_weather(json.loads(data)),
                                                      ensure_ascii=False, separators=(',', ':')) + '\n').encode('utf-8')
    record = {'version': 1, 'build': assets.build_hash,
              'sha256': {name: hashlib.sha256(data).hexdigest() for name, data in output.items()}}
    output[RECEIPT] = (json.dumps(record, indent=2) + '\n').encode('utf-8')
    return output


def prepare(game, mod_name='D2R-PCVR', casclib=None):
    """Return a complete payload; the installer's transaction performs writes."""
    with open_assets(Path(game), casclib) as assets:
        return generate(assets, mod_name)


def check_existing(mod, payload):
    """Never replace a player's untracked or edited visual override."""
    mod = Path(mod)
    if mod.resolve() != mod:
        raise ValueError('Arcane mod folder must not redirect to another location')
    receipt = mod / RECEIPT
    if receipt.resolve() != receipt:
        raise ValueError('Arcane preparation receipt points outside the mod')
    previous = {}
    if receipt.is_file():
        record = json.loads(receipt.read_text(encoding='utf-8'))
        if not isinstance(record, dict) or record.get('version') != 1 or not isinstance(record.get('sha256'), dict):
            raise ValueError('Unknown Arcane preparation receipt')
        previous = record['sha256']
    for name, data in payload.items():
        path = mod / name
        if path.resolve() != path:
            raise ValueError('Arcane destination must not contain links')
        if not path.exists() or name == RECEIPT:
            continue
        old = path.read_bytes()
        if old != data and hashlib.sha256(old).hexdigest() != previous.get(name):
            raise ValueError(f'Existing Arcane override left unchanged: {name}')
