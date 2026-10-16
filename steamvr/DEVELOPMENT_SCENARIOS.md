# Offline development scenarios

Build with `steamvr/build_windows.ps1 -Developer -Test`. The developer DLL is
staged at `build-frame/windows-developer/d2rl-vrcam.dll`; normal builds explicitly
disable the backend and continue to use `build-frame/windows-native/`.
Neither build installs or launches the game.

This feature is for disposable **offline local expansion characters**. Do not
use TCP/IP hosting/joining or online characters for development testing.

## Development branch

Keep this work on the long-lived `feat/development-scenarios` branch. Per the
owner's direction, push development changes to that branch without opening a
pull request for it. Periodically bring in changes from `main` while continuing
development, using a merge to preserve the branch's history:

```powershell
git switch feat/development-scenarios
git fetch origin main
git merge origin/main
# Resolve any conflicts and run checks appropriate to the incoming changes.
git push origin feat/development-scenarios
```

## Setup and use

1. Install the developer DLL using the usual VR installation workflow, selecting
   the staged developer DLL as the plugin. Keep the normal release DLL available
   to restore after testing. See [installation](README.md).
2. Run `deps/python/python.exe steamvr/prepare_dev_scenarios.py --game "YOUR GAME DIRECTORY"`.
   This creates only `mods/pcvr-dev/pcvr-dev.mpq/modinfo.json` with a dedicated
   `pcvr-dev/` save path. It does not copy, generate, rename or edit any saves.
   Conflicting metadata is refused; existing files are not replaced.
3. Launch VR with `-mod pcvr-dev`. For the Steam wrapper, append these arguments
   after the game executable, for example:
   `deps/python/python.exe steamvr/launch_windows.py --frame-vr "YOUR GAME DIRECTORY/D2R.exe" -mod pcvr-dev`.
   Use the appropriate runtime option if your normal VR shortcut requires it.
4. Create a **new offline expansion character** in this environment and enter a game.
   Existing normal-play characters should not appear in this separate save list.
