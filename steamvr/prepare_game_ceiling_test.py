"""The game-drawn ceiling's maker on synthetic Granny files (no game file is in the repository).

The golden case builds steamvr/game_ceiling_golden.cpp (the source's own C++ remesher, third_party/natpoh_ceiling)
with g++ and checks that granny_write.py writes the same bytes; skipped where no g++ is installed.
"""
import hashlib
import json
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib
from pathlib import Path
from types import SimpleNamespace

sys.path.insert(0, str(Path(__file__).resolve().parent))
import granny_write as gw  # noqa: E402
import prepare_game_ceiling as pgc  # noqa: E402
from potion_assets import Granny  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
SIGNATURE = bytes.fromhex('e59b495e6f631f141e13eba990beedc4')


class Builder:
    """A Granny file laid out by hand: types in section 1, objects in section 0, every pointer a fixup."""

    def __init__(self):
        self.sec = [bytearray(), bytearray()]
        self.fix = [[], []]

    def alloc(self, s, n, align=8):
        while len(self.sec[s]) % align:
            self.sec[s].append(0)
        at = (s, len(self.sec[s]))
        self.sec[s].extend(bytes(n))
        return at

    def put(self, at, fmt, *values):
        struct.pack_into(fmt, self.sec[at[0]], at[1], *values)

    def ptr(self, at, to):
        if to is not None:
            self.fix[at[0]].append([at[1], to[0], to[1]])

    def string(self, text):
        at = self.alloc(0, len(text) + 1, 1)
        self.sec[0][at[1]:at[1] + len(text)] = text.encode()
        return at

    def type(self, entries):
        at = self.alloc(1, 44 * (len(entries) + 1))
        for i, (kind, name, ref, width) in enumerate(entries):
            e = (at[0], at[1] + 44 * i)
            self.put(e, '<i', kind)
            name_at = self.alloc(1, len(name) + 1, 1)
            self.sec[1][name_at[1]:name_at[1] + len(name)] = name.encode()
            self.ptr((e[0], e[1] + 4), name_at)
            self.ptr((e[0], e[1] + 12), ref)
            self.put((e[0], e[1] + 20), '<i', width)
        return at

    def array(self, at, items):
        """A count and a pointer at `at` to an array of pointers to `items`."""
        arr = self.alloc(0, 8 * len(items))
        for i, item in enumerate(items):
            self.ptr((arr[0], arr[1] + i * 8), item)
        self.inline(at, arr, len(items))

    def inline(self, at, place, count):
        """A count and a pointer at `at` to `count` structs laid at `place`."""
        self.put(at, '<i', count)
        self.ptr((at[0], at[1] + 4), place)

    def file(self, root_type, root_obj):
        f = SimpleNamespace(magic=SIGNATURE + bytes(16), sec=self.sec, fix=self.fix,
                            h=[7, 0, 0, 72, 2, root_type[0], root_type[1], root_obj[0], root_obj[1], 0x80000037] + [0] * 8,
                            sh=[[0, 0, 0, 0, 4, len(s), len(s), 0, 0, 0, 0] for s in self.sec])
        return gw.write(f)


