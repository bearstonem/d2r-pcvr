# VR equipment, spells, potions and journal

Implementation prototype, 2026-10-09. The owner authorized implementation of
this plan. The current development build contains the interaction foundation,
radial wheel, physical belt, wrist status, native-set holsters and spatial quest
page. It is installed on the owner's machine alongside the subsequent visual
fixes; detailed interaction validation remains pending. See
[GAMEPLAY.md](GAMEPLAY.md) for controls and [../HANDOFF.md](../HANDOFF.md) for the
latest deployment and evidence.

## Implemented candidate and remaining work

| Area | Candidate behavior | Remaining validation / work |
|---|---|---|
| Ownership and native state | Session-tagged equipment, belt, skill and vitals snapshots; stale request expiry; input rearm after UI/grabs/recenter/tracking loss | Verify native snapshots and outcomes in the headset |
| Spell wheel | Eight themed radial slots with native icons/localized names, persistent native bindings and VR quick-cast labels; all LT variants editable; selection trigger cannot cast | Headset shortcut validation; charge/readiness indicators; names for newer IDs can fall back to `Skill N` |
| Potions | Original 3D models/textures for all 15 variants; nearest-slot grip grab, bottle-opening/tip dwell, quick-use alternative; exact item revalidation and confirmed removal | GPU stereo/asset and interaction checks pass; headset reach, mixed/refilled belt and refused consumption need verification. Uses its own depth buffer, without scene/arm occlusion |
| Wrist | Live native health/mana in generated gothic glass orbs; back-of-left-wrist (-X grip) reveal, upright text, left-only tracking; brief low-health/low-mana cue | Native values, readability, thresholds and corrected watch gesture in headset |
| Holsters | Configurable hip anchors and seated preset, grip-and-pull requests set I/II, waits for observed set; explicit stow/draw | Initial set inference, identical/empty sets; item labels currently use base codes, no silhouettes; no arbitrary inventory equips |
| Journal | Quest UI-only copy, stereo scene behind it, left-hand placement, grip/release pinning, original page UV input; falls back to normal panel if capture unavailable | First headset verification of crop, native act tabs, close/reopen and tracking recovery; other pages and decorative geometry remain |
| Casting | Equip/select, point right hand, press/hold trigger; directional casts follow the visible casting shaft (hand aim for other weapons), with native target snapping suppressed only during that cast; native spread/resources/effects | Teeth headset check pending. **Full 3D spell flight/contact and immediate accepted native spawn are not implemented.** Native animation and ground-plane flight remain |
| Motion weapon skills (2026-10-11 source candidate) | Selected Amazon arrow skills use one draw/release per native cast/volley; Smite uses energetic left-shield contact when its native hook is identified; shield attachment follows the tracked wrist independently of the main weapon | Windows build and automated checks pass; installed for offline testing in both existing Steam/Battle.net VR copies on 2026-10-12. Headset validation pending. Special arrows retain native animation/ground-plane collision. Live Smite discovery, shield fit, resources/cadence and cancellation during wind-up need offline verification |
| Active-skill cues (2026-10-11 source candidate) | Persistent gold ACTIVE marker distinct from hover; closed-wheel weapon icon with brief name/action hint; temporary quick-cast cue restores the persistent choice | Synthetic preview checked; headset readability/placement and native Strafe busy state need verification; no inferred mana/cooldown readiness |

All new surfaces share one change-driven atlas (up to 10 updates/second, only
while visible) and respect the runtime's composition-layer limit. They do not
have scene-depth occlusion. No game artwork is distributed. The camera,
lighting/creature-label and gaze-VRS implementations are retained.

Host state/math regressions and the Windows DLL cross-build pass. A synthetic
GDI atlas was rendered through Proton and visually inspected. Native reader
signatures match the owner's local game-code snapshot. These checks do not
establish that the game accepts every native action or that surfaces align in
Steam Frame; use the candidate test sequence in GAMEPLAY.md.

