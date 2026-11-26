# D2R VR Unleashed v0.3.36 - Windows preview

This update puts the main menu's act scenes around you in 3D and brings
multiplayer through D2RLoader, a Whirlwind you steer with the walk stick and
flails that swing. It also has a long list of community contributions.

## Gameplay and combat

- **Whirlwind on (Barbarian):** do the T (both arms straight out to the sides a
  moment, both grips held) or press a button that holds Whirlwind, and Whirlwind
  stays on.
  - It lasts 5 seconds at level 1 and a tenth of a second more each level,
    +skills counted, up to 8 seconds.
  - The walk stick moves the hero, whirling. He turns as the stick turns, even
    halfway through a whirl, and stops when you let go.
  - Before you push the stick, the first whirl goes where you look.
  - It ends when its time runs out, when you do the T or press the button
    again, or when you cast something else. Running low on mana, entering
    town, opening a menu or dying also end it.
  - Holding the button still chains whirls for as long as you hold it.
- **Flails swing:** with a Flail, Knout, Scourge, Khalim's Flail or Khalim's Will,
  the chain and ball hang from the handle and swing with your hand, the same in
  both eyes. A flail's hit now reaches its head, 0.9 m from the grip.
  `[hands] flail_chain` (on).
- **Hands on the controllers:** after body calibration, the middle of the hero's
  palm sits on your controller's grip. An arm too short to reach your hand is
  drawn longer, up to twice its length, so the hero's hands stay on yours at
  full reach. `[arms] stretch` (on); `[arms] hand_along` moves the hand along
  itself (-5 to 5 cm).
- **Multiplayer through D2RLoader (experimental) - contributed by
  geekstreetstudios:** host or join TCP/IP games from D2RLoader's multiplayer
  panel, with VR and desktop players together.
  - A joining VR player's swings hit with their real controllers, and the host
    works out the damage.
  - Other VR players' heads, arms, hands and weapons move as theirs do.
  - The desktop host doesn't need a headset.
  - Your party's and your mercenary's health show beside the wrist map, over
    their heads, or both: **Labels > Names and bars > Ally health bars**
    (Both by default).
  - Loot beams show for joining players too, and the multiplayer panel's boxes
    take the VR keyboard.
- **Game chat - contributed by geekstreetstudios:** in a multiplayer game,
  either hand wheel's **CHAT** tab shows the game's own chat, so VR and desktop
  players see the same conversation. Type with the VR keyboard, or pull the
  panel out to keep a read-only copy in view.
- **Enemies wake farther away - contributed by RobinHeggelund:** offline,
  enemies and objects become active up to 5 room rings round you, so they stop
  popping in close by. **Graphics > Performance > Draw distance > Enemy
  activation distance** sets 1-8 rings, or 0 for the game's own. More rings
  cost more performance.
- **Suggested potions - contributed by ptkk73:** when no belt column holds a
  healing, mana or rejuvenation potion but your inventory does, it shows in the
  belt at half opacity. Drink it, or feed it to your mercenary, as usual.
  Potions are also easier to grab, by the neck too.
- **A potion in each hand - contributed by ptkk73:** both hands can take a
  potion from the belt at once, for you or your mercenary.
- **Voice casting - contributed by ptkk73:** **Gameplay > Voice** binds up to
  six sounds of your own to skills or army commands. Each character has its
  own.
  - Record a short sound (one example is enough, up to three), choose its
    skill, select **On** and **Save changes**, then close settings to cast.
  - The page tests your sounds without casting, with a microphone meter and
    the last match. Pick the microphone and the sensitivity there too.
  - It matches the sound itself, so any word in any language works.
  - The game's own mana, skill and cooldown checks apply. Voice casts work in
    offline games, and not in menus.
  - Your recordings stay on your PC. Voice is off until you turn it on.
- **Warlock: Miasma Bolt leaves your hand,** as Fire Bolt does, and aims up and
  down with your hand.
