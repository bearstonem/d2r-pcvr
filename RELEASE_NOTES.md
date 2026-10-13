# D2R PCVR v0.3.10 — Windows preview

Play **Diablo II: Resurrected in VR on Windows** with
**SteamVR or Virtual Desktop (VDXR)**. This preview is for **local offline
single-player** and accepts your **Steam copy of D2R 3.3.93787** or
**Battle.net copy of D2R 3.3.93847** (experimental).

**This download is a mod, not the original game. You must purchase and install
your own legitimate copy of Diablo II: Resurrected.** D2R PCVR adds VR
support and motion interactions to that existing game installation.

**No Blizzard game assets are included in the installer or ZIP.** They contain mod files
and licensed third-party components. Required item/skill icons and potion
models are read from your own installed game into local caches during setup;
do not redistribute those caches or your game files.

## Headset connections

- **Steam Frame:** SteamVR streaming.
- **Quest 3:** Steam Link with SteamVR, or Virtual Desktop with VDXR.
- **Quest 2:** the same Quest connection options; compatibility is not yet confirmed.

The game runs on your Windows PC and streams to your headset.

Battle.net has been confirmed with SteamVR. Battle.net with Virtual Desktop /
VDXR is available but has not yet been confirmed. The Steam desktop client
is required for library shortcuts, including with the Battle.net edition.
**Battle.net owners do not need to purchase a second copy through Steam.**

## What's new: performance update phase 1

- **Remove a periodic VR stall:** CPU diagnostics now run on a background
  worker. The system thread scan previously blocked rendering for about 53 ms
  every ten seconds on the test PC.
- **Reduce render callback contention:** world draws avoid unnecessary HUD
  locking, and immutable resource descriptions are cached with lifetime guards.
- **100% resolution by default:** fresh and full installations now set the game's
  Resolution Scale to 100%, keeping FXAA, DLSS off and medium shadows/AO/atmospherics.
  Update / repair preserves your existing video settings. If an older installation
  uses 80%, change **Options > Video > Resolution Scale** to **100%** in the game.
- **Consistent draw-distance defaults:** missing INI values use the shipped
  two terrain rings and object radius 300. Explicit choices are retained;
  the object-distance control now changes in steps of 50.
- **Clearer performance diagnostics:** logs separate XR waiting/submission from
  eye draws, and profiling reports use the full stereo-pair refresh deadline.
- **Installer file-access errors:** setup identifies the dependency it could not
  read and points to Windows Security Protection History when relevant.

Run **D2R-PCVR-Setup-0.3.10.exe** for graphical setup, or use the portable ZIP.
The EXE includes a private Python runtime and creates the matching shortcuts.
Choose **Update / repair and keep calibration and video settings** when upgrading;
keep existing setup backups. Normal desktop Play and Windows removal are retained.

Use the game's **left or right corner minimap** layout for the Map tab.
Full-screen automap is available through the game's normal map controls.

## Validation and remaining checks

The native Windows build passes **43 CTest checks**, and **84 Python checks**
cover setup, repair/removal, rollback, Battle.net, launcher lifecycle, installer
payload staging and profiling reports. Installer fixtures verify that a full
install changes 80% to 100%, repair preserves custom settings, and rollback
retains character progress for both Steam and Battle.net.

Phase 1 was tested with an offline Paladin on Steam D2R 3.3.93787, Ryzen 9 5950X /
RTX 5090, SteamVR OpenXR and Steam Link to Steam Frame at 90 Hz. Background
scans of 58-63 ms no longer produced the recurring long eye-draw stall in three
360-pair captures. A separate one-minute walking/turning trial with the existing
optional GPU-overlap setting had normal stereo, tracking and controls according
to the owner. That option remains off in the shipped defaults.