The remainder of this document is the **target design**. Its future requirements,
particularly spell-family adapters and readiness metadata, must not be mistaken for
features already completed in the candidate.

Owner refinement: **spell gestures are out of scope**. Equip a spell, point the
right hand and pull the trigger to cast. Prioritize responsive projectile spells
with impressive, coherent effects. The target-family adapters below are engine
implementation details, not different gestures the player must learn.

## Recommended interaction scheme

Keep free-swing melee and the two-hand bow. Use **grip to grab objects**, and
**trigger to act with what the hand holds**. Bow trigger continues to hold the
string. Explicitly claiming a bottle, book or menu consumes the gesture until
release, so it cannot also swing a weapon, fire a spell or select a game menu.
An equipped weapon remains held without continuously squeezing grip.

| Purpose | Proposed interaction | Feedback |
|---|---|---|
| Weapons | Reach to a holster, squeeze grip, pull the set into use | Holster highlight, item silhouette/name, then confirmed equipped set |
| Skills/spells | Hold Left Y for an eight-slot wheel over the left palm; point right hand and click trigger to select | Actual skill name/icon, selected mode and availability |
| Minimap + compass | Choose Wrist, Palm, Either, or Off in **VR Settings > HUD > Minimap / compass location** | Current act and localized area name sit above the bronze bearing strip and map, independently of health/mana. Wrist is the default; Palm uses the same deliberate hold and casting guards as vitals. The center marker shows facing, N is map-up. The wheel's **MAP** tab remains available even when this display is Off |
| Quick casting | Hold Left Trigger + a face button; add Left Grip for alternate four slots; right hand aims | Same native assignments as the wheel; see GAMEPLAY mapping |
| Show all nearby loot labels (Alt) | Hold **Right Grip + Right Trigger**, grip first, outside menus/wheels | Holds native Show Items while both buttons are held, using the game's Alt range independently of aim. Release either to restore normal drop/hover labels; no cast or shot accompanies the combo. Names use 150% text size and stronger dark backing; item-quality colors remain. **VR Settings > Controls** lists the shortcut; stick clicks and native panel controls keep working |
| Potions | Reach to the front belt, grip a bottle, bring to mouth and tip | Actual potion type/count, then confirmed consumption |
| Health/mana | Choose Wrist, Palm, Either, or Off and adjust **Health / mana tilt** in **VR Settings > HUD** | Tilt the hand-mounted vitals/XP panel from -60 to +60 degrees in 5-degree steps; Apply saves, 0 restores the original angle. Palm requires left palm upward in front of the eyes for 0.25 seconds, with left trigger/grip released and no left-hand cast. Vitals follow the hand; automatic low-resource popups/vibration are a separate opt-in, off by default |
| Journal/map | Open Journal through the native tabbed game menu (Left X / Frame Left A) | Native quest status/text and tabs, readable at a comfortable size |

With current dual-hand controls, Left Grip + Left Stick Click opens the
native inventory menu; plain stick clicks toggle the corresponding wheel.
Height reset remains available in VR Settings. Right A defaults to interaction.
Right B cancels the active grab/preview. Keep button and wheel alternatives for
seated play or limited reach. All features must work without eye tracking and
without a particular character class.

## Holsters: two native sets, reachable positions

Start with two configurable holsters representing **D2R weapon sets I and II**.
Grabbing a holster requests that entire set, including its shield, offhand or
quiver. It does not equip an arbitrary inventory weapon or split the two sets.
Inventory remains the place to assign their equipment.

Use front/side hip positions by default, moved upward for a seated preset.
An optional shoulder location can suit a bow or long weapon, but reaching behind
the headset should not be required. The drawing hand determines the interaction;
the native equipment determines which hands display the weapon(s).

