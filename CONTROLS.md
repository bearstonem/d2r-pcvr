# D2R PCVR controls

Windows preview for **SteamVR and Virtual Desktop (VDXR)**. These controls
use two tracked VR controllers; an Xbox controller is not required.

## Button names

| Hand / button | Quest / Touch | Valve Index | Steam Frame |
| --- | --- | --- | --- |
| Right primary / secondary | A / B | A / B | A / B |
| Left primary / secondary | X / Y | A / B | D-pad Down / Up |
| Trigger | Index trigger | Index trigger | Index trigger |
| Grip | Grip button | Squeeze | Grip input |
| Stick click | Press the thumbstick | Press the thumbstick | Press the thumbstick |

**Left primary** below means **Quest X / Index A / Frame D-pad Down**.
**Left secondary** means **Quest Y / Index B / Frame D-pad Up**.

Wheel button badges, grip combinations, skill-picker headings and the Controls
help use the active controller layout. Open either wheel > **VR SETTINGS** >
**VR**, then use the **Headset / controllers** dropdown to choose **Automatic**
(default), **Quest / Touch**, **Valve Index**, **Steam Frame**, or **Generic**.
Select **Apply** to save; closing without Apply discards the choice.

Automatic reads each hand's active OpenXR interaction profile and refreshes
when controllers change. This also supports mixed controller layouts. Unknown
profiles display Primary / Secondary rather than guessed button letters. The
legacy WMR profile displays Unbound for face slots, which it does not bind.
The override changes labels only; it preserves skill assignments and controls.
Custom SteamVR remappings may need the Generic labels.

