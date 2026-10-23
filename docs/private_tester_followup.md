# Private test feedback follow-up

Feedback was recorded against private build `20261013-6abf4e5`. The changes below
address that build's menu comfort, discovery, map and casting feedback.

## October 14 combined session candidate

This candidate combines PRs #78, #79, #81 and #82 in one DLL: area and act
information above the hand compass, adjustable health/mana tilt, larger
dropped-item names with a Right Grip + Right Trigger hold, and sky/world
effects retained behind NPC dialogue. The area label follows the navigation
placement; health/mana tilt adjusts the vitals panel independently.

The native Windows build at `2f5c6db` passed all 65 automated checks and was
installed into the owner's Steam installation. After the final hold-to-show
change, the owner reported that all session changes now work in-headset.
This is owner-reported validation; the agent did not perform headset gameplay.
The checklist below remains available for broader regression testing; individual
headset/controller, resolution and character-class combinations were not recorded
in the final confirmation.
Building stages the DLL only. Install that combined DLL with the game closed
before launching the headset test; launching an existing installation does not
deploy the staged output or include open PRs automatically.

The owner's first combined headset run reported intermittent loot-shortcut
registration and invisible NPC choices when story dialogue opens. The loot
shortcut now uses the requested grip/trigger combo with analog hysteresis and
its own Controls entry. A second run showed only the right 20% of NPC choices
after pressing A. The widget crop has now been removed on every controller
profile: conversations retain the complete fitted native canvas and matching
laser mapping, with a stable upper-band anchor through story transitions.
The owner confirmed this correction working. The loot combo now holds the native
Show Items (Alt) action only while Right Grip and Right Trigger are both held.
It shows the native nearby loot list, with no VR aiming/pickup-distance restriction.
Release either button to restore ordinary drop/hover labels. Label rendering is
never vetoed by the shortcut. The revised Show Items hold is included in the
owner's successful session confirmation; larger text and stronger backing remain.
The revised native build passes all 65 automated checks, including the actual
Show Items command dispatcher with a simulated loader/UI queue.

The owner also reported `ACT -- / Location unavailable` both in Act I town
and outside it. The location reader had used the archive BIN layout (404 bytes,
name at 261) for the live table returned by the loader (396 bytes, name at 253).
It now reads the verified runtime layout and the full two-byte level ID. The
native compiler descriptors and installed loader's table service/validator were
independently checked; regression coverage includes town, outdoor, dungeon and
act transitions. Changes in level, lookup result, row size and resolved name
are logged for follow-up. The correction is included in the owner's successful
session confirmation.

## Controller haptic tuning follow-up

VR Settings now has a Haptics tab with overall strength and independent melee,
bow draw, bow/throw release, spell-cast, interaction and resource-alert strengths.
All range from 0% to 200% in 10% steps and default to 100%. Category and overall
strength multiply, capped at the controller's maximum amplitude. Each category's
Test button previews the draft on both hands; Apply saves and Close discards.
Supported independent hand spells add one short pulse per successful cast, on the
casting hand, including one pulse for a multi-projectile volley.

The native build passed all 65 checks, including category scaling/muting,
preview isolation, spell-event expiry and INI persistence. Synthetic Haptics and
Controls previews were checked for layout. The owner reported that the category
controls worked, but the melee Test button produced no perceptible feedback at
any strength. That preview uniquely used a 10 ms pulse at a fixed 180 Hz; its
timing/frequency are the suspected cause, not yet a confirmed hardware diagnosis.
**Short melee pulses** now defaults Off, selecting a 45 ms tap at the controller's
preferred frequency for both the preview and actual impacts. On restores the
10 ms / 180 Hz pulse. Each manual test logs hand, category, amplitude, duration,
frequency and runtime result. The revised melee pulses need headset verification.
On an offline character,
compare the same category's Test at 0%, 50%, 100% and 200%; save and reopen, then
restart to check persistence. Verify that reducing bow draw leaves melee and
release feedback unchanged. Compare melee Test with Short melee pulses Off and
On, including an unsaved toggle, and check it persists only after Apply. Check a
melee/shield impact with each pulse choice, a bow draw/release,
spell casts on each hand, a loot shortcut and potion interaction. Check that
refused casts do not vibrate, multi-projectile spells pulse once per cast, and
menus/tracking loss do not replay old feedback. Resource alerts should remain
opt-in through HUD settings. Overall Off should silence every category; category
Off should silence only that category. Test a draft change and close without Apply
to confirm gameplay keeps the saved values. Repeat on available controller types.