5. Open either hand's wheel and choose **DEV > Gear / Tests > Prepare <your class>**. The page
   adapts to the loaded class: Amazon, Sorceress, Necromancer, Paladin, Barbarian,
   Druid, Assassin or Warlock (requires the game's expansion content).
   Preparation requests level 90, 300 strength/dexterity/energy, 400 vitality,
   and all 30 class skills at level 20. Repeated preparation sets the same values.
   Assign active abilities using the usual wheel slot editor; passives apply
   normally. Existing bindings are preserved. For physical Paladin shield bashes,
   assign **Smite to the left trigger** and enable skill gestures. This is an
   overpowered test preset, not a legal progression build. Unlocking skills does
   not add new VR gestures; skills without gestures use native button casting.
6. Make room in the inventory, select a class gear kit, and equip the items using
   **Inventory**. Item creation is an all-or-nothing SDK transaction: an inventory
   capacity failure leaves existing items intact. The installed loader requires
   an exchange input, so the request creates a temporary inventory potion and
   consumes it in the kit transaction. A failed exchange removes only that
   generated potion. Nothing from your preexisting inventory is automatically removed
   or unequipped. Repeated grants add another kit if space permits.
7. Select **Arcane Sanctuary**. Other choices are Rogue Encampment, Lut Gholein,
   River of Flame and Chaos Sanctuary. Travel does not intentionally unlock quests
   or waypoints. Normal game effects still apply to the disposable character.
8. For wing rendering inspection, choose **DEV > Tyrael - Act II** on either
   hand's wheel. This requests level 73 at `(22578,15594)`, near Tyrael on his
   platform in Tal Rasha's Chamber. The destination comes from the installed
   3.3.93787 level/preset data; it does not complete the Duriel quest or kill
   the boss. Verify arrival, walk around Tyrael, and compare his wings before
   and during dialogue. The owner verified arrival and confirmed that disabling
   added indoor fog removes the wing silhouettes in this chamber.
9. For the Duriel arena's reported black room edge, choose **DEV > Duriel - Act II**.
   This requests level 73's native arrival point (`goto 73`), separately from the
   Tyrael platform shortcut. Prepare and equip the disposable character first:
   the command does not kill Duriel or complete his quest. Walk around the arena
   and inspect the walls with the current indoor-fog-off build. The owner found
   the room looked normal during inspection; the reported black edge was not
   reproduced, so its cause and whether the fog change fixes it remain unconfirmed.
10. For crossbow firing (#92), choose **DEV > Crossbow + bolts** on any class.
    This adds a unique Light Crossbow (Leadcrow) and 250 normal bolts, without
    granting another armor set. Use **Prepare <your class>** first if needed to
    meet equipment requirements, then equip the crossbow and bolts in Inventory.
    Choose **Blood Moor** to test outside town. Try Right Trigger with the current
    assignment, then use the right-hand wheel's slot editor to assign **Attack**
    to **Right Trigger** and compare. Test separate presses and a held trigger,
    aiming with the right hand alone and with a left-hand support grip. Release
    Right Grip while firing: Right Grip + Right Trigger is the loot-label chord.
    Record the original trigger assignment, whether bolts launch, aim direction
    and whether enemies take damage. The kit preserves bindings so the suspected
    missing/incompatible attack assignment can be checked; it is not a firing fix.
11. Choose **DEV > Bow + arrows** for Windforce and 250 normal arrows on any
    class, including Paladin. Equip them in Inventory after preparing the class.
    The existing **Crossbow + bolts** button remains available separately.
12. Choose **DEV > Small shield** for a Pelta Lunata buckler. The Paladin's
    Combat and Caster gear kits still supply the larger Herald of Zakarum, so
    keep it and swap shields in Inventory to compare reach and wrist rotation.
    The buckler is a separate grant because adding its four inventory cells to
    either 37-cell Paladin kit would exceed the inventory's 40-cell capacity.
    The small-shield button also works on other expansion classes.

Each class kit includes weapons, a helmet, armor, gloves, boots, belt, two rings and an
amulet. Staff and shapeshift kits omit an off-hand item. Native unique selection
and stat rolls apply: a Phase Blade can be Lightsabre or Azurewrath, for example,
and jewelry varies. Amazon bow gear includes 250 normal arrows; the separate
crossbow test kit grants only its weapon and 250 normal bolts. The standalone
bow kit grants Windforce and 250 arrows, and the small-shield kit only a buckler.
These buttons do not change equipped gear or bindings. Kits grant gear
only; choose one at a time and equip/stash it before granting the alternative.

| Class | Primary gear | Alternate gear |
| --- | --- | --- |
| Amazon | Windforce bow and arrows | Titan's Revenge javelins and Stormshield |
| Sorceress | Oculus orb and Stormshield | Mang Song's Lesson staff |
| Necromancer | Boneshade wand and Darkforce Spawn head | Wizardspike dagger and head for Poison Dagger |
| Paladin | Phase Blade and Herald of Zakarum shield | Caduceus scepter and shield |
| Barbarian | Two Grandfather swords; equip one for two-hand attacks | Throwing javelins and axe for Double Throw |
| Druid | Unique Thunder Maul for shapeshifting | Wizardspike dagger and Stormshield for casting |
| Assassin | Bartuc's and Jade Talon claws for dual-claw abilities | Wizardspike dagger and Stormshield for traps |
| Warlock | Grandfather sword and unique grimoire | Wizardspike dagger and unique grimoire |

Combat armor uses a unique Sacred Armor, Vampirebone Gloves, War Boots and
Mithril Coil. Caster armor uses Skin of the Vipermagi, Magefist, Waterwalk and
Arachnid Mesh. Barbarian and Druid helmets use their class uniques.

## Filming locations

Choose **DEV > Scenic Locations** on either hand's wheel. **Gear / Tests** keeps
all existing loadouts and inspection destinations, including Tyrael, Duriel,
Blood Moor, Arcane Sanctuary and Chaos Sanctuary. Reopening DEV starts on that
first page. Page tabs only navigate the menu; select a destination to travel.

| Destination | Suggested footage |
| --- | --- |
| Tristram | Burning village, close combat and loot labels |
| Cathedral | Gothic interior and indoor lighting |
| Andariel's chamber | Catacombs Level 4 and boss-room atmosphere |
| Kurast Bazaar | Ruins, stairs and ranged combat |
| Travincal | Temple architecture and Council arena |
| Mephisto's chamber | Durance of Hate Level 3 interior |
| Harrogath | Town backdrop for gear, shield reach and menu demonstrations |
| Bloody Foothills | Outdoor melee, bow and crossbow movement |
| Crystalline Passage | Ice cavern and indoor lighting |
| Arreat Summit | Mountain panorama and the Ancients' platform |
| Throne of Destruction | Baal's throne room |
| Worldstone Chamber | Worldstone backdrop |

These use native level entrances; walk to the desired shot after arrival.
Level IDs were checked against the installed 3.3.93787 tables. The shortcuts
do not reset bosses, start encounters, complete quests or respawn enemies;
what is available depends on the disposable offline character's game state.
Automated checks cover both menu pages, visible click targets, stale-page
rejection and native dispatch. Arrival and filming conditions still need
headset verification.

## Boundaries and status

The backend is compiled only with `D2RVR_DEVELOPER=ON`. Even that DLL enables the
tab only when the loader reports the exact `pcvr-dev` mod and its metadata matches
the generated isolated save path. Actions require a fresh, verified local class snapshot,
an alive/standing/walking/running character (including standing or walking in town),
and no open native menu. Pending
requests are cancelled on focus/tracking/session changes; old callbacks cannot
consume a later request.
Native command dispatch and item calls run without holding the developer status
lock, so rendering/status reads during area travel can continue. Completion from
a cancelled call cannot replace a newer request's status.
This is not a multiplayer-mode detector: use offline
local Play only, as required for all game testing in this repository.

The [pinned SDK](https://github.com/D2RLoader/PluginSDK/tree/717f727a0ec52912d1558764345f8fa3453a2bd6)
documents `ExecuteConsoleCommand` as **dispatch acceptance**, not gameplay success.
The UI therefore reports “Commands sent” for preparation/travel. Verify stats and
the area in game. Gear success means the SDK confirmed the complete inventory
transaction; SDK failures are logged under `Frame development`.
Preparation and travel refusals show an action-unavailable message. Only a failed
gear transaction suggests making room in inventory; preparation does not use
inventory space. Refused requests log their player mode and readiness checks.

Arcane Sanctuary targets **area 74's native arrival point**, not Horazon's Journal.
The Summoner's platform must still be found in the generated map. Direct arrival
at the platform, automatic character creation, and automatic equipment replacement
are not implemented. No game assets or save files are distributed.

Automated checks cover wheel routing for all eight classes, class-dependent paint
invalidation, all 240 exact skill assignments, class kits and standalone weapon/
shield transactions for every class,
cancelled/stale requests, inventory failure, and isolated setup. A local table
audit checked skill identifiers, unique base availability, level requirements
and empty-inventory cell capacity. No extracted tables are distributed.
Live follow-up on Steam D2R 3.3.93787 / SteamVR OpenXR with an offline Paladin:
logs confirm level-90 preparation, a committed combat kit, and travel from Act I
to the `act2_arcane` biome with continuing native vitals updates after arrival.
An earlier travel hang was traced to rendering waiting on the developer status
lock; native dispatch now runs outside that lock. The headset model was not
recorded for this follow-up. Headset comfort, visual correctness, other
destinations and long-session behavior remain unverified. Do not treat the
preview or mocked SDK tests as headset validation.

The subsequent melee investigation found native inventory slots disagreeing with
the visible VR wrists: a shield was reported in slot 4 while drawn on the left,
and the sword in slot 5 while drawn on the right. Main-weapon contact now follows
the final right-controller attachment (or the authored free-staff attachment),
independently of inventory debug axes. Either native slot can establish shield
presence. Motion thresholds and existing bindings are preserved. Contact logs
now distinguish invalid geometry, insufficient speed and insufficient stroke
travel. The latest hand-mapping fixes and all-class presets still need live
headset verification; automated contact tests do not establish rendered alignment.