- **Weapons collide, more quietly:** your weapon against your own shield or
  your other weapon now clanks out loud only on a hard knock (3 m/s or more),
  at most every half second. The controllers still feel every touch.

## Controls and interface

- **The main menu in 3D - contributed by Kudlatytrue:** character select's act
  scenes stand round you, as the campfire does:
  - Act 1: the forest and the tower's ruins, with wisps.
  - Act 2: the road and dunes under a burning sky.
  - Act 3: Kurast Docks.
  - Act 4: the River of Flame.
  - Act 5: the frozen highlands.

  Each act has its own sky, and switching acts no longer stalls the game. The
  installer builds the scenes from your own game. `[frontend] scene` (on).
- **Your heroes in order:** the main menu lists the five heroes you played last
  first, then the rest by name. The mod changes only the save files' dates,
  never their contents. `[frontend] sort_characters` (on).
- **Floor map - contributed by geekstreetstudios:** set **Menus > Controller
  menus > Detached map** to **Floor**.
  - Pull the wrist map off and let go at any height: it lies see-through on the
    ground ahead of you and comes along as you walk.
  - Turn your head and hold it there, and the map comes round to your new
    heading.
  - **Floor map width** and **Ground map distance** set its size and place.
  - **HUD** stays the default.
- **Floating map - contributed by beadlebytes:**
  - **Interface > Wrist > Map background** lets the world show through the
    pulled-off map (100%, solid, by default).
  - Its border now shows only while you point at it or hold it.
  - A grab hint shows that Right Grip pulls the map off your wrist:
    **Map grab hint** (on).
- **Area banner - contributed by ptkk73:** entering a new area shows its name
  before your eyes, in the game's own font and language. In a terror zone it
  also shows the terror level.
- **Facing on arrival - contributed by ptkk73:** you face into a cave, dungeon
  or new area with its entrance behind you. A town portal home leaves you facing
  Cain, Fara or the traders, or in Harrogath the town itself.
- **Town portal in front of you - contributed by ptkk73:** it opens ahead of
  you, or at the nearest clear spot.
- **Monster abilities - contributed by ptkk73:** a monster's label lists its
  abilities and immunities, in the game's own words.
- **The laser stays - contributed by ptkk73:** holding A or using an item in the
  inventory no longer hides the laser.
- **Hand wheel:** the EQUIPMENT page is gone, and Stow / draw weapons with it.
  Swap weapon sets over your shoulder and drink at the belt. The CHAT tab shows
  only in multiplayer games.
- **Controller menu size - contributed by geekstreetstudios:** **Menus >
  Controller menus > Controller menu size** (50-200%).
- **Kick has an icon - contributed by beadlebytes,** in place of an empty plate.

## Visuals and stability

- **Graphics > Effects - contributed by DevilInLaw:** hide armors and buffs that
  block your view. They keep working while hidden.
  - **Ice Armor:** Off, Original, or Icicles (new, the default).
  - **Cyclone Armor:** Off, Original, or a white mist on your forearms.
  - **Psychic Ward:** Off, Original, or a purple mist on your forearms.
  - **Bone Armor:** Off or Bones.
  - **Auras** and **shout buffs:** on or off, yours and your companions'
    separately.
  - **Assassin charges:** compact orbs on the forearm.
- **Campfire 1.0 - contributed by Kudlatytrue:** a new mountain panorama made
  from three photographs, and the game's wind gusts along the wall left of the
  gate.
- **Ceilings in Baal's and Mephisto's rooms:** the game-drawn ceiling now clears
  the tallest pieces in these places:
  - the Worldstone Keep's pillars and stairs;
  - the Throne of Destruction;
  - the Worldstone;
  - Mephisto's compound in the Durance of Hate.

  It used to sit at a raised hand's height there.
- **Monster health bars - contributed by ptkk73:** the red of a monster's bar no
  longer disappears at some viewing angles, which made the monster look out of
  health.