def rock_model(maps=pgc.SOURCE_MAPS):
    """A small model laid out as the game's rock: one mesh of 4 vertices and 2 triangles, a skeleton of one scaled
    bone, three textures by name, the model's lodLevel."""
    b = Builder()
    t_vertex = b.type([(17, 'Position', None, 4), (13, 'Normal', None, 4), (13, 'Tangent', None, 4), (21, 'TextureCoordinates0', None, 2)])
    t_vdata = b.type([(7, 'Vertices', None, 0)])
    t_group = b.type([(19, 'MaterialIndex', None, 0), (19, 'TriFirst', None, 0), (19, 'TriCount', None, 0)])
    t_i32, t_i16 = b.type([(19, 'Int32', None, 0)]), b.type([(16, 'Int16', None, 0)])
    t_topo = b.type([(3, 'Groups', t_group, 0), (3, 'Indices', t_i32, 0), (3, 'Indices16', t_i16, 0)])
    t_bb = b.type([(8, 'BoneName', None, 0), (10, 'OBBMin', None, 3), (10, 'OBBMax', None, 3)])
    t_mext = b.type([(10, 'bbMin', None, 3), (10, 'bbMax', None, 3), (10, 'VertexScale', None, 0)])
    t_mesh = b.type([(8, 'Name', None, 0), (2, 'PrimaryVertexData', t_vdata, 0), (2, 'PrimaryTopology', t_topo, 0),
                     (3, 'BoneBindings', t_bb, 0), (5, 'ExtendedData', None, 0)])
    t_bone = b.type([(8, 'Name', None, 0), (19, 'ParentIndex', None, 0), (9, 'Transform', None, 0), (10, 'InverseWorldTransform', None, 16)])
    t_skel = b.type([(8, 'Name', None, 0), (3, 'Bones', t_bone, 0)])
    t_modext = b.type([(19, 'lodLevel', None, 0)])
    t_model = b.type([(8, 'Name', None, 0), (2, 'Skeleton', t_skel, 0), (5, 'ExtendedData', None, 0)])
    t_tex = b.type([(8, 'FromFileName', None, 0)])
    t_root = b.type([(4, 'Textures', t_tex, 0), (8, 'FirstMap', None, 0), (4, 'Skeletons', t_skel, 0), (4, 'Meshes', t_mesh, 0),
                     (4, 'Models', t_model, 0), (4, 'TriTopologies', t_topo, 0)])
    names = [b.string(m) for m in maps]
    textures = []
    for n in names:
        t = b.alloc(0, 8)
        b.ptr(t, n)
        textures.append(t)
    bone = b.alloc(0, 8 + 4 + 68 + 64)
    b.ptr(bone, b.string('rock'))
    b.put((0, bone[1] + 8), '<i', -1)
    struct.pack_into('<I16f', b.sec[0], bone[1] + 12, 7, 1, 2, 3, 0, 0, 0, 1, 1.7, 0, 0, 0, 1.7, 0, 0, 0, 1.7)
    struct.pack_into('<16f', b.sec[0], bone[1] + 80, *([0.5] * 16))
    skel = b.alloc(0, 8 + 12)
    b.ptr(skel, b.string('skeleton'))
    b.inline((0, skel[1] + 8), bone, 1)
    verts = b.alloc(0, 4 * 20, 4)
    for i, (x, z) in enumerate(((-1, -1), (1, -1), (-1, 1), (1, 1))):
        struct.pack_into('<4h4b4b2H', b.sec[0], verts[1] + 20 * i, x * 1000, 0, z * 1000, 9000, 0, 127, 0, 0, 127, 0, 0, -127, 0, 0)
    vdata = b.alloc(0, 20)
    b.ptr(vdata, t_vertex)
    b.put((0, vdata[1] + 8), '<i', 4)
    b.ptr((0, vdata[1] + 12), verts)
    group = b.alloc(0, 12)
    b.put(group, '<3i', 3, 0, 2)
    idx = b.alloc(0, 12, 4)
    b.put(idx, '<6H', 0, 1, 2, 1, 3, 2)
    topo = b.alloc(0, 36)
    b.inline(topo, group, 1)
    b.inline((0, topo[1] + 24), idx, 6)
    bb = b.alloc(0, 32)
    b.ptr(bb, b.string('rock'))
    b.put((0, bb[1] + 8), '<6f', -1, -1, -1, 1, 1, 1)
    mext = b.alloc(0, 28)
    b.put(mext, '<7f', -1, -0.5, -1, 1, 0.5, 1, 1.5)
    mesh = b.alloc(0, 8 + 8 + 8 + 12 + 16)
    b.ptr(mesh, b.string('rocks03'))
    b.ptr((0, mesh[1] + 8), vdata)
    b.ptr((0, mesh[1] + 16), topo)
    b.inline((0, mesh[1] + 24), bb, 1)
    b.ptr((0, mesh[1] + 36), t_mext)
    b.ptr((0, mesh[1] + 44), mext)
    modext = b.alloc(0, 4)
    b.put(modext, '<i', 1)
    model = b.alloc(0, 8 + 8 + 16)
    b.ptr(model, b.string('rocks03'))
    b.ptr((0, model[1] + 8), skel)
    b.ptr((0, model[1] + 16), t_modext)
    b.ptr((0, model[1] + 24), modext)
    root = b.alloc(0, 12 + 8 + 12 * 4)
    b.array(root, textures)
    b.ptr((0, root[1] + 12), names[0])
    b.array((0, root[1] + 20), [skel])
    b.array((0, root[1] + 32), [mesh])
    b.array((0, root[1] + 44), [model])
    b.array((0, root[1] + 56), [topo])
    return b.file(t_root, root)


RENAMES = [(pgc.SOURCE_MAPS[i], pgc.map_path(0, w)) for i, w in enumerate(('ALB', 'NRM', 'ORM'))]


