# Install and play D2R VR Unleashed on Windows

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
**Steam > Exit**, then click **Install**. Setup lets you retry if Steam is still open.
Exiting Steam temporarily disconnects headset streaming.

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

If setup fails, keep `setup-backups`. The helper folder contains
`setup-last.log` with the wizard output and `setup-install.log` with the mod
installer's full output. Share the error at the end of those logs when reporting
a problem; review them for personal paths before posting. Older installers may
only report an exit code in `setup-last.log` and do not create `setup-install.log`.

If Setup reports **Permission denied** for **launcher_icon.png**, fully close
**D2R VR Launcher** and any image viewer showing that file, then rerun Setup
with **Update / repair**. Keep Steam and the game closed while Setup runs.
The launcher can hold its displayed icon open, preventing an installer from
replacing it. This error alone does not mean you need administrator rights or
to change antivirus settings. Keep the installation backups when retrying.

If setup reports **Checksum mismatch**, the downloaded bytes do not match a
version approved by that installer. An upstream download can be replaced while
keeping the same filename; a damaged download can also cause this error. Get the
latest D2R PCVR installer and retry. If it persists, report the dependency URL
and the expected/received SHA-256 values from the log. Keep antivirus protection
enabled; disabling it does not resolve a changed upstream checksum. Setup stops
before changing game files when a dependency fails this check.

