"""The main menu's act scenes (character select) in VR: a scene round you, not a picture before you.

The game opens on one preset for all five acts, data/hd/env/preset/ui/frontendact2.json: each act's
vignette is built for one fixed camera looking down -x, with painted imposter cards (forest, hills)
and a painted sky behind it, and nothing behind the camera. With [frontend] enabled the head stands
where that camera stands (vrcam CampfireView); this fills the rest round it:

  - the clouds' cards (FX_UIFrontEnd*_clouds) out: in the headset they stuck to the eyes;
  - round each prepared act, copies of its own ground, plants, stones, imposter cards and mountains,
    turned about the camera a quarter, a half and three quarters of a turn - only those that come to
    lie outside the front the game built (its own picture stays as it is), the ground a hair lower
    so that the scene's own wins where both lie, and what lies far pulled in toward the camera and
    scaled by the same share (the same size to the eye, clear of the next act's vignette).
  - Act 1: a leafy forest round you (the Dark Wood's trees, thickets, grass, the map's fields as asked);
  - Act 2: the desert outside Lut Gholein - its own plants, palms, mounds and rocks turned round, an oasis of
    palms behind you, big sand mounds as dunes, scrub, sand piles and rocks (SCATTER), all within SIDES.

Read from the player's own game, never packaged.

  python prepare_frontend_scene.py "<game folder>" --casclib "<...>/bin/CascLib.dll" [--write]
  python prepare_frontend_scene.py --from-json frontendact2.json --out <file>      (development)
Undo: delete <game>/mods/D2R-PCVR/D2R-PCVR.mpq/data/hd/env/preset/ui/frontendact2.json
"""
import argparse
import copy
import json
import math
import sys
from pathlib import Path

PRESET = 'data/hd/env/preset/ui/frontendact2.json'
CLOUDS = '_clouds'
COPY = '_pcvr_r'

# Each act's camera (x, y, z), as the game puts it (vrcam's log, 2026-11-13); all look down -x.
CAMERAS = {
    1: (50.0, 4.0, 483.0),
    2: (80.0, 4.0, 110.0),
    3: (23.0, 4.0, -313.5),
    4: (30.0, 4.0, -789.0),
    5: (34.0, 4.0, -1258.5),
}
ACTS = (1, 2, 3, 4, 5)            # the acts filled round (one by one, as each is tuned)
REACH = 700.0                    # an act's own pieces: within this of its camera
LANE = 180.0                     # ... and within this of its camera's z (the vignettes lie along z)
TURNS = (90.0, 180.0, 270.0)     # degrees about the camera's vertical
FRONT = 60.0                     # a copy that comes to lie within this of straight ahead is left out
NEAR = 6.0                       # nor one right at the eyes
FAR = 220.0                      # beyond this a copy is pulled in to it (scaled, the same size to the eye)
SINK = 0.06                      # each turn's ground this much lower than the one before
# and below the act's own (Act 2, owner 2026-11-14: the turned ground's sand rose over the cobbles under the hero)
SINK_UNDER = {2: 2.5}

GROUND = ('ui_groundplane/terrain_act', 'ui_act2_vignette/ui_act2_vignette_terrain')
NOT = ('terrain_act1_path', '_sky.', 'campfire', 'bonfire', 'wall', 'top_cap', 'fence', 'stoneware')
# ... but these of an act's own do come round (Act 4, 2026-11-15 09:23, "the heap still hangs in the air": the gate's
# pillar, its skull and the rubble on top stand on the ruined wall ui_wall_destroyed01 - 'wall', so it stayed behind)
NOT_OK = {}   # (2026-11-17: Act 4's ui_wall_destroyed01 brought round floated - "a piece of island with a turret"; out again)
# Pieces of an act's own not copied round, by entity name (Act 4, 2026-11-15 10:12, a screenshot: "a heap of small round
# things at my eye height, a little left, on the land past the guardian's island" - the skull pile that lies 7.2 up,
# on the menu's own high ground, which does not come round with it)
NOT_COPY = {4: ('lava_skull_pile_large03_04',)}
# The painted cards (ui_imposter), mountains and the depth fade are not copied round: every card has the
# Countess's tower on it and at the sides they showed for the pictures they are (owner 2026-11-13).
KEEP = GROUND + ('grass', 'weeds', 'bush', 'fern', 'tree', 'ivy', 'branch', 'boulder', 'stone', 'pebbles', 'grunge')
FAR_KINDS = ()
# Per act, kinds pulled in to a radius of their own (Act 5: the far mountains, 250-380 ahead, turned to the left
# would lie in Act 4 - pulled in to 190, the same size to the eye). {act: (radius, kinds)}
FAR_ACT = {5: (190.0, ('act5_vignette_mountain',))}
# Each act's own: what of its pieces is copied round (Act 2, Lut Gholein's outskirts, owner 2026-11-14: "Pelne 3D
# dla scen: Akt 2" - the desert's own plants, palms, sand mounds and rocks; its walls, gate and camp stay its own).
KEEP_ACT = {2: GROUND + ('grass', 'bush', 'palm', 'mound', 'sand_pile', 'rock_'),
            # Act 4, the River of Flame (owner 2026-11-15, "Pelne 3D dla scen: Akt 4"): its rock islands and ridges,
            # skulls, bones, rubble, spikes, the ruined gate's pillars and walls, the lava pools - all round you
            4: GROUND + ('ui_act4_vignette/rocks/', 'act4_lava_islands/', 'act4_lava_skull_piles/', 'act4_mesa_skeleton_piles/',
                         'act4_mesa_rubbles_props/', 'act4_lava_crystals/', 'act4_diab_rubble/', 'act4_mesa_props/',
                         'act4_mesa_pillars/', 'act4_mesa_rubble/', 'act4_lava_lava_kits/', 'ui_act4_vignette/architecture/',
                         'act4_diab_cliffs/', 'act4_lava_floor_tiles/', 'act4_lava_props/', 'act4_mesa_gates/'),
            # Act 5, the frozen highlands (owner 2026-11-15, "Pelne 3D dla scen: Akt 5": "Mrozne wyzyny", the level's
            # own sky, no falling snow yet): the menu's own snowy hills, rocks, snow piles, moorland grass, shrubs,
            # bristlecones and dead trees, and its far mountains - round you; the siege (walls, pikes, weapons) stays ahead
            5: GROUND + ('expansion_grass', 'expansion_shrub', 'expansion_flower', 'expansion_tree_', 'expansion_bush_',
                         'rocks_snowy', 'slush_pile', 'snow_pile', 'snow_grunge',
                         'ui_pebbles', 'rock_pile', 'act5_vignette_mountain')}
            # (2026-11-15 10:12: the raised ground with its steps - siege_hills slope_*, ui_ridge_bridge - not round you:
            # "the copy behind me, right/down... I don't want it there"; the highlands' own cliffs come from the editor)
LANE_ACT = {2: 120.0, 4: 150.0, 5: 150.0}  # Act 2's own scene lies within about 120 of its camera's z
# Copies only between these (right of the camera, -left .. right): Act 1's forest begins about 137 to Act 2's
# left (its cut_right 285 shows Act 1 to 88 to Act 2's left), Act 3's scene about 300 to its right.
# Act 4 (2026-11-15): Act 3 shows to 237 right of itself (its cut), i.e. to 239 left of Act 4 (476 apart); Act 5 is
# 469.5 to the right (the cuts meet at 234). The copies' places kept well inside, their models reach out further.
SIDES = {2: (-85.0, 200.0), 4: (-190.0, 190.0),
         # Act 5 (2026-11-15): Act 4 to the left (the cuts meet 234 left of us), nothing to the right
         5: (-190.0, 450.0)}
# Pieces with a sound of their own (Act 2's palms: wind in the leaves) are copied without it.
SOUND_OK = {2}
# Pieces that see-through-when-behind (WallTransparencyComponent: the gate's pillars, chains and skull arch) are copied
# without it (Act 4, 2026-11-15 02:24: "a little heap floats in the air" - the rubble that lies on the gate's pillars
# came round, the pillars did not).
WALLS_OK = {4}

# A forest behind and beside you (owner 2026-11-13: "chcemy osiągnąć las", leafy trees first, closer
# together the farther off): rings round the camera, each (radius, trees), within FOREST_ARC degrees of
# straight behind; each tree a little off its place, turned at random, its size within FOREST_SCALE.
FOREST_TREES = ('data/hd/env/foliage/act1/model/act1_trees_beech_frontend/act1_tree_beech_frontend01.model',
                'data/hd/env/foliage/act1/model/act1_trees_elm_frontend/act1_tree_elm_frontend01.model')
FOREST_RINGS = ((35.0, 3), (65.0, 6), (100.0, 11), (140.0, 18), (185.0, 28))
FOREST_ARC = 110.0
FOREST_JITTER = 0.35             # of the space between neighbours, in radius and along the ring
FOREST_SCALE = (0.9, 1.4)
FOREST_Y = -0.5                  # the foot a little in the ground
FOREST_NEAR = 30.0               # no trunk nearer the eyes than this

# More forest (owner 2026-11-13: "jeszcze za mało drzew", about 100 more of the Dark Wood's leafy trees,
# 20% taller, in all kinds of groupings): the leafy trees the Dark Wood's tiles (act1/outdoors/trees*.json)
# place, set at random behind you within DARK_ARC of straight behind, DARK_NEAR..DARK_FAR off, never
# nearer a neighbour than DARK_GAP (the near end .. the far end, so the farther the denser), some in
# clumps (DARK_CLUMP of them by one already set).
DARK_TREES = tuple(f'data/hd/env/foliage/act1/model/{m}.model' for m in (
    'act1_trees_elm/act1_tree_elm02', 'act1_trees_beech/act1_tree_beech01', 'act1_trees_maple/act1_tree_maple04',
    'act1_trees_linden/act1_tree_linden02', 'act1_trees_linden/act1_tree_linden01', 'act1_trees_elm/act1_tree_elm01',
    'act1_trees_oak/act1_tree_oak03', 'act1_trees_maple/act1_tree_maple02'))
DARK_COUNT = 100
DARK_ARC = 115.0
DARK_NEAR, DARK_FAR = 40.0, 240.0
DARK_GAP = (24.0, 10.0)
DARK_CLUMP = 0.4
DARK_SCALE = (0.9, 1.15)
DARK_TALL = 1.2                  # and this much taller

# No tree in these fields of the grid map round the act's camera (owner 2026-11-13: the trees nearest
# behind, F7 F8 G7 G8 H8, have next to no leaves). The map: 40-unit fields, columns A.. from the left,
# rows 1.. from the front, the camera in the middle of G7.
CLEAR = {1: ('F7', 'F8', 'G7', 'G8', 'H8')}
GRID = 40.0
GRID_LEFT, GRID_FRONT = -260.0, 260.0   # the map's left edge (right of you +) and front edge (ahead +)

# A thicket close behind you (owner 2026-11-13: 50 of them in E8-H9): the same leafy trees, at random in
# those fields, never nearer than THICK_NEAR to the eyes nor THICK_GAP to any other tree.
# More of the same, as densely (owner 2026-11-13): 50 in C5-D9 and 50 in I5-J9. Each: (fields, trees).
THICK = {1: ((('E8', 'E9', 'F8', 'F9', 'G8', 'G9', 'H8', 'H9'), 50),
             (tuple(f'{c}{r}' for c in 'CD' for r in range(5, 10)), 50),
             (tuple(f'{c}{r}' for c in 'IJ' for r in range(5, 10)), 50),
             # and (owner 2026-11-13, evening) about 20 in E6-F7, and I3-J5 as densely (30)
             (('E6', 'E7', 'F6', 'F7'), 20),
             (tuple(f'{c}{r}' for c in 'IJ' for r in range(3, 6)), 30),
             (('E4', 'E5'), 10),   # (owner 2026-11-13, later)
             (('D3', 'D4'), 6),   # (owner 2026-11-13, evening: "kilka drzew w D3 i D4")
             (('F5',), 5))}   # (owner 2026-11-13, 21:13)
THICK_NEAR = 16.0
THICK_GAP = 7.0

# Grass everywhere round you, as densely as it goes (owner 2026-11-13): A6-C13, D8-H13 and I6-M13. The scene's
# own clumps on a jittered grid GRASS_STEP apart, turned and sized at random, a little in the ground.
GRASS = {1: tuple(f'{c}{r}' for c in 'ABC' for r in range(6, 14)) +
            tuple(f'{c}{r}' for c in 'DEFGH' for r in range(8, 14)) +
            tuple(f'{c}{r}' for c in 'IJKLM' for r in range(6, 14))}
GRASS_MODELS = tuple(f'data/hd/env/model/ui/ui_act1_vignette/foliage/{m}.model' for m in (
    'ui_act1_grass_clump05', 'ui_act1_grass_clump05', 'ui_act1_grass_clump03', 'ui_act1_grass_clump_low01',
    'ui_act1_grass_clump_low01', 'ui_act1_grass_clump04', 'ui_act1_weeds06', 'ui_act1_grass_tall01'))
# More (owner 2026-11-13, evening): E6-F7 and I3-J5, with bushes and weeds among the grass ("inny foliage").
# Each further group: (fields, models, its own seed) - the first group stays as it was.
GRASS_MORE = {1: ((('E6', 'E7', 'F6', 'F7') + tuple(f'{c}{r}' for c in 'IJ' for r in range(3, 6)),
                   GRASS_MODELS + tuple(f'data/hd/env/model/ui/ui_act1_vignette/foliage/{m}.model' for m in (
                       'ui_act1_bush_hawthorn01', 'ui_act1_bush_hawthorn05', 'ui_act1_bush_witchhazel01',
                       'ui_act1_bush_witchhazel04', 'ui_act1_weeds02', 'ui_act1_weeds03', 'ui_act1_weeds05')), 5101),
                  (tuple(f'H{r}' for r in range(3, 8)), GRASS_MODELS, 5102))}   # (owner 2026-11-13, later)
# and then none in rows 11-13 nor in columns A, B, L, M (owner 2026-11-13, later)
GRASS_DROP = {1: tuple(f'{c}{r}' for c in 'ABCDEFGHIJKLM' for r in range(11, 14)) +
                 tuple(f'{c}{r}' for c in 'ABCDKLM' for r in range(1, 14))}   # K, then C and D too (owner 2026-11-13)