class Assets:
    """A game's archives in memory: canonical names, read, a build."""

    def __init__(self, files):
        self.files = {k.lower(): v for k, v in files.items()}
        self.build_hash = 'b' * 64

    def read(self, name):
        return self.files[name.lower()]


def preset(biome, pieces):
    entities = [{'type': 'Entity', 'name': f'e{i}', 'components': [
        {'type': 'TransformDefinitionComponent', 'position': {'x': 0, 'y': y, 'z': 0}, 'scale': {'x': 1, 'y': s, 'z': 1}},
        {'type': 'ModelDefinitionComponent', 'filename': model}]} for i, (model, y, s) in enumerate(pieces)]
    return json.dumps({'biomeFilename': f'data/hd/env/biome/{biome}.json', 'entities': entities}).encode()


def game(maps_for=(0, 3), extra=None):
    rock = rock_model()
    files = {pgc.SOURCE: rock, 'data/hd/env/model/act1/caves/act1_caves_walls/cliff01_lod0.model': rock,
             'data/hd/model_lod_desc.json': b'{"tests/test_detailnormal/cliffside01_lod0.model": 1}',
             'data/hd/env/preset/act1/caves/cave1.json': preset('act1_caves', [
                 ('data/hd/env/model/act1/caves/act1_caves_walls/cliff01.model', 2.0, 8.0),
                 ('data/hd/env/model/act1/caves/act1_caves_walls/cliff01.model', 4.0, 8.0),
                 ('data/hd/env/model/act1/caves/act1_caves_walls/cliff01.model', 5.0, 8.0),
                 ('data/hd/env/model/act1/caves/act1_caves_rocks/pebble01.model', 0.0, 1.0)]),
             'data/hd/env/preset/act1/caves/outdoors.json': preset('act1_wilderness', [
                 ('data/hd/env/model/act1/caves/act1_caves_walls/cliff01.model', 50.0, 8.0)])}
    for k in maps_for:
        for w in ('ALB', 'NRM', 'ORM'):
            files[pgc.map_path(k, w)] = b'texture'
    files.update(extra or {})
    return Assets(files)


