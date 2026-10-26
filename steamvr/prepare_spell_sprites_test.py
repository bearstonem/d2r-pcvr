"""Spell sprite cache contracts with synthetic textures; no Blizzard data in fixtures."""
import json
import struct
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch
import prepare_spell_sprites as sprites


def native(fmt, width=4, height=4, mips=1):
    """A D2R texture with valid mip offsets (relative to their own field)."""
    sizes = []
    for level in range(mips):
        w, h = max(1, width >> level), max(1, height >> level)
        sizes.append(w * h * 4 if fmt == 31 else ((w + 3) // 4) * ((h + 3) // 4) * (8 if fmt == 63 else 16))
    data = bytearray(40 + mips * 8)
    struct.pack_into('<IHHII', data, 0, 0x2845443c, fmt, 0, width, height)
    struct.pack_into('<I', data, 28, mips)
    for level, size in enumerate(sizes):
        struct.pack_into('<II', data, 36 + level * 8, size, len(data) - (40 + level * 8))
        data += bytes([level + 1]) * size
    return bytes(data)


class Assets:
    def __init__(self, missing=None):
        self.build_hash = 'fixture-build'
        self.reads = 0
        self.files = {}
        for name, missile, use in sprites.TEXTURES:
            path = f'data/hd/vfx/textures/{name}.texture'
            self.files[path] = native(31 if use == 'gradient' else 63, mips=2)
        for missile in {missile for _, missile, _ in sprites.TEXTURES}:
            particle = f'data/hd/vfx/particles/missiles/{missile}/effect.particles'
            self.files[f'data/hd/missiles/{missile}.json'] = json.dumps(
                {'dependencies': {'particles': [{'path': particle}]}}).encode()
            # The compiled effect names its textures among other binary data.
            self.files[particle] = b'\x00CParticleEffect' + b''.join(
                b'\x00' + f'data/hd/vfx/textures/{name}.texture'.encode()
                for name, owner, _ in sprites.TEXTURES if owner == missile and name != missing)
    def __enter__(self):
        return self
    def __exit__(self, *args):
        pass
    def read(self, name):
        self.reads += 1
        return self.files[name]


class SpellSpriteTests(unittest.TestCase):
    def test_texture_formats_and_mips(self):
        self.assertEqual(sprites.texture(native(63), 'mask'), (4, 4, 80, [bytes([1]) * 8]))
        self.assertEqual(sprites.texture(native(61), 'mask')[2], 77)
        width, height, fmt, mips = sprites.texture(native(31, 8, 4, 2), 'gradient')
        self.assertEqual((width, height, fmt, [len(m) for m in mips]), (8, 4, 29, [128, 32]))

    def test_invalid_textures_rejected(self):
        for data, use in ((native(63), 'gradient'), (native(31), 'mask'), (native(63)[:-1], 'mask'),
                          (native(63)[:40], 'mask'), (b'\0' + native(63)[1:], 'mask'), (native(63, 2, 4), 'mask')):
            with self.assertRaises(ValueError):
                sprites.texture(data, use)
        wrong = bytearray(native(63))
        struct.pack_into('<I', wrong, 36, 16)
        with self.assertRaises(ValueError):
            sprites.texture(bytes(wrong), 'mask')

    def test_cache_layout(self):
        data = sprites.encode([(4, 4, 80, [b'a' * 8, b'b' * 8]), (8, 4, 29, [b'c' * 128])])
        self.assertEqual(struct.unpack_from('<3I', data), (sprites.MAGIC, sprites.VERSION, 2))
        self.assertEqual(struct.unpack_from('<5I', data, 12), (0, 4, 4, 80, 2))
        self.assertEqual(struct.unpack_from('<I', data, 32), (8,))
        self.assertEqual(len(data), 12 + 20 + 2 * (4 + 8) + 20 + 4 + 128)

    def test_prepare_caches_and_reuses(self):
        with tempfile.TemporaryDirectory() as folder:
            assets = Assets()
            with patch.object(sprites, 'open_assets', return_value=assets):
                result = sprites.prepare('game', folder)
                self.assertEqual(result['textures'], len(sprites.TEXTURES))
                cache = (Path(folder) / sprites.FILE).read_bytes()
                self.assertEqual(struct.unpack_from('<3I', cache), (sprites.MAGIC, sprites.VERSION, len(sprites.TEXTURES)))
                reads = assets.reads
                self.assertEqual(sprites.prepare('game', folder), result)
                self.assertEqual(assets.reads, reads, 'an up-to-date cache is reused')
                assets.build_hash = 'patched-game'
                sprites.prepare('game', folder)
                self.assertGreater(assets.reads, reads, 'a game update rebuilds the cache')

    def test_textures_must_belong_to_their_spell(self):
        with tempfile.TemporaryDirectory() as folder:
            with patch.object(sprites, 'open_assets', return_value=Assets(missing='mask/T_FX_Flash_Center')):
                with self.assertRaises(ValueError):
                    sprites.prepare('game', folder)
            self.assertFalse((Path(folder) / sprites.FILE).exists(), 'nothing is written on failure')


if __name__ == '__main__':
    unittest.main()
