# D2R VR Unleashed v0.3.31 - Windows preview

This update lets you cast by drawing glyphs in the air, adds Paladin gestures,
loot beams and a pick-up that takes the item you point at, gives your mercenary
tactical commands, and makes Unleashed melee fairer.

## Gameplay and combat

- **Glyph casting:** Put **Draw glyph** on a hand button: open a hand's wheel,
  select a button and pick **Draw glyph**, last under **General / Items**. Then
  give shapes their skills on the wheel's new **GLYPHS** page (the box at the
  upper right): circle, triangle, square, lightning bolt, spiral, V, star and
  wave. Hold the button, draw the shape in the air in front of you and let go:
  the skill on that shape is cast with its own mana, cast rate and effect. It is
  meant for buffs, auras and other skills that need no target. The glyph hangs
  where you drew it in its skill's element: flames and embers, ice shards that
  crack, crackling lightning, dripping poison, rising holy light, a crimson curse,
  shadow, spinning steel and more. Then it leaves in its own way.
- **Paladin gestures - contributed by ChuckRibbits:** Make the sign of the cross
  (forehead, chest, one shoulder, the other) with either hand to cast **Holy
  Shield**. For **Fist of the Heavens**, make a fist with your arm straight and
  raised, then bring it down to stop about level with your shoulder. The bolt
  falls on the monster your arm points at. Both need the skill on one of your
  buttons. Werewolf and Werebear now carry the wheel's GESTURE tag, and skills
  that lock onto a monster (Charge, Leap Attack and others) now find it reliably.
- **Mercenary tactical commands - contributed by Mcklem:** Point at your
  mercenary, or the ground at its feet, hold **Grip** and pull **Trigger**, aim
  and let go:
  - at the ground, it walks there and holds;
  - at a monster, it focuses it;
  - on the mercenary itself, it holds its place, or regroups if it was holding.

  Grip first or both at once both work, the trigger casts nothing while you do,
  and a monster nearer your aim keeps the trigger an attack. The mercenary also
  fights from the spot you send it to, and when badly hurt falls back beside you,
  ready for a thrown potion.
  **VR Settings > Survival > Tactical commands** turns the commands off.
- **A fairer Unleashed attack cycle - contributed by ChuckRibbits:**
  - Swings between beats never delay your next full hit; dual wielders share one
    cycle.
  - Dual wield swings at native D2R speed, and strike skills from either blade
    swing the primary weapon.
  - Life Tap, monster flinches, Thorns, Iron Maiden, weapon wear and poison now
    follow the stroke: in-between strokes no longer heal you through Life Tap or
    stun-lock monsters.
- **Loot pick, on by default - contributed by beadlebytes:** With several items
  on the floor, hold your dominant hand out and point at one: **A** picks it up
  instead of the item nearest your feet. Its name shows with a thin rim in its
  rarity color within reach, or red when too far, where A takes nothing and
  doesn't walk there. Pointing at no item, doors, chests and NPCs work as before.
  The switch is on **VR Settings > Interface**, beside Loot name size.

## Controls and interface

- **Loot beams - contributed by ChuckRibbits:** Colored pillars of light rise over
  unique (gold), set (green) and rare (yellow) items and runes Vex and up
  (orange) lying near you; walls and hills hide them. **VR Settings > Interface >
  Loot beams** sets their brightness, and its **Filter** chooses which kinds.
- **The held menu - contributed by ChuckRibbits:** While you hold the tabbed
  menu, the right stick turns you, and pulling the trigger off the menu drops
  the item you picked up. Life, mana and XP show above the menu. Reach back over
  your left shoulder and grip to put it away.
- **Reset Skills & Stats - contributed by ChuckRibbits:** A button on the tabbed
  menu's Character page, in offline games, resets every stat and skill point
  with the game's own reset. It is free and unlimited, asks for confirmation
  and needs one free inventory square.
- **Talking to NPCs - contributed by ChuckRibbits:** Pressing A on an NPC while
  walking stops you first, so the conversation opens with you standing.
- **Arcane Decay - contributed by Mcklem:** A spectral blue vignette grows at the
  edge of your view as your mana runs low. **VR Settings > Interface > Mana
  Warning** turns it on or off, sets the mana level it starts below (50%) and
  its strength, and fades it in and out.

## Visuals and stability

- **Save and Exit crash - contributed by ChuckRibbits:** Save and Exit could
  crash the game from the held menu. The menu no longer shows the minimap beside
  it, which caused the crash.
- **Your shadow has a head - contributed by beadlebytes:** In first person your
  shadow now keeps its head and helm; the eye views hide the head instead.
- **Dungeon torchlight:** **Torches**, beside Dungeon ceilings in **VR Settings >
  Performance**, lets the game's own torches and braziers light the dungeon
  around them. It is off by default because finding them costs CPU.
- **Pushable bodies cost less:** Only bodies within 3 meters are pushed. With
  frame generation on, turning Pushable bodies off no longer gains 10-15 frames
  a second.

