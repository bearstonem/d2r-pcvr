# D2R VR Unleashed v0.3.27 - Windows preview

This update makes the full map on your wrist fast, brings frame generation back
after By turns under Virtual Desktop and starts it without a game restart, and
treats trigger melee like a swing in Unleashed.

## Gameplay and combat

- **Trigger melee follows the Unleashed rules - contributed by ChuckRibbits:**
  A basic attack or melee skill on the trigger, and Smite, now counts as a stroke
  like a swing. Your attack rating scales its damage, and its other effects
  (procs, leech, critical strikes) happen only when the game's own hit roll would
  have landed. It shares your swings' recovery, so mixing trigger attacks with
  swings is never faster than swinging. Trigger attacks in a row are still paced
  only by their animation, and multi-hit skills such as Zeal keep every hit. Smite
  still always hits and is not scaled by attack rating. With a low attack rating,
  trigger attacks now deal less damage, on average what the game would deal with
  its misses.
- **Swing speed follows skill speed and slows - contributed by ChuckRibbits:** In
  human form, Fanaticism, Frenzy and Burst of Speed now shorten your swings'
  recovery, and chill and Decrepify lengthen it, as they already did in Werewolf
  and Werebear form.

## Controls and interface

- **The full map on your wrist keeps the frame rate:** With **Map view** set to
  Full map or Full top-down, looking at the wrist map no longer drops the frame
  rate. The search for your hero on the map ran on a single GPU thread and took
  over 20 ms of GPU time a frame; it now takes about 0.1 ms. The arrow keeps a steady size
  while you move.
- **The full map's arrow matches your hero - contributed by beadlebytes:** The
  heading arrow is sized from your hero's X at every game zoom. It no longer grows
  to several times its size next to water, as in the Rogue Encampment, or pulls
  the map off the hero.

## Visuals and stability

- **Frame generation starts when you turn it on:** Turning it on in **VR Settings
  > Perf** now restarts the VR session, a moment of black, and OFXR Bridge runs
  without restarting the game.
- **Frame generation with Virtual Desktop:**
  - Switching to By turns and back to Both eyes no longer leaves frame generation
    failed. If OFXR Bridge fails to resume, the VR session restarts once and it
    runs again.
  - The mod's menus and panels now bypass OFXR Bridge under Virtual Desktop, as
    they already did under SteamVR.
  - With Virtual Desktop's Synchronous Spacewarp (SSW) on as well, the game could
    draw only a quarter of the headset's frames. The Perf page now warns in amber
    when that happens. Set SSW to **Disabled** in Virtual Desktop when you use
    frame generation.

## Updating

Download **D2R-PCVR-Setup-0.3.27.exe** for graphical installation, or
**d2r-pcvr-windows.zip** for portable setup. Existing users should choose
**Update / repair and keep calibration and video settings**.

Close the game and fully exit Steam before updating. Closing D2R VR Launcher
also avoids file locks. Update / repair preserves settings, calibration and saves.
A full install applies recommended defaults and resets calibration.

On native Windows, Setup downloads OFXR Bridge for frame generation, so keep the
internet connection on. If that download fails, everything else still installs
and frame generation reports that it is not installed. Frame generation also
needs the Microsoft Visual C++ x64 runtime.

The Windows installer includes **D2R VR Launcher 0.3.5**, installing or updating
it when needed while preserving newer versions. Direct Steam shortcuts and
portable setup remain available; Authentic is a separate optional mod.

### First-time setup and using the launcher

1. Install and start the original Diablo II: Resurrected through Steam or
   Battle.net once. Complete its account prompts, create or select an offline
   character, then fully exit the game.
2. Download **D2R-PCVR-Setup-0.3.27.exe** from the assets below. Fully exit
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

For support, reproduce the problem, close the game and send these logs from
your game folder. In Steam's default library that is
`C:\Program Files (x86)\Steam\steamapps\common\Diablo II Resurrected`:

```text
mods\D2R-PCVR\d2rloader\logs\d2r-vr-frame.log
mods\D2R-PCVR\d2rloader\logs\d2r-pcvr-vrcam.log
mods\D2R-PCVR\reshade\ReShade.log
d2rloader\logs\d2rloader.log
```

The `d2r-vr-frame.log` and `d2r-vr-vrcam.log` in the game's main
`d2rloader\logs` folder are left over from older versions and are no longer
updated. Developer mode uses `mods\pcvr-dev` in place of `mods\D2R-PCVR`.
Installer failures are recorded separately in
`%LOCALAPPDATA%\Programs\D2R PCVR\setup-install.log`.

## Verification and known issues

The release build passed **142 native checks**. **250 of 255 Python checks**
passed on Windows: the 4 session checks need Linux's `/proc`, and 1 symbolic-link
check was skipped because this Windows session cannot create symbolic links. The
in-headset Notes pages were rendered and inspected for fit and content.