## Settings and behavior

| Feedback | Change |
| --- | --- |
| Menus too close | VR Settings > HUD > Game menus, left stepper: Close / Medium / Far. Medium is the default, 30 cm farther than the previous build. Close retains the previous placement; Far adds 60 cm. Inventory/merchant anchors are 0.55 / 0.85 / 1.15 m; main menus are 0.70 / 1.00 / 1.30 m. Their size and eye-level alignment remain fixed. NPC speech keeps its separate placement. |
| Controller picker hard to find | The VR tab has a larger highlighted dropdown, an explicit controller-label heading and detected left/right controller names. Automatic and the Valve Index override remain available. |
| Two hand wheels not obvious | Controls help explains clicking either stick and assigning with the opposite hand's laser. Each hand wheel also explains assignment and the top trigger slot's compatible weapon gestures. |
| Compass option hard to find | The HUD tab explicitly labels the combined Minimap & Compass control, including Off and the independently accessible wheel map. |
| Raising a hand opens a full-screen map | While VR requests map capture, borrow a native corner layout and restore the previous transient layout when capture ends. The game's saved map setting is not rewritten. Both hand and wheel capture benefit. |
| Tooltip hover depends on aim position | Use exact laser coordinates for the item grid's draw-time hover query as well as pointer dispatch. Previously, drawing could fall back to the controller-selected cell after laser dispatch ended. |
| Tooltips can obscure other items | HUD > Item tooltip timing offers Instant (default), Short (300 ms), and Original. Short tracks the native item and widget, so movement within a multi-cell item does not restart the timer. Switching items, dragging, losing the ray or a long frame gap resets the dwell. |
| Equipped comparisons unclear | Retain native comparison generation and eligibility, requesting comparison only at the item-tooltip Shift query. No keyboard Shift event is generated for purchasing or transfers. The main tooltip and its comparisons are delayed together in Short mode. |
| Charged Bolt too quiet | Restore the skill's native casting-start sound, which direct casting bypassed along with the character animation. Play once per successful cast, independently per hand, not once per volley member. Keep the game's volume, audio channel, travel sounds and native sound redirection. This also restores start cues for the other supported hand spells. |
| Missing out-of-mana feedback | Separately addressed by PR #57. The original private ZIP predates that fix; the next combined candidate must include it. |

All native changes are scoped to active VR ownership and signature checked for
D2R 3.3.93787. Desktop pointer/controller behavior and saved native options are
preserved. The map layout and audio fixes reuse native functions and installed
game data; no extracted game assets are distributed.

## Verification

- Native Windows build and all 56 checks passed. After adding further settings
  assertions, both updated settings interaction and INI tests also passed.
- Added coverage for menu preset geometry, exact item cursor-query scope,
  per-item dwell and reset conditions, temporary map layout restoration,
  one casting cue per volley/per hand, stale audio rejection, and setting
  defaults, normalization and persistence.
- Inspected synthetic Controls, VR and HUD previews for readable labels and
  non-overlapping controls.
- Compared all four newly used native signature regions with the locally owned
  game's existing code snapshot. Runtime guards check them again before use.
- Index/Quest headset testing and audible validation have not been performed.
  The socket report is tentative: the hover correction addresses one concrete
  cause, but does not establish that every socket-related visual overlap is fixed.

## Offline tester checklist

Use local offline characters only. Record DLL/build identifier, Steam or
Battle.net edition, headset, controllers, connection/runtime and resolution.

1. On Index, check Automatic identifies both controllers and that selecting
   Valve Index shows A/B and Grip + A/B on both wheels. Check each stick click,
   opposite-hand laser, trigger and face-button assignments. Repeat on Quest
   with left X/Y and right A/B. Verify settings survive restart.
2. Turn NVIDIA DSR off if previously enabled. In VR Settings > Perf select
   4096 x 4096 and Apply. Confirm the reported source resolution and compare
   clarity/performance with a lower preset. High native graphics settings or
   VD quality alone do not establish the game's actual render resolution.