This phase improves stalls and callback overhead. It still runs two engine draw
passes per stereo pair; a larger rendering change remains future work. These
captures do not establish a universal FPS gain or resolve the RTX 5080 report in
[issue #20](https://github.com/bearstonem/d2r-pcvr/issues/20).

The compiled Inno Setup lifecycle fixture passes installation, Windows Apps
registration, helper retention after a blocked removal, and successful removal.
It uses a stub backend and does not modify a real game installation.

Longer sessions, area transitions, both-eye Map/label/spatial-menu regressions,
Amazon arrow skills and a fresh owner-triggered Frame shortcut launch still need
headset checks. This phase has no Battle.net gameplay or RTX 5080 validation.
Quest 2 and Battle.net VDXR remain unconfirmed. Issue #11 still needs the
reporter's detailed installation log.

## Features

- First-person VR with motion controls, free-swing melee, aimed spells, bows
  and physical javelin throws.
- Guided Windows installation, headset/connection selection and automatic
  Steam library shortcuts. Update / repair keeps calibration and video settings.
- Body calibration for your height and arm span on first load, or through
  **Left Grip + Frame Left B / Quest Left Y → Calibrate body**.
- Weapon-set swapping by reaching your right hand over either shoulder and
  pressing its grip; the swap hand is adjustable in VR settings.
- Physical belt potions and wrist health, mana, level and experience indicators,
  plus a spell-menu Map tab showing explored areas and native markers.
- Item pickup, chests, shrines and cave-entrance interactions through the
  right A button.
- Inventory, skills, conversations and Journal through the tabbed game menu:
  **Frame Left A / Quest Left X**.
- Native menu buttons and sticks alongside laser selection and inventory dragging.
  Native **X** drops an item; **press and hold right-stick click** sorts inventory.
- A complete **[controller reference](CONTROLS.md)** in the README and ZIP.

## Download, install and play

**Download blocked or stuck as `Unconfirmed... .crdownload`?** Press **Ctrl+J**
in your browser and check the exact download message. Browser download protection
is separate from Windows real-time protection. If a file is reported as a virus
or disappears, check **Windows Security > Virus & threat protection >
[Protection history](https://support.microsoft.com/en-us/windows/security/windows-security/protection-history-in-the-windows-security-app)**
or your other antivirus history. Keep protection enabled while investigating,
and share the warning/detection name and affected filename when asking for help.
An unfamiliar-download warning does not establish whether a file is safe; see
[Chrome's download guidance](https://support.google.com/chrome/answer/6261569?hl=en).

First launch your purchased game normally through its store, finish the account
prompts, load an offline character, then use **Save & Exit** and fully close D2R.

For graphical setup, download **D2R-PCVR-Setup-0.3.10.exe**, run it and follow the
wizard. Choose **Steam > Exit** when asked; closing its window leaves Steam
running. Reopen Steam, connect your headset and use
the matching VR shortcut with an offline character.

For portable setup:

1. Download **d2r-pcvr-windows.zip** under **Assets** below.
2. Extract `d2r-pcvr-preview` to a permanent folder.
3. Double-click **Install-Windows.cmd** and follow the setup wizard. It creates
   the Steam shortcuts for your chosen game edition, headset and connection.
   See **INSTALL-WINDOWS.md** for help or manual setup.
4. Open Steam, connect your headset and use the matching **SteamVR** or
   **Virtual Desktop** library entry. Battle.net entries include **(Battle.net)**.
   Keep SteamVR closed for Virtual Desktop / VDXR.
5. Accept Steam's launch prompt if shown and select an **offline character**.

To launch from Steam or the headset, use the shortcuts created by setup.
The included **Launch-VR-*.cmd** files are for the **Steam edition**;
Battle.net players should use the **(Battle.net)** library entries. These
launch the mod directly, so the Battle.net app may not show it as in progress.
Manual directions are under **Add Steam library shortcuts** in **INSTALL-WINDOWS.md**.
Use the matching entry for SteamVR or Virtual
Desktop; keep SteamVR closed when using Virtual Desktop/VDXR.

**Upgrading from an earlier preview?** Follow **Upgrade while keeping your settings** in
**INSTALL-WINDOWS.md** to retain body calibration and video settings.

Keep the installed helper folder or extracted ZIP folder after installation. For ordinary desktop play,
fully exit VR and use the original Steam or Battle.net **Play** button.
Use the unmodded game for multiplayer. Keep **setup-backups**, including
backups in older extracted release folders, until removal is complete.

Read **INSTALL-WINDOWS.md** and **CONTROLS.md** in the helper folder or ZIP, or open the
[installation guide](https://github.com/bearstonem/d2r-pcvr/blob/v0.3.10-windows-preview/INSTALL-WINDOWS.md)
and [controller reference](https://github.com/bearstonem/d2r-pcvr/blob/v0.3.10-windows-preview/CONTROLS.md).