Tested in a headset with offline characters on Steam D2R 3.3.94995:
- **The full map's frame rate:** tested on a Steam Frame through SteamVR at
  3072x3072, with Full top-down and rotation on. With the map up, Both eyes went
  from 25 to 45 frame pairs a second, and By turns from 39 to 69-74 frames a
  second (88-89 with the map down). The arrow kept a steady size while moving.
- **Turning frame generation on while playing:** tested through SteamVR. It ran
  about 5 seconds after Apply.
- **Virtual Desktop:** the problems above were reproduced with Virtual Desktop on
  v0.3.26, and found in a tester's logs with an RTX 4090. With SSW Disabled, frame
  generation held 44-45 real frames a second for the whole run. The fixes
  themselves have not been retested in a headset yet.

Contributed features were tested by their authors before review:
- the arrow size, by beadlebytes, on a Quest 3 through SteamVR (Battle.net
  edition) at every game zoom, but at only one map size;
- trigger melee and swing speed, by ChuckRibbits, with a Paladin on v0.3.25. Not
  yet covered: dual wield, Druid forms on the trigger, Bash, Frenzy, Burst of
  Speed, Decrepify and a low attack rating.

Known issues:
- **Virtual Desktop freezes:** the game freezes for about a tenth of a second
  every so often under Virtual Desktop, with or without frame generation. This is
  being investigated.
- **By turns with the full map:** it still runs about 15 frames a second below
  the map-down rate while the full map is shown.
- **Stuns in Unleashed:** a stun now only knocks you back. You can move again as
  soon as the knockback ends, even while the stun is still shown.
- **Frame generation is experimental.** It uses a pre-release bridge. Expect
  artifacts around moving monsters and during fast head turns. Frame counters,
  including the Perf page's, count only the frames the game draws. Once, with it
  on, the Escape menu fell back to the flat panel.
- **Throwing potions:** the burst follows the game's short throw animation, and
  only right-hand potions are thrown by hand for now.
- **By turns:** it still renders each eye on alternate frames. Transparent
  effects and wispy hair without useful depth may keep artifacts.
- **Sorceress elemental hands:** they are drawn over the picture, so they can show
  through your weapon or a wall.
- **Not retested:** the latest rendering changes have not been retested on other
  GPU vendors. Battle.net remains experimental.

## Special thanks

Thanks to **beadlebytes** for smarter aiming, ground skills, the bow path laser,
the forearm and shield, wrist map zoom, centered auras and the full map's arrow.
Thanks to **ChuckRibbits** for Druid shapeshifting, the strike framework,
Unleashed combat, the knockback fix and trigger melee. Thanks to **Mcklem** for the
mercenary, enemy sight, torches and the Kurast Docks fix. Thanks to
**tig3rmast3r** for OFXR Bridge, and to everyone testing, reporting bugs and
helping improve the mod!

D2R VR Unleashed is a mod by **monkeylashes**, with contributions from the community.

## Downloads and links

- [Windows installer, portable ZIP and checksums](https://github.com/bearstonem/d2r-pcvr/releases/tag/v0.3.27-windows-preview)
- [Installation instructions](https://github.com/bearstonem/d2r-pcvr/blob/v0.3.27-windows-preview/INSTALL-WINDOWS.md)
- [Controls](https://github.com/bearstonem/d2r-pcvr/blob/v0.3.27-windows-preview/CONTROLS.md)
- [Full source comparison](https://github.com/bearstonem/d2r-pcvr/compare/v0.3.26-windows-preview...v0.3.27-windows-preview)
- [Project on GitHub](https://github.com/bearstonem/d2r-pcvr)
- [Discord](https://discord.gg/DmbFfJAnnm)
- [Support](https://ko-fi.com/bearstonem)

<!-- PCVR_IN_HEADSET_NOTES
release: v0.3.27-windows-preview
credits: ChuckRibbits; beadlebytes; Mcklem; tig3rmast3r (OFXR Bridge); Sergious.
- Wrist full map: looking at it no longer drops the frame rate (Full map, Full top-down).
- The full map's arrow matches your hero's X at every zoom, even next to water.
- Trigger melee and Smite count as strokes: attack rating, hit-roll effects, shared recovery.
- Swing speed follows Fanaticism, Frenzy, Burst of Speed and slows such as chill.
- Frame generation starts when you turn it on: the VR session restarts, no game restart.
- Virtual Desktop: frame generation comes back after By turns; panels bypass OFXR Bridge.
- Perf warns when Virtual Desktop's SSW halves frame generation again: set SSW to Disabled.
PCVR_IN_HEADSET_NOTES -->
