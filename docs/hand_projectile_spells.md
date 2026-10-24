# Hand-origin projectile spells

Experimental support for Fire Bolt, Fireball, Ice Bolt, Ice Blast and Charged Bolt on the
Sorceress, Bone Spear on the Necromancer, and Holy Bolt on the Paladin, using the existing per-hand skill wheels. Assign either hand's
trigger or face-button shortcut. Hold it to repeat; both hands can cast their
own assigned projectile at the same time, with separate aim and cast timing.
Walking and turning remain available during these casts.

Straight free-hand shots start 5 cm along that controller's aim ray. The right casting
weapon uses its visible shaft direction and an origin 12 cm beyond its hand
attachment when a fresh weapon pose is available. This is a hand muzzle,
not an automatically measured staff tip. Each accepted launch freezes its
position and direction. In planar mode, pitch is clamped after the weapon pose is
applied, retaining continuous 360-degree yaw and the muzzle's launch height.
In planar mode, within five degrees of vertical, that hand keeps its last stable heading;
without one, it waits for a usable aim. Free aim retains full pitch, including
vertical shots. Turning afterward does not steer a shot.
Bone Spear and Holy Bolt use tracked controller aim even with a wand or scepter equipped. Bone Spear's
native piercing result is preserved, with each enemy contacted at most once.
The owned visual definition's four-unit height and three-unit backward offset
are canceled so the visible spear starts at the casting hand.

In **VR Settings > Aim**, **Keep spell shots level** defaults to **Off** when
no preference is saved. Keep it **Off** for full up/down aiming of
Fire Bolt, Fireball, Ice Bolt, Ice Blast, Bone Spear and Holy Bolt. The change applies to new casts from
both hands without restarting; projectiles already in flight keep their original
direction. Charged Bolt retains its native planar wandering in either mode.
The saved preference is `[gameplay] planar_spell_aim=1` (or `0` for free aim).

## Optional projectile aim assist

In **VR Settings > Aim**, choose **Spell aim assist**: **Off**,
**Light** or **Strong** (the default for missing preferences), then press **Apply**. The saved setting
is `[gameplay] projectile_aim_assist=0`, `1`, or `2`, respectively. This applies
to the supported Sorceress hand spells, Necromancer Bone Spear and Paladin Holy Bolt, independently for each
hand. Bow, javelin and native fallback skills retain their existing aiming.

Light permits up to four degrees of launch correction; Strong permits twelve.
On an accepted cast, assist checks living hostile monster bodies within a
12-metre ray reach and requires a clear path from the hand muzzle. A shot
already intersecting an eligible body keeps its exact launch direction. Otherwise,
Light aims halfway inside the body's radius; Strong aims at its axis, capped
at the correction limit. This leaves more room for enemy movement than aiming
at the edge, but does not predict movement or guarantee a hit. Assist favors
a small correction into a nearby body, with a slight preference
for the previous target during uninterrupted repeat casts. That preference
never widens the angle or range limits and expires after 750 ms without a cast.
Release, lifecycle and assignment changes reset it. Aim outside the assist
range or away from eligible bodies and the shot keeps its original direction.

Planar aiming permits horizontal corrections only, so a straight spell cannot
be assisted into a body entirely below or above its launch height. Free aiming
also permits a small vertical correction. Charged Bolt uses the target's ground
footprint and adjusts the volley's heading, preserving native spread and
wandering, with launch-only assistance even on Strong.

For Fire Bolt, Fireball, Ice Bolt, Ice Blast, Bone Spear and Holy Bolt, **Strong adds gentle homing**
toward the one enemy selected at launch, including a directly aimed target.
Steering is limited to 60 degrees/second, 45 degrees total, and 1.5 seconds.
Planar shots stay horizontal; free-aim shots can also bend vertically. The
projectile never acquires a different enemy after launch. Death, disappearance,
loss of hostility, a blocked path, a teleport, or passing the target ends
steering permanently; the projectile continues on its last heading. Normal
wall/body collisions and native lifetime still apply. Turning your hand or
casting from the other hand does not redirect an existing shot.

Off uses free aim; Light remains launch-only. Neither assistance mode adds a
movement lock, target indicator or changes to damage, mana costs and cadence.

Target scanning is bounded and runs only after native cast acceptance, avoiding
repeated scans while a held trigger is refused for insufficient mana or town
restrictions. It reuses the existing native hostility, body and wall queries;
no new native address hooks are required. Strong rechecks only its acquired
enemy on each native missile tick, resolving its ID afresh without retaining
a native target pointer. The collision bodies and launch-floor approximation
still apply. There is no predicted lead, and neither mode guarantees a hit;
enemies can evade the limited steering or leave the projectile's height.

## Native casting and collision

The native skill-start and skill-dispatch routines validate and consume mana
on the game thread. Each hand supplies its own skill in a scoped selection;
the original selection is restored before returning. No controller cast button
or native casting animation is sent for these supported spells. Each hand
uses the character's ordinary casting frames, including Faster Cast Rate:
14 base frames for Sorceress and 16 for Necromancer/Paladin, excluding the terminal frame. Two
hands intentionally permit twice the single-hand throughput, at the combined
native mana cost. There is no unlimited or free casting mode.

