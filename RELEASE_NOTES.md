# D2R VR Unleashed v0.3.33 - Windows preview

This update makes the game draw each eye about a third faster, lets frame
generation run without holding the game back, adds room-scale play and a
left-handed mode, and brings a large batch of community features.

## Performance

- **The game's own drawing, about a third faster:** six changes to the game's
  render code, made in memory while it runs, take wasted work out of every eye
  pass: a slow read on every transparent draw, a profiler lock taken by every
  draw, a debug name written for every draw, and a debug pass over every visible
  object. Sun shadows and the visibility check are now done once a frame, not
  once per eye. In one heavy fight on the test PC an eye pass went from 9.3 ms
  to about 6.3 ms, and the frame rate from 36-38 to the 45 the headset asked
  for. Each change checks the game's code first and stays out if it differs.
  There is no setting.
- **Frame generation no longer holds the game back:** with frame generation on,
  the bridge made the game wait for the graphics card at the start of every
  frame. The mod now asks for the frame's timing just before it draws, when
  that wait is already over. On the test PC at 120 Hz this went from 55 real
  frames a second to 60. Both eyes on SteamVR only.
- **Both eyes may now be the better choice:** with the faster drawing, Both
  eyes held 60 real frames per eye at 120 Hz on the test PC, where By turns gave
  each eye 36 to 45. Try **VR Settings > Graphics > Performance > Eyes drawn per
  frame**. By turns remains the default.
- **Less work in the mod itself:** ragdoll physics runs on its own thread, wrist
  and panel pictures are uploaded only where they changed, the skeleton hook
  reads far less, idle effect passes are skipped, and new full installs switch
  off a ReShade add-on the VR picture never used.

## Gameplay and combat

- **Room-scale:** turns pivot at your head, your body stays under you anywhere
  in your room, and with **Body > Room follows you** (on) the hero's spot follows
  you as you walk about.
- **Left-handed (experimental):** **Hands > Left-handed** mirrors every weapon
  kind, the shield, the class motions, the interface and the controls.
- **Aim with your eyes:** on headsets with eye tracking, **Targeting > Aim with
  your eyes** picks the monster you look at. Target picks also follow monsters
  sooner.
- **Hit reactions:** a committed stroke staggers a monster and a heavy one
  knocks it back. **Combat > Melee reach** lets near misses beside a monster
  still hit.
- **More motions:** sixteen Warlock skills, and the Barbarian's Find Potion,
  Grim Ward and Taunt. Whirlwind now whirls once for each T you hold.
- **Assassin:** cross your claws to block. Throwing potions in the off hand are
  thrown by hand too, and aimed trap and potion throws show a sigil where they
  will land.
- **Sorceress:** Lightning and Chain Lightning leave the casting hand. Teleport
  is aimed with the controller along an arc that lands on its sigil.
- **Bows and crossbows:** a face button loads Multiple Shot, Guided Arrow and
  Strafe into the draw, Multiple Shot's fan follows your up and down aim, Guided
  Arrow always homes on the aimed monster, skill arrows fly at arrow speed, and
  the right trigger is Attack while a crossbow is equipped. **Bow bash** and
  **Weapon heft** are new and off by default.
- **Spells and arrows break things:** spells break containers and push props
  and bottles. Arrows and bolts break barrels, urns and other breakables -
  contributed by **Mcklem**.
- **Charge** goes to the aimed monster whatever the aim options.
- **Monster immunity indicators - contributed by RobinHeggelund:** immune
  monsters show what they are immune to.
- **Summon range - contributed by RobinHeggelund:** **Monsters > Summon range**
  widens how far summons attack and follow.
- **Mercenary potion throw autoaim and Click to drink - contributed by
  RobinHeggelund:** both optional, under **Monsters > Mercenary** and **Body >
  Potion belt**.

## Controls and interface

- **VR Settings regrouped - contributed by beadlebytes:** the menu is grouped by
  what players look for, and it now has room for 256 settings.
- **Floating map - contributed by beadlebytes:** pull the full map off your
  wrist and put it back there.