Steam Frame uses its native profile when the runtime advertises it. With an
older runtime's Touch compatibility binding, Automatic can report Touch;
select Steam Frame manually to show its D-pad labels. The compatibility binding
also aliases the upper face/D-pad buttons to the secondary action; the wheel
shows B and D-pad Up as the representative controls. See
[Valve's Frame input documentation](https://partner.steamgames.com/doc/steamhardware/steamframe/input)
and the [OpenXR controller profiles](https://registry.khronos.org/OpenXR/specs/1.1/html/xrspec.html#interaction-profiles).

The saved override is `[interactions] controller_labels`: 0 Automatic, 1 Quest /
Touch, 2 Index, 3 Frame, 4 Generic. Missing or invalid values use Automatic.

## Gameplay

With **Motion combat** enabled, each hand has its own saved five-button layout.
Quest left X/Y correspond to Index left A/B; right A/B labels are the same.

| Action | VR control |
| --- | --- |
| Walk / turn | Left stick / right stick, including with a wheel open; snap by default, with optional smooth turning |
| Open or close a hand wheel | Click that hand's thumbstick; it stays open without holding |
| Switch to the other wheel | Click the other thumbstick |
| Assign a button | Opposite hand's laser + trigger: select a wedge, then a skill |
| Finish editing | Click the wheel hand's stick again, or select CLOSE in its center |
| Inventory / character / skills / journal | Left Grip + left stick click; or either wheel > INVENTORY; native tabs retain their controls |
| VR settings / body calibration | Either wheel → VR SETTINGS |
| Minimap | Either wheel → MAP; movement remains available |
| Reset height | VR Settings > Reset eye height |
| Adjust controller vibration | **VR Settings > Haptics**: overall strength plus separate melee, bow draw, bow/throw release, spell, interaction and resource-alert strengths. Tests match each action's hands; **Apply** saves |
| Hold to show all nearby loot labels (Alt) | Hold **Right Grip + Right Trigger** during gameplay, grip first. Labels stay shown while both are held; release either to restore normal drop/hover labels. Uses the native Alt range, independently of where you aim. No cast or shot accompanies the combo. Also listed in **VR Settings > Controls** |
| Interact / pick up / talk | Right A by default; requires an aimed interactable. One press acts on one target: press again for each item, including loot a chest just dropped |
| Free melee swing / thrust | Move the equipped weapon through an enemy |
| Bow draw | Right trigger near the left-hand bow string; draw and release |
| Javelin throw | Clear the right trigger skill for basic weapon mode; hold trigger, throw, release |
| Cancel a physical attack / draw | Open either wheel, then release the held controls |
| Swap weapon sets I / II | Dominant grip over either shoulder; or wheel → EQUIPMENT |
| Stow / draw weapons | Wheel → EQUIPMENT → Stow / Draw |
| Grab / drink potion | Grip a belt bottle, bring its opening to the mouth and tip it; release to return unused |
| Quick potion | Wheel → EQUIPMENT → potion, using opposite laser + trigger |
| Grab wheel potion | Wheel → EQUIPMENT → potion, using the pointing hand's grip |
| Wrist health / mana / XP | Look at the back of the left wrist |
| Wipe blood off the visor | Scrub either hand side to side in front of your face, trigger released. About six strokes clear a full visor. **HUD > Face wipe gesture** turns this off |
| Recenter / stereo capture | PC F11 / F8 |

Editing consumes combat inputs but never pauses the game or zeros locomotion.
Selection clicks do not cast. Release buttons and triggers after closing before
using them in combat. Menu, tracking, equipment and assignment changes also
require a fresh press. Only one wheel is open at a time.

Interact latches the target present when you press A. Pressing at empty space
does not wait for an item to come into range. Release and press again for each
new target. A release during an inventory/NPC menu rearms interaction; holding
the button through a menu or game-session change cannot reuse the old target.

On the **VR** settings tab, **Smooth turning** defaults to **Off** (30-degree
snap turns). Turn it **On**, then **Apply**, for continuous right-stick turning.
**Snap turn angle** and **Smooth turn speed** keep their values when switching
modes. Existing smooth-turn profiles retain their selected mode.

The **Haptics** tab offers **Weapon / Shield** tests for melee, a combined bow-draw
test, **Bow / Throw** release tests, **Test L / Test R** for spells and interactions,
and one left-hand alert test. Tests use the displayed values before Apply.
**Short melee pulses** defaults Off: weapon impacts last 90 ms and shield impacts
45 ms at the controller's preferred frequency. On selects the earlier 10 ms /
180 Hz pulse. **HUD > Low health / mana alerts** must also be enabled for
automatic resource warnings.

**HUD > Blood on visor** (default On) splashes blood around the edge of the
view as life drops below 60%, 40% and 20%; large splats drip slowly, then dry.
Healing back above a band, from any source, fades that band's blood out over
about a second. Changing maximum life alone (Battle Orders, Oak Sage, +life gear)
neither splashes nor fades. With **HUD > Face wipe gesture** On (the default),
scrubbing a hand side to side in front of the face wipes blood
at any time: each stroke ticks the controller and smears some splats away. While
you are still hurt, the visor then stays clean until the next hit splashes it
again. The centre of the view always stays clear. While a hand is scrubbing it
does not attack; a held potion, squeezed trigger or bow draw never counts as
wiping.

## Hand skill layouts

| Slot | Quest left | Quest right | Index, either hand |
| --- | --- | --- | --- |
| Primary skill | Trigger | Trigger | Trigger |
| Second action | X | A (Interact by default) | A |
| Third action | Y | B | B |
| Alternate second | Left Grip + X | Right Grip + A | Same-hand Grip + A |
| Alternate third | Left Grip + Y | Right Grip + B | Same-hand Grip + B |

There is no left-trigger modifier layer. Each hand aims its own spells. Face
shortcuts do not change either trigger assignment. On a Sorceress, Fire Bolt,
Fireball, Ice Bolt, Ice Blast and Charged Bolt can fire from both hands simultaneously,
each with independent aim and its own Faster Cast Rate timing. Hold the input to repeat. Native
mana costs apply to each cast, and walking remains available without entering
the original casting animation. See [hand projectile spells](docs/hand_projectile_spells.md)
for the experimental adapter's scope and offline testing checklist.

**VR Settings > Controls > Planar spell aiming** defaults to Off for new or
missing settings, allowing up/down aiming of straight projectile spells. Turn
it On and Apply to keep those spells level; Charged Bolt keeps its native planar
spread. Existing saved preferences are retained, and changes apply to new casts.

**Projectile aim assist** on the same tab defaults to Strong for new or missing
settings; existing saved choices are retained. Off disables assistance. Light corrects nearby
misses at launch; Strong adds gentle homing toward the enemy selected at launch
for straight hand spells. Charged Bolt remains launch-only. This does not affect
bows, thrown weapons or other native spells, and does not guarantee a hit.

Other skills retain native repeat behavior, delays and animation restrictions.
A native action has one owner: the first held input wins, with right-hand
priority for simultaneous presses. Native and physical casts do not start
together. A competing input must be released and pressed again; it is never
queued to fire unexpectedly when the first hand stops.

Select a wheel wedge to edit its binding directly. **CLEAR SLOT** removes the
skill; clearing right A restores Interact, and clearing right trigger restores
basic weapon handling. Interact can also be chosen explicitly from the picker.
Assigning a spell to right A deliberately replaces its interaction action.
Grip changes cannot switch a held face button to another spell: release the
face button first. Held potions and holsters take priority over spell shortcuts.

Bindings use the game's controller assignments, saved by the game per character
and weapon-set bank. Existing assignments are retained in these native slots:

| Hand | Trigger | Primary face | Secondary face | Grip + primary | Grip + secondary |
| --- | --- | --- | --- | --- | --- |
| Right | B | A | X | Y | RB |
| Left | LT + B | LT + X | LT + Y | LT + RB | LT + A |

These are underlying **native gamepad slots**, not physical VR button labels.
Native duplicate-assignment rules still apply: assigning a skill may clear its
previous native slot. Native menus show their usual gamepad prompts. The mod
does not directly edit saves or invent extra game-owned skill records.

## Weapon skill gestures

Active skill icons and name panels beside the weapons are **off by default**.
Review assignments by opening either hand's wheel. To restore the weapon
indicators, open **VR Settings > Controls > Weapon skill indicators**, turn it
**On**, and press **Apply**. This changes both hands' visual indicators only;
assigned spells and weapon skill gestures keep working. The saved preference
is `[interactions] weapon_skill_indicators=0` (hidden) or `1` (shown).

The trigger skill is the persistent gesture skill for its hand. Optional
hand-side icons show the assignments. Left-trigger Smite supports physical shield swings
and thrusts when a shield and its native hook are available. Supported arrow
skills assigned to left trigger fire on bow draw/release using the right hand
to pull the string. The left trigger remains a button fallback; right trigger
away from the string uses the right hand's own assignment.

Face-button skills never replace gesture skills. Ordinary melee swings remain
basic attacks for skills without a supported physical adapter; assigning a
spell does not cast it on every swing. Physical skill gestures can be disabled
under **VR settings → VR → Weapon skill gestures** (on by default), retaining
button casting and basic physical attacks. This implementation supports the
existing Smite and bow adapters; it does not generalize arbitrary melee skills.

Turning gestures off or changing equipment/assignments cancels an in-progress
skill draw. Shield contact and its trigger fallback cannot duplicate a cast.
Projectile targets follow the casting hand rather than locomotion direction.
The five supported Sorceress projectile spells bypass the native casting
animation; other skills can still stop movement through their native animation.
The new physical casting path still requires offline headset validation.

## Menus: laser and native buttons

Both methods work in the main menu and native game panels, including inventory,
stash and merchants. Keep the D2R window focused on the PC.

Use a **stick or face button** to start native button navigation and follow the
game's displayed prompts. The idle laser will not move your native selection.
To return to the laser, release the native controls and deliberately move your
aim across the panel. A small hand tremor will not change methods.

### Laser controls

| Action | VR control |
| --- | --- |
| Hover / point | Aim the right controller's laser |
| Select / drag an item | Right trigger; hold while moving the item and release to place it |
| Context / alternate click | Left Grip + Right Trigger (hold grip first) |
| Native Ctrl-click shortcut | Hold Left Trigger, then click with Right Trigger; the game chooses drop, sell or transfer for the current panel |
| Native Shift modifier | Hold Right Grip while using the laser |
| Back / close | Right B, or point and click the panel's close/back button |
| Tabs / page controls | Point and click the native on-screen controls |

Dragging keeps the item held if the ray briefly leaves the panel. Releasing
the trigger ends the drag at the last valid pointer position. Tracking or
desktop focus loss releases the injected mouse button; release the trigger
before starting again. Pressing or releasing Left Grip during a drag will
not convert it into a context-click.

### Native menu prompts

| Game's displayed input | Quest control | Steam Frame control |
| --- | --- | --- |
| A | Right A | Right A |
| B / back | Right B | Right B |
| X | Left X | Left A |
| Y | Left Y | Left B |
| Left stick / right stick | Left stick / right stick | Left stick / right stick |
| LS / RS click | Left stick click / right stick click | Left stick click / right stick click |
| Inventory: X / drop selected item | Left X; hold if the native prompt requires it | Left A; hold if the native prompt requires it |
| Inventory: hold RS / sort | Press and hold right stick click | Press and hold right stick click |
| LB / RB | Left Grip / Right Grip | Left Grip / Right Grip |
| LT / RT | Left Trigger / Right Trigger | Left Trigger / Right Trigger |
| Menu / Start, where available to the game | Controller menu button | Controller menu button |

Native menu actions use the same buttons in the main menu, tabbed menu, stash,
inventory and merchant panels. **Inside a native menu, left primary performs the displayed X action.**
During motion gameplay, Inventory and VR Settings are available in either wheel.
Right-stick click toggles the right wheel during gameplay. Use the shoulder
gesture or **EQUIPMENT / BELT** wheel for weapon configurations.

Quest controllers do not need a separate D-pad or Xbox controller: use sticks
for native navigation and the laser for the on-screen controls. When using
the laser, Right Trigger is a mouse click and the grips/triggers above provide
its modifiers; once native navigation is selected, they perform LB/RB/LT/RT.
Release buttons, triggers and sticks when opening or closing menus so a held
input cannot also act in the next screen or in gameplay.

The mod's own settings and calibration panels use their VR laser controls:
right laser + trigger to select, Right B to close/cancel. Native gamepad input
does not pass through those panels.