class GrannyWriteTest(unittest.TestCase):
    def test_halves_and_snorms(self):
        self.assertEqual([gw.half(v) for v in (0.0, 1.0, 0.5, 0.015625, -2.0)], [0, 0x3C00, 0x3800, 0x2400, 0xC000])
        self.assertEqual([gw.snorm8(v) for v in (-2.0, -1.0, 0.0, 0.5, 1.0, 3.0)], [-127, -127, 0, 64, 127, 127])

    def test_meshes(self):
        slab = gw.ceiling_mesh(40.0, 6, 6, 6.0)
        self.assertEqual((len(slab.v), len(slab.idx)), (73 * 73, 12 * 72 * 72))   # the slab's index count, as the DLL checks it
        self.assertEqual(slab.scale, 240.0)
        self.assertTrue(-6.0 <= slab.mn[1] < slab.mx[1] <= 0.0 and slab.mn[0] == -240.0 and slab.mx[2] == 240.0)
        flat = gw.ceiling_mesh(40.0, 6, 6, 0.0)
        self.assertEqual((flat.mn[1], flat.mx[1]), (0.0, 0.0))
        self.assertTrue(all(v[1][1] == -127 for v in flat.v))   # normals down
        wall = gw.wall_mesh(pgc.WALL_RADIUS, 40.0, 96, 4, 40.0)
        self.assertEqual((len(wall.v), len(wall.idx), wall.scale), (97 * 5, 12 * 96 * 4, 214.0))
        before = sorted(tuple(sorted(slab.idx[i:i + 3])) for i in range(0, len(slab.idx), 3))
        gw.salt(slab, 5)
        self.assertEqual(len(slab.v), 73 * 73 + 5)
        self.assertEqual(sorted(tuple(sorted(slab.idx[i:i + 3])) for i in range(0, len(slab.idx), 3)), before)
        self.assertNotEqual(slab.idx[:3], gw.ceiling_mesh(40.0, 6, 6, 6.0).idx[:3])

    def test_remesh_written_and_read_back(self):
        f = gw.File(rock_model())
        mesh = gw.ceiling_mesh(40.0, 6, 6, 6.0)
        gw.salt(mesh, 1)
        gw.remesh(f, mesh, RENAMES, 3)
        data = gw.write(f)
        h = struct.unpack_from('<18I', data, 32)
        self.assertEqual((h[0], h[1], h[2]), (7, len(data), zlib.crc32(data[104:]) & 0xFFFFFFFF))
        self.assertTrue(all(s[0] == 0 for s in gw.File(data).sh))   # every section stored uncompressed
        g = Granny(data)   # the potion reader, as a check from another reader
        [(name, vertices, indices)] = g.meshes()
        self.assertEqual((name, len(vertices), len(indices)), ('rocks03', len(mesh.v), len(mesh.idx)))
        textures = [g.string(g.ptr(t['FromFileName'][3])) for t in g.array(g.root['Textures'])]
        self.assertEqual(textures, [r[1] for r in RENAMES])
        self.assertEqual(g.string(g.ptr(g.root['FirstMap'][3])), RENAMES[0][1])   # every pointer to a name renamed
        back = gw.File(data)
        w = gw.Walk(back)
        model, t = w.member(back.root_type, back.root_obj, 'Models')
        model_type, obj = w.ref(t), w.ptr(w.ptr(gw.at(model, 4)))
        ext, _ = w.member(model_type, obj, 'ExtendedData')
        lod, _ = w.member(w.ptr(ext), w.ptr(gw.at(ext, 8)), 'lodLevel')
        self.assertEqual(w.get(lod, '<i'), 3)
        self.assertEqual(gw.bounds(back), ([-240.0, mesh.mn[1], -240.0], [240.0, mesh.mx[1], 240.0]))
        shapes = {}   # each struct named once, then known by its shape from file to file
        self.assertEqual([gw.bounds(gw.File(data), shapes) for _ in range(2)], [gw.bounds(back)] * 2)
        self.assertIn(['bbMin', 'bbMax', 'VertexScale'], shapes.values())
        skel, t = w.member(back.root_type, back.root_obj, 'Skeletons')
        bones, bt = w.member(w.ref(t), w.ptr(w.ptr(gw.at(skel, 4))), 'Bones')
        transform, _ = w.member(w.ref(bt), w.ptr(gw.at(bones, 4)), 'Transform')
        self.assertEqual(struct.unpack_from('<I16f', back.sec[transform[0]], transform[1]),
                         (0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 1, 0, 0, 0, 1))   # the bone's 1.7 scale gone

    def test_refused_layouts(self):
        with self.assertRaisesRegex(ValueError, 'texture name'):
            gw.remesh(gw.File(rock_model()), gw.ceiling_mesh(40.0, 1, 1, 0.0), [('not/in/the/file.texture', 'x')], 0)
        with self.assertRaises(ValueError):
            gw.File(b'\0' * 50)


class GoldenTest(unittest.TestCase):
    """The source's own C++ (third_party/natpoh_ceiling) and the port write the same bytes."""

    def test_same_bytes_as_the_source(self):
        compiler = shutil.which('g++')
        if not compiler:
            self.skipTest('no g++ to build the source remesher')
        with tempfile.TemporaryDirectory() as temporary:
            temporary = Path(temporary)
            exe = temporary / 'golden.exe'
            subprocess.run([compiler, '-std=c++20', '-O2', '-o', str(exe), str(Path(__file__).with_name('game_ceiling_golden.cpp'))],
                           check=True, capture_output=True)
            (temporary / 'in.model').write_bytes(rock_model())
            subprocess.run([str(exe), str(temporary / 'in.model'), str(temporary)], check=True, capture_output=True)
            cases = {'slab': (gw.ceiling_mesh(40.0, 6, 6, 6.0), 1, 2), 'flat': (gw.ceiling_mesh(40.0, 6, 6, 0.0), 7, 0),
                     'wall': (gw.wall_mesh(pgc.WALL_RADIUS, 40.0, 96, 4, 40.0), 2, 4)}
            for name, (mesh, salt, lod) in cases.items():
                gw.salt(mesh, salt)
                self.assertEqual(mesh.vertex_bytes() + mesh.index_bytes(), (temporary / f'{name}.mesh').read_bytes(), name)
                f = gw.File(rock_model())
                gw.remesh(f, mesh, RENAMES, lod)
                self.assertEqual(gw.write(f), (temporary / f'{name}.model').read_bytes(), name)


