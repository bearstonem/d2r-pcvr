# Offline development scenarios

Build with `steamvr/build_windows.ps1 -Developer -Test`. The developer DLL is
staged at `build-frame/windows-developer/d2rl-vrcam.dll`; normal builds explicitly
disable the backend and continue to use `build-frame/windows-native/`.
Neither build installs or launches the game.

This feature is for disposable **offline local expansion characters**. Do not
use TCP/IP hosting/joining or online characters for development testing.

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
5. Open either hand's wheel and choose **DEV > Prepare <your class>**. The page
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

Each kit includes weapons, a helmet, armor, gloves, boots, belt, two rings and an
amulet. Staff and shapeshift kits omit an off-hand item. Native unique selection
and stat rolls apply: a Phase Blade can be Lightsabre or Azurewrath, for example,
and jewelry varies. Amazon bow gear includes 250 normal arrows. Kits grant gear
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
invalidation, all 240 exact skill assignments, both gear transactions per class,
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
