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
Ground placements, buffs, summons and corpse-only skills keep native targeting.
Host tests and native signature/table checks pass; this aim correction still
needs a headset check with Teeth. Immediate hand-origin **3D** spell flight/contact
remains pending: missiles still use the native ground plane, animation, effects,
spread/homing, mana, charges and casting cadence. Spell gestures are excluded.

Revision 4 matches controller input to the rendered weapon's timestamp and uses
that pose's world placement, fixing missed contacts during fast motion. Its
synthetic timing regression passes, and the owner reported **"works great"** in
Steam Frame on 2026-10-09 after the sword comparison. Other weapon categories and
creature sizes still need checks.

Fresh installs enable motion controls in `GAME/d2rloader/plugins/d2r_vr.ini`:

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
| Quick cast without opening wheel | Hold Left Trigger, point right hand and press a face button; add Left Grip for the alternate four slots (mapping below) |
| Melee swing / thrust | Swing the equipped melee weapon through a creature, including with a skill selected; no grip, trigger, or preselected target |
| Button attack / crossbow | Weapon combat mode, right trigger; native crossbow firing |
| Nock bow | Bow equipped: bring right hand to string and hold right trigger; no left grip required |
| Draw and shoot | Pull string hand back at least 12 cm (28 cm total separation), then release trigger |
| Throw a javelin | Javelin equipped in WEAPON mode: hold right trigger, make a deliberate throwing motion, release to throw along the hand's motion. Native basic throw consumes ammunition; trigger-free jabs stay available |
| Cancel active grab/draw | Right B; release inputs before starting again |
| Physical belt potion | Reach to an original 3D bottle on the front belt and hold **Right Grip** to grab; left-hand grip also works |
| Drink | Keep grip held, bring the bottle's opening to your mouth and tip nearly horizontal for 0.25 seconds; release to return an unused potion |
| Quick potion | Left Y → **EQUIPMENT / BELT** → trigger a potion wedge; grip on a potion wedge instead grabs it into the right hand |
| Swap weapon sets | Reach the swap hand over either shoulder and press its grip; right hand by default, selectable in VR settings. Hip anchors are removed |
| Seated weapon selection | Utility wheel → **Weapon I** or **Weapon II** |
| Stow/draw | Utility wheel → **Stow weapons** / **Draw weapons**; native equipped stats stay unchanged |
| Health/mana | Check the back of your left wrist, or choose **Palm** in **VR Settings > HUD** and hold your left palm upward in front of you briefly. No button required; automatic alerts are off by default |
| Show all nearby loot labels (Alt) | Hold **Right Grip + Right Trigger**, grip first, to hold the native Show Items action. Release either to restore normal drop/hover labels; listed in **VR Settings > Controls** |
| Explored minimap | Hold Left Y ? **MAP** in the bottom tabs; point right laser + trigger. **SPELLS** returns to skills; releasing Left Y closes the menu |
| Quest journal | Open the native tabbed game menu with Left X / Frame Left A, then select Journal. The duplicate wheel entry is removed |
| Journal controls | Right laser + trigger uses native page controls/act tabs; Right B follows native back; Left X performs native X |
| Inventory drop / sort | Native X (Quest Left X / Frame Left A) / press and hold right stick click; right-stick click no longer swaps in gameplay |
| Walk / snap turn | Sticks; paused while wheel or journal is open |
| Inventory menu / height reset | Left grip + left stick click / VR Settings > Reset eye height |

