# Physical shield blocking

In Unleashed, a raised physical guard can block while standing, walking or
running. Classic keeps the native block rules. The native block/dodge function
still runs, and its eligibility checks remain in place.

## Coverage

Raise the shield and place its broad face toward the attacker. For recognized
shield meshes, coverage follows the fully rendered item, including its animated
bones and native attachment overrides. Looking away while keeping the shield
in place does not turn its blocking direction. Both faces can block; a nearly
edge-on or horizontally flattened shield cannot intercept a horizontal attack.
Lowering the visible shield below the torso opens the guard.

The incoming native check supplies the attacker position, not an exact 3D hit
point. Coverage is therefore a forgiving horizontal approximation. The actual
bone-binding bounding box supplies the shield face and dimensions. Its projected
width plus a 12 cm torso allowance determines coverage around the incoming line.
Wrist rotation narrows coverage continuously; only nearly edge-on faces (within
about 9 degrees of edge-on) are rejected outright. The old 60-degree cutoff
could reject a tilted large shield which still covered the player.
The face centre may sit up to 20 cm behind the body line to accommodate a guard
held against the chest. These are rectangular bounds, not per-triangle collision
or the exact flight/height of an incoming projectile. For meshes with several
bindings, the largest valid visible face is used. Unsupported meshes retain the
attachment approximation (60 degrees, 45 cm lateral allowance).
Both paths evaluate facing against the enemy-to-body direction. The fallback
previously aimed that direction at the shield grip instead, so moving the grip
could change the angle verdict without rotating the shield. Inward/outward
movement still must pass the same placement and height limits; there is no
minimum arm-extension requirement.

Height uses the visible surface overlapping the torso band relative to the
same-pose eyes, allowing a lower grip on a tall shield. Horizontal hand reach
is radial. Surface position, eye position, scale and hand input use the matching
completed hero pose, after body calibration. The native transform callback
then applies the held item's own current bone and world transform. The thin
axis of its bounds determines the face, independently of the wrist axes.
Source timestamps expire old poses instead of refreshing them when
the UI polls again. Snap-turn epochs invalidate coverage until the new pose
arrives; tracking loss opens immediately. Existing calibration is not changed.
The captured item must belong to the exact local hero entity and match its
session, equipment and generation. Dropped shields, inventory previews and
other actors cannot provide coverage. Hidden/invalid/implausibly scaled geometry
is rejected. No extracted assets or caches are required.

Parries retain their diagonal motion and timing requirements. Guard strain only
changes the visor effect; it does not turn blocking off. Shield bashes have a
separate motion/skill path.

## Investigation and verification limits

The original owner session recorded 13 blocks, 34 open outcomes and one native
check that did not reach the block roll. Eleven otherwise valid guard decisions
were explicitly changed to open by the walking/running restriction, now removed.

In the follow-up headset test the owner reported much better blocking with both
shields, but more apparent misses with the large shield. All 22 rejected guard
decisions were outside the old headset-facing cone, with the hand raised and a
current pose. The large-shield section also had nine native checks which never
reached the block roll. These are not proven landed hits: native flags, block
eligibility or other handling can skip that roll. The owner subsequently tested surface coverage and still reported many misses.
In that run, 44 decisions contained 22 covered guards, 16 surface-angle
rejections, three hand-height rejections and three placement rejections. Of the
22 covered guards, 17 reached the native roll and blocked. The other five, plus
six open guards, had native `block=0`; all eleven came from scarab class 91.
The audited missile caller only enables shielding for nonzero physical damage,
so scarab lightning is a strong lead, not a confirmed per-hit classification.
The trace now records the caller RVA to distinguish this path. Do not bypass
native unblockable flags or interpret every OPEN verdict as a landed hit.

A separate confirmed adapter bug spent pending parries even when native handling
skipped the block roll. Consumption now waits for the native roll. This does
not change which damage types can be blocked. Remaining visual angle/placement
disagreements led to the rendered-bounds correction described above. It passes
synthetic tests but still needs owner headset verification; calibration has not
been demonstrated to cause the failures.

The owner subsequently verified the debug overlay and reported that apparent
misses during Act II testing were likely enemy hit-roll misses against the
level-90 developer preset. The native adapter runs after the enemy hit check;
absence of block feedback alone does not demonstrate a guard failure. That
21:17–21:24 session contained 55 block checks: all 29 covered guards blocked,
while 25 failed the fallback angle and one failed hand height. None failed
placement. The owner also reported a close-body guard facing the enemy in the
overlay. A reproduced fallback direction mismatch is corrected above; that
follow-up still requires headset testing. These logs cannot count attacks that
missed before the block function, so they do not prove the preset was the sole
cause of the earlier reports.

All 55 checks still used `mesh=0`. Developer discovery stages stopped at `0x1F`
(skeleton read, before mesh metadata). Visual verification of the overlay is
not verification of runtime mesh-size capture. Keep the measured capture issue
open separately; the fallback correction does not repair that discovery path.

The shield pose blocking reads is the steadied one (`vr/shield_steady.h`: 1 degree / 1.5 mm
dead zones, so the controller's tremor doesn't move the guard; real moves pass in
the same frame).

Holy Shield (state 101): while the local Paladin has it, the covered width grows
1.6x about the shield's center - the measured face's extent across the attack, or
the fallback's lateral proxy; the raised-height check uses the real shield - because blocks are expected to land on the glow drawn
around the shield. The glow's size is not in the tables (states.txt names its
overlay; Overlay.txt gives only a light radius), so 1.6 is a visual estimate
(`HolyShieldCover` in `steamvr/shield_guard.h`). The state comes from the
game-thread effects sample (every 200 ms, stale after 1 s); facing, height and
parry rules are unchanged. `Frame guard: Holy Shield coverage on/off` is logged
on each switch.

## Diagnostics and test route

- `Frame guard rejected:` counts decisions rejected by hand height/reach,
  tracking, invalid/stale geometry, `shield_angle` or `shield_position`.
- `[debug] guard_trace=1` adds per-call `Frame guard trace:` and
  `Frame guard native:` plus periodic `Frame guard pose:`. It defaults on in a
  developer build; an explicit saved value still wins. Regular builds default
  it off.
- Traces include surface center/normal, eye and attacker positions relative to
  the player, calibrated units per metre, movement mode and source-pose age.
  `mesh=1 size=...` confirms rendered bounds are active; `mesh=0` is the fallback.
  `Frame shield surface: rendered mesh captured` reports the measured dimensions
  and difference from the former attachment approximation once per generation.
- Native traces include block/avoid arguments, return value, roll-served status
  shield durability and native caller RVA. A pre-call BLOCK verdict is not a confirmed native
  block when the native function skips its roll.

Use an offline Paladin with each shield. Hold it toward one attacker while
stationary, walking and running; look aside without moving the shield. Move it
to intercept a side attacker, then turn it edge-on, lower it and hold it away
from the attack line to verify those open the guard. Repeat after recentering
and body calibration and while leaning/crouching. Preserve saved calibration
when installing. Verify Classic and shield bashes separately. Synthetic tests
cover transforms, gaze independence, source-pose expiry, native dispatch and
parries; they do not replace headset verification of the visual alignment.
