# Linux installation notes

For the current Windows release, use [INSTALL-WINDOWS.md](INSTALL-WINDOWS.md).
These Linux/Proton instructions are retained for development. Earlier
Steam Frame testing encountered streaming resets and game-update stalls;
Linux/WiVRn and ALVR/Quest compatibility remain unverified. The current release
is packaged and tested on native Windows. Do not treat its Windows results as
a Linux headset test. Keep the extracted package because Steam uses its wrapper.

This is an **offline single-player** preview for the **Steam edition of
D2R**. Use your own purchased game and offline character. Refresh the
product license by launching the original game through Steam while connected
to the internet, then exit before installing. No game files or saves are shipped.

The installer prepares spell/item icons and all 15 original potion models from
your local Steam archives. These caches stay under `d2rloader/plugins/` and are
included in installation backup/restore. For an existing installation, with D2R
closed, run `python3 steamvr/prepare_potions.py --game "/path/to/Diablo II Resurrected"`
(Windows: `py -3` instead of `python3`) alongside the updated plugin. No Blender,
Rust, proprietary model SDK or network asset download is required. If preparation
fails, the mod logs the reason and retains flat potion previews.

**Historical Linux evidence:** Proton Experimental + SteamVR + Steam Frame
reached first-person gameplay, with severe streaming resets and separate
game-update stalls, including after baseline rollback and a PC reboot. Current
Windows/Quest verification is in [steamvr/QUEST_TESTING.md](steamvr/QUEST_TESTING.md).

## Linux + Quest 3 + WiVRn (friend's likely setup)

1. Install Steam, D2R, **Python 3.10+**, and Proton Experimental. Launch stock
   D2R once, finish account/license setup, create an offline character, and exit.