# All foliage out of columns C and D (owner 2026-11-13, 21:13): the turned copies' plants there too.
FOLIAGE_DROP = {1: tuple(f'{c}{r}' for c in 'CD' for r in range(1, 14))}

# Half the foliage (ours and the turned copies' plants) out of columns E and J and row 10 (owner
# 2026-11-13, 23:44): every second one there, counted from the farthest.
FOLIAGE_HALF = {1: tuple(f'{c}{r}' for c in 'EJ' for r in range(1, 14)) + tuple(f'{c}10' for c in 'ABCDEFGHIJKLM')}
# Our trees out of row 3 (owner 2026-11-13, 23:44); the scene's own stay. Back again (owner 2026-11-14 00:52:
# "dostaw z powrotem drzewa ... tam, gdzie ostatnio je usunąłeś" - the big trees, D3, I3, J3).
TREES_OUT = {}
# Single trees right where asked, after everything else: (field, model[, (right, ahead) from the field's middle,
# size]). F6's middle (owner 2026-11-13, 23:44). A broad oak at E5's corner nearest you, on the line from you to
# the painted mountain beyond E5, to hide it (owner 2026-11-14 00:47).
TREES_AT = {1: (('F6', 'data/hd/env/foliage/act1/model/act1_trees_elm/act1_tree_elm02.model'),
                ('E5', 'data/hd/env/foliage/act1/model/act1_trees_oak/act1_tree_oak03.model', (20.0, -20.0), 1.4))}

# A few of our trees out of rows 12 and 13 (owner 2026-11-13, evening): every THIN-th one there, counted
# from the farthest, goes (the scene's own stay).
# And half of ours in columns A, B, C (owner 2026-11-13, 21:13). Each: (fields, every n-th out).
THIN = {1: ((tuple(f'{c}{r}' for c in 'ABCDEFGHIJKLM' for r in (12, 13)), 2),
            (tuple(f'{c}{r}' for c in 'ABC' for r in range(1, 14)), 2))}
GRASS_STEP = 5.0

# Act 2 round you (owner 2026-11-14): the desert outside Lut Gholein's wall - an oasis of palms behind you, scrub,
# sand mounds and rocks, a little in the sand. Each group: (name, fields, count, models, gap, (size lo, hi), taller,
# y); set at random in its fields, never nearer the eyes than SCATTER_NEAR nor nearer than gap to another of the
# same kind (palms: to any tree), the same from run to run. The map as Act 1's: the camera in the middle of G7.
A2 = 'data/hd/env/'
A2_PALMS = tuple(f'{A2}foliage/act2/model/act2_trees_palm/{m}.model' for m in ('act2_tree_palm01', 'act2_tree_palm02'))
A2_SCRUB = (f'{A2}foliage/act2/model/act2_bushes/act2_bush01.model', f'{A2}foliage/act2/model/act2_bushes/act2_bush02.model',
            f'{A2}foliage/act2/model/act2_bushes/act2_bush04.model', f'{A2}foliage/global/model/act2_wadi_bushes/act2_wadi_bush01.model',
            f'{A2}foliage/global/model/act2_grassclumps/act2_grass_clump02.model',
            f'{A2}model/global/prop/act2/outdoors/act2_outdoors_grass/grass_small01.model')
A2_MOUNDS = (f'{A2}model/ui/ui_act2_vignette/mound01_low.model',)
A2_SAND = (f'{A2}model/ui/ui_act2_vignette/sand_pile01.model',)
A2_ROCKS = tuple(f'{A2}model/global/prop/act2/outdoors/act2_outdoors_rocks/{m}.model'
                 for m in ('rock_large02', 'rock_set02', 'rock_small04'))
def _cells(cols, rows):
    return tuple(f'{c}{r}' for c in cols for r in rows)
SCATTER_V12 = {2: (
    # the oasis: palms in two groves behind you, left and right, and a few on their own between and farther off
    ('palm', _cells('EF', range(9, 12)), 14, A2_PALMS, 9.0, (0.85, 1.2), 1.0, 0.3),
    ('palm', _cells('IJ', range(9, 12)), 14, A2_PALMS, 9.0, (0.85, 1.2), 1.0, 0.3),
    ('palm', _cells('GH', range(11, 14)) + ('E6', 'J6', 'K8'), 8, A2_PALMS, 14.0, (0.9, 1.3), 1.0, 0.3),
    # dunes: the vignette's sand mounds made big, mostly far behind and at the sides
    ('dune', _cells('EFGHIJK', range(11, 14)) + _cells('EK', range(3, 11)), 28, A2_MOUNDS, 18.0, (1.5, 3.5), 0.8, -0.8),
    # scrub and dry grass all round, sparse (a desert)
    ('scrub', _cells('EFGHIJK', range(4, 13)), 140, A2_SCRUB, 4.0, (0.4, 0.9), 1.0, 0.2),
    # sand piles and rocks here and there
    ('sand', _cells('EFGHIJK', range(6, 13)), 18, A2_SAND, 10.0, (0.8, 1.6), 1.0, 0.1),
    ('rock', _cells('EFGHIJK', range(5, 13)), 14, A2_ROCKS, 12.0, (0.6, 1.3), 1.0, -0.3),
)}
SCATTER = {}                     # (Act 2's own scatter, v10-v12, put aside for its strips 2026-11-14)
SCATTER_NEAR = 6.0

# Lut Gholein's low wall on (owner 2026-11-14, screens 2 and 3): ui_wall01 again and again along its own line, to the
# right from the last of it and to the left from the cheap piece beside the gate (a whole one in its place, the gap at
# its foot filled), up to the walls of our sky. Each act: (the first piece, the next, the cheap piece left of the gate,
# pieces to the right, pieces to the left, how much lower the left ones stand).
WALLS = {2: ('ui_wall01_01', 'ui_wall01_02', 'ui_wall01_cheap_05', 9, 4, 0.8)}   # left: Act 1's forest begins at 137
WALLS_RIGHT_OF = {2: 'ui_wall01_03'}

# The cobbled road from the gate to the hero on behind you to the horizon (owner 2026-11-14: "zeby ta droga byla
# ulozona az po horyzont za mna"): its straight run of stone trims laid again and again along its own line; the
# pieces that ended it at the hero out. Each act: (the run, the end pieces out, how far behind, the gap between runs).
ROAD = {2: (('trim_straight02_04', 'trim_single02_03', 'trim_straight01_11', 'trim_straight01_05', 'trim_straight03_07',
             'trim_straight06_14', 'trim_straight01_13', 'trim_straight03_08'),
            ('trim_pile01_01', 'trim_end01_12', 'trim_end01_11', 'trim_straight05_11', 'trim_corner01_07'),
            340.0, 3.8)}
ROAD_CLEAR = 9.0                 # nothing scattered nearer the road than this
ROAD_PALMS = {2: (26.0, 13.0, (0.9, 1.25))}   # palms along it: every so far, so far to the side, their size
ROAD_LINE = {}                   # the act's road (a point, its direction on the ground), set by road()

# Act 2 behind you, made anew (owner 2026-11-14 01:48: the road to the horizon as the scene's spine, rocks as in
# the Far Oasis and the Lost City at its side): the game's own way out of Lut Gholein to the west, preset
# act2/town/transW2 - a strip 100 wide (x) and 563 long (z) with a road 29 wide across it (between its curbs at
# z 123 and 152) and the desert's big cliffs along it - laid again and again behind you, its road on the line
# of the vignette's cobbled road from the gate, the strip's long side to your left (its cliffs: the scene's left
# edge). Its ground, its plants, cliffs, curbs and rocks, its sand in the air; not its terrain stamps, prefabs,
# decals, sounds nor bodies. Each act: (the preset, the road's middle on the strip's z, the strip's width along the
# road, the first strip and how many, its height, the first one's extra depth (under the vignette), how far behind
# the camera the road begins).
TILES = {2: ('data/hd/env/preset/act2/town/transw2.json', 137.5, 100.0, -1, 6, 0.4, 1.0, 15.0)}
# Its curbs run 63 of the strip's 100 (x 13..76): the gap to the next strip filled with its own curbs moved on
# (owner 2026-11-14: curbs the whole length). (z from, z to, by how much, only up to this x).
TILE_CURBS = {2: (115.0, 160.0, 63.0, 113.0)}
# The road's curbs this much higher (owner 2026-11-14 16:02: "podnieś wszystkie murki naszego krawężnika o 0,20").
CURB_LIFT = {2: 0.20}
TILE_SIDES = (-125.0, 215.0)
ROAD_SIDES = TILE_SIDES
# The road turns right, towards Act 3, gently (owner 2026-11-14 07:48: at most 20 degrees, not 90): (from how far
# along it, over how far, by how many degrees). And how far it goes (the sky's right wall cuts it after).
ROAD_TURN = {2: (10.0, 120.0, 20.0)}   # (08:03: the turn not seen - from much nearer, 20 degrees)
# The strip's road (its curbs) moved this far to the camera's right, onto the cobbles (08:03: the vignette's line
# of trims is the cobbles' left edge, not their middle - one curb ran down the cobbles, the other under the sand)
ROAD_SHIFT = {2: 0.0}   # (08:20: the road is its own path now, ROAD_PATH; 14.5 = the cobbles' middle off the trims)
ROAD_COBBLES_OFF = 14.5          # the vignette's cobbles' middle: this far right of its line of trims
# The road's own path (owner 2026-11-14 08:20, drawn on the map): from under the camera straight back, then
# bending right towards Act 3 and on to the sky's right wall. Points (right, ahead) of the camera, smoothed.
ROAD_PATH_V16 = {2: ((-2.0, 12.0), (-1.0, -28.0), (10.0, -52.0), (38.0, -70.0), (85.0, -85.0), (135.0, -96.0),
                     (200.0, -110.0), (330.0, -138.0), (520.0, -178.0))}
# 08:57: the turn halved (it ends ~38 degrees off straight back, not ~76), the road 20% narrower (curbs 11.6 either
# side), the whole of it moved right by half its width.
_RS = 11.6
ROAD_PATH = {2: tuple((r + _RS, a) for r, a in ((0.0, 12.0), (0.0, -40.0), (6.0, -65.0), (20.0, -90.0), (40.0, -115.0),
                                                 (71.0, -154.0), (102.0, -193.0), (133.0, -232.0), (164.0, -271.0),
                                                 (226.0, -349.0), (350.0, -505.0)))}
ROAD_HALF = 14.5                 # the strip's curbs: this far either side of its road's middle
ROAD_NARROW = {2: 0.8}           # ... drawn in to this share (08:57: 20% narrower)
# Along the road (08:03): grass by both curbs, outside them (every so far, from, to, models); two palms near
# behind, one each side ((along, to the right, size)...)
CURB_GRASS_V15 = {2: (3.5, 4.0, 260.0, ('data/hd/env/foliage/global/model/act2_grassclumps/act2_grass_clump02.model',
                                     'data/hd/env/foliage/global/model/act2_grassclumps/act2_grass_clump01.model',
                                     'data/hd/env/model/global/prop/act2/outdoors/act2_outdoors_grass/grass_small01.model',
                                     'data/hd/env/foliage/global/model/act2_wadi_tulips/act2_wadi_tulips02.model'))}
CURB_GRASS = {}
ROAD_SIDE_PALMS = {2: ((22.0, -22.0, 1.1), (38.0, 23.0, 1.0))}   # (along, from the road's middle, size)
ROAD_FAR = {2: 520.0}
# The road's cobbles and the sand by it (owner 2026-11-14 07:48: "zeby ta droga ... kostka brukowa leciala dalej",
# "po obu bokach ... piasek taki, jaki jest teraz przy murach"): the strip's own ground showed the game's 'no
# texture' squares in the menu, so the vignette's ground instead - the one under the hero, its cobbled road on the
# same line - again and again along the road; and under it all, wider, the same ground larger for the sand far off.
# (its entity, every so far, each this much lower, the wide one's size, every so far, how much lower)
# 08:57: once the vignette's own cobbles (gate to the hero) laid on behind the camera, then the ground plane's flat
# cobbles for the rest. (its entity, every so far, each this much lower, -, -, -; ROAD_GROUND_COUNT copies)
ROAD_GROUND = {2: ('ui_act2_vignette_terrain_01', 70.0, 0.5, 1.0, 0.0, 0.0)}   # 0.5 under: its city end must stay under the hero's cobbles
ROAD_GROUND_COUNT = {2: 1}
ROAD_GROUND_V17 = {2: ('ui_act2_vignette_terrain_01', 50.0, 0.02, 3.0, 0.0, 0.8)}   # (wide every 0: none)
# 08:29 - "chcialbym najpierw zobaczyc te droge, a pozniej bedziemy kombinowac z jej poboczami": the copies flat (their
# dunes and mounds pressed down to nothing, only the ground's painted cobbles and sand left) and under the vignette's
# own ground; of the strip only its curbs; no grass by them.
ROAD_FLAT = {}                   # (v17: {2: (0.04, 0.35)} - shimmered)     # (the copies' height scale, how far under the vignette's ground they lie)
TILE_ONLY = {2: ('stone_trims/trim_',)}
# A tower of the city over its wall (owner 2026-11-14: "za murem postaw wieze ... zeby wystawala nam na widok"): the
# menu's own spire model, left of the gate behind the wall. Each: (model, right, ahead, height, size).
TOWERS = {2: (('data/hd/env/model/act2/town/act2_town_palace_walls/tower01.model', -95.0, 58.0, 0.0, 1.7),   # (08:40: much taller)   # (08:20: the city's rightmost tower, not the spire - it looked like a piece of something much taller)
             # 08:05: the painted city's tower left of the big dome is a hollow shell in the headset (one card,
             # it cannot be lowered alone): a whole palace tower stood into it to fill it (a first try)
             ('data/hd/env/model/act2/town/act2_town_palace_walls/tower01.model', -7.0, 205.0, -3.0, 1.15))}   # (08:40: behind the hollow one, a little more left)   # (08:29: at 178 it hid behind the card; nearer and a little larger)   # its foot ~21 under its middle (08:03: it hung over the wall)
