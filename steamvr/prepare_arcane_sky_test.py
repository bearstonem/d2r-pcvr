"""Synthetic texture/visual fixtures only; no game assets or saves."""
import copy
import hashlib
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch

import prepare_arcane_sky as sky


def block(a, b, indices):
    return bytes((a, b)) + sum(index << (3 * i) for i, index in enumerate(indices)).to_bytes(6, 'little')


def texture(data, width=4, height=4):
    header = bytearray(44)
    struct.pack_into('<IHHII', header, 0, 0x2845443c, 63, 0, width, height)
    struct.pack_into('<I', header, 28, 1)
    struct.pack_into('<II', header, 36, len(data), 4)
    return bytes(header) + data


def visual():
    return {'type': 'VisualDataDefinition', 'name': 'synthetic', 'dependencies': {'particles': [{'path': sky.WEATHER}]},
            'entities': [{'components': [{'type': 'VisualDataComponent', 'screenspaceVfx': sky.WEATHER,
                                         'exposureValue': 4, 'heightFogDensity': 0},
                                        {'type': 'DirectionalLightDefinitionComponent', 'power': 18}]}]}


class ArcaneSkyTests(unittest.TestCase):
    def test_bc4_both_endpoint_modes_and_indices(self):
        data = texture(block(210, 0, list(range(8)) * 2))
        self.assertEqual(sky.bc4_texture(data), (4, 4, bytes([210, 0, 180, 150, 120, 90, 60, 30] * 2)))
        data = texture(block(0, 200, list(range(8)) * 2))
        self.assertEqual(sky.bc4_texture(data)[2], bytes([0, 200, 40, 80, 120, 160, 0, 255] * 2))

    def test_blocks_map_to_rows_not_linear_runs(self):
        data = texture(block(50, 0, [0] * 16) + block(100, 0, [0] * 16), 8, 4)
        self.assertEqual(sky.bc4_texture(data)[2], bytes([50] * 4 + [100] * 4) * 4)

    def test_rejects_truncated_wrong_format_and_out_of_bounds_mips(self):
        valid = texture(block(255, 0, [0] * 16))
        for data in (b'', valid[:43], valid[:-1], valid[:4] + b'\x3d\x00' + valid[6:]):
            with self.subTest(data=data[:8]), self.assertRaises(ValueError):
                sky.bc4_texture(data)
        bad = bytearray(valid)
        struct.pack_into('<I', bad, 40, 0xffffffff)
        with self.assertRaises(ValueError):
            sky.bc4_texture(bad)

    def test_changes_only_original_weather_not_lights_or_source(self):
        source = visual()
        original = copy.deepcopy(source)
        changed = sky.replace_weather(source)
        self.assertEqual(source, original)
        self.assertEqual(changed['entities'][0]['components'][0]['screenspaceVfx'], '')
        changed['entities'][0]['components'][0]['screenspaceVfx'] = sky.WEATHER
        self.assertEqual(changed, original)

    def test_other_weather_or_unknown_visual_shape_is_not_disabled(self):
        for source in ([], {}, {'type': 'Preset'}, visual()):
            if isinstance(source, dict) and source.get('type') == 'VisualDataDefinition':
                source['entities'][0]['components'][0]['screenspaceVfx'] = 'rain.particles'
            with self.assertRaises(ValueError):
                sky.replace_weather(source)
        for weather in (None, [], 42):
            source = visual()
            source['entities'][0]['components'][0]['screenspaceVfx'] = weather
            with self.assertRaises(ValueError):
                sky.replace_weather(source)

    def test_complete_payload_contains_all_periods_and_local_hashes(self):
        class Assets:
            build_hash = 'synthetic-build'
            def read(self, name):
                return json.dumps(visual()).encode() if name.endswith('.json') else name.encode()
        def decoded(data):
            width, height = (1024, 512) if b'stars_' in data else (512, 512)
            return width, height, bytes([80]) * (width * height)
        for mod in ('D2R-PCVR', 'pcvr-dev'):
            with patch.object(sky, 'bc4_texture', side_effect=decoded):
                result = sky.generate(Assets(), mod)
            self.assertEqual(len(result), 10)
            record = json.loads(result[sky.RECEIPT])
            self.assertEqual(record['sha256'], {name: hashlib.sha256(data).hexdigest()
                                               for name, data in result.items() if name != sky.RECEIPT})
            for period in sky.PERIODS:
                self.assertIn(f'{mod}.mpq/{sky.VIS}act2_arcane_{period}.json', result)
            self.assertTrue(result[sky.TEXTURES + 'stars.png'].startswith(b'\x89PNG'))

    def test_existing_edits_and_untracked_overrides_are_preserved(self):
        with tempfile.TemporaryDirectory() as tmp:
            mod = Path(tmp).resolve()
            name = 'D2R-PCVR.mpq/' + sky.VIS + 'act2_arcane_day.json'
            path = mod / name
            path.parent.mkdir(parents=True)
            path.write_bytes(b'player override')
            with self.assertRaisesRegex(ValueError, 'left unchanged'):
                sky.check_existing(mod, {name: b'prepared'})
            self.assertEqual(path.read_bytes(), b'player override')
            (mod / sky.RECEIPT).write_text(json.dumps({'version': 1, 'sha256': {name: hashlib.sha256(b'previous preparation').hexdigest()}}))
            with self.assertRaisesRegex(ValueError, 'left unchanged'):
                sky.check_existing(mod, {name: b'prepared'})
            path.write_bytes(b'previous preparation')
            sky.check_existing(mod, {name: b'prepared'})
            self.assertEqual(path.read_bytes(), b'previous preparation') # validation never writes

    def test_unknown_destination_is_rejected(self):
        with self.assertRaises(ValueError):
            sky.generate(None, '../other-mod')


if __name__ == '__main__':
    unittest.main()