2. Install [WiVRn server and its Quest client](https://github.com/WiVRn/WiVRn#usage).
   Connect the Quest before launching the game. First verify another OpenXR game
   streams successfully. Follow the dashboard's Steam launch/environment guidance.
   WiVRn uses its own OpenXR runtime; Steam is the launcher and **SteamVR is not
   required for native OpenXR apps on this path**. Its Flatpak includes xrizer /
   OpenComposite for OpenVR compatibility.
3. From the extracted preview folder, install (replace the game path):

   ```sh
   python3 steamvr/install.py \
     --game "$HOME/.local/share/Steam/steamapps/common/Diablo II Resurrected" \
     --plugin bin/d2rl-pcvr.dll --deps deps --download-deps
   ```

   The installer downloads checksum-pinned D2RLoader 1.3.1-beta and ReShade
   6.8.0 with full add-on support from their official sites. On Linux it also
   downloads Microsoft's Windows SDK package (about 160 MB) and extracts its
   shader compiler, installed privately under the mod. This resolves ReShade
   compilation staying busy with Wine's compiler; the older Winetricks compiler
   lacks the shader target ReShade needs. It backs up saves,
   replaced files and video settings, and prints the backup location. It keeps
   the original executable and uses no automatically loaded `dxgi.dll`.
4. Run `chmod +x steamvr/launch.sh steamvr/run-proton.sh`. Set D2R's Steam
   Properties → General → Launch Options to the actual extracted path:

   ```text
   "/absolute/path/to/d2r-pcvr-preview/steamvr/launch.sh" %command%
   ```

   If WiVRn supplies a required environment prefix, place it **before** this
   wrapper; retain only one `%command%`. Have your agent use the dashboard's
   current instructions rather than copying a prefix from another machine.
5. With WiVRn connected, run `./steamvr/run-proton.sh` from the extracted folder.
   Choose **Play** with your offline character. Normal Steam Play remains stock;
   the separate command supplies `--frame-vr` to select D2RLoader.

   For a Steam-library launcher, choose **Add a Game → Add a Non-Steam Game →
   Browse**, select `steamvr/run-proton.sh`, and name the entry **Diablo II VR**.
   In its Properties, enable **Include in VR Library** if available. Leave
   compatibility forcing off for this shortcut: it is a Linux script that asks
   Steam to launch the purchased game using that game's Proton configuration.
   Connect the headset, then select **Diablo II VR → Play**. The original D2R
   library entry continues to launch the unmodded game for license refresh.

**For your agent:** WiVRn compatibility has not been established. Confirm the
active OpenXR runtime and Proton's WineOpenXR bridge. The Windows game needs
`XR_KHR_D3D12_enable`; the bridge must expose it over WiVRn's Vulkan runtime.
If the log reports a missing extension, stop and resolve the runtime/bridge
configuration. Do not remove the extension check or assume SteamVR settings apply.
Use [HANDOFF.md](HANDOFF.md) for code, diagnostics and current limitations.

For a SteamVR-based Linux route, [ALVR](https://github.com/alvr-org/ALVR) supports
Quest 3: connect through ALVR, start SteamVR and select SteamVR as the active
OpenXR runtime, then use the same mod install/launch steps. That headset path is
also untested by this project. Steam Frame uses Steam's PCVR connection directly.

## Controls and settings

If the image becomes blurry or ghosted after changing a graphics preset, check
**Options → Video → Anti-Aliasing** again. **FXAA**, **DLSS off** and **100%
resolution scale** restored clarity in the owner's October 9 headset test;
Very High with DLSS off alone did not restore the previous AA setting. Select
the quality preset first, then set FXAA. The current-source installer chooses
FXAA and 100% resolution scale; older packages used 80%.

Fresh installs from current source enable **motion controls and fixed foveation**,
with **eye-tracked foveation off**. The [motion gameplay prototype](steamvr/GAMEPLAY.md)
includes melee swings, bow draws, a hand palette and grab-to-mouth belt potions.
Bow drawing and free-swing melee have been tested on Steam Frame; other
interactions remain experimental. These features are included in the v0.2
handoff packages; the old v0.1 preview lacks them. Read the control changes
before playing. Set
`[gameplay] motion_controls=0` to use the legacy button controls.

**Keep the D2R window focused on the PC while playing.** Menu laser clicks and
dragging require desktop focus, even if the headset still displays the game.
After using another desktop app, select D2R again before interacting with menus.
Left X can open inventory without desktop focus, but laser clicks cannot.

| Control | Action |
|---|---|
| Left stick | Walk |
| Right stick left/right | Snap turn |
| Left grip + left stick click | Reset eye height |
| Left stick click alone | Map |
| Left X (Frame Left A) | Open tabbed game menu / close panels; Journal is available there |
| Left Grip + Y | VR settings, including repeat body calibration |
| Swap-hand trigger behind either shoulder | Swap weapon sets I/II; right hand by default |
| Right-hand cyan laser + right trigger | Click/drag on menu panels |
| Right B in a menu | Back / Escape |
| Right A in the world | Native interaction / loot target (hand-ray selection unfinished) |
| F11 | Full position, height and facing recenter |
| F4 | First person (native VR keeps this mode; legacy view shortcuts are ignored) |
| F8 | Diagnostic screenshot of both eyes; expect a brief capture pause |

The current-source installer selects 100% scene resolution, medium shadows/AO/atmospherics,
native height fog, VSync off, DLSS off and non-temporal anti-aliasing. Fixed
foveation uses a full-quality center and 2x2 peripheral shading on supported
D3D12 VRS hardware; unsupported devices stay at full quality. This default uses
a fixed center. To compare, set `[render] fixed_foveation=0` in
`GAME/mods/D2R-PCVR/d2rloader/plugins/d2r_pcvr.ini` (`d2rloader/plugins/d2r_vr.ini` up to 0.3.15). Settings reload live.

Current source also includes an **experimental Steam Frame eye-tracking mode**,
disabled by default and included in the v0.2 handoff packages. See
[the prototype settings and limitations](steamvr/README.md#eye-tracked-foveation-prototype) below
the fixed-foveation description. It requires actual OpenXR gaze input from the
connected headset. Keep it off for Quest 3; changing streaming software cannot
provide eye-tracking hardware that the headset does not have.

The installer leaves **DLSS off**, the current clarity baseline. The October 9
development fixes corrected washed-out colors/artifacts, then aligned the depth
range used by native rendering and particle effects. The owner confirmed that
**DLSS Quality looks clearer around smoke and effects** with the latest fix.
Earlier Balanced and Quality builds looked soft compared with native/FXAA;
overall clarity and stereo-motion reconstruction still need comparison with
the latest build. DLSS is not yet fully validated, and performance gains have
not been measured in a matched comparison.
The latest development build separates the correction
shader from diagnostics, reducing its isolated compilation from roughly 23 seconds
to 33 ms on the test PC. This startup improvement is not an FPS benchmark.

## Troubleshooting and removal

- **Square light patches that differ between eyes:** the latest development build
  enables `[render] symmetric_projection=1` in `d2r_pcvr.ini` (reloads live).
  This removed the reported waypoint patches in an initial Steam Frame test,
  with some image softness. Other scenes/headsets are unverified. Set 0 to
  restore the original projection; older preview binaries lack this option.
- **Creature labels at the wrong depth:** the latest build enables
  `[hud] monster_draw_order=1` to paint health/name boxes using the current eye's
  updated position. The owner verified this fix on Steam Frame. Older preview
  binaries lack it; check the release notes when downloading a build.
- **Different library/prefix or redirected saves:** pass `--game` and `--saves`
  explicitly. Linux defaults to the game's Steam library prefix, Windows to
  `%USERPROFILE%\Saved Games\Diablo II Resurrected`. Battle.net installations
  have not been adapted to these Steam launch wrappers.
- **30-day license warning:** exit fully and retry once. A failed Wine service
  startup has produced this warning even with a valid license; a clean restart
  recovered without refreshing it. If it persists, use normal Steam Play,
  without `--frame-vr`, to refresh the license online, then exit and try offline
  VR. Keep the same installation and Proton prefix. Do not replace the original
  executable. For diagnosis, save the Proton log before restarting and check
  for `Couldn't start services.exe` / `Failed to open service manager`.
- **No VR / flat view:** check the headset is connected and the correct OpenXR
  runtime is active. Read `GAME/d2rloader/logs/d2r-vr-frame.log`, the startup
  `d2r-pcvr-vrcam.log` (`d2r-vr-vrcam.log` up to 0.3.15), and `GAME/ReShade.log`. Linux Proton logs are in the
  extracted folder's `build-frame/`. Share log excerpts with your agent.
- **Menus:** click/focus the game window on the PC if pointer input is ignored.
- **ReShade stays “Compiling”:** reinstall with `--download-deps` using the
  current installer. Linux needs the pinned SDK compiler in
  `d2rloader/runtime/d3dcompiler_47.dll`; stock launch and prefix DLLs stay intact.
- **Remove:** save and exit D2R; run the command below with the backup path
  printed during installation. Use `py -3` instead of `python3` on Windows.
  This restores replaced files and video settings, preserving current character
  progress. Remove Steam's launch options afterward.

  ```sh
  python3 steamvr/install.py --game "/your/game/folder" --restore "build-frame/backups/YOUR-SNAPSHOT"
  ```

Own licenses and upstream attribution are retained. Dependencies are downloaded
from [D2RLoader](https://d2rloader.net/) and [ReShade](https://reshade.me/), rather
than bundled with the preview. See `THIRD_PARTY.md` and `LICENSE`.

## Native wheel icons

The installer prepares spell and inventory icons from **your own installed
Steam game**, on Linux or Windows. No additional packages are required beyond
Python 3.9+. The gothic wheel/wrist frames are embedded in the plugin. Spell
slots follow native controller assignments; equipment and belt icons follow
the current items, including potion tiers and recognized unique/set artwork.
Blank/unreadable slots retain text instead of a guessed icon. Affix color
transforms and charge/cooldown badges are not implemented.

After a game update, or if icons are missing, close D2R and rebuild the cache:

```bash
python3 steamvr/prepare_icons.py --game "/path/to/Diablo II Resurrected" --force
```

On Windows use `py -3` instead of `python3`, with the Windows game path. Run
this from the extracted release/repository folder. The output is
`d2rloader/plugins/d2r_vr_icons` under the game directory. Keep it local; do not
upload it with the mod. The reader supports Steam static-container installs;
other storage layouts fall back to wheel labels. Reinstalling through the
installer also regenerates the cache. Cache file reads/decoding run on a worker
at runtime and do not run on every frame. These installer changes are tested
with fixtures and the owner's Steam installation; native Windows and Quest
headset checks remain pending.