- **Less work each frame:** props nobody touches are no longer physics bodies,
  and finding monsters to target takes less of the game's time.

## Updating

Download **D2R-PCVR-Setup-0.3.36.exe** for graphical installation, or
**d2r-pcvr-windows.zip** for portable setup. Existing users should choose
**Update / repair and keep calibration and video settings**.

An update keeps your saved settings, so changed defaults reach new full installs
only. New installs start with **Terrain draw distance** 4 (was 6) and **Fog
distance** 400 (was 600).

These new settings are on after either kind of install:
- the 3D main menu (the installer builds its scenes from your game) and the
  hero order;
- Enemy activation distance 5;
- the map grab hint;
- the flail chain;
- arms reaching your controllers;
- Ice Armor's icicles.

The campfire's new panorama also sits at the right height after an update.

## Verification and known issues

Automated: the native Windows build and its 304 tests pass, with the release
notes, identity and installer staging checks.

Tried in the headset (one PC, SteamVR, offline):
- **Whirlwind on:** eight runs, steered by the walk stick through every turn.
- **Flails:** the chain swinging in the game. The fix for the chain looking
  different in each eye is confirmed in the log; how it looks has not been
  reported yet.
- **Hands on the controllers:** the arms reaching the controllers.
- **Enemy activation distance:** enemies no longer popping in close.
- **The 3D main menu:** switching act scenes, before the fix for its stall.
- **From the logs:** untouched props with no physics bodies, no CHAT tab
  offline, and the area banner and town portal running on Steam 3.4.95041.

Contributors tried their own work in their headsets on v0.3.35-based builds:
- multiplayer, with a desktop host and a VR client;
- chat, with a VR host and a VR client;
- the floor map, the floating map's background and grab hint, and Kick's icon;
- suggested potions, monster abilities, the area banner, facing on arrival, the
  town portal, the health bars and the laser fix;
- Graphics > Effects;
- Campfire 1.0.

Not yet tried in a headset:
- voice casting, which its contributor showed working in their game;
- Miasma Bolt from the hand;
- the new ceiling heights;
- a hard knock's clank;
- the fix for the act-switching stall;
- multiplayer and chat on this release's combined build;
- VR-with-VR multiplayer since the latest changes.

Known issues:
- **Multiplayer is experimental.** Let the first connection finish before you
  swing, and restart both games between sessions.
- **Battle.net remains experimental.**
- **A Paladin's shield whose shape could not be measured:** a weapon can sink
  40-60 cm into it before it is pushed back.
- **Frame generation is experimental.** It uses a pre-release bridge. Expect
  artifacts around moving monsters and during fast head turns.
- **Left-handed** is experimental.
- **By turns:** small bits of texture can flicker along some edges.
- **Virtual Desktop freezes:** the game freezes for about a tenth of a second
  every so often under Virtual Desktop. This is being investigated.

## Special thanks

Thanks to these contributors:
- **Kudlatytrue** for the 3D main menu and Campfire 1.0;
- **geekstreetstudios** for multiplayer, game chat and the floor map;
- **ptkk73** for voice casting, suggested potions, monster abilities, the area
  banner, facing on arrival, the town portal in front, a potion in each hand,
  the health bar and laser fixes;
- **beadlebytes** for the floating map's background and grab hint, and Kick's
  icon;
- **DevilInLaw** for Graphics > Effects, the icicles, the forearm mists and the
  charge orbs;
- **RobinHeggelund** for the enemy activation distance.

Thanks also to the players who reported Whirlwind's direction, the stiff flails,
the Warlock's aim and the low ceilings in Baal's and Mephisto's rooms, and to
everyone testing, reporting bugs and helping improve the mod!

D2R VR Unleashed is a mod by **monkeylashes**, with contributions from the
community. From v0.3.29 its source is under the Mozilla Public License 2.0;
releases up to 0.3.28 stay available under the MIT license.

