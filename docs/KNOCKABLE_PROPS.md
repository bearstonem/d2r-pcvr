# Knockable level props (Box3D)

Status: owner verified in the headset (2026-10-22, offline character,
isolated developer build), on the third version. The headset tests:
1. The first version worked but not for everything. Books on a shelf woke
   each other without end, which used up the moving-prop limit, and props
   inside buildings were missing from the catalog.
2. The second was "much better". Some objects still did not react, and
   books on a mantle slid far over an invisible surface before falling. The
   mantle is part of the building, and the building's bounds became a 1.5 m
   plate. Every knock came from the right hand, because the off hand had no
   collider without dual wielding or a shield, and the nearest-64 limit was
   full.
3. The third, with guessed and joined ledges, props on props, bare-hand
   fists and the 160 nearest, verified.

The level's own small props near the player (cups, plates, bowls, vases,
jugs, bottles, books, skulls, baskets, pails, stools, chairs, barrels, sacks,
cloths and similar) can be knocked about
with swords, other melee weapons, fists, the shield, or a dropped bottle.
A prop stays exactly where the game placed it until something reaches it. It
then falls, bounces and rolls in the same Box3D world as the
[dropped bottles](DROPPED_BOTTLES.md), and freezes again where it comes to
rest. The game draws its own model there, shadow included. This is purely
visual: no native request, item, collision or game state is involved, and
everything returns to its place when the area loads again.
VR Settings > Video > Knockable props (`[interactions] prop_physics=0`)
turns it off, live.

## Pieces

- **Catalog** (`steamvr/prepare_props.py`, at install). Every HD level preset
  (`data/hd/env/preset/**.json`) lists its entities with an `id`. The
  installer reads the player's own game files and keeps each entity that is
  only a transform and a model, whose model name is one of 101 kinds. Nothing
  that lights, emits or animates is kept. Props that fade with the walls
  (most props inside buildings) are kept, and so are props with a static
  native body (many vases, pails and chairs): only the HD renderer's debris
  and ragdolls touch that body, and it stays where it was. The installer
  writes the id, the preset position and the kind to
  `d2r_vr_props/props.bin` beside the plugin. That is about 28,000 props
  from 2,100 presets in roughly 14 seconds on Steam 3.3.93787. The catalog
  is cached by game build and never shipped.
  Candles, lanterns and lamps are left out because their flames are separate
  entities.
- **Scan** (`vr/prop_scan.cpp`, a worker thread, twice a second). The HD
  renderer keeps its world in an entt registry. Pool numbers are runtime type
  numbers, so the three storages that matter are recognised by their contents:
  - the 0xd0-byte transforms: local matrix, world matrix, the previous frame's
    world matrix, and the parent;
  - the 4-byte preset ids, whose values the catalog knows at those entities'
    local positions;
  - the 0x60-byte bounds, whose world box is the local box under the world
    matrix.

  Entities within 3 m of the player and between 0.5 m below and 2.5 m above
  the floor become props when their id and their local position both match
  the catalog. The prop must be between 3 mm and 1.2 m in size, have a rigid
  matrix (no mirroring or shear), and belong to a level preset. At most the
  160 nearest are kept. All reads are guarded copies; nothing is called in
  the game. The ten-second log line counts the catalogued props in reach that
  were left out, and why.
- **What a prop rests on.** Tables, shelves and counters mostly have no
  native collision.
  - **Another prop.** A prop whose base sits on another prop's top (a cup on
    a plate, anything on a barrel) is held by that prop's own body. Nothing
    is added, so it falls once the one under it is knocked away.
  - **Furniture.** Otherwise, among the level entities' bounds around the
    prop (excluding props), the smallest footprint under its centre whose
    height range holds the prop's base is its support. The surface is that
    entity's top, or the prop's own base where that is lower (a shelf inside
    a cabinet, or a prop sunk a little into the top), so it never pops the
    prop up. It is a 2 cm static plate over that footprint, reaching at most
    1.5 m either side of the prop.
  - **Rooms and buildings.** Bounds wider than 1.6 m both ways are a room or
    a building, and what the prop stands on inside them (a mantle, a ledge)
    is unknown. So is nothing found at all. The plate is then guessed: 3 cm
    past the prop's own footprint, at its base. Guessed plates at the same
    height within 30 cm of each other join into one plate over the row, so
    books along a mantle fall at the row's edge.
  - **The floor.** A prop on the floor rests on the bottles' ground.
