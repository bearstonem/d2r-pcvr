# D2R VR Unleashed v0.3.24 - Windows preview

This update focuses on smoother rendering and settings, easier performance tuning,
movable menus and improved shield handling.

## Gameplay and combat

- **More natural shield wrists - contributed by ChuckRibbits:** Fixed the
  Necromancer's shield hand bending opposite to the controller. The wrist stop
  now follows the back of the hand consistently across classes.
- **Optional guard-strain vignette - contributed by ChuckRibbits:**
  **VR Settings > Combat > Guard strain color** now offers **White, Black or Off**.
  Off hides the effect; strain, guard breaks, chip damage and feedback still apply.
  Act bosses also correctly remain immune to late parries after a shield block.

## Controls and interface

- **Grab and position menus - contributed by beadlebytes:** Point the right
  laser at a menu, hold **Right Grip**, then move your hand toward or away from
  it to adjust its distance. VR Settings can also move sideways, up and down,
  and turn or tilt with your wrist. Placement is remembered for the session;
  grip-and-click still performs the ordinary Shift-click action.
- **Performance controls in one place:** Added **VR Settings > Perf**, bringing
  together stereo rendering, VR resolution, terrain and object distances,
  foveation, ReShade depth and the desktop mirror. Live frame rate and timing
  readouts help identify what to adjust. The page also shows the game's current
  graphics settings; change those under the game's **Options > Video**.
  ReShade depth changes require a restart. Release Notes is now the **Notes** tab.
- **Reliable menus and wrist maps at larger VR resolutions:** Fixed the laser
  failing to reach the right side of menus and the wrist minimap appearing empty
  when the headset render resolution exceeds the desktop game window.

## Visuals and stability

- **Cleaner By turns rendering:** Improved edges on moving heads, hands and
  thin weapons, and reduced ghosting around effects such as Tyrael's wings and
  weapon glows. **By turns** and **Both eyes** remain available under **Perf**.
  The experimental Replay mode has been removed; saved Replay selections switch
  to By turns.
- **Less work on the frame thread:** VR Settings and calibration pages now paint
  in the background, and By turns prepares its rendering passes in the background
  to reduce startup and graphics-change hitches. Crossbow and shield discovery
  also avoid repeated work while the game poses other characters and objects.
- **Lower default object distance:** Object draw distance now defaults to 210.
  Existing installs keep their current value; change it in **VR Settings > Perf**,
  or do a full install.

## Updating

Download **D2R-PCVR-Setup-0.3.24.exe** for graphical installation, or
**d2r-pcvr-windows.zip** for portable setup. Existing users should choose
**Update / repair and keep calibration and video settings**.

Close the game and fully exit Steam before updating. Closing D2R VR Launcher
also avoids file locks. Update / repair preserves settings, calibration and saves.
A full install applies recommended defaults and resets calibration.

The Windows installer includes **D2R VR Launcher 0.3.5**, installing or updating
it when needed while preserving newer versions. Direct Steam shortcuts and
portable setup remain available; Authentic is a separate optional mod.

### First-time setup and using the launcher

1. Install and start the original Diablo II: Resurrected through Steam or
   Battle.net once. Complete its account prompts, create or select an offline
   character, then fully exit the game.
2. Download **D2R-PCVR-Setup-0.3.24.exe** from the assets below. Fully exit
   Steam using **Steam > Exit**, then run Setup and choose **Full install with
   recommended settings**. Follow the wizard to select your game copy, headset
   and connection; the shared launcher is included.
3. After Setup finishes, reopen Steam and reconnect your headset.
4. Open **Start > D2R VR Launcher**, confirm the **Game folder** and select
   **D2R VR Unleashed**.
5. Choose **SteamVR** for Steam Frame or Quest through Steam Link/SteamVR.
   Choose **Virtual Desktop (VDXR)** for Quest through Virtual Desktop, with its
   Quest app and Windows Streamer running and SteamVR closed. The runtime choice
   applies to this launch without changing your Windows-wide OpenXR setting.
