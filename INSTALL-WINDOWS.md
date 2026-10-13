# Install and play D2R PCVR on Windows

This preview supports **SteamVR and Virtual Desktop (VDXR)** on Windows.

**D2R PCVR is a mod installed on top of the original game. You must purchase
and install your own legitimate copy of Diablo II: Resurrected. The
game is not included, and this mod cannot run without it.**

**The installer and release ZIP include no Blizzard game assets.** They contain the VR mod
and licensed third-party components. The installer reads the required
item/skill icons and potion models from your own installed game and keeps
them in local caches. Do not redistribute those caches or your game files.

This preview accepts your **Steam copy of Diablo II: Resurrected
3.3.93787** or **Battle.net copy of D2R 3.3.93847** (experimental).
Use a **local offline character**. For Battle.net, see
[Battle.net preview packages](#battlenet-preview-packages).
The game runs on your PC and streams to the headset.

## Before you start

1. Purchase and install D2R through Steam or Battle.net. Launch it normally
   through that store once, finish any account prompts, create/select an offline
   character, then fully exit. Install the Steam desktop client and sign in once,
   including for a Battle.net installation; setup uses it for VR library shortcuts.
2. Set up your headset and both controllers, including floor/room setup:

   | Headset | Connection |
   | --- | --- |
   | Steam Frame | SteamVR streaming |
   | Quest 3 | [Steam Link](https://help.steampowered.com/en/faqs/view/0E2C-406B-9135-38A4) with SteamVR, or [Virtual Desktop](https://www.vrdesktop.net/) with its Windows Streamer |
   | Quest 2 | The same Quest connection options; compatibility is not yet confirmed |

   For Virtual Desktop, use the **Quest app** and install the **Windows
   Streamer** on your PC. Connect until you can see the PC desktop in the headset.
3. Open the [release page](https://github.com/bearstonem/d2r-pcvr/releases).
   Under **Assets**, choose **D2R-PCVR-Setup-VERSION.exe** if offered, or the
   portable **d2r-pcvr-windows.zip**. Older releases, including v0.3.7, offer the ZIP only.
4. For the ZIP, extract `d2r-pcvr-preview` to a permanent folder, such as
   `C:\Mods\d2r-pcvr-preview`. Keep this folder after installation.

**Download blocked or left as `Unconfirmed... .crdownload`?** Press **Ctrl+J**
in your browser and read the exact status beside the download. A browser
warning, antivirus detection and interrupted transfer need different fixes.
Browser protection is separate from Windows real-time protection; disabling
one does not resolve the other. Check the browser message and, if it reports a
virus or a file disappears, Windows Security **Protection history** or your
other antivirus history. Include the warning/detection name and filename when
asking for help. Keep protection enabled while investigating. See
[Chrome download warnings](https://support.google.com/chrome/answer/6261569?hl=en).

## Install

### Graphical Setup EXE

Double-click **D2R-PCVR-Setup-VERSION.exe** on the PC desktop. The wizard
asks which purchased game copy, headset and connection to use. Confirm the
detected Steam, game, Saved Games and SteamVR folders; use **Browse** to correct
them. Select a Steam account if more than one is present.

On a first install, choose **Full install with recommended settings**. For an
existing mod, choose **Update / repair and keep calibration and video settings**.
Choose **Only configure Steam shortcuts** to repair library entries without
replacing the installed mod. Review the choices, fully exit D2R and use
**Steam > Exit**. Closing Steam's window leaves it running. On **Check before
installing**, setup shows the detected game/Steam process status, selected VR
runtime and any selections or backups needing attention. Resolve the reported
items and click **Check again**, then proceed to **Install**. Setup checks again
before making changes. Exiting Steam temporarily disconnects headset streaming;
you can reopen Steam after setup finishes.

The EXE includes its own verified Python runtime; no separate Python install
is needed. Internet access is still required to download D2RLoader and ReShade.
It installs helper files, instructions, logs and **setup-backups** in
`%LOCALAPPDATA%\Programs\D2R PCVR`. Keep this folder: Steam's VR shortcuts
use it. Installation help and controller mappings are also in the Start menu.

After setup, reopen Steam, reconnect the headset and use the matching **Diablo
II: Resurrected VR** library entry with an **offline character**. Steam Frame
uses **SteamVR**. Quest offers **Steam Link / SteamVR**, **Virtual Desktop / VDXR**
or **Both**, which creates two shortcuts. Virtual Desktop needs its Quest app
and Windows Streamer; keep SteamVR closed when launching with VDXR.

The finish screen names the exact Steam shortcut(s) to launch. **Check
installation** reads the required mod files, packaged DLL hashes, runtime
manifest(s) and Steam shortcut configuration; it does not launch the game or
verify headset operation. **Open setup logs** and **Open game logs** help collect
diagnostics if the game closes at startup. For Battle.net account prompts, use
normal Battle.net Play, finish the prompt and exit before trying VR again.

If setup fails, keep `setup-backups`. The helper folder contains
`setup-last.log` with the wizard output and `setup-install.log` with the mod
installer's full output. Share the error at the end of those logs when reporting
a problem; review them for personal paths before posting. Older installers may
only report an exit code in `setup-last.log` and do not create `setup-install.log`.
The error dialog offers **Windows Security**, **Open logs**, **Retry** and
**Stop setup**. Resolve the reported error before retrying. Errors before helper
installation log to a temporary folder; use **Open logs** to find that attempt's
`setup-last.log` before closing the wizard. Review personal paths before sharing.

If a log says a required download cannot be read, or a browser reports a virus,
check **Windows Security > Virus & threat protection >
[Protection history](https://support.microsoft.com/en-us/windows/security/windows-security/protection-history-in-the-windows-security-app)**
(or your other antivirus application's history). Include the detection name,
affected file path and time with the error report, after removing personal details.
A readable dependency ZIP with the pinned SHA-256 is reused on retry; setup does
not deliberately delete it. A file disappearing or becoming unreadable needs
its security/file-access event checked. A checksum match establishes the expected
version, not that an antivirus detection is a false positive. Keep protection enabled
while the detection is investigated.

You can rerun the EXE safely. It updates matching shortcuts without duplicates.
When upgrading a recorded ZIP installation, it copies and verifies its referenced
game backups into the permanent helper folder before updating the mod. Do not
delete the old extracted folder until Setup succeeds. If an older mod has an
incomplete installation record, the wizard asks you to locate its original
backup folder. The page shows the last recorded location and why the backup
lookup failed. Choose the original extracted mod folder, an individual snapshot,
or the folder containing snapshots (`setup-backups\game` or `build-frame\backups`).
Setup validates your selection on **Next** and also checks alongside recorded
backups for older snapshots. **Deleted the old folder?** opens recovery help.
If the original backup is unavailable, cancel; see
[Original backups permanently deleted](#original-backups-permanently-deleted)
to prepare a fresh mod installation without downloading the game again.

Setup does not change Windows security settings or the system OpenXR runtime.
Normal Steam / Battle.net **Play** launches the stock desktop game.

### Portable ZIP setup

Double-click **Install-Windows.cmd** in the extracted folder and follow its prompts
on the PC desktop. No separate Python install is needed; setup downloads a small,
verified private runtime from [python.org](https://www.python.org/downloads/release/python-31210/).
An internet connection is needed for the required components.

1. Choose **Steam** or **Battle.net**. This selects which installed copy to
   modify when you have both. Older Steam-only packages skip this choice;
   use the v0.3.7 ZIP for Battle.net.
2. Choose **Steam Frame**, **Quest 3**, **Quest 2** or another SteamVR headset.
3. For Quest, choose **Steam Link / SteamVR**, **Virtual Desktop / VDXR**,
   or **Both**. Steam Frame uses SteamVR.
4. Confirm the detected Steam and game folders, plus SteamVR if selected. Setup
   looks up the selected edition's game folder; you can enter its path if needed.
   It checks the game edition and supported version before installation. Choose
   your Steam account if more than one is present, and confirm the saved-games folder.
5. For an existing mod installation, choose **Update / repair and keep calibration
   and video settings**. A full install applies recommended settings and resets calibration.
   **Only configure Steam shortcuts** skips installing mod files.
6. Review the choices and continue. When prompted, use **Save & Exit** in D2R and
   **Steam > Exit** on the desktop. Steam must be fully closed while setup adds
   shortcuts. This also disconnects headset streaming temporarily.
7. When setup finishes, open Steam, reconnect your headset and play the matching
   **Diablo II: Resurrected VR** entry in your library. Select an **offline character**.

Setup creates the appropriate Steam library shortcut automatically, including
**Include in VR Library** for SteamVR. For the Steam edition it also configures
D2R's Steam launch options; for Battle.net it creates shortcuts that launch
the mod from the selected Battle.net folder. Choosing
**Both** creates two entries. Rerunning setup updates these entries without adding
duplicates. The original Steam or Battle.net **Play** button still launches the stock desktop game.
It does not change the system's OpenXR runtime or Windows security settings.

Backups are kept in **setup-backups** inside the extracted mod folder: game files
and saves under **game**, and Steam configuration under **steam-...**. Keep this
folder and **setup-result.json** after installation. Steam backups contain account
configuration; keep them private and do not share them with the mod ZIP.

## Battle.net preview packages

The v0.3.7 ZIP includes **bin/CascLib.dll** and offers both editions in
**Install-Windows.cmd**. For Battle.net, it requires
your **purchased Battle.net copy of D2R 3.3.93847** on Windows. Use a local
offline character. SteamVR and Virtual Desktop (VDXR) launch options are
available. Battle.net has been tried with SteamVR; Battle.net with VDXR has not
yet been confirmed. Battle.net support is still experimental.

1. Launch your Battle.net game normally once, complete its account prompts,
   load an offline character, then use Save & Exit and fully close the game.
2. Run **Install-Windows.cmd** and select **Battle.net**. Choose your
   headset and connection, then confirm the game and saved-games folders.
3. Follow the remaining setup prompts above, including fully exiting Steam
   while its library shortcuts are created.
4. Reopen Steam and use **Diablo II: Resurrected VR (Battle.net) - SteamVR**
   or **Diablo II: Resurrected VR (Battle.net) - Virtual Desktop**.

These entries can coexist with the Steam edition's VR shortcuts. They launch
the loader in the selected Battle.net game folder and select the OpenXR runtime
for that process. They do not change the Steam edition's launch options or
Battle.net's normal Play button. Steam is used for library shortcuts and, when
selected, SteamVR; a second game purchase through Steam is not required.
These are non-Steam library entries. They start the mod loader directly, so the
Battle.net app may not show the game as in progress. Its normal **Play** button
continues to launch the unmodded game.

If included, **Install-BattleNet-Preview.cmd** opens the same setup wizard;
it is not required. If setup offers only Steam, download a package that
explicitly includes Battle.net support before installing on that edition.

Required icons and potion models are read from your own Battle.net archives
and kept locally. No Blizzard game files or generated asset caches are included
in the mod package. The existing [controller mappings](CONTROLS.md) also apply.

## Manual installation (Steam edition)

Use this section only for the Steam edition if you are installing without the
setup wizard. Battle.net players should use **Install-Windows.cmd** as described above.
Install [Python 3.10 or newer](https://www.python.org/downloads/windows/) with
the Windows **py launcher**, then check `py -3 --version`.

Open PowerShell in the extracted **d2r-pcvr-preview** folder. Find your D2R
folder through Steam → D2R Properties → Installed Files → Browse.
Replace the example path below with that folder, then run both lines:

```powershell
$Game = 'C:\Program Files (x86)\Steam\steamapps\common\Diablo II Resurrected'
py -3 .\steamvr\install.py --game "$Game" --plugin .\bin\d2rl-vrcam.dll --openxr-loader .\bin\openxr_loader.dll --deps .\deps --download-deps
```

The installer downloads the required components, prepares icons and potions
from your installed game, and backs up your saves and replaced files.
**Keep the backup path it prints** in case you want to remove the mod later.
The download does not include D2R itself or other players' saves.

The installer applies recommended video settings, including FXAA, DLSS off,
100% resolution scale and medium shadows. Running the full installer again
resets the mod's settings, including body calibration; use the upgrade steps
below if you want to keep your settings.

If your saves are in a custom location, add
`--saves 'D:\Your\Saved Games\Diablo II Resurrected'` to the install command.

## Set Steam launch options (Steam edition)

The wizard handles this automatically. For a manual installation, find SteamVR's folder through
Properties → Installed Files → Browse. Set its path below to the
`steamxr_win64.json` file inside that folder, then run these lines in the
same PowerShell window:

```powershell
$SteamVR = 'C:\Program Files (x86)\Steam\steamapps\common\SteamVR\steamxr_win64.json'
$Python = py -3 -c "import sys; print(sys.executable)"
$Wrapper = (Resolve-Path .\steamvr\launch_windows.py).Path
if (!(Test-Path -LiteralPath $SteamVR)) { throw 'Set $SteamVR to your installed SteamVR folder.' }
'"{0}" "{1}" "--frame-openxr-runtime={2}" %command%' -f $Python, $Wrapper, $SteamVR
```

Copy the **single command it prints** into Steam → D2R Properties → General
→ Launch Options. Keep exactly one `%command%`.
This setup lets you use either of the included VR launchers.

**Only using Virtual Desktop, without SteamVR installed?** Generate your
launch options with these lines instead:

```powershell
$Python = py -3 -c "import sys; print(sys.executable)"
$Wrapper = (Resolve-Path .\steamvr\launch_windows.py).Path
'"{0}" "{1}" %command%' -f $Python, $Wrapper
```

## Launch and play

Connect your headset first, then open the extracted mod folder:

The following `.cmd` launchers are for the **Steam edition**. For Battle.net,
use the **(Battle.net)** library entries created by the wizard.

| Connection | Double-click |
| --- | --- |
| Steam Frame with SteamVR | **Launch-VR-SteamVR.cmd** |
| Quest with Steam Link and SteamVR | **Launch-VR-SteamVR.cmd** |
| Quest with Virtual Desktop | **Launch-VR-VDXR.cmd**, with SteamVR closed |

Accept Steam's extra launch-arguments prompt if shown, then select your
**offline character**.

Your store launcher's normal **Play** button opens the unmodded desktop game. This mod is
for offline play only; use the unmodded game for multiplayer. Always use
**Save & Exit** and fully close D2R before switching headset connections or
returning to desktop play.

## Add Steam library shortcuts

The setup wizard adds shortcuts for your selected edition automatically.
The manual directions below are for the **Steam edition**. Battle.net players
can rerun **Install-Windows.cmd**, select **Battle.net**, and choose
**Only configure Steam shortcuts** to recreate their entries.

To add Steam-edition shortcuts manually after
completing **Set Steam launch options** above, you can add separate
Steam library entries for each VR connection. These also let you launch from
the headset without opening the mod folder.

1. In desktop Steam, choose **Games → Add a Non-Steam Game to My Library**,
   click **Browse**, and select `steam.exe` from your Steam installation.
   Its default location is `C:\Program Files (x86)\Steam\steam.exe`; use
   your actual Steam folder, which may differ from your D2R library folder.
2. Click **Add Selected Programs**, then right-click the new library entry
   and open **Properties**.
3. Set **Target** and **Start In** to your Steam paths, keeping the quotes:

   | Field | Example |
   | --- | --- |
   | Target | `"C:\Program Files (x86)\Steam\steam.exe"` |
   | Start In | `"C:\Program Files (x86)\Steam"` |

4. Choose the matching name and paste its **Launch Options**:

   | Library name | Launch Options |
   | --- | --- |
   | Diablo II: Resurrected VR - SteamVR | `-applaunch 2536520 --frame-vr` |
   | Diablo II: Resurrected VR - Virtual Desktop | `-applaunch 2536520 --frame-vr --frame-vdxr` |

   These are the shortcut's launch options. Leave the original D2R game's
   Python/wrapper launch options configured as described above.
5. For the **SteamVR** entry, enable **Include in VR Library**. For the
   **Virtual Desktop** entry, leave it disabled so Steam does not start SteamVR.
6. To add the other connection, repeat these steps with a second entry.
   If Steam will not add `steam.exe` twice, add another program as a
   placeholder, then replace its **Target**, **Start In**, name and launch
   options with the values above before using it.

Connect your headset, then select the matching library entry and press
**Play**. On Steam Frame or Quest Steam Link, use the **SteamVR** entry in
the headset's Steam library. For Virtual Desktop, connect through its Quest
app, close SteamVR, and start the **Virtual Desktop** entry from the streamed
PC desktop. Select an **offline character** in the game.

The original D2R library entry's **Play** button still opens the unmodded game.
If a shortcut remains on **Launching**, fully close D2R and check that its
launch options use the `-applaunch` command above.
Steam's [non-Steam shortcut guide](https://help.steampowered.com/en/faqs/view/4B8B-9697-2338-40EC)
also explains how to add a library entry.

## Calibrate your body

The first time you enter the world, a calibration prompt appears:

1. Stand straight somewhere safe, with both feet on the floor.
2. Stretch your arms out sideways at shoulder height, like a T.
3. Release both triggers, then hold them together while staying still.
4. When **Saved** appears, release the triggers to return to the game.

The world stays live during calibration. **Right B** cancels or skips.
Repeat calibration through **Left Grip + Frame Left B / Quest Left Y → Calibrate body**, especially
after changing headset or player. If the prompt cannot find the floor,
complete your headset's floor/room setup and reopen calibration.

## Controls

| Action | Control |
| --- | --- |
| Walk / snap turn | Left stick / right stick left-right |
| Game menu: inventory, skills, character and Journal | **Frame Left A / Quest Left X** |
| Menu select / drag | Point the right-hand laser and use right trigger |
| Laser context / alternate click | Left Grip + Right Trigger |
| Native menu navigation / back | Sticks and face buttons / Right B |
| Inventory: drop selected item | Quest Left X / Frame Left A; follow the native hold prompt |
| Inventory: sort | Press and hold right stick click |
| VR settings | Left Grip + Frame Left B / Quest Left Y |
| Spell wheel | Hold Left Y, turn the left palm toward you, then select with right laser + trigger |
| Assign wheel skills | Spell wheel → **EDIT SKILLS** → slot → learned skill |
| Basic attack / bow on right trigger | Select **WEAPON** in the wheel center; release the selection trigger first |
| Melee | Swing your equipped melee weapon through an enemy, even with a skill selected |
| Manual attack / selected spell | Right trigger |
| Bow | Bring the right hand to the bowstring, hold right trigger, draw back, then release |
| Javelin | In **WEAPON** mode, hold right trigger, make a throwing motion, then release |
| Swap weapon sets I/II | Reach your right hand over either shoulder and press grip; change the swap hand in VR settings |
| Seated weapon swap / stow | Wheel → **EQUIPMENT / BELT** → **Weapon I**, **Weapon II** or **Stow/Draw** |
| Grab a belt potion | Reach to a bottle and hold grip with either hand |
| Drink | Keep grip held, bring the bottle opening to your mouth and tip it; release an unused bottle to return it |
| Quick potion | Wheel → **EQUIPMENT / BELT** → select a potion with right trigger |
| Wrist health / mana / XP | Look at the back of your left wrist, like checking a watch |
| Minimap tab | Spell wheel ? **MAP** at the bottom; right laser + trigger |
| Map / height reset | Left stick click / Left Grip + left stick click |
| Recenter | PC F11 |

Keep the D2R window focused on the PC so menu clicks work.
The spell wheel's bottom **MAP** tab shows the explored minimap. Keep holding
left secondary to view it; choose **SPELLS** to return to skills or release the
button to close. Use the game's left or right corner minimap layout. The Map
tab temporarily opens the automap when needed and restores its previous
visibility afterward. Left stick click still toggles the normal map. Health,
mana, level and XP progress remain on your left wrist.
For the full Quest and Steam Frame mappings, including all eight quick-cast
shortcuts and the game's A/B/X/Y/LB/RB/LT/RT menu prompts, see
**[Controller reference](CONTROLS.md)**. Use a stick or face button for native
navigation; release it and deliberately re-aim the laser to resume pointing
and dragging. Laser dragging works in inventory, stash and merchant panels.
Free swings pause while casting, using menus or handling a potion. Selecting
a wheel skill changes the right-trigger action; its displayed quick-cast
binding also remains available.

## If something goes wrong

- **Game opens flat:** use the matching VR library shortcut and connect your
  headset before launching. For the Steam edition, check the Steam launch options.
- **Virtual Desktop cannot launch VR:** install its Windows Streamer in the
  default location, connect the headset and close SteamVR before trying again.
- **An install file is quarantined:** check Windows Security's Protection
  History for the affected file. Installation cannot finish while a required
  component is quarantined.
- **Setup cannot update Steam:** choose **Steam > Exit**, then retry. Closing
  Steam's window leaves it running. Setup does not terminate games or Steam for you.
- **Setup reports an unsupported Steam configuration:** the original configuration
  is kept. Steam-edition players can use the manual installation and shortcut
  directions above; those commands do not apply to Battle.net.
- **D2R asks for a license/account check:** fully exit, launch the game with
  your store launcher's normal Play button while connected to the internet, finish the prompt
  and exit before launching VR again.
- **Body height or reach feels wrong:** repeat body calibration while standing
  straight with your arms out, and check the headset's floor setup.

### Original backups permanently deleted

The original backup is needed to restore the exact pre-mod installation.
Downloading the mod ZIP again does not recreate a backup made by an earlier
setup, and **Full install** alone does not bypass a missing backup. First check
the Recycle Bin or another backup for the original extracted mod folder.

If it is permanently gone and you want a **fresh mod installation**, you can
archive the existing mod components and install again without downloading the
game data. This resets mod calibration; it cannot reconstruct your lost
pre-mod settings or the exact previous installation.

1. Cancel setup and Save & Exit D2R. **Steam edition only:** remove the old D2R
   PCVR launch wrapper from the original game's **Properties > General > Launch
   Options**, preserving unrelated custom arguments. Ask for help if you cannot
   identify those options; a fresh receipt cannot recover the launch options
   from before the lost installation. Then choose **Steam > Exit**.
2. Copy the **entire Diablo II Resurrected Saved Games folder** to a separate
   backup location. Its default location is
   `%USERPROFILE%\Saved Games\Diablo II Resurrected`; use your actual folder if
   Saved Games is redirected. Keep the original save folder in place too.
   This includes your offline characters, shared stash and settings.
3. Open the game folder containing `D2R.exe`: **Battle.net > D2R > Show in
   Explorer**, or **Steam > Properties > Installed Files > Browse**.
4. Create a separate archive folder **outside** the game folder, such as
   `Desktop\D2R-old-mod`. Move these exact items into it **if present**:

   ```text
   d2rloader
   reshade-shaders
   D2R_Sky_ours
   D2RLoader.exe
   D2RCore.dll
   d2rloader.mpq
   ReShade64.dll
   openxr_loader.dll
   ReShade.ini
   D2R_Frame_ReShade.ini
   ```

   These loader/ReShade folders may also contain other custom mod components;
   keep the archive. Leave `D2R.exe`, `Data`, `mods`, Saved Games and all other
   game files in place. Do not just delete the installation receipt or invent
   a replacement backup. Moving the full set removes the old active mod and
   its receipt together while retaining a copy for reference.
5. Rerun the latest Setup EXE, choose the same edition and game folder, and
   select **Full install with recommended settings**. The old mod is no longer
   active, so setup can create a new original backup for this fresh install.
6. Confirm the new VR shortcut works with an offline character. Keep the
   archive, your separate save copy and the new setup backups until verified.

This procedure only prepares a fresh mod installation. If normal desktop Play
also fails, use your store's game-file repair and resolve that failure first.

## Upgrade or remove

**Installed with the EXE:** close D2R and fully exit Steam, then run the newer
Setup EXE and choose **Update / repair and keep calibration and video settings**.
You can also rerun the same version to repair it or change headset connections.
To add the other game edition, rerun Setup and select that copy.

Fresh and full installs use **100% resolution scale** starting with v0.3.10.
Update / repair preserves your existing scale; if an older install still uses
80%, set **Options > Video > Resolution Scale** to **100%** in the game.

To remove an EXE installation, open **Windows Settings → Apps → Installed apps →
D2R PCVR → Uninstall**, or the D2R PCVR Start menu uninstaller. This removes the
mod from **every Steam and Battle.net copy managed by that EXE**, restores its
tracked original files and removes matching VR shortcuts. Your current characters,
shared stash and video settings are kept. Backups and logs remain in the helper
folder; delete them yourself only after removal succeeds. If Steam is running or
required backups are missing, removal stops and keeps the helpers for recovery.
Use this Windows uninstaller for EXE installations; the ZIP's uninstaller handles
one edition at a time and does not unregister the EXE's helper installation.

**Installed with the portable ZIP:** follow the directions below.

**Upgrade while keeping your settings:** fully close D2R, download and extract
the new release to a permanent folder, run **Install-Windows.cmd**, select the
same game edition if prompted, and choose
**Update / repair and keep calibration and video settings**. Setup refreshes
the mod and its launch paths while keeping your calibration and video settings.

For a manual DLL-only upgrade, keep a copy of the existing
`d2rloader\plugins\d2rl-vrcam.dll` and `d2r_vr.ini` from your game folder.
Replace only that DLL with the new release's `bin\d2rl-vrcam.dll`.
Keep your existing INI to retain calibration. For the Steam edition, repeat
the Steam launch-option step using the new extracted folder. For Battle.net,
rerun the wizard and choose **Only configure Steam shortcuts** to update its paths.
Run the full installer if the new release
asks you to update its other components.

**Guided removal:** double-click **Uninstall-Windows.cmd** in the extracted folder.

1. Select **Steam** or **Battle.net** and confirm the game folder.
2. Setup follows the installation backups, including upgrades from older
   extracted release folders. If a backup was moved or an older installation
   has no record, choose the folder containing its original backup when prompted.
3. Keep current video settings, or choose to restore the pre-install settings.
   Both game editions share video settings, so keeping them is the default.
4. Review removal, then choose **Remove the VR mod**. Use **Save & Exit** in D2R
   and **Steam > Exit** when prompted.

The wizard restores replaced files and removes files introduced by the mod,
including files added by upgrades. It removes matching VR library entries
from local Steam accounts while keeping other games and the other edition's
shortcuts. For Steam, it restores the recorded prior launch options if the VR
wrapper is still active; any launch options you changed afterward are kept.
Battle.net removal does not edit the Steam edition's launch options.

**Your current characters and shared stash are never restored from an old
backup or deleted.** The original game installation is kept. Files changed
during removal are backed up under **setup-backups/uninstall-...**. The extracted
mod folder, installation backups and diagnostic logs remain until you remove
them yourself. If the original pre-mod backup is unavailable, guided removal
stops and lets you locate it or cancel.

Keep all referenced installation backups while using the mod, even after
upgrading to a new extracted folder. Rerunning the installer keeps the backup
history. The v0.3.7 ZIP includes the uninstaller. Older packages without it
can use the manual instructions below.

**Manual restore / removal:** fully close D2R. Open PowerShell in the extracted folder,
set `$Game` to your D2R folder as in the install step, then replace the backup
path below with the one the installer printed. Wizard backups are under
`setup-backups\game`; manual installer backups are under `build-frame\backups`.
The wizard's private Python can be used without installing Python separately:

Select the original pre-mod snapshot for removal. A later upgrade snapshot
restores the previous mod build. Use guided removal to include files added by
subsequent upgrades automatically.

```powershell
& .\.runtime\python\python.exe .\steamvr\install.py --game "$Game" --restore 'C:\Mods\d2r-pcvr-preview\setup-backups\game\YOUR-SNAPSHOT'
```

For a manual installation, use `py -3` in place of the private Python path.
Then remove that edition's VR shortcuts. For the Steam edition, also clear
D2R's Steam launch options; Battle.net setup does not change those options. To restore the
previous Steam configuration instead, fully exit Steam and copy the VDF files
present in the matching `setup-backups\steam-...` folder back to
`Steam\userdata\YOUR-LOCAL-ID\config` (the ID is in `setup-result.json`).
Battle.net setup backs up only `shortcuts.vdf` because it does not edit
`localconfig.vdf`; leave files that are not in the backup untouched.
If no `shortcuts.vdf` existed before setup, its backup is absent; remove only the
new D2R VR entries through Steam instead of replacing unrelated shortcuts.
Your current character progress is kept;
the manual restore restores replaced files and your previous video settings.

If guided removal reports a write failure, keep its **uninstall-...** backup.
It attempts to roll back changes immediately. Its `manifest.json` lists each
changed path and the corresponding previous file under `files` for recovery.

See [THIRD_PARTY.md](THIRD_PARTY.md) for licenses and credits.
