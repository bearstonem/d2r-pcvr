# Motion gameplay prototype

## Sorceress casting regression candidate (2026-10-13)

The tester reported a left-trigger Town Portal casting the right-trigger fire
spell, inaccurate fire-spell aim, and movement stopping during casts. The log
confirmed distinct native assignments (right slot 0: skill 36, left slot 4:
skill 219) and an installed spell resolver hook. Controller quick casts also
use the `0x1446c0 -> 0x143960` aim path; they need a final integer-target
correction there, independently of the existing `0x190440` resolver hook.
Only the aim caller gets this correction; movement callers retain their points.
Native VR casts bypass the legacy walk-learned direction calibration, and a
missing/stale rendered staff pose falls back to the casting hand's tracked aim.

The left and right trigger use the same native B button in different banks.
The candidate sends the alternate modifier alone for at least 50 ms and a new
published packet before allowing its button through. This sequencing happens
where the game polls the pad, so an XR/render hitch cannot skip the modifier
report. A release cancels the pending cast. This is a candidate fix for native
input ordering, not yet confirmed in a headset.

**Movement lock remains unresolved.** Keeping locomotion input present does
not bypass the native cast state/animation gate. This candidate does not claim
to enable walking through spell animations. Live native-path inspection and
offline headset checks are still needed for all three reported symptoms.

## Two-hand wheels source candidate (2026-10-13)

The current source replaces the single held wheel with two toggled hand wheels.
Click either stick, then use the opposite laser and trigger to edit that hand's
five slots. Trigger assignments are persistent; face shortcuts are temporary.
The character's native controller assignments persist the ten bindings across
weapon sets. Right A retains Interact when its native slot is basic/unassigned;
an explicit spell assignment replaces it. Native duplicate-binding rules apply.

The left trigger assignment now owns supported bow gestures and physical Smite.
Right trigger still grabs the bow string; away from the string it uses its own
assignment. The Weapon skill gestures VR setting disables those skill gestures
without removing button casts. Unsupported melee skills remain button-only.

Physical Smite keeps its native input pulse alive for 180 ms so the left-hand
alternate-bank preparation (50 ms at the game consumer) can finish before the
button disappears. Native Smite completion cancels that pulse immediately;
assignment, equipment, menu and tracking cancellation remain in effect. Other
weapon gesture pulses retain their original duration. A regression fixture
combines XR report timing with 25 Hz native pad polling across 160 scheduling
combinations; the former 65 ms pulse loses the button edge in all of them.
The longer pulse passes those cases; live shield-contact verification is pending.
Each hand has its own aim pose and optional skill indicator (off by default). Left casts cannot inherit
an equipped right-hand wand/staff shaft. Simultaneous input is serialized with
release gating, not delayed until another cast finishes.

Movement and turning remain available while the wheel owns combat input.
The Inventory, Map and VR Settings utility tabs replace displaced shortcuts;
native menu navigation and motion-disabled fallback controls remain available.
See [CONTROLS.md](../CONTROLS.md) for the current layout. The older controls and
installed-build reports below are historical and do not describe this candidate.
This candidate is not installed or headset-tested; the inherited aim/movement
fix also does not establish uninterrupted movement through native animations.


Right-handed OpenXR controls for offline D2R. The owner verified the live native
bow string/draw and two-hand aiming, then verified revision 4 free-swing melee
on 2026-10-09. A new **physical basic-arrow build is installed for testing**:
immediate native firing on release, a frozen two-hand 3D trajectory, corrected
projectile ownership/speed, timestamp-matched held bow poses, and swept contacts.
The first flight test showed arrows originating above the player. A mesh-only
transform correction is installed; its code/geometry checks pass, and the owner
will test its visual result later. See [../HANDOFF.md](../HANDOFF.md) for the
latest installed build and runtime status.
The current Windows preview includes these implementations. See
[INSTALL-WINDOWS.md](../INSTALL-WINDOWS.md) for the release and current
[Quest verification](QUEST_TESTING.md) for headset coverage.

The **installed development build** adds a radial spell
wheel, native-state belt and wrist gauges, shoulder weapon swapping and a handheld or
pinned quest page. Detailed headset checks for these interactions remain pending.
The controls below describe this build. [INTERACTIONS.md](INTERACTIONS.md)
separates completed code from the full target design.

The **2026-10-11 source candidate** adds motion input for selected Amazon bow
skills, an active-skill indicator, independent left-wrist shield attachment and
contact-triggered native Smite. It was installed for the owner's offline testing
in both existing Steam/Battle.net VR copies on 2026-10-12, with DLL hashes checked
and calibration/settings/saves preserved. The owner's first headset test confirmed
the ACTIVE marker and Smite icon, but physical Smite did not register while button
casts worked. The motion hook had failed activation. A function-directory fix now
activates it in live Steam/Battle.net main-menu checks. The owner subsequently
verified Paladin shield bash working on Steam Frame with SteamVR streaming and
Steam 3.3.93787; logs record 14 completed native Smite casts. Amazon skills and
the broader scenario checks below remain pending.

The build also adds an **in-headset VR settings panel**.
Hold **Left Grip and press Left Y**, or select **VR SETTINGS** in the wheel.
The shortcut also works with motion combat disabled. Use the right laser and
trigger; **Right B**, the shortcut again, or **Close** dismisses the panel.
Point at a row for a moment to see its help beside the panel. Rows with named
choices, such as Stereo rendering or Foveated rendering, also explain the
selected choice, so stepping through them with - and + explains each one.
The owner verified panel visibility and laser/tab interaction after the
October 9 Proton layer-conversion fix. Apply/cancel and broader input-ownership
checks remain pending.

Spell casting remains **equip, point, pull right trigger**. Directional spells
now use the visible wand, staff, scepter or orb shaft; other equipment uses the
right-hand aim. Native selected-target snapping and aim assist are suppressed
only while resolving an owned directional cast, including quick-cast shortcuts.
Projectile targets follow the sampled aim even while walking backward or
strafing. They use the current native player position and world-space weapon
direction, without the legacy walking calibration; the final target is corrected
even when the native movement path skips the usual attack-point helper.
The controller's press/repeat command is corrected again after its second
selected-unit lookup, before native command validation. This override changes
only the current local directional cast's target, without changing native
action flags, costs or cadence. Resolver, final-command and missile-creation
diagnostics distinguish the requested aim from what native creation receives.
The owner confirmed separate trigger spells now work, but reported fire-spell
direction sticking between quarter turns even after the final-command correction.
Missile logs and native validation identified the cause: the previous 60-cell
aim point exceeded the server's 50-cell limit on each axis except near diagonals.
The aim point now sits 40 cells away, leaving movement margin and preserving all
headings. This changes the heading reference, not native projectile lifetime or
travel range. The corrected range still needs headset validation.
While a trigger skill or quick-cast shortcut is held, locomotion stays on the
native controller stick instead of the legacy direct-walk reinjection path.
Legacy aim modes cannot replace that stick or hold stand-still Shift during a
VR skill cast. The owner reports that casting still stops character movement;
this input fix does not remove the game's animation/state restrictions.
Teeth keeps its native fan, centered on the weapon's horizontal direction.
Ground placements, buffs, summons and corpse-only skills keep native targeting
(unless VR Settings > Aim turns the aim rules on; see the Aim page).
Host tests and native signature/table checks pass; this aim correction still
needs a headset check with Teeth. Immediate hand-origin **3D** spell flight/contact
remains pending: missiles still use the native ground plane, animation, effects,
spread/homing, mana, charges and casting cadence. Spell gestures are excluded.

Revision 4 matches controller input to the rendered weapon's timestamp and uses
that pose's world placement, fixing missed contacts during fast motion. Its
synthetic timing regression passes, and the owner reported **"works great"** in
Steam Frame on 2026-10-09 after the sword comparison. Other weapon categories and
creature sizes still need checks.

Fresh installs enable motion controls in `GAME/mods/D2R-PCVR/d2rloader/plugins/d2r_pcvr.ini`:

```ini
[gameplay]
motion_controls=1
```

Settings reload live. Set to `0` to return to the previous button controls.
The installer also enables fixed foveation and leaves eye tracking off. Release triggers/grips
when changing modes. Keep the game focused as before; native game menu laser
clicks still require it. The new mod settings panel handles clicks locally.
Eye-tracked foveation, centered projection and label fixes are retained.

## Controls

Both controllers and the headset must be tracked. Their physical positions and
orientations drive gestures in room space; turning the camera/head alone does
not generate melee velocity. The layout currently assumes a right-handed player.

