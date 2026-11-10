# Spells that reach objects

Status: built and covered by fixtures; **not yet tested in a headset**.

Your own spell missiles now reach what your weapons reach. They break the
game's breakable containers (pots, urns, baskets, barrels and exploding
barrels), and they push the level's small props and dropped bottles out of
their way. `[interactions] spell_impacts=0` in `d2r_pcvr.ini` turns all of it
off (there is no VR Settings row yet); it is read with the other settings, so a
settings Apply picks a change up.

In the game itself, missiles pass through barrels. Arrows breaking them was
already this mod's choice; spells now follow it.

## What each spell does

| Spell (missile) | Containers | Props and bottles |
| --- | --- | --- |
| Fire Bolt (58), Ice Bolt (59), Teeth (114), Lightning (98), Chain Lightning (93), Charged Bolt (56), Twister (477), Shock Wave (511), War Cry (235), Frozen Orb's bolts (261) | broken where the missile passes | a light push along its flight |
| Fire Ball (62) | broken where it passes, and within 2.5 cells of where it bursts | a hard push along its flight, and outward from the burst |
| Glacial Spike (96) | broken where it passes, and within 3 cells of where it ends | a hard push, and outward from where it ends |
| Ice Blast (91), Bone Spear (192), Blessed Hammer (92) | broken where the missile passes | a hard push along its flight |
| Nova (90), Frost Nova (119) | broken as the ring crosses them | a light push outward, as each ring missile passes |
| Poison Nova (118), Inferno (60, 61, 575), Arctic Blast (459, 460), Firestorm (457) | broken where a missile passes | a faint push from each of the many missiles |
| Meteor (101) | broken within 4 cells of where it lands | outward from where it lands |

Holy Bolt is left out, as it is for arrows' container test: it heals.
Arrows, bolts, javelins and thrown potions are unchanged.

### Containers

- A container is opened by the game's own operation, the one a weapon or an
  arrow uses (`breakables::OperateMissile`, beside `OperateArrow`): the same
  loot, and an exploding barrel explodes and hurts as it does when you hit it.
  There is no barrel kick and no pulse in the hand.
- **Cast from the hand** (Fire Bolt, Fire Ball, Ice Bolt, Ice Blast, Bone
  Spear, and Lightning once it flies from the hand): the missile meets the
  container in three dimensions and ends there, as an arrow does. This is the
  arrows' own test in `vr/bow.cpp`, which hand-cast spells have shared since
  arrows learned to break containers; the switch now governs it for spells.
  Aim over a pot and the bolt passes over it.
- **Flown by the game** (Charged Bolt, Nova, Frost Nova, Glacial Spike, Teeth
  and every other spell cast the game's own way): the missile is followed on
  the game's flat grid and breaks the containers within half a cell of its
  path. It is not used up: it flies on, as the game has it fly through barrels.
- **Bursts** reach every container around the point where the missile ended,
  whatever ended it (a monster, a wall, a container, its range). A burst is
  not stopped by a wall between it and a pot.
- At most eight containers per missile step. A container the game refuses is
  not tried again for a third of a second.

### Props and bottles

- Each step of a missile, and each burst, is posted from the game thread to
  the physics world that already holds the [knockable props](KNOCKABLE_PROPS.md)
  and the [dropped bottles](DROPPED_BOTTLES.md) (`bottles::World::Push`).
  There is no second physics world. The props switch (Knock over small props)
  and the bottles switch (Roll and break bottles) still decide what is in it.
- A frozen prop in the missile's way is set going, as by a weapon, and takes
  an impulse along the flight with a little lift, so it tips and tumbles. A
  burst pushes outward, hardest at its centre and a quarter as hard at its
  edge. The impulse is the same for every prop, so a cup flies (about 2 m/s
  from a bolt, 4 m/s from a Fire Ball) and a stool barely shifts. Nothing
  leaves faster than 5 m/s.
- A hand-cast missile pushes what is within 12 cm of its real path (20 cm
  for the heavy ones), at its real height. A missile the game flies has no
  height, so it pushes whatever stands within 30 cm of its path on the floor
  plan, on the floor or on a table.
- A spell never freezes a moving prop to make room: with 12 moving, more are
  left where they are. The nearest to the path or the burst go first.
- Props are only known within 3 m of you, and only while motion combat is on
  (the existing scan). A Fire Ball bursting across the room moves nothing
  there. Bottles are pushed anywhere they lie.

## Pieces

- `steamvr/spell_impacts.h`: the spell table, the grid-plane tests, the
  followed-missile table, the mailbox between the threads and the push
  arithmetic. Plain data and arithmetic; `spell-impacts-test`.
- `vr/spell_impacts.cpp` (`impactvr`): the game thread. No game code is newly
  hooked. The server's missile creation hook (`vr/projectiles.cpp`) enters
  your own listed missiles; the missile update hook (`vr/bow.cpp`, the one that
  flies arrows) steps them, breaks containers and posts passages. Only
  missiles the game steps with its standard server function are listed, since
  that function is the hook.
- `vr/breakables.cpp`: `Nearby` lists containers around the hero (a few times
  a second while a spell is in flight) and `OperateMissile` opens one.
- `steamvr/dropped_bottles.cpp`: `World::Push`.
- `steamvr/transport.cpp`: `PushSpells` takes the passages each frame.

## Not built

- Bone Spirit, Frozen Orb's orb, Tornado, Molten Boulder, Fissure, Volcano,
  Blaze, Fire Wall, Hydra, Blizzard and Meteor's fire are moved by other
  server functions, which this build does not follow. Corpse Explosion and
  Static Field are not missiles.
- Monsters' limbs (Pushable bodies) are not pushed by spells.
- Explosive arrows and potions do not burst containers or props around them.
- Minions', mercenaries' and traps' missiles do nothing here: only your own.

## The log

- `Frame spell impact: Fire Ball (missile 62) passed container 41 at cell
  5012,4873: broken` (or `burst at`, or `the game refused ...`), at most six
  lines a second.
- `Frame spell impact: Fire Bolt (missile 58) flew through and pushed 2 props
  and 0 bottles at cell ...` (or `passed and pushed` for a missile the game
  flies, `burst and pushed` for a burst), at most four lines a second.
- `Frame spell impact: ten seconds: spell missiles followed=..., containers
  broken in their path=..., by bursts=..., native refused=..., bursts=...,
  passages posted for props and bottles=...`.
- `Frame spell impact: off ...` and `... on` when the INI switch changes.

## Headset check (offline character)

1. Sorceress in the Den of Evil or the Cold Plains: cast Fire Bolt and Ice
   Bolt from the hand at pots and barrels, then Fire Ball beside a group of
   them. Each bolt breaks the one it hits; the Fire Ball's burst breaks those
   around it. Aim over a pot: the bolt passes.
2. Charged Bolt, Nova and Frost Nova beside pots: they break as the bolts
   and the ring cross them, and the missiles fly on.
3. An exploding barrel by Fire Ball from a distance, then by Nova standing
   beside one: it explodes and hurts as when a weapon breaks it.
4. Lut Gholein tavern, standing at a table: Fire Bolt through the cups, a
   Fire Ball at the table, Frost Nova beside it. Cups and plates leave along
   the bolt and outward from the burst; drop a bottle and cast past it.
5. A necromancer's Teeth and Bone Spear, and a monster standing among pots:
   the monster is hit as before.
6. Set `[interactions] spell_impacts=0`, Apply: spells pass pots and props
   again, arrows still break pots.
