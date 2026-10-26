"""Synthetic preset fixtures for Kurast Docks flying canoe fix; no game assets."""
import copy
import hashlib
import json
from pathlib import Path
import tempfile
import unittest

import prepare_kurast_dock as dock


def entity(eid, name, prefab=None, pos=None):
    comps = []
    if pos is not None:
        comps.append({
            'type': 'TransformDefinitionComponent',
            'name': f'{name}_Transform',
            'position': pos,
            'orientation': {'x': 0.0, 'y': 0.0, 'z': 0.0, 'w': 1.0},
            'scale': {'x': 1.0, 'y': 1.0, 'z': 1.0}
        })
    if prefab is not None:
        comps.append({
            'type': 'PrefabPlacementDefinitionComponent',
            'name': f'{name}_Prefab',
            'prefab': prefab
        })
    return {
        'type': 'Entity',
        'name': name,
        'id': eid,
        'components': comps
    }


def sample_preset():
    return {
        'type': 'Preset',
        'name': 'docktown3',
        'entities': [
            entity(101, 'normal_hut', pos={'x': 200.0, 'y': 0.5, 'z': 200.0}),
            entity(102, 'water_canoe', prefab=dock.CANOE_PREFAB, pos={'x': 150.0, 'y': -2.5, 'z': 180.0}),
            entity(dock.FLYING_CANOE_ID, 'prefab014', prefab=dock.CANOE_PREFAB, pos={'x': 0.0, 'y': 50.0, 'z': 0.0}),
            entity(103, 'dock_meshif', pos={'x': 220.0, 'y': 0.0, 'z': 340.0})
        ]
    }


class KurastDockTests(unittest.TestCase):
    def test_removes_flying_canoe_by_id_and_preserves_others(self):
        source = sample_preset()
        original = copy.deepcopy(source)
        result, count = dock.remove_flying_canoe(source)
        self.assertEqual(count, 1)
        self.assertEqual(source, original)
        self.assertEqual(len(result['entities']), 3)
        ids = [e['id'] for e in result['entities']]
        self.assertEqual(ids, [101, 102, 103])
        self.assertNotIn(dock.FLYING_CANOE_ID, ids)

    def test_removes_flying_canoe_by_heuristic(self):
        source = {
            'type': 'Preset',
            'name': 'docktown3',
            'entities': [
                entity(999, 'custom_flying_canoe', prefab='some/path/pf_canoe01_07_bob.json', pos={'x': 0.0, 'y': 48.0, 'z': 0.0}),
                entity(102, 'water_canoe', prefab=dock.CANOE_PREFAB, pos={'x': 150.0, 'y': -2.5, 'z': 180.0})
            ]
        }
        result, count = dock.remove_flying_canoe(source)
        self.assertEqual(count, 1)
        self.assertEqual(len(result['entities']), 1)
        self.assertEqual(result['entities'][0]['id'], 102)

    def test_fails_if_no_flying_canoe_found(self):
        source = {
            'type': 'Preset',
            'name': 'docktown3',
            'entities': [
                entity(101, 'normal_hut', pos={'x': 200.0, 'y': 0.5, 'z': 200.0}),
                entity(102, 'water_canoe', prefab=dock.CANOE_PREFAB, pos={'x': 150.0, 'y': -2.5, 'z': 180.0})
            ]
        }
        with self.assertRaises(ValueError):
            dock.remove_flying_canoe(source)

    def test_fails_if_multiple_flying_canoes_found(self):
        source = {
            'type': 'Preset',
            'name': 'docktown3',
            'entities': [
                entity(dock.FLYING_CANOE_ID, 'flying1', prefab=dock.CANOE_PREFAB, pos={'x': 0.0, 'y': 50.0, 'z': 0.0}),
                entity(999, 'flying2', prefab=dock.CANOE_PREFAB, pos={'x': 10.0, 'y': 60.0, 'z': 10.0})
            ]
        }
        with self.assertRaises(ValueError):
            dock.remove_flying_canoe(source)

    def test_rejects_malformed_preset_objects(self):
        for bad in ({}, {'type': 'NotPreset'}, {'type': 'Preset', 'entities': 'not a list'}):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                dock.remove_flying_canoe(bad)

    def test_generate_payload(self):
        class MockAssets:
            build_hash = 'test_hash_123'
            def read(self, name):
                if name == dock.PRESET:
                    return json.dumps(sample_preset()).encode('utf-8')
                raise KeyError(name)

        payload = dock.generate(MockAssets(), 'D2R-PCVR')
        self.assertIn(f'D2R-PCVR.mpq/{dock.PRESET}', payload)
        self.assertIn(dock.RECEIPT, payload)
        receipt = json.loads(payload[dock.RECEIPT])
        self.assertEqual(receipt['version'], 1)
        self.assertEqual(receipt['build'], 'test_hash_123')
        self.assertEqual(receipt['removed_entities'], 1)
        preset_content = json.loads(payload[f'D2R-PCVR.mpq/{dock.PRESET}'])
        self.assertEqual(len(preset_content['entities']), 3)

    def test_check_existing_behavior(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td).resolve()
            payload = {'file.json': b'hello', dock.RECEIPT: b'receipt'}
            # Clean install works
            dock.check_existing(root, payload)

            # Writing receipt and matching payload works
            (root / 'file.json').write_bytes(b'hello')
            receipt = {
                'version': 1,
                'sha256': {'file.json': hashlib.sha256(b'hello').hexdigest()}
            }
            (root / dock.RECEIPT).write_text(json.dumps(receipt), encoding='utf-8')
            dock.check_existing(root, payload)

            # Untracked modified file raises ValueError
            (root / 'file.json').write_bytes(b'modified content')
            with self.assertRaises(ValueError):
                dock.check_existing(root, payload)


if __name__ == '__main__':
    unittest.main()