- **T cross to calibrate - contributed by beadlebytes:** hold your arms out in a
  T to calibrate height and arm span.
- **Position-only recenter - contributed by DevilInLaw:** F13 recenters your
  position without turning you.
- **VR keyboard for character names - contributed by Kudlatytrue.**
- **Campfire character creation in 3D - contributed by Kudlatytrue:** the
  character creation scene is drawn in stereo around you.
- **Shared stash tab buttons - contributed by RobinHeggelund.**
- **Walk direction:** **Hands > Walk direction** walks where you look or where
  the left controller points.
- **Wrist skill cooldowns:** recharging skills show between the orbs.
- **Leather menu:** tooltips that run past the menu's foot are shown, and the
  hero stops as soon as the stick is let go.

## Visuals and stability

- **Heads turn to you:** monsters and townsfolk look at your real head.
- **Richer haptics and sound:** hit feel by target, a ramping bowstring, a felt
  heartbeat at low life, and the game's own echo raised for the headset.
- **Dungeon rooms at different heights join up - contributed by ChuckRibbits.**
- **Your own helm** is left out of your view and kept in your shadow, and the
  black flicker from the head's shadow is gone.
- **Fog** is measured from your eye, **Light Radius** widens the dungeon light,
  and the sky is one band once round the circle.
- **SteamVR menu flicker:** layers keep their place when the menu is hidden.
- **Setup:** it no longer refuses game builds that are not on a list, and
  downloads retry through Windows when a certificate is rejected.

## Updating

Download **D2R-PCVR-Setup-0.3.33.exe** for graphical installation, or
**d2r-pcvr-windows.zip** for portable setup. Existing users should choose
**Update / repair and keep calibration and video settings**.

An update keeps your saved settings, so changed defaults reach new full
installs only. The performance changes need no setting and apply after either
kind of install.

## Verification and known issues

Automated: the native Windows build and its 230 tests pass, with the release
notes, identity and installer staging checks.

Headset: the performance changes and the frame generation change were measured
and played on one PC (NVIDIA GPU, SteamVR, Both eyes, at 90 and 120 Hz) in
Act 2 field fights, with a profiler capture before and after each change. The
picture was as before, transparent effects and shadows included.

Not tested: the performance changes in dungeons and other acts, Virtual
Desktop, other GPU vendors, the frame generation change at 90 Hz and with
FidelityFX, and the off hand potion throw. Battle.net remains experimental.

- **Frame generation is experimental.** It uses a pre-release bridge. Expect
  artifacts around moving monsters and during fast head turns. With it on, head
  and hand positions are taken a few milliseconds earlier than before. A menu
  stops the generated frames while it is open, and there can be a short stutter
  as it opens.
- **A game update can switch the performance changes off.** Each one compares
  the game's code with what it knows and stays out when it differs; the log
  says so in a `Frame game patches:` line.
- **Left-handed** is experimental.
- **By turns:** it still renders each eye on alternate frames. Small bits of
  texture can flicker along some edges.
- **Virtual Desktop freezes:** the game freezes for about a tenth of a second
  every so often under Virtual Desktop. This is being investigated.

## Special thanks

Thanks to **RobinHeggelund** for the immunity indicators, summon range,
mercenary potion autoaim, click to drink and the stash tab buttons; to
**beadlebytes** for the regrouped VR Settings, the floating map, T cross
calibration and the per-feature ReShade files; to **Kudlatytrue** for the VR
keyboard and the 3D campfire; to **ChuckRibbits** for the dungeon room heights;
to **Mcklem** for arrows that break things; to **DevilInLaw** for position-only
recenter; to **tig3rmast3r** for OFXR Bridge; and to everyone testing,
reporting bugs and helping improve the mod!

D2R VR Unleashed is a mod by **monkeylashes**, with contributions from the
community. From v0.3.29 its source is under the Mozilla Public License 2.0;
releases up to 0.3.28 stay available under the MIT license.

## Downloads and links