## Quality of Life (unreleased)

- **VR Settings > Quality of Life:** Optional incoming direct-damage and hostile
  monster movement multipliers, plus shared player summon
  aggression and leash controls. Skeletal mage missile speed follows summon aggression.
  Skeleton and golem aggro uses the range multiplier squared (4x becomes 16x);
  their leash uses the normal multiplier. Mercenaries retain native behavior.
  Summon sliders support up to 5x. All default to 1x (native behavior). Damage and movement apply
  live; summon range changes require restarting the game after Apply. Attack
  reach stays native. Intended for local offline characters. Poison and
  burning are outside the direct-damage control's established coverage.

## Updating

Download **D2R-PCVR-Setup-0.3.31.exe** for graphical installation, or
**d2r-pcvr-windows.zip** for portable setup. Existing users should choose
**Update / repair and keep calibration and video settings**.

New settings start at their defaults on an update too: Loot pick, Loot beams,
Arcane Decay and Tactical commands are on, and Dungeon torchlight is off.
Mercenary commands now take Grip + Trigger; the open-palm hold is gone.

Updating from v0.3.28 or earlier: v0.3.29's new defaults come with a full
install: Terrain distance 6 rings, Big objects 600, Fog distance 600 and a
gentler fog. **Update / repair keeps your saved values**, so set them in VR
Settings > Performance and World to try them.

Close the game and fully exit Steam before updating. Closing D2R VR Launcher
also avoids file locks. Update / repair preserves settings, calibration and saves.
A full install applies recommended defaults and resets calibration.

On native Windows, Setup downloads OFXR Bridge for frame generation, so keep the
internet connection on. If that download fails, everything else still installs
and frame generation reports that it is not installed. Frame generation also
needs the Microsoft Visual C++ x64 runtime.

The Windows installer includes **D2R VR Launcher 0.3.5**, installing or updating
it when needed while preserving newer versions. Direct Steam shortcuts and
portable setup remain available; Authentic is a separate optional mod.

### First-time setup and using the launcher

1. Install and start the original Diablo II: Resurrected through Steam or
   Battle.net once. Complete its account prompts, create or select an offline
   character, then fully exit the game.
2. Download **D2R-PCVR-Setup-0.3.31.exe** from the assets below. Fully exit
   Steam using **Steam > Exit**, then run Setup and choose **Full install with
   recommended settings**. Follow the wizard to select your game copy, headset
   and connection; the shared launcher is included.
3. After Setup finishes, reopen Steam and reconnect your headset.
4. Open **Start > D2R VR Launcher**, confirm the **Game folder** and select
   **D2R VR Unleashed**.
5. Choose **SteamVR** for Steam Frame or Quest through Steam Link/SteamVR.
   Choose **Virtual Desktop (VDXR)** for Quest through Virtual Desktop, with its
   Quest app and Windows Streamer running and SteamVR closed. The runtime choice
   applies to this launch without changing your Windows-wide OpenXR setting.
6. Click **Play**, select a **local offline character** and complete body
   calibration when prompted.

### Current log location

For support, reproduce the problem, close the game and send these logs from
your game folder. In Steam's default library that is
`C:\Program Files (x86)\Steam\steamapps\common\Diablo II Resurrected`:

```text
mods\D2R-PCVR\d2rloader\logs\d2r-vr-frame.log
mods\D2R-PCVR\d2rloader\logs\d2r-pcvr-vrcam.log
mods\D2R-PCVR\reshade\ReShade.log
d2rloader\logs\d2rloader.log
```

The `d2r-vr-frame.log` and `d2r-vr-vrcam.log` in the game's main
`d2rloader\logs` folder are left over from older versions and are no longer
updated. Developer mode uses `mods\pcvr-dev` in place of `mods\D2R-PCVR`.
Installer failures are recorded separately in
`%LOCALAPPDATA%\Programs\D2R PCVR\setup-install.log`.

## Verification and known issues

The release build passed **174 native checks**. **263 of 269 Python checks**
passed on Windows: the 5 session checks need Linux's `/proc`, and 1
symbolic-link check was skipped because this Windows session cannot create
symbolic links. The in-headset What's new pages and the Interface page of VR
Settings were rendered and checked for fit.

Every change was tested in a headset with offline characters before it was
merged, by the owner or its contributor:
- **Glyph casting** on a Paladin: auras cast through the button they sat on and
  through the hidden right-trigger slot, and the elemental trails.
- **Mercenary commands:** moves, holds and a monster focus started by Grip +
  Trigger, with no skill cast at the mercenary.
- **Loot pick:** the pointed items were picked up, and items past reach showed
  a red rim.
- **Paladin gestures:** Holy Shield and Fist of the Heavens.
- **Unleashed and other contributions:** the attack cycle, dual wield, loot beams,
  Reset Skills & Stats, the held menu, NPC talk, the shadow's head and Arcane
  Decay.
