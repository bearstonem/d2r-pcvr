"""Cache contracts and invalid-input tests; no Blizzard data in fixtures."""
import math
import struct
import unittest
from prepare_potions import encode,encode_arrow,encode_toss,ARROW_LENGTH,TOSS,TOSS_SIZE,FILES,THROWN,VERSION
from potion_assets import Granny,texture,decode

class PotionTests(unittest.TestCase):
    def test_normalization_top_axis_and_topology(self):
        # Offset source model whose neck is not at its native origin.
        verts=[(1,0,3,0,1,0,0,0),(2,0,3,0,1,0,1,0),(1.5,4,3,0,1,0,.5,1)]
        result=encode([('glassShape',verts,[0,1,2])],(4,4,78,[bytes(16)]))
        h=struct.unpack_from('<8I',result)
        self.assertEqual(h,(0x31525650,1,3,3,4,4,78,1))
        v=list(struct.iter_unpack('<9f',result[32:32+3*36]))
        self.assertAlmostEqual(v[2][0],0);self.assertAlmostEqual(v[2][1],.09);self.assertAlmostEqual(v[2][2],0)
        self.assertAlmostEqual(v[0][1],-.09);self.assertEqual(v[0][-1],1)
        self.assertEqual(struct.unpack_from('<3I',result,32+3*36),(0,1,2))

    def test_arrow_along_y_centred_and_scaled(self):
        # The game's arrow: along +Z, tip at +1, tail at -.98, a little off its axis.
        verts=[(.5,.2,-.984,0,0,-1,0,0),(.6,.2,1.,0,1,0,1,0),(.5,.3,1.,1,0,0,1,1)]
        result=encode_arrow([('shaft',verts,[0,1,2])],(4,4,78,[bytes(16)]))
        self.assertEqual(struct.unpack_from('<8I',result),(0x31525650,1,3,3,4,4,78,1))
        v=list(struct.iter_unpack('<9f',result[32:32+3*36]))
        self.assertAlmostEqual(v[1][1]-v[0][1],ARROW_LENGTH,places=5)   # tip +Y, ARROW_LENGTH from the tail
        self.assertAlmostEqual(v[0][1]+v[1][1],0,places=5)   # centred
        self.assertEqual(v[0][-1],0)   # no glass
        self.assertEqual(tuple(round(x,5) for x in v[1][3:6]),(0,0,-1))   # +Y normal turned with the mesh
        self.assertEqual(tuple(round(x,5) for x in v[0][3:6]),(0,-1,0))   # the tail's -Z normal points back down the arrow
        self.assertIn('arw.pvr',FILES)
        with self.assertRaises(ValueError):encode_arrow([('flat',[(0,0,0,0,1,0,0,0)]*3,[0,1,2])],(4,4,78,[bytes(16)]))
        with self.assertRaises(ValueError):encode_arrow([('wide',[(0,0,-1,0,1,0,0,0),(5,0,1,0,1,0,0,0),(0,0,0,0,1,0,0,0)],[0,1,2])],(4,4,78,[bytes(16)]))

    def test_thrown_trap_centred_and_scaled_on_its_own_axes(self):
        # A sentry: taller than wide, standing on y=0, off centre in x.
        verts=[(1.,0.,0.,0,1,0,0,0),(-.5,2.,0.,0,0,1,1,0),(.25,1.,.5,1,0,0,1,1)]
        result=encode_toss([('a',verts[:2],[0,1,1]),('b',verts[2:],[0,0,0])],(4,4,78,[bytes(16)]))
        self.assertEqual(struct.unpack_from('<8I',result),(0x31525650,1,3,6,4,4,78,1))
        v=list(struct.iter_unpack('<9f',result[32:32+3*36]))
        self.assertAlmostEqual(v[1][1]-v[0][1],TOSS_SIZE,places=5)   # its largest side (height) is TOSS_SIZE
        self.assertAlmostEqual(v[0][1]+v[1][1],0,places=5)   # centred
        self.assertAlmostEqual(v[0][0],.75*TOSS_SIZE/2,places=5)   # x keeps its axis and the same scale
        self.assertEqual(tuple(v[1][3:6]),(0,0,1))   # normals and UVs as authored
        self.assertEqual(v[2][-1],0)   # no glass
        self.assertEqual(struct.unpack_from('<6I',result,32+3*36),(0,1,1,2,2,2))   # each mesh's indices after the last
        for code in TOSS:self.assertIn(code+'.pvr',FILES)
        self.assertEqual(sorted(TOSS),['tbs','tfb','tst','tsw','twk'])
        # The six throwing potions (#292) fly as their own bottles: cache v4.
        for code in THROWN:self.assertIn(code+'.pvr',FILES)
        self.assertEqual(THROWN,('gps','gpm','gpl','ops','opm','opl'))
        self.assertEqual(VERSION,4)
        with self.assertRaises(ValueError):encode_toss([('point',[(0,0,0,0,1,0,0,0)]*3,[0,1,2])],(4,4,78,[bytes(16)]))

    def test_texture_relative_offset_mips_and_truncation(self):
        data=bytearray(68)
        struct.pack_into('<IHHII',data,0,0x2845443c,62,0,4,4)
        struct.pack_into('<I',data,28,1)
        struct.pack_into('<II',data,36,16,12)
        data[52:68]=bytes(range(16))
        self.assertEqual(texture(data),(4,4,78,[bytes(range(16))]))
        with self.assertRaises(ValueError):texture(data[:-1])
        struct.pack_into('<I',data,40,0)
        with self.assertRaises(ValueError):texture(data)

    def test_parser_bounds_before_decompression(self):
        for data in (b'',bytes(104),bytes.fromhex('e59b495e6f631f141e13eba990beedc4')+bytes(100)):
            with self.assertRaises(ValueError):Granny(data)
        with self.assertRaises(ValueError):decode(b'',100*1024*1024,4)
        with self.assertRaises(ValueError):decode(b'',4,1)
        with self.assertRaises(ValueError):decode(b'abc',4,0)

if __name__=='__main__':unittest.main()
