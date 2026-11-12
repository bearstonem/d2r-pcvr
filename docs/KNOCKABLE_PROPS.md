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
VR Settings > Gameplay > Physics > Knock over small props (`[interactions] prop_physics=0`)
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
  friction, bounce and rolling resistance, and its kind and size its mass
  (see [Weight](#weight)). Overlaps resolve at 1 m/s, so nothing pops apart.
  - **Waking.** Before each frame's steps, a weapon capsule swept from its
    last pose to its new one (six samples), passing within 1 cm of a prop,
    sets that prop going. A hand without a weapon capsule (empty, or the off
    hand without dual wielding or a shield) pushes as a 4.5 cm fist from 3 cm
    behind the controller to 8 cm ahead, unless it holds a bottle. So does a bottle or another prop moving faster than
    0.3 m/s and about to reach it, with momentum enough to move it at
    0.1 m/s (a rolling cup leaves a barrel frozen); a prop settling slowly
    does not wake the row it leans on. The prop turns dynamic and the hand
    feels the strike (see [Weight](#weight)).
  - **The limit.** At most 12 move at once. A weapon reaching another prop
    then freezes the one that has moved longest.
  - **Freezing.** A moving prop is frozen again (static, where it is) after
    it has moved under 3 cm/s for 0.6 s, or less than 1.5 cm and 4 degrees
    over half a second. It is also frozen when Box3D puts it to sleep, after
    12 s, or 12 m from the player. One that falls through everything is set
    back on the floor. A prop a weapon is pushing on is not frozen for
    moving slowly (a barrel being tipped over).
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

## Weight

Status: candidate (2026-11-12); automated checks only, no headset test yet.
The owner asked for "weight and heft" on the props weapons can move. Before
this, every weapon was a kinematic capsule: of infinite mass, it pushed
whatever it met aside at its own speed, so a 119 kg barrel left a 5 m/s sword
swing at 6.9 m/s and slid 5 m, like a cup. Now (`steamvr/prop_heft.h`;
VR Settings > Gameplay > Physics > Props have weight, `[interactions]
prop_heft`, on by default, shown while Knock over small props is on, live):

- **Mass by kind and size.** Each prop's mass comes from its catalogue name
  and the size of its bounds. A vessel (cups, jugs, vases, urns, bottles,
  pails, baskets) is its open-topped walls: stoneware 3-15 mm thick by its
  width, glass 2.5-6 mm, sheet iron 1.2-1.5 mm, cast iron 3 mm. A barrel or
  crate is a closed shell of wood with some load inside. Everything else is
  solid through a share of its shape: books, skulls (mostly hollow), food,
  cloth, stone tablets, grain sacks and hay bales, and chairs and stools
  (legs, seat and back, 6% of their box). Stacks of plates or bowls are a
  third solid. A name not listed takes its material's build. Each kind is
  kept within a plausible range. The Box3D body gets the density that makes
  its own shape weigh that. For example: a cup 0.18 kg, a bottle 0.42, a
  plate 0.77, a book 1.1, a pail 2.7, a 50 cm vase 5.1, a stool 2.5, a chair
  9.8, a 60 x 90 cm barrel 48, a 60 cm crate 52, a sack of grain 60, a large
  urn 59.
- **The weapon has mass.** As for the monster bodies (`limb_physics.cpp`),
  each weapon, fist or shield is a dynamic capsule held to a kinematic hand,
  where the controller has it, by a motor joint (30 Hz) of limited strength.
  Its striking mass comes from the weapon's heft (`weapon_heft.h` Weight,
  read whatever Weapon heft is set to): 0.9 kg for a fist, dagger, claw or
  wand, 1.6 for a one-handed sword, 2.9 for a spear or staff, 3.5 for a
  two-handed sword, 5 for a great maul, 3 for a shield. A committed swing has
  the arm and body behind it: the mass grows with the blade's speed, to three
  times at 4 m/s and over (a hard punch then strikes with 2.7 kg, as measured
  punches do). The joint pushes with at most 250 N (a fist) to 500 N (a
  maul), 400 N for a shield. Touching nothing, the blade moves exactly with
  its hand. Touching something, Box3D shares the momentum by the two masses
  and the joint pushes on as strong as it is. Left 20 cm or 0.6 rad behind
  its hand by something it cannot move, it slips: it jumps back to the hand
  and passes through everything for 0.25 s. The blade meets only moving
  things (bottles and props set going). The hand's sweep sets a frozen prop
  going first, so the blade never catches on a floor, wall or surface.
- **Moving things.** A rolling bottle or a knocked prop sets a frozen one
  going only with momentum enough to move it at 0.1 m/s: a cup bounces off a
  barrel and leaves it frozen. A spell sets one going only if its kick moves
  it at 0.05 m/s (an Inferno flame leaves a barrel; spells already kicked by
  mass). A prop a blade is pushing on is not frozen for moving slowly.
- **The hand feels it.** The tick or thud follows the share of the swing's
  speed the prop took from the weapon (`prop_heft.h` Thud, on the Melee hits
  strength). Something under 2 kg is one short tick, a little stronger for a
  faster swing. A heavier thing is a knock (160 Hz, 25 ms) and then a low
  thud (70 Hz) of 40 ms up to 110 ms for 64 kg and more. A full swing into a
  barrel is a 0.83 knock; into a cup 0.15. Short melee taps cuts each step to
  10 ms at 180 Hz.
- **Off.** Weapons are kinematic again, masses are the material densities of
  before, and every strike is the old light tick.

What a weapon does, from `prop-heft-test` (Box3D; a blade 60% of the prop's
height up, through it at 5 m/s, or 4 cm into it and back at 1 m/s; 3 s to
settle):

| Prop | Sword tap | Sword swing | Other |
| --- | --- | --- | --- |
| cup, 0.18 kg | 14 cm, falls over | 4.6 m, 7.4 m/s | weight off: 4.7 m |
| chair, 9.8 kg | 17 cm, falls over | 1.1 m, 3.4 m/s | |
| barrel, 48 kg | stays (0.12 m/s) | rocks 3.4 deg, 8 mm | struck low: slides 4.6 cm; maul: knocked over, 26 cm; fist: stays; weight off: 5.2 m at 6.9 m/s |
| sack, 60 kg | stays | 1 mm | maul: 4 cm; weight off: 3.1 m |

A full swing into the barrel or sack stops the blade, which slips through
once; through a cup it never does. The cost, twelve cups moving and three
weapons sweeping them, was the same with weight on and off (45 us a frame
for the world's 120 Hz steps, the test machine).

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
- Masses are by kind name and bounds, not by the model's real shape: a model
  whose bounds include empty space (an open book, a figurine in a box) is
  given a share of them. The kinds' real sizes in the game were not surveyed;
  each kind's range keeps an odd one plausible.
- The drawn weapon is never slowed by a heavy prop: only its body in the
  physics is, and the hand feels the thud. Slowing the drawn hand belongs to
  the held-weapon collision work. Props cannot be picked up or carried.

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

`prop-heft-test`:
- masses by kind and size: each of 17 kinds within what such a thing weighs,
  light to heavy in order, a vessel growing with its surface and a solid with
  its volume, families by name, unknown kinds by material, unreadable sizes;
- the shapes' volumes as the world builds them;
- striking mass and strength by heft, the share of a strike a prop takes and
  the weapon loses, what a moving thing must carry to wake a frozen prop;
- the hand's tick and thud: the old tick with weight off, a light tick for a
  cup, a knock and a longer thud for heavier props, weaker for a tap, short
  melee taps;
- in the real Box3D world, the table above as assertions: a tap and a full
  swing on a cup, chair, barrel and sack, a fist and a maul, struck low, and
  the same with weight off;
- the bodies' masses as built, and the switch changing them live;
- a rolled bottle bouncing off a frozen barrel, and a light spell bolt
  leaving a barrel but setting a cup going (both moving them with weight off);
- the cost, printed for weight on and off.

`dropped-bottles-test` runs with weight on. Two cases were changed for it:
the 13th prop is now behind the knocked row rather than in its cups' flight
line, and the burst's two cups weigh half a kilo, so neither kick reaches the
5 m/s limit and the nearer one goes faster.

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
per weapon knock, with its mass (`Frame props: knocked a barrel (right hand,
4.2 m/s, 47.7 kg)`). `Frame props: ten seconds: N weapon slips through heavy
props` counts blades left behind.

Weight headset check (offline character, weight on): find a decorative
barrel, sack or large urn and a table with cups. Breakable barrels and urns
(game objects that drop loot) are not knockable props: the log line
`Frame props: knocked a barrel (..., 47.7 kg)` confirms a prop. Tap the barrel with a sword
held still in the hand: it should not move, and the hand should feel a firm
knock. Swing through it hard at mid height: it rocks or shifts a little and
the hand feels a hard knock and a low thud. Swing low, near its foot: it
slides a few centimetres. With a two-handed weapon, swing high: it should
tip or fall. A cup tapped or swung at still flies, with a light tick. A
chair is shoved and knocked over but does not fly like a cup. Then switch
Props have weight off in VR Settings and swing at the barrel again: it flies
like a cup, as before.
