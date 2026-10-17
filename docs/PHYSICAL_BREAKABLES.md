# Physical pots and barrels

Swing a held melee weapon into a breakable pot, urn, basket or barrel to smash
it. Use the same deliberate movement and Combat thresholds as physical melee,
with the weapon's trigger released. Either supported Barbarian weapon can
break containers. Existing button interaction, including the native kick,
remains available.

This works in both melee modes. Container strikes have separate bookkeeping
from creature hits, recovery and cleave. Ordinary chests, doors, shrines, racks,
portals, corpses and special quest containers still use their normal controls.
Shields do not use this new weapon-contact path.

## Native dispatch

The existing melee engine supplies validated swept weapon contacts. On the
owning game thread, a bounded object scan collects nearby IDs; each is resolved
again before use. Only loaded, neutral, selectable and targetable objects with
`IsAttackable0` and native operations 3, 5 or 7 qualify. Contact uses the native
footprint and an approximate vertical capsule, with the same rendered-world
coordinate conversion as creature melee. A native interaction line test rejects
intervening walls while excluding the container's own collision footprint.

The game's object-operation adapter (`592C40` in supported build 3.3.93787)
owns animation, loot, traps, explosions and collision changes. This path does
not create items or apply replacement damage. IDs are collected before native
operations because exploding barrels can change the object list. Each object
is attempted once per hand/stroke; already broken objects are rejected, and
processing stops if a trap kills the player.

Barrels normally request a player kick before breaking. The player-mode hook
at `42D020` skips only the exact barrel kick call returning to `58E942`, inside
a thread-local scope matching our physical strike's game, player and object.
Ordinary button kicks, other callers, players and threads retain native mode
changes. No permanent skill, equipment, configuration or save changes occur.

Small code fingerprints guard native functions, call sites and field layouts;
unexpected code disables this feature. The legacy
[D2MOO object operations](https://github.com/ThePhrozenKeep/D2MOO/blob/master/source/D2Game/src/OBJECTS/ObjMode.cpp)
helped identify the flow. Current addresses, argument layout and object fields
were checked against the owner's supported-build code snapshot and local game
tables. No snapshot or extracted game assets are distributed.

## Validation

`breakables-test` exercises the actual adapter against synthetic units and
native functions: eligible/excluded objects, walls, missed contacts, stale
input, both weapon hands, repeat prevention, refused operations, trap death,
object-list changes, signature failure, kick forwarding and thread isolation.
Existing melee tests cover movement thresholds and cancellation.

Headset verification is pending. On an offline developer character, test low
pots and tall barrels with deliberate swings, either Barbarian weapon, and
the existing button kick. Check loot, explosions, movement during strikes and
nearby chests/doors. The capsule height is an approximation rather than mesh
collision and may need adjustment for particular models or uneven floors.

Logs report `Frame breakables: physical pots/barrels ready`, then per-hand
broken counts, blocked lines and refused native operations. A ready message
alone is not gameplay verification.