An insufficient-mana attempt requests the character's normal localized spoken
warning. A town-restricted spell requests the native "not in town" response,
including when mana is also low. The native town and mana predicates are sampled
while that hand's skill is selected, before native skill-start can clear it on
refusal. Successful starts and unrelated refusals do not request a warning. Both hands
share a coalesced notification, delivered to the current local client player
on the UI thread. The game's speech routine retains its voice selection and
75-tick repeat cooldown. Notifications expire after 500 ms and are discarded
on a player/session mismatch or while a menu or death animation is active.
Speech does not send a cast input, consume mana or change movement/cast timing.
These optional native entry points are separately checked; an unsupported
speech layout leaves hand casting available without this warning.

Straight projectile flight shares the bow's swept body collision and conservative wall checks.
Before native hit handling, the missile's actual position and room membership
move to the physical contact. This lets native Fireball splash, resistances
and secondary effects use the impact's grid position. Native client hit effects
run separately on the UI thread; they do not apply damage again. The
supported spell VFX retain their game assets, with their authored ground-launch
offsets canceled so straight projectiles start at the hand and Charged Bolt
appears on the launch floor.

Charged Bolt invokes the native volley once per cast. The game evaluates its
bolt count and retains each bolt's wandering path, lifetime, grid collision and
damage. A scoped target query and copied creation request supply the casting
hand's horizontal origin and aim without changing player movement or selection.
The visuals follow each native path's displacement on the launch floor, retaining
the hand's horizontal placement without its height. Charged Bolt still hits
through the game's ground grid. Space for the complete
24-bolt maximum is reserved before dispatch, and mana/cadence apply once per
volley. Each bolt has a separate visual identity. No straight-shot replacement
or second damage calculation runs for these bolts.

Menus, wheels, equipment/assignment changes, tracking loss, world transitions
and released buttons cancel pending requests. A hand's latest pose replaces
older pending samples; there is no accumulated burst after a stall. A cast
already in flight keeps its release direction until impact or native expiration.

## Bone placement and Holy Bolt

Bone Wall and Bone Prison retain native casting, mana costs, lifetime and
placement restrictions. Their target comes from the casting controller's ray
meeting the hero's floor, clamped to 2–40 grid cells. Point down to choose a
closer location; a level or upward ray places them six cells ahead. A moving
character, an old hover target or the wand's decorative angle does not supply
the destination. Bone Prison retains the game's own nearby-target/placement
validation; aiming at the ground near the monster's feet is the test case.

Holy Bolt uses full tracked-controller aim with Planar spell aiming Off.
Off/Light/Strong assistance applies as it does to Bone Spear. Assistance selects
hostile undead and demons from the current compiled MonStats table; it does
not steer toward ordinary beasts or town NPCs. A ray aimed directly at a
friendly creature keeps its direction and disables enemy steering for that
shot, preserving deliberate mercenary healing.

Swept contacts include friendly monsters/mercenaries through the game's own
type-3 missile collision filter. Its current signature is `(bank, target, args)`;
`args` holds native game/missile/owner/table-row pointers. Both the caller and
actual callback register/row layout are checked. Native hit handling still decides healing eligibility, damage,
immunity and piercing. It does not add damage to beasts or invent a healing
calculation. Other players are not scanned; only offline local play is supported.
The extra adapter is guarded separately: absent SDK services or a changed
native collision layout leaves Holy Bolt on its original casting path without
disabling other hand spells. Borrowed table rows stay on the game thread and
are reacquired each tick. The visual cancels its native four-unit emitter offset.

## Current limits

- Other class skills, charged item skills, other fans, homing projectiles, channels,
  ground effects and utility skills retain native input. This includes Town
  Portal, Lightning and Nova. A native action and physical spells do not start simultaneously;
  a competing button must be released before retrying. Native fallback skills
  can still impose their original movement lock.
- Terrain handling currently shares the bow's launch-floor approximation.
  Walls extend vertically; shots cannot bypass walls by aiming above them.
- Native splash damage still follows Diablo II's grid rules. Secondary impact
  VFX positioning, particle trails, room crossings and weapon muzzle placement
  require headset validation. This is not a claim of headset-tested accuracy.
- Native adapters are signature-checked for D2R 3.3.93787. Unsupported layouts
  leave the original casting path active. Desktop input is unchanged.

## Verification and offline headset checklist

Run `powershell -NoProfile -ExecutionPolicy Bypass -File steamvr/build_windows.ps1 -Test`.
The spell fixtures cover independent button ownership, repeated casts, release
and lifecycle cancellation, FCR boundaries, tick wraparound, planar aim across
all headings, vertical fallback, Charged Bolt eligibility and native path
displacement, and visible emitter alignment. Existing bow, wheel and
controller fixtures must also pass. These fixtures do not execute the injected
game routines.
The feedback fixture also checks refusal classification before selection is
cleared, town priority, successful-start suppression, simultaneous failures,
reason changes, held-input coalescing, expiry, menu/death cancellation,
character/session changes and clock regression.

