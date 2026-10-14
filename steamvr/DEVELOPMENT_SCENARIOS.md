# Offline development scenarios

Build with `steamvr/build_windows.ps1 -Developer -Test`. The developer DLL is
staged at `build-frame/windows-developer/d2rl-vrcam.dll`; normal builds explicitly
disable the backend and continue to use `build-frame/windows-native/`.
Neither build installs or launches the game.

This feature is for disposable **offline local expansion Paladins**. Do not
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
4. Create a **new offline expansion Paladin** in this environment and enter a game.
   Existing normal-play characters should not appear in this separate save list.
5. Open either hand's wheel and choose **DEV > Prepare Paladin**. This requests
   level 90, test attributes, and Zeal, Smite, Holy Shield, Sacrifice and Fanaticism
   at level 20, plus prerequisite skills. Assign abilities using the usual wheel
   slot editor. This is an overpowered test preset, not a legal progression build.
6. Make room in the inventory, select **Combat gear**, and equip the items using
   **Inventory**. Item creation is an all-or-nothing SDK transaction: an inventory
   capacity failure leaves existing items intact. The installed loader requires
   an exchange input, so the request creates a temporary inventory potion and
   consumes it in the kit transaction. A failed exchange removes only that
   generated potion. Nothing from your preexisting inventory is automatically removed
   or unequipped. Repeated grants add another kit if space permits.
7. Select **Arcane Sanctuary**. Other choices are Rogue Encampment, Lut Gholein,
   River of Flame and Chaos Sanctuary. Travel does not intentionally unlock quests
   or waypoints. Normal game effects still apply to the disposable character.

The combat kit uses unique Phase Blade, Gilded Shield, Shako, Templar Coat,
Vampirebone Gloves, War Boots and Mithril Coil bases, plus two unique rings and a
unique amulet. Native unique selection and stat rolls apply, so the Phase Blade
can be Lightsabre or Azurewrath and jewelry varies. The alternative **Caster gear**
kit uses a Caduceus, Serpentskin Armor, Battle Gauntlets, Demonhide Boots and
Spiderweb Sash in place of the corresponding combat pieces. It grants gear only;
it does not change the Zeal/Smite skill preset.

## Boundaries and status

The backend is compiled only with `D2RVR_DEVELOPER=ON`. Even that DLL enables the
tab only when the loader reports the exact `pcvr-dev` mod and its metadata matches
the generated isolated save path. Actions require a fresh local Paladin snapshot,
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

Automated checks cover wheel routing, disabled states, the real backend with fake
SDK services, cancelled/stale requests, inventory failure, and isolated setup.
Live follow-up on Steam D2R 3.3.93787 / SteamVR OpenXR with an offline Paladin:
logs confirm level-90 preparation, a committed combat kit, and travel from Act I
to the `act2_arcane` biome with continuing native vitals updates after arrival.
An earlier travel hang was traced to rendering waiting on the developer status
lock; native dispatch now runs outside that lock. The headset model was not
recorded for this follow-up. Headset comfort, visual correctness, other
destinations and long-session behavior remain unverified. Do not treat the
preview or mocked SDK tests as headset validation.
