# D2R VR Unleashed v0.3.32 - Windows preview

This update lets every class cast its skills with body motions, has the game draw
dungeon ceilings itself, lifts the view with Leap, and brings dual-wield Double
Swing and Frenzy and combat walk.

## Gameplay and combat

- **Motion casting for every class:** Hold both grips and do a skill's motion to
  cast it, with no button needed. Each class with motions has a **MOTIONS** page:
  the MOTIONS box at the lower right of the wheel's Spells and Equipment pages
  opens it. Its cards show each motion and an ON/OFF switch, and a skill you have
  no points in is dimmed. The motions are quick strokes and short holds with the
  hands above the waist, measured against your calibrated body, and both
  controllers hum warmer as a motion nears its cast. They need **Weapon skill
  gestures** on, as it is by default.
  - **Sorceress (14):** Nova, Frost Nova, Static Field, Energy Shield, Thunder
    Storm, Blaze, Frozen Armor, Shiver Armor, Chilling Armor, Fire Wall, Meteor,
    Blizzard, Hydra and Teleport. For Teleport, point the right arm where you want
    to land, a circle shows there, then yank the fist back to your shoulder.
    Meteor and Blizzard land on the highlighted monster when there is one.
  - **Paladin and Druid:** their gestures (Charge, Blessed Hammer, Holy Shield,
    Fist of the Heavens, Werewolf and Werebear) now take both grips too, like
    every other class's motions, and have a MOTIONS page each.
  - **Amazon (4):** Inner Sight, Slow Missiles, Decoy and Valkyrie.
  - **Barbarian (10):** Battle Orders, Shout, Battle Command, War Cry, Howl,
    Battle Cry, Find Item, Leap, Leap Attack and Whirlwind. Leap is a fast swing
    of both arms up and lands 25 cells ahead, the way your arms swung. Leap
    Attack is the same swing started with both arms pulled well back, and goes for
    the monster that way. Whirlwind is a T held a moment, and whirls ahead.
  - **Necromancer (12):** Bone Armor, Raise Skeleton, Raise Skeletal Mage,
    Corpse Explosion, Amplify Damage, Lower Resist, Poison Nova, Bone Prison, Bone
    Wall, Poison Explosion and two golem cards: the right arm raises your
    strongest golem and the left arm the next one, and each card names it.
  - **Assassin (7):** Burst of Speed, Fade, Cloak of Shadows, Venom, Blade Shield,
    Shadow Warrior (Shadow Master once learned) and Mind Blast.

  While a motion skill is on, holding both grips never grabs a belt potion, a
  holster, the shoulder swap, the over-the-shoulder menu or a staff: those take
  one grip held alone.
- **Glyphs, any way you draw them:** A glyph is now recognized whichever way you
  draw it: mirrored, tilted or on any plane in front of you. Town Portal can go
  on a glyph, using a scroll or tome. Each hand also has its own set of eight
  shapes on the GLYPHS page. A left-hand shape with no skill of its own casts
  the right hand's.
- **Double Swing and Frenzy for dual wielders - contributed by ChuckRibbits:** On
  a trigger, a full swing that lands casts the skill with that blade, and your
  other blade's next hit is its second swing. A dual wielder's attack beat now runs
  at the two weapons' native average speed. The wheel's LEFT HAND STRIKES box
  shows for dual wielders too.
- **Combat walk - contributed by ChuckRibbits:** While you fight, the hero walks.
  That means being struck, landing a strike, or casting or shooting from the hand.
  Your Movement speed returns about a second after the last of these.
  `[gameplay] combat_walk_ms=0` in `d2r_pcvr.ini` turns it off.
- **Mercenary commands work again:** Grip + Trigger on your mercenary could stop
  working for a whole session: the mercenary was never found and the trigger cast
  your skill instead.

## Controls and interface

- **Cast On Strike - contributed by ChuckRibbits:** In the skill picker and on
  both hand wheels, skills a landed swing or shield bash casts are tagged **CAST
  ON STRIKE**. These are the melee strike skills, Fury and Zeal, Double Swing and
  Frenzy, and Smite with a shield. **Gesture** is kept for body gestures, bow draws
  and throws. The trigger slot's header reads **Primary**.