class MakerTest(unittest.TestCase):
    def test_tables(self):
        self.assertEqual(len(pgc.KINDS), 15)
        self.assertEqual(len({k[3] for k in pgc.KINDS} | {k[4] for k in pgc.KINDS if k[4]}), 18)   # every model its own path
        self.assertTrue(all(0 <= kind < 15 for _, kind in pgc.BIOME_KINDS))
        self.assertEqual(pgc.lod_path('@act1/x/_pit02', 2), 'data/hd/env/model/act1/x/_pit02_lod2.model')
        self.assertEqual(pgc.lod_path('test_cube', 0), 'data/hd/env/model/Tests/test_cube_lod0.model')
        self.assertEqual((pgc.unit_path(0, False), pgc.unit_path(0, True), pgc.unit_path(3, False)),
                         ('data/hd/env/model/d2r_vr/ceiling/cave_ceiling_unit.json', 'data/hd/env/model/d2r_vr/ceiling/cave_ceiling_wall_unit.json',
                          'data/hd/env/model/d2r_vr/ceiling/crypt_ceiling_unit.json'))
        self.assertEqual([pgc.salt_of(k, w) for k, w in ((0, False), (0, True), (3, False))], [1, 2, 7])

    def test_unit(self):
        unit = json.loads(pgc.unit_json(1, True))
        self.assertEqual(unit['name'], 'd2r_vr_jungle_cave_wall')
        components = [c for e in unit['entities'] for c in e['components']]
        self.assertEqual(sum(c['type'] == 'UnitRootComponent' for c in components), 1)   # the game asserts on none or two
        model = next(c for c in components if c['type'] == 'ModelDefinitionComponent')
        self.assertEqual((model['lightMask'], model['shadowMask'], model['filename']), (19, 0, 'data/hd/env/model/Tests/test_cloth/cloth04.model'))
        self.assertEqual([e['id'] for e in unit['entities']], [1000000003, 1000000004])
        self.assertEqual(unit['dependencies']['textures'][0]['path'], pgc.KINDS[1][1] + '_ALB.texture')

    def test_measures(self):
        self.assertEqual(pgc.biome_of('{"biomeFilename": "data/hd/env/biome/Act1_Caves.json"}'), 'act1_caves')
        self.assertEqual(pgc.biome_of('{}'), '')
        self.assertTrue(pgc.is_wall_piece('x/act1_caves_cliff02.model') and not pgc.is_wall_piece('x/rock01.model'))
        self.assertEqual(pgc.wall_top('a_wall.model', 2.0, 2.0, 0.0, 3.5), 9.0)
        self.assertLess(pgc.wall_top('a_wall.model', 2.0, 1.0, 0.0, 6.0), 0.0)   # not over 6 units tall: trim
        self.assertEqual((pgc.median([3.0, 1.0, 2.0]), pgc.median([1.0, 4.0]), pgc.median([])), (2.0, 2.5, 0.0))
        self.assertEqual(pgc.format_measures([('act1_caves', 12.345)]).splitlines()[1:], ['version 5', 'walls act1_caves 12.35'])

    def test_collision_guard(self):
        texts = {'data/hd/model_lod_desc.json': 'tests/test_cube_lod0.model', 'data/hd/env/preset/test/chad.json': 'tests/test_cube.model',
                 'data/hd/env/preset/act1/town/x.json': 'data/hd/env/model/tests/test_cube_tex_lod0.model'}
        hits = pgc.named_in_play(texts, '')
        self.assertEqual((hits['test_cube'], hits['test_cube_tex']), ([], ['data/hd/env/preset/act1/town/x.json']))
        self.assertEqual(pgc.named_in_play(texts, 'Act1\\Test\\chad.ds1')['test_cube'], ['data/hd/env/preset/test/chad.json'])

    def test_generate(self):
        payload, walls = pgc.generate(game(maps_for=(0, 3)), 'D2R-PCVR', log=lambda *_: None)
        record = json.loads(payload.pop(pgc.RECEIPT))
        self.assertEqual((record['kinds'], record['mpq'], record['game_build']), (['cave', 'crypt'], 'D2R-PCVR.mpq', 'b' * 64))
        self.assertEqual(len(record['skipped']), 13)
        self.assertEqual(set(payload), {'D2R-PCVR.mpq/' + rel for rel in record['files']})
        self.assertEqual(len(record['files']), 5 + 1 + 5 + 1 + 5 + 1 + 1)   # cave slab and wall, crypt slab, measures
        for rel, sha in record['files'].items():
            self.assertEqual(hashlib.sha256(payload['D2R-PCVR.mpq/' + rel]).hexdigest(), sha)
        lod2 = gw.File(payload['D2R-PCVR.mpq/' + pgc.lod_path('test_detailnormal/cliffside01', 2)])
        self.assertEqual(len(Granny(gw.write(lod2)).meshes()[0][1]), 73 * 73 + 1)
        crypt = Granny(payload['D2R-PCVR.mpq/' + pgc.lod_path('vertblend_house', 0)])
        self.assertEqual([crypt.string(crypt.ptr(t['FromFileName'][3])) for t in crypt.array(crypt.root['Textures'])][0],
                         pgc.map_path(3, 'ALB'))
        # the walls' top: three cliffs of the caves' own biome (the outdoors' one left out), the pebble no wall
        self.assertEqual(walls, [('act1_caves', 8.0)])   # tops 6, 8 and 9: each y plus 8 times the model's 0.5
        self.assertIn(b'walls act1_caves 8.00', payload['D2R-PCVR.mpq/' + pgc.MEASURES])

    def test_named_model_refused(self):
        assets = game(maps_for=(0,), extra={'data/hd/env/preset/act1/town/x.json': b'"data/hd/env/model/tests/test_detailnormal/decal01_lod0.model"'})
        payload, _ = pgc.generate(assets, 'D2R-PCVR', log=lambda *_: None)
        record = json.loads(payload[pgc.RECEIPT])
        self.assertEqual(record['kinds'], [])
        self.assertIn('decal01', record['skipped'][0])

    def test_no_rock(self):
        with self.assertRaisesRegex(ValueError, 'rock model'):
            pgc.generate(Assets({}), 'D2R-PCVR', log=lambda *_: None)
        with self.assertRaises(ValueError):
            pgc.generate(game(), 'elsewhere', log=lambda *_: None)

    def test_existing_files(self):
        payload, _ = pgc.generate(game(maps_for=(3,)), 'D2R-PCVR', log=lambda *_: None)
        with tempfile.TemporaryDirectory() as temporary:
            mod = Path(temporary).resolve()
            pgc.check_existing(mod, payload)   # nothing there
            pgc.write_files(mod, payload)
            pgc.check_existing(mod, payload)   # ours, the same
            name = 'D2R-PCVR.mpq/' + pgc.lod_path('vertblend_house', 1)
            changed = dict(payload)
            changed[name] = b'newer'
            pgc.check_existing(mod, changed)   # ours by the receipt: replaced
            (mod / name).write_bytes(b'the player\'s own')
            with self.assertRaisesRegex(ValueError, 'left unchanged'):
                pgc.check_existing(mod, payload)
            (mod / name).write_bytes(payload[name])
            self.assertEqual(pgc.measured_before(mod, 'b' * 64), [('act1_caves', 8.0)])   # kept for this build
            self.assertIsNone(pgc.measured_before(mod, 'c' * 64))                         # measured again on another
            self.assertIsNone(pgc.parse_measures('version 4\nwalls a 1\n'))               # another version: not ours
            self.assertEqual(pgc.remove_files(mod), len(payload) - 1)
            self.assertFalse((mod / pgc.RECEIPT).exists())