The aim-assist fixtures cover angular limits across headings and pitches,
unchanged direct shots, planar/free/ground targeting, corrected-path wall
rejection, selection preference and its reset conditions, independent hands,
and settings normalization, interaction and INI persistence. Follow-up fixtures
cover sideways target movement, the wider Strong limit, homing versus straight
flight, steering bounds, continuous positions through turns, visual heading,
planar/free/ground behavior, and permanent loss of steering.

Holy Bolt's callback fixture also checks forwarding the leading data bank,
target and all four argument pointers, accepted/refused results, and rejection
of absent callbacks or invalid banks. This guards against accidentally using
the legacy two-argument callback convention. Runtime diagnostics distinguish
native-filter rejection from accepted contacts, and sample target life before
and after native hit handling; automated fixtures do not execute those game routines.
The Windows build and all 57 checks passed; its settings layout was also
inspected with the synthetic preview. The initial headset test reported
Fireballs passing beside moving enemies even on Strong. Subsequent runtime
logs confirmed launch corrections were applied, but that launch-only policy
did not track enemy movement during flight. The gentle homing change still
needs headset validation.

The existing runtime log now includes `Frame spell assist` counters per hand:
the sampled mode (0 Off, 1 Light, 2 Strong), casts, nearby bodies, bodies outside
the launch height, eligible candidates considered for selection, paths blocked
by terrain, direct shots, launch corrections, homing steps, ended homing tracks,
and the last launch correction angle. These
periodic diagnostics distinguish weak correction from a missing target or an
unexpected setting without retaining native enemy pointers after launch.

On an offline Sorceress in Steam or Battle.net D2R 3.3.93787:

1. Assign Fireball to the right trigger and Charged Bolt to the left. Fire each
   separately, then together at different targets. Check mana deductions and
   the separate repeat intervals, including insufficient mana for both.
   Exhaust mana and try each hand, then hold both: confirm the character's
   mana voice plays without overlap or rapid repeats. Drink a mana potion and
   confirm casting resumes immediately. In town, confirm the town restriction
   voice plays with full or low mana instead of the mana warning. Allow the native
   speech cooldown between attempts. Repeat while walking and with each supported
   projectile spell. Audible behavior requires a live headset test.
2. Walk forward, backward and sideways while casting. Physically rotate and
   snap-turn through all headings; tilt the controllers up and down. Check that
   straight projectiles stay level at the muzzle height, without snapping to
   cardinal headings; Charged Bolt stays on the floor even with a raised hand.
   Off/Light shots keep their trajectory; Strong shots can follow only the
   enemy acquired at launch, independently of later hand/head movement.
   Turn Planar spell aiming Off, Apply, and check up/down fire from both hands;
   restore On and confirm new shots return to level flight. Reopen settings and
   restart to check that the chosen mode persists.
3. Check Fireball splash and Charged Bolt's native spread against clustered
   enemies and walls, including a room boundary. Check bolt counts, repeated
   two-handed volleys, hit effects and travel audio in both eyes.
4. Release, reassign, swap weapons, open a wheel/menu, lose controller tracking
   and change areas while holding a trigger. Confirm there are no delayed casts.
5. Check Town Portal and unsupported skills through their existing native path;
   check bow and javelin flight and normal unmodded desktop play separately.
6. Compare aim assist Off, Light and Strong against single and clustered enemies,
   using both hands at different targets. Check direct shots, small misses,
   deliberate shots away from enemies, walls, closed doors and room boundaries.
   Repeat with planar aiming On/Off and enemies at different heights; confirm
   Charged Bolt stays grounded. Check target changes after releasing a trigger,
   opening a menu, changing skills and turning. Reopen settings and restart to
   verify persistence. Record headset/runtime and edition with the results.
7. On Strong, cast at enemies moving sideways and confirm a gentle visible
   curve. Kill the target with the other hand, let it move behind a wall, and
   fire past it: the old shot must not loop around or switch enemies. Compare
   Light's straight flight and confirm Charged Bolt retains its native spread.

Native behavior was investigated using the locally owned game's code and data,
with the public [D2MOO research](https://github.com/ThePhrozenKeep/D2MOO) as a
reference. No D2MOO implementation or Blizzard game assets are included.

Necromancer/Paladin regression checks use an offline developer character with
the existing level presets and Wand gear / Caster gear. Compare Bone Spear
Off/Light/Strong while walking and turning, both hands, native piercing through
multiple enemies, and mana/FCR cadence. Cast Bone Wall/Prison near, far and at
different headings while strafing. Check Holy Bolt with Planar Off/On and
assist Off/Light/Strong, both hands, walking/turning and clearly pitched shots.
Use undead/demons to check hits and homing, beasts to check native immunity,
and an injured mercenary to check healing with an enemy nearby. These new behaviors require
headset validation; automated geometry and eligibility tests are separate.