HMD and controllers do not measure the torso. The owner requested waist equipment
always stay in front as they turn: use tracked head position and horizontal
facing for the upright body frame, without a heading deadband or proximity/grab
freeze. Pitch/roll do not tilt the anchors; retain the last reliable heading at
a nearly vertical gaze. The grabbed bottle stays hand-attached. Measure holster
pull relative to the moving body so a turn/step is not itself a weapon draw.
Refresh after recenter or tracking discontinuity. Waist height/forward reach
remain configurable; free-form anchor repositioning is not implemented.

Require a fresh grip inside the highlighted volume followed by a small outward
pull. Brushing past a hip during combat cannot swap equipment. Ask for a desired
set, rather than blindly toggling on every sample: send one native swap only if
needed and wait for the equipment snapshot to confirm it. Show failure if the
game refuses. Preserve the verified melee pose reset on equipment changes.

Returning an equipped set to its holster may visually stow it and suppress its
VR attacks; native equipment/stats remain equipped. Clearly distinguish that
presentation state from an inventory unequip. A bottle or journal can temporarily
claim a hand and hide its weapon without changing equipment; that hand's attacks
stay disabled until the object is released and the input is rearmed.

## Spell wheel and casting

Replace the current B/X/Y/RB text grid with a radial wheel for the same eight
assigned native skills initially. Read actual assignments, names and icons;
include item-granted skills/charges and refresh on weapon swaps. A center tile
returns to weapon combat; a utility tab contains equipment and belt actions.
Journal is accessed through the native tabbed game menu rather than the wheel.
An unassigned/unknown slot must not display an invented spell or availability.

Hold Left Y to show the wheel, aim at a wedge, then click right trigger to
select. Close it after selection and latch it closed until Y is released.
Releasing Y without selecting cancels. Consume the selection trigger until it
is fully released, so choosing Fire Bolt never immediately fires it. Start with
the existing stationary selection behavior; locomotion while selecting is a
separate comfort option. Use a dead zone and hover hysteresis at wedge borders.

Casting uses one rule: **point and press right trigger**. A ready spell commits
on trigger press; projectile casts do not wait for trigger release, a hand
movement, a charge pose or the stock body's casting wind-up. Hold to repeat only
for skills whose native behavior supports it, at their legal cast cadence;
hold/release starts/stops continuous skills. Mana, charges and cast-rate gates
remain native. Changing spells or closing the wheel requires releasing the
selection trigger before it can cast.

The implementation still needs to resolve each native target type:

| Skill family | Proposed action | Native behavior retained |
|---|---|---|
| Directed projectile | Point the right hand and press trigger to fire from the hand/held casting tip along the sampled 6DoF ray | Mana, charges, damage, cooldown/cast-rate gate, special effects |
| Continuous stream/channel | Hold trigger to emit; move the hand to aim; release to stop | Native repeat cadence and resource drain; no extra charge mechanic |
| Ground placement / teleport / summon | Point at the destination indicator and press trigger | Range, walkability/placement and skill-specific destination restrictions |
| Creature / corpse target | Point at the highlighted eligible unit and press trigger | Hostility, corpse eligibility, range, visibility and native effect |
| Self buff / shout | Deliberate trigger press | Native self effect and cost; no forced target selection |
| Persistent aura | Equip the aura and press trigger to activate it | Native persistence/toggle semantics; wheel does not invent repeated casts |

Classify by skill behavior/data, with explicit supported adapters. Unknown or
complex skills keep a visibly identified native fallback. Read availability
instead of assuming every selected assignment can cast. Gesture recognition,
throw-to-cast, two-hand casting, gesture strength and release-to-cast spell
controls are outside this phase. Bow draw/release and potion drinking retain
their own existing physical controls.

### Responsive, visually complete projectile spells

Make the first legal shot respond to trigger press, with exactly one native
resource/damage transaction. Eliminate input queuing and body-animation delays
before increasing travel speed. Maintain the native cast-rate/recovery gate
between shots, and discard stale/rejected requests rather than firing them
after a menu closes or tracking returns. A held repeat samples a fresh pose for
each accepted shot; moving the head or hand cannot steer an ordinary projectile
already launched. Preserve intentional homing and other skill-specific behavior.