| Action | Control |
|---|---|
| Open tabbed character menu | Left primary face button (Left X / Frame Left A); inside menus this is native X |
| VR settings and controls reference | Left Grip + Left Y; also **VR SETTINGS** at the bottom of the wheel |
| Spell wheel | Hold Left Y; raise/turn left palm toward you; aim right-hand laser at a wedge and click trigger |
| Assign wheel skills | Hold Left Y → **EDIT SKILLS** → choose a slot → choose a learned skill; **CLEAR SLOT** removes a binding |
| Basic attack / bow on right trigger | Click **BASIC ATTACK** in the wheel center; release the selection trigger first |
| Selected skill / spell | Point right hand and press trigger; supported bow skills also use draw/release as described below |
| Selected Amazon arrow skill | Draw and release once for the native skill; Multiple Shot emits its fan, Strafe requests its native volley |
| Selected Smite with a shield | Swing or thrust the left shield through an enemy; keep right trigger released |
| Cast without opening the wheel | Each hand casts its own skills, aimed with that hand: trigger, two face buttons, and same-hand grip + face button for two more ([hand skill layouts](../CONTROLS.md#hand-skill-layouts)) |
| Melee swing / thrust | Swing the equipped melee weapon through a creature, including with a skill selected; no grip, trigger, or preselected target |
| Button attack / crossbow | Weapon combat mode, right trigger; native crossbow firing |
| Nock bow | Bow equipped: bring right hand to string and hold right trigger; no left grip required |
| Draw and shoot | Pull string hand back at least 12 cm (28 cm total separation), then release trigger |
| Throw a javelin, throwing knife or axe | Any javelin, throwing knife or throwing axe equipped in WEAPON mode, right trigger bound to Attack, Throw or nothing: hold right trigger, make a deliberate throwing motion, release to throw along the hand's motion. The game's own Throw fires the weapon's own missile and consumes its quantity; a knife flies point first, an axe tumbles end over end. With a javelin, Poison Javelin, Lightning Bolt, Plague Javelin or Lightning Fury on the right trigger (or loaded from a face button, like the bow quiver) rides the throw: the skill's own mana, damage and effects. A Barbarian with a throwing weapon in each hand and Double Throw on the right trigger (or loaded) throws with both hands: the left trigger throws the left weapon, and each throw is one native Double Throw of that hand's weapon. Trigger-free jabs and swings stay available |
| Cancel active grab/draw | Right B; release inputs before starting again |
| Physical belt potion | Reach to an original 3D bottle on the front belt and hold **Right Grip** to grab; left-hand grip also works |
| Drink | Keep grip held, bring the bottle's opening to your mouth and tip nearly horizontal for 0.25 seconds; release to return an unused potion |
| Drop the empty bottle | Open your hand after drinking: the bottle falls, bounces and rolls on the floor (visual only); hit it with your weapon or shield to knock it away |
| Heal the mercenary | Toss a held Health, Rejuvenation, Antidote or Thawing bottle (release mid-swing, at least 1.4 m/s) so it lands within 1.5 m of your mercenary's feet. It shatters on landing; a miss leaves the potion on the belt |
| Quick potion | Left Y → **EQUIPMENT / BELT** → trigger a potion wedge; grip on a potion wedge instead grabs it into the right hand |
| Swap weapon sets | Reach the swap hand over either shoulder and press its grip; right hand by default, selectable in VR settings. Hip anchors are removed |
| Seated weapon selection | Utility wheel → **Weapon I** or **Weapon II** |
| Stow/draw | Utility wheel → **Stow weapons** / **Draw weapons**; native equipped stats stay unchanged |
| Health/mana | Check the back of your left wrist, or choose **Palm** in **VR Settings > HUD** and hold your left palm upward in front of you briefly. No button required; automatic alerts are off by default |
| Show all nearby loot labels (Alt) | Hold the **right stick down** to hold the native Show Items action; let go to restore normal drop/hover labels. Push it **up** to toggle running / walking. Listed in **VR Settings > Controls** |
| Explored minimap | Hold Left Y ? **MAP** in the bottom tabs; point right laser + trigger. **SPELLS** returns to skills; releasing Left Y closes the menu |
| Quest journal | Open the native tabbed game menu with Left X / Frame Left A, then select Journal. The duplicate wheel entry is removed |
| Journal controls | Right laser + trigger uses native page controls/act tabs; Right B follows native back; Left X performs native X |
| Inventory drop / sort | Native X (Quest Left X / Frame Left A) / press and hold right stick click; right-stick click no longer swaps in gameplay |
| Walk / snap turn | Sticks; paused while wheel or journal is open |
| Inventory menu / height reset | Left grip + left stick click / VR Settings > Reset eye height |

Dropped-item names use **VR Settings > HUD > Loot label size**: **45%** by
default, adjustable from **20% to 150%** in five-percentage-point steps.
Apply saves `[hud] labels_size` in `d2r_pcvr.ini` and updates it live; existing
saved sizes are preserved. Text, dark backing bounds and click targets scale
together; item-quality colors are preserved. Holding the **right stick down**
holds the native **Show Items (Alt)** action, with a short vibration when the
hold starts. All nearby loot labels stay shown while the stick is held, using
the game's native Alt range rather than a VR aiming or pickup radius. Let go to
restore normal labels for newly dropped or hovered items. Pushing the right
stick **up** toggles running / walking, once per push, like the wheel's
RUN / WALK. Its sideways pushes keep turning: a push counts only when mostly
vertical, and from then until the stick is back near the centre it turns
nothing. Neither works while a wheel, menu or VR Settings is open; a stick
still held when they close must return to the centre first.
Release the combo before casting or starting a grip interaction again.
Use it outside menus and wheels, with no potion or holster interaction active.
Normal right-stick clicks still toggle the wheel; native menu hold-to-sort stays
available. Menus, tracking/focus loss, leaving VR or changing character release
the command. Release the trigger before starting a fresh hold after an interruption;
there is no saved toggle or automatic resume.
The game's native item/filter rules apply. Inventory tooltips and monster labels
keep their styling.

**Choosing which item Interact picks up (native VR).** VR Settings > Aim >
**Loot pick**: Off (default), On, or On with glow. With several items on the
floor, Interact (right A by default) takes the item you choose rather than the
game's nearest one. Hold the aiming arm out (about 20 cm from your head, less when
pointing steeply down at your feet) and point at an item: the item nearest the
spot where the hand's ray meets the floor is chosen. With the arm resting, the
item nearest the spot you look at is chosen instead (the look is taken a few
degrees below the head's forward, where the eyes rest). An item counts within one
cell of that spot and up to 25 cells away. Between two items side by side the
current choice stays until the other is clearly nearer, and the choice is held
while Interact is held. If no item is chosen, the game's own target is used,
including doors, chests, waypoints and NPCs. A chosen item comes first: looking
at a chest with loot at its foot picks up the loot.
A chosen item farther than the pick-up reach (5.5 cells by default, a tuned value)
is not picked up and the hero does not walk to it; the game's nearest item is not
taken instead. Doors, waypoints and NPCs the game targets keep working. Step
closer or look at a nearer item. The item list comes from the hero's rooms, the
walk the Kurast sewer entrance search uses; it has no line-of-sight test, so an
item just behind a wall can be chosen and the hero walks around to it.

With **On with glow**, the item Interact will take glows along its 3D model's
outline in its rarity color: white normal (runes too), gray socketed or ethereal,
blue magic, green set, yellow rare, gold unique, orange crafted. With no item
chosen it outlines the game's own nearest item. Its name, when the game shows
it (for example while holding the right stick down to show items, or for a fresh
drop), gets a frame in the same color and is drawn 30% larger. The glow pulses
gently. A chosen item past the pick-up reach glows a steady red instead. With A
set to attack only, the game's "Interact" prompt keeps naming its nearest item
while the glow shows the chosen one; Interact still takes the chosen one. The
reach is INI only:

```ini
[interactions]
loot_pick_reach_cells=5.5 ; 0 = no limit (A walks to the chosen item), up to 25
```

The highlight uses the same depth-outline pass as the monster target outline,
so it follows the ReShade effect being active. Desktop play is unchanged.

The minimap and compass form one navigation display on your left hand.
**VR Settings > HUD > Minimap / compass location** offers **Wrist** (default),
**Palm**, **Either**, or **Off**. Click **Apply** to save. Both appear and hide
together, independently of health/mana. When both displays share a hand face,
navigation sits above the vitals; otherwise it sits directly at the hand anchor.
The wheel's **MAP** tab remains available even when navigation is **Off**.

The label above the compass shows your current **act (I-V)** and **area name**,
including named dungeon floors. It follows the same wrist/palm placement and
visibility as navigation, and updates when you cross a boundary or use a waypoint
or portal. Area names use the game's current language. While location data is
unavailable, the label says **Location unavailable** instead of retaining the
previous area. The label and compass can still appear while map capture is unavailable.

A slim bronze **compass** sits above the minimap. Its fixed
center marker shows your facing direction; **N** is the top of the native map,
with **E/S/W** and diagonal bearings around it. Physical turns, stick turns and
recenter use the same orientation as the stereo camera. Looking straight down
retains the last reliable horizontal head direction so the bearings do not flip.
Wrist placement uses the watch gesture. Palm placement uses the same deliberate
palm-up hold and casting guards as health/mana below. Both navigation elements
hide while a wheel or menu is open; low-resource alerts never summon them.
The compass shows direction only; it does not add destination or quest markers.

**VR Settings > HUD > Health / mana location** offers **Wrist** (default),
**Palm**, **Either**, or **Off**. Click **Apply** to save. Palm mode puts the
health/mana and XP display just above your left palm: raise it in front of you,
turn the palm upward toward your eyes, and hold for a quarter second. Release
the left trigger and grip; casting with that hand hides the display. Lower or
turn your hand to hide it. Losing left-hand tracking or opening a menu hides it
and requires a new deliberate hold. Navigation has its own location setting, so
you can keep health/mana on the wrist and navigation in the palm, or vice versa.

**VR Settings > HUD > Health / mana tilt** adjusts the angle of the hand-mounted
health, mana and XP display from **-60 to +60 degrees**, in **5-degree steps**.
Positive values lean its top edge toward you; negative values lean it away.
Click **Apply** to save and use the angle immediately. **0 degrees** restores
the original angle and is the default for existing profiles. The panel pivots
around its center on both Wrist and Palm placements (including Either).
Navigation keeps its own placement, and low-resource popups continue to face you.

**Low health / mana alerts** is a separate toggle, **Off** by default. Leave it
off for gesture-only vitals. Opting in adds a brief popup near your view and
left-hand vibration below 25% health or 10% mana in spell mode. Location **Off**
suppresses these alerts too. Palm placement and gesture comfort still require
headset testing on Quest and Index controllers.

Native OpenXR gameplay hides the original flat bottom toolbar (health/mana
orbs, belt and skill buttons), including with older `hud.hide=0` profiles or
legacy camera settings that would otherwise draw it back into the view. This
is automatic and does not change your INI. Native inventory, dialogue and
other menu panels remain available; desktop/legacy camera HUD preferences
still apply outside active headset gameplay. Labels and tooltips outside the
toolbar strip remain visible. Verify both eyes and the game's different UI
safe-area sizes when testing this fix in a headset.

Quest updates and level-ups no longer hang in front of your eyes. In the
headset the game's notification box ("Quest has been updated", "You've
leveled up") is hidden, and its message appears on a small card in the room
instead: about a metre ahead where you were looking, a little below eye
level. Each card stays put for 3 seconds; a level-up shows its skill-point
and stat-point cards one after the other. The text is the game's own, in its
current language. The hidden box keeps its native timing. Cards wait while
an opaque menu or VR Settings is open and are dropped after 10 seconds.
Placement, readability, and whether ≡ still opens the quest page while the
hidden box is up still need a headset check.

The Map tab preserves your selected spell and uses the native explored areas
and markers. Choose the left or right corner minimap layout in game options;
full-screen automap capture is unsupported. When neither the wrist nor the Map
tab needs it, the native map returns to its prior visibility. The wrist retains
health, mana, level and XP progress with the wrist minimap turned off.

### Inventory, conversations and trading in VR

Native in-game menus share a room-anchored surface in front of you. The full
tabbed character menu, stash, inventory, cube, trade and waypoint surfaces use one opaque
black background in both eyes. Dialogue and journal panels retain the world
backdrop, including any enabled sky, fog and cave ceiling effects. Opening an
NPC conversation or continuing its speech keeps those effects visible; entering
a merchant, stash or waypoint retains the shared black management background.
NPC choices and story text retain the complete native canvas, using the same
fitted surface and laser coordinates on all supported controllers. Their
placement stays fixed when speech ends and choices return. The previous
widget-based magnified crop was removed because it cut off choices after a
VR resize; readability and selection still need a headset check.
Inventory/equipment, NPC
choices and speech, shops, stash, cube, waypoint, skills and character panels
retain D2R's original artwork, text, prices, grids and tooltips. Inventory and
shop panes keep their native relative positions; hovering a comparison tooltip
does not resize or move the surface. The quest log retains its handheld/pinned
placement. Title and character-selection screens keep the existing flat panel.

| Native panel action | Control |
|---|---|
| Open the tabbed character menu | Left primary face button during gameplay; native X in menus |
| Hover, select, pick up/place, drag | Right laser + right trigger; native click/drag behavior |
| Context action (native right-click) | Left Grip + Right Trigger while pointing; action depends on the item and panel |
| Native menu navigation | Sticks or face buttons; triggers and grips map LT/RT/LB/RB in native navigation |
| Native Ctrl modifier | Hold left trigger while clicking |
| Native Shift modifier | Hold right grip while clicking |
| Push / pull the panel | Point at it, hold right grip and move that hand toward or away from you; release to leave it there |
| Back / close | Right B, or the native close button |

Game menus push and pull only: the panel moves nearer or farther by exactly
the hand's travel toward or away from it, between 0.40 m and 1.50 m, and
ignores sideways, vertical and turning movement. It starts only when the right
laser is on the panel and the hand travels 3 cm toward or away before any
click, so a still grip followed by a trigger click stays a native Shift-click;
a trigger squeezed while moving does nothing until released. The laser and
clicks follow the moved panel. Every in-game and main/title menu reopens at
that distance until the game is restarted or **VR Settings > HUD > Menu
distance** is changed. The handheld quest log keeps its left-grip placement.

The VR Settings panel is grabbed like a real window with the same right grip:
the point you grabbed stays on the end of the laser. Moving the hand without
turning it slides the panel 1:1 (up, down, left, right, nearer, farther);
tilting the controller up/down or turning it left/right swings the panel
around your hand by the same angle, but it never rolls. Tilt stops at 60
degrees and the panel stays 0.40 m to 1.50 m from your head.
It reopens in the same place relative to where you face until restart.

Sticks/face buttons select native menu navigation without an idle laser stealing
focus. Release native controls and deliberately move the ray across the surface
to return to laser input. Dragging retains pointer ownership. All player-facing
Quest and Frame mappings are in [CONTROLS.md](../CONTROLS.md).

For the native quick-drop shortcut, point at an inventory item, hold **Left
Trigger**, then click **Right Trigger** (Ctrl-click). With inventory alone this
requests a ground drop; vendor/stash contexts instead sell/transfer. The modifier
is wired; this particular action still needs headset verification.

D2R handles all moves, equips, purchases, sales, prices and confirmation dialogs.
The menu button opens the native tabbed container directly; inventory, character,
skills and other pages are selected through its tabs. Back is no longer needed
to reach that container from a detached inventory page. If the game starts in
keyboard/mouse mode, the same button requests controller mode first and waits
for the native layout switch before opening the tabs; no thumbstick click is
required.
No item or save data is edited by this presentation layer. Keep the game window
focused. Movement and combat input are suspended while a panel is open; the world
continues rendering and follows the game's own pause rules. A trigger held on
entry must be released before clicking. Briefly missing the surface during a
drag retains the held button; tracking/focus loss releases it. If native UI
capture or conversion is unavailable, the existing flat menu is retained.

Laser input retains an already active controller layout. Native UI pointer
events and hover updates temporarily use mouse widget behavior on the dispatch
thread; rendering and nested panel-open messages keep the actual input mode.
All laser UI events are consumed, including clicks on empty panel space. A
separate map-click guard blocks gameplay clicks while menus own input. Queued
button releases stay protected briefly after a menu closes; ownership expires
after VR relinquishes the pointer. The adapter requires matching native input
signatures. Desktop input outside VR retains automatic layout switching.
Native item operations with this routing still need an in-game check. The SDR
UI texture is converted from sRGB to linear light with coverage preserved for
the XR panel. Its original resolution is retained. A verified final composite
uses only the world image in both eyes while the shared menu layer is visible;
the native UI texture is not cleared underneath the game's compositor.

The build and GPU conversion/input tests cover this implementation. First
in-headset checks of menu placement, tooltips and native item operations remain
pending.

### In-headset VR settings

The panel opens in front of the headset and stays in room space. It has nine
tabs and a **Reset eye height** button:

| Tab | Contents |
|---|---|
| Controls | Left/right controller reference for movement, menus, melee, bow, spells, potions, holsters and height reset |
| VR | Controller labels, motion combat, seated belt position, dominant hand, smooth/snap turning, belt height and reach, weapon skill gestures and ordinary mob bars |
| Video | Added sky, fog and dungeon ceilings, and the experimental Sorceress elemental hand effects (Off by default) |
| HUD | Health/mana location and tilt, optional resource alerts, navigation location, menu distance and tooltip timing |
| Combat | Melee rules, swing hit speed and thrust speed |
| Aim | Whether spells and bow skills go to the aimed monster, the staff aiming where your arm points, and the aimed monster's outline: strength, which hands show it, and each hand's color (by range or one hue), where ground skills land (the game's aim, where you point or the aimed monster), and landing circles. Every option that changes how a cast aims is Off (Game) by default |
| Haptics | Overall strength and separate melee, bow draw, bow/throw release, spell, interaction and resource-alert strengths; tests follow each action's hands |
| Perf | Everything that changes the frame rate: stereo rendering (Both eyes / By turns, By turns by default), VR render resolution (Headset by default), terrain and object draw distances, foveation Off / Fixed / Eye tracked and its strength, ReShade depth (after a restart) and desktop one eye; the actual VR source size, a Defer GPU wait button while frames wait for the GPU, and warnings while the game's Vertical Sync is on or its anti-aliasing is not FXAA. Under them, each second's frame rate and where the frame's time goes, whether the CPU's part fits the headset's frame, and the game's own graphics options |
| Notes | The last published release's notes, read-only |

Pages show only the rows that can apply to the character the panel is open
for. Another class's rows are left out:
- elemental hand effects are the Sorceress's;
- Ground skills and Landing circles are for the Sorceress, Assassin, Druid and Necromancer;
- the bow path laser is the Amazon's, or any class's with a bow in hand;
- staff aim is the Sorceress's and Druid's, or any class's with a staff in hand.

**Skills go to the aimed monster** offers Spells, Bow skills or Both to an
Amazon. Other classes see **Spells go to the aimed monster**, On or Off, and
keep the Amazon's bow skills choice. Rows that only matter with another
row's choice also stay hidden until they do:
- the outline's highlights and hand colors while Monster outline is Off;
- Landing circles while Ground skills is Game;
- foveation strength while foveated rendering is Off;
- the snap angle or smooth turn speed of the turning mode not chosen;
- the standing belt drop while seated;
- the face wipe without blood on the visor.

Settings that belong together share a row, with a stepper each: **Hand colors**
(left hand, right hand) and **Guard strain** (color, buildup). The title and
character screens show every class's rows.

Added indoor fog defaults to **Off** for new profiles, preserving the game's
own lighting and effects, including Tyrael's wings. Outdoor fog keeps its
existing setting. There is currently no indoor-fog toggle in VR Settings.
Existing saved `[fog] caves` choices are preserved when updating.

**VR Settings > Haptics** tunes vibration for different controllers. Each strength
ranges from **0% (Off) to 200%**, in **10% steps**, with **100%** as the default.
Overall strength multiplies the category strength: 50% overall and 50% bow draw
produce 25% of the original draw amplitude. The controller's maximum amplitude
still limits strong pulses. **Short melee pulses** is **Off by default**: weapon
impacts use a 90 ms tap to make contact easier to feel during fast swings;
shield impacts retain their 45 ms tap. Both use the controller's preferred
frequency. Turn it On for the original 10 ms / 180 Hz tap on either hand.
The **Weapon** and **Shield** tests use the same choice, including unsaved edits.

Tests use the displayed, unsaved values and follow gameplay hand ownership:
**Weapon** tests the right hand, **Shield** the left; bow draw tests both hands
with the normal asymmetric pull pulses. **Bow** release tests both hands and
**Throw** only the right. Spells and interactions retain **Test L / Test R**
because either hand can perform them. Resource alerts have one **Test** on the
left hand. A test is a sample, not a simulated gameplay action.
**Apply** saves and updates gameplay feedback; **Close** discards unsaved edits.
Muting a category stops its active vibration; Overall Off stops both controllers.
Bow draw covers string proximity, nocking, pulling and full draw. Bow/throw release
covers shooting and throwing. Melee covers weapon and shield impacts. Interactions
cover loot, potions, holsters and menu feedback. Resource alerts still require
**HUD > Low health / mana alerts** to be enabled for automatic warnings.

Supported independent hand spells now give one brief pulse on the casting hand
after a spell successfully creates its projectiles. A volley gives one pulse,
and a refused cast gives none. Feedback is discarded when stale, inactive or from
another character session. The **Spell casts** setting controls this pulse.
Settings are stored under `[haptics]` as `strength`, `melee`, `bow_draw`,
`bow_release`, `spells`, `interactions` and `alerts`; missing strength keys default
to 100. `[haptics] short_melee=0` selects the longer tap; `1` selects the short tap.

Click **Apply** to save changed values to `mods/D2R-PCVR/d2rloader/plugins/d2r_pcvr.ini` and
reload them live. Closing without Apply discards pending edits. The first Apply
creates `d2r_pcvr.ini.before-vr-menu`; later saves retain that original backup.
Unrelated INI entries are preserved. A conflicting external edit reloads the
current values instead of overwriting them. Reset eye height is immediate and
does not require Apply. The controls tab is a reference, not a binding editor.

Movement and combat inputs are blocked while the panel is open; **the world
continues running**. Release held triggers/grips before resuming gameplay. The
settings laser never sends a native game click. The panel remains accessible
when motion combat is off and from the title/character screen while XR is active.

Eye-tracked shading requires runtime support, permission and valid gaze. Missing
gaze uses full quality; unsupported VRS also renders at full quality. The menu
does not change runtime privacy permissions. DLSS, textures and shadows stay in
the game's **Options → Video**. **VR render resolution** selects the game's
source size independently of the physical monitor. See [resolution settings
and testing notes](VR_RESOLUTION.md). There is no control rebinding here.

First headset check: open/close from the shortcut, then from the wheel; visit all
six tabs and reset height; cancel an edit and confirm it is discarded; Apply
a turn-mode change and confirm it persists on reopening; disable motion combat
and reopen with the shortcut; switch foveation off/fixed/eye tracked and check
clarity and gaze status. Confirm no menu click attacks, drinks or swaps a weapon,
including a held trigger across closing or tracking loss. Also check native
inventory/journal controls after closing. Synthetic previews and automated
tests do not establish headset readability, laser alignment or stereo behavior.

To assign skills without a gamepad, hold **Left Y**, point at **EDIT SKILLS**
and click the right trigger. Choose any of the eight wheel slots, then choose
an available skill from the icon list. **PREVIOUS/NEXT** changes pages;
**CLEAR SLOT** removes the chosen binding. Keep Y held during editing, release
the trigger between clicks, then use **DONE** or release Y when finished.
Right B cancels. Editing blocks movement and combat, and choosing a skill does
not cast it. Only a confirmed native readback shows **Assigned**.

The catalog contains the character's learned and item-granted active abilities;
passives and unlearned skills are excluded. The Town Portal scroll/tome sources
use the game's combined controller shortcut. Edits target the current native
weapon-set binding bank. D2R applies its normal duplicate-slot rules (assigning
the same skill can clear its previous slot) and saves bindings normally.
If equipment, session or the selected slot changes before applying, the edit is
refused; choose the slot again. Headset assignment/persistence checks remain pending.

The wheel uses the eight existing native controller assignments: **B, X, Y, RB**
and four **LT +** variants. The wheel reads saved controller bindings even after
mouse/keyboard menu input; assigned icons begin loading before the wheel opens.
The native binding stays visible above every skill icon, including while editing.
All four **LT +** slots can be edited with the same slot → skill picker; the skill
picker title names the chosen native binding. The native controller UI can also
assign these slots. Names come from the game; unreadable assignments show Unknown. A known
unassigned slot is disabled. A lit slot indicates a present assignment, not a
promise that mana, charges or cooldown permit casting. Native spell icons come
from the local game cache; charge/cooldown counts remain pending. Slots run
clockwise from the upper-right segment. Equipment icons follow the live weapon
sets and next potion in each belt column, including potion tiers. Empty slots
show no item icon. The generated stone-and-bronze frame supplies background and
selection highlights; native icons keep their original art. Selecting closes
the wheel and latches it until Y is released;
release the selection trigger before casting. **BASIC ATTACK** restores the basic
right-trigger attack or physical bow controls. Free melee swings remain
available with a skill selected, pausing during casting, menus or potion handling.

There is no left-trigger quick-cast layer. Each hand casts its own wheel
skills, aimed with that hand: its trigger, its two face buttons, and the same
hand's grip plus a face button for two more. Hold the input for the native
repeat/channel behavior; release it to stop. The slots, the native gamepad
bindings behind them and what happens when both hands cast at once are in
[hand skill layouts](../CONTROLS.md#hand-skill-layouts).

Equipping a different melee weapon or bow selects weapon mode. Javelins,
throwing knives and throwing axes swing with the right trigger released and
throw by hand with it held (see the controls table above).
Full 3D spell projectiles, reliable terrain-aware destinations and spell-family
adapters remain pending behind the same simple point-and-trigger control.

The belt displays the native next item in each column using its original 3D
model and mipmapped texture, including all five healing/mana tiers and both
rejuvenation tiers. Stock it through the native Inventory panel. Bigger equipped
belts supply extra rows behind the same four grab positions, up to four items
per column; the count includes those reserves. The next native item appears
after consumption. The wheel uses/grabs existing contents rather than assigning
items. Bottles stay upright at the waist and follow the held grip
in six degrees of freedom. A 90-degree local pitch correction keeps grabbed
bottles upright in either hand; drinking checks use the same corrected opening.
Reach highlights the nearest occupied slot; a grab
moves that bottle off the belt into the hand without consuming it. Empty columns
have no bottle. A refill/reorder cancels an unspent grab. The held model keeps its
captured identity even after automatic refill. It darkens only after the
native item disappears. A request, grab haptic or mouse click is not proof of
consumption. A refused attempt never automatically retries. Confirm HP/mana and
belt changes in the first test. Success feedback is brief and a grab consumes
at most once. Weapon swaps similarly wait for the observed equipment set.

A Health, Rejuvenation, Antidote or Thawing bottle released with a deliberate throwing motion,
away from its belt slot, flies on a ballistic arc and lands on the world floor.
Its launch velocity is 1:1 with the hand: the fastest controller velocity the
OpenXR runtime reported in the last 0.12 s before the grip opened (pose
differences only when the runtime reports none), capped at 20 m/s. Once released
it is in the world: its arc is integrated in world metres from the release frame,
against the floor height under the player at release, so walking, turning or
recentering afterwards never carries it along.

The landing point is checked on the game thread against the live, non-hostile
hireling. Within 1.5 m of its feet, the UI thread presses the column's native
belt key while the belt's own "give to mercenary?" query (the check Shift+belt
uses) answers yes for that one press only, so the native mercenary use, voice
line and rules apply. It is confirmed by the item leaving the belt. If that
query cannot be answered (hook unavailable or its native gate set) nothing is
pressed, so a throw never becomes your drink. A miss, a dead or absent
mercenary or a changed belt sends nothing and the potion stays belted. Every
landing plays the game's own glass-break sound (`object_glass_break_1`, whose
native group picks one of three variants and its HD version) at the player, and
a confirmed feed plays the game's own `healing` overlay (overlay.txt row 26) on
the merc through the native set-overlay call. Slow releases still return the
bottle; mana and stamina potions are never thrown. The log records each
landing's hireling class and distance (`Frame potion toss:`).

**Emptied bottles.** Letting go of a bottle you have drunk from no longer makes
it vanish: it leaves the hand with the hand's speed and turn and falls into the
world. It bounces and rolls on the floor under the player and stops at the
game's walls and closed doors near it, as a [Box3D](https://github.com/erincatto/box3d)
rigid body (a cylinder the model's height and width). Up to six lie in the
world; each goes after 20 seconds, or once you are 12 m away. This is purely
visual. Nothing is sent to the game, and an emptied bottle has no item behind
it. The floor is the one under you when you let go, flat like the floor
thrown potions land on, so stairs and slopes are not followed. Walls are the
game's wall cells (the same missile-barrier test as physical arrows), sampled
on the game thread around the bottle every half second and standing 6 m high.
Like the belt, the bottles draw over the scene with their own depth, so
monsters and walls do not hide them. Swords, other melee weapons, fists and
the shield knock them about: while physical melee is active, the same posed
capsule it tests creatures with moves through the physics world, pushing the
bottles it meets, and a hit gives that hand a light tick (the melee haptic
strength applies). Slow pushes roll a bottle; a hard swing sends it flying.
The shield uses a thinner capsule than its creature test. A warp or recentre
moves a weapon without sweeping it through bottles. `[interactions]
bottle_physics=0` restores the old vanishing bottle. The log notes each release (`Frame dropped bottles:`)
and wall sampling every ten seconds while bottles are in the world.

**Knockable props.** The level's own small props within 3 m (cups, plates,
bowls, vases, jugs, bottles, books, skulls, baskets, pails, stools, chairs,
barrels, sacks and about 80 more kinds) join the same physics world. Each stays
where the game placed it until a weapon, fist or shield passes within a
centimetre of it (a hand with no weapon, including the off hand, pushes as a
fist), or a dropped bottle or another prop reaches it. It then falls, bounces
and rolls, and freezes again where it rests. The game draws its own model
there, with its shadow, until the area loads again. The installer catalogues
these props from your game files (`d2r_vr_props`), and the plugin matches each
one by its level-preset id and position, so nothing else is ever moved. Tables
and shelves are their bounding boxes: a prop pushed off a shelf's back falls
through the cabinet. Inside a room or building (a mantle or ledge that is part
of it), the surface ends 3 cm past the row of props on it. A prop on another
prop (a cup on a plate) falls when that one is knocked away. At most 12 move at
once; a new weapon hit then freezes the one moving longest. This is purely
visual. `[interactions] prop_physics=0` turns it off; see [knockable
props](../docs/KNOCKABLE_PROPS.md). The log notes each weapon knock (`Frame
props: knocked a cup ...`) and a summary every ten seconds.

**Hurt merc.** During VR gameplay the local hireling's life is sampled on the
game thread every 200 ms with the same native stat call as the wrist vitals.
Below 50% life it is tinted with the game's own `red` state (states.txt row 91,
a colour shift with no stats) through the native toggle-state call, so the game
syncs it like any other state. The game shows only the highest-priority tint
(cold 100 and poison 95 would hide red's 70), so a hook on the native colour
update applies red's own colour shift last, only on a hireling that has it; a
chilled or poisoned hurt merc still shows red. The tint clears on recovery, on death (the corpse
too), for a rehired or resurrected merc, and within about 1.5 s of leaving VR
gameplay; desktop and flat play never sample or tint. The log records each
change (`Frame merc:`).

The 18 cm bottle's opening must be within 10.5 cm of the mouth while tipped for
0.25 seconds; bringing only the controller center to the face does not count.
Release grip to return an unused bottle. The installer extracts the models
locally; if the cache cannot load, labeled flat previews remain available.
Bottles render as stereo first-person geometry with their own depth buffer;
world/arm occlusion, transparent liquid simulation and native scene lighting
are not implemented in this pass. The physical interaction awaits headset testing.

Holsters represent complete native sets I/II, including shields and quivers;
they do not move arbitrary inventory items. Native set inference still requires
a test with empty/identical sets. Item labels currently show base item codes.
Hands carrying a bottle/book hide the associated weapon; stow hides both.
These are presentation changes only. Combat is suspended while an interaction
owns input, and resumes after release.

Optional live configuration (metres below/forward/from the estimated torso):

```ini
[interactions]
seated=0
navigation_location=0
vitals_location=0
vitals_tilt=0
critical_alerts=0
waist_below_eyes=0.65
belt_forward=0.26
hip_width=0.28
holsters=1
bottle_physics=1
prop_physics=1
```

`vitals_location` and `navigation_location`: `0` Wrist, `1` Palm, `2` Either,
`3` Off. Older profiles without `navigation_location` start at Wrist if either
legacy `wrist_map` or `wrist_compass` is enabled, or Off if both are disabled.
An explicit navigation location takes precedence over those legacy switches.
`vitals_tilt` is the health/mana/XP panel's tilt offset in degrees, clamped to
`-60` through `60`; a missing or nonfinite value uses `0`.

Seated mode raises the waist anchors. The belt and hip holsters follow tracked
position and horizontal facing every frame, including while hands are lowered
or holding a bottle. Pitch/roll do not tilt the belt; a nearly vertical gaze
retains the last reliable heading. A held bottle follows its hand and returns to
the current waist position when released unused. Holster pulls are measured
relative to the body so turning/stepping with a hand at the hip does not draw a
weapon. The body frame uses the headset; there is no independent torso tracker
or free-form anchor editor. The journal uses a UI-only copy rather than capturing the world
inside its page. If the UI layer/rectangle/format/copy is unavailable it retains
the ordinary menu surface. Only the quest page is spatial in this candidate;
other menus retain their existing panel. Compositor quads have no scenery/hand
occlusion and the bottle art is a functional placeholder.

### Interaction candidate test order

1. Confirm the installed bow-height correction separately if desired; the new
   candidate includes it too. Keep `free-swing4-20261009-023651` as the verified
   melee rollback. Do not overwrite an active game DLL.
2. Check wheel names against native assignments, select a skill, and keep the
   trigger held: selecting must not cast. Release, then point/press to cast.
3. Use utility set I/II and then grip/pull each hip holster. Confirm the native
   equipment and set label, including an empty set. Check stow/draw.
4. Grab/cancel a bottle; then drink with room for healing/mana. Confirm one item
   consumed, correct next bottle/count after refill, and no accidental attack.
   Try quick-use and a refused/full-health potion without an automatic retry.
5. Check wrist values against the native character screen. Test seated reach if
   needed. Brief warning cues should not become a permanent HUD.
6. Open Journal, turn your head, use a native act tab, pin/regrab, close/reopen.
   The world must remain stereo and clicks must land on the visible page.
   Keep the desktop game focused for injected pointer clicks.
7. Regression: inventory, creature labels, melee, bow, recenter, tracking loss
   and return. No held input should cause a delayed drink, swap, cast or hit.


## Motion weapon skills and shield attachment (2026-10-11 candidate)

Select a supported skill on the wheel, release the selection trigger, then use
the weapon's familiar motion. The gold **ACTIVE** border/badge identifies the
persistent selection independently of the hovered wedge. Weapon-side indicators
are off by default; enable **VR Settings > Controls > Weapon skill indicators**
and Apply to restore them. When enabled with the wheel closed, a small native
skill icon follows the weapon hand. Its name and action hint
appear briefly on change, or while busy/unavailable. Quick casting temporarily
shows its own icon and **QUICK CAST** cue, then restores the persistent choice.
These indicators show assignments and observed actions, not invented mana or
cooldown readiness.

For bows, supported skills are **Magic Arrow, Fire Arrow, Cold Arrow, Multiple
Shot, Exploding Arrow, Ice Arrow, Guided Arrow, Strafe, Immolation Arrow and
Freezing Arrow**. One deliberate draw/release sends one short native skill-button
press. Multiple Shot keeps the game's fan, Strafe keeps its automatic targeting,
volley count and sequence, and elemental/Guided arrows keep native effects and
homing. Drawing farther does not multiply damage or arrow count. A fresh trigger
press away from the string keeps point-and-trigger casting; moving into the
string while that press is held cannot also nock an arrow. Quick casting remains
point-and-trigger. Unknown skills and crossbows retain their native fallback.

Selected bow skills retain **native animation, ground-plane flight and collision**.
The ordinary basic-arrow 3D path described below does not yet handle these special
missiles. Draw/release is implemented as their input adapter, not a new 3D volley
simulation. Native mana, ammunition, charges and attack cadence remain in charge;
a rejected input cannot also fire a basic arrow. Strafe's busy cue follows an
observed native sequence after a physical release; it does not estimate readiness.

The equipped left shield now follows its authored bind-pose relationship to the
tracked wrist, including wrist rotation, instead of an attack/walk animation's
attachment or the main weapon's adjustment. In native first-person VR, reaching
beyond the model's normal arm length extends the shield arm to follow the
controller. The palm, fingers and shield retain their size; bringing the hand
back restores normal arm proportions. Extension is bounded at twice the arm's
current length for extreme tracking/calibration errors. Contact uses the same
final shield pose. The owner verified this adjustment in the combined developer
build; individual class/shield combinations were not specified.
With **Smite** selected, an energetic
left-arm swing or thrust that intersects an eligible creature requests one native
Smite against that creature. A stationary shield cannot bash; one continuous
stroke requests at most one bash, and a deliberate reversal/settle rearms it.
**VR Settings > Combat > Shield bash speed** controls this gesture in both melee
modes (default 2 m/s; 0.75-10 m/s in 0.25 steps; `[gameplay] shield_bash_speed`).
A bash needs 12 cm of net tracked hand travel within 200 ms and must meet the
selected speed at contact. Wrist rotation alone cannot arm, power or rearm a bash;
shield size therefore cannot amplify wrist twisting into an attack. The contact
sweep covers only the current fast interval, not earlier slow approaches.
Applying a new bash speed invalidates old queued/posed contacts. Native skill
selection, mana costs, trigger casts, blocking and parries are unchanged.
Native reach, walls, resources, animation/cadence, damage, stun and knockback
remain authoritative. Left-hand haptics acknowledge native acceptance, not proof
of positive damage. Physical blocking and parrying are described under melee rules below.

Contact casting activates only when the native Smite implementation is identified
from the audited attack/throw dispatch table, an exact bounded entry in the game
image's exception directory and a decoded call to the native target accessor. The
loader's OS unwind lookup did not validate that entry in the first installed build,
silently disabling physical Smite. Discovery now reads the game directory directly.
If activation fails, an expanded **TRIGGER ONLY** cue explains that shield bashing
is unavailable; the native trigger fallback remains available. Tracking loss,
menus, recentering, equipment/assignment changes and quick casting cancel physical
preparation and invalidate its target. Shield collision uses an approximate capsule
around the attachment; fit across shield shapes and detailed contact behavior still
need headset checks. Logs separately report hook/pose/input readiness, energetic
shield sweeps, cast requests and native completion/refusal.

Windows build, gesture/contact/pose regressions and the UI preview pass (39 checks).
The owner confirmed ACTIVE/Smite cues and reported failed physical Smite in the
first build. The corrected hook activates in live Steam 3.3.93787 and experimental
Battle.net 3.3.93847 main-menu runs; no character was entered for those checks.
On 2026-10-12 the owner verified physical Paladin shield bash working with Steam
Frame / SteamVR streaming on Steam 3.3.93787. The direct-launch session logged 15
contact cast requests and 14 native completions, with zero reported native refusals,
between 14:26 and 14:29 PDT. Completion counters do not measure damage; the working
combat result is owner-confirmed. The Steam library shortcut crashed before plugin
loading, so this test used a direct SteamVR launch. Remaining offline checks outside
town:

1. Amazon: compare Multiple Shot's arrow count/ammo/mana and Strafe's volley/cadence
   with native trigger casts; test elemental and Guided effects, repeated draws,
   short/cancelled draws, empty quiver and insufficient mana.
2. Paladin: confirm shield fit while translating/rotating the wrist, then test
   Smite swings, thrusts, backhands, stationary overlap, misses and walls. Compare
   costs, damage, stun and knockback with trigger Smite; check one bash per stroke.
3. Change assignments and weapon sets, open menus/settings, recenter and interrupt
   tracking during preparation and native wind-up. Check for delayed/duplicate
   actions, stuck skill buttons and correct recovery.
4. Check ACTIVE versus hover, closed-wheel icon/readability and temporary quick-cast
   restoration. Recheck ordinary 3D arrows, free sword swings, inventory and frame
   pacing. Repeat shield-fit checks with several shield shapes.

## What is physical and what is still native

Hand/weapon presentation and input gestures use tracked 6DoF poses. The bow
aim uses the line from the drawing hand through the bow hand. Release freezes
its full position and direction in world space; later head/hand motion cannot
steer an arrow already in flight.

Free-swing melee replaces gesture-to-button attacks. It sweeps a capsule on the
final posed weapon attachment between frames, tests creature body capsules, and
invokes native basic-attack damage for the actual contacted unit on the loader's
authoritative game thread. No controller A press, selected target or attack
animation delay is involved. A contact can hit each creature once per stroke;
settling or a deliberate translation/rotation reversal permits another stroke.
A tracking discontinuity drops that motion and requires 80 ms of fresh coherent
poses; continued swinging can recover without holding the hand still. Strength
is judged over recent motion, so small windups cannot lock out later slashes. Swing
contacts require a coherent 18 cm excursion within 120 ms and at least 12.0 m/s of
estimated motion at contact, including follow-through. The estimate combines hand
translation with blade-axis rotation, capped by tracked hand rotation; twisting a
shaft in place does not power a hit. The sweep follows the rotating blade’s arc
between the latest poses without replaying earlier unpowered brushes when a
stroke arms. Shield/Smite uses its separate hand-travel threshold below. Swing speed
is adjustable under **VR Settings > Combat > Melee hit speed**
(minimum 1.5 m/s, no upper cap, 0.25 steps, default 12.0). Apply saves
`[gameplay] melee_hit_speed` and updates the detector live; changing it clears
the previous stroke. Swing travel remains 18 cm.

**Thrusts** use a separate forward-motion check in both Unleashed and Classic.
Push toward the posed weapon tip for at least 12 cm within 200 ms, with at least
85% of the hand's travel along that axis. Default forward speed is 2.0 m/s,
adjustable under **VR Settings > Combat > Melee thrust speed** from 0.75 to
10 m/s in 0.25 steps (`[gameplay] melee_thrust_speed`). Apply updates it live
and cancels the previous stroke without changing the swing-speed preference.
The axis comes from the final weapon attachment and its matching tracked grip,
including weapon adjustments and two-hand posing; controller-forward alone is
not assumed to point along the blade. Contact uses the latest swept capsule,
without replaying earlier low-speed brushes. Holding or slowly advancing a
blade inside an enemy does not keep dealing damage. Pulling back at least 8 cm
rearms a quick jab without counting the withdrawal as another stab. Thrusts
share the same per-creature stroke deduplication and Classic hit/cadence rules
as swings. Automated traces cover 36–144 Hz, dagger/sword/spear lengths,
repeated jabs, pullbacks, jitter, settings changes and invalid axes; physical
headset testing of the new thrust detector remains pending.

Physical controller translation/rotation supplies energy, so walking or turning a stationary weapon
into an enemy does not cause damage. Right trigger remains an explicit native
button fallback and suppresses simultaneous physical contact attacks.

Confirmed weapon impacts request a single 90 ms haptic tap; shield impacts keep
their 45 ms tap. Both use 95% base amplitude and the runtime's preferred
frequency on the next XR input update.
**VR Settings > Haptics > Short melee pulses** opts into a 10 ms / 180 Hz tap;
it is Off by default, and both melee Test and real impacts use the selected pulse.
Overall and Melee impacts strength scale its amplitude. Events are consumed once
rather than held during overlap. A dedicated timer
explicitly stops the vibration at its deadline even if rendering stalls; pulse
replacement, focus loss and session shutdown cancel pending stops safely.
Weaker incidental cues cannot replace a still-active melee pulse. New impacts,
stronger cues and deliberate settings previews can replace it; mute and explicit
cancellation still stop it immediately.
Weapon hits target the right controller; shield bashes target the left. Each
controller has its own OpenXR vibration action and binding, shared by pulse
start and stop, to avoid depending on a shared action's hand filtering.
While motion controls supply focused VR input, the mod suppresses the game's
XInput pad-0 rumble (including native stop requests) so it cannot compete with
OpenXR feedback. Those gamepad motor channels represent low/high frequency,
not VR hand ownership. An existing desktop rumble is cleared once on takeover;
desktop play, other gamepads and expired VR input pass through normally. The
game's saved rumble preference is unchanged. Native rumble suppression and
weapon/shield output requests are logged to distinguish overlapping feedback
from reversed controller routing. Physical confirmation remains necessary.
Exact feel and frequency support depend on the controller/runtime and need
headset validation. Existing saved melee speed choices remain unchanged.

Collision volumes are currently **approximations**, not skinned mesh hitboxes:
weapon category determines length/radius on the final attachment; native monster
footprint and legacy artwork height determine its body capsule. Generic class
support is implemented, but specific weapon axes, small creatures, bosses and
sloped ground need headset checks. Basic contacts use the main hand; the candidate
adds selected Smite shield contacts. Barbarians with two melee weapons and
Assassins with two claws also support independent left/right physical swings
and thrusts; see
[physical dual wield](../docs/BARBARIAN_DUAL_WIELD.md). Physical shield blocks
and parries are below; other classes' off-hand weapon strikes remain future work.
Dual-claw contacts are basic attacks, not automatic Dragon Claw or charge-up
skill casts. Release both triggers for independent left/right physical contacts.

Physical weapon contacts also break attackable pots, urns, baskets and barrels
in either melee mode, including with either Barbarian weapon or Assassin claw.
The game handles loot, traps and explosions; ordinary button interaction/kicking remains
available. These contacts do not consume creature hit/recovery/cleave state.
See [physical breakables](../docs/PHYSICAL_BREAKABLES.md) for scope and validation.

Only a confirmed physical contact overrides legacy melee grid reach. Native
line-of-sight, hostile relationship, creature life state, damage, elemental
resistances/immunities, criticals, leech, procs, durability, death and XP still run.
**Melee rules** (VR Settings > Combat, `[gameplay] melee_rules`, applied live,
mid-game) choose between two modes for physical swings and trigger/button melee:

- **Unleashed** (`0`, the default): every weapon contact strikes each
  creature once per stroke. No to-hit, block or dodge roll and no attack-speed
  gate (the `contact_hits` policy). Instead, attack rating and attack speed scale
  how hard a physical swing lands:
  - **Attack rating** (`ar_damage`, 1): damage x the native to-hit chance (5-95%,
    attack rating, defense and level penalty), read where the game's hit check
    passes it on. `ar_damage_floor` (0) keeps that % of damage at zero chance.
  - **Recovery** (`swing_recovery_damage`, 1): damage x the share of the native
    attack cycle (class, weapon, IAS, WSM) since the previous landed stroke, to
    the power `swing_recovery_curve` (2), at most 1. With 2, flailing at twice
    the cycle's rate deals half the damage per second (1 would be neutral).
    `swing_recovery_grace` (100 = none) counts a stroke from that % of the cycle
    as fully recovered. Every creature one stroke reaches shares the stroke's
    recovery factor, so flailing does not pay.
  - **Cleave falloff** (`cleave_falloff`, 25): each further creature one stroke
    lands on deals that % less than the one before, never below `cleave_min` (25):
    100%, 75%, 50%, 25%, 25%... 0 turns it off. Creatures count in the order
    the game resolves them, which follows the blade frame to frame but not
    within a single frame. A riposte is still a clean hit (no falloff) and
    counts as one of the stroke's creatures.
  Leech, crushing blow and other on-hit effects still run on the unscaled hit.
  `Frame swing damage:` in the log reports scaled hits and the last factors.
- **Classic** (`1`): native attack rating, monster defense, block and the
  level penalty decide each attack, and attack speed paces them (below). A rolled
  miss uses up the attack, as a missed swing animation would.

Missile hit checks (arrows, missile spells) keep `contact_hits` in both modes.
In Classic, button melee keeps native monster block only where the game checks
block inside its hit check; `native block/dodge in melee` in `Frame combat:`
confirms it. Every weapon contact with a hostile creature taps the controller once
per stroke and creature, whether the attack then lands, misses its roll or is
refused by attack speed; damage, not the tap, reports the outcome.

**Attack speed** (Classic melee rules) restores the basic
attack's native pace without its animation. A resolved attack, hit or rolled
miss, starts the character's normal-attack cycle; contacts before it ends are
refused, and a stroke attacks only one creature. The cycle uses D2's own formula (`attack_speed.h`): class and
weapon class (Weapons.txt `wclass`) set the animation length, item IAS reduces
it with the usual diminishing returns, and the weapon's speed modifier (WSM)
lengthens or shortens it, so IAS breakpoints apply to swinging as they would to
clicking. Whiffs, refused contacts and native rejections never start the cycle.
`missed` in `Frame melee:` counts attacks whose to-hit roll failed.
Trigger/button melee shares the cycle: each of its native melee hit checks
starts it (`button_cycles`), so alternating trigger attacks with swings is no
faster than swinging. A trigger attack started right after a swing is still
paced only by its own native animation. This needs the combat hooks, which
`contact_hits=1` (the default) installs.
Skill speed (Fanaticism, Frenzy, Burst of Speed, werewolf), slows and dual-wield
attack sequences are not read yet. An unknown class or weapon class (including
classes outside the original seven) leaves the cycle unknown, which does not
gate hits. Unleashed restores unlimited free-swing contacts. The log
reports the cycle inputs (`Frame melee attack speed:`) and refused contacts
(`too_soon`, `extra_targets` in `Frame melee:`). `[debug] melee_trace=1` adds one
`Frame melee trace:` line per native attack and per refused stroke, with the
time since the previous landed hit; landed hits are never closer than `cycle`.

### Physical shield blocking, parry and riposte (Unleashed)

With **Unleashed** melee rules and a shield on, blocking is decided by where the
shield is held, not by the block-chance roll. A monster still has to pass its
native to-hit roll; then its native block roll (44B930) takes the physical verdict
instead of a random one. Classic keeps the native roll.

- **Block**: raise the shield's broad face between the attacker and your torso.
  Walking/running and looking away do not cancel a valid guard. Recognized held
  meshes use their current rendered bone bounds and native item transform, so
  wrist rotation and shield size change coverage. The projected face width has
  a 12 cm torso allowance; nearly edge-on faces are rejected. Height uses the
  visible face, allowing a lower grip on a tall shield. Unsupported meshes use
  the posed-attachment fallback. This approximates a native 2D incoming hit,
  not per-triangle projectile collision; see [shield blocking](../docs/SHIELD_BLOCKING.md).
  Every block plays `[gameplay] block_sound` (default 398, block_weapon_1) and
  taps the left controller. Native unblockable damage remains unblockable.
- **Parry**: a diagonal shield swing across the view, lower right to upper left for
  a left-hand shield (at least 15 cm within 120 ms, within ~35 degrees of the
  diagonal), starting in the attacker's downstroke and at most 450 ms
  (fixed) before the hit, from a melee attacker within 3 m (2026-10-18: tightened
  from a 750 ms window with a 450 ms lead so a parry is a timed answer, not a
  held-up guess). A parry swing whose window runs out unspent gives a soft, dull
  buzz on the shield hand (unlike a block's sharp tap; scaled by the Melee haptic
  strength), once per swing. A timed swing is judged before face
  coverage: the swing itself turns the face edge-on, so it needs only the raised
  shield out on the attacker's side of the eyes (it may finish up to 10 cm behind
  them along the attack line, e.g. beside the head after a big swing). A monster
  melee attack always lands (`enemies_always_hit`, default 1), so every attack is
  judged at its hit frame: parry, else block or hit by the shield's position. Only
  with `enemies_always_hit=0` can a monster attack miss its native to-hit roll; it
  then never reaches the block check;
  a timed swing still parries it at that roll (the native miss is unchanged, and
  there is no shield wear to restore). Low-level monsters miss most attacks
  against a stronger hero, which made correct swings look ignored (2026-10-18:
  two Zombies, 7 hits from dozens of attacks). One parry per swing.
  **Downstroke** (2026-10-19): resolving as soon as the swing and the attack had
  both begun staggered monsters that had barely raised their arms. The attack's
  start (mode change, 34E5F0) to its to-hit roll (the hit frame) is that
  monster type's wind-up, measured per class and attack mode from its last
  attack; the swing must start once `[gameplay] parry_downstroke` % of it has
  passed (default 90, from 60 then 80: parries still came before the blow visibly
  landed - swings centred 55 ms after the hit frame, 2026-10-19; 0 = anywhere
  after the attack began, the old rule). An
  earlier swing is a `windup` miss and only blocks. Until a type's wind-up has
  been measured once this session, its attacks are not staggered early and the
  parry is judged at the hit as below. **Parry at hit** (`[gameplay] parry_at_hit`,
  default 1, 2026-10-19): a downstroke swing resolves at the attacker's hit frame,
  when the blow makes contact: the parry deflects it (no damage, any wear it caused
  restored) and the attacker staggers. A swing that does not qualify is no parry;
  the shield's position at that moment alone decides block or hit, as below.
  Staggering at the swing itself fired 50-90 ms before the hit (Zombies: swing at
  281 ms, hit at 330-375 ms) and cut the blow off before it came down. With 0, a
  downstroke swing staggers the attacker on the next game update, before its hit frame.
  **Late parry** (`[gameplay] parry_late_ms`, default 200, max 400, 0 = off): the
  game's hit frame comes before the blow lands on screen - 15 of 24 swings
  (2026-10-19) started 0-156 ms after a blocked hit, timed to the animation. A
  parry swing within this long after a blocked melee hit (within 3 m) still parries
  it: the attacker staggers and the block's wear is restored; the block's strain
  stands. Act bosses are never parried late either (2026-10-20: the late parry
  had skipped the act-boss gate, so Andariel could be parried). A hit that landed on you is not undone. Logged as `Frame guard late parry:`
  and `late_parries` in `Frame guard:`; later swings stay `swing too late`. Hit-frame parries
  had come a median 516 ms after the swing (2026-10-18). An attack whose to-hit
  roll has already run is never staggered early; otherwise the to-hit roll or
  block check above still resolves the parry. The attacker is
  put into native hit recovery (445E60) on a later game update, any shield wear
  the game applied for that hit is restored (`shield_wear_refunded` in `Frame
  guard:` counts it; not every block wears a shield, and none has been observed
  yet in testing), and `parry_sound` plays (default 236, the Archon Plate equip
  sound).
- **Riposte**: the next physical weapon hit on the parried monster within
  `riposte_ms` (1500) is a clean hit - no attack-rating or recovery penalty - plus
  `riposte_bonus` % more damage (0), and plays a built-in
  impact sound (`steamvr/assets/riposte-impact.wav`; `riposte_sound_file` replaces
  it with a WAV next to the ini). Its volume follows the game's own Master x Combat
  sliders (re-read from Settings.json when the game saves it), times
  `riposte_volume` (100).
- **Guard strain light**: every blocked hit (not a parry) strains the guard by
  `guard_strain_per_block` % (10). **VR Settings > Combat > Guard strain** (right stepper: buildup)
  selects Slow / Medium / Fast (`guard_strain_speed` 0 / 1 / 2, default Medium).
  Slow halves strain per block; Fast doubles it. With default tuning, full
  strain takes about 20 / 10 / 5 blocks in a row. The
  vignette size, lowering/reset time and blocking rules stay the same. Existing
  custom `guard_strain_per_block` tuning is multiplied, not overwritten. Strain shows as a soft vignette at the
  rim of the view (a pale white light by default; VR Settings > Combat > **Guard strain
  color** switches it to a black shadow, `guard_strain_dark` 1, or hides it, `guard_strain_dark` 2
  (Off: no light, but strain, guard breaks and chip damage still apply); the black is drawn denser by
  `guard_strain_dark_density` (2.5) so it hides as much as the light, and
  `guard_strain_color` takes any RRGGBB hex, default FFF7EA), beneath the blood on
  the visor, creeping inward as it builds;
  each block sends one smooth pulse in from the edge. At full strain the guard
  breaks: the light swells until only `guard_strain_clear` % (10) of the view is
  clear and breathes slowly (`guard_strain_breathe_ms`, 1600), with one alert tap on
  the left controller. The shield keeps blocking throughout; the cost is sight.
  Strain holds while hits keep reaching the shield. Once no hit (block or parry)
  has reached it for `guard_strain_recover_ms` (1500; 0 = only lowering drains), it
  drains with the shield still up; the next hit stops the drain and restarts the
  wait. Lowering it (or a menu, or losing the shield) drains at once, without the
  wait. Either way full strain drains in `guard_strain_reset_ms` (1500), so a broken guard left alone clears 3 s after the last hit; a broken guard
  stays broken until the light has fully faded, and raising the shield again part
  way keeps what is left. Opacity is `guard_strain_light` (100 %, 0 = none; strain
  still runs). The light's inner blend is
  `guard_strain_softness` % of the visor radius (45) about a fixed midpoint, so it
  fades toward the centre with no visible inner rim. Pulses re-trigger without
  dropping, so rapid blocks hold a swell rather than flashing.
- **Low-life heartbeat** (Classic and Unleashed; guard strain no longer drives it):
  **VR Settings > Combat > Heartbeat starts at** (`low_life_heart_start`, 60 %; Off or
  10-90 % in 10 % steps) sets where it begins. Below that life the heartbeat steps up
  every 10 % lost, louder and faster, to full loudness and `low_life_heart_bpm_max`
  (115) below 10 %: a 50 % start gives five steps, at 50, 40, 30, 20 and 10 %. The
  tempo runs from `low_life_heart_bpm_min` (70); loudness starts at 12 % of full and
  rises with each step. Applied live. Wiping the visor clean (the last splat) fades
  the heartbeat out while still hurt; the next hit (the blood visor's own hit size)
  brings it straight back at its step, and healing to silence clears the wipe. A
  partial wipe leaves it beating. The beat itself keeps
  its natural speed and pitch; only the time between beats shortens. A heal winds it
  down no faster than `low_life_heart_fade_ms` (4000) from full: quieter and slower beat
  by beat, then silent; so does death or a loading screen. Loudness is
  `low_life_heartbeat` % (200, 0 = off) on top of the game's
  Master x Combat volume; above 70 % of full scale the beat bends smoothly toward
  full scale (a soft limiter) rather than clipping. The built-in beat is a recording embedded in the plugin
  (`steamvr/assets/guard-heartbeat.wav`); `low_life_heart_file` replaces it with a
  one-beat 16-bit PCM WAV, next to the ini unless a full path. It plays on its own
  audio stream, so it never cuts off the riposte or block sounds. The old
  `guard_strain_heart*` keys are still read when the matching `low_life_heart*` key
  is absent. The log's `Frame low-life heartbeat: started life=N%` line marks each start.
- **Shield wear**: the game wears armor only when its wearer is struck: each
  landed hit picks one armor piece by body-location weight (head 3, torso 5, left
  hand 4, belt, feet and gloves 2) and gives it a 10% chance to lose 1 durability
  (SUnitDmg 4526C0 / 441B10). A block never got there, so a raised shield never
  wore. A physical block gives the shield alone that native roll, and a chip
  through a broken guard has its struck wear given to the shield instead of a
  random piece, for `shield_block_wear` % of contacts (22: the shield's native
  share, 4 of 18 x 10% = 2.2% a contact; 100 = the full 10%; 0 = off). A parry wears nothing. Unblocked hits are
  untouched. Indestructible and broken handling are the game's own.
  In this setup the game updated only the server copy of a worn item, so
  tooltips showed the old durability until a relaunch (its own armor wear
  too). After every wear on the local player's gear the new value is copied
  to the client item, so tooltips follow at once.
- **Guard break**: the moment the guard breaks a bone snap plays
  (`steamvr/assets/guard-break-bone.wav`) on its own audio stream, at
  `guard_break_volume` % (100, 0 = off) of the game's Master x Combat volume, through
  the heartbeat's soft limiter; `guard_break_sound_file` replaces it with a 16-bit PCM
  WAV. While the guard is broken, a block lands as a **chip** (`guard_break_chip`, 1 in
  Unleashed): the hit lands natively (damage reduction, resists and absorb; armor
  wear; when-struck procs; the hero's hit sound) and its physical part is scaled by
  (1 - chance to block) x `guard_break_chip_scale` (1), on top of the Defense factor.
  Chance to block is the native rate, a third while running, capped at 75 %.
  Elemental damage and its effects (poison, chill...) land as normal. The block
  pulse, haptic and sound still play.

Damage is scaled at the native damage application's final life write (44CE80,
whose `Set(defender, life, ...)` returns to 44D098), so resistances, criticals and
leech have already run; only the local player's physical swings are scaled - not
spells, missiles or thorns.

**Incoming defense (Unleashed):** every monster melee attack and projectile aimed at
the local player lands - its to-hit roll always succeeds, so only the raised shield
(or the native block roll without one) stops it. Defense still counts: the native
chance the attack would have had (the monster's attack rating against your
Defense and level, clamped 5-95%) scales that attack's damage at the same life
write, so a 5% chance lands 5% of the hit. The whole attack is scaled, elemental
riders included (Cold/Lightning/Fire Enchanted, cold melee, poison arrows), and
after damage reduction, resistances and absorb, so expected damage equals native
without the hit-or-miss variance. Spells never roll to hit and are unchanged.
`[gameplay] enemies_always_hit` (default 1) turns the rule off with 0, restoring
native monster misses at full damage. Classic always uses the native monster
chance to hit. The log's `Frame incoming defense:` line counts scaled hits.
`[debug] guard_trace=1` logs every judged hit (`Frame guard trace:`) with the hand
position, cover angle, swing and attack timing. Its `parry=` field names why a
raised guard did not parry: `no_swing`, `spent`, `early` (more than 450 ms before
the hit), `before_attack`, `windup` (before the downstroke), `range` or `side`; `ok` on a parry. `downstroke_at=` is
when that attack's downstroke opened, in ms after it began (-1: wind-up not measured yet). `Frame guard swing: armed` marks each new parry swing,
`Frame guard miss:` each monster melee attack that missed its to-hit roll (and
whether a swing parried it), `Frame guard early parry:` each parry made before
the hit (`trigger=attack_start` or `swing`), and `Frame guard:` adds
`incoming_hits`, `incoming_misses`, `miss_parries`, `early_parries` and
`parry_misses` (parry swings that buzzed). Hard swings (a riposte straight
after a parry) are tracked up to 20 m/s and 100 rad/s; `Frame melee resets:` and
`Frame melee stale:` report any swing the tracker discarded, and
`Frame riposte: window expired unused` any parry whose riposte never landed.

Physical **basic-arrow** release queues one direct native basic attack on the authoritative
game thread. The attack creates its normal damage-bearing missile and handles
ammunition; no simulated A-button press or native animation wind-up is needed.
A client visual is allocated separately on the UI thread, without running a
second attack or spending another arrow. Each release carries a session,
equipment identity, recenter/turn epoch and 150 ms expiry; it cannot be replayed.

Basic arrows follow a straight 3D path from the string hand through the bow hand.
The complete tip segment is tested every native simulation tick, including the
initial shaft, so fast arrows cannot tunnel through body capsules. Native grid
barriers are checked along every crossed cell. The first contact is resolved
through the native missile hit handler, retaining damage, elemental effects,
criticals, leech, procs, immunity and pierce. Piercing cannot damage the same
creature twice. Native lifetime remains in charge; there is no damage multiplier
for drawing farther. Both eyes share the same flying-arrow transform per pair.

This first physical path supports **ordinary basic arrows**, not elemental,
explosive, magic or guided missile variants. A bow that converts its basic attack
to a special missile is currently refused before ammo is spent; use its assigned
native skill through the palette instead. Selected arrow skills gain the candidate
draw/release adapter above; their flight remains native. Other skills/spells
retain their existing controls, costs, timings and collision behavior.

**Bow path laser** (**VR Settings > Aim > Bow path laser**, `[bow] path_laser_hue`,
Off by default): at full draw, while a release would shoot a physical
arrow (basic, or Magic, Fire, Cold, Ice, Exploding, Immolation or Freezing
Arrow), a faint line runs from the arrow's tip along the
release's own direction. The one row chooses Off or the line's hue in
15-degree steps, wrapping through Off in both directions (345°, Off, 0°);
any negative saved value is Off (written as -15). The end dot is the same
hue, brighter. The line has its own small atlas, repainted only when the
color changes. Arrows have no gravity, so the line is the path. It
stops with a dot where the flight would meet the launch floor, with the
flight's own clearance, and otherwise fades out at 15 m: it is drawn over the
scene and does not stop at walls or monsters, which only the flight tests.

**Throw lift** (**VR Settings > Aim > Throw lift**, `[gameplay] throw_lift_deg`,
10° by default): a thrown javelin, knife or axe leaves along the throwing
hand's recent motion, which dips as the hand follows through. Every throw,
from either hand and with any javelin skill or Double Throw, leaves this many
degrees above that path, keeping its heading; -10° to 30° in 5° steps, Off at
0°. Shown for the Amazon and Barbarian, or any class holding a thrown weapon.
Multiple Shot, Guided Arrow, Strafe and crossbows show no line. The first line of each draw logs
`Frame bow laser: shown (length N m)` (`, floor` added when it ends on the
floor), at most every 2 seconds.

For ordinary **basic crossbow bolts**, the held model's `bolt_helper` supplies
the barrel line and muzzle. The supporting palm sits below that line, including
the item's attachment transform and weapon adjustments. Native server/client
creation uses the same frozen muzzle and full barrel direction. Basic bolts
use the bow's straight 3D flight, swept creature collision and wall checks,
including upward/downward aim. Their native missile still supplies damage,
ammunition, lifetime and trigger attack cadence; no second attack or client
projectile is created. Both eyes share the corrected current/previous mesh
transforms. The support palm keeps the controller's wrist angle when gripping
from underneath or above. Special missile skills and unrecognized/stale rigs
retain their native release behavior. The physical collision limitations below
also apply to basic crossbow bolts.

Crossbow support is a held grip: release **Left Grip** to free the support hand.
After a shoulder weapon swap, release the swap grip before grabbing the stock.
The latch resets when equipment or the hero model changes, and on lost tracking;
reacquiring tracking while still squeezing does not grab the stock. Waiting for
a usable attachment keeps the support hand free instead of selecting the older
always-two-handed pose. Native controller placement accepts wide idle/swap hand
poses, so switching from a shield set does not require firing first to initialize
the crossbow. The owner verified the swap/release fix in a headset on 2026-10-15
and approved merging it. The headset/runtime and individual regression scenarios
were not specified in that confirmation.

Collision is approximate: creature capsules use native footprint/artwork height,
and the floor is the launch floor. Walls extend vertically; arrows cannot fly
over them. The native damage-owner proxy retains its launch grid location while
physical flight and rendering move independently. Effects that query the missile's
native source position, room transitions and uneven terrain need further work;
do not assume exact spatial behavior for every item proc. These limitations and
HD-model alignment require headset checks. Arrows are straight, without gravity.

Recognized bow rigs (initially the hunter's bow) use their existing native mesh:
physical draw amount drives the string, limb bends and nocked arrow continuously.
The string snaps back on release; the later native attack animation cannot play
a second visible draw. Only the bow attached to the current hero is modified,
with rig names/hierarchy, ownership and finite-matrix checks. Bow poses come from
the installed asset's bind geometry, with fixed limb/string lengths. With a valid current attachment
transform, the drawing hand sets the nock
position and the arrow points from it toward the bow hand in full 3D. Unreachable
hand positions are constrained to protect the rig. Invalid attachment data falls
back to a normalized draw (45 cm of extra hand separation). More bow variants
and exact fingertip offsets still need calibration. Projectile
release now invokes the native missile immediately through the physical-arrow
path. Held-bow posing matches the controller sample timestamp to the completed
attachment pose, avoiding a newer hand position being applied to an older rig.

A pull produces light haptic pulses as draw distance changes, plus nock and
release pulses. Runtime acceptance is logged; that does not prove the user felt
it. The native arrow is hidden until nocked. Unknown rigs retain their native
string and limb animation; their string does not yet follow the drawing hand.
The teal V-string overlay is removed for all bow rigs. The nock marker and
crossed-plane arrow fallback remain available when their native counterparts
are not driven by the mod.

The palette, neutral bottle and fallback nock/bow guides are lightweight
OpenXR quads, stereo-positioned but not depth-tested against the game world.
The held bottle now uses the original 3D model when the local cache is available. Spell/item icons, belt contents/counts
and health/mana now use the read-only native snapshot and local icon cache.
Do not describe this prototype as completed physics combat.

## Head-relative sound

Monsters, NPCs, objects and other positioned sounds now come from where they
are relative to where you look. Turn your head (or the stick) away from a
monster and its sound moves to the other ear; a looping sound such as the
Rogue Encampment's stream swings as you turn without you moving.

Stock D2R pans its 3D sounds by their left/right position on the flat
screen and never moves FMOD's listener, so in VR they stayed tied to the
fixed camera. The plugin lays each 3D sound's screen offset onto the ground
(screen-up is ahead) at the game's own distance, and turns FMOD's listener
with the view every 10 ms, so FMOD re-pans everything that is playing.

- Stereo left/right plus the game's distance falloff; front and back sound
  alike (no HRTF).
- Music, ambience, UI sounds and your own hero's sounds stay centred.
- Not yet: monsters' attack cries (`<monster>_attack_*`, `damage_dealt`)
  are 2D sounds in the game's sound table and still play centred.
- If nothing pans at all, check Windows **Mono audio** (Settings >
  Accessibility > Audio): it folds the PC's sound to mono before Quest Link,
  Air Link or Virtual Desktop stream it. While it is on, VR Settings shows
  **Windows Mono audio is on** at its top right on every page; pointing at it
  explains where to turn it off.

`d2r_pcvr.ini`, read at start:

| `[audio]` key | Default | |
|---|---|---|
| `head_follow` | `1` | `0` = the game's own panning (flat-camera frame). |
| `listener_turn` | `1` | `0` = keep the ground mapping but leave FMOD's listener facing the camera. |
| `forward_sign` | `-1` | Which screen direction counts as ahead (`-1` = up). |
| `depth_scale` | `2` | Stretches screen up/down to undo the isometric squash. |

## Cancellation and performance

Menus, focus/tracking loss, weapon changes, recenter and discontinuous samples
cancel unfinished gestures. Held triggers cannot leak from selection/drinking
into a world attack. Tracking must be valid and tracked for both controllers;
after interruptions release the controls before retrying. Frame gaps over
100 ms cancel rather than infer a high-speed gesture from stale poses.

Melee automatically becomes available with a melee weapon, even with a wheel
skill selected. Input gating and posed contact recognition share the same
angular tracking limit, so valid fast swings can reach contact recognition.
After a tracking
discontinuity it needs 80 ms of fresh coherent samples, without a stationary
pose or grip press/release. A
stroke requires minimum physical speed and net excursion. Its initial acceleration
path is included when it qualifies, and wrist-only backhands rearm independently. Rapid small jitter
cannot accumulate into a strike; a resting blade cannot repeatedly damage a
creature. Menus, skill selection, potion handling, equipment changes, tracking
jumps, snap turns and recentering cancel pending sweeps. Game-thread requests
expire after 150 ms, carry session/equipment identities and are revalidated
before damage. No native unit pointer is passed from the render thread.
Bow shots require a nearby nock, additional deliberate draw, minimum duration,
forward direction relative to the bow hand and actual trigger release. Analog
trigger hysteresis prevents a small squeeze fluctuation from releasing the string.
The grab region is 28 cm around a marker 16 cm behind the bow hand; it accepts
press-before-approach, but a grab begun with separated hands still requires a pull. No arrows
are fired on a tracking-loss release. Potions need a fresh grab and a sustained,
tilted pose near the mouth; consumption requests expire after 200 ms.

The palette atlas is generated/uploaded once at OpenXR initialization (one
1024x1024 texture), using GDI text and simple procedural shapes. Per-frame input
uses fixed state, no heap allocations, texture updates or extra GPU waits.
Additional composition quads are submitted only while their guides are visible.
Measure live cost before making a performance claim.

## Validation and remaining work

Portable gesture tests and the Proton cross-build pass. Tests cover swing
debouncing, head motion, pose discontinuity, bow direction/release/cancellation,
menu click ownership, native slot routing, potion dwell/single-use/cancellation,
invalid poses and palette hit geometry. They do not prove native game behavior
or headset comfort. Run:

```sh
g++ -std=c++20 -O2 -Wall -Wextra -Werror steamvr/gameplay_test.cpp -o build-frame/gameplay-test-host
build-frame/gameplay-test-host
g++ -std=c++20 -O2 -Wall -Wextra -Werror steamvr/bow_pose_test.cpp -o build-frame/bow-pose-test-host
build-frame/bow-pose-test-host
g++ -std=c++20 -O2 -Wall -Wextra -Werror steamvr/melee_contact_test.cpp -o build-frame/melee-contact-test-host
build-frame/melee-contact-test-host
cmake --build build-frame -j4
```

Headset checks, in order:

1. Open/close inventory repeatedly; palette shows and can be aimed at comfortably.
2. Select an assigned skill; release trigger, aim, press again. Verify costs,
   correct action and repeated/channelled behavior. No attack from selection.
3. Swap sets once per click. Check the held model and gesture mode follow.
4. Melee: any offline character with a melee weapon. Outside town, release all
   buttons and slash, chop or thrust through an enemy. Check health change and
   contact feedback, overhead/beside misses, no damage from resting the blade
   inside a body, one hit per creature per stroke, and no hits from head turns.
   Try a backhand, swaps, menus and tracking recovery. Compare right-trigger
   fallback separately. Repeat with short and two-handed weapons; support across
   categories is an implementation goal, not a claim that every model was tested.
5. Belt: cancel a grab, then drink once; verify native item/count and health/mana.
6. Bow-equipped offline character with arrows: visible marker/arrow and grab/draw
   haptics, no left grip required, no shot on nock, one on release,
   aim follows draw line; short draw, menus and tracking loss do not shoot.
7. Hunter's bow: real string/limbs/arrow must move while holding trigger, remain
   drawn when held still, then reset once on release. Check pull haptics before
   release, menus/cancel/tracking loss, walking, weapon swaps and NPC bows.
8. Check native animations, labels, lighting and frame pacing for regressions.

Implemented since this original checklist: native inventory/skill metadata,
waist potion belt with counts and original models, and wrist health/mana UI.
Remaining work includes headset validation of drinking, accurate world target/loot highlights, responsive
point-and-trigger projectile casting/ground reticle, refined weapon/body collider
dimensions and two-hand grips, bowstring and arrow meshes, crossbow loading,
haptic success signals,
left-handed and accessible button mappings. Each needs game/headset validation.

## Physical basic-arrow test (2026-10-09)

With `motion_controls=1`, `arrow_speed=2` scales the native creation velocity;
the physical path maps a full-draw ordinary arrow to approximately **40 m/s** at
that setting (20 m/s at 1; supported multiplier range 1–3). Shorter valid draws
reduce speed by up to 30%. Native velocity changes such as Slow Missiles are
read on each tick. This mapping intentionally replaces the older slow native
travel speed; `motion_controls=0` restores legacy controls and flight.

The former ownership check treated the SDK's opaque player handle as a native
unit pointer. It now uses the validated local native player id, including id
zero, and checks owner type/id. That defect could prevent earlier speed and
release corrections from activating. Headset speed improvement is not yet
verified for this revision.

Physical arrows always bypass the stock half-second HD release-bone blend.
`arrow_direct_release` still controls the legacy handoff for other local basic
arrow creation; it cannot reintroduce a delayed blend into a physical shot.
All required native signatures and layout call sites must match before the
physical path activates. Unsupported builds fail closed and log refused/stale
requests rather than synthesizing a second button attack.

The test build logs release/native/visual counts, refusal/stale counts, body
contacts, barriers, expiry, last queue latency and physical speed. It does not
record full hand poses. Host tests cover cancellation/deduplication, high-speed
and vertical contacts, head/side misses, thin walls/corners, speed, bounded
prediction, visual axes, live bow poses and melee regressions. They do not prove
native ammo/HD rendering behavior; verify those in game.

For the next headset pass, use the existing bow character **outside town**:

1. Draw, hold, move both hands, release; confirm immediate flight along the shaft.
2. Fire level, above and beside a creature, then through it. Look for pitch-sensitive
   misses and consistent contacts without preselection or an attack-button hold.
3. Fire beside a wall/corner and at close range; arrows should stop at barriers.
4. Check one arrow spent per release, no shot on B/menu/tracking cancellation,
   and normal behavior on an empty quiver. Fired arrows may finish after a menu opens.
5. Try repeated draws, a weapon swap, leaving/re-entering the game, then a few
   melee swings. Check stereo, lighting and frame pacing for regressions.

## Deterministic player hits (2026-10-09)

The owner verified grip swings and trigger attacks, then requested that valid
melee, arrow and magic contact should connect without dice-roll misses.
With motion controls enabled, `[gameplay] contact_hits=1` now removes random
accuracy and block/dodge/avoid/evade failures for the local player's attacks
against creatures. Set it to `0` to restore the native rolls. This intentionally
changes combat balance; attack-rating/defense values no longer determine these
outgoing hit results. Their ordinary character-sheet display remains unchanged.

For native button/ranged actions, range, line-of-sight, collision/target eligibility,
immunity, damage, criticals, timing and resource costs still run through the game. Arrows and
spells must pass their existing native collision or area checks; a shot that
misses geometrically remains a miss. Incoming enemy attacks, summoned creatures'
attacks and attacks against players retain their normal rules. The modification
uses the identified native player id and current SDK session; SDK handles are
opaque and must not be treated as pointers.

The accuracy hook changes only the specific hit-chance roll and still executes
native success-side behavior such as Prevent Monster Heal. It advances the
original attacker's random stream once normally, keeping the next damage/proc
roll in place. All signatures must match before any override activates.

Free-swing melee now adds a separate swept-capsule contact layer (above), with
physical reach and stroke cadence. The owner verified the sword setup; broader
collider-fit and damage/proc checks remain. Accepted-hit haptics do not prove
positive damage against an immune creature.

Host rule tests and the cross-build pass. Live sword combat on October 9 logged
three eligible melee checks, three accuracy overrides and three block bypasses
without failures. The final free-swing run recorded 58 physical contacts and
58 native accepted hits with no rejections; the owner reported "works great."
Arrow/spell live verification remains open.

### Ice Bolt visual height correction

New or missing combat preferences default to **Planar spell aiming: Off** and
**Projectile aim assist: Strong**. Players can still choose planar aiming, Light
assistance or Off in VR Settings; explicit saved preferences are preserved.

Hand-cast Ice Bolt uses the same airborne flight/planar aiming path as Fireball;
Charged Bolt alone keeps its native ground path. Spell visuals now identify the
asset instance root from the native entity list and Transform parent, rather
than taking the first entity without TrackReleasePoint. Ice Bolt's VFX child
also lacks that component, so the previous heuristic could bind the child and
subtract its authored height at the wrong level. Binding only the actual root
keeps the emitter at the physical shot position with the existing +4-unit
child-height compensation. Native costs, damage, speeds and flight remain.

The new list/storage/parent offsets are guarded against local 3.3.93787 code.
Synthetic fixtures visit the release-free child before the root, validate full
entity generations and reject missing/malformed state. Height checks cover Ice
Bolt and Fireball at varied hand/floor heights alongside grounded Charged Bolt.
All 55 native checks pass. The correction still needs an offline headset test
of Ice Bolt in each hand, planar/free aim and concurrent casting. No agent
headset validation or game asset distribution. A bounded first-eight-entities
log records Ice Bolt root classification to help verify the native handoff.

### Potion sipping tolerance

A held potion now begins its 250 ms sip when the rendered opening is within
16 cm of the estimated mouth and tipped at least about 50 degrees from upright.
Once a sip starts, 19 cm / about 41 degrees keeps the dwell alive through small
tracking wobbles. Leaving those bounds resets it. This accommodates headset
clearance and face-position differences; the previous 10.5 cm / 75-degree
requirement could reject a natural sip entirely.

Upright bottles, distant hands, release, tracking loss, menus, recentering and
changed belt items still cannot consume a potion. Only the exact captured item
is requested, once per grab, through the native belt command; the bottle becomes
empty only after native inventory confirms consumption. Feedback uses the hand
holding the bottle. Bounded `Frame potion sip` logs report opening distance,
tilt, gesture completion and native consumption so failed gestures can be
separated from native refusals (including a resource already being full).

Both hands and turned poses are covered by synthetic natural-sip and boundary
wobble checks, alongside the existing rejection/cancellation tests. A Steam
Frame test confirmed the gesture registered, but exposed a second problem:
hiding the native HUD also stopped its belt widget from binding the local
player. Native commands were handled without consuming anything, even with
mana missing. Before dispatching a validated sip, the UI adapter now refreshes
that hidden belt's owner with the game's own setter and verifies the result.
It leaves HUD visibility and native potion rules unchanged, keeps no widget
pointer between requests, and still requires confirmed inventory removal.

The native-belt fixture covers an uninitialized hidden belt, rebuilt layouts,
player/session changes, missing widgets, unsupported signatures, rejected
commands without retries, and unchanged desktop routing. On 2026-10-14 the owner
confirmed drinking works on Steam Frame / SteamVR (Steam edition, offline
Paladin). Runtime logs confirmed the hidden belt owner changed from -1/type 6
to the local player, followed by consumption of the exact captured mana potion.
