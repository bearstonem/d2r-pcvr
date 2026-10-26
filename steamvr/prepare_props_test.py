"""Prop catalog contracts on synthetic presets; no Blizzard data in fixtures."""
import json
import struct
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch
import prepare_props as props


def entity(identifier, model, position=(1.5, 2.25, -3.0), extra=()):
    components = [
        {'type': 'TransformDefinitionComponent', 'position': dict(zip('xyz', position)),
         'orientation': {'x': 0, 'y': 0, 'z': 0, 'w': 1}, 'scale': {'x': 1, 'y': 1, 'z': 1}},
        {'type': 'ModelPlatformTierComponent'},
        {'type': 'ModelDefinitionComponent', 'filename': model},
    ]
    return {'type': 'Entity', 'name': 'e', 'id': identifier, 'components': components + [{'type': t} for t in extra]}


class Assets:
    def __init__(self, files):
        self.build_hash = 'fixture-build'
        self.files = {name: json.dumps(data).encode() if not isinstance(data, bytes) else data for name, data in files.items()}
        self.reads = 0
    def __enter__(self):
        return self
    def __exit__(self, *args):
        pass
    def read(self, name):
        self.reads += 1
        return self.files[name]


def decode(data):
    magic, fmt, kinds, count = struct.unpack_from('<4I', data)
    names = []
    for i in range(kinds):
        name, shape, material, _ = struct.unpack_from('<28sBBH', data, 16 + i * 32)
        names.append((name.rstrip(b'\0').decode(), props.SHAPES[shape], props.MATERIALS[material]))
    rows = [struct.unpack_from('<I3fHH', data, 16 + kinds * 32 + i * 20) for i in range(count)]
    assert len(data) == 16 + kinds * 32 + count * 20
    return magic, fmt, names, [(r[0], names[r[4]][0], r[1:4]) for r in rows]


class PropCatalogTests(unittest.TestCase):
    def test_kind_from_model_name(self):
        self.assertEqual(props.kind_of('data/hd/env/model/x/cup01.model'), 'cup')
        self.assertEqual(props.kind_of('data/HD/Env/Vase_Small06.model'), 'vase_small')
        self.assertEqual(props.kind_of('data/hd/x/book_red_01.model'), 'book_red')
        self.assertIsNone(props.kind_of('data/hd/x/candle01.model'))   # its flame is another entity
        self.assertIsNone(props.kind_of('data/hd/x/cupboard01.model'))
        self.assertEqual(props.kind_of('data/hd/x/sword_rusty02.model'), 'sword_rusty')   # 4: loose weapons
        self.assertEqual(props.kind_of('data/hd/x/L_Barrel01.model'), 'l_barrel')
        self.assertIsNone(props.kind_of('data/hd/x/wall_shield01.model'))   # on a wall
        self.assertIsNone(props.kind_of('data/hd/x/crates01.model'))   # several crates, one model
        self.assertIsNone(props.kind_of('data/hd/x/cup01.texture'))

    def test_only_plain_catalogued_props(self):
        preset = {'entities': [
            entity(7, 'a/cup01.model'),
            entity(8, 'a/cup01.model', extra=('VfxDefinitionComponent',)),           # it emits something
            entity(13, 'a/cup01.model', extra=('WallTransparencyComponent',)),       # fades with the walls: kept
            entity(9, 'a/table01.model'),                                            # not a prop
            entity(0, 'a/plate02.model'),                                            # no id
            entity(2**32, 'a/plate02.model'),
            {'type': 'Entity', 'id': 10, 'components': [{'type': 'ModelDefinitionComponent', 'filename': 'a/cup01.model'}]},
            entity(11, 'a/plate02.model', position=(1, 'x', 2)),
            entity(12, 'a/skull03.model', position=(10.25, 0.5, 4.75)),
        ]}
        static = entity(14, 'a/chair02.model')
        static['components'].append({'type': 'PhysicsBodyDefinitionComponent', 'bodytype': 'static'})
        moving = entity(15, 'a/chair02.model')
        moving['components'].append({'type': 'PhysicsBodyDefinitionComponent', 'bodytype': 'kinematic'})
        preset['entities'] += [static, moving]
        self.assertEqual([(i, k) for i, k, _ in props.entities(preset)], [(7, 'cup'), (13, 'cup'), (12, 'skull'), (14, 'chair')])
        self.assertEqual(next(props.entities(preset))[2], (1.5, 2.25, -3.0))
        self.assertEqual(list(props.entities([])), [])

    def test_encoding_sorted_unique_with_kinds(self):
        rows = [(30, 'cup', (1., 2., 3.)), (5, 'skull', (0., 0., 0.)), (30, 'cup', (1., 2., 3.)),   # instanced preset
                (30, 'plate', (-1., 2., 3.))]
        data, count, kinds = props.encode(rows)
        self.assertEqual((count, kinds), (3, ['cup', 'plate', 'skull']))
        magic, fmt, names, entries = decode(data)
        self.assertEqual((magic, fmt), (props.MAGIC, props.FORMAT))
        self.assertEqual(names, [('cup', 'round', 'ceramic'), ('plate', 'round', 'ceramic'), ('skull', 'box', 'bone')])
        self.assertEqual([(e[0], e[1]) for e in entries], [(5, 'skull'), (30, 'plate'), (30, 'cup')])
        self.assertEqual(entries[2][2], (1., 2., 3.))

    def test_every_kind_has_a_known_shape_and_material(self):
        for name, (shape, material) in props.KINDS.items():
            self.assertIn(shape, props.SHAPES, name)
            self.assertIn(material, props.MATERIALS, name)
            self.assertLess(len(name.encode('ascii')), 28, name)

    def test_prepare_writes_and_reuses_cache(self):
        files = {
            'data/hd/env/preset/act2/town/a.json': {'entities': [entity(41, 'm/mug01.model'), entity(42, 'm/bowl03.model', (5, 6, 7))]},
            'data/hd/env/preset/act1/b.json': {'entities': [entity(43, 'm/apple02.model')]},
            'data/hd/env/preset/act1/broken.json': b'{not json',
            'data/hd/env/other/c.json': {'entities': [entity(44, 'm/cup01.model')]},   # not a level preset
        }
        assets = Assets(files)
        with tempfile.TemporaryDirectory() as out, patch.object(props, 'open_assets', return_value=assets):
            result = props.prepare('game', out)
            self.assertEqual((result['presets'], result['props'], result['kinds']), (2, 3, 3))
            _, _, _, entries = decode((Path(out) / 'props.bin').read_bytes())
            self.assertEqual([e[0] for e in entries], [41, 42, 43])
            reads = assets.reads
            self.assertEqual(props.prepare('game', out), result)
            self.assertEqual(assets.reads, reads)   # unchanged build: the cache is kept
            (Path(out) / 'props.bin').write_bytes(b'damaged')
            self.assertEqual(props.prepare('game', out)['sha256'], result['sha256'])
            self.assertGreater(assets.reads, reads)

    def test_no_props_is_an_error(self):
        assets = Assets({'data/hd/env/preset/a.json': {'entities': [entity(1, 'm/table01.model')]}})
        with tempfile.TemporaryDirectory() as out, patch.object(props, 'open_assets', return_value=assets):
            with self.assertRaises(ValueError):
                props.prepare('game', out)
            self.assertFalse((Path(out) / 'props.bin').exists())


if __name__ == '__main__':
    unittest.main()