- [Windows installer, portable ZIP and checksums](https://github.com/bearstonem/d2r-pcvr/releases/tag/v0.3.33-windows-preview)
- [Installation instructions](https://github.com/bearstonem/d2r-pcvr/blob/v0.3.33-windows-preview/INSTALL-WINDOWS.md)
- [Controls](https://github.com/bearstonem/d2r-pcvr/blob/v0.3.33-windows-preview/CONTROLS.md)
- [Full source comparison](https://github.com/bearstonem/d2r-pcvr/compare/v0.3.32-windows-preview...v0.3.33-windows-preview)
- [Project on GitHub](https://github.com/bearstonem/d2r-pcvr)
- [Discord](https://discord.gg/DmbFfJAnnm)
- [Support](https://ko-fi.com/bearstonem)

<!-- PCVR_IN_HEADSET_NOTES
release: v0.3.33-windows-preview
credits: RobinHeggelund, beadlebytes, Kudlatytrue, ChuckRibbits, Mcklem, DevilInLaw, tig3rmast3r.
- PERFORMANCE: the game draws each eye about a third faster. No setting; it is always on.
- Four wasted steps are taken out of every draw: a slow read, a lock, a debug name, a debug pass.
- Sun shadows and the visibility check are done once a frame, not once per eye.
- Frame generation no longer holds the game back: 60 real frames at 120 Hz on the test PC.
- Both eyes may now beat By turns: Graphics > Performance > Eyes drawn per frame.
- Ragdoll physics runs on its own thread; wrist and panel pictures upload only what changed.
- ROOM-SCALE: turns pivot at your head and your body stays under you anywhere in your room.
- Body > Room follows you (on): the hero's spot follows you as you walk your room.
- Hands > Walk direction: walk where you look or where the left controller points.
- Hands > Left-handed (experimental): weapons, shield, motions, interface and controls mirrored.
- Targeting > Aim with your eyes: eye-tracked targeting on headsets that support it.
- Position-only recenter on F13 - by DevilInLaw.
- COMBAT: a committed stroke staggers a monster; a heavy one knocks it back.
- Combat > Melee reach: near misses beside a monster still hit.
- Warlock: sixteen skills cast by motion.
- Barbarian: Find Potion, Grim Ward and Taunt by motion; Whirlwind whirls once per T.
- Assassin: cross your claws to block; off-hand throwing potions are thrown by hand.
- Aimed trap and potion throws show a sigil where they will land.
- Sorceress: Lightning and Chain Lightning leave the casting hand.
- Teleport: aim with the controller along an arc that lands on the sigil.
- Bow: a face button loads Multiple Shot, Guided Arrow and Strafe into the draw.
- Multiple Shot's fan follows your up and down aim; Guided Arrow always homes.
- Crossbow: the right trigger is Attack. Bow bash and Weapon heft are new, off by default.
- Spells break containers and push props and bottles.
- Arrows and bolts break barrels and urns - by Mcklem.
- Monster immunity indicators - by RobinHeggelund.
- Monsters > Summon range: summons attack and follow farther - by RobinHeggelund.
- Mercenary potion throw autoaim (optional) - by RobinHeggelund.
- Body > Click to drink potions (optional) - by RobinHeggelund.
- Shared stash tab buttons - by RobinHeggelund.
- INTERFACE: VR Settings regrouped by what you look for - by beadlebytes.
- Floating map: pull the full map off your wrist and put it back - by beadlebytes.
- Hold your arms in a T to calibrate height and arm span - by beadlebytes.
- VR keyboard for character names - by Kudlatytrue.
- Campfire character creation in 3D - by Kudlatytrue.
- Wrist: recharging skills show between the orbs.
- WORLD: monsters and townsfolk turn their heads to your real head.
- Haptics: hit feel by target, a ramping bowstring, a felt heartbeat at low life.
- Sound: the game's own echo raised for the headset.
- Dungeon rooms at different heights join up - by ChuckRibbits.
- Your own helm is left out of your view and kept in your shadow.
- Fog is measured from your eye; Light Radius widens the dungeon light.
- Fixed: SteamVR menu flicker when the menu is hidden.
- Setup no longer refuses game builds that are not on a list.
PCVR_IN_HEADSET_NOTES -->