- **Dungeon torchlight and the pushable-bodies radius:** the radius cut the
  bodies' cost from 17-61 ms to under 5 ms each second.

No new headset session is claimed for the version and release-notes update
itself.

Known issues:
- **Glyph casting:** the shapes are shared by all your characters, so a shape
  whose skill your hero lacks casts nothing. The Warlock's glyphs take their
  look from the skill's tree. A shape's skill uses the game's right-trigger
  slot when it is on none of your buttons, replacing what a plain controller
  layout had there.
- **Virtual Desktop freezes:** the game freezes for about a tenth of a second
  every so often under Virtual Desktop, with or without frame generation. This is
  being investigated.
- **Stuns in Unleashed:** a stun now only knocks you back. You can move again as
  soon as the knockback ends, even while the stun is still shown.
- **Frame generation is experimental.** It uses a pre-release bridge. Expect
  artifacts around moving monsters and during fast head turns. Frame counters
  count only the frames the game draws, and a menu stops the generated frames
  while it is open.
- **Throwing potions:** the burst follows the game's short throw animation, and
  only right-hand potions are thrown by hand for now.
- **By turns:** it still renders each eye on alternate frames. Small bits of
  texture can still flicker along some edges, and transparent effects and wispy
  hair without useful depth may keep artifacts.
- **Sorceress elemental hands:** they are drawn over the picture, so they can show
  through your weapon or a wall.
- **Not retested:** the latest rendering changes have not been retested on other
  GPU vendors. Battle.net remains experimental.

## Special thanks

Thanks to **ChuckRibbits** for the Paladin's Holy Shield and Fist of the
Heavens, loot beams, the fixed Unleashed attack cycle and native dual wield,
Reset Skills & Stats, the held menu, NPC talk and the Save and Exit fix. Thanks
to **beadlebytes** for Loot pick and the shadow's head. Thanks to **Mcklem** for
the mercenary's tactical commands and AI, and for Arcane Decay. Thanks to
**tig3rmast3r** for OFXR Bridge, and to everyone testing, reporting bugs and
helping improve the mod!

D2R VR Unleashed is a mod by **monkeylashes**, with contributions from the
community. From v0.3.29 its source is under the Mozilla Public License 2.0;
releases up to 0.3.28 stay available under the MIT license.

## Downloads and links

- [Windows installer, portable ZIP and checksums](https://github.com/bearstonem/d2r-pcvr/releases/tag/v0.3.31-windows-preview)
- [Installation instructions](https://github.com/bearstonem/d2r-pcvr/blob/v0.3.31-windows-preview/INSTALL-WINDOWS.md)
- [Controls](https://github.com/bearstonem/d2r-pcvr/blob/v0.3.31-windows-preview/CONTROLS.md)
- [Full source comparison](https://github.com/bearstonem/d2r-pcvr/compare/v0.3.30-windows-preview...v0.3.31-windows-preview)
- [Project on GitHub](https://github.com/bearstonem/d2r-pcvr)
- [Discord](https://discord.gg/DmbFfJAnnm)
- [Support](https://ko-fi.com/bearstonem)

<!-- PCVR_IN_HEADSET_NOTES
release: v0.3.31-windows-preview
credits: ChuckRibbits; beadlebytes; Mcklem; tig3rmast3r (OFXR Bridge); Sergious.
- Glyph casting: hold Draw glyph, draw a shape in the air and let go to cast its skill.
- Wheel > GLYPHS: give eight shapes a buff or aura; put Draw glyph on a hand button.
- Glyphs glow in their element: flames, ice, lightning, holy light, curses, shadow, steel.
- Paladin: the sign of the cross casts Holy Shield.
- Paladin: raise a straight arm with a fist and bring it down for Fist of the Heavens.
- Mercenary: point at it, hold Grip, pull Trigger, aim and let go to move it or focus a foe.
- Loot pick (on): hold your arm out and point at an item; A picks that one, not the nearest.
- Loot beams: colored pillars over unique, set and rare items and high runes.
- Interface > Loot beams sets their brightness; Filter chooses which kinds.
- Unleashed: extra swings never delay your next full hit; dual wield shares one cycle.
- Dual wield swings at native speed; in-between strokes no longer stun-lock or heal.
- Character page: Reset Skills & Stats, free and unlimited offline, with a confirmation.
- Held menu: turn with the stick, drop items off the menu, put it away over your shoulder.
- Arcane Decay: a blue vignette at the edge of your view when mana runs low.
- A on an NPC while walking stops you, so the conversation opens with you standing.
- Your shadow keeps its head in first person.
- Performance > Torches: the game's torches light dungeons (off by default, costs CPU).
- Pushable bodies only push bodies within 3 m: no fps loss with frame generation.
- Fixed: Save and Exit no longer crashes from the held menu.
PCVR_IN_HEADSET_NOTES -->
