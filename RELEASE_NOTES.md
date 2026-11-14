# D2R VR Unleashed v0.3.35 - Windows preview

This hotfix makes your summoned army fight in formation, keeps your view on your
hero through Leap, and guards against a game crash in the Frozen Tundra.
[v0.3.34](https://github.com/bearstonem/d2r-pcvr/releases/tag/v0.3.34-windows-preview)
came out only a day earlier, so its notes are repeated in full below: solid
weapons, props with weight, cinematics in VR and Commander of the Dead.

## Fixed in this hotfix

- **Your army fights in formation:** in a Commander of the Dead formation, the
  summons were walked back to their places several times a second, which
  cancelled their attacks, so they hardly fought. They now fight monsters within
  their formation's reach - close in for Phalanx Wall and Protective Ring, far
  out for Spearhead Assault - and form up again when the fight is over. A new
  order still sends them to their places first.
- **Leap keeps your view on your hero:** with **Room follows you** on, a Leap or
  Leap Attack could leave your view behind while the hero flew on: a broken
  third-person view, sometimes outside the walls, until the game was reloaded.
  The room now holds still while the game carries the hero, follows only moves
  that bring him back to you, and puts itself back under you if you and the hero
  are ever 1.5 m apart for 1.5 seconds.
- **A crash in the Frozen Tundra:** the game's own effect code could crash when
  a visual effect ended. The mod now skips the one call that crashed, after
  checking that the game's code there is exactly what it expects; otherwise it
  leaves the game as it is. `[game_patches] effect_listener_guard` (on).

## From v0.3.34

### Gameplay and combat

- **Weapons collide:** your weapon stops at the shield on your other arm and
  slides along its face, and two weapons stop at each other. Your real hand
  cannot be stopped, so the hero's hand gives way, up to 60 cm, then they pass
  through. A strike clanks in the controller, and a fast one plays the block
  sound. **Combat > Melee > Weapons collide** (on).
- **Weapons hit walls:** a weapon or shield in either hand also stops at walls,
  closed doors and the floor, and slides along them. Barrels and urns still
  break. **Physics > Weapons hit walls** (on).
- **Props have weight:** knockable props weigh what they look like - a cup
  under 0.2 kg, a chair about 10 kg, a barrel or crate about 50 kg - and a hit
  shares its momentum by weight. A tap sends a cup flying and leaves a barrel
  standing; a full swing rocks it and a maul knocks it over. Heavy props thud in
  the hand. **Physics > Props have weight** (on).
- **Commander of the Dead - contributed by Mcklem:** the Necromancer's skeletons,
  mages, golem and revives take orders - Phalanx Wall, Protective Ring, Marching
  Escort, Spearhead Assault, Hold Ground, Regroup and Fall Out - set on glyphs or
  the wheel like skills. Formations follow your body's facing, and the army walks
  to its places.
- **Your Defense eases guard strain:** blocking strains your guard by the
  attacker's chance to hit you. With high Defense, a normal monster takes up to
  35 blocks to break it instead of 10.
- **Barbarian warcries cast at once:** bring your fist (or both) up to your mouth
  and straight out; there is no pause at the mouth any more.
- **Both grips held for a motion never pick up loot** beside you.
- **Mercenary potion toss:** a bottle landing within 4 m of your mercenary
  feeds it (was 1.5 m), so a walking mercenary still catches it.
- **Left-handed:** the weapon stays in the left hand under a shield, and
  **Hands > Sticks stay right-handed** keeps the left stick walking and the right
  stick turning.

### Controls and interface

- **Cinematics on a screen in the world:** the intro, the act videos and the
  main menu's Cinematics play on a screen 6 m wide, 5 m ahead, that stays put
  while you look round. In a game the headset used to show no picture during
  them. `[cinema]` in `d2r_pcvr.ini` sets its size and distance.
- **Campfire character creation, tuned - contributed by Kudlatytrue:** every
  install now builds the tuned campfire scene (Harrogath round the fire, its
  road and rocks), with a mountain panorama round it and snow now and then, and
  it is on by default. The fire and the Hardcore box are easier to point at.
- **Labels > Monster names:** choose **All**, **Bosses and elites** or **None**.

### Visuals and stability

- **Corpses stop looking at you:** dead monsters and townsfolk no longer turn
  their heads to follow you.
- **Valve Index controllers vibrate:** most vibrations were sent without a
  frequency, which Index controllers do not play. They now play at 170 Hz on an
  Index; other controllers are unchanged.

## Updating

Download **D2R-PCVR-Setup-0.3.35.exe** for graphical installation, or
**d2r-pcvr-windows.zip** for portable setup. Existing users should choose
**Update / repair and keep calibration and video settings**.

An update keeps your saved settings, so changed defaults reach new full
installs only. To get v0.3.34's campfire scene and its mountain panorama after
an update, set `enabled=1` and `panorama=1` under `[campfire]` in
`d2r_pcvr.ini`. Weapons collide, Weapons hit walls, Props have weight and the
crash guard are on after either kind of install.

## Verification and known issues

Automated: the native Windows build and its 238 tests pass, with the release
notes, identity and installer staging checks.

Headset (one PC, SteamVR): this hotfix's army and Leap fixes. A Necromancer's
army of up to 12 broke off to fight in formation and formed up again. About 15
Leaps and 8 Leap Attacks kept the view within 11 cm of the hero. From v0.3.34:
Weapons collide with a mace and a shield on a Paladin, cinematics from the main
menu, Commander of the Dead switching formations, and the tuned campfire scene.

Not yet tried in a headset: the crash guard in the Frozen Tundra (the crash has
not been reproduced here; the guard only acts when the game would have
crashed), and from v0.3.34 Weapons hit walls, Props have weight, cinematics in
a game, Defense and guard strain, the instant warcries, the Index fix on an
Index, and the left-handed changes. Battle.net remains experimental.

- **Weapons hit walls** uses the game's own wall map in cells of about 46 cm,
  so a weapon can stop a little short of a drawn wall or a little inside it. The
  floor is taken as flat, so stairs and slopes within reach are not followed.
- **A shield whose shape could not be measured** collides as a 34 x 44 cm board.
- **Frame generation is experimental.** It uses a pre-release bridge. Expect
  artifacts around moving monsters and during fast head turns.
- **Left-handed** is experimental.
- **By turns:** small bits of texture can flicker along some edges.
- **Virtual Desktop freezes:** the game freezes for about a tenth of a second
  every so often under Virtual Desktop. This is being investigated.

## Special thanks

Thanks to **Mcklem** for Commander of the Dead, to **Kudlatytrue** for the tuned
campfire scene, its panorama and its snow, to the players who reported the Leap
view and the Frozen Tundra crash with their logs, and to everyone testing,
reporting bugs and helping improve the mod!

D2R VR Unleashed is a mod by **monkeylashes**, with contributions from the
community. From v0.3.29 its source is under the Mozilla Public License 2.0;
releases up to 0.3.28 stay available under the MIT license.

## Downloads and links

- [Windows installer, portable ZIP and checksums](https://github.com/bearstonem/d2r-pcvr/releases/tag/v0.3.35-windows-preview)
- [Installation instructions](https://github.com/bearstonem/d2r-pcvr/blob/v0.3.35-windows-preview/INSTALL-WINDOWS.md)
- [Controls](https://github.com/bearstonem/d2r-pcvr/blob/v0.3.35-windows-preview/CONTROLS.md)
- [Full source comparison](https://github.com/bearstonem/d2r-pcvr/compare/v0.3.33-windows-preview...v0.3.35-windows-preview)
- [Project on GitHub](https://github.com/bearstonem/d2r-pcvr)
- [Discord](https://discord.gg/DmbFfJAnnm)
- [Support](https://ko-fi.com/bearstonem)

<!-- PCVR_IN_HEADSET_NOTES
release: v0.3.35-windows-preview
credits: Mcklem, Kudlatytrue.
- HOTFIX: your army fights in formation, then forms up again when the fight is over.
- HOTFIX: Leap keeps your view on your hero (Room follows you).
- HOTFIX: a game crash in the Frozen Tundra is guarded against.
- WEAPONS COLLIDE: your weapon stops at your shield; two weapons stop at each other.
- The hero's hand gives way up to 60 cm, then they pass through. Combat > Melee.
- Weapons and shields stop at walls, closed doors and the floor. Physics > Weapons hit walls.
- PROPS HAVE WEIGHT: a tap sends a cup flying; a barrel takes a full swing to rock.
- A maul knocks a barrel over; heavy props thud in the hand. Physics > Props have weight.
- COMMANDER OF THE DEAD: give your skeletons, mages and golem orders - by Mcklem.
- Phalanx Wall, Protective Ring, Escort, Spearhead, Hold Ground, Regroup, Fall Out - by Mcklem.
- Set the army's orders on glyphs or the wheel; the army walks to its places.
- Your Defense eases guard strain: high Defense takes far more blocks to break.
- Barbarian warcries cast at once: up to the mouth and straight out, no pause.
- Holding both grips for a motion never picks up loot.
- Mercenary potion toss: a bottle within 4 m of your mercenary feeds it.
- Left-handed: the weapon stays in the left hand under a shield.
- Hands > Sticks stay right-handed: left stick walks, right stick turns.
- CINEMATICS play on a screen 6 m wide, 5 m ahead, that stays put as you look round.
- Campfire character creation: the tuned scene for every install - by Kudlatytrue.
- A mountain panorama round the campfire, and snow now and then - by Kudlatytrue.
- Labels > Monster names: All, Bosses and elites, or None.
- Corpses no longer turn their heads to follow you.
- Valve Index controllers vibrate (pulses at 170 Hz on an Index).
PCVR_IN_HEADSET_NOTES -->