6. Click **Play**, select a **local offline character** and complete body
   calibration when prompted.

### Current log location

For support, reproduce the problem and send the current gameplay log from
inside your game folder:

```text
mods\D2R-PCVR\d2rloader\logs\d2r-vr-frame.log
```

Developer mode uses `mods\pcvr-dev\d2rloader\logs\d2r-vr-frame.log`.
Installer failures are recorded separately in
`%LOCALAPPDATA%\Programs\D2R PCVR\setup-install.log`.

## Verification and known issues

The production build passed **116 native checks** and **223 Python checks**.
One additional Python symlink check was skipped because this Windows session
cannot create symbolic links. The compiled installer lifecycle, embedded payload
and package checksums were checked separately. The in-headset release page was
also rendered and inspected for fit and content.

The merged features were tested with offline characters on Steam D2R 3.3.93787
through SteamVR in the isolated developer environment. Testing included By turns
effects at Tyrael, crossbow and shield behavior, menu interaction, larger VR
resolutions with a smaller desktop window, settings hover and performance tuning.
The menu contributor also tested repositioning on Quest 2 through SteamVR with
the Battle.net edition. Shield-wrist testing covered small shields on every class.
These are feature-level checks; no new headset session is claimed for the version
and release-notes update itself.

By turns still renders each eye on alternate frames. Transparent effects and
wispy hair without useful depth may retain artifacts; the effect fix has not
been verified on every model or with DLSS. The latest rendering and performance
changes have not been retested through VDXR or on other GPU vendors. Battle.net
remains experimental. Larger shields need further wrist-stop testing. Brief
game-side rebuild pauses after graphics or resolution changes can still occur.
Co-op setup/networking work and the Sorceress elemental-hands prototype are not
part of this release.

## Special thanks

Thanks to **beadlebytes** for movable menus and continued interface work,
**ChuckRibbits** for shield and guard improvements, and everyone testing,
reporting bugs and helping improve performance!

D2R VR Unleashed is a mod by **monkeylashes**, with contributions from the community.

## Downloads and links

- [Windows installer, portable ZIP and checksums](https://github.com/bearstonem/d2r-pcvr/releases/tag/v0.3.24-windows-preview)
- [Installation instructions](https://github.com/bearstonem/d2r-pcvr/blob/v0.3.24-windows-preview/INSTALL-WINDOWS.md)
- [Controls](https://github.com/bearstonem/d2r-pcvr/blob/v0.3.24-windows-preview/CONTROLS.md)
- [Full source comparison](https://github.com/bearstonem/d2r-pcvr/compare/v0.3.23-windows-preview...v0.3.24-windows-preview)
- [Project on GitHub](https://github.com/bearstonem/d2r-pcvr)
- [Discord](https://discord.gg/DmbFfJAnnm)
- [Support](https://ko-fi.com/bearstonem)

<!-- PCVR_IN_HEADSET_NOTES
release: v0.3.24-windows-preview
credits: beadlebytes; ChuckRibbits; Sergious.
- New Perf tab: rendering controls, live frame rate, timings and game graphics readout.
- Point and hold Right Grip to push/pull menus; VR Settings can also move and tilt.
- Cleaner By turns edges on heads, hands, thin weapons, wings and weapon glows.
- Smoother settings hover; background page painting and By turns startup preparation.
- Reduced crossbow/shield discovery work while characters and objects are posed.
- Larger VR resolution than the desktop window: full laser reach and wrist map fixed.
- Shield wrist follows correctly across classes, including the Necromancer.
- Combat: guard strain color now offers Off; act bosses cannot be late-parried.
- Objects default to 210. Existing values stay; change under Perf or do a full install.
- Stereo choices: By turns or Both eyes. Saved Replay selections become By turns.
- Windows Setup includes D2R VR Launcher 0.3.5; update preserves settings/calibration.
PCVR_IN_HEADSET_NOTES -->