Dropped-item names use **VR Settings > HUD > Loot label size**: **45%** by
default, adjustable from **20% to 150%** in five-percentage-point steps.
Apply saves `[hud] labels_size` in `d2r_vr.ini` and updates it live; existing
saved sizes are preserved. Text, dark backing bounds and click targets scale
together; item-quality colors are preserved. **Right Grip + Right
Trigger** holds the native **Show Items (Alt)** action, with a short vibration
when the hold starts. All nearby loot labels stay shown while both buttons are
held, using the game's native Alt range rather than a VR aiming or pickup radius.
Release either button to restore normal labels for newly dropped or hovered items. It consumes
the trigger squeeze without casting or shooting, including if you release grip first.
Hold grip before squeezing the trigger; adding grip to an existing attack does
not turn that attack into a loot-label hold. Analog hysteresis tolerates squeeze noise.
Release the combo before casting or starting a grip interaction again.
Use it outside menus and wheels, with no potion or holster interaction active.
Normal right-stick clicks still toggle the wheel; native menu hold-to-sort stays
available. Menus, tracking/focus loss, leaving VR or changing character release
the command. Release the trigger before starting a fresh hold after an interruption;
there is no saved toggle or automatic resume.
The game's native item/filter rules apply. Inventory tooltips and monster labels
keep their styling.

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
| Back / close | Right B, or the native close button |

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

The panel opens in front of the headset and stays in room space. It has six
tabs and a **Reset eye height** button:

| Tab | Contents |
|---|---|
| Controls | Left/right controller reference for movement, menus, melee, bow, spells, potions, holsters and height reset |
| VR | Controller labels, motion combat, seated belt position, dominant hand, smooth/snap turning, belt height and reach, weapon skill gestures and ordinary mob bars |
| Video | Foveation Off / Fixed / Eye tracked, actual VR source size, added sky, fog, dungeon ceilings, terrain and object draw distances, VR render resolution |
| HUD | Health/mana location and tilt, optional resource alerts, navigation location, menu distance and tooltip timing |
| Combat | Melee rules, swing hit speed and thrust speed |
| Haptics | Overall strength and separate melee, bow draw, bow/throw release, spell, interaction and resource-alert strengths; tests follow each action's hands |

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

Click **Apply** to save changed values to `d2rloader/plugins/d2r_vr.ini` and
reload them live. Closing without Apply discards pending edits. The first Apply
creates `d2r_vr.ini.before-vr-menu`; later saves retain that original backup.
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

Quick casting uses the **same saved bindings**, while keeping the selected
right-trigger ability. Point with the right hand, hold **Left Trigger**, then
press one face button. Hold the button to use the native repeat/channel behavior;
release it to stop. Add **Left Grip** before pressing for the alternate slot.

| VR face button while Left Trigger is held | Native binding | With Left Grip also held |
|---|---|---|
| Right A | B | LT + B |
| Right B | X | LT + X |
| Left A on Frame / X on Quest | Y | LT + Y |
| Left B on Frame / Y on Quest | RB | LT + RB |

The wheel shows these VR shortcuts below the skill names. These chords require
motion controls and apply during gameplay; menus keep their pointer/modifier
controls. While quick casting, face buttons do not also open inventory/wheel,
interact, cancel, or open settings, and Left Grip cannot grab a belt potion or
holster. Entering the layer cancels a drawn bow without shooting. Release and
press again after changing the alternate-slot grip, tracking interruption or
weapon swap. Unassigned/unavailable slots cannot cast; costs and timing remain
native. Candidate host tests pass; headset quick-cast validation is pending.

Equipping a different melee weapon or bow selects weapon mode. Javelins and
throwing weapons use free-swing contacts; select the native Throw skill to throw.
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
Native reach, walls, resources, animation/cadence, damage, stun and knockback
remain authoritative. Left-hand haptics acknowledge native acceptance, not proof
of positive damage. Physical blocking/parrying is still pending.

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
stroke arms. Shield/Smite gesture thresholds remain unchanged. Swing speed
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
adds selected Smite shield contacts. Other off-hand strikes, physical shield blocks
and parries remain future work.

Only a confirmed physical contact overrides legacy melee grid reach. Native
line-of-sight, hostile relationship, creature life state, damage, elemental
resistances/immunities, criticals, leech, procs, durability, death and XP still run.
**Melee rules** (VR Settings > Combat, `[gameplay] melee_rules`, applied live,
mid-game) choose between two modes for physical swings and trigger/button melee:

- **Unleashed** (`0`, the default): every weapon contact strikes each
  creature once per stroke. No to-hit, block or dodge roll and no attack-speed
  gate (the `contact_hits` policy).
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
physical weapon throwing, haptic success signals,
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