The cast flash, missile mesh, trail, emitted light and impact should read as one
effect, attached to the same accepted cast and actual flight/contact. Retain
the game's characteristic particles, sound and impact effects, with a brief
haptic acknowledgement. Reposition their emitters with the physical projectile;
do not leave a second effect at the native body/ground origin or a trail stretching
back to it. A denied cast gets failure feedback, not a fake launched projectile.
Both eyes must use the same effect state/time and world depth. Use the existing
lighting/projection fix and preserve full-quality transparent effects under VRS.

Start with a simple single-projectile spell. Verify hand-to-flight alignment,
immediate feedback, clean trails and impressive impacts before extending to
multi-shot, area or homing effects. Tune velocity per spell only after separating
launch latency from travel time; a universal arrow-speed multiplier would change
the behavior and appearance of unlike skills. Measure trigger/accepted-cast/
missile-spawn/first-visible timestamps and resource counts without heavy frame
captures. Check effect cost and both-eye consistency alongside responsiveness.

The current spell path emits native controller skill inputs and redirects
ground-plane aim from the visible casting weapon, using a fresh assigned-skill
snapshot to identify directional projectiles. Native selected-target queries
and final aim assist are suppressed only inside that local cast resolution.
Teeth's native fan and other spells' native spread/homing remain unchanged.
That is **not full 6DoF projectile casting**. Extend the bow's
release ownership, timestamp matching and swept contacts per projectile family;
do not apply the basic-arrow hook indiscriminately to homing, piercing, bouncing,
area or multi-projectile spells. Use the same world ray for the visual preview
and committed action. Ground spells need terrain-aware intersections, not the
bow prototype's flat launch floor. Purely cosmetic hand-origin effects are not
a substitute for matching collision/damage.

## Potion belt and wrist status

Place four small bottle groups across the reachable front waist, corresponding
to the native belt columns. Show the actual next consumable's type and count;
mixed columns, empty slots and automatic refill must match native selection.
Keep the belt quiet until a hand approaches or the player deliberately looks
down; offer the same belt on the wrist/wheel as a seated alternative.

Grip grabs a preview of the next bottle, with no consumption yet. Hold it near
the mouth and tip for the existing short dwell to request one use. Releasing
early cancels; do not create or discard an inventory item. Capture its item
identity and column revision, revalidate immediately before use, and cancel if
the column now contains something else. Reuse the game's belt action; do not
decrement quantity directly or simulate health/mana gains. Show success only
after a native acknowledgement/authoritative inventory change, distinct from
the haptic that acknowledges the grab. Never drink twice from one grab.

For quick use, the belt wheel can offer an explicit trigger action without the
drinking gesture. This is an alternative, not automatic drinking at low health.
Refresh wrist health/mana from an authoritative source, with bounds/current
maximums and invalid-data handling. Gauges appear when the wrist is presented;
brief critical-status cues must remain noticeable without a permanent HUD.

## Journal and other menus in space

Make the quest log the first journal page, followed by map and character/skills
tabs. Open in the left hand; grip the cover/handle to reposition it, or pin it
at the released pose. Right-hand laser and trigger operate page controls;
direct touch can follow once pointer behavior is reliable. Left X still opens
inventory, and every page has a clear close/back action. Only one reading
surface is active at first; it owns menu input and suspends combat gestures.

The journal must carry **native live quest information**, not duplicate quest
progress in mod state. Initially preserve the game's own page rendering and
interaction. This requires isolating UI from the scene: current `panelMode`
switches the entire view to a flat scene plus UI. Shrinking that capture onto a
book would put the flat world inside the book and lose the stereo world outside.
Extend the existing HUD-layer extraction for a clean panel texture, keep the
stereo world running, and map ray UVs back to the original panel coordinates.
Update the native UI once, and present the same surface to both eyes. Only then
add a book cover, independent tabs or a custom text layout. A grab must not send
a game click or move inventory items beneath the cover.

