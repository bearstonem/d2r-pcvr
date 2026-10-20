# D2R VR Unleashed v0.3.23 - Windows preview

This hotfix fixes ground-item names disappearing when you look down at loot,
including while holding Show Items. It includes the installer repair fix from
[v0.3.22](https://github.com/bearstonem/d2r-pcvr/releases/tag/v0.3.22-windows-preview)
and the rendering, shield combat,
directional sound and interface features from [v0.3.21](https://github.com/bearstonem/d2r-pcvr/releases/tag/v0.3.21-windows-preview).

## Controls and interface

- **Loot labels stay visible when looking down:** Item names can now use the
  full headset view instead of being cut off at the bottom of the game's menu
  area. This addresses labels disappearing over the item you are looking at,
  reported on Valve Index and Quest 3 through SteamVR, especially at the 2K
  render preset. Ordinary drop/hover labels and held Show Items use the same fix.

## Updating

Download **D2R-PCVR-Setup-0.3.23.exe** for graphical installation, or
**d2r-pcvr-windows.zip** for portable setup. Existing users should choose
**Update / repair and keep calibration and video settings**.

Close the game, Steam and D2R VR Launcher before updating. Existing settings,
calibration and saves are preserved. If an earlier installer stopped on
**Permission denied: launcher_icon.png**, rerun this version with **Update / repair**.
The unchanged-icon repair fix from v0.3.22 is included.

The Windows installer includes **D2R VR Launcher 0.3.5**. It installs or updates
it when needed and preserves newer versions. Direct Steam shortcuts and portable
setup remain available; Authentic is a separate optional mod.

### First-time setup and using the launcher

1. Install and start the original Diablo II: Resurrected through Steam or
   Battle.net once. Complete its account prompts, create or select an offline
   character, then fully exit the game.
2. Download **D2R-PCVR-Setup-0.3.23.exe** from the assets below. Fully exit
   Steam using **Steam > Exit**, then run Setup and choose **Full install with
   recommended settings**. The shared launcher is included; follow the wizard
   to select your game copy, headset and connection.
3. After Setup finishes, reopen Steam and reconnect your headset.
4. Open **Start > D2R VR Launcher**. Confirm the **Game folder** at the top
   (use **Browse** if needed) and select **D2R VR Unleashed**.
5. Choose **SteamVR** for Steam Frame or Quest through Steam Link/SteamVR.
   Choose **Virtual Desktop (VDXR)** for Quest through Virtual Desktop; its
   Quest app and Windows Streamer are required, with SteamVR closed. This choice
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
An older log under the game's root `d2rloader\logs` folder may remain from
before the mod-folder migration and will not contain current Unleashed sessions.
Installer failures are recorded separately in
`%LOCALAPPDATA%\Programs\D2R PCVR\setup-install.log`.

## Verification and known issues

The production build passed **114 native checks** and **223 Python checks**.
One additional Python symlink check was skipped because this Windows session
cannot create symbolic links. Installer regressions cover actual Windows
file-sharing locks, unchanged-icon repair, changed-icon preflight, retry after
releasing the lock, rollback and removal. The compiled installer lifecycle,
embedded payload, package contents and release checksums were checked separately.
The in-headset release page was rendered and visually inspected.

The loot-label fix was tested in the isolated developer environment on Quest 3
through SteamVR with Steam D2R 3.3.93787 at 4096 x 4096. A player also tested a
private build and confirmed the labels were fixed; their headset and resolution
were not reported back. The 2K preset and Index dimensions are covered by
automated checks but have not been retested in our own headset runs. VDXR and
Battle.net have not been tested for this correction. See v0.3.21's notes for
rendering tradeoffs and earlier feature verification. Battle.net remains experimental.
Multiplayer and the Sorceress elemental-hands prototype remain excluded.

## Special thanks

Thanks to everyone reporting the disappearing labels and helping test the fix!
Thanks also to **ChuckRibbits**, **beadlebytes** and **Sergious** for the combat,
audio, interface, rendering and shared launcher contributions included from v0.3.21.

D2R VR Unleashed is a mod by **monkeylashes**, with contributions from the community.

## Downloads and links

- [Windows installer, portable ZIP and checksums](https://github.com/bearstonem/d2r-pcvr/releases/tag/v0.3.23-windows-preview)
- [Installation instructions](https://github.com/bearstonem/d2r-pcvr/blob/v0.3.23-windows-preview/INSTALL-WINDOWS.md)
- [Controls](https://github.com/bearstonem/d2r-pcvr/blob/v0.3.23-windows-preview/CONTROLS.md)
- [Full source comparison](https://github.com/bearstonem/d2r-pcvr/compare/v0.3.22-windows-preview...v0.3.23-windows-preview)
- [Project on GitHub](https://github.com/bearstonem/d2r-pcvr)
- [Discord](https://discord.gg/DmbFfJAnnm)
- [Support](https://ko-fi.com/bearstonem)

<!-- PCVR_IN_HEADSET_NOTES
release: v0.3.23-windows-preview
credits: ChuckRibbits; beadlebytes; Sergious.
- Hotfix: loot labels stay visible when looking down, including with Show Items held.
- Included from v0.3.22: repair keeps an unchanged launcher icon in place.
- Included from v0.3.21: By turns, Both eyes and Replay rendering under Video.
- v0.3.21: faster wrist/wheel refreshes and steadier potions and game labels.
- v0.3.21: guard strain, chip damage, shield wear and improved parry timing.
- v0.3.21: adjustable low-life heartbeat and head-relative 3D sound.
- v0.3.21: optional enemy outlines under Aim, with strength and colors; default Off.
- Windows Setup includes the shared D2R VR Launcher 0.3.5.
PCVR_IN_HEADSET_NOTES -->
