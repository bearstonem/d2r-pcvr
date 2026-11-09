import json
import struct
import unittest
from unittest.mock import patch
import prepare_monster_immunity as prep


class Assets:
    build_hash = 'fixture'

    def __init__(self):
        self.files = {
            prep.OVERLAY: json.dumps({'type': 'OverlayDefinition', 'dependencies': {
                'particles': [{'path': prep.PARTICLES}]}}).encode(),
            prep.PARTICLES: b'CParticleEffect\0' + prep.TEXTURE.upper().encode(),
            prep.TEXTURE: b'fixture',
        }

    def read(self, name):
        return self.files[name]


class PreparationTests(unittest.TestCase):
    def test_validated_artwork_and_receipt(self):
        assets = Assets()
        with patch.object(prep, 'bc4_texture', return_value=(256, 256, b'\x80' * 65536)):
            payload = prep.generate(assets)
        self.assertEqual(set(payload), {prep.IMAGE, prep.RECEIPT})
        self.assertTrue(payload[prep.IMAGE].startswith(b'\x89PNG\r\n\x1a\n'))
        receipt = json.loads(payload[prep.RECEIPT])
        self.assertEqual(receipt['effect'], prep.PARTICLES)
        import hashlib
        self.assertEqual(receipt['sha256'][prep.IMAGE], hashlib.sha256(payload[prep.IMAGE]).hexdigest())

    def test_rejects_changed_effect(self):
        assets = Assets()
        assets.files[prep.PARTICLES] = b'different effect'
        with self.assertRaisesRegex(ValueError, 'references'):
            prep.generate(assets)
        assets = Assets()
        assets.files[prep.OVERLAY] = b'{"type":"UnitDefinition"}'
        with self.assertRaisesRegex(ValueError, 'overlay'):
            prep.generate(assets)

    def test_rejects_texture_layout_changes(self):
        with patch.object(prep, 'bc4_texture', return_value=(128, 128, b'')):
            with self.assertRaisesRegex(ValueError, 'dimensions'):
                prep.generate(Assets())

    def test_actual_bc4_decoder_validates_all_mips(self):
        # One real BC4 block: its sixteen pixels all select endpoint 255.
        header = bytearray(44)
        struct.pack_into('<IHHII', header, 0, 0x2845443c, 63, 0, 4, 4)
        struct.pack_into('<I', header, 28, 1)
        struct.pack_into('<II', header, 36, 8, 4)
        self.assertEqual(prep.bc4_texture(bytes(header) + bytes([255, 0]) + b'\0'*6), (4, 4, b'\xff'*16))
        with self.assertRaises(ValueError):
            prep.bc4_texture(bytes(header) + b'\0'*7)


if __name__ == '__main__':
    unittest.main()