## Visuals and stability

- **Dungeon ceilings the game draws:** In VR first person a dungeon's ceiling is
  now a model the game draws itself, in the area's own rock, stone or beams. It is
  lit by the game's torches, fires and spells and fogged by its fog. Setup makes it
  from your own game files. The shader's ceiling stays as the fallback.
- **The view through Leap:** In first person the view now rises with the hero
  through Leap's whole arc, up to 3.8 m, and both eyes stay matched. Running into
  a Leap, it glides on through the take-off crouch instead of stopping dead.
  Leap Attack does the same.
- **Spell glow on the Sorceress's hands:** It is now always on and has left the
  menu. The flames, lightning and frost are made from the game's own spell effects,
  and each glowing hand lights the dungeon around it.
- **The first cast of a skill:** A skill a glyph or motion puts on the right
  trigger now waits a moment for the game to take it. Before, Raise Skeleton and
  others could fail on the first try after another skill.
- **Looking up:** The head kept for your shadow is now cut farther ahead when you
  look up, against the warped circle seen above you in places like the Den of
  Evil. `[arms] head_cut_level`, `head_cut_up` and `head_cut_down` set the cuts.

## Updating

Download **D2R-PCVR-Setup-0.3.32.exe** for graphical installation, or
**d2r-pcvr-windows.zip** for portable setup. Existing users should choose
**Update / repair and keep calibration and video settings**.

New settings start at their defaults on an update too:
- Every class's motions are on; turn any off on its MOTIONS page.
- Combat walk is on.
- The game-drawn ceilings are on. Setup makes them while installing, which takes
  about two and a half minutes the first time and again after a game update.
- The Sorceress's hand glow is always on, and its VR Settings switch is gone.

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
2. Download **D2R-PCVR-Setup-0.3.32.exe** from the assets below. Fully exit
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

The release build passed **182 native checks**. **280 of 285 Python checks**
passed on Windows: the 4 session checks need Linux's `/proc`, and 1
symbolic-link check was skipped because this Windows session cannot create
symbolic links. The in-headset What's new page and all 2175 VR Settings help
cards were rendered and checked for fit.

Tested in a headset with offline characters before merging, by the owner:
- **Motions:**
  - All 14 of the Sorceress's.
  - The Barbarian's, including about 40 Leap Attacks and 10 Whirlwinds.
  - The Amazon's 4 and the Assassin's 7.
  - The Necromancer's, after its redesign.
  - The first-cast wait after another skill.
- **Glyphs:** each hand's own set.
- **Dual wield and combat walk:** Double Swing, Frenzy and the shared beat on a
  dual-wielding Barbarian, and the combat walk toggling in fights.
- **Dungeon ceilings:** the cave ceiling placed, shown and put away, and leaving
  the game cleanly.
- **The view through Leap:** the arc, the stereo and the take-off glide.
- **Mercenary commands:** the mercenary found again at once, then commands and a
  monster focus.

Not yet tried in a headset:
- **Town Portal on a glyph.**
- **The ceilings:** the other 14 areas' look, torchlight on the rock, and the
  Battle.net read path.
- **Motions:**
  - The latest Whirlwind T, read whichever way you look.
  - Leap Attack at a monster handed over by the aim rules.
  - The golem cards' names.
  - Cloak of Shadows and Shadow Warrior since they were loosened.
  - The body scale for players of another size.
- **The hand glow:** its look and the hand light have not been reviewed.

No new headset session is claimed for the version and release-notes update
itself.

Known issues:
- **Frenzy and Double Swing on two triggers:** an open Frenzy cast takes your
  other blade's next hit as its second swing, so alternating blades leaves Double
  Swing almost no swing to start from. Put one of them on a trigger.
- **Motions:** both grips must already be held when a motion starts. Squeezing
  them partway through a swing misses its start.
- **Ceilings:** a pit or a two-level cave gets one ceiling at one height. The
  barracks' beams and the domed vaults are not drawn by the game yet.
- **Glyph casting:** the shapes are shared by all your characters, so a shape
  whose skill your hero lacks casts nothing. A shape's skill uses the game's
  right-trigger slot when it is on none of your buttons, replacing what a plain
  controller layout had there. Motions do the same.
