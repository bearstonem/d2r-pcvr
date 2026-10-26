# Hand-wheel quick use

Candidate, headset testing pending. See [controls](../CONTROLS.md).

## Interaction

A separate Quick Use box on both spell/equipment wheels shares an item-type
assignment. A short trigger press uses on release; a 0.65 s hold opens an
8-row paginated inventory picker. Empty-slot clicks also edit. Tracking loss,
session changes, hand changes, a lost pointer and replaced/empty items cancel
use. The slot remembers `[wheel] quick_use_item` (decimal native four-character
item code) in the active mod's INI. No installer defaults or existing bindings
are changed. The setting is shared across characters using that mod profile.

The picker includes only the local player's carried inventory items accepted
by the native usable-item flag. It groups by code, totals charges/counts and
prefers a nonempty tome. Restocking retains the assignment. Stash, cube, cursor,
equipment and belt items are excluded. Native restrictions still apply.

RUN / WALK and the wheel's movement slider are gone: walking and run speed live
in VR Settings > VR > Movement speed (`[gameplay] run_speed_pct`,
`steamvr/run_speed.h`): Walk (-10), Base run (0), Run 10..90%, Full run (100).
Walk walks and every other stop runs; the game's own run / walk command 0x23 is
sent when its toggle is set the other way (`runspeed::GaitSync`: a run seen means
"on run", only a full-stick walk for 0.6 s with stamina left means "on walk").
On a run stop only the local hero's run-mode path velocity is scaled (setter
0x342AE0, client and server alike): base = native x 150 / stat 67. The minimap's GPU blit draws a narrow
bronze frame and fits the whole map inside it, with no new composition layer.

## Native adapter

UI-thread inventory reads use D2RLoader services, never opaque handles cast to
pointers. A bounded, checked client inventory walk resolves the current item's
runtime ID/class; the native code and usable flag must match. Entry and layout
fingerprints gate the adapter on the audited 3.3.93787 code path. The same layout
is used by the supported loader mapping; new builds fail closed on mismatch.

InventoryGrid's native handler 0x161f20(item, 3) owns consumption, restrictions,
targeting and packets. No direct item deletion, charge editing or fabricated
portal is used. The request revalidates ownership and the exact current item
before dispatch. Identify first opens inventory (native command 0x01), then
resolves the item again because opening a panel can pump callbacks. Dispatch is
reported separately from confirmed consumption; there is no automatic retry.
The interaction snapshot lock is released before invoking these native actions.

## Checks and headset route

Automated fixtures exercise short-click/hold separation, cancellation, clear,
run/walk debounce, paint invalidation, actual native-adapter dispatch, persisted
assignments, empty tomes, identify routing, corrupt lists and code mismatch.
GPU fixtures cover map framing and preservation of normal panel conversion.
The generated wheel and picker preview was inspected with locally extracted
icons; no game artwork or caches are committed.

Pending offline headset checks: assign/use/refill a Town Portal tome; assign
and target Identify; try a carried consumable, empty tome and out-of-stock
assignment; hold without consuming; clear/reassign; reopen/restart; move the
pointer away; switch hands; toggle walking/running; inspect map corners/markers.
Native tests do not prove that a particular game session accepts item use.