- **Physics** (`steamvr/dropped_bottles.*`). Each prop is a static Box3D body
  in the shape of its model's bounds, 2 mm (at most 5%) inside them so
  props packed together do not start overlapping. Vessels and plates are
  upright cylinders. Books, skulls, tools, pillows, cloths, sacks and
  furniture are boxes, and apples and potatoes are spheres. Its material sets
  density, friction, bounce and rolling resistance; chairs and stools count
  as mostly air. Overlaps resolve at 1 m/s, so nothing pops apart.
  - **Waking.** Before each frame's steps, a weapon capsule swept from its
    last pose to its new one (six samples), passing within 1 cm of a prop,
    sets that prop going. A hand without a weapon capsule (empty, or the off
    hand without dual wielding or a shield) pushes as a 4.5 cm fist from 3 cm
    behind the controller to 8 cm ahead, unless it holds a bottle. So does a bottle or another prop moving faster than
    0.3 m/s and about to reach it; a prop settling slowly does not wake the
    row it leans on. The prop turns dynamic and the hand gets the usual light
    tick.
  - **The limit.** At most 12 move at once. A weapon reaching another prop
    then freezes the one that has moved longest.
  - **Freezing.** A moving prop is frozen again (static, where it is) after
    it has moved under 3 cm/s for 0.6 s, or less than 1.5 cm and 4 degrees
    over half a second. It is also frozen when Box3D puts it to sleep, after
    12 s, or 12 m from the player. One that falls through everything is set
    back on the floor.
  - **Restarts.** A new floor, game or scale restarts the world only once no
    prop is moving.
- **Drawing.** While a prop moves, the transport sends its interpolated pose
  as a renderer matrix, composed with the prop's own scale and its own units
  per metre. Inside the renderer's transform pass (the existing
  `UpdateTransform` hook, once per stereo pair), each matrix is written over
  the cached world matrix. The old one is copied into the previous-frame slot,
  as the game does, so motion vectors stay right. The world bounds are moved
  too, so culling follows. Before every write the entity must still be at
  that index, with the same parent, the same preset position and the matrix
  last put there. Otherwise it is dropped and the physics forgets it. One
  frame after the last move, the previous-frame slot is set to the final pose.

## Limits

- Supports are bounding boxes. A shelf's back and sides are not there, so a
  prop pushed backwards off a shelf falls through the cabinet. Props under a
  table can fall through its top. Sloped or uneven surfaces are flat.
- A guessed surface ends 3 cm past the props on it. A lone prop on a deep
  built-in shelf, or on a raised floor inside a building the player is not
  standing on, falls sooner than it should. The game's models (real
  surfaces) would take minutes to read at install in Python.
- The player's body and monsters do not touch props. Weapons only do while
  physical melee is active, as for bottles.
- Props that the game builds from several entities move only the catalogued
  part. Props with native collision (crates and sacks that the game itself
  collides with) are not catalogued.
- A moving prop left behind on a reset (headset focus lost, leaving the
  game) stays where it was until the area loads again.
- Steam 3.3.93787 layouts; another game build needs the registry address to
  be found again, and the scan logs why it finds nothing.

## Verification

`props-test`:
- the catalog parser (matching by id and position, malformed files);
- the plank's real matrices and bounds from the developer probe,
  decomposed, composed back and bounded as the renderer does;
- support selection: floor, table top, a shelf inside a cabinet, a long
  counter cut to reach, a mantle inside a building (guessed), nothing found,
  scaled units;
- guessed planes joining along a mantle but not across shelves or gaps;
- a cup resting on a plate;
- capsule reach, straight through, grazing, beside, over, inside and turned.

`dropped-bottles-test`, in the real Box3D world:
- a cup near a table's edge stays put until a blade sweeps through it, then
  falls off the edge, lands, rests and is reported once;
- a later scan keeps it, and lost and out-of-reach props go;
- a rolled bottle sets a floor cup going;
- a lost moving prop goes at once;
- another floor waits for moving props;
- invalid props are refused;
- no more than 12 move at once;
- a row of eight touching books on a shelf, one of them knocked, all settle
  with each knocked at most once (without the fixes they never settle);
- with 12 moving, a weapon still sets a 13th going;
- a cup on a plate gets no plane of its own, and falls to the floor once the
  plate is gone.

`prepare_props_test.py` covers kinds, plain-entity filtering, encoding and the
cache.

Headset check (offline character): stand by a table in the Lut Gholein
tavern. Tap a cup with a sword, swing through a plate stack, block a bowl
with the shield and punch a vase. Knock a cup off the table edge, then walk
away and back. Check that the props and their shadows move, that nothing
smears in DLSS, and that leaving and re-entering the area puts everything
back. The log has `Frame props: catalog of ...` at start, then
`Frame props: ten seconds: ...` (props in reach, the storages found, knocks
by weapons and by moving things, writes) and `Frame props: knocked a ...`
per weapon knock.