3. Try all menu distances with inventory, character, merchant and main menus.
   Check eye-level content, full tooltips, readable text and matching laser hits.
   In VR Settings > HUD, adjust Health / mana tilt to negative, zero and positive
   angles, then Apply. Try Wrist, Palm and Either with navigation on the same
   face and on the opposite face. Check readability, a stable panel center,
   unchanged map/compass placement, and persistence after restart. Close without
   Apply should discard edits.
   With the sky enabled outdoors, open NPC choices, continue speech and open the
   journal; verify the sky stays visible behind these panels. Repeat with fog
   enabled and, indoors, an enabled cave ceiling. Enter a merchant or waypoint
   and check the shared black background, then return to dialogue and close it.
   Check both eyes during transitions.
   Engage NPCs with A, including one with automatic story text. Check the whole
   choices menu (Trade, Talk, topics and Cancel), then dismiss/finish speech and
   return to choices. Confirm no cut-off text or surface jump. Try laser and
   native controller selection at 3072/4096 square and a widescreen resolution;
   verify the changed canvas remains readable at each menu-distance preset.
4. Start with the game's full-size map setting, then raise the wrist/palm and
   open each wheel's MAP tab. Expect a compact map and no full-screen map flash.
   Lower the hand, open menus and leave/rejoin a character; confirm native map
   visibility/layout restoration. Repeat with left/right corner settings and
   hand navigation Off. Compass, area/act label and map should appear/hide together.
   Check the area name (including dungeon floor) and act above the compass while
   walking across a boundary, using a waypoint/portal and changing acts. Try Wrist,
   Palm and Either; verify no overlap with vitals and no previous-area label after
   loading or rejoining. The area label and compass should remain usable if map
   capture is unavailable.
5. Sweep the laser over every occupied cell of a tall/wide item, its edges and
   its sockets in inventory and a merchant. Check ordinary, empty-socket and
   socket-filled items. Expect a stable item tooltip and eligible equipped
   comparison. Check another item, empty cells, dragging and controller
   navigation. Try Instant, Short and Original; verify purchasing/stack
   shortcuts still honor only the player's actual modifiers.
   Check the original-size dropped-item names with darker backing against bright/dark terrain and in a
   crowded loot pile; verify quality colors, spacing, click targets and pickup.
   Check VR Settings > Controls says to hold Right Grip + Right Trigger for item names.
   Let dropped-item labels time out, then hold Right Grip and squeeze Right
   Trigger: all nearby labels should stay visible as if holding Alt, including
   distant items away from the aiming ray. Compare coverage with native Alt.
   Release either button: ordinary new-drop and hovered-item labels must still
   appear, while the forced labels stop. Verify the native loot filter still applies.
   Test the combo before any labels have appeared. Holding must never latch it on;
   releasing grip first must not cast or shoot. Re-squeeze grip while keeping the
   consumed trigger held; labels should return without grabbing a potion or weapon.
   Release only the trigger and repeat with grip held; check small squeeze
   fluctuations do not interrupt the hold. Adding grip to an existing attack
   must not turn that attack into a loot-label hold.
   Try ordinary wheel clicks and native inventory hold-to-sort afterward, then
   menus, tracking loss and leaving/rejoining a character. Each interruption must
   release Show Items and require a fresh trigger release before another hold. Verify inventory
   tooltips and monster labels retain their appearance.
6. Cast Charged Bolt alone, then alongside Fireball, while stationary and
   moving. Listen for one normal casting cue per hand per accepted cast, with
   no burst proportional to bolt count. Check native effects volume/mute.
   With PR #57 included, drain mana and verify the character warning and its
   native cooldown. Separately check optional low-resource alerts if desired.
7. Test Fire Bolt, Fireball, Ice Bolt, Ice Blast and Charged Bolt on each hand,
   alone and together while walking, physically turning and snap turning.
   Straight projectiles should start at hand/weapon height; Charged Bolt stays
   grounded. Repeat with planar aiming On/Off. Unsupported native skills still
   have their documented native behavior and may lock movement.

If sockets still obscure a tooltip, capture whether the tooltip disappears,
changes to another item, or remains visible behind socket artwork. These are
different failure modes and need a specific reproduction before changing
render order or hiding socket information.
