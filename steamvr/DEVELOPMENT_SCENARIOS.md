# Offline developer mode

Developer mode provides class preparation, gear grants, travel shortcuts and
an in-headset release reference. Use **new, disposable offline expansion
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
The ordinary game/settings UI has no DEV tabs.

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

- **Character:** level 10, 30, 60 and 90 presets; heal life/mana.
- **Equipment:** starter primary/alternate gear, class unique primary/alternate
  gear, crossbow + bolts, bow + arrows, and a small shield.
- **Act I - III:** towns, combat areas and inspection destinations.
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

## Locations

| Page | Destinations |
| --- | --- |
| Act I - III | Rogue Encampment, Blood Moor, Tristram, Cathedral, Andariel's chamber, Lut Gholein, Arcane Sanctuary, Tyrael, Duriel, Kurast Bazaar, Travincal, Mephisto's chamber, Spider Forest, Flayer Jungle |
| Act IV - V | River of Flame, Chaos Sanctuary, Harrogath, Bloody Foothills, Crystalline Passage, Arreat Summit, Throne of Destruction, Worldstone Chamber |

Travel uses native area entrances; bosses depend on the test character's save.
It does not deliberately finish quests, change difficulty, reset bosses or
respawn enemies. Arcane Sanctuary does not teleport directly to the Summoner.
Tyrael targets his platform in level 73 at `(22578,15594)`; Duriel targets that
level's native arena entrance. Prepare and equip before dangerous destinations.

## Release reference in VR

**VR Settings > DEV** shows one line per feature from the last published
release, currently [v0.3.17](https://github.com/bearstonem/d2r-pcvr/releases/tag/v0.3.17-windows-preview).
The reference is built in and works offline. It does not list unreleased PRs
or automatically fetch GitHub. Update `steamvr/dev_release_notes.h` when
publishing a release; keep the title/tag/link and one-line summaries in sync.
The preview check measures every line to prevent wrapping/clipping.

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