## Code foundations and gaps

| Existing code | Reuse / required work |
|---|---|
| [gameplay.h](gameplay.h), `Engine::Step` | Wheel selection, bow and drinking state already exist. Split per-hand ownership from the current global bottle/selection state; add explicit cancellation and native-result states. |
| [transport.cpp](transport.cpp), `SetupGameplay`, `End` | Current static atlas and both-eye quads are useful scaffolding. Add change-driven icon/text uploads, radial hit testing and body anchors. Replace the fixed ten-layer assumption with a bounded submission plan. |
| [gamestate.cpp](../vr/gamestate.cpp), `ReadItems` | Already reads four hand slots through SDK inventory/item services. Publish stable IDs for both sets, not just the active item-code hash. Set inference needs confirmation for empty/identical-code sets. |
| Local SDK `InventoryService` / `Items::ItemInfo` | Inventory enumeration supplies copied container, x/y, identity and quantity data. Verify belt ordering and native refill/consume choice. Snapshot only on the permitted UI/game thread. |
| [gamecmd.cpp](../vr/gamecmd.cpp) | Belt commands already queued on the UI thread; add typed request/result handling. Quest log is command `0x04`, weapon swap `0x2C`. A handled command alone does not prove successful consumption/equip. |
| [vrcam.cpp](../vr/vrcam.cpp), aim hooks | Existing native skill inputs and XY aim are a fallback. Need assignment/skill metadata, resource/cast-rate acknowledgement, full 3D targeting and per-family adapters. |
| [hud.cpp](../vr/hud.cpp), [gamestate.cpp](../vr/gamestate.cpp) panel messages | HUD render-target extraction and menu detection exist. Quest-only extraction, retained stereo behind menus and pinned/handheld input mapping still need work. |

Publish immutable, session-tagged UI snapshots; never walk game pointers from
the render loop. Cache assets and update labels/counts on changes at a bounded
rate. Use small procedural bottle/book/holster meshes with game icons read from
the user's installation; no new generated assets are needed for the pilot.
Preserve centered projection, creature-label ordering, foveation exclusions for
UI, and both-eye coherence. A compositor quad has physical placement but does
not automatically have world depth occlusion; integrate depth where physical
objects must disappear behind hands/scenery rather than claiming it is automatic.

## Implementation order and checks

1. **Foundation + wheel:** immutable equipment/belt/skill snapshots and one input
   owner per hand; actual skill names/icons in the wheel and confirmed set swap.
   Keep the current palette usable when metadata is unavailable.
2. **Belt + wrist:** real counts/type, physical grab/drink and quick use, native
   success/failure acknowledgement, then health/mana. Test empty/mixed/refilled
   columns, full health, and a refused request without consuming another item.
3. **Holsters:** configurable front/side anchors and seated preset, pending-set
   confirmation and stow state. Test looking down, crouching, snap-turn/recenter,
   same-type weapons in both sets, two-handed sets and bow/potion conflicts.
4. **Journal:** isolate the quest panel while retaining stereo; add hand/pin
   placement and mapped input before decorative pages. Check reopen/close,
   desktop-focus requirement, tooltips and no repeated UI/game updates per eye.
5. **Point-and-trigger casting:** prioritize one snappy, visually complete
   directed projectile, then ground placement/self buffs, channels, eligible
   corpse/unit targets and complex missiles behind the same simple controls.
   Verify latency, pose/effect/impact agreement, resources/cast rate, immunity/
   procs and cancellation. Spell gestures remain out of scope.

Across every stage: death/loading/session changes, stale tracking, recenter,
menu open/close and equipment changes cancel pending actions and require fresh
input. Test that a menu selection cannot cast, a holster pull cannot melee-hit,
a sip cannot shoot, and a released stale request cannot consume resources later.
Automated state/math tests precede deployment; physical comfort, reach, clarity
and native outcomes require the owner's later headset pass. No further headset
capture is needed to begin these foundations.
