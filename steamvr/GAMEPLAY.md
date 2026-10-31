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
Point at a row for a moment to see its help on the card under the rows. Point
at one of a row's choices, such as Eyes drawn per frame or Lower detail at
edges, and the card explains that choice.
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
(unless VR Settings > Aim turns the aim rules on; see the Aim page below).
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
| Throw a trap or bomb | An Assassin's Fire Blast, Shock Web, Blade Sentinel, sentry or wake on the right trigger: press it and the game's own bomb or trap model appears in the hand; throw and let go, and the native cast lands where the arc meets the floor, within the skill's reach (the throw picks the spot only). With Bottle physics on, the arc is traced through the physics world as you let go, so a wall, a table top or a prop stops it and the cast lands there; running carries the throw either way. A quick press casts where you point; held still for a moment, the native cast holds and repeats as before. Weapon skill gestures Off keeps the trigger native |
| Throw a gas or oil potion | A stack of throwing potions (Rancid, Choking or Strangling Gas; Oil, Exploding or Fulminating) in the right hand, right trigger bound to Attack or Throw: press it, throw and let go. With Bottle physics on, the potion leaves the hand as its own bottle (the game's model) and flies in the physics world until it first touches the floor, a wall, a table top or a prop; there it shatters. The game's own Throw goes as the bottle leaves the hand, aimed where that flight lands (the same physics world's trace), and its potion flies hidden, so the native gas cloud or blast follows the shatter there, within the game's reach. Off (or without the cached model) the game's own Throw lands the potion where the arc meets the floor; running carries the throw and Thrown weapon lift raises it. A quick press or a still hold throws as before. The potion hand never swings as a melee weapon. Potions in the left hand keep the native button. Weapon skill gestures Off keeps the trigger native |
| Cancel active grab/draw | Right B; release inputs before starting again |
| Physical belt potion | Reach to an original 3D bottle on the front belt and hold **Right Grip** to grab; left-hand grip also works |
| Drink | Keep grip held, bring the bottle's opening to your mouth and tip nearly horizontal for 0.25 seconds; release to return an unused potion |
| Drop the empty bottle | Open your hand after drinking: the bottle falls, bounces and rolls on the floor (visual only); hit it with your weapon or shield to knock it away |
| Heal the mercenary | Toss a held Health, Rejuvenation, Antidote or Thawing bottle (release mid-swing, at least 1.4 m/s) so it lands within 1.5 m of your mercenary's feet. It shatters on landing (with bottle physics, where it first touches a wall, table, prop or the floor); a miss leaves the potion on the belt |
| Quick potion | Left Y → **EQUIPMENT / BELT** → trigger a potion wedge; grip on a potion wedge instead grabs it into the right hand |
| Swap weapon sets | Reach the swap hand over either shoulder and press its grip; right hand by default, selectable in VR Settings > Body > Dominant hand. Hip anchors are removed |
| Seated weapon selection | Utility wheel → **Weapon I** or **Weapon II** |
| Stow/draw | Utility wheel → **Stow weapons** / **Draw weapons**; native equipped stats stay unchanged |
| Health/mana | Check the back of your left wrist, or choose **Palm** in **VR Settings > Wrist > Life and mana** and hold your left palm upward in front of you briefly. No button required; automatic alerts are off by default |
| Show all nearby loot labels (Alt) | Hold the **right stick down** to hold the native Show Items action; let go to restore normal drop/hover labels. Push it **up** to toggle running / walking. Neither while you look at the wrist / palm map, which they zoom instead. Listed in **VR Settings > Guide** |
| Explored minimap | Hold Left Y ? **MAP** in the bottom tabs; point right laser + trigger. **SPELLS** returns to skills; releasing Left Y closes the menu |
| Zoom the minimap | Look at the wrist / palm map or the wheel's MAP tab; right stick up zooms in, down zooms out. No run / walk toggle or loot labels meanwhile; left / right still turns, except during a zoom push; looking away returns the stick |
| Quest journal | Open the native tabbed game menu with Left X / Frame Left A, then select Journal. The duplicate wheel entry is removed |
| Journal controls | Right laser + trigger uses native page controls/act tabs; Right B follows native back; Left X performs native X |
| Inventory drop / sort | Native X (Quest Left X / Frame Left A) / press and hold right stick click; right-stick click no longer swaps in gameplay |
| Walk / snap turn | Sticks; paused while wheel or journal is open |
| Inventory menu / height reset | Left grip + left stick click / VR Settings > Body > Height and arm span > Set eye height |

Dropped-item names use **VR Settings > Interface > Loot name size**: **45%** by
default, adjustable from **20% to 150%** in five-percentage-point steps.
Apply saves `[hud] labels_size` in `d2r_pcvr.ini` and updates it live; existing
saved sizes are preserved. Text, dark backing bounds and click targets scale
together; item-quality colors are preserved. Holding the **right stick down**
holds the native **Show Items (Alt)** action, with a short vibration when the
hold starts. All nearby loot labels stay shown while the stick is held, using
the game's native Alt range rather than a VR aiming or pickup radius. Let go to
restore normal labels for newly dropped or hovered items. Pushing the right
stick **up** does nothing unless **Stick toggle** is On (walking and run speed are VR Settings > Body >
Movement speed). Its sideways pushes keep turning: a push counts only when mostly
vertical, and from then until the stick is back near the centre it turns
nothing. It does not work while a wheel, menu or VR Settings is open; a stick
still held when they close must return to the centre first.
Release the combo before casting or starting a grip interaction again.
Use it outside menus and wheels, with no potion or holster interaction active.
Normal right-stick clicks still toggle the wheel; native menu hold-to-sort stays
available. Menus, tracking/focus loss, leaving VR or changing character release
the command. Release the trigger before starting a fresh hold after an interruption;
there is no saved toggle or automatic resume.
The game's native item/filter rules apply. Inventory tooltips and monster labels
keep their styling.

The minimap and compass form one navigation display on your left hand.
**VR Settings > Wrist > Map and compass** offers **Wrist** (default),
**Palm**, **Wrist or palm**, or **Off**. Click **Apply** to save. Both appear and hide
together, independently of health/mana. When both displays share a hand face,
navigation sits above the vitals; otherwise it sits directly at the hand anchor.
The wheel's **MAP** tab remains available even when navigation is **Off**.

**Zoom:** while the wrist or palm map is shown and you look at it (its center
within about 22 degrees of where your head points; it holds until about 30, so a
glance at the edge does not flicker), the right stick's up / down zooms it:
up zooms in about your hero, up to 4x; down zooms out to 1x, the whole native
map, where it stops. Only a mostly vertical push zooms, and during it left /
right does not turn until the stick is back near the center. A light tick marks
each quarter step (faster as it zooms in), a firmer one either end. While you
look at the map, right stick up / down does nothing else (it does not toggle
running or show loot labels); left / right still turns. Wrist, palm and the
wheel's **MAP** tab share one zoom, and the stick zooms the MAP tab the same way
while you look at it. It is not saved: every new game starts at 1x, so a player
who has forgotten the control still sees the whole map.

**Full map:** **VR Settings > Wrist > Map view** = **Full map** or **Full top-down**
shows the game's full (Tab) map on the wrist instead of its corner map, in the same
two views. 1x is the game's widest zoom (0.125); up zooms in to 16x, redrawn by the
game closer as you go so it stays sharp. It is a screen-sized window around your
hero, not the whole level, and it is not drawn in your view while the wrist shows it.

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

**VR Settings > Wrist > Life and mana** offers **Wrist** (default),
**Palm**, **Wrist or palm**, or **Off**. Click **Apply** to save. Palm mode puts the
health/mana and XP display just above your left palm: raise it in front of you,
turn the palm upward toward your eyes, and hold for a quarter second. Release
the left trigger and grip; casting with that hand hides the display. Lower or
turn your hand to hide it. Losing left-hand tracking or opening a menu hides it
and requires a new deliberate hold. Navigation has its own location setting, so
you can keep health/mana on the wrist and navigation in the palm, or vice versa.

**VR Settings > Wrist > Life and mana tilt** adjusts the angle of the hand-mounted
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

The tabbed character menu (inventory, character, skill tree, quests, options
and every other tab, with its dialogs and a cube opened from the inventory) is
held in your hand instead: see "The menu on leather" below. The other native
in-game menus share a room-anchored surface in front of you. The stash, trade,
merchant and waypoint surfaces (and a cube beside the stash) use one opaque
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

Talking to an NPC stops you. Press A on an NPC while walking and you stop at
the press: the left stick is let go for that moment (the game's own stop, as
when you release it) and A reaches the game 50 ms later, so the conversation
opens with you standing. Once a conversation is up the game takes no walk at
all, so the hero used to carry on to the point the stick had set, round the
NPC where it did not block the way. A on items, chests and doors, and A while
standing, are as before. Log: `vrcam: NPC talk: A on an NPC while walking ...`
(developer builds also log how far you moved after the conversation opened).

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
that distance until the game is restarted or **VR Settings > Interface > Menu
distance** is changed. The handheld quest log keeps its left-grip placement.

### The menu on leather

Reach your **left hand over your left shoulder** and grip (as if pulling a
rolled hide from a pack) to open the tabbed menu; Left Grip + left stick click
and the wheel's INVENTORY open it too. It comes up pinned to a stretched animal
hide (the one drying on the Rogue Encampment's farm racks, read from your own
game at install) strapped to your left hand, and the world stays in view all
round it: no black backdrop. You hold the hide at the menu's left edge; its
neck and legs reach past your fist. The hide follows the hand whether or not
you hold the grip, until the menu closes (Right B, its close button, putting it
**back over your left shoulder** - reach there and grip again, the way it came out - or
anything that closes the native menu); then it is gone. Putting it away leaves it open
while an item is on the cursor (log: `Frame leather: put away over the left shoulder`). Below the menu's hint row sits a
small stone **Advanced Stats** button (three gold bars), the same one the room
panel shows when the leather is off: laser + trigger sends the game one View
press, a button VR controllers lack (log:
`Frame panels: Advanced Stats button - View sent to the game`). Right B goes to the game first,
so a cube, a dialog or the options pages still step back one at a time; a press
the game leaves unanswered for a quarter of a second, with only the menu and
its pages open and nothing on the cursor, closes the menu (the log:
`gamestate: B on the leather went unanswered by the game ...`). The right laser and every native
control work on it as on the room panel, Left Grip + Right Trigger included
(the left grip is free while the sheet is strapped on). The **left stick walks** and the
**right stick turns** while the menu is held (Leather or Menu only), as in
play (neither moves the menu's controller cursor); the right laser and the face buttons
work the menu (the right stick is all turning while it is held). An item picked
up with the laser (a click, as ever) and then **clicked outside the menu** - a trigger pull off
the menu's art, on or past the hide - drops to the ground, as in flat D2R: the mod presses the
controller menu's own Drop (X) for the item held (logs: `Frame panels: item let go outside the
menu - X ...`, then whether it dropped; a pull off the menu that did not drop says why). The **life / mana / XP panel comes with it**,
above the menu (unless its VR Settings location is Off); the minimap does not (the game's map
is never kept open under a menu). Nothing else is drawn over it: quest and level-up cards wait until it
closes, and the wrist and palm displays themselves are hidden while a menu is open, as before.

Only the menu's 16:9 band is on the hide, on the middle of its body, and the
game's translucent dim under the menu is taken out of it, so the hide shows
round the menu's own art, as is the panel's faint black drop shadow. The hide
itself goes to the headset premultiplied: nothing of its rectangle shows round
it. The game also leaves the world unpainted behind its opaque menu panel (one
draw stamps the panel's rectangle into the scene's stencil); while the leather
is out the mod binds a copy of that draw that writes no stencil, so the world
behind the hide is whole (log: `Frame leather: the game's menu stencil stamp
found ...`). Opening the cube from the inventory
keeps it on the leather; the stash, merchants, trade, gambling, imbue, hiring,
waypoints and NPC talk keep the room panel. While the sheet is held, the left
hand's own weapon or shield is hidden, as with the quest log. With **Dominant
hand = Left** a left grip over either shoulder swaps weapons, so the shoulder
reach is off; Left Grip + left stick click and the wheel still open the leather
(the menu laser stays on the right hand).

**VR Settings > Interface > Menu distance** carries **Hold menu** on its right half:
**Leather** (the default), **Menu only** (the same menu held in your left hand with
no hide: the fist holds the menu's own left edge, and the shoulder reach, walking
and the Advanced Stats button work the same) or **Off** (the tabbed menu in front of
you against black as before, with no shoulder reach). It applies at Apply.

The installer cuts the hide out of the game's own colour map
(`act1_town_farming_pelt_hanging_b`) into `d2rloader\plugins\d2r_vr_leather`
(`prepare_leather.py`); it is never shipped with the mod. Without that cache the
menu sits on a painted stand-in hide. Settings, live in `[interactions]`:

```ini
[interactions]
leather_menu=1      ; Hold menu: 1 Leather, 2 Menu only (no hide), 0 Off (the room panel, no shoulder reach)
leather_menu_width=0.57  ; the menu across, metres (0.30 to 0.90); the hide is sized round it (about 0.83 m)
leather_tilt=0      ; degrees leaned back toward you from the quest log's angle (-45 to 60)
leather_world=1     ; 0: the old black backdrop behind the leather (if the eyes disagree)
leather_backdrop=0  ; 1: keep the game's translucent dim under the menu on the hide
```

The log notes `Frame leather: the game's hide, ...` (or `... the painted
stand-in`) once at start, `Frame panels: tabbed menu on the leather, left hand
(...)` when it opens and `Frame leather: menu band ...` when the part of the
canvas on the hide changes. Still needs a headset check: whether the world behind matches
in both eyes (on 2026-10-09 the transparent tabbed-menu background differed
between eyes, which is why the room panel went black), readability at arm's
length and tooltips near the band's lower edge. The cube from the inventory
shares the tabbed menu's band (fixed after v0.3.29, where it showed cut off).

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

The panel opens in front of the headset and stays in room space. A rail on its
left lists **Presets** (grayed out for now: presets come in a later update) and eleven pages; **Guide** and **What's new** open from
the links at its top right. Point the right laser at anything to read about it
on the help card under the rows, and pull the trigger to press. A number (cm,
m/s, %, degrees, distances) has a bar: grab its value and drag left or right,
or step it with - and +; it stops at its ends. A short list shows every choice as a
chip: one press picks. A longer list, and a color, shows its value between < and >,
which step and go round from the last choice to the first; pressing the value opens
all the choices over the page (colors as a grid of dots with By range or Off above):
one press picks, and pointing away, pressing outside it or Right B closes it unchanged. Every control takes a
press anywhere in its row's height. Changes stay a draft until
**Apply**; **Close** with a draft asks first (Discard, Keep editing, or Apply and
close). A changed row's card offers **Undo** and **Default**, and **Reset page**
sets the page's rows back to their defaults, with **Undo reset** for 6 seconds.

| Page | Contents |
|---|---|
| Presets | Coming in a later update: the tab is grayed out and its card says so. Each preset will set only the rows it lists |
| Body | Dominant hand, smooth turning with its snap angle or turn speed, seated play, the belt's height and reach, and **Height and arm span** (**Calibrate body**, **Set eye height**) |
| Combat | Motion combat, swing and stab speed to hit, skills on your moves (melee skills on a landed strike, Smite on a bash, arrow skills on a release), skill icons on weapons and thrown weapon lift |
| Aim | Whether skills go to the aimed monster, spell aim assist, keeping spell shots level, aiming a staff along the arm, aiming with your view (the eyes' 17-degree tunnel, the hands within 30 degrees of your head, a raised hand aiming alone), where ground skills land (the game's aim, where you point or the aimed monster) and landing circles. Every option that changes how a cast aims is Off (Game's aim) by default, except Spell aim assist (Strong) |
| Targets | The aimed monster's outline: its strength, which hands show it, and each hand's outline color (By range or one color, shown as a swatch); the arrow path line |
| Shield | Bash speed to hit, the guard strain shadow (On or Off), and where a held shield sits on the forearm: along, out and across (all 0 by default), wrist turn and roll |
| Wrist | Life and mana location and tilt, map and compass location, map view (Isometric, Top-down, Full map or Full top-down), turning the map with you, buffs and companions |
| Interface | Menu distance with **Hold menu** beside it (Leather by default: the tabbed menu on a hide in your left hand; Menu only: no hide; Off), item details delay, button names, loot name size and weak monster bars |
| Survival | The low life and mana alert, heartbeat, blood on the visor and wiping it, offline difficulty, monster sight range and the smarter mercenary |
| World | Outdoor sky, distance fog with its **Fog distance** beside it while the fog is on (where it is complete, 100-1200 units, 600 by default), dungeon ceilings, rolling bottles, knockable props, pushable bodies, the experimental Sorceress spell glow on hands (Off by default) and the torch club's flame (On; its light stays when it is off) |
| Vibration | Overall vibration and separate melee, short melee taps, bow draw, bow/throw release, spell, loot/potion/menu and alert strengths; **Feel it** on a row's card |
| Performance | Everything that changes the frame rate: eyes drawn per frame (Both / One by turns, One by turns by default), render resolution (Headset size by default), lower detail at edges (Off / Fixed / Eye tracked) and its edge detail reduction, frame generation (experimental: Off / FidelityFX / NVIDIA, turned on only with Both eyes and paused by turns, with what OFXR Bridge is doing under its label; NVIDIA's flow quality and scale on More while NVIDIA is chosen), terrain and scenery distances with **Big objects** beside Scenery distance (600 by default); the actual VR source size, a Defer GPU wait button while frames wait for the GPU, and warnings while the game's Vertical Sync is on or its anti-aliasing is not FXAA. Under them, each second's frame rate and where the frame's time goes, whether the CPU's part fits the headset's frame, and the game's own graphics options |
| Guide | Left/right controller reference for movement, menus, melee, bow, spells, potions and game menus, in the button names Interface > Button names chooses |
| What's new | The last published release's notes, read-only; a long release pages them, 12 lines a page, with **Previous** and **Next** |
| More | Shown (a band link with a count) only while a setting has no place in the menu yet, such as one added since the menu was last sorted: its own label and help, 8 rows a page. A setting like that whose ini section has an owner page (sky, fog, ceiling and torch: World; render and stereo: Performance; haptics: Vibration; hud: Interface; bow: Targets; turn and calibration: Body) shows in that page's New section instead, while the page has room. NVIDIA's flow quality and flow scale show there while NVIDIA frame generation is chosen (Performance has no room for them) |

Pages show only the rows that can apply to the character the panel is open
for. Another class's rows are left out:
- spell glow on hands is the Sorceress's;
- Ground skill aim and Landing circles are for the Sorceress, Assassin, Druid and Necromancer;
- the arrow path line is the Amazon's, or any class's with a bow in hand;
- staff aim is the Sorceress's and Druid's, or any class's with a staff in hand.

**Skills go to the aimed monster** offers Spells, Bow skills or Both to an
Amazon. Other classes see **Spells go to the aimed monster**, On or Off, and
keep the Amazon's bow skills choice. Rows that only matter with another
row's choice also stay hidden until they do:
- Outlines shown and both hands' outline colors while Monster outline is Off;
- Landing circles while Ground skill aim is Game's aim;
- Aim with your view until the outline, Skills go to the aimed monster or
  Ground skill aim on Monster is on;
- Edge detail reduction while Lower detail at edges is Off;
- the snap angle or smooth turn speed of the turning mode not chosen;
- the standing belt drop while seated;
- Wipe blood by hand without blood on the visor;
- Fog distance while Distance fog is Off;
- NVIDIA flow quality and NVIDIA flow scale (on More) unless Frame generation is NVIDIA;
- Defer GPU wait once it is On (the menu only turns it on).

Each setting has its own row: the left and right hand's outline colors are
separate rows. Four settings sit on the right of the row they belong to, as
they did before: Movement speed's **Stick toggle**, Menu distance's **Leather
menu**, Distance fog's **Fog distance** and Scenery distance's **Big objects**.
A switch there flips on a press; a distance's box opens the list of its values.
The title and
character screens show every class's rows.

**Big objects** (Performance > Draw distance, beside Scenery distance; 600 by default; experimental)
keeps the game's own trees, houses and walls drawn past the object draw
distance. The game stops drawing every model - houses, props, monsters - once it
is farther from you across the ground than the object draw distance. With Big
objects a model's distance grows with its size, its height or half its width,
thirty times over, up to the setting (300-1000 units or Off, `[render] big_radius`): a
20-unit tree stays drawn to 600 units, while a stone or a tuft of grass keeps the
object draw distance. The game builds models only in the rooms it keeps, so
Terrain draw distance 3 or more is needed for anything to show farther (6 by default). It costs
frames where there are many big models, in forests and towns.

Added indoor fog defaults to **Off** for new profiles, preserving the game's
own lighting and effects, including Tyrael's wings. Outdoor fog keeps its
existing setting. There is currently no indoor-fog toggle in VR Settings.
Existing saved `[fog] caves` choices are preserved when updating. The River
of Flame gets no added fog either, though it is open air: its lava's own glow
and haze stay the game's. `[fog] no_fog_areas` lists such areas by biome name
(`act4_lava` by default; empty puts the fog back there).

**VR Settings > Vibration** tunes vibration for different controllers. Each strength
ranges from **0% (Off) to 200%**, in **10% steps**, with **100%** as the default.
Overall vibration multiplies the category strength: 50% overall and 50% bow draw
produce 25% of the original draw amplitude. The controller's maximum amplitude
still limits strong pulses. **Short melee taps** is **Off by default**: weapon
impacts use a 90 ms tap to make contact easier to feel during fast swings;
shield impacts retain their 45 ms tap. Both use the controller's preferred
frequency. Turn it On for the original 10 ms / 180 Hz tap on either hand.

Pointing at a strength row shows **Feel it** on its help card: a sample of that
action at the strength shown, unsaved edits included, on the hands the action
uses in play. A test is a sample, not a simulated gameplay action.
**Apply** saves and updates gameplay feedback; **Close** asks before it drops unsaved edits.
Muting a category stops its active vibration; Overall Off stops both controllers.
Bow draw covers string proximity, nocking, pulling and full draw. Bow/throw release
covers shooting and throwing. Melee covers weapon and shield impacts. Interactions
cover loot, potions, holsters and menu feedback. Resource alerts still require
**Survival > Low life and mana alert** to be enabled for automatic warnings.

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
does not require Apply. The Guide is a reference, not a binding editor.

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
differences only when the runtime reports none), capped at 20 m/s, plus your
own speed through the world when running or walking (measured from the
player's position frame to frame, smoothed over 0.1 s; a waypoint, portal or
other jump faster than 10 m/s is not counted). Running never turns a slow
release into a throw: that still depends on the hand alone. Once released
it is in the world: its arc is integrated in world metres from the release frame,
against the floor height under the player at release, so walking, turning or
recentering afterwards never carries it along. With bottle physics on
(`[interactions] bottle_physics`, the default) the bottle flies as a body in the
same Box3D world as the dropped bottles and the level's props, spinning with
your hand's last turn. It stops at the game's walls, can land on a table or
shelf surface, knocks props it reaches, and lands wherever it first touches
anything. Your own weapons and fists never touch it. If the physics world
cannot take it at that moment (another floor while props are still moving),
it flies the arc above; with bottle physics off, it always does.

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
bottle_physics=0` (VR Settings > World > Roll and break bottles: Off) restores the old vanishing bottle, and thrown potions fly the old arc. The log notes each release (`Frame dropped bottles:`)
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
visual. VR Settings > World > Knock over small props (`[interactions] prop_physics`) turns it off; see [knockable
props](../docs/KNOCKABLE_PROPS.md). The log notes each weapon knock (`Frame
props: knocked a cup ...`) and a summary every ten seconds.

**Pushable bodies.** In first person with Motion combat, your weapon, shield
or fist pushes the monsters, mercenaries and townsfolk it meets. Each
monster or townsperson within 4 m is given a body of capsules, one on each
of its larger bones (sized from its model's own per-bone boxes, learned once
per model). A weapon coming within 60 cm makes that body a ragdoll in its
own Box3D world: springs at the joints hold the animated pose between its
limbs, and a spring holds its hips to where the animation has them. A struck
limb swings away at its joint, the body leans or is shoved back (at most
30 degrees a limb and 10 cm for the body), then both spring back into the
game's animation. Each weapon or fist pushes with limited strength, so light
limbs give way and heavy bodies resist; a landed hit adds a kick. A body
whose animation has held still for a second (a corpse) is never pushed. The ragdoll follows the animation as it plays, so a
monster that runs or attacks does not lag behind it. Purely visual: where a
monster stands, its hits and your attacks are unchanged. Up to six bodies
move at once; one back on its animation with no weapon near for 1.5 s is let
go. VR Settings > World > Pushable bodies (`[interactions] body_physics`)
turns it off. The log names each model's limbs once (`Frame bodies: <model>
- N limbs of M bones`), the first push each second, and a summary every ten
seconds.

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
body_physics=1
leather_menu=1
leather_menu_width=0.57
leather_tilt=0
leather_world=1
leather_backdrop=0
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
are off by default; enable **VR Settings > Combat > Skill icons on weapons**
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

**Forearm twist and shield position (2026-10-21).** With the hands turning
with the controllers, the forearm takes a share of the hand's twist about its
own length, so sleeves and bracers follow the wrist. The shield is placed in the
arm's own frame, built from joint positions only (elbow, wrist and the index and
little-finger knuckles), never from a bone's axes, which each class's bind pose
lays differently: on the Paladin the old "Up" moved the shield sideways off the
arm. **VR Settings > Shield** now means the same directions on every class:
**Grip offset** slides the shield along the forearm, + toward the elbow and -
toward the hand (-10 to 40 cm), **Out** moves it away from the arm on the
shield's side (the back of the hand, or the palm if the bind pose hangs it
there) and **Up** toward the thumb (-30 to +30 cm each), in 1 cm steps scaled by
the model's forearm; all 0 by default (the original position when Shield follows is
100%); about 3 / 2 / 2
cm suits a buckler and large shields need more (`[gameplay] shield_grip_cm`,
`shield_out_cm`, `shield_up_cm`). The posed shield is also the guard pose that
blocking, parrying and bashing read, so the shield blocks where it is shown.

**Shield follows** sets how much the shield turns with the wrist
(`[gameplay] shield_follow_pct`, 0-100 in 10% steps, default 50%; the row reads
"0% Forearm" and "100% Wrist" at its ends). 0 straps the shield to the forearm, so a wrist bend leaves it still; 100 turns it
with the hand, the original wrist attach. The blend is a slerp between the two,
so it is smooth at every value. Twisting the forearm turns the shield at every
value.

A resting roll then turns the shield about the forearm: **Shield roll**
(`[gameplay] shield_roll_deg`), default 0 (the attach's own facing), -180 to +180, snapped to
5-degree steps; positive turns the face from the back of the hand toward the
little finger. With the usual near thumb-up grip, 0 seats every class's shield
on the outside of the forearm; +90 suits a controller held palm down (the face
outward instead of lying on the forearm like a tray), while with the thumb up it
lays the shield flat under the wrist.

The side of the forearm the shield is on (for Out only; the wrist stop is unchanged) is
decided from the bind pose (the same every frame), never from the live bones:
read from the live bones, it flipped when the shield's bone sat near the
forearm's line, and the shield jumped across the wrist.

**Steady shield.** The controller's own tremor shook the shield about as much as
the hand (about 1-2 degrees and 1.6-1.9 cm a second of back-and-forth while held still,
at every Shield follows), so the placed shield ignores changes under 1 degree
and 1.5 mm (`vr/shield_steady.h`). The dead zones are soft: past them only the
excess passes, so the shield never jumps at their edge, and any real move goes
through in the same frame. (A smoothing filter tried first trailed behind when
walking side to side, then snapped back.) Rotation and position are separate;
a pause over 250 ms restarts it. There is no setting. The guard pose blocking
reads is this bone, so blocking uses the steady shield. `Frame shield steady: on`
is logged once.

`[debug] shield_trace=1` (off by default)
logs the held shield's pose chain at 5 Hz, and at once when it jumps:

`Frame shield trace: t=1234567 face=35 hand=(-21.0,-38.5,-30.2|12,-35,80) wrist=(-19.8,-41.0,-27.5|10,-33,78) elbow=(-24.1,-48.7,-3.0|5,-20,70) seat=(-3.0,6.1,2.0) shield=(-15.9,-43.1,-31.8|-80,10,15) side=back stop=0 follow=50% roll=0 bend=24 turn=3.2deg`

Everything is in model space from the eye, so a body turn or a level load no
longer reads as a flip. A new world placement (snap turn, recenter, level), the
first trace frame after the shield or arm appears (or its bone or skeleton
changes), and a gap of over 0.5 s with no trace frame (a level loading) each
restart the jump check, so spawning never prints a JUMP.

- `t`: milliseconds (GetTickCount64); ` JUMP` after it marks a line written
  early because the shield turned over 20 degrees in one frame.
- `face`: the yaw the controllers are placed with in model space, degrees.
- `hand`: the tracked left controller's grip point, model space from the eye,
  cm (at the user's scale), then its heading, pitch and roll in degrees.
- `wrist`, `elbow`: the model's hand and forearm bones, the same way.
- `seat`: the shield's attach bone from the wrist in the arm's frame (along,
  out, up), cm. It moves with Grip / Out / Up, not the wrist or the roll.
- `shield`: the shield's attach bone (after the steady shield's dead zones), model space from the eye, and its angles.
- `side`: the side of the forearm the shield is on (`back` of the hand or
  `palm`); `stop`: degrees of wrist bend the shield stop held back.
- `follow`, `roll`: the settings in use; `bend`: degrees between the forearm and
  wrist frames (the bend shield_follow_pct splits);
  ` (wrist attach)` when the hand has no knuckle bones and the original wrist
  attach is used unmoved.
- `turn`: the shield's turn since the previous line, degrees - a flip shows as
  a jump toward 180.

With **Smite** selected, an energetic
left-arm swing or thrust that intersects an eligible creature requests one native
Smite against that creature. A stationary shield cannot bash; one continuous
stroke requests at most one bash, and a deliberate reversal/settle rearms it.
**VR Settings > Shield > Bash speed to hit** controls this gesture in both melee
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
is adjustable under **VR Settings > Combat > Swing speed to hit**
(minimum 1.5 m/s, no upper cap, 0.25 steps, default 12.0). Apply saves
`[gameplay] melee_hit_speed` and updates the detector live; changing it clears
the previous stroke. Swing travel remains 18 cm.

**Thrusts** use a separate forward-motion check in both Unleashed and Classic.
Push toward the posed weapon tip for at least 12 cm within 200 ms, with at least
85% of the hand's travel along that axis. Default forward speed is 2.0 m/s,
adjustable under **VR Settings > Combat > Stab speed to hit** from 0.75 to
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
**VR Settings > Vibration > Short melee taps** opts into a 10 ms / 180 Hz tap;
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
**Melee rules.** Melee is Unleashed only since 2026-10-23: `[gameplay]
melee_rules` is ignored and no longer in VR Settings. The Classic rules below
describe code that stays until it is removed. The two modes for physical swings
and trigger/button melee were:

- **Unleashed** (`0`, the default): every weapon contact strikes each
  creature once per stroke. No to-hit, block or dodge roll and no attack-speed
  gate (the `contact_hits` policy). Instead, attack rating and attack speed scale
  how hard a physical swing lands:
  - **Attack rating** (`ar_damage`, 1): damage x the native to-hit chance (5-95%,
    attack rating, defense and level penalty), read where the game's hit check
    passes it on. `ar_damage_floor` (0) keeps that % of damage at zero chance.
  - **Recovery** (`swing_recovery_damage`, 1): damage x the share of the native
    attack cycle (class, weapon, IAS, WSM, skill speed and slows) since the previous landed stroke, to
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
  - **On-hit effects** come only from a stroke that waited a full native attack
    cycle (the breakpoint, from the previous landed stroke on the same recovery
    clock: per hand when dual wielding, one shared clock for a shapeshift
    form's two claws), and only on the first creature that stroke lands on.
    That covers life and mana leech, critical and deadly strike, crushing blow,
    open wounds, freeze, slow, knockback, flee, blind, prevent monster heal,
    defense lowered per hit and chance to cast on striking or attack (curses
    included). Every other contact still lands its scaled damage, with no
    on-hit effects. `swing_recovery_grace` does not shorten the wait. A riposte
    always carries them; an unknown cycle counts as full. Elemental damage is
    damage (scaled like the hit; poison and burn by the contact's factor), its
    effects (chill, freeze) follow the would-have-hit gate below, and on-kill
    effects are unchanged. The game's
    own roll sites are checked at start (`Frame on-hit gate:`); a part not
    found stays native. `[debug] melee_trace=1` adds `onhit=1/0` to each
    traced contact.
  - **Would-have-hit gate:** a contact's effects other than its damage also
    need the swing's native roll to have hit (below, *Would-have-hit gate*),
    so a full-cycle stroke at 60% attack rating chance carries them about 60%
    of the time, as natively. `would_hit=1/0` (-1: not gated, e.g. a riposte)
    ends each `melee_trace` contact line.
  - **Melee skill strikes** (CONTROLS.md): a hand armed with a strike skill
    (the Druid's Maul, Feral Rage, Rabies, Fire Claws, Hunger and Fury; Bash, Stun, Concentrate, Berserk, Sacrifice, Vengeance, Conversion, Zeal,
    Power Strike, Impale, Charged Strike, Lightning Strike and Poison Dagger) casts it natively on a full-cycle stroke's first landed
    contact, inside that contact's scaling and on-hit gate; every other contact
    stays a basic attack. Classic casts on every paced contact. Developer
    builds log `Frame melee skills:` (armed contacts, casts, refusals by
    reason) and add `strike=<skill> cast|basic_strike` to `melee_trace` lines.
  - **Fury and Zeal** (Cal, 2026-10-25; replaces the swing flurry; rev2
    without the native mode, which rooted the hero): the casting stroke runs
    the skill's start and one do 13 hit (count 1) on the struck creature.
    The mod then lands the cast's other hits itself (`strike_skills.h`
    `Sequence`, `melee.cpp` `SequenceHit`), one do 13 call each, at the native
    sequence's rhythm measured from native casts (rev4): Fury 4/9 and Zeal 1/2
    of the attack cycle (160 ms at 360, 280 ms at 560; 500 ms cycle if
    unknown), up to 20 ms early to meet the game update, the next timed from
    when the last was due, each on the nearest other live hostile within 3.5
    cells and in sight (else the previous one; none ends the cast). do 13
    still checks native melee range and picks its own creature in reach if
    needed. The hero never enters the skill's mode, so movement stays free.
    Those hits are the cast's own (rev3): the trigger rules' recovery factor
    and on-hit gate don't apply to them (attack rating still scales them, and
    the would-have-hit gate still holds back effects a miss would not have).
    Each is still the latest attack later swings are measured from (rev5), so
    strokes while it runs stay recovery-scaled basic attacks; a game/form change or death ends it. Developer builds log `Frame melee sequences (Fury, Zeal):`
    and trace `cast 1/N` on the casting stroke and
    `sequence skill=... hit k/N target=... after N ms` for the rest.
  - **Zeal's light** (Cal, 2026-10-26; `steamvr/zeal_light.h`): every Zeal
    hit flashes the Den of Evil's light (Missiles.txt `DenOfEvilLight`, row
    287; HD `missiles/light_beams.json`) at the struck creature's cell,
    created as a client-only visual through the bow preview's path
    (`projectiles::CreateVisual`): no server missile, packet, attack or
    damage. The hits: the casting stroke, the cast's later hits (the creature
    do 13 really struck) and Zeal held on the trigger. rev2 (Cal: "they didnt
    come and go with the hits. Quick fade in/fade out"): a beam per hit, its
    range (params flag 0x8000, +0x5c) `[gameplay] zeal_light_frames` (2-25,
    default 6 = 240 ms, before the next hit at a 560 ms cycle) instead of the
    native 300, then the particle's own fade. A client-made missile is not
    removed when its range runs out (rev2 test: the beams stayed), so rev3
    hands each to `bowvr::RetireVisualAt`: the bow's client tick kills it at
    its time, as it retires its own preview (`Frame timed visuals:`). rev4
    (Cal: the light appeared about a second late; the effect's spawner has a
    first spawn delay): while a hand's trigger skill is Zeal (seen within 3 s)
    `[gameplay] zeal_light_pool` beams (default 6, 0-10) are kept lit at the
    hero's cell, their visual root dropped 100000 units below the world in the
    bow's transform hook (`bowvr::StageVisual`, as the hidden potions); a hit
    shows the longest-lit one by moving its root onto the creature's cell (2
    render units a cell; the effect and spotlight inherit it), then retires
    it. None lit: a new beam, late, as rev3. The pool is let go 15 s after
    Zeal was last armed. Logs `Frame staged visuals:`. rev11 (Cal, Cave: the
    lights were late and several showed in sequence for one hit): beams lit in
    the previous level or rooms left behind never reach the client tick that
    retires them, so their 12 slots filled; every other pool beam was then
    refused a slot, retired at once, and its visual outlived it as a stray
    light at the hero's cell. Now the pool's lit beams are let go on a level
    change and once the hero is 20 cells from where they were lit, a slot whose
    retirement is 2 s overdue is free (as is a retirement wait in the 16-slot
    timed list), and the pool makes no beam without a free slot (`no free
    slot=` on `Frame zeal light:`). rev5 (Cal: "fade out
    when they go away and not just suddenly turn off"): removing a missile
    ends its visual at once (natively too), and the effect's fade input
    (`__a_Fade`, its material's MonsterFade) is never driven for missiles, so
    a shown beam leaves by its root transform before it is retired:
    `[gameplay] zeal_light_exit` 1 rises 30 render units into the sky
    (default), 2 sinks as far into the ground (its soft edge fades at the
    floor), 3 shrinks every entity's basis, 0 none; over `zeal_light_exit_ms`
    (default 250, 0-1000), eased. rev6: a beam whose unit is gone (retired,
    or a hidden one ended natively after ~12 s) kept rendering for a few
    frames un-hidden at the hero cell it was lit at, a stray lit beam after
    each hit; its entities now stay hidden 1.5 s after the unit, and hidden
    ones are retired (still hidden) after 8 s and lit anew. rev7 (Cal: "I
    just dont want it ever generating from behind my player. It just ends up
    looking like a flash of light in my visor"): the Den light slants (its
    spotlight sits at local 12, 20, -9.96 above its foot), so each shown beam
    is yawed about its root until that side points from the hero toward the
    creature: the light comes from beyond it. Children are turned too unless
    they already take the root's turn (told from the spotlight's offset).
    `[gameplay] zeal_light_turn` trims it (degrees). rev7's log: the children
    follow the root's turn. rev8 (Cal: "Its still not always behind the
    enemy. I want it behind the enemy at different angles"): the beam's foot
    stands `zeal_light_behind` cells (default 1.5) beyond the creature from
    the hero, at a random angle up to `zeal_light_spread` (default 60, max
    90) degrees either side of straight behind, and its light is turned to
    come from that same side (`zeal_light.h` `Behind`). One hit seen twice within
    120 ms makes one beam; at most 8 show at once. `[gameplay] zeal_light`
    (default 1). Developer builds log `Frame zeal light:`, `Frame staged
    visuals:` and `Frame timed visuals:`; with `melee_trace` each beam's cell.
  `Frame swing damage:` in the log reports scaled hits and the last factors.
- **Classic** (`1`): native attack rating, monster defense, block and the
  level penalty decide each attack, and attack speed paces them (below). A rolled
  miss uses up the attack, as a missed swing animation would.

Missile hit checks (arrows, missile spells) keep `contact_hits` in both modes. In
Unleashed your arrows and throws then follow the contact rules: attack rating
against the monster's defense scales each projectile's damage at the life write
(`ar_damage`; no recovery penalty), and the would-have-hit gate decides its other
effects. `Frame shot damage:` counts scaled hits. Classic: full damage, as before.
In Unleashed your trigger/button melee (a melee hit check outside a physical
swing: the trigger's basic attack or melee skill, Smite from the trigger or a
shield bash) is a stroke like a swing (2026-10-24): it lands, attack rating scales
its damage at the life write (`ar_damage`), the would-have-hit gate decides its
other effects, and it shares the swings' recovery. It is measured from your latest
landed swing on any hand (the recovery damage factor and the on-hit cycle gate, as
a swing's), and every later swing is measured from it as well as from its own
clock, so mixing trigger attacks with swings is never faster than swinging. Trigger
hits never count against each other: the native animation already paces them, and
a multi-hit cast (Zeal, Fury, Whirlwind) keeps its later hits. Smite always hits
natively, so it skips attack rating and the would-have-hit gate and only shares the
recovery. **Melee skills deliver natively (Cal, 2026-10-26):** a hit of a melee
skill outside a physical swing - a trigger-held cast, Smite, a Fury or Zeal cast's
later hit - is whole: no recovery factor and its on-hit effects, however recently
you swung (the hero's used skill is past Skills.txt's basic rows 0-5). Attack
rating still scales it (as every Unleashed hit, Cal's choice A: the same average
as native's hit-or-miss roll, without whiffs) and the would-have-hit gate holds back
the effects a miss would not have. It still stamps the trigger clock, so swings
right after it stay recovery-scaled: Unleashed only keeps swings from slipping in
between a skill's own casting windows. A swing that casts a strike skill is a
full-cycle stroke's first contact, so it is whole too; a riposte adds its bonus to
it. The trigger's plain Attack keeps the trigger rules above. `button_cycles` in
`Frame melee:` counts trigger hits; developer builds log `Frame trigger melee:`
(hits inside a swing's cycle; `skill hits left whole=`) and `Frame trigger melee
damage:` (verdicts, scaled hits), and `melee_trace` adds a `trigger target=` line
per hit (`skill hit: whole` for skills). Classic: unchanged.
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
Skill speed and slows (Fanaticism, Frenzy, Burst of Speed, chill, Decrepify) come
from the unit's attackrate (ItemStatCost 68, which already subtracts the main
hand's WSM; `attack_speed.h` `SkillSpeed`); the forms' own cycle is below.
Dual-wield attack sequences are not read yet. An unknown class or weapon class (including
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
  `guard_strain_per_block` % (10), weighted by the attacker's rank: about 10
  normal-monster blocks in a row reach full strain. There is no buildup speed
  setting (`guard_strain_speed` is no longer read). Strain shows as a soft black
  vignette at the rim of the view, drawn denser by `guard_strain_dark_density` (2.5)
  so it hides more over D2's dark scenes, beneath the blood on the visor, creeping
  inward as it builds. **VR Settings > Shield > Guard strain shadow**
  (`guard_strain_vignette`, 1 = On) turns it Off: no shadow, but strain, guard breaks
  and chip damage still apply. A profile without that key that had chosen the old
  Guard strain color Off (`guard_strain_dark` 2) stays Off; `guard_strain_dark` and
  `guard_strain_color` are otherwise no longer read (the rim is black only);
  each block sends one smooth pulse in from the edge. At full strain the guard
  breaks: the shadow swells until only `guard_strain_clear` % (10) of the view is
  clear and breathes slowly (`guard_strain_breathe_ms`, 1600), with one alert tap on
  the left controller. The shield still stops every hit, but through a broken
  guard each block leaks a chip (below); the other cost is sight.
  Strain holds while hits keep reaching the shield. Once no hit (block or parry)
  has reached it for `guard_strain_recover_ms` (1500; 0 = only lowering drains), it
  drains with the shield still up; the next hit stops the drain and restarts the
  wait. Lowering it (or a menu, or losing the shield) drains at once, without the
  wait. Either way full strain drains in `guard_strain_reset_ms` (1500), so a broken guard left alone clears 3 s after the last hit; a broken guard
  stays broken until the shadow has fully faded, and raising the shield again part
  way keeps what is left. Opacity is `guard_strain_light` (100 %, 0 = none; strain
  still runs). The shadow's inner blend is
  `guard_strain_softness` % of the visor radius (45) about a fixed midpoint, so it
  fades toward the centre with no visible inner rim. Pulses re-trigger without
  dropping, so rapid blocks hold a swell rather than flashing.
- **Low-life heartbeat** (Classic and Unleashed; guard strain no longer drives it):
  **VR Settings > Survival > Heartbeat starts at** (`low_life_heart_start`, 60 %; Off or
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
leech have already run. Scaled: the local player's physical swings, trigger/button
melee, arrows and throws, and every forced incoming hit. Not scaled: spells (yours
or theirs; they never roll to hit), thorns, and anything that never passes a hit
check.

**No flinching (Unleashed, 2026-10-22):** the hero never enters the game's hit
recovery (GetHit) or block animation when struck; Classic keeps native flinching,
so FHR and FBR still work there. Natively both lock out casts, button skills and
movement for their FHR / FBR frames, which a VR body cannot follow and which
stun-locks in a crowd. The game's unit mode setter (34E5F0) refuses
those two modes for the local hero's server and client units, so the hero keeps
whatever it was doing; the hit's damage is unchanged. Physical swings never waited
on hit recovery anyway (a contact calls the native attack directly). Knockback stays
native, but ends in Neutral: the game ends every knockback by asking for GetHit, and
refusing that left the hero in knockback, asking again at each animation end, locked
in place until save and exit (Duriel's stunning Smite, 2026-10-24). Any flinch asked
for while in knockback now sets Neutral instead (`knockback_to_neutral=` in the
`Frame flinch:` line). `[gameplay] player_flinch` (default 0) restores native flinching with 1;
applied live. Developer builds log `Frame flinch:` counts and, once per distinct
call chain (at most 8 a session), a `Frame flinch probe:` line naming the game code
that asked for the flinch.

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
chance to hit. The log's `Frame incoming defense:` line counts scaled hits. It
needs the combat hooks, which `contact_hits=1` (the default) installs: with
`contact_hits=0` monsters roll to hit natively whatever this key says.

**Would-have-hit gate (Unleashed):** contact always lands, both ways - a monster's
melee hit or missile on you (`enemies_always_hit`) and your physical swing - and
its damage follows the Unleashed formula above. Every other effect of that attack
delivers natively: only when the attack's native to-hit roll would have hit. The
verdict is the game's own, not an average: the forced hit check still advances the
attacker's real random seed once, and that roll against the clamped chance is what
the game would have compared. On a would-miss contact:
- **Off:** the attacker's on-hit procs (crushing blow, open wounds, freeze, slow,
  knockback, chance to cast on striking, curses such as Amplify from a monster's
  hit, Iron Maiden on a cursed attacker), the struck unit's when-struck effects
  (chance to cast when struck, damage taken goes to mana, attacker takes lightning
  damage, Frozen Armor, Life Tap's heal), life/mana/stamina leech and drain, mana
  burn, chill, freeze and stun lengths, absorb heal, monster mods' on-hit
  effects, Thorns' damage return, prevent monster heal, defense lowered per hit,
  and item wear (your armor when struck, your weapon when you strike).
- **As a miss:** effects the game fires on a miss as well - chance to cast on
  attack and attacked-in-melee (attacker takes damage, Shiver Armor) - run as if
  the attack had missed.
- **Unchanged:** critical and deadly strike (they multiply the damage, which the
  chance already scales), blocks, parries, chip, guard strain and the block's
  shield wear (designed), spells and enchantment procs (never roll to hit),
  on-kill effects. A riposte is a clean hit and always carries its effects.
- **Damage over time** - poison and burn - is damage that never reaches the scaled
  life write: it takes the hit's own factor (Defense, or attack rating x recovery
  x cleave, a riposte's bonus) on every contact.
- **Absorb skills** (Energy Shield, Bone Armor, Cyclone Armor, Psychic Ward,
  Health Link: the absorbdamage event, before the life write) see the contact's
  damage at its factor and what they leave is scaled back, so they soak and drain
  their mana or pool at native-average rates.
On average every effect then lands as often as natively. Your arrows and throws
and trigger/button melee are covered (above; Smite, which always hits natively, is not gated). Each part is checked against the build
at start (`Frame would-hit gate: verdict= events= effects= ...`); a part not
found stays as before (its effects on every contact). `Frame would-hit gate:
incoming would hit= would miss= swings ...` counts verdicts; developer builds add
what was switched off (`Frame would-hit gate native:`). `Frame guard native:`
ends in `would_hit=1/0`.
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

**Arrow path line** (**VR Settings > Targets > Arrow path line**, `[bow] path_laser_hue`,
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

**Thrown weapon lift** (**VR Settings > Combat > Thrown weapon lift**, `[gameplay] throw_lift_deg`,
10° by default): a thrown javelin, knife or axe leaves along the throwing
hand's recent motion, which dips as the hand follows through. Every throw,
from either hand and with any javelin skill or Double Throw, and every tossed
trap, leaves this many degrees above that path, keeping its heading; -10° to
30° in 5° steps, Off at 0°. Shown for the Amazon, Barbarian and Assassin, or
any class holding a thrown weapon.
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

## Druid shapeshift forms in first person

The game draws Werewolf and Werebear with enemy models
(`data/hd/character/enemy/wolf`, `.../bear`) that carry only claws: no weapon
or shield bones. While the player has the form's state (139 Werewolf, 140
Werebear), the skeleton hook takes that model as the hero if it stands within
2 units of the player and stands (by its hips: the game's werewolf bites with
its head low, and the head test a person's rig uses left those frames to the
game). A rig taken as the hero is kept while the game still
draws it (40 ms, about three frames) unless another is clearly nearer (a summon
can share the rig), and the druid's own model is not posed while a form rig is
drawn. The state sample trails the game's swap of the models by up to a few
hundred ms; a form's rig is taken as soon as the rig the sample still names
(the druid's, or the other form's) has stopped being drawn, and the druid's
model as soon as the form's has. This works for any class shifted by an item
(Beast, Wolfhowl). While a rig is drawn as the hero, it - not the sample - says
whether the player is in form (the sample can read human for a second or two
after a load), so the shield guard, the left claw and the form's attack cycle
never drop out mid-form. Left to the game, a werewolf's first frames
drew its own unscaled beast leaning out ahead of the eyes (its back in view).

- **Grafted under the player** (first person only; third person and flat menus
  show the game's form, unscaled). The player sees arms, torso and legs, so the
  form's body goes where the player's would be. Its torso is held tipped
  upright about the spine root (the authored werewolf leans 66° with its head
  below its shoulders), its shoulders sit under the eyes where a person's do
  (the druid's proportions), and its own neck and head are hidden.
- **Its own height.** The werewolf's eyes stand at 98 % of the druid's, the
  werebear's at 112 % (6.93 and 7.92 in the druid's model units, her eyes 7.07;
  the forms' models are drawn at world scale 1.0 against her 0.95, and the
  heights and the world's scale are taken across). Only the camera moves: the
  world keeps the druid's scale (units per metre from the druid's eyes), and
  the hands and arms hang by that same scale. The rig is drawn at the uniform
  scale that puts its upright shoulders under those eyes (about 0.76 for the
  werewolf, 0.92 for the werebear), from the feet. Taking a form eases the eye height over half a
  second (once per stereo pair); returning to human form takes the druid's at
  once, and a person's rig is never drawn scaled. `[shapeshift] wolf_height` /
  `bear_height` (% of the form's own height, 50–150, default 100) adjust it.
- **No idle sway.** Above the hips the body is held as for every class, and a
  form's hips are held at rest too. While the form stands still its legs and
  tail are held; while it walks or runs they take the game's stride, hung from
  the held hips, with the feet kept at the stride's height by leg IK (150 ms
  blend either way). At the drawn scale the stride is shorter than the ground
  covered, so the feet may slide.
- **Arms on the controllers,** fitted to the player's reach from a person's
  shoulders rather than the form's (the werebear's shoulders are 3.11 units
  apart against the druid's 1.72; fitted to the T-pose span from those, its
  arms were cut to half and a forward reach fell short). Forms may fit down
  to 0.5 of their authored length, and beyond full reach a form's arm
  lengthens (up to twice, as the shield arm does) so the paw stays on the
  controller. Both paws are free hands: the grip curls the claws.
- **Equipment is stats without a visual.** In form, aiming and holding treat
  the player as unarmed: no bow draw, crossbow, javelin rod, staff or two-hand
  grip, and no physical shield: the shield's block chance goes to the native
  block roll. Physical block, parry, guard strain and chip damage don't apply
  in form.
- **Claw strikes.** Each paw is a physical claw: a capsule from the palm's
  middle to past the middle finger's last joint, half the knuckle span thick,
  measured on the paw's rest shape hung on the drawn wrist (a grip curls the
  drawn fingers into a fist; the claw keeps its reach and its forward
  direction). Both paws strike, with the existing swing and
  thrust thresholds (release both triggers), the left claw too though a druid
  never dual-wields; each claw has its own strokes,
  hit list and cleave, and both share **one recovery** in Unleashed (and one
  attack cycle in Classic). A claw hit is the native basic attack with the
  druid's one weapon, so the form's damage, criticals, procs and leech stay
  native; a weapon contact taps the paw's own controller. Both paws can break
  barrels and crates. Contacts one batch applies out of time order never run
  the recovery clock back: a claw timed before the latest landed hit is not
  recovered. Skills (Fury, Maul,
  Feral Rage, Rabies, Hunger...) stay trigger/button casts.
- **The form's attack cycle** (recovery in Unleashed, pacing in Classic) is the
  D2R 2.4 wereform normal attack (`attack_speed.h` `WereformFrames`): the
  druid's A1 animation speed and start frame for the weapon class, the
  increment slowed by the form's A1 length over the human's, the form's own
  length (Werewolf 13, Werebear 12), and EIAS from item IAS plus the unit's
  attackrate (ItemStatCost 68: Werewolf and Maul skill speed, auras, less WSM
  and slows), capped at −85…150. It matches ChthonVII's 2.4 wereform
  calculator for every weapon class and EIAS. `Frame melee attack speed:`
  shows `attackrate=` and `form=`; `Frame claws:` counts landed hits per paw.

F10's skeleton dump adds each bone's rest translation (`b x y z`). Each learned
form rig logs one `Frame shapeshift body:` line (rest-pose head, lean, upright
shoulders, rig eye, legs, fingers); `VR body pose:` adds `form`, `scale` and
`legs_held`. Developer builds also log a form rig rejected for standing more
than 2 units from the player, and, when they change (at most every 5 s), the
form rig frames posed ahead of the state sample, left to the game (not
standing, another rig taken) and grafted without the neck anchor.

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
  **Windows Mono audio is on** at its top on every page; pointing at it
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

### Druid shapeshift gestures

A Druid changes form by performing the transformation, in either melee rules
mode, while **VR Settings > Combat > Skills on your moves** is on (as the other
gesture skills):

- **Werewolf:** cross both arms over your chest (each hand on the opposite side),
  bow forward over them, then wrench upright while flinging both arms up and
  outward.
- **Werebear:** hold both hands above your head for a moment, then slam them
  down together to waist height or lower.

The gesture presses the controller button the shape skill is assigned to, so the
skill must be on one of the ten slots; the native cast, mana, cooldown and the
transformation itself stay native. Performing the gesture again while
transformed casts the skill again, which returns you to human form; a 1.5 s
cooldown after each cast stops one sloppy performance from casting twice. A
performance is ignored while a hand is busy (trigger squeezed, bottle held, bow
drawn, face wipe), while menus or the wheel are open, and without the skill on a
button (the log says which skill was not assigned). Both controllers pulse once
when the cast is issued. Thresholds live in `shapeshift::Tuning`
(`steamvr/shapeshift_gesture.h`); `shapeshift-gesture-test` walks both
performances, lazy and partial versions, loot stooping and overhead swings.

### Paladin Blessed Hammer fist pump

A Paladin casts Blessed Hammer by punching a fist up to the sky, while
**VR Settings > Combat > Skills on your moves** is on:

- Make a fist (hold the grip, trigger free) with either hand somewhere below
  your head, then punch it straight up so it ends above your head.
- One pump casts one hammer. Bring the fist back down below your head to load
  the next pump; a fist held up high never repeats.
- The punch counts from where it began (the bottom of the wind-up), so an
  ordinary accelerating punch from the shoulder or hip qualifies: at least
  25 cm of rise, ending above your head.
- The skill picker and the hand wheel tag Blessed Hammer GESTURE on any slot
  ("Punch up to cast").

The pump presses the controller button Blessed Hammer is assigned to, so the
skill must be on one of the ten slots (the trigger still casts it as before);
mana, cast rate and the hammer's spiral stay native, and a pump faster than the
game's cast rate is a press the game ignores, as with the button. The hammer's
heading comes from the usual aim rules (the outlined monster, else where you
point or look). A pump is ignored while that hand is busy (trigger squeezed,
bottle or other held item, bow drawn, face wipe) and while menus or the wheel
are open. The punching hand gets a short pulse when the cast is issued. The log
(`Frame hammer:`) has the first cast and a running count every 25, a line for
each abandoned punch (why), and a line when the skill is unavailable.
Thresholds live in `hammerpump::Tuning` (`steamvr/hammer_gesture.h`);
`hammer-gesture-test` covers both hands, holding high, re-loading, open hands,
slow raises, stalled and forward punches.

### Paladin Charge punch

A Paladin casts Charge by punching straight out with the weapon hand, while
**VR Settings > Combat > Skills on your moves** is on:

- Make a fist with the weapon hand (hold the grip, trigger free), drawn back
  near your body, then punch it straight out ahead, extending the arm.
- One punch casts once. Draw the fist back in to load the next; an arm held
  out never repeats.
- The punch counts from the most drawn-back point, so an ordinary accelerating
  punch qualifies: at least 20 cm of travel, ending with the arm extended.
- The skill picker and the hand wheel tag Charge GESTURE on any slot ("Punch
  out to charge").
- Physical melee contacts pause while a Charge is in flight (however it was
  cast), from the press until the hero leaves the rush (mode 18, at most 4 s).
  A swing or the punch itself touching a monster mid-rush used to land a basic
  attack inside the Charge sequence and leave the hero unable to move. The log
  has one `Frame charge: melee contacts held N ms during the charge` line per
  Charge.

The punch presses the controller button Charge is assigned to (the skill must be
on one of the ten slots; the trigger still casts it as before). The aim rules
hand Charge the monster the weapon hand points at, else the spot it points to;
the rush, mana and hit stay native. "Extended" scales with the calibrated arm
span (28 % of it, 0.38-0.60 m ahead of the head; 0.45 m without calibration).
The weapon hand is the right hand. A punch is ignored while that hand is busy
(trigger squeezed, bottle or other held item, bow drawn, face wipe) and while
menus or the wheel are open. The weapon hand gets a short pulse when the cast
is issued. The log (`Frame charge:`) has the first cast and a running count
every 25, a line for each abandoned punch (why), and a line when the skill is
unavailable. Thresholds live in `chargepunch::Tuning`
(`steamvr/charge_gesture.h`); `charge-gesture-test` covers headings, holding
out, re-loading, open and busy hands, slow reaches, half punches, the hammer's
upward punch, chops, slashes, jabs from an extended arm, and arm-span scaling.

### Ice Bolt visual height correction

New or missing combat preferences default to **Aim > Keep spell shots level: Off** and
**Aim > Spell aim assist: Strong**. Players can still choose level shots, Light
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
