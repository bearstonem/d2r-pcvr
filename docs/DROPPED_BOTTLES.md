# Dropped empty bottles (Box3D)

Status: first physics prototype; the owner verified it working in the headset
(2026-10-22, offline character, isolated developer build). The weapon hits
below were verified the same day ("that works great").

After drinking, opening the hand lets the empty bottle fall into the world.
It keeps the hand's velocity and turn, bounces, rolls and stops at the game's
walls. Previously it vanished on release. This is purely visual: no native
request, item or game state is involved. VR Settings > World > Roll and break
bottles (`[interactions] bottle_physics=0`) restores the old behavior, live,
for dropped bottles and thrown potions alike.

## Pieces

- **Release** (`steamvr/interactions.h`, `Engine::Discard`). When the grip
  opens on a confirmed-empty bottle, the engine reports one `Output::discard`.
  It carries the held model's world pose (the same `Stage::World` mapping
  thrown potions use), the hand velocity and the floor under the player. The
  velocity is the peak in the last 0.12 s, capped at 20 m/s, plus the player's
  own world velocity when running or walking (`interaction::Carry`: the stage
  anchor's motion smoothed over 0.1 s, reset by jumps faster than 10 m/s or
  gaps over 0.25 s). A bottle let go of mid-run moves on with you rather than
  dropping straight down. Turn is the mean
  hand angular velocity over that window, from pose differences, capped at
  30 rad/s. A full bottle released still returns to the belt.
- **Physics** (`steamvr/dropped_bottles.*`). [Box3D](https://github.com/erincatto/box3d)
  0.1.0, MIT, pinned by `setup_windows_tools.ps1` into `deps/box3d` and
  compiled into the plugin unmodified. One world with `workerCount=1`, so
  Box3D starts no threads in the game process. It steps at a fixed 120 Hz with
  four substeps, up to eight steps per frame, and renders an interpolation of
  the last two steps. Both eyes therefore draw the same pose. This avoids the
  per-eye stepping problem in [hanging fixtures](HANGING_FIXTURE_PHYSICS.md).
  Each bottle is a 24-sided cylinder hull, 18 cm tall, whose radius is the
  loaded model's widest horizontal extent. Simulation runs near a local
  origin so float precision does not depend on map coordinates.
- **Floor.** The world's single static ground box has its top at the floor
  under the player at release. Thrown potions use the same flat floor. A
  release on a different floor, in another game, or more than 20 m away
  restarts the world.
- **Walls** (`vr/wall_cells.*`). While an empty bottle is held or bottles lie
  in the world, the XR thread asks for a 32 x 32 cell square around them. The game
  thread samples it with the same room lookup and `0x804` mask arrows use.
  Closed or unloaded rooms count as walls. A new sample is taken every 0.5 s,
  or sooner when the point moves six cells. Blocked cells merge into
  rectangles and become 6 m static boxes standing on the floor. Only the
  floor's shear is removed, as for arrows. Unchanged cells rebuild nothing;
  opened doors remove their box. With unknown signatures the sampler logs
  once and bottles keep the floor only.
- **Weapons.** While physical melee is active, the render side's posed
  melee capsules (each hand's weapon or fist, and the shield) reach the
  transport through `BottleWeapon`. Each becomes a kinematic Box3D capsule.
  Before each frame's steps, `b3Body_SetTargetTransform` moves it to its new
  pose over exactly those steps and wakes the bottles it touches. Afterwards
  it stops, so a pose that stops arriving (older than 100 ms) never coasts.
  A new or resized weapon, or a move over 0.75 m or faster than 30 m/s (a
  warp or recentre), is placed without sweeping. Radii are clamped to
  2-8 cm, which thins the shield's 15 cm creature capsule. Box3D hit events
  report the approach speed; above 1 m/s that hand gets a short Melee-category
  tick, at most every 80 ms. The weapons exist only while bottles or
  [knockable props](KNOCKABLE_PROPS.md) are in the world. Those props
  share this world, so bottles and props knock each other too.
- **Thrown potions.** A Health, Rejuvenation, Antidote or Thawing bottle
  thrown toward the hireling flies in this world too while bottle physics is
  on. The engine hands the release over (`Output::toss`, an id with the same
  pose, velocity and spin as a discard). `World::Toss` makes it a body like
  the dropped ones, with contact events on, which weapons never touch. Its
  first begin-touch with anything (the floor, a wall, a support plate or a
  prop) lands it there: the body goes, and the engine sends the hireling
  feed request with that point. While it flies, the engine draws the
  bottle's body pose. If no body follows within 0.2 s (the world was busy
  with moving props on another floor), the throw flies the old ballistic arc.
  A body that touches nothing lands where it is after 3 s.
- **Thrown traps and bombs.** While an Assassin's trap or bomb is in the
  hand, the world is readied around the player (`World::Prepare`, no body)
  and asks for walls. On release, `World::Trace` sweeps a 6 cm ball along the
  throw's arc in 1/60 s pieces. The arc carries the player's running speed.
  The ball meets walls, support plates, props and bottles, never weapons. The
  native cast is aimed at the floor cell under the first hit; with nothing
  hit (or Bottle physics off) it lands where the arc meets the floor, as
  before.
- **Leaving the hand.** For 0.3 s after release, a dropped bottle passes
  through weapons and fists, so the hand that let go does not shove it.
- **Drawing.** `potions::AddDropped` puts up to six empty-looking models into
  the potion scene, mapped back to room space through the current stage
  frame. The scene grew by six entries.

## Limits

- The renderer uses its own depth buffer. Bottles draw over monsters and over
  walls they have rolled behind; the belt and thrown bottles have the same
  limit.
- The floor is flat. Stairs, slopes and raised ground are not followed.
- Monsters and the player's body do not collide with bottles. Weapons only
  push them while physical melee is active (not with a bow, menus or a held
  potion), and the left hand only when dual wielding or holding a shield.
  The weapon capsules are the melee categories' sizes, not the meshes.
- At most six bottles; each is removed after 20 s or 12 m from the player.
  Menus and loading pause them in place.

## Verification

`dropped-bottles-test` runs the real Box3D world. It covers wall merging,
cell mapping at several floor heights, release validation, a dropped bottle
bouncing and settling on the floor, a throw stopping at a wall and coming back,
unchanged/foreign/opened wall patches, the six-bottle cap, floor change,
range, lifetime and pause handling. Weapon cases: a blade swept through a
sleeping bottle pushes it ahead and reports one hit; putting the weapon away
removes it; a 4 m warp passes a bottle untouched; a weapon whose pose stops
changing stays put; malformed capsules are refused. `interaction-test` covers the release
hand-off: world pose, velocity and spin through a turned stage frame, a still
release, no frame, and a full bottle that is never discarded.

Headset check (offline character): drink a potion, then drop the bottle at
your feet, toss it gently, and throw it at a nearby wall. Watch both eyes
while walking and turning. Check the log for `Frame dropped bottles:` release
lines, `native wall cells ready`, and wall sample counts. Frame time, Battle.net
and other game builds are unverified.