## Downloads and links

- [Windows installer, portable ZIP and checksums](https://github.com/bearstonem/d2r-pcvr/releases/tag/v0.3.36-windows-preview)
- [Installation instructions](https://github.com/bearstonem/d2r-pcvr/blob/v0.3.36-windows-preview/INSTALL-WINDOWS.md)
- [Controls](https://github.com/bearstonem/d2r-pcvr/blob/v0.3.36-windows-preview/CONTROLS.md)
- [Full source comparison](https://github.com/bearstonem/d2r-pcvr/compare/v0.3.35-windows-preview...v0.3.36-windows-preview)
- [Project on GitHub](https://github.com/bearstonem/d2r-pcvr)
- [Discord](https://discord.gg/DmbFfJAnnm)
- [Support](https://ko-fi.com/bearstonem)

<!-- PCVR_IN_HEADSET_NOTES
release: v0.3.36-windows-preview
credits: Kudlatytrue, geekstreetstudios, ptkk73, beadlebytes, DevilInLaw, RobinHeggelund.
- WHIRLWIND ON: do the T or press its button; Whirlwind stays on for 5 to 8 seconds.
- While it is on, the walk stick steers each whirl, even halfway; let go to stop.
- FLAILS SWING: the chain and ball swing with your hand; the hit reaches the head.
- Hands on the controllers: the palm on the grip; arms reach your hands at full reach.
- MULTIPLAYER (experimental): TCP/IP games through D2RLoader - by geekstreetstudios.
- A joining VR player's swings hit with real controllers; VR players' arms show to others.
- Party and mercenary health: Labels > Names and bars > Ally health bars.
- GAME CHAT on the wheel's CHAT tab in multiplayer games - by geekstreetstudios.
- Enemies wake up to 5 room rings out, so no close pop-in - by RobinHeggelund.
- Set it: Graphics > Performance > Draw distance > Enemy activation distance.
- Suggested potions: a missing type shows in the belt from your inventory - by ptkk73.
- Potions are easier to grab, and each hand can take one at once - by ptkk73.
- VOICE CASTING: Gameplay > Voice binds up to six sounds to skills - by ptkk73.
- Record a sound, choose its skill, select On and Save; each character has its own.
- Warlock: Miasma Bolt leaves your hand and aims up and down.
- Weapons collide: the clank sounds only on a hard knock; the hands still feel each touch.
- MAIN MENU IN 3D: each act's scene and sky stand round you - by Kudlatytrue.
- The main menu lists your 5 last played heroes first, then the rest by name.
- FLOOR MAP: Menus > Controller menus > Detached map > Floor - by geekstreetstudios.
- Floating map: a see-through background and a grab hint - by beadlebytes.
- Area banner: a new area's name before your eyes, with the terror level - by ptkk73.
- You face into a new area, and a town portal home faces the town - by ptkk73.
- The town portal opens in front of you - by ptkk73.
- Monster labels list abilities and immunities - by ptkk73.
- The laser stays while you hold A or use an item in the inventory - by ptkk73.
- Hand wheel: EQUIPMENT page removed; CHAT shows only in multiplayer games.
- Controller menu size: Menus > Controller menus - by geekstreetstudios.
- Kick has its own icon - by beadlebytes.
- GRAPHICS > EFFECTS: hide armors, auras and shouts; they keep working - by DevilInLaw.
- Ice Armor icicles, forearm mists for Cyclone Armor and Psychic Ward - by DevilInLaw.
- Assassin charges as compact orbs on the forearm - by DevilInLaw.
- Campfire 1.0: a new mountain panorama and wind gusts - by Kudlatytrue.
- Ceilings clear the tallest pieces in Baal's and Mephisto's rooms.
- Monster health bars keep their red at every viewing angle - by ptkk73.
PCVR_IN_HEADSET_NOTES -->