# The vignette's own pieces moved a little (name: down by) - 08:03: the bush and grass nearest the eyes hung
NUDGE = {2: {'act2_wadi_bush01_12': 0.5, 'act2_grass_clump02_14': 0.5, 'act2_grass_clump02_19': 0.5}}
# Rocks on the left from the city wall's end back to the strip's cliffs (08:03: "skaly po lewej ... stykaly sie z
# murem"), and sand at both ends of the wall where the ground ended (big low sand mounds).
# Each: (model, right, ahead, y, turn in degrees, size)
_BC = 'data/hd/env/model/act2/bigcliff/act2_bigcliff_ranges/'
_MD = 'data/hd/env/model/act2/outdoors/act2_outdoors_mounds/'
_CL = 'data/hd/env/model/act2/bigcliff/act2_bigcliff_cliffs/'
SET_PIECES = {2: (
    # a wall of rock (08:20: not five mounds side by side) - the cliff borders round Lut Gholein, end to end
    # along the left from the city wall's end back (each about 62 long on its own z, turned to run back)
    # (08:29: it ran up onto the city wall - each 36 further back, the small piece by the wall's end out)
    # (08:57: back as it stood at 08:20 - in the city wall's end - and on as a wall to the map's bottom, every 30)
    (_CL + 'border02.model', -108.0, -6.0, 0.0, 90.0, 1.0),
    (_CL + 'border02.model', -108.0, -36.0, 0.0, 270.0, 1.0),
    (_CL + 'border02.model', -108.0, -66.0, 0.0, 90.0, 1.0),
    (_CL + 'border02.model', -108.0, -96.0, 0.0, 270.0, 1.0),
    (_CL + 'border02.model', -108.0, -126.0, 0.0, 90.0, 1.0),
    (_CL + 'border02.model', -108.0, -156.0, 0.0, 270.0, 1.0),
    (_CL + 'border02.model', -108.0, -186.0, 0.0, 90.0, 1.0),
    (_CL + 'border02.model', -108.0, -216.0, 0.0, 270.0, 1.0),
    (_CL + 'border02.model', -108.0, -246.0, 0.0, 90.0, 1.0),
    (_CL + 'border02.model', -108.0, -276.0, 0.0, 270.0, 1.0),
    (_CL + 'border02.model', -108.0, -306.0, 0.0, 90.0, 1.0),
    ('data/hd/env/model/ui/ui_groundplane/act2_groundplane.model', 20.0, -100.0, 0.2, 0.0, 1.0),   # (08:57: up, it lay under)
)}   # by the wall's left end (07:48)     # right of the camera: nothing of the strips beyond
TILE_KEEP = ('TransformDefinitionComponent', 'ModelDefinitionComponent', 'ModelPlatformTierComponent', 'VfxDefinitionComponent')
GRASS_SCALE = (0.8, 1.3)
GRASS_NEAR = 4.0                 # none at your feet

# Ivy hanging from nothing (owner 2026-11-13, at your left, about E6-E7): our copies of the hanging ivy out
# there, and anywhere none of the trees it hung from is within IVY_TREE of it.
IVY_FIELDS = {1: ('E6', 'E7')}
IVY_TREE = 12.0

# Not to be seen while the other acts' vignettes still stand round you (owner 2026-11-13: Act 2's great
# painted sky, the temple, catches the eye from Act 1).
HIDE = (   # (act1_depth_fade01 tried out 2026-11-13: not the dark half-round; act2_sky_01 back 2026-11-13 23:55 -
          # the mod's [frontend] cut_right keeps Act 2's scene out of Act 1's view, its 2D stays whole)
        # a test (2026-11-13): Act 1's painted sky card - its edge stood in our sky as a line that went with the head
        'act1_sky_01',
        # and Act 2's again (2026-11-14 00:40): seen from Act 1 behind its forest, the trees' edges kept its light -
        # they glowed in our sky even with the card cut out by the mod. Act 2 gets its own sky when it is made.
        'act2_sky_01',   # (shown again for a look 2026-11-14 01:28; out 01:48 - it breaks the other acts. Act 2's sky is
                         # now ours: [frontend] pano_2, a burning yellow noon like this card's)
        # and all the other acts' cards (2026-11-14 00:45): Acts 3 and 4's glowed in Act 1's trees from afar too.
        # Every act stands under its own sky of ours now ([frontend] acts), cut off from its neighbours by the mod.
        # (Act 3's own back for a look 2026-11-14 19:11; out again 19:57 - Act 3's sky is now ours: [frontend] pano_3,
        # D2R_Front_act3.png, an overcast dusk like this card's)
        'act3_sky_01', 'act4_sky_01', 'act5_sky_01',
        'ui_skybox_act3_depth_fade01_01', 'ui_skybox_act5_depth_fade01_01',
        # Act 2: wooden posts hanging in the air right of the city (owner 2026-11-14, screen 1) - they stood on a
        # painted card's roof in the flat picture
        'wall_posts01_03',
        # Act 2: the sand in the air before the gate and just behind it (G6, owner 2026-11-14 16:02): the game's own
        # particles, flat sprites turned to the screen - in the headset they lean with the head. Out until they can
        # be made to stand upright.
        'FX_SandHaze_low_small_gusts_01', 'FX_SandHaze_low_small_gusts_03',
        'FX_UIFrontEndAct2_sand_30x30_01', 'FX_UIFrontEndAct2_motes_30x30_01',)

# Light behind you (owner 2026-11-13: "trzeba tę scenę doświetlić"): the act's own moonlight (a spot
# from above, ahead of you) turned round the camera as the copies are, and a few dim moonlight-blue
# points over the forest: (ahead degrees, off, high, power).
MOON = {1: 'act1_vignette_accent_directional02_01'}
# A point light reaches only its radius: the scene's own have one (25 for power 1000); the first of ours had
# none and lit nothing (2026-11-13). Hung up in the crowns they looked unnatural (owner 2026-11-13): below the
# branches, a little over the grass, and more of them, in arcs from your right round behind you to your
# left at the distances they stood at. Each ring: (off, lights, high, power, radius); 90 degrees = right.
GLOW_RINGS = ((35.0, 7, 6.0, 2500.0, 35.0), (85.0, 9, 6.0, 3500.0, 45.0), (145.0, 11, 6.0, 4500.0, 55.0))
GLOW_ARC = (90.0, 270.0)
# Off for now (owner 2026-11-13: "wyłącz na chwilę ... ale nie likwiduj") while the wisps ([frontend] wisps,
# our shader's) are tried: the moon turned round and the glows stay set here, not put in the scene.
LIGHTS_ON = False
GLOWS = tuple((GLOW_ARC[0] + (GLOW_ARC[1] - GLOW_ARC[0]) * i / (n - 1), off, high, power, radius)
              for off, n, high, power, radius in GLOW_RINGS for i in range(n))
GLOW_COLOR = {'x': 0.6, 'y': 0.73, 'z': 1.0}


def transform(entity):
    return next((c for c in entity.get('components', []) if c.get('type') == 'TransformDefinitionComponent'), None)


def model_of(entity):
    return next((str(c.get('filename', '')).lower() for c in entity.get('components', [])
                 if c.get('type') == 'ModelDefinitionComponent'), '')


def plain_model(entity, sound=False):
    """A model and nothing else (no light, effect, sound, decal, skeleton); with sound, a sound may be on it."""
    kinds = {c.get('type') for c in entity.get('components', [])}
    return 'ModelDefinitionComponent' in kinds and kinds <= {
        'TransformDefinitionComponent', 'ModelDefinitionComponent', 'ModelPlatformTierComponent'} | (
        {'AudioEmitterComponent'} if sound else set())