- **Sorceress hand glow:** it is drawn over the picture, so it can show through
  your weapon or a wall.
- **Virtual Desktop freezes:** the game freezes for about a tenth of a second
  every so often under Virtual Desktop, with or without frame generation. This is
  being investigated.
- **Stuns in Unleashed:** a stun now only knocks you back. You can move again as
  soon as the knockback ends, even while the stun is still shown.
- **Frame generation is experimental.** It uses a pre-release bridge. Expect
  artifacts around moving monsters and during fast head turns. Frame counters
  count only the frames the game draws, and a menu stops the generated frames
  while it is open.
- **By turns:** it still renders each eye on alternate frames. Small bits of
  texture can still flicker along some edges, and transparent effects and wispy
  hair without useful depth may keep artifacts.
- **Not retested:** the latest rendering changes have not been retested on other
  GPU vendors. Battle.net remains experimental.

## Special thanks

Thanks to **ChuckRibbits** for Double Swing and Frenzy for dual wielders, combat
walk and the Cast On Strike labels. Thanks to **Sergious** for the game-drawn
dungeon ceilings, adapted with permission. Thanks to **tig3rmast3r** for OFXR
Bridge, and to everyone testing, reporting bugs and helping improve the mod!

D2R VR Unleashed is a mod by **monkeylashes**, with contributions from the
community. From v0.3.29 its source is under the Mozilla Public License 2.0;
releases up to 0.3.28 stay available under the MIT license.

## Downloads and links

- [Windows installer, portable ZIP and checksums](https://github.com/bearstonem/d2r-pcvr/releases/tag/v0.3.32-windows-preview)
- [Installation instructions](https://github.com/bearstonem/d2r-pcvr/blob/v0.3.32-windows-preview/INSTALL-WINDOWS.md)
- [Controls](https://github.com/bearstonem/d2r-pcvr/blob/v0.3.32-windows-preview/CONTROLS.md)
- [Full source comparison](https://github.com/bearstonem/d2r-pcvr/compare/v0.3.31-windows-preview...v0.3.32-windows-preview)
- [Project on GitHub](https://github.com/bearstonem/d2r-pcvr)
- [Discord](https://discord.gg/DmbFfJAnnm)
- [Support](https://ko-fi.com/bearstonem)

<!-- PCVR_IN_HEADSET_NOTES
release: v0.3.32-windows-preview
credits: ChuckRibbits; tig3rmast3r (OFXR Bridge); Sergious (dungeon ceilings).
- Motions: hold both grips and do a skill's motion to cast it, with no button needed.
- Each class's MOTIONS page (Spells page, lower right) turns its motions on or off.
- The controllers hum warmer as a motion nears its cast.
- Sorceress: 14 motions. Teleport: point where to land, then yank the fist back.
- Sorceress: Meteor and Blizzard land on the highlighted monster.
- Paladin and Druid gestures now take both grips too.
- Amazon: Inner Sight, Slow Missiles, Decoy and Valkyrie by motion.
- Barbarian: the warcries, Find Item, Leap, Leap Attack and Whirlwind by motion.
- Leap: swing both arms up fast; it lands 25 cells ahead, the way your arms swing.
- Leap Attack: the same swing from your arms pulled far back. Whirlwind: hold a T.
- Necromancer: 12 motions; each golem card names the golem that arm raises.
- Assassin: seven motions, from Burst of Speed to Mind Blast.
- Glyphs: recognized whichever way you draw them; Town Portal can go on a glyph.
- Glyphs: each hand has its own set of eight shapes.
- Dual wield: Double Swing and Frenzy cast from your swings, one of each blade.
- Dual wield: the attack beat runs at your two weapons' native average speed.
- Combat walk: you walk while fighting; your run returns a moment after.
- Strike skills on a trigger are tagged CAST ON STRIKE, not GESTURE.
- Dungeon ceilings are drawn by the game itself, lit by its torches, in its fog.
- First person: the view rises with Leap's arc and glides through its take-off.
- Sorceress: the hand glow is always on, made from the game's own spell effects.
- Fixed: mercenary commands could stop working for a whole session.
- Fixed: a skill's first cast by glyph or motion could fail after another skill.
PCVR_IN_HEADSET_NOTES -->
