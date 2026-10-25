# Offline developer mode

Developer mode provides class preparation, gear grants, travel shortcuts and
a separate offline test environment. Use **new, disposable offline expansion
characters**. The `pcvr-dev` mod has its own character list and save directory;
normal characters are not copied or modified. Do not host/join TCP/IP or use
online play for testing.

This requires a build containing the developer-mode change. Published v0.3.17
does not contain these tools; it is the release summarized by the reference page.

## Launch

Install Unleashed normally, close the game, then add **`--frame-dev`** to the
existing VR wrapper command, alongside `--frame-vr`. On Steam, the supplied
`Launch-Dev-SteamVR.cmd` launches that combination through the configured Steam
wrapper. Ordinary VR shortcuts continue to select the normal mod.

From a source checkout:

```powershell
.\deps\python\python.exe steamvr/launch_windows.py --frame-vr --frame-dev "C:\Games\Diablo II Resurrected\D2R.exe"
```

Use the package's Python interpreter instead when running an installed package.
Retain `--frame-vdxr` or `--frame-openxr-runtime=PATH` if your normal shortcut
uses it. Steam still uses the Explorer handoff; developer mode does not change
the system-wide OpenXR runtime or bypass ownership requirements.

The first launch stages a private mod from the working `mods/D2R-PCVR` install:
plugin, OpenXR loader, settings/calibration, locally generated caches and
ReShade. It downloads nothing. Exact metadata is:

```json
{"name":"pcvr-dev","savepath":"pcvr-dev/"}
```

The metadata file ends with a newline. D2RLoader selects `-mod pcvr-dev` and
mounts `mods/pcvr-dev/pcvr-dev.mpq`. Conflicting metadata or linked paths are
refused. A global VR plugin must be migrated with the normal installer first
so two VR plugins cannot load together. Existing developer settings and DLLs
are preserved on later launches, including manually installed test candidates.

Experimental Battle.net retains its explicit edition gate:

```powershell
.\deps\python\python.exe steamvr/launch_windows.py --frame-vr --frame-dev --experimental-battlenet "--battlenet-game=C:\Games\Diablo II Resurrected"
```

The flag is consumed by the wrapper, not forwarded to the game. It rejects a
conflicting `-mod` or save-path override. This is not a multiplayer detector;
choose local offline Play. Create a new character in the separate character list.

## Build and update a test DLL

