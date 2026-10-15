# D2R PCVR

Play **Diablo II: Resurrected in VR on Windows**, with support for
**SteamVR and Virtual Desktop (VDXR)**. Use motion controls to swing your
weapon, aim spells, drink belt potions and explore Sanctuary in first person.

[![Watch the D2R PCVR introduction on YouTube](https://github.com/bearstonem/d2r-pcvr/blob/main/docs/media/d2r-pcvr-introduction.jpg?raw=true)](https://www.youtube.com/watch?v=RpqVcaseReI)

**[Watch the introduction on YouTube](https://www.youtube.com/watch?v=RpqVcaseReI)** (10:27):
physical bow and melee combat, over-the-shoulder weapon swaps, raising
skeletons, aimed spells, belt potions and VR menus, recorded in the headset.

D2R PCVR builds on **[D2R VR by BodyWalkVR](https://github.com/natpoh/diablo2-vr-port)**,
the original VR mod for Diablo II: Resurrected. See [Credits](#credits).

**This is a mod for the original game, not a standalone game. You must purchase
and install your own legitimate copy of Diablo II: Resurrected.** The mod adds
VR support and motion interactions on top of your existing game installation.

The Windows preview supports **Steam 3.3.93787** and experimental
**Battle.net 3.3.93847** installations.
Run Setup (or **Install-Windows.cmd** in the ZIP) and choose **Steam** or **Battle.net** before choosing
your headset and connection. See
[Battle.net preview installation](INSTALL-WINDOWS.md#battlenet-preview-packages).
Battle.net has been confirmed with SteamVR; Battle.net with Virtual Desktop / VDXR
has not yet been confirmed.

**No Blizzard game assets are included in this repository, installer or release ZIP.**
The download contains the mod and licensed third-party components, not the
game, its executable, models, textures or audio. During installation, required
item/skill icons and potion models are read from your own installed game into
local caches. Keep those caches local; do not include them when sharing the mod.

**[Download the Windows preview](https://github.com/bearstonem/d2r-pcvr/releases)**
and follow **[Install and play](INSTALL-WINDOWS.md)**.
If the release offers **D2R-PCVR-Setup-VERSION.exe**, download and run it for
graphical setup. It keeps its helpers in
`%LOCALAPPDATA%\Programs\D2R PCVR`, includes a private Python runtime and
adds removal under **Windows Settings → Apps → Installed apps → D2R PCVR**.
Rerun Setup to update or repair while keeping calibration and video settings.

For the portable option (including v0.3.7), download **d2r-pcvr-windows.zip**,
extract it to a permanent folder, then double-click **Install-Windows.cmd**.
The setup wizard installs the mod, lets you choose your headset and connection,
and adds the matching Steam library shortcuts. It asks which game edition to
use and detects its installation folder.
No separate Python install is needed.
The ZIP also includes **Uninstall-Windows.cmd**. It keeps
your current characters and shared stash, restores files from the installation
backups and removes the selected edition's VR shortcuts. Keep the installation
backups, including those in older release folders, until removal is complete.

## What you need

- A Windows PC with your **purchased Steam copy of D2R 3.3.93787**, or
  **Battle.net copy of D2R 3.3.93847** (experimental support).
- The Steam desktop client, including for Battle.net VR library shortcuts.
- A VR headset and two tracked controllers.
- **SteamVR**, or **Virtual Desktop for Quest with its Windows Streamer**.
- Internet access during setup to download the required components.
- A **local offline character**. This preview is for offline single-player.

## Headset connections

| Headset | Connection |
| --- | --- |
| Steam Frame | SteamVR streaming |
| Quest 3 | Steam Link with SteamVR, or Virtual Desktop with VDXR |
| Quest 2 | The same Quest connection options; compatibility is not yet confirmed |

This is a PCVR mod: the game runs on your Windows PC and streams to the headset.

## In the game

- First-person VR with your character's body, hands and equipment.
- Free-swing melee, including with a wheel skill selected, aimed spells, bows and physical javelin throws.
- Belt potions you can grab and drink, plus wrist health, mana, level and experience progress.
- A left-hand minimap/compass with current area and act, independent health/mana tilt,
  and adjustable controller vibration under **VR Settings > Haptics**.
- A **MAP** tab at the bottom of the spell menu, showing explored areas and native markers.
- Selected Amazon arrow skills work with bow draw/release; selected Paladin Smite works with left-shield contact.
- Body calibration for your height and arm span, available on first load
  and through **Left Grip + left secondary → Calibrate body**.
- Weapon-set swapping: reach your right hand over either shoulder and press
  its grip. You can change the swap hand in VR settings.
- Inventory, skills, conversations and Journal through the game menu:
  **Frame Left A / Quest Left X**.

[Watch the earlier wrist HUD demo (v0.3.6, 12 seconds)](https://github.com/bearstonem/d2r-pcvr/releases/download/v0.3.6-windows-preview/d2r-pcvr-wrist-hud-demo.mp4).
The current preview offers wrist or palm placement for vitals and navigation,
alongside the larger map in the spell menu.

## Controller reference

Windows preview for **SteamVR and Virtual Desktop (VDXR)**. These controls
use two tracked VR controllers; an Xbox controller is not required.

### Button names

| Hand / button | Quest 2 / 3 label | Steam Frame label |
| --- | --- | --- |
| Right primary / secondary | A / B | A / B |
| Left primary / secondary | X / Y | A / B |
| Trigger | Index trigger | Index trigger |
| Grip | Grip button | Grip input |
| Stick click | Press the thumbstick | Press the thumbstick |

**Left primary** below means **Quest Left X / Frame Left A**.
**Left secondary** means **Quest Left Y / Frame Left B**.

### Gameplay

With **Motion combat** enabled, each hand has its own saved five-button layout.
Quest left X/Y correspond to Index left A/B; right A/B labels are the same.

| Action | VR control |
| --- | --- |
| Walk / turn | Left stick / right stick, including with a wheel open |
| Open or close a hand wheel | Click that hand's thumbstick; it stays open without holding |
| Switch to the other wheel | Click the other thumbstick |
| Assign a button | Opposite hand's laser + trigger: select a wedge, then a skill |
| Finish editing | Click the wheel hand's stick again, or select CLOSE in its center |
| Inventory / character / skills / journal | Left Grip + left stick click; or either wheel → INVENTORY |
| VR settings / body calibration | Either wheel → VR SETTINGS |
| Minimap | Either wheel → MAP; movement remains available |
| Reset height | VR Settings > Reset eye height |
| Adjust controller vibration | **VR Settings > Haptics**; preview each action and **Apply** to save |
| Hold to show nearby loot labels (Alt) | Hold **Right Grip + Right Trigger**, grip first; release either to restore normal drop/hover labels |
| Interact / pick up / talk | Right A by default; requires an aimed interactable |
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
| Recenter / stereo capture | PC F11 / F8 |

Editing consumes combat inputs but never pauses the game or zeros locomotion.
Selection clicks do not cast. Release buttons and triggers after closing before
using them in combat. Menu, tracking, equipment and assignment changes also
require a fresh press. Only one wheel is open at a time.

Ordinary monsters' health and name bars are hidden by default in VR. To show
them, open **VR settings > VR**, turn **Ordinary mob bars** On, then **Apply**.
Bosses, champions, uniques and their minions keep their bars with either setting.

### Hand skill layouts

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
settings. Strong adds launch assistance and gentle homing for supported straight
hand spells; Light assists only at launch, and Off disables assistance. Charged
Bolt uses launch assistance only. Bows, thrown weapons and other native spells
are unchanged. Existing saved choices are retained; Strong does not guarantee a hit.

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

### Weapon skill gestures

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

### Menus: laser and native buttons

Both methods work in the main menu and native game panels, including inventory,
stash and merchants. Keep the D2R window focused on the PC.

Use a **stick or face button** to start native button navigation and follow the
game's displayed prompts. The idle laser will not move your native selection.
To return to the laser, release the native controls and deliberately move your
aim across the panel. A small hand tremor will not change methods.

#### Laser controls

| Action | VR control |
| --- | --- |
| Hover / point | Aim the right controller's laser |
| Select / drag an item | Right trigger; hold while moving the item and release to place it |
| Context / alternate click | Left Grip + Right Trigger (hold grip first) |
| Native Ctrl-click shortcut | Hold Left Trigger, then click with Right Trigger; the game chooses drop, sell or transfer for the current panel |
| Native Shift modifier | Hold Right Grip while using the laser |
| Back / close | Right B, or point and click the panel's close/back button |
| Tabs / page controls | Point and click the native on-screen controls |

Hovering an item with the laser shows its tooltip immediately, including the
game's equipped-item comparison when a relevant equipped item exists. This
works in your inventory and merchant inventories without holding a modifier.

Dragging keeps the item held if the ray briefly leaves the panel. Releasing
the trigger ends the drag at the last valid pointer position. Tracking or
desktop focus loss releases the injected mouse button; release the trigger
before starting again. Pressing or releasing Left Grip during a drag will
not convert it into a context-click.

#### Native menu prompts

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

After setup, connect your headset and double-click **Launch-VR-SteamVR.cmd**
or **Launch-VR-VDXR.cmd**. Select an offline character when the game opens.
For ordinary desktop play, use Steam's normal **Play** button. Fully exit
the VR session before switching to desktop play or another headset connection.
You can also add separate **SteamVR** and **Virtual Desktop** entries to your
Steam library using the [shortcut instructions](INSTALL-WINDOWS.md#add-steam-library-shortcuts).

See [Install and play](INSTALL-WINDOWS.md) for setup, controls and removal.

For the complete additions overview, download the
[PDF](https://github.com/bearstonem/d2r-pcvr/blob/main/docs/D2R-PCVR-Additions.pdf) or
[editable Word document](https://github.com/bearstonem/d2r-pcvr/blob/main/docs/D2R-PCVR-Additions.docx).

## Contributing

To contribute, **fork this repository, create a branch in your fork, and open a
pull request against `main`**. Maintainers also use branches and pull requests;
direct pushes to `main` are blocked. Read [CONTRIBUTING.md](https://github.com/bearstonem/d2r-pcvr/blob/main/CONTRIBUTING.md) for
the workflow, review requirements, build checks and rules on game assets.
Use the [issue forms](https://github.com/bearstonem/d2r-pcvr/issues/new/choose)
to report bugs or propose larger changes.

## Credits

D2R PCVR is built on **[D2R VR](https://github.com/natpoh/diablo2-vr-port) by
[BodyWalkVR](https://bodywalkvr.com/mods/d2r-vr)**, the original VR mod for
Diablo II: Resurrected. Its D2RLoader plugin, first-person view, painted skies
and depth fog are the foundation this project started from, used under its
[MIT license](LICENSE). Thank you to BodyWalkVR for releasing it as open source.
Their original documentation is kept in [UPSTREAM.md](https://github.com/bearstonem/d2r-pcvr/blob/main/UPSTREAM.md). For their
own release, which runs through BodyWalk and FlatVR, visit the
[D2R VR page on BodyWalkVR](https://bodywalkvr.com/mods/d2r-vr).

D2R PCVR adds native OpenXR support for SteamVR and Virtual Desktop (VDXR),
motion combat, the physical bow and potions, VR menus, foveated rendering and
the guided Windows installer. Licenses for all third-party components are in
[THIRD_PARTY.md](THIRD_PARTY.md).

This project is not affiliated with or endorsed by Blizzard Entertainment.