If a log says a required download cannot be read, or a browser reports a virus,
check **Windows Security > Virus & threat protection >
[Protection history](https://support.microsoft.com/en-us/windows/security/windows-security/protection-history-in-the-windows-security-app)**
(or your other antivirus application's history). Include the detection name,
affected file path and time with the error report, after removing personal details.
A readable dependency ZIP with the pinned SHA-256 is reused on retry; setup does
not deliberately delete it. A file disappearing or becoming unreadable needs
its security/file-access event checked. A checksum match establishes the expected
version, not that an antivirus detection is a false positive.

When Windows reports a confirmed security block, setup shows the affected file
and recovery steps. VR loaders hook into the game to enable mods, which can
trigger antivirus detections. Review the detection before continuing. If you
trust the file, allowing that specific detection keeps protection active for
other files.

If you choose to retry with protection temporarily off, open **Windows Security
> Virus & threat protection > Manage settings**, turn **Real-time protection**
Off, then rerun Setup and choose **Update / repair**. Turn protection back **On
immediately after the attempt, even if setup fails**. Only do this for a download
you trust: your PC is less protected while it is off. See Microsoft's
[real-time protection instructions](https://support.microsoft.com/en-us/windows/security/windows-security/stay-protected-with-the-windows-security-app).
Setup never changes antivirus settings and continues to enforce the approved
dependency checksums. Ordinary missing-file/permission errors do not trigger
this confirmed-block message.

You can rerun the EXE safely. It updates matching shortcuts without duplicates.
When upgrading a recorded ZIP installation, it copies and verifies its referenced
game backups into the permanent helper folder before updating the mod. Do not
delete the old extracted folder until Setup succeeds. If an older mod has an
incomplete installation record, setup checks known backup locations and the
old helper folder referenced by your VR shortcut. When it finds one verified
location for this game, it fills in the backup page automatically. Otherwise,
choose the snapshot folder or the folder containing snapshots inside the old
mod folder (`setup-backups\game` or `build-frame\backups`). Setup validates it
when you click **Next**. After copying the backups into the permanent helper
folder, it records their locations in this game's `d2rloader/pcvr-install.json`;
future setup runs can find them without asking again. Changing the selected
game clears the previous game's suggested folder. If the original backup is
unavailable, cancel and keep the current installation.

You do not need a `d2r-pcvr` folder under your Windows user folder. The backup
location depends on how you installed the earlier version:

- **Portable ZIP:** open the folder where you previously ran
  `Install-Windows.cmd`, then select `setup-backups\game` inside it. This is
  normally inside the extracted `d2r-pcvr-preview` folder, wherever you kept it.
- **Setup EXE:** paste `%LOCALAPPDATA%\Programs\D2R PCVR\setup-backups\game`
  into File Explorer's address bar to check the default helper location.
- **Manual/development install:** check `build-frame\backups` inside the old
  checkout, unless you selected a different backup location.

The game folder itself or a freshly extracted ZIP is not a replacement for an
original backup. If none of these locations contains it, cancel and report
which mod/version and installation method you used previously; an installation
made by another mod may not have created a compatible backup.

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

Required icons, potion models and Arcane sky artwork are read from your own Battle.net archives
and kept locally. No Blizzard game files or generated asset caches are included
in the mod package. The existing [controller mappings](CONTROLS.md) also apply.

Setup also prepares a surrounding starfield for Arcane Sanctuary. It uses the
game's star and cloud textures, mapped around the VR view instead of the native
flat weather cards. These generated textures and Arcane visual definitions stay
inside this mod's folder and are tracked for repair/removal. Existing custom
Arcane overrides are preserved. If preparation fails, setup retains the existing
background and reports the reason. This level backdrop is independent of the
added outdoor sky setting.

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
py -3 .\steamvr\install.py --game "$Game" --plugin .\bin\d2rl-pcvr.dll --openxr-loader .\bin\openxr_loader.dll --deps .\deps --download-deps
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

Setup checks Windows HDR on active desktop displays. When it is on, the wizard
shows an advisory before installation; the portable setup and logs show it too.
Players have reported VR visual problems that improve with HDR off. Open
**Windows Settings > System > Display > HDR**, select the relevant display and
turn **Use HDR** Off before playing. Setup can continue and does not change
display settings. If the query is unavailable, setup says it could not check;
HDR support alone does not trigger the enabled warning. This check covers the
current desktop display mode, not per-game Auto HDR or an in-game HDR toggle.

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

## Upgrade or remove

**Installed with the EXE:** close D2R and fully exit Steam, then run the newer
Setup EXE and choose **Update / repair and keep calibration and video settings**.
You can also rerun the same version to repair it or change headset connections.
To add the other game edition, rerun Setup and select that copy.

Fresh and full installs use **100% resolution scale** starting with v0.3.10.
Update / repair preserves your existing scale; if an older install still uses
80%, set **Options > Video > Resolution Scale** to **100%** in the game.

To remove an EXE installation, open **Windows Settings → Apps → Installed apps →
D2R VR Unleashed → Uninstall**, or the D2R VR Unleashed Start menu uninstaller. This removes the
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

**The D2RLoader mod D2R-PCVR (shown as D2R VR Unleashed):** everything of
this mod is in the game's `mods\D2R-PCVR` folder: the plugin
`mods\D2R-PCVR\d2rloader\plugins\d2rl-pcvr.dll` with its settings
`d2r_pcvr.ini` and its OpenXR loader beside it, its own ReShade and ReShade's
files in `mods\D2R-PCVR\reshade\`, and `mods\D2R-PCVR\D2R-PCVR.mpq\modinfo.json`
(`savepath "../"`: your usual characters). The VR shortcuts start
`D2RLoader.exe -mod D2R-PCVR`; D2RLoader loads a mod's plugin only while that mod
runs, so another VR mod in its own mod folder (D2R VR as `mods\D2R-VR`) can be
installed beside it and the shortcut you start picks the mod. Setup keeps an
existing `d2rloader\config\d2rloader.toml` (your D2RLoader settings and
`default_mod`) as it is. Removal takes this mod's own files; D2RLoader itself
stays while another mod uses it. An install of this release's first file names
in D2RLoader's global plugins folder (`d2rloader\plugins\d2rl-pcvr.dll`, which
would load with every mod) moves into the mod folder with your settings and
calibration on **Update / repair** (the old files stay in the setup backup).

**File names:** D2R PCVR is `d2rl-pcvr.dll` with its settings in `d2r_pcvr.ini`.
Releases up to 0.3.16 used `d2rl-vrcam.dll` and `d2r_vr.ini`, the names of
D2R VR, the mod D2R PCVR is based on. **Update / repair** carries your settings
and body calibration over to `d2r_pcvr.ini` and removes the old D2R PCVR files
(they stay in the setup backup). Setup recognises them by its own installation
record, not by the name: if D2R VR 0.167 or older is installed in the same game
folder (its plugin in D2RLoader's global `d2rloader\plugins`, loaded with every
mod), setup leaves D2R VR's files alone and stops, because D2RLoader would start
both mods. Update D2R VR to its D2RLoader-mod release (`mods\D2R-VR`) or remove
it with its own uninstaller before installing this one.

For a manual DLL-only upgrade, keep a copy of the existing
`mods\D2R-PCVR\d2rloader\plugins\d2rl-pcvr.dll` and `d2r_pcvr.ini` from your game folder.
Replace only that DLL with the new release's `bin\d2rl-pcvr.dll`.
Keep your existing INI to retain calibration. Coming from 0.3.16 or older, use
**Update / repair** once instead, so the old names are carried over. For the Steam edition, repeat
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