class DriftTest(unittest.TestCase):
    """The tables match the DLL's shared/game_ceiling.h, once it is in the tree."""

    def test_kinds_match_the_header(self):
        header = ROOT / 'shared/game_ceiling.h'
        if not header.is_file():
            self.skipTest('shared/game_ceiling.h is not in this tree yet')
        text = header.read_text(encoding='utf-8')
        rows = re.findall(r'\{"(\w+)", "([^"]+)", (kRelief|0\.0f|[\d.]+f), "([^"]+)", (nullptr|"[^"]+")\}', text)
        self.assertEqual([(n, m, s, None if w == 'nullptr' else w.strip('"')) for n, m, _, s, w in rows],
                         [(n, m, s, w) for n, m, _, s, w in pgc.KINDS])
        reliefs = [pgc.RELIEF if r == 'kRelief' else float(r.rstrip('f')) for _, _, r, _, _ in rows]
        self.assertEqual(reliefs, [k[2] for k in pgc.KINDS])
        biomes = re.findall(r'\{"(\w+)", (\d+)\}', text[text.find('kBiomeKinds'):])
        self.assertEqual([(b, int(k)) for b, k in biomes][:len(pgc.BIOME_KINDS)], list(pgc.BIOME_KINDS))
        for name, value in (('kTile', '40.0f'), ('kTilesHalf', '6'), ('kCells', '6'), ('kRelief', '6.0f')):
            self.assertRegex(text, rf'{name} = {re.escape(value)};')


if __name__ == '__main__':
    unittest.main()