Normal native builds include the dormant backend. It only activates when the
loader's active mod and exact metadata both identify the isolated environment.
The hand-wheel DEV tab is hidden in ordinary play. VR Settings > Notes (Release Notes)
is available in both regular and developer builds.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File steamvr/build_windows.ps1 -Developer -Test
.\deps\python\python.exe steamvr/prepare_dev_scenarios.py --game "C:\Games\Diablo II Resurrected" --plugin build-frame/windows-developer/d2rl-pcvr.dll
```

`-Developer` selects `build-frame/windows-developer/` and enables developer
diagnostics; neither build command installs or launches anything. Normal builds
stage in `build-frame/windows-native/`. `-NoDevTools` (CMake
`D2RVR_DEV_TOOLS=OFF`) omits the backend, and that DLL cannot enable the DEV UI.

The staging helper requires the game closed. An explicit `--plugin` updates only
the developer DLL and keeps a hash-named backup beside it; existing settings,
calibration, gear, saves and the normal mod remain intact. Initial staging
failures can be retried. Missing dependencies must be installed through normal
setup first. Return to normal play with the ordinary VR shortcut.

## Hand-wheel DEV tools

Open either hand's wheel and choose **DEV**. Its tabs are grouped by purpose:

- **Character:** level 10, 30, 60 and 90 presets; heal life/mana; unlock quests and
  waypoints; a **Throwing potions** kit (one stack each of Rancid, Choking and
  Strangling Gas and Oil, Exploding and Fulminating Potion, to equip in the right
  hand and throw; on this page as Equipment is full).
- **Equipment:** starter primary/alternate gear, class unique primary/alternate
  gear, crossbow + bolts, bow + arrows, a small shield, **Render debug draws**,
  **By turns own picture**, **By turns filter**, **By turns ghosts**, a **Throwing kit** and a **Potion kit** (four Super
  Healing, two Full Rejuvenation, two Antidote and two Thawing potions, normal
  items in the inventory: put them in the belt to toss at the mercenary).
- **Act I - II:** towns, combat areas and inspection destinations.
- **Act III:** Kurast regions, sewer entrances, jungle areas and bosses.
- **Act IV - V:** later-act combat areas and filming locations.

Preparation applies exact values to the current class. Skill ranks are capped
at 20 and at `character level - required level + 1`; locked skills are explicitly
reset to zero, including when returning from a higher preset. Barbarian and
Warlock use their own required-level ordering. Level 30 unlocks every skill tier
but gives newly unlocked level-30 skills rank 1. Level 60/90 gives all skills
rank 20. Preparation also heals the character.

Strength/dexterity/energy are 50, 100, 200 or 300 for those four presets;
vitality is that value plus character level plus 10. These are **test presets**,
with more total stats and skill points than normal progression permits. They
test skill availability and combinations, not balanced leveling builds. Existing
items and button bindings stay in place; higher-level gear may become unusable
after selecting a lower preset. Existing summoned units or buffs can retain
their old strength until recast or a new game.

Starter kits provide normal base weapons appropriate to the class plus basic
armor, without random unique requirements. Use them for low-level tests.
Unique kits are intended for level 90. Gear is added to inventory; **equip it
manually**. No existing item is deleted or automatically unequipped. Make room
and equip/stash one kit before requesting another. Failed transactions leave
existing gear untouched. The SDK requires an exchange input: only a newly
created temporary potion is consumed or removed, never a preexisting item.

| Class | Unique primary | Unique alternate |
| --- | --- | --- |
| Amazon | Windforce + arrows | Titan's Revenge + Stormshield |
| Sorceress | Oculus + Stormshield | Mang Song's Lesson |
| Necromancer | Boneshade + Darkforce Spawn | Wizardspike + head |
| Paladin | Phase Blade + Herald of Zakarum | Caduceus + shield |
| Barbarian | Phase Blade + Berserker Axe | Throwing javelins + axe |
| Druid | Thunder Maul | Wizardspike + Stormshield |
| Assassin | Bartuc's + Jade Talon | Wizardspike + Stormshield |
| Warlock | Grandfather + grimoire | Wizardspike + grimoire |

Native unique selection and random rolls apply. The separate crossbow kit is
Leadcrow plus 250 bolts; the bow kit is Windforce plus 250 arrows. The small
shield is a Pelta Lunata buckler. It is separate to avoid exceeding inventory
capacity in a full Paladin kit.

The throwing kit, for any class, is one normal stack of each thrown weapon
missile: a Javelin, a Pilum, a Glaive, Throwing Knives and Throwing Axes
(14 inventory cells). Equip one in the right hand and throw it by hand with the
right trigger on Attack, Throw or nothing; the kit leaves bindings alone. A
Barbarian can equip the knives in one hand and the axes in the other to test
Double Throw.

## Shield debug draws

Select **DEV > Equipment > Render debug draws** to toggle the shield guard
overlay. It starts Off each launch and does not change combat rules or saved
settings. Equip a shield (melee is Unleashed), close the menu, and
compare the outline with the visible shield while moving and rotating it.

- **Cyan:** measured shield bounds used by the blocking check.
- **Amber:** fallback attachment position and facing. Its rectangle is a
  reference plane (0.9 m wide, 0.6 m tall), not measured mesh coverage.
- **Grey:** the guard is lowered/inactive.
- A hit flashes **green** for a native block, **red** for a rejected guard,
  or **pink** if native combat skipped its block roll (for example an
  unblockable attack or an earlier dodge). The flash lasts 0.6 seconds.

The center cross and arrow show the guard position and facing. The temporary
attacker line shows the native horizontal approach at shield height; the game
does not provide an exact point where a weapon/projectile touched the shield.
Lines draw over scenery so clipping cannot hide the inspected geometry.
The overlay hides in menus, without a current shield/tracking pose, and in
Classic mode. Test the large and small shields with the same enemies, including
stationary guards, wrist rotation, walking and turning. Record video and keep
the corresponding frame log; visual inspection still requires headset testing.

While enabled, `Frame shield debug` logs discovery progress every two seconds.
`cumulative_stages` is a diagnostic bitmask, accumulated during enabled periods:
`0x1` hero pose, `0x2` context, `0x4` storage scan, `0x8` candidate,
`0x10` skeleton, `0x20` mesh metadata, `0x40` shield name, `0x80` bounds,
`0x100` local ownership, `0x200` source published, `0x400` render callback,
`0x800` world matrices, `0x1000` valid surface, `0x2000` surface published.
These are milestones across candidates, not proof a single candidate passed
every stage. `mesh=1` confirms the current check actually uses measured bounds.

## By turns own picture

Select **DEV > Equipment > By turns own picture** to step, for the session,
where the eye not drawn takes its picture with Stereo rendering: By turns.
**FX** (the default, `[stereo] turns_keep_own=3`): where the game's effects
layer has something - spells, flames, Tyrael's wings - it keeps its own
picture from a frame back where that shows the same surface as the eye just
drawn, so they stand where that eye sees them; elsewhere it takes the drawn
eye's picture. **NEAR+FX** (4): round near things as well - the hands, the
weapon, a monster close by - for anything else drawn without depth there
(the game's bloom). **OFF** (0): the drawn eye's
picture wherever it reaches, as v0.3.23 does. **NEAR** (1): round near things
only. **ALL** (2): its own picture wherever it shows the same surface, so the
whole eye is a frame behind the other. The button steps in that order, shows
the current state, and the frame log records each step. It changes nothing
else and no saved setting; editing the ini's value applies that value again.
Compare Tyrael's wings, a waypoint's flame and spells against the scene
behind them, the sword against the sky and monsters' heads close up, with
the same view in each state.

## By turns filter

Select **DEV > Equipment > By turns filter** to switch, for the session, how
the eye not drawn reads the pictures it is made from with Stereo rendering:
By turns. **SHARP** (the default, `[stereo] turns_sharp=1`): Catmull-Rom,
kept within the nearest texels. **SOFT** (0): bilinear, as before. The
button shows the current state, and the frame log records each switch. It
changes no saved setting. Compare text on loot labels and in the distance,
ground textures and fine edges, looking with one eye then the other.

## By turns ghosts

Select **DEV > Equipment > By turns ghosts** to switch, for the session, the
strips beside near things that only the eye not drawn sees. **FIXED** (the
default, `[stereo] turns_fill=1`): where its frame-old picture there shows
something nearer since gone, what is behind beside the strip instead. **OLD**
(0): its own frame-old picture, as before. Watch a monster walking across
in front of a wall a few metres away, and the edges of things while you
strafe: with OLD a frame-old copy of the monster can trail beside it in one
eye. The button shows the current state, and the frame log records each
switch. It changes no saved setting.

## By turns motion

Select **DEV > Character > By turns motion** to step, for the session, how
the eye not drawn treats things that move by themselves with Stereo
rendering: By turns; shelved, off by default (#281). **ON** (`[stereo] turns_motion=1`): its own
picture from a frame back also goes on by the game's motion vectors, so a
monster walking stands where it is now in the strip beside it that only this
eye sees and where the effects keep this eye's own picture. **SHOW** (2): the
eye not drawn from its own picture alone (no drawn eye's picture over it),
what moved on tinted red. Red should sit on walking monsters and missiles,
not on the ground or walls, even while you turn your head or walk. With
SHOW, a monster walking across should look single in both eyes; with OFF it
is a frame behind in one eye. **OFF** (0): moved with the head and the hero
alone, the default. The button steps in that order, shows the current state,
and the frame log records each step. It changes no saved setting.

Nothing moves on until the frame log says the camera's motion explains the
game's vectors ("the camera's motion explains ...% of the game's motion
vectors from 2 m out"). Every 10 seconds a line gives, by half a metre of
distance, how many vectors were still, how many the camera's motion
explains, and their mean length.

## Locations

| Page | Destinations |
| --- | --- |
| Act I - II | Rogue Encampment, Blood Moor, Tristram, Cathedral, Andariel's chamber, Lut Gholein, Arcane Sanctuary, Tyrael, Duriel |
| Act III | Kurast Bazaar, Upper Kurast, Kurast Sewers, Travincal, Mephisto's chamber, Spider Forest, Flayer Jungle |
| Act IV - V | River of Flame, Chaos Sanctuary, Harrogath, Bloody Foothills, Crystalline Passage, Arreat Summit, Throne of Destruction, Worldstone Chamber |

Travel uses native area entrances; bosses depend on the test character's save.
It does not deliberately finish quests, change difficulty, reset bosses or
respawn enemies. Arcane Sanctuary does not teleport directly to the Summoner.
Tyrael targets his platform in level 73 at `(22578,15594)`; Duriel targets that
level's native arena entrance. Prepare and equip before dangerous destinations.

For the Kurast sewer entrance report, choose **Act III > Kurast Sewers** and
use its stairs to return outside. Inspect the stairwell and try selecting it
with Interact while standing normally, then compare crouching and turning.
Check entrances in both Kurast Bazaar and Upper Kurast. Console dispatch alone
does not confirm arrival at a specific entrance or completion of a quest.
**Equipment > Render debug draws** also enables a read-only, once-per-second
`Frame entrance targeting` log with native target type/ID and eye height/pitch.
It does not expand targeting, choose an exit or teleport on an interaction.

## Release reference in VR

**VR Settings > Release Notes** is available to all players and shows one line
per feature from the last published
release, currently [v0.3.17](https://github.com/bearstonem/d2r-pcvr/releases/tag/v0.3.17-windows-preview).
The reference is built in and works offline. It does not list unreleased PRs
or automatically fetch GitHub. The build generates this page from the versioned
`PCVR_IN_HEADSET_NOTES` summary in `RELEASE_NOTES.md`. Follow the
[release workflow](../docs/RELEASING.md) when publishing; the build and packager
check the version and bundled summary. The preview check measures text to prevent
wrapping/clipping.

## Validation and limits

Backend actions require a fresh class/session snapshot, live idle/walking/running
player (including town modes), and no native menu. Tracking/focus/session changes
cancel pending requests. Native console and item calls never hold the developer
status mutex: travel may pump rendering/UI while dispatch runs. Old callbacks
cannot consume newer requests or overwrite their results.

The [pinned SDK](https://github.com/D2RLoader/PluginSDK/tree/717f727a0ec52912d1558764345f8fa3453a2bd6)
defines console success as **dispatch acceptance**. The UI therefore reports
"Commands sent"; verify actual level, skill ranks and arrival in game. Gear
success means the complete SDK inventory transaction was accepted.

Automated checks cover native backend routing/cancellation, all class skill
assignments and lower presets, kits, menu hit targets and stale-page rejection,
release text layout, launch flags, isolation, staging retries and DLL backups.
New lower-level presets, launch-flag flow and rearranged menus still need
headset confirmation; synthetic fixtures do not verify native command effects.
Use offline characters only. No game assets or saves are distributed.