def qmul(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (aw * bx + ax * bw + ay * bz - az * by,
            aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw,
            aw * bw - ax * bx - ay * by - az * bz)


def without_clouds(preset):
    keep = [e for e in preset['entities'] if CLOUDS not in str(e.get('name', '')).lower()]
    removed = len(preset['entities']) - len(keep)
    preset['entities'] = keep
    return removed


def fill_round(preset, act, ids):
    if act in TILES or act in TOWN:   # behind it the game's own strips / town instead (TILES, TOWN)
        return 0, 0
    cx, cy, cz = CAMERAS[act]
    sources = []
    for e in preset['entities']:
        t, m = transform(e), model_of(e)
        if not t or not m or COPY in str(e.get('name', '')) or str(e.get('name', '')).startswith('pcvr_'):
            continue
        kinds = {c.get('type') for c in e.get('components', [])}
        if not plain_model(e) and not (act in SOUND_OK and plain_model(e, sound=True)) and not (
                act in WALLS_OK and 'ModelDefinitionComponent' in kinds and kinds <= {
                    'TransformDefinitionComponent', 'ModelDefinitionComponent', 'ModelPlatformTierComponent',
                    'WallTransparencyComponent'}):
            continue
        base = m.rsplit('/', 1)[-1]
        if e.get('name') in NOT_COPY.get(act, ()):
            continue
        if (any(n in base for n in NOT) and not any(k in base for k in NOT_OK.get(act, ()))) or not any(k in m if '/' in k else k in base for k in KEEP_ACT.get(act, KEEP)):
            continue
        p = t['position']
        dx, dz = float(p['x']) - cx, float(p['z']) - cz
        if math.hypot(dx, dz) > REACH or abs(dz) > LANE_ACT.get(act, LANE):
            continue
        sources.append(e)
    added = []
    for n, turn in enumerate(TURNS, 1):
        th = math.radians(turn)
        c, s = math.cos(th), math.sin(th)
        yaw = (0.0, math.sin(th / 2), 0.0, math.cos(th / 2))
        for e in sources:
            p = transform(e)['position']
            dx, dy, dz = float(p['x']) - cx, float(p.get('y', 0.0)) - cy, float(p['z']) - cz
            nx, nz = dx * c + dz * s, -dx * s + dz * c
            r = math.hypot(nx, nz)
            m = model_of(e)
            ground = any(g in m for g in GROUND)
            if not ground:
                ahead = math.degrees(math.acos(max(-1.0, min(1.0, -nx / r)))) if r > 1e-6 else 0.0
                if ahead < FRONT or r < NEAR:
                    continue
            lo, hi = SIDES.get(act, (-1e9, 1e9))
            if not ground and not lo <= -nz <= hi:   # right of the camera is -z
                continue
            far, kinds = FAR_ACT.get(act, (FAR, FAR_KINDS))
            k = far / r if (r > far and any(f in m for f in kinds)) else 1.0
            x = copy.deepcopy(e)
            x['components'] = [c for c in x['components'] if c.get('type') not in ('AudioEmitterComponent',
                                                                                   'WallTransparencyComponent')]
            x['name'] = f'{e.get("name", "entity")}{COPY}{int(turn)}'
            new = (int(e.get('id', 0)) * 2654435761 + 0x9E3779B9 * n) & 0xFFFFFFFF
            while new in ids or new == 0:
                new = (new + 0x01000193) & 0xFFFFFFFF
            ids.add(new)
            x['id'] = new
            t = transform(x)
            t['position'] = {'x': cx + nx * k, 'y': cy + dy * k - ((SINK * n + SINK_UNDER.get(act, 0.0)) if ground else
                                                                SINK_UNDER.get(act, 0.0)), 'z': cz + nz * k}
            q = t.get('orientation') or {'x': 0.0, 'y': 0.0, 'z': 0.0, 'w': 1.0}
            qx, qy, qz, qw = qmul(yaw, tuple(float(q.get(a, 0.0)) for a in ('x', 'y', 'z', 'w')))
            t['orientation'] = {'x': qx, 'y': qy, 'z': qz, 'w': qw}
            if k != 1.0:
                sc = t.get('scale') or {'x': 1.0, 'y': 1.0, 'z': 1.0}
                t['scale'] = {a: float(sc.get(a, 1.0)) * k for a in ('x', 'y', 'z')}
            for comp in x.get('components', []):
                if comp.get('name') and comp is not t:
                    comp['name'] = f'{comp["name"]}{COPY}{int(turn)}'
            t['name'] = f'{t.get("name", "transform")}{COPY}{int(turn)}'
            added.append(x)
    preset['entities'].extend(added)
    return len(sources), len(added)


def forest(preset, act, ids):
    """FOREST_RINGS of FOREST_TREES behind the act's camera, on the same random from run to run (Act 1's)."""
    if act != 1:
        return 0
    import random
    rnd = random.Random(1000 + act)
    cx, cy, cz = CAMERAS[act]
    added = []
    for ring, (radius, count) in enumerate(FOREST_RINGS):
        step = 2.0 * FOREST_ARC / count
        for i in range(count):
            behind = -FOREST_ARC + step * (i + 0.5) + rnd.uniform(-1, 1) * FOREST_JITTER * step
            r = max(FOREST_NEAR, radius + rnd.uniform(-1, 1) * FOREST_JITTER * math.radians(step) * radius)
            a = math.radians(180.0 + behind)          # 0 = straight ahead (-x)
            x, z = cx - r * math.cos(a), cz - r * math.sin(a)
            yaw = rnd.uniform(0.0, 2.0 * math.pi)
            sc = rnd.uniform(*FOREST_SCALE)
            model = FOREST_TREES[(ring + i) % len(FOREST_TREES)]
            name = f'pcvr_forest_{act}_{ring}_{i}'
            new = rnd.getrandbits(32)
            while new in ids or new == 0:
                new = rnd.getrandbits(32)
            ids.add(new)
            added.append({'type': 'Entity', 'name': name, 'id': new, 'components': [
                {'type': 'TransformDefinitionComponent', 'name': f'{name}_TransformDefinitionComponent',
                 'position': {'x': x, 'y': FOREST_Y, 'z': z},
                 'orientation': {'x': 0.0, 'y': math.sin(yaw / 2), 'z': 0.0, 'w': math.cos(yaw / 2)},
                 'scale': {'x': sc, 'y': sc, 'z': sc}, 'inheritOnlyPosition': False},
                {'type': 'ModelDefinitionComponent', 'name': f'{name}_ModelDefinitionComponent', 'filename': model,
                 'visibleLayers': 1, 'lightMask': 19, 'shadowMask': 0, 'ghostShadows': False, 'floorModel': False,
                 'terrainBlendEnableYUpBlend': False, 'terrainBlendMode': 1}]})
    preset['entities'].extend(added)
    deps = preset.setdefault('dependencies', {}).setdefault('models', [])
    have = {str(d.get('path', '')).lower() for d in deps if isinstance(d, dict)}
    for m in FOREST_TREES:
        if m.lower() not in have:
            deps.append({'path': m})
    return len(added)


def new_id(rnd, ids):
    new = rnd.getrandbits(32)
    while new in ids or new == 0:
        new = rnd.getrandbits(32)
    ids.add(new)
    return new


def tree(name, ident, model, x, y, z, yaw, scale):
    return {'type': 'Entity', 'name': name, 'id': ident, 'components': [
        {'type': 'TransformDefinitionComponent', 'name': f'{name}_TransformDefinitionComponent',
         'position': {'x': x, 'y': y, 'z': z},
         'orientation': {'x': 0.0, 'y': math.sin(yaw / 2), 'z': 0.0, 'w': math.cos(yaw / 2)},
         'scale': {'x': scale[0], 'y': scale[1], 'z': scale[2]}, 'inheritOnlyPosition': False},
        {'type': 'ModelDefinitionComponent', 'name': f'{name}_ModelDefinitionComponent', 'filename': model,
         'visibleLayers': 1, 'lightMask': 19, 'shadowMask': 0, 'ghostShadows': False, 'floorModel': False,
         'terrainBlendEnableYUpBlend': False, 'terrainBlendMode': 1}]}


def floors():
    """Our floor plates (prepare_frontend_floor.py): {model path, lower case: its terrain maps without _ALB.texture}."""
    f = Path(__file__).resolve().parent / 'frontend_floors.json'
    try:
        return {v['model'].lower(): v['maps'] for v in json.loads(f.read_text(encoding='utf-8')).values() if 'maps' in v}
    except (OSError, ValueError, KeyError, TypeError):
        return {}


def floor_names():
    """Our floor plates by name: {'act3_stone': its model path, ...} (frontend_floors.json; none before it is made)."""
    f = Path(__file__).resolve().parent / 'frontend_floors.json'
    try:
        return {k: v['model'] for k, v in json.loads(f.read_text(encoding='utf-8')).items()}
    except (OSError, ValueError, KeyError, TypeError, AttributeError):
        return {}


def ground_pavers(name):
    """A ground of ours' flat stones (prepare_frontend_floor.GROUNDS): [[x, y, z, yaw, scale, model], ...] in its space."""
    f = Path(__file__).resolve().parent / 'frontend_floors.json'
    try:
        return list(json.loads(f.read_text(encoding='utf-8'))[name].get('pavers', ()))
    except (OSError, ValueError, KeyError, TypeError, AttributeError):
        return []


def depend(preset, models):
    deps = preset.setdefault('dependencies', {}).setdefault('models', [])
    have = {str(d.get('path', '')).lower() for d in deps if isinstance(d, dict)}
    plates = floors()
    for m in models:
        if m.lower() not in have:
            deps.append({'path': m})
            have.add(m.lower())
        if m.lower() in plates:   # a floor plate of ours: the game's terrain maps it names
            tex = preset['dependencies'].setdefault('textures', [])
            th = {str(d.get('path', '')).lower() for d in tex if isinstance(d, dict)}
            for w in ('ALB', 'NRM', 'ORM'):
                t = f'{plates[m.lower()]}_{w}.texture'
                if t.lower() not in th:
                    tex.append({'path': t})


def field(act, x, z):
    """The grid map's field ('G8') a point lies in, round the act's camera."""
    cx, _, cz = CAMERAS[act]
    right, ahead = cz - z, cx - x
    col, row = math.floor((right - GRID_LEFT) / GRID), math.floor((GRID_FRONT - ahead) / GRID) + 1
    return f'{chr(65 + col)}{row}' if 0 <= col < 26 and row > 0 else ''


def darkwood(preset, act, ids):
    """DARK_COUNT of the Dark Wood's DARK_TREES behind the act's camera, the same from run to run (Act 1's)."""
    if act != 1:
        return 0
    import random
    rnd = random.Random(2000 + act)
    cx, cy, cz = CAMERAS[act]
    clear = set(CLEAR.get(act, ()))
    placed = [(transform(e)['position']['x'], transform(e)['position']['z']) for e in preset['entities']
              if str(e.get('name', '')).startswith(f'pcvr_forest_{act}_')]
    mine, added, tries = [], [], 0
    while len(added) < DARK_COUNT and tries < 200000:
        tries += 1
        if mine and rnd.random() < DARK_CLUMP:
            bx, bz, br = rnd.choice(mine)
            gap = DARK_GAP[0] + (DARK_GAP[1] - DARK_GAP[0]) * (br - DARK_NEAR) / (DARK_FAR - DARK_NEAR)
            a, d = rnd.uniform(0, 2 * math.pi), rnd.uniform(1.0, 1.6) * gap
            x, z = bx + d * math.cos(a), bz + d * math.sin(a)
        else:
            r = DARK_NEAR + (DARK_FAR - DARK_NEAR) * math.sqrt(rnd.random())   # more far off
            a = math.radians(180.0 + rnd.uniform(-DARK_ARC, DARK_ARC))
            x, z = cx - r * math.cos(a), cz - r * math.sin(a)
        dx, dz = x - cx, z - cz
        r = math.hypot(dx, dz)
        if not DARK_NEAR <= r <= DARK_FAR or math.degrees(math.acos(max(-1.0, min(1.0, -dx / r)))) < 180.0 - DARK_ARC:
            continue
        if field(act, x, z) in clear:
            continue
        gap = DARK_GAP[0] + (DARK_GAP[1] - DARK_GAP[0]) * (r - DARK_NEAR) / (DARK_FAR - DARK_NEAR)
        if any(math.hypot(x - px, z - pz) < gap for px, pz in placed):
            continue
        placed.append((x, z))
        mine.append((x, z, r))
        sc = rnd.uniform(*DARK_SCALE)
        name = f'pcvr_darkwood_{act}_{len(added)}'
        added.append(tree(name, new_id(rnd, ids), rnd.choice(DARK_TREES), x, FOREST_Y, z,
                          rnd.uniform(0.0, 2.0 * math.pi), (sc, sc * DARK_TALL, sc)))
    preset['entities'].extend(added)
    depend(preset, DARK_TREES)
    return len(added)


def thicket(preset, act, ids):
    """Each of the act's THICK groups: its count of leafy DARK_TREES at random in its fields."""
    total = 0
    for n, (fields, count) in enumerate(THICK.get(act, ())):
        total += thicket_group(preset, act, ids, fields, count, 4000 + act + 100 * n)
    return total


def thicket_group(preset, act, ids, fields, count, seed):
    import random
    rnd = random.Random(seed)
    cx, cy, cz = CAMERAS[act]
    # our sparse-leaved menu trees and copies there make way for it (the scene's own stay)
    preset['entities'] = [e for e in preset['entities'] if not (
        (COPY in str(e.get('name', '')) or str(e.get('name', '')).startswith('pcvr_forest_')) and transform(e) and
        'tree' in model_of(e).rsplit('/', 1)[-1] and
        field(act, float(transform(e)['position']['x']), float(transform(e)['position']['z'])) in fields)]
    cols = [ord(f[0]) - 65 for f in fields]
    rows = [int(f[1:]) for f in fields]
    placed = [(float(transform(e)['position']['x']), float(transform(e)['position']['z'])) for e in preset['entities']
              if transform(e) and 'tree' in model_of(e).rsplit('/', 1)[-1]]
    added, tries = [], 0
    while len(added) < count and tries < 200000:
        tries += 1
        right = GRID_LEFT + GRID * rnd.uniform(min(cols), max(cols) + 1)
        ahead = GRID_FRONT - GRID * (rnd.uniform(min(rows), max(rows) + 1) - 1)
        x, z = cx - ahead, cz - right
        if field(act, x, z) not in fields or math.hypot(x - cx, z - cz) < THICK_NEAR:
            continue
        if any(math.hypot(x - px, z - pz) < THICK_GAP for px, pz in placed):
            continue
        placed.append((x, z))
        sc = rnd.uniform(*DARK_SCALE)
        added.append(tree(f'pcvr_thicket_{act}_{seed}_{len(added)}', new_id(rnd, ids), rnd.choice(DARK_TREES), x, FOREST_Y, z,
                          rnd.uniform(0.0, 2.0 * math.pi), (sc, sc * DARK_TALL, sc)))
    preset['entities'].extend(added)
    depend(preset, DARK_TREES)
    return len(added)


def grass(preset, act, ids):
    """GRASS_MODELS over the act's GRASS fields, then each GRASS_MORE group, the same from run to run."""
    grass_group(preset, act, ids, GRASS.get(act, ()), GRASS_MODELS, 5000 + act, 'pcvr_grass')
    for k, (fields, models, seed) in enumerate(GRASS_MORE.get(act, ())):
        grass_group(preset, act, ids, fields, models, seed, f'pcvr_grass{k + 2}')
    drop = set(GRASS_DROP.get(act, ()))
    cdrop = set(FOLIAGE_DROP.get(act, ()))
    def gone(e):
        n, t = str(e.get('name', '')), transform(e)
        if not t:
            return False
        f = field(act, float(t['position']['x']), float(t['position']['z']))
        if n.startswith('pcvr_grass'):
            return f in drop
        base = model_of(e).rsplit('/', 1)[-1]
        return COPY in n and f in cdrop and any(k in base for k in ('grass', 'weeds', 'bush', 'fern', 'pebbles', 'grunge'))
    keep = [e for e in preset['entities'] if not gone(e)]
    preset['entities'] = keep
    return sum(1 for e in keep if str(e.get('name', '')).startswith('pcvr_grass'))


def grass_group(preset, act, ids, fields, models, seed, prefix):
    """models over fields, GRASS_STEP apart (jittered), turned and sized at random."""
    import random
    rnd = random.Random(seed)
    cx, cy, cz = CAMERAS[act]
    fields = set(fields)
    added = []
    steps = int(GRID / GRASS_STEP)
    for f in sorted(fields):
        col, row = ord(f[0]) - 65, int(f[1:])
        for i in range(steps):
            for j in range(steps):
                right = GRID_LEFT + GRID * col + GRASS_STEP * (i + rnd.uniform(0.1, 0.9))
                ahead = GRID_FRONT - GRID * (row - 1) - GRASS_STEP * (j + rnd.uniform(0.1, 0.9))
                if math.hypot(right, ahead) < GRASS_NEAR:
                    continue
                sc = rnd.uniform(*GRASS_SCALE)
                name = f'{prefix}_{act}_{len(added)}'
                g = tree(name, new_id(rnd, ids), rnd.choice(models), cx - ahead, rnd.uniform(-0.5, 0.0), cz - right,
                         rnd.uniform(0.0, 2.0 * math.pi), (sc, sc, sc))
                g['components'][1]['lightMask'] = 19
                added.append(g)
    preset['entities'].extend(added)
    depend(preset, models)
    return len(added)


def scatter(preset, act, ids):
    """The act's SCATTER groups, in order."""
    import random
    cx, cy, cz = CAMERAS[act]
    total = 0
    for n, (kind, fields, count, models, gap, size, taller, y) in enumerate(SCATTER.get(act, ())):
        rnd = random.Random(7000 + 100 * act + n)
        fields = set(fields)
        cols = [ord(f[0]) - 65 for f in fields]
        rows = [int(f[1:]) for f in fields]
        tree_kind = any('tree' in m for m in models)
        placed = [(float(transform(e)['position']['x']), float(transform(e)['position']['z'])) for e in preset['entities']
                  if transform(e) and (('tree' in model_of(e).rsplit('/', 1)[-1]) if tree_kind else
                                       str(e.get('name', '')).startswith(f'pcvr_{kind}_{act}_'))]
        added, tries = [], 0
        while len(added) < count and tries < 200000:
            tries += 1
            right = GRID_LEFT + GRID * rnd.uniform(min(cols), max(cols) + 1)
            ahead = GRID_FRONT - GRID * (rnd.uniform(min(rows), max(rows) + 1) - 1)
            x, z = cx - ahead, cz - right
            if field(act, x, z) not in fields or math.hypot(ahead, right) < SCATTER_NEAR or not off_road(act, x, z):
                continue
            if any(math.hypot(x - px, z - pz) < gap for px, pz in placed):
                continue
            placed.append((x, z))
            sc = rnd.uniform(*size)
            low = SINK_UNDER.get(act, 0.0) * max(0.0, min(1.0, (-ahead + 10.0) / 20.0))   # behind: on the lower ground
            added.append(tree(f'pcvr_{kind}_{act}_{n}_{len(added)}', new_id(rnd, ids), rnd.choice(models), x, y - low, z,
                              rnd.uniform(0.0, 2.0 * math.pi), (sc, sc * taller, sc)))
        preset['entities'].extend(added)
        depend(preset, models)
        total += len(added)
    return total


def by_name(preset):
    return {e.get('name'): e for e in preset['entities']}


def walls(preset, act, ids):
    """The act's WALLS: its low wall's pieces on along its line, right and left."""
    if act not in WALLS:
        return 0
    import random
    rnd = random.Random(8000 + act)
    first, second, cheap, right_n, left_n, lower = WALLS[act]
    named = by_name(preset)
    a, b = transform(named[first])['position'], transform(named[second])['position']
    step = (float(b['x']) - float(a['x']), float(b['z']) - float(a['z']))
    added = []
    def put(src, x, y, z, k):
        w = copy.deepcopy(named[first])
        name = f'pcvr_wall_{act}_{k}'
        w['name'], w['id'] = name, new_id(rnd, ids)
        for c in w['components']:
            c['name'] = f'{name}_{c.get("type", "c")}'
        t = transform(w)
        t['position'] = {'x': x, 'y': y, 'z': z}
        t['orientation'] = dict(transform(src)['orientation'])
        added.append(w)
    last = transform(named[WALLS_RIGHT_OF[act]])['position']
    for k in range(1, right_n + 1):
        put(named[WALLS_RIGHT_OF[act]], float(last['x']) + step[0] * k, float(last['y']), float(last['z']) + step[1] * k, f'r{k}')
    c = named[cheap]
    cp = transform(c)['position']
    for k in range(0, left_n + 1):
        put(c, float(cp['x']) - step[0] * k, float(cp['y']) - lower, float(cp['z']) - step[1] * k, f'l{k}')
    preset['entities'] = [e for e in preset['entities'] if e is not c]
    preset['entities'].extend(added)
    return len(added)


def road(preset, act, ids):
    """The act's ROAD: its run of trims again and again behind the camera, palms along it."""
    if act not in ROAD:
        return 0
    import random
    rnd = random.Random(9000 + act)
    run, ends, far, gap = ROAD[act]
    cx, cy, cz = CAMERAS[act]
    named = by_name(preset)
    pieces = [named[n] for n in run]
    pos = [(float(transform(e)['position']['x']), float(transform(e)['position']['z'])) for e in pieces]
    (x0, z0), (x1, z1) = pos[0], pos[-1]
    ln = math.hypot(x1 - x0, z1 - z0)
    d = ((x1 - x0) / ln, (z1 - z0) / ln)                 # from the gate towards you (+x = behind)
    along = [(x - x0) * d[0] + (z - z0) * d[1] for x, z in pos]
    period = max(along) - min(along) + gap
    ROAD_LINE[act] = ((x0, z0), d)
    preset['entities'] = [e for e in preset['entities'] if e.get('name') not in ends]
    if act in TILES:   # the strips (tiles) bring the road on
        return 0
    added, k = [], 1
    while True:
        ox, oz = d[0] * period * k, d[1] * period * k
        if x0 + ox - cx > far:
            break
        for e in pieces:
            p = transform(e)['position']
            x, z = float(p['x']) + ox, float(p['z']) + oz
            behind = x - cx
            sink = SINK_UNDER.get(act, 0.0) * max(0.0, min(1.0, (behind + 10.0) / 20.0))
            r = copy.deepcopy(e)
            name = f'pcvr_road_{act}_{k}_{e["name"]}'
            r['name'], r['id'] = name, new_id(rnd, ids)
            for c in r['components']:
                c['name'] = f'{name}_{c.get("type", "c")}'
            transform(r)['position'] = {'x': x, 'y': float(p['y']) - sink, 'z': z}
            added.append(r)
        k += 1
    if act in ROAD_PALMS:
        every, side, size = ROAD_PALMS[act]
        s, n = every * 0.6, 0
        while True:
            bx, bz = x0 + d[0] * (ln + s), z0 + d[1] * (ln + s)
            if bx - cx > far:
                break
            sd = side * (1 if n % 2 == 0 else -1) + rnd.uniform(-3.0, 3.0)
            x, z = bx - d[1] * sd + rnd.uniform(-3.0, 3.0), bz + d[0] * sd
            if math.hypot(x - cx, z - cz) > SCATTER_NEAR * 2:
                sc = rnd.uniform(*size)
                added.append(tree(f'pcvr_roadpalm_{act}_{n}', new_id(rnd, ids), rnd.choice(A2_PALMS), x, 0.3 - SINK_UNDER.get(act, 0.0),
                                  z, rnd.uniform(0.0, 2.0 * math.pi), (sc, sc, sc)))
            s += every * rnd.uniform(0.7, 1.3)
            n += 1
        depend(preset, A2_PALMS)
    preset['entities'].extend(added)
    return len(added)


def tiles(preset, act, ids, sources):
    """The act's road behind the camera: its cobbles (the vignette's own ground again and again along the road),
    sand wide round it (the same ground, larger, lower) and the game's TILES strip's cliffs, curbs and plants, all
    laid along one road that turns gently right (ROAD_TURN)."""
    if act not in TILES or act not in ROAD_LINE:
        return 0
    import random
    name_, mid, width, first, count, high, deeper, start = TILES[act]
    tile = json.loads(sources[name_])
    rnd = random.Random(9500 + act)
    cx, cy, cz = CAMERAS[act]
    (x0, z0), d = ROAD_LINE[act]
    t0 = (cx + start - x0) / d[0]                     # the road's point 'start' behind the camera
    sx, sz = x0 + d[0] * t0, z0 + d[1] * t0
    phi0 = math.atan2(-d[1], d[0])                    # the vignette's line of trims' heading (u = (cos, -sin))
    # the road's middle: ROAD_PATH smoothed (Catmull-Rom), from in front of the camera on; s = 0 level with it
    pts = [(cx - 90.0, cz + 3.0)] + [(cx - a_, cz - r_) for r_, a_ in ROAD_PATH[act]]
    pts = [pts[0]] + pts + [pts[-1]]
    line = []
    for i in range(1, len(pts) - 2):
        p0, p1, p2, p3 = pts[i - 1], pts[i], pts[i + 1], pts[i + 2]
        for k in range(40):
            t_ = k / 40.0
            line.append(tuple(0.5 * (2 * p1[c] + (-p0[c] + p2[c]) * t_ + (2 * p0[c] - 5 * p1[c] + 4 * p2[c] - p3[c]) * t_ ** 2 +
                                     (-p0[c] + 3 * p1[c] - 3 * p2[c] + p3[c]) * t_ ** 3) for c in (0, 1)))
    line.append(pts[-2])
    along = [0.0]
    for i in range(1, len(line)):
        along.append(along[-1] + math.hypot(line[i][0] - line[i - 1][0], line[i][1] - line[i - 1][1]))
    zero = next(along[i] for i in range(len(line)) if line[i][0] >= cx)   # level with the camera
    along = [a_ - zero for a_ in along]
    def seg(s):
        lo, hi = 0, len(along) - 1
        while hi - lo > 1:
            m_ = (lo + hi) // 2
            if along[m_] <= s:
                lo = m_
            else:
                hi = m_
        return lo
    def phi(s):
        i = seg(s)
        return math.atan2(-(line[i + 1][1] - line[i][1]), line[i + 1][0] - line[i][0])
    def mid_at(s):
        i = seg(s)
        w = (s - along[i]) / max(1e-6, along[i + 1] - along[i])
        return line[i][0] + (line[i + 1][0] - line[i][0]) * w, line[i][1] + (line[i + 1][1] - line[i][1]) * w
    sx, sz = mid_at(0.0)
    def warp(s, t):
        mx, mz = mid_at(s)
        a_ = phi(s)
        return mx - math.sin(a_) * t, mz - math.cos(a_) * t, a_ - phi0
    def turned(q, by):
        r = (0.0, math.sin(by / 2), 0.0, math.cos(by / 2))
        x_, y_, z_, w_ = qmul(r, tuple(float(q.get(k, 0.0)) for k in ('x', 'y', 'z', 'w')))
        return {'x': x_, 'y': y_, 'z': z_, 'w': w_}
    u0, n0 = (math.cos(phi0), -math.sin(phi0)), (-math.sin(phi0), -math.cos(phi0))
    added, models, particles = [], set(), set()
    far = ROAD_FAR[act]

    # 1. the cobbles and the sand by them: the vignette's ground, on and on along the road (each a little lower
    # than the one before, so the nearer one wins where they lie on each other)
    ground = next(e for e in preset['entities'] if e.get('name') == (ROAD_GROUND.get(act) or ROAD_GROUND_V17[act])[0])
    gp = transform(ground)['position']
    go = (float(gp['x']) - x0, float(gp['z']) - z0)   # the vignette's ground in the frame of its line of trims
    gt = go[0] * n0[0] + go[1] * n0[1] - ROAD_COBBLES_OFF   # ... from its cobbles' middle
    gs = -(cx - float(gp['x']))                       # ... level with it on our road (s: behind the camera)
    _, every, lower, wide, wide_every, wide_lower = ROAD_GROUND.get(act) or ROAD_GROUND_V17[act]
    def lay(kind, j, s, scale, y):
        x, z, by = warp(s, gt * scale if kind == 'wide' else gt)
        if cz - z > ROAD_SIDES[1] + 40.0:
            return
        g = copy.deepcopy(ground)
        name = f'pcvr_road{kind}_{act}_{j}'
        g['name'], g['id'] = name, new_id(rnd, ids)
        for comp in g['components']:
            comp['name'] = f'{name}_{comp.get("type", "c")}'
        tt = transform(g)
        tt['position'] = {'x': x, 'y': y, 'z': z}
        tt['orientation'] = turned(transform(ground)['orientation'], by)
        flat = ROAD_FLAT.get(act)
        tt['scale'] = {'x': scale, 'y': flat[0] if flat else 1.0, 'z': scale}
        if flat:
            tt['position']['y'] = float(gp['y']) - flat[1] - (y and (float(gp['y']) - y))
        added.append(g)
    j = 1
    while act in ROAD_GROUND and gs + every * j <= far and j <= ROAD_GROUND_COUNT.get(act, 999):
        lay('cobbles', j, gs + every * j, 1.0, float(gp['y']) - lower * j)
        j += 1
    j = 0
    while wide_every and gs + wide_every * j <= far + wide_every:
        lay('wide', j, gs + wide_every * j, wide, float(gp['y']) - wide_lower)
        j += 1

    # 2. the strip's cliffs, curbs, plants and sand in the air, along the same road
    curbs = TILE_CURBS.get(act)
    more = []
    if curbs:
        for e in tile.get('entities', []):
            tr, m = transform(e), model_of(e)
            if tr and 'stone_trims/trim_' in m and curbs[0] <= float(tr['position']['z']) <= curbs[1] and \
                    float(tr['position']['x']) + curbs[2] < curbs[3]:
                f = copy.deepcopy(e)
                f['name'] = f'{e.get("name", "trim")}_fill'
                transform(f)['position']['x'] = float(tr['position']['x']) + curbs[2]
                more.append(f)
    for k in range(max(first, 0), first + count):
        for e in tile.get('entities', []) + more:
            comps = e.get('components', [])
            kinds = {comp.get('type') for comp in comps}
            if not ({'ModelDefinitionComponent', 'VfxDefinitionComponent'} & kinds) or 'TerrainDefinitionComponent' in kinds:
                continue
            if act in TILE_ONLY and not any(k in model_of(e) for k in TILE_ONLY[act]):
                continue
            tr = transform(e)
            if not tr:
                continue
            p = tr['position']
            sa = width * (k + 1) - float(p['x'])      # the strip's x runs towards the camera
            ta = float(p['z']) - mid
            if 'stone_trims/trim_' in model_of(e) and abs(ta) < ROAD_HALF + 8.0:
                ta *= ROAD_NARROW.get(act, 1.0)          # the curbs drawn in
            ta += ROAD_SHIFT.get(act, 0.0)               # ... its z to the camera's right
            if sa > far:
                continue
            x, z, by = warp(sa, ta)
            right, ahead = cz - z, cx - x
            if ahead > -8.0 and abs(right) < 80.0 or ahead > 60.0:
                continue   # nothing of it in the vignette's way
            if not ROAD_SIDES[0] <= right <= ROAD_SIDES[1]:
                continue   # nor past the walls of our sky (Act 1's forest to the left, Act 3 to the right)
            n = copy.deepcopy(e)
            n['components'] = [comp for comp in n['components'] if comp.get('type') in TILE_KEEP]
            name = f'pcvr_tile_{act}_{k}_{e.get("name", "e")}'
            n['name'], n['id'] = name, new_id(rnd, ids)
            for comp in n['components']:
                comp['name'] = f'{name}_{comp.get("type", "c")}'
                if comp.get('type') == 'ModelDefinitionComponent':
                    models.add(comp['filename'])
                if comp.get('type') == 'VfxDefinitionComponent':
                    particles.add(comp['filename'])
            tt = transform(n)
            lift = CURB_LIFT.get(act, 0.0) if 'stone_trims/trim_' in model_of(e) else 0.0
            tt['position'] = {'x': x, 'y': float(p.get('y', 0.0)) + high + lift, 'z': z}
            q = tt.get('orientation') or {'x': 0.0, 'y': 0.0, 'z': 0.0, 'w': 1.0}
            # the strip turned so its x runs back to the camera (heading phi0 + 180) and on with the road
            tt['orientation'] = turned(q, phi0 + math.pi + by)
            added.append(n)
    # 3. grass by both curbs, two palms near behind
    def ground_y(s):
        return float(gp['y']) - lower * max(0.0, (s - gs) / every) - 0.15
    sh = ROAD_SHIFT.get(act, 0.0)
    if act in CURB_GRASS:
        gstep, g0, g1, gm = CURB_GRASS[act]
        n = 0
        for side in (-1.0, 1.0):
            sa = g0
            while sa < g1:
                ta = sh + side * (ROAD_HALF + rnd.uniform(1.5, 5.0))
                x, z, by = warp(sa + rnd.uniform(-1.0, 1.0), ta)
                if ROAD_SIDES[0] <= cz - z <= ROAD_SIDES[1]:
                    sc = rnd.uniform(0.6, 1.1)
                    m = rnd.choice(gm)
                    added.append(tree(f'pcvr_curbgrass_{act}_{n}', new_id(rnd, ids), m, x, ground_y(sa), z,
                                      rnd.uniform(0, 2 * math.pi), (sc, sc, sc)))
                    models.add(m)
                    n += 1
                sa += gstep * rnd.uniform(0.6, 1.4)
    for n, (sa, ta, sc) in enumerate(ROAD_SIDE_PALMS.get(act, ())):
        x, z, by = warp(sa, sh + ta)
        m = A2_PALMS[n % len(A2_PALMS)]
        added.append(tree(f'pcvr_roadpalm_{act}_{n}', new_id(rnd, ids), m, x, ground_y(sa) - 0.2, z,
                          rnd.uniform(0, 2 * math.pi), (sc, sc, sc)))
        models.add(m)
    preset['entities'].extend(added)
    depend(preset, sorted(models))
    deps = preset.setdefault('dependencies', {}).setdefault('particles', [])
    have = {str(x.get('path', '')).lower() for x in deps if isinstance(x, dict)}
    for f in sorted(particles):
        if f.lower() not in have:
            deps.append({'path': f})
    return len(added)


def towers(preset, act, ids):
    import random
    rnd = random.Random(9700 + act)
    cx, cy, cz = CAMERAS[act]
    out = 0
    for k, (model, right, ahead, y, size) in enumerate(TOWERS.get(act, ())):
        preset['entities'].append(tree(f'pcvr_tower_{act}_{k}', new_id(rnd, ids), model, cx - ahead, y, cz - right,
                                       math.radians(200.0), (size, size, size)))
        depend(preset, (model,))
        out += 1
    for k, (model, right, ahead, y, turn, size) in enumerate(SET_PIECES.get(act, ())):
        preset['entities'].append(tree(f'pcvr_piece_{act}_{k}', new_id(rnd, ids), model, cx - ahead, y, cz - right,
                                       math.radians(turn), (size, size, size)))
        depend(preset, (model,))
        out += 1
    for e in preset['entities']:
        dy = NUDGE.get(act, {}).get(e.get('name'))
        if dy and transform(e):
            transform(e)['position']['y'] = float(transform(e)['position']['y']) - dy
            out += 1
    return out


def off_road(act, x, z):
    """Not on the act's road (none: everywhere)."""
    if act not in ROAD_LINE:
        return True
    (x0, z0), d = ROAD_LINE[act]
    t = (x - x0) * d[0] + (z - z0) * d[1]
    if t < 0:
        return True
    return abs(-(x - x0) * d[1] + (z - z0) * d[0]) > ROAD_CLEAR


def ivy_out(preset, act):
    """Our hanging ivy copies in the act's IVY_FIELDS, or with no tree near."""
    if act not in IVY_FIELDS:
        return 0
    fields = set(IVY_FIELDS.get(act, ()))
    trees = [(float(transform(e)['position']['x']), float(transform(e)['position']['z'])) for e in preset['entities']
             if transform(e) and 'tree' in model_of(e).rsplit('/', 1)[-1]]
    keep, out = [], 0
    for e in preset['entities']:
        t = transform(e)
        if COPY in str(e.get('name', '')) and t and 'ivy' in model_of(e).rsplit('/', 1)[-1]:
            x, z = float(t['position']['x']), float(t['position']['z'])
            if field(act, x, z) in fields or not any(math.hypot(x - tx, z - tz) < IVY_TREE for tx, tz in trees):
                out += 1
                continue
        keep.append(e)
    preset['entities'] = keep
    return out


def thin(preset, act):
    """Every n-th of our trees in each of the act's THIN groups of fields out."""
    return sum(thin_group(preset, act, set(fields), n) for fields, n in THIN.get(act, ()))


def thin_group(preset, act, fields, n):
    ours = [e for e in preset['entities'] if transform(e) and 'tree' in model_of(e).rsplit('/', 1)[-1] and
            (COPY in str(e.get('name', '')) or str(e.get('name', '')).startswith('pcvr_')) and
            field(act, float(transform(e)['position']['x']), float(transform(e)['position']['z'])) in fields]
    ours.sort(key=lambda e: -float(transform(e)['position']['x']))   # the farthest behind first
    out = {id(e) for k, e in enumerate(ours) if k % n == 0}
    preset['entities'] = [e for e in preset['entities'] if id(e) not in out]
    return len(out)


FOLIAGE_KINDS = ('grass', 'weeds', 'bush', 'fern', 'pebbles', 'grunge')


def finish(preset, act, ids):
    """FOLIAGE_HALF, TREES_OUT and TREES_AT, last of all."""
    import random
    def ours(e):
        n = str(e.get('name', ''))
        return COPY in n or n.startswith('pcvr_')
    def at(e):
        t = transform(e)
        return field(act, float(t['position']['x']), float(t['position']['z'])) if t else ''
    half = set(FOLIAGE_HALF.get(act, ()))
    plants = [e for e in preset['entities'] if ours(e) and at(e) in half and
              any(k in model_of(e).rsplit('/', 1)[-1] for k in FOLIAGE_KINDS)]
    plants.sort(key=lambda e: (-float(transform(e)['position']['x']), float(transform(e)['position']['z'])))
    out = {id(e) for k, e in enumerate(plants) if k % 2 == 0}
    treeless = set(TREES_OUT.get(act, ()))
    out |= {id(e) for e in preset['entities'] if ours(e) and at(e) in treeless and
            'tree' in model_of(e).rsplit('/', 1)[-1]}
    preset['entities'] = [e for e in preset['entities'] if id(e) not in out]
    rnd = random.Random(6000 + act)
    cx, cy, cz = CAMERAS[act]
    for k, (f, model, *more) in enumerate(TREES_AT.get(act, ())):
        off = more[0] if more else (0.0, 0.0)
        sc = more[1] if len(more) > 1 else 1.0
        right = GRID_LEFT + GRID * (ord(f[0]) - 65) + GRID / 2 + off[0]
        ahead = GRID_FRONT - GRID * (int(f[1:]) - 1) - GRID / 2 + off[1]
        preset['entities'].append(tree(f'pcvr_tree_at_{act}_{k}', new_id(rnd, ids), model, cx - ahead, FOREST_Y,
                                       cz - right, rnd.uniform(0.0, 2.0 * math.pi), (sc, sc * DARK_TALL, sc)))
        depend(preset, (model,))
    return len(out)


def clear_fields(preset, act):
    """Our trees (copies and the forest, never the scene's own) out of the act's CLEAR fields."""
    clear = set(CLEAR.get(act, ()))
    keep, out = [], 0
    for e in preset['entities']:
        n, t = str(e.get('name', '')), transform(e)
        ours = COPY in n or n.startswith(('pcvr_forest_', 'pcvr_darkwood_'))
        if ours and t and 'tree' in model_of(e).rsplit('/', 1)[-1] and \
                field(act, float(t['position']['x']), float(t['position']['z'])) in clear:
            out += 1
            continue
        keep.append(e)
    preset['entities'] = keep
    return out


def lights(preset, act, ids):
    """The act's moonlight turned round the camera, and GLOWS over the forest (none while not LIGHTS_ON)."""
    if not LIGHTS_ON:
        return 0
    import random
    rnd = random.Random(3000 + act)
    cx, cy, cz = CAMERAS[act]
    added = []
    moon = next((e for e in preset['entities'] if e.get('name') == MOON.get(act)), None)
    if moon:
        p = transform(moon)['position']
        dx, dy, dz = float(p['x']) - cx, float(p['y']), float(p['z']) - cz
        for turn in TURNS:
            th = math.radians(turn)
            c, s = math.cos(th), math.sin(th)
            x = copy.deepcopy(moon)
            x['name'] = f'{moon["name"]}{COPY}{int(turn)}'
            x['id'] = new_id(rnd, ids)
            t = transform(x)
            t['position'] = {'x': cx + dx * c + dz * s, 'y': dy, 'z': cz - dx * s + dz * c}
            q = t['orientation']
            qx, qy, qz, qw = qmul((0.0, math.sin(th / 2), 0.0, math.cos(th / 2)),
                                  tuple(float(q.get(a, 0.0)) for a in ('x', 'y', 'z', 'w')))
            t['orientation'] = {'x': qx, 'y': qy, 'z': qz, 'w': qw}
            for comp in x['components']:
                comp['name'] = f'{comp.get("name", "c")}{COPY}{int(turn)}'
            added.append(x)
    for i, (ahead, off, high, power, radius) in enumerate(GLOWS):
        a = math.radians(ahead)
        name = f'pcvr_glow_{act}_{i}'
        added.append({'type': 'Entity', 'name': name, 'id': new_id(rnd, ids), 'components': [
            {'type': 'TransformDefinitionComponent', 'name': f'{name}_TransformDefinitionComponent',
             'position': {'x': cx - off * math.cos(a), 'y': high, 'z': cz - off * math.sin(a)},
             'orientation': {'x': 0.0, 'y': 0.0, 'z': 0.0, 'w': 1.0}, 'scale': {'x': 1.0, 'y': 1.0, 'z': 1.0},
             'inheritOnlyPosition': False},
            {'type': 'PointLightDefinitionComponent', 'name': f'{name}_PointLightDefinition', 'color': dict(GLOW_COLOR),
             'power': power, 'radius': radius, 'attenuation': 4.0, 'shadowCaster': False, 'lightMask': 1, 'isLocalLight': False,
             'diffuseContribution': 1.0, 'specularContribution': 1.0}]})
    preset['entities'].extend(added)
    return len(added)


# Act 3 (owner 2026-11-14 17:02, "Pelne 3D dla scen: Akt 3"): behind you a piece of Kurast Docks itself - the town's
# preset laid round the camera as if you stood on its pier (the map's F8, looking to G7: the pier on the town's
# left that runs from Meshif's ship to Cain) - everything of the town behind you, and the pier you stand on, which
# the menu's own broken dock lacks. Town (x, z) -> the camera's frame: ahead = at_z - z, right = x - at_x.
#   (the town's preset, where you stand in it (x, z), its y to ours, the menu's own scene ahead (right from, to,
#    ahead from, to) where none of the town goes, how far round, the sides)
# 19:11 the whole town round you (owner: "a lot is missing - huts and walkways right and left, the heads in the water
# before us"), but where the menu's own scene stands ahead of you.
TOWN = {3: ('data/hd/env/preset/act3/docktown/docktown3.json', (265.8, 268.7), 0.7, (-75.0, 75.0, 12.0, 250.0), 420.0,
            (-245.0, 245.0))}
# 19:15 "level everything to our pier - it was a bit crooked, no straight line from its start before me to its end
# behind me": the town's pier (x 265.8, straight along its z) laid on the menu's own: dock01_02 (yaw 80, its deck
# 10 wide, 20 long - the town's dock01 too) runs 10 degrees to the right of straight ahead, its middle through right
# -4.0 at the camera (its edges lined up with the town's dock03_13); the town's y + 0.7 puts its dock03 / dock02 (-4.2) at the menu's dock01 (-3.47). The town's own
# pier pieces ahead of 12 (dock01_05, dock04_mirror_03) lay over the menu's and stay out (the zone above).
#   (right of the pier's middle at the camera, its turn to the right in degrees)
TOWN_ALIGN = {3: (-4.0, 10.0)}
# always, also in the menu's own scene: the stone heads in the water before you
TOWN_ALWAYS = ('stone_head',)
# What floats (boats, the ship, scum, the heads, grass in the water) goes down to the menu's water: the town's water
# is at about -3.22 (its scum), the menu's 2.9 lower (-6.11) - 19:11 "the water's levels are uneven". All else (the
# piers, the huts, walls and plants on the shore) stays with the walking level.
TOWN_WATERLINE = {3: -3.22}
TOWN_FLOATING = ('scum', 'act3_docktown_boats', 'bobbing_misc', 'grass_clump_water', 'stone_head', 'act2_town_ship',
                 'lily')
TOWN_PIER = ('act3_docktown_docks/', 'act3_docktown_dock_pieces/')
TOWN_KEEP = ('TransformDefinitionComponent', 'ModelDefinitionComponent', 'ModelPlatformTierComponent',
             'PrefabPlacementDefinitionComponent')
# whole pieces left behind: what moves on a skeleton or cloth (banners, curtains, the hanging frog, corpses), and the
# canoe the town's preset leaves flying 50 up (prepare_kurast_dock.py)
TOWN_SKIP_KINDS = ('SkeletonDefinitionComponent', 'PhysicsClothDefinitionComponent', 'PhysicsPartialRagdollDefinitionComponent')
TOWN_SKIP_IDS = (3418903870,)
# How far to the right the town and its water go: (its pieces, its water's edge), from the camera. Act 4 stands 476 to
# the right of Act 3 (its left cut 237): what lay past 239 showed in Act 4 (owner 2026-11-15 01:48: the town from 200 on
# out - the far right of the docks, the water cut at 239, its last tiles narrowed to end there).
TOWN_RIGHT_MAX = {3: (200.0, 239.0)}
# Water under all of it: the menu's own water (its y), again and again behind you, side by side.
TOWN_WATER = {3: 'water01_01'}
# ... at this height (owner 2026-11-14 22:08: "the water is now at its right level, -3, and it must stay"; the menu's
# own lies at -6.11) - all of it, the menu's own tile too; what floats goes with it (TOWN_FLOATING)
TOWN_WATER_Y = {3: -3.0}
# Our floor plates laid in the town (prepare_frontend_floor.py: FLOORS, SHAPES): (plate, right, ahead, y, yaw, scale).
# Act 3's stone under the crossing behind you (G8-G9), where the town's pavers lie (y 0.4 - 0.7), turned with the town
# (80, as its piers) and cut to its land (a cross).
TOWN_PLATES = {3: (('act3_stone', -15.0, -70.0, 0.35, 80.0, 1.0),)}
# The menu's own models that become a ground of ours (prepare_frontend_floor.py: GROUNDS) - the same place, its stones
# laid on it: {act: ((entity name, ground name), ...)}. Act 3: its ground terrain_act3 as mud and flat stones, without
# the arms that stood over the water at the sides (the game draws no copy of a game model, cut or whole - 2026-11-14).
GROUND_SWAP = {3: (('terrain_act3_01', 'act3_ground'),)}
WATER_EXTENT = {}   # model -> (min x, min z, max x, max z) in its own space, measured from the game (--game) - or a guess
WATER_GUESS = (-0.83 * 128, -128.0, 0.42 * 128, 128.0)


def measure(assets, model):
    """A model's true extent (min xyz, max xyz) from the player's archives (podglad_modeli's reader), or None."""
    try:
        import potion_assets
        import podglad_modeli
        potion_assets.LIMIT = 256 * 1024 * 1024
        names = {n.lower(): n for n in assets.files}
        stem = model.lower()[:-len('.model')]
        for f in [f'{stem}_lod{k}.model' for k in range(6)] + [model.lower()]:
            if f not in names:
                continue
            try:
                meshes = podglad_modeli.Big(assets.read(names[f])).all_meshes()
            except Exception:   # noqa: BLE001 - the next lod
                continue
            pts = [v[:3] for _, _, data in meshes for v in data]
            if pts:
                return [min(p[k] for p in pts) for k in range(3)], [max(p[k] for p in pts) for k in range(3)]
    except Exception:   # noqa: BLE001
        pass
    return None


def town(preset, act, ids, sources):
    """TOWN: the town's own pieces behind you, turned so that its way along the pier is your way ahead; water under."""
    if act not in TOWN:
        return 0
    import random
    name_, (ax, az), dy, (z_r0, z_r1, z_a0, z_a1), reach, (lo, hi) = TOWN[act]
    src = json.loads(sources[name_])
    rnd = random.Random(9700 + act)
    cx, cy, cz = CAMERAS[act]
    r0, phi = TOWN_ALIGN.get(act, (0.0, 0.0))
    yaw = math.pi / 2 - math.radians(phi)   # a quarter turn (the town's -z is our -x), less the pier's own turn
    turn = (0.0, math.sin(yaw / 2), 0.0, math.cos(yaw / 2))
    cf, sf = math.cos(math.radians(phi)), math.sin(math.radians(phi))
    added, models, particles = [], set(), set()
    water = next((e for e in preset['entities'] if e.get('name') == TOWN_WATER.get(act)), None)
    if water and act in TOWN_WATER_Y:
        transform(water)['position']['y'] = TOWN_WATER_Y[act]
    for e in src.get('entities', []):
        comps = e.get('components', [])
        kinds = {c.get('type') for c in comps}
        if int(e.get('id', 0)) in TOWN_SKIP_IDS or kinds & set(TOWN_SKIP_KINDS) or 'TerrainDefinitionComponent' in kinds:
            continue
        if not ({'ModelDefinitionComponent', 'PrefabPlacementDefinitionComponent'} & kinds):
            continue
        tr = transform(e)
        if not tr:
            continue
        p = tr['position']
        ahead, right = az - float(p['z']), float(p['x']) - ax
        m = model_of(e) or str(next((c.get('prefab', '') for c in comps
                                     if c.get('type') == 'PrefabPlacementDefinitionComponent'), '')).lower()
        always = any(k in m for k in TOWN_ALWAYS)
        if not always and z_r0 <= right <= z_r1 and z_a0 <= ahead <= z_a1:
            continue   # the menu's own scene
        if math.hypot(ahead, right) > reach or not lo <= right <= hi:
            continue
        if float(p.get('y', 0.0)) > 30.0:
            continue   # (anything else left up in the air)
        n = copy.deepcopy(e)
        n['components'] = [c for c in n['components'] if c.get('type') in TOWN_KEEP]
        name = f'pcvr_town_{act}_{e.get("name", "e")}'
        n['name'], n['id'] = name, new_id(rnd, ids)
        for c in n['components']:
            c['name'] = f'{name}_{c.get("type", "c")}'
            if c.get('type') == 'ModelDefinitionComponent':
                models.add(c['filename'])
        t = transform(n)
        y = float(p.get('y', 0.0)) + dy
        wl = TOWN_WATERLINE.get(act)
        if wl is not None and water and any(k in m for k in TOWN_FLOATING):
            y += float(transform(water)['position']['y']) - (wl + dy)
        a2, r2 = ahead * cf - right * sf, ahead * sf + right * cf + r0   # on the menu's pier
        if r2 > TOWN_RIGHT_MAX.get(act, (1e9, 1e9))[0]:
            continue
        t['position'] = {'x': cx - a2, 'y': y, 'z': cz - r2}
        q = t.get('orientation') or {'x': 0.0, 'y': 0.0, 'z': 0.0, 'w': 1.0}
        qx, qy, qz, qw = qmul(turn, tuple(float(q.get(k, 0.0)) for k in 'xyzw'))
        t['orientation'] = {'x': qx, 'y': qy, 'z': qz, 'w': qw}
        added.append(n)
    # the water: the menu's own, tiled round you over all of it (the same height: one level)
    nw = 0
    if water:
        wm = model_of(water)
        x0, z0, x1, z1 = WATER_EXTENT.get(wm, WATER_GUESS)
        wt = transform(water)
        # level (19:36 "is the water tilted to look right from the isometric camera? make it level"): the menu's
        # own is turned 5 degrees about z (0, 0, -0.044, 0.999) - each copy of it a step in the water
        wt['orientation'] = {'x': 0.0, 'y': 0.0, 'z': 0.0, 'w': 1.0}
        wp = wt['position']
        sc = float((wt.get('scale') or {}).get('x', 1.0))
        sx, sz = (x1 - x0) * sc, (z1 - z0) * sc
        ni, nj = int((reach + 80.0) / max(sx, 1.0)) + 2, int((max(-lo, hi) + 80.0) / max(sz, 1.0)) + 2
        for i in range(-ni, ni + 1):
            for j in range(-nj, nj + 1):
                if i == 0 and j == 0:
                    continue
                x, z = float(wp['x']) + i * sx, float(wp['z']) + j * sz
                # the tile's middle, in our frame
                mx, mz = x + (x0 + x1) / 2 * sc, z + (z0 + z1) / 2 * sc
                ahead, right = cx - mx, cz - mz
                if math.hypot(max(abs(ahead) - sx / 2, 0.0), 0.0) > reach or right + sz / 2 < lo or right - sz / 2 > hi:
                    continue
                edge = TOWN_RIGHT_MAX.get(act, (1e9, 1e9))[1]
                if right - sz / 2 >= edge:
                    continue
                f = 1.0
                if right + sz / 2 > edge:   # the last tile narrowed to end at the edge
                    f = (edge - (right - sz / 2)) / sz
                    z = cz - edge - z0 * sc * f
                w = copy.deepcopy(water)
                name = f'pcvr_townwater_{act}_{i}_{j}'
                w['name'], w['id'] = name, new_id(rnd, ids)
                for c in w['components']:
                    c['name'] = f'{name}_{c.get("type", "c")}'
                transform(w)['position'] = {'x': x, 'y': float(wp['y']), 'z': z}
                if f != 1.0:
                    ws = dict(transform(w).get('scale') or {'x': 1.0, 'y': 1.0, 'z': 1.0})
                    ws['z'] = sc * f
                    transform(w)['scale'] = ws
                added.append(w)
                nw += 1
    plates = floor_names()
    for ename, cname in GROUND_SWAP.get(act, ()):
        if cname not in plates:
            continue
        for e in list(preset['entities']):
            if e.get('name') == ename:
                for c in e.get('components', []):
                    if c.get('type') == 'ModelDefinitionComponent':
                        c['filename'] = plates[cname]
                        models.add(plates[cname])
                at_ = transform(e)['position']
                for k, (x, y, z, yaw, sc, model) in enumerate(ground_pavers(cname)):
                    added.append(tree(f'pcvr_paver_{act}_{k}', new_id(rnd, ids), model, at_['x'] + x, at_['y'] + y,
                                      at_['z'] + z, math.radians(yaw), (sc, sc, sc)))
                    models.add(model)
    for k, (plate, r, ah, y, yaw, sc) in enumerate(TOWN_PLATES.get(act, ())):
        if plate in plates:
            added.append(tree(f'pcvr_floor_{act}_{k}', new_id(rnd, ids), plates[plate], cx - ah, y, cz - r,
                              math.radians(yaw), (sc, sc, sc)))
            models.add(plates[plate])
    preset['entities'].extend(added)
    depend(preset, sorted(models))
    # the town's textures for the folders of what came over (its models name them; the menu lists its own)
    folders = {m.rsplit('/', 2)[-2] for m in models}
    tex = [d for d in src.get('dependencies', {}).get('textures', []) if isinstance(d, dict) and
           any(f'/{f}/' in str(d.get('path', '')).lower() or f'/{f}_' in str(d.get('path', '')).lower() for f in folders)]
    deps = preset.setdefault('dependencies', {}).setdefault('textures', [])
    have = {str(d.get('path', '')).lower() for d in deps if isinstance(d, dict)}
    for d in tex:
        if str(d['path']).lower() not in have:
            deps.append({'path': d['path']})
            have.add(str(d['path']).lower())
    TOWN_REPORT[act] = f'{len(added) - nw} town pieces, {nw} water tiles ({sx:.0f} x {sz:.0f})' if water else \
        f'{len(added)} town pieces, no water'
    return len(added)


TOWN_REPORT = {}

# Act 4 (owner 2026-11-15): the River of Flame round you - the menu's own lava (its river tile, expansion_lavaE2) laid
# again and again round the camera, side by side, all level at its height (the menu's own is tipped 0.75 degrees for
# the flat camera); the rock islands and ridges come round with fill_round (KEEP_ACT). (entity, how far round, sides)
# The sides are hard edges (Act 3's chat, 2026-11-15 01:57: "Act 4's lava in Act 3" - the 200 tiles reached ~380 to the
# left, into Act 3 by its temple): a tile across one is narrowed to end on it, as Act 3's water (TOWN_RIGHT_MAX).
# Left: Act 3's right edge, 239 to our left; right: halfway to Act 5 (234).
# (2026-11-15 09:23, owner: "I still see the river of lava in Act 3, and in Act 5 too": a neighbour's cut fades its
# view into our sky over 25 units past the cut (D2R_FogWorld.fxh SkyCutAt), and the lava ended 1.5 past both cuts - in
# that fade. Now clear of it: Act 3 sees to 237 + 25 right of itself (214 left of us), Act 5 to 234 + 25 left of itself
# (210.5 right of us) - the lava to -205 / 200.)
LAVA = {4: ('expansion_lavae2_01', 420.0, (-205.0, 200.0))}
LAVA_EXTENT = {}   # model -> (min x, min z, max x, max z), measured from the game (--game); else LAVA_GUESS
LAVA_GUESS = (0.0, 0.0, 200.0, 200.0)
# A lava fall to try (owner 2026-11-15: "a river of lava turned 90 degrees, standing upright by a rock?"): the same lava
# tile, small and stood on end, at your left over the lava - one tipped about its x, one about its z (which way its
# lava runs is the tile's own; the one that runs down stays). (name, ahead, right, width, height, about)
# (02:07: out - the game paints the lava from above, by where it lies, so on end it is only streaks: "a graphic effect,
# not what I have under my feet". Kept for a later try with a lava of our own.)
LAVA_FALLS = {}
# Turned copies brought down (owner 2026-11-15 02:07: "it flies - put it down"): the ruin on the high rock ahead-left
# (a slab, a topless pillar, a brazier, rubble, a lava pool) stands on the menu's own ground's high part, which does not
# come round with it - each copy of these this much lower, onto the lava. {act: (entity names, drop)}
DROP_COPIES = {4: (('hell_brazier02_01', 'ui_wall_destroyed02_01', 'rubble_pile02_04', 'lava04_off_02',
                    'ui_pillar01_blank_topless_01', 'pillar05_05', 'gate_chain_single01_03', 'gate_chain_single01_04'), 10.5)}
# The guardians (owner 2026-11-15 02:12: "the statues of armoured knights by the River of Flame's forge, that turn their
# heads after you - one on each of the three small bone islands round me, facing me"): the Hellforge's statue01 (its
# body, and its head as the game sets it, apart), at the middle of each turned copy of the hero's own island (its bones
# about 15 ahead of the camera, turned a quarter, a half, three quarters round), its face (the model's +x) to you.
#   {act: ((right, ahead) of each, y, size)}
STATUES = {4: (((-15.0, 0.0), (0.0, -15.0), (15.0, 0.0)), 0.8, 1.0)}
STATUE_MODELS = ('data/hd/env/model/act4/diab/act4_diab_props/statue01.model',
                 'data/hd/env/model/act4/diab/act4_diab_props/statue01_head_neutral.model')
STATUE_TEXTURES = tuple(f'data/hd/env/texture/act4/diab/act4_diab_props_statue01_{p}_{m}.texture'
                        for p in 'ab' for m in ('alb', 'nrm', 'orm'))


def lava(preset, act, ids):
    """LAVA: the menu's own lava tile round you, level; LAVA_FALLS: the same stood on end."""
    if act not in LAVA:
        return 0
    import random
    rnd = random.Random(9800 + act)
    name_, reach, (lo, hi) = LAVA[act]
    src = next((e for e in preset['entities'] if str(e.get('name', '')).lower() == name_), None)
    if not src:
        return 0
    cx, cy, cz = CAMERAS[act]
    t0 = transform(src)
    p0 = t0['position']
    q0 = t0.get('orientation') or {'x': 0.0, 'y': 0.0, 'z': 0.0, 'w': 1.0}
    # level: only its turn about the vertical kept (the menu's own too)
    yaw0 = 2.0 * math.atan2(float(q0.get('y', 0.0)), float(q0.get('w', 1.0)))
    t0['orientation'] = {'x': 0.0, 'y': math.sin(yaw0 / 2), 'z': 0.0, 'w': math.cos(yaw0 / 2)}
    x0, z0, x1, z1 = LAVA_EXTENT.get(model_of(src), LAVA_GUESS)
    w, d = x1 - x0, z1 - z0
    c, s_ = math.cos(yaw0), math.sin(yaw0)
    ex, ez = (c * w, -s_ * w), (s_ * d, c * d)   # the tile's own x and z edges, in the world (yaw: x -> (c, 0, -s))
    added = []
    ux, uz = (c, -s_), (s_, c)   # the tile's own x and z, in the world's (x, z)
    # which of its axes runs across (along the world's z, our right) - the one a side cuts
    across_x = abs(ux[1]) >= abs(uz[1])
    n = int(reach / max(min(w, d), 1.0)) + 2
    narrowed = 0
    for i in range(-n, n + 1):
        for j in range(-n, n + 1):
            if i == 0 and j == 0:
                continue
            px, pz = float(p0['x']) + i * ex[0] + j * ez[0], float(p0['z']) + i * ex[1] + j * ez[1]
            # its corners, in our frame (ahead = cx - x, right = cz - z)
            cs = [(px + (x0 + a * w) * ux[0] + (z0 + b * d) * uz[0], pz + (x0 + a * w) * ux[1] + (z0 + b * d) * uz[1])
                  for a in (0.0, 1.0) for b in (0.0, 1.0)]
            aheads, rights = [cx - x for x, _ in cs], [cz - z for _, z in cs]
            r_lo, r_hi = min(rights), max(rights)
            near = math.hypot(max(min(aheads), 0.0, -max(aheads)), max(r_lo, 0.0, -r_hi))
            if near > reach or r_hi <= lo or r_lo >= hi:
                continue
            f0, f1 = 0.0, 1.0   # the share of its across-axis kept, from its own low edge
            u, lo0, span = (ux, x0, w) if across_x else (uz, z0, d)
            if r_lo < lo or r_hi > hi:
                # right along that axis: from its low edge (share 0) to its high edge (share 1)
                r0 = cz - (pz + lo0 * u[1])
                r1 = r0 - span * u[1]
                ta, tb = (lo - r0) / (r1 - r0), (hi - r0) / (r1 - r0)
                f0, f1 = max(0.0, min(ta, tb)), min(1.0, max(ta, tb))
                if f1 - f0 < 0.02:
                    continue
                narrowed += 1
            e = copy.deepcopy(src)
            name = f'pcvr_lava_{act}_{i}_{j}'
            e['name'], e['id'] = name, new_id(rnd, ids)
            for comp in e['components']:
                comp['name'] = f'{name}_{comp.get("type", "c")}'
            t_ = transform(e)
            if f0 > 0.0 or f1 < 1.0:
                k = f1 - f0
                # the narrowed tile's low edge (lo0 * k from its new origin) on the kept part's (lo0 + f0 * span)
                shift = lo0 + f0 * span - lo0 * k
                px, pz = px + shift * u[0], pz + shift * u[1]
                sc = dict(t_.get('scale') or {'x': 1.0, 'y': 1.0, 'z': 1.0})
                key = 'x' if across_x else 'z'
                sc[key] = float(sc.get(key, 1.0)) * k
                t_['scale'] = sc
            t_['position'] = {'x': px, 'y': float(p0['y']), 'z': pz}
            added.append(e)
    tiles_ = len(added)
    for name, ahead, right, width, height, about in LAVA_FALLS.get(act, ()):
        e = copy.deepcopy(src)
        e['name'], e['id'] = name, new_id(rnd, ids)
        for comp in e['components']:
            comp['name'] = f'{name}_{comp.get("type", "c")}'
        t = transform(e)
        fx, fz = cx - ahead, cz - right
        vx, vz = cx - fx, cz - fz   # toward the camera
        if about == 'x':
            # tipped about its own x: its z hangs down from the origin, its face (+y) to its z; width along its x
            th = math.atan2(vx, vz)   # its z -> (sin th, 0, cos th) toward you
            q = qmul((0.0, math.sin(th / 2), 0.0, math.cos(th / 2)), tilt_q(90.0, 0.0))
            sx, sz = width / w, height / d
            ax_, az_ = math.cos(th), -math.sin(th)   # its x (the width) in the world
            pos = (fx - ax_ * width / 2, float(p0['y']) + height, fz - az_ * width / 2)
        else:
            # tipped about its own z: its x stands up from the origin, its face (+y) to its -x; width along its z
            th = math.atan2(vz, -vx)   # its -x -> (-cos th, 0, sin th) toward you
            q = qmul((0.0, math.sin(th / 2), 0.0, math.cos(th / 2)), tilt_q(0.0, 90.0))
            sx, sz = height / w, width / d
            ax_, az_ = math.sin(th), math.cos(th)   # its z (the width) in the world
            pos = (fx - ax_ * width / 2, float(p0['y']), fz - az_ * width / 2)
        t['position'] = {'x': pos[0], 'y': pos[1], 'z': pos[2]}
        t['orientation'] = {'x': q[0], 'y': q[1], 'z': q[2], 'w': q[3]}
        t['scale'] = {'x': sx, 'y': 1.0, 'z': sz}
        added.append(e)
    preset['entities'].extend(added)
    spots, sy, ssc = STATUES.get(act, ((), 0.0, 1.0))
    for k, (right, ahead) in enumerate(spots):
        # its +x to the camera (02:20: with its -x there all three stood with their backs to you)
        th = math.atan2(right, -ahead) + math.pi
        for m in STATUE_MODELS:
            nm = f'pcvr_guardian_{act}_{k}_{m.rsplit("/", 1)[-1][:-len(".model")]}'
            added.append(tree(nm, new_id(rnd, ids), m, cx - ahead, sy, cz - right, th, (ssc, ssc, ssc)))
    if spots:
        preset['entities'].extend(added[-len(spots) * len(STATUE_MODELS):])
        depend(preset, STATUE_MODELS)
        tex = preset.setdefault('dependencies', {}).setdefault('textures', [])
        have = {str(d.get('path', '')).lower() for d in tex if isinstance(d, dict)}
        tex.extend({'path': t} for t in STATUE_TEXTURES if t not in have)
    names, drop = DROP_COPIES.get(act, ((), 0.0))
    dropped = 0
    for e in preset['entities']:
        nm = str(e.get('name', ''))
        if COPY in nm and nm.split(COPY, 1)[0] in names:
            transform(e)['position']['y'] = float(transform(e)['position']['y']) - drop
            dropped += 1
    TOWN_REPORT[act] = (f'{len(spots)} guardians, {dropped} copies brought down {drop}, ' f'{tiles_} lava tiles ({w:.0f} x {d:.0f}; {narrowed} narrowed to the sides {lo:.0f} / {hi:.0f}), '
                        f'{len(added) - tiles_ - len(spots) * len(STATUE_MODELS)} lava falls to try')
    return len(added)


def hide(preset):
    keep = [e for e in preset['entities'] if e.get('name') not in HIDE]
    out = len(preset['entities']) - len(keep)
    preset['entities'] = keep
    return out


# The owner's own edits from the scene editor (tools/scene_editor, 2026-11-14): per act, a file of moves, turns,
# sizes, heights, removals, additions and the road's points, applied over what this script lays.
EDITS_FILES = {a: Path(__file__).resolve().parent / f'frontend_edits_act{a}.json' for a in (2, 3, 4, 5)}


def load_edits(act, override=None):
    f = override or EDITS_FILES.get(act)
    if not f or not Path(f).exists():
        return None
    return json.loads(Path(f).read_text(encoding='utf-8'))


def tilt_q(tilt_x, tilt_z):
    """The editor's tilt (degrees) in the model's own frame: first about its z, then about its x (then its yaw)."""
    ax, az = math.radians(float(tilt_x or 0)), math.radians(float(tilt_z or 0))
    return qmul((math.sin(ax / 2), 0.0, 0.0, math.cos(ax / 2)), (0.0, 0.0, math.sin(az / 2), math.cos(az / 2)))


def apply_edits(preset, act, edits, ids):
    """The editor's moves, turns, sizes, heights, removals and additions (by entity name, in the camera's frame)."""
    if not edits:
        return 0
    import random
    rnd = random.Random(9900 + act)
    cx, cy, cz = CAMERAS[act]
    gone = set(edits.get('deleted') or ())
    preset['entities'] = [e for e in preset['entities'] if e.get('name') not in gone]
    n = len(gone)
    moved = edits.get('moved') or {}
    for e in preset['entities']:
        d = moved.get(e.get('name'))
        t = transform(e) if d else None
        if not t:
            continue
        p = t['position']
        if 'right' in d:
            p['x'], p['z'] = cx - float(d['ahead']), cz - float(d['right'])
        if 'y' in d:
            p['y'] = float(d['y'])
        if 'turn' in d:
            a = math.radians(float(d['turn']))
            q = t.get('orientation') or {'x': 0.0, 'y': 0.0, 'z': 0.0, 'w': 1.0}
            qx, qy, qz, qw = qmul((0.0, math.sin(a / 2), 0.0, math.cos(a / 2)), tuple(float(q.get(k, 0.0)) for k in 'xyzw'))
            t['orientation'] = {'x': qx, 'y': qy, 'z': qz, 'w': qw}
        if d.get('tilt_x') or d.get('tilt_z'):   # tipped over in its own frame (a column laid down)
            q = t.get('orientation') or {'x': 0.0, 'y': 0.0, 'z': 0.0, 'w': 1.0}
            qx, qy, qz, qw = qmul(tuple(float(q.get(k, 0.0)) for k in 'xyzw'), tilt_q(d.get('tilt_x', 0), d.get('tilt_z', 0)))
            t['orientation'] = {'x': qx, 'y': qy, 'z': qz, 'w': qw}
        if 'scale_by' in d:
            sc = t.setdefault('scale', {'x': 1.0, 'y': 1.0, 'z': 1.0})
            for k in 'xyz':
                sc[k] = float(sc.get(k, 1.0)) * float(d['scale_by'])
        n += 1
    for a in edits.get('added') or ():
        sc = float(a.get('scale', 1.0))
        e = tree(a['name'], new_id(rnd, ids), a['model'], cx - float(a['ahead']), float(a.get('y', 0.0)),
                 cz - float(a['right']), math.radians(float(a.get('yaw', 0.0))), (sc, sc, sc))
        if a.get('tilt_x') or a.get('tilt_z'):
            t = transform(e)
            q = t['orientation']
            qx, qy, qz, qw = qmul((q['x'], q['y'], q['z'], q['w']), tilt_q(a.get('tilt_x', 0), a.get('tilt_z', 0)))
            t['orientation'] = {'x': qx, 'y': qy, 'z': qz, 'w': qw}
        preset['entities'].append(e)
        depend(preset, (a['model'],))
        n += 1
    return n


def prepare(preset_bytes, acts=ACTS, sources=None, edits_files=None, assets=None):
    preset = json.loads(preset_bytes)
    if not isinstance(preset, dict) or not isinstance(preset.get('entities'), list):
        raise ValueError('The main menu preset has no entity list; left as it is')
    if assets is not None:   # the true size of the water we lay (TOWN_WATER)
        for e in preset['entities']:
            if e.get('name') in TOWN_WATER.values():
                b = measure(assets, model_of(e))
                if b:
                    WATER_EXTENT[model_of(e)] = (b[0][0], b[0][2], b[1][0], b[1][2])
            if str(e.get('name', '')).lower() in [v[0] for v in LAVA.values()]:   # and of the lava (LAVA)
                b = measure(assets, model_of(e))
                if b:
                    LAVA_EXTENT[model_of(e)] = (b[0][0], b[0][2], b[1][0], b[1][2])
                    print(f'lava tile {model_of(e)}: x {b[0][0]:.1f}..{b[1][0]:.1f}, y {b[0][1]:.2f}..{b[1][1]:.2f}, '
                          f'z {b[0][2]:.1f}..{b[1][2]:.1f}')
    clouds = without_clouds(preset)
    ids = {int(e.get('id', 0)) for e in preset['entities']}
    report = [f'clouds out: {clouds}, hidden: {hide(preset)}']
    edits = {act: load_edits(act, (edits_files or {}).get(act)) for act in acts}
    for act in acts:
        ed = edits.get(act)
        if ed and ed.get('road'):
            ROAD_PATH[act] = tuple(tuple(p) for p in ed['road'])
        if ed and ed.get('road_half'):
            ROAD_NARROW[act] = float(ed['road_half']) / ROAD_HALF
        src, add = fill_round(preset, act, ids)
        trees = forest(preset, act, ids)
        out = clear_fields(preset, act)
        dark = darkwood(preset, act, ids)
        ivy = ivy_out(preset, act)   # before the thickets: their trees are not what the ivy hung from
        thick = thicket(preset, act, ids)
        grasses = grass(preset, act, ids)
        thinned = thin(preset, act)
        finished = finish(preset, act, ids)
        walled = walls(preset, act, ids)
        roads = road(preset, act, ids)
        scattered = scatter(preset, act, ids)
        tiled = tiles(preset, act, ids, sources or {}) + towers(preset, act, ids) + town(preset, act, ids, sources or {}) + \
            lava(preset, act, ids)
        if act in TOWN_REPORT:
            report.append(f'act {act}: {TOWN_REPORT[act]}')
        edited = apply_edits(preset, act, ed, ids)
        if edited:
            report.append(f'act {act}: {edited} of the owner\'s edits applied')
        report.append(f'act {act}: {src} pieces of its own, {add} copies round the camera, {trees} trees '
                      f'({out} trees out of {", ".join(CLEAR.get(act, ()))}), {dark} Dark Wood trees, {thick} in thickets, {thinned} thinned out, {finished} more out, {ivy} ivy out, {grasses} grass, {walled} wall pieces, {roads} road pieces, {scattered} scattered, {tiled} from game strips, '
                      f'{lights(preset, act, ids)} lights')
    return (json.dumps(preset, ensure_ascii=False, separators=(',', ':')) + '\n').encode('utf-8'), report


def source_presets():
    """The game's presets the scene is built from besides the menu's own (TILES, TOWN)."""
    return sorted({t[0] for t in TILES.values()} | {t[0] for t in TOWN.values()})


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('game', type=Path, nargs='?')
    ap.add_argument('--casclib', type=Path, default=None)
    ap.add_argument('--write', action='store_true')
    ap.add_argument('--from-json', type=Path)
    ap.add_argument('--out', type=Path)
    ap.add_argument('--mod', default='D2R-PCVR')
    ap.add_argument('--edits', type=Path, help='the scene editor\'s file of changes for Act 2 (default: frontend_edits_act2.json here, if any)')
    ap.add_argument('--tiles-from', type=Path, help='(development) a folder with the TILES presets, as dump_act2_presets.py writes them')
    args = ap.parse_args()
    wanted = source_presets()
    sources = {}
    if args.from_json:
        raw = args.from_json.read_bytes()
        for w in wanted:
            f = (args.tiles_from or Path('.')) / w.split('/preset/', 1)[-1].split('/', 1)[-1].replace('/', '__')
            if not f.exists():   # (Act 3's town: extracted/act3, beside Act 2's strips in extracted/act2)
                f = f.parent.parent / w.split('/preset/', 1)[-1].split('/', 1)[0] / f.name
            sources[w] = f.read_bytes()
        data, report = prepare(raw, sources=sources, edits_files={2: args.edits} if args.edits else None)
    else:
        sys.path.insert(0, str(Path(__file__).resolve().parent))
        from game_assets import open_assets
        with open_assets(args.game, args.casclib) as assets:
            raw = assets.read(PRESET)
            for w in wanted:
                sources[w] = assets.read(w)
            data, report = prepare(raw, sources=sources, edits_files={2: args.edits} if args.edits else None, assets=assets)
    print('\n'.join(report))
    target = args.out or (args.game / 'mods' / args.mod / f'{args.mod}.mpq' / PRESET if args.game else None)
    if target and (args.write or args.out):
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
        print(f'written: {target}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
