# Native OpenXR PCVR — technical notes

For opt-in offline developer tools and the `--frame-dev` launch flag, see
[developer mode](DEVELOPMENT_SCENARIOS.md).

For the compiled preview and portable friend setup, start with
[INSTALL.md](../INSTALL.md). These notes retain the owner machine's build and
Steam Frame testing details. Native Windows + SteamVR/Steam Frame is now tested
with offline gameplay. The owner also confirms Quest 3 with Steam Link/SteamVR
works; calibration, melee contacts and normal exit are logged. Quest 3 with
Virtual Desktop/VDXR now has owner-confirmed normal colors and logged outdoor
melee after the sRGB correction. Physical Quest 2 remains unverified.
Work continues on `vr/quest-2-3-support`; see
[QUEST_TESTING.md](QUEST_TESTING.md). WiVRn remains unverified.

This branch adds a native OpenXR transport to the MIT-licensed
[D2R VR camera mod](https://github.com/natpoh/diablo2-vr-port). It reuses its
first-person camera, body, controller aim, and two scene draws per game update.
The transport submits those separate eye images to SteamVR through native
Windows OpenXR, or Proton's WineOpenXR D3D12 bridge on Linux. BodyWalk and
FlatVR are not required by this build.

## Current verification

- Quest 3 + Steam Link + SteamVR: owner reports "ok it all works". The unchanged
  verified DLL passes startup/tracking and logged calibration/outdoor melee/exit.
  Virtual Desktop/VDXR also reaches tracked gameplay, with owner-confirmed
  corrected colors, 45 accepted outdoor melee reports and normal exit. The final
  release removes Journal's duplicate wheel entry; native tabbed access remains.
  Its generated preview passes; a fresh headset run of that final cleanup and
  physical Quest 2 verification remain pending.

- Native Windows build passes 34 C++ tests. Owner reports excellent performance;
  a controlled native baseline/candidate timing comparison remains pending.
  Current setup, installed DLL and unresolved visual/transition bugs are in
  [WINDOWS_HANDOFF.md](../WINDOWS_HANDOFF.md).
- Cave melee candidate corrects terrain elevation shear between native grid and
  rendered VR coordinates for the player guard and enemy contact volumes. Captured
  cave regressions and teleport/miss checks pass. The owner reports "it all works"
  after the offline retest; logs confirm native cave contacts and normal exit.
- First-load and settings-menu T-pose calibration save standing eye height and
  controller span, fitting per-rig arm proportions with physical controller
  scale. Dominant-hand grip over either shoulder requests the opposite native
  weapon set; hip anchors are removed. See [BODY_CALIBRATION.md](BODY_CALIBRATION.md).
  Automated checks pass. Calibration save/reopen is explicitly owner-verified;
  the later combined offline retest received an overall "it all works" report.
- SteamVR native D3D12 swapchain images are typeless. Potion models and native
  UI conversion use explicit typed views matching the selected swapchain format;
  typed/typeless GPU readback and potion stereo tests pass, including sRGB.
- Installer camera settings are verified with the Win32 profile API. A malformed
  duplicate camera section previously selected a large default near plane;
  correcting it restored torso, arms and close shield rendering in the headset.
- Cross-compiles with Clang 21 and MinGW headers/static libraries.
- Camera math checks cover asymmetric eye FOV, physical eye separation, view
  orientation, and the game's reverse-Z projection.
- The plugin's camera, input and stereo hooks load with Steam D2R **3.3.93787**.
- Fixed MinGW/MSVC aggregate-return ABI differences in the vendored ReShade
  interfaces; the title screen now runs without the earlier callback crashes.
- The independent `openxr-probe.exe` successfully negotiates WineOpenXR,
  creates a D3D12-enabled instance, identifies `SteamVR/OpenXR`, finds Frame,
  and creates a matching D3D12 device, queue, and OpenXR session.
- The offline character screen was reached twice with Proton's `S:` game-drive
  mapping. The warning returned on a launch where that mapping was absent;
  the wrapper now explicitly supplies the library/install paths to keep it stable.
- Game-owned D3D12 session/swapchain creation and image copies succeed. SteamVR
  exposes an SRGB format for this SDR backbuffer; the transport selects that
  compatible format while preserving the game image bytes. A headset capture
  confirms the title screen on a world-anchored panel.
- Offline first-person gameplay is running on Frame. The user confirmed it works;
  a headset capture shows Nekro's hands and weapon in the Rogue Encampment.
- Height calibration checks cover seated height, continued crouching, unchanged
  horizontal position and eye separation, and keeping calibration clicks out of
  both the map action and the emulated gamepad.
- A crash in `probe::SafeCopy` during the background hero-facing scan was traced
  to an inaccessible page. The native branch uses checked `ReadProcessMemory`
  reads there; its Proton test rejects inaccessible, freed and null memory.
- Menu hit tests cover panel edges, misses, rear-facing rays and rotated panels.
- A second scan crash was traced to `gamefog::ScanThread`. The native scan now
  searches bounded snapshots and uses checked writes, rather than walking live
  pages directly. Checked reads and writes pass the Proton memory test.
- Player-class skeletons are filtered by camera-target proximity and original
  standing head height before they can control the hands, facing or body. The
  corpse-at-respawn case is included in the selection checks. This is a spatial
  and pose filter, not an authoritative unit-ID binding.
- Launcher checks cover ordinary exit codes, cleaning helpers after game exit,
  and leaving unrelated processes running.

## Run

Keep Steam and SteamVR running, with Frame connected for PC streaming.
Set this game's Steam Properties → General → Launch Options to:

```text
"/home/berkybear/diablo2-vr-port/steamvr/launch.sh" %command%
```

The normal Steam **Play** button still starts the original, unmodded `D2R.exe`.
To explicitly launch the offline VR prototype:

```sh
./steamvr/run-proton.sh
```

This runs `steam -applaunch 2536520 --frame-vr`. The wrapper consumes that
argument, replaces only the game executable with `D2RLoader.exe`, and preserves
Steam's runtime, Proton, prefix, launch arguments, and authentication context.
For VR it also imports the host OpenXR runtime into the Steam Linux Runtime.
Use the offline character **Nekro** for game testing.

Main menus and in-game panels use a world-anchored display. Aim the right
controller's cyan laser and use **right trigger** to click or drag. **Left Grip
+ Right Trigger** sends a context click. Sticks or face buttons select native
controller navigation; after releasing them, deliberately re-aim the laser to
return to pointer control. **Right B** follows the native back action.
**Keep the D2R window focused on the PC.** Mouse input is sent only
while that window has focus; after using another desktop app, select D2R again
before clicking or dragging in menus. Opening inventory with Left X does not
guarantee desktop focus. Gameplay
uses separate projection views with the headset's
actual eye positions and FOV. Controller actions feed the existing game-side
input bridge. Native VR stays in first person and F11 recenters the camera.
The legacy F1–F3/F12 view shortcuts cannot switch native VR to an incompatible
camera. Old saved VR modes are repaired to first person on load. Flat monitor
mode and the separate FlatVR build retain their view selection.

The desktop game window currently presents the alternating eye frames, so it
can visibly flicker even when the headset stereo is correct. It does not render
a third scene for the monitor. Desktop presentation still has overhead;
suppressing it safely is an open performance task. Do not minimize the game as
a workaround: background/focus handling also affects input and frame pacing.

VR Settings > Perf > **Stereo rendering** chooses how the two eyes of each
headset frame are drawn:

- **Both eyes** draws the left eye, then the right with the game time held.
- **By turns**, the default, draws one eye per game frame, alternating. The headset gets that
  eye fresh and the other eye's last image turned on the GPU from the pose it
  was drawn at to the current one, head, stick and mouse turns included, and
  moved with the scene depth it was drawn with: a grid over its image, each
  point put back at its distance and seen from the current eye, nearer points
  over farther ones, so both eyes go at one pose as a pair does. This halves
  the draw work per headset frame. Each eye is still drawn at half the frame
  rate, so over that the drawn eye's picture of the same frame is laid,
  moved across with its own depth: monsters and missiles stand where the
  drawn eye shows them instead of a game frame back. Along an edge each pixel
  goes with its own depth, so a head's outline or a thin blade stays whole.
  Where the game's effects are - spells, flames, Tyrael's wings, which it
  draws into a layer of their own with no depth - the other eye's own older
  image, moved with its own depth, stays where it shows the same surface as
  the drawn eye's: moved across from the drawn eye they stood where that eye
  saw them, the scene showing through; from the eye's own image they stand
  where that eye sees them, a frame old. Elsewhere, and where the two part
  (what moved since), the drawn eye's picture goes, so the rest of the scene
  is of one moment in both eyes. An effect counts from
  `[stereo] turns_effects_level` (0.02, the layer's strongest channel), kept
  within `turns_effects_reach` pixels of it (32). Anything else drawn
  without depth and not in that layer (the game's bloom) can be kept round
  near things too - nearer than what is round them by more than
  `turns_keep_gap` pixels of parallax (48), within `turns_keep_reach` pixels
  of them (96). The pixel figures are of a 4096-wide eye.
  `turns_keep_own` (live; in developer mode also
  DEV > Equipment > By turns own picture) is 3 for the effects (the
  default), 1 round near things, 4 both, 2 wherever the own image shows the
  same surface and 0 for the drawn eye's picture wherever it reaches;
  `turns_from_drawn_eye=0` (live) keeps the other eye's own image alone.
  The belt's potion models, drawn by the mod over the game's picture,
  are drawn fresh in that eye at its own pose. The game's interface (labels
  and health bars over monsters, names, chat) is taken out of each eye's
  picture before it is kept and laid back over it as the game lays it; the
  eye not drawn gets its own interface from its last pass, turned to the
  current head, so each label stays at its own depth instead of moving with
  the scene behind it (moved with it, nameplates were cut along the
  monster's edge and flickered). A label over a monster that moves, or
  while the hero runs, is a pass old in that eye. Edges where something
  near uncovers what is behind it stretch for that frame; transparent
  effects move as the scene behind them. Within arm's reach (nearer than 1.0 m along the
  view, blended out by 1.6 m) points move with the hero instead of the world:
  the hands, the weapon and the body stay with the eye while running, where
  moved as the world they trailed a frame behind and smeared; a monster that
  close moves with the hero for that frame. The depth is the same copy the
  added fog reads (DLSS's corner included). Until both eyes have one, a pair
  is drawn from the hero's first step to 0.3 s after the last.
  The pictures turned, moved and laid over the eye not drawn land between
  texels nearly always; read bilinear, a half-texel step averaged two and
  every other frame in each eye was softer than Both eyes. They are read
  Catmull-Rom instead (`[stereo] turns_sharp=1`, live; in developer mode also
  DEV > Equipment > By turns filter), kept within the four nearest texels so
  it adds no ring or halo: at half a texel, stripes 4 px apart keep about a
  third more contrast, and a texel's centre reads exactly that texel. On an
  RTX 5090 it costs no measurable GPU time (the eye not drawn 0.55 ms at
  3072 square, 0.97 ms at 4096, either way). 0 reads bilinear.
  Beside anything near, the eye not drawn sees a strip the drawn eye cannot,
  and keeps its own picture from a frame back there; a monster that stood
  there a frame ago and has walked on was left in it, a ghost beside it in
  one eye. Such a strip is always of what is behind (the drawn eye's view of
  it is blocked by something nearer), so where the eye's own picture there is
  nearer than the farther end of the strip along the row by more than 2 px of
  parallax, the picture beside that end goes there instead; where it shows
  what is behind, its own texture stays (`[stereo] turns_fill=1`, live; in
  developer mode also DEV > Equipment > By turns ghosts; 0 as before). The
  ends are looked for up to 128 px of a 4096-wide eye either side. On an RTX
  5090 it adds 0.1 ms at 3072 square and 0.23 ms at 4096.

By turns draws a pair on a snap turn, outside first person, in open menus,
during stereo captures and on any frame without a reusable image for both eyes.
With `[render] desktop_one_eye` (On by default), the kept left image covers the
right frames on the monitor.

Replay (both eyes from one CPU pass, adapted from upstream) was a third
choice until 2026-10-20. On the owner's PC it halved the CPU work a frame but
not the frame time, as the right eye's GPU work waits for the whole left pass,
and it ran at 45 Hz like Both eyes while By turns ran 74-90
(`PERFORMANCE.md`). A profile saved with it now goes by turns; its code
(`vr/stereo_replay.cpp`) is no longer registered.

Every choice needs `[render] defer_gpu_wait=1` for its full rate; the
installer sets it for new installs, and an upgrade keeps the existing value.
With the conservative wait each frame takes the CPU's time plus the GPU's,
and at 90 Hz SteamVR then paces the game at half (By turns at 3128 square:
43-46 frames/s, against 86-88 with the wait deferred). While the wait is not
deferred, the Perf page shows a warning and a **Defer GPU wait** button that
turns it on (Apply saves it; it applies live), and the console says so once.
The Perf page also warns while the game's Vertical Sync is on or its
anti-aliasing is anything but FXAA (the game's Options > Video). It reads the
mod's own game settings, `Saved Games\Diablo II Resurrected\mods\<mod>\Settings.json`,
which the game keeps apart from the unmodded game's
`Saved Games\Diablo II Resurrected\Settings.json`.

The Perf page holds every setting that changes the frame rate, and under them
the last second's frames as the render thread measured them: frames per
second, the game's drawing (one eye by turns, both with Both eyes), its update,
the mod's own work and the wait for the headset. It says whether the CPU's part
fits the headset's frame (11.1 ms at 90 Hz); past it, late frames wait a whole
refresh. Drawing grows with what is in view, the draw distances and the game's
shadows, and hardly with resolution. The game's own graphics options are shown
as it last saved them; change those in its Options > Video.

VR Settings > Perf > **Frame generation** (experimental, Off by default) runs
[OFXR Bridge](https://github.com/tig3rmast3r/OFXR-Bridge) by tig3rmast3r
(LGPL-3.0-or-later), an OpenXR API layer that adds a frame made by optical flow
between two the game draws. On native Windows, Setup downloads its pinned
0.2.1 release (build V116; the SHA-256 is in `install.py`) and installs the
layer, its licenses, a layer manifest and `ofxr_bridge.ini` in
`mods\D2R-PCVR\d2rloader\plugins\ofxr`. Its tray program is not installed and
nothing is registered: while the setting is on, the mod adds that folder to
`XR_API_LAYER_PATH` for its own process and names the layer in
`xrCreateInstance` (`steamvr/frame_generation.h`). Turning it on or off takes a
restart; Off while it is loaded asks it to step aside at once. FidelityFX runs
on any graphics card; NVIDIA uses the optical flow hardware of RTX and GTX 1660
cards, with its quality (Slow, Medium, Fast) and scale (100%, 75%, 50%) on a
row under it, sent to the loaded layer live through its control API. The Perf
page's last line and the log say whether it is off, waiting for a restart,
active, bypassed or failed. While it runs it doubles the frame time the game
is given, so the game draws every other headset frame (45 of 90) and the
bridge makes the rest: Both eyes then shows each eye half its frames for real,
By turns only a quarter, and the Perf line suggests Both eyes while By turns
is on. The bridge takes over every color swapchain whose images are listed
through it (three more swapchains and an optical flow pipeline each); with the
mod's panels SteamVR ran out of swapchains, and menus fell back to the flat
panel. While it is loaded the mod lists the panels' images through the
runtime's own `xrEnumerateSwapchainImages`, below the bridge, so it takes over
the two eyes only; the log says so, or why not. If the loader does not list the layer (a game run
as administrator ignores `XR_API_LAYER_PATH`) or the instance fails with it,
the game starts without it; Proton's WineOpenXR has no API layers. The
generated frames come from color alone, so moving things and head turns can
show artifacts, and the Perf page's frame rate counts the frames the game
draws, not the generated ones. The layer needs the Microsoft Visual C++ x64
runtime. Its settings are `[render] frame_generation` (0 Off, 1 FidelityFX,
2 NVIDIA, 3 Game motion), `frame_generation_preset` (0 Slow, 1 Medium, 2 Fast),
`frame_generation_scale` (0 100%, 1 75%, 2 50%) and
`frame_generation_monsters` (1).

**Game motion** (#308 step 3, `steamvr/frame_motion.h`) gives the bridge
the mod's own motion vectors through its DLSS-style input
(`OFXR_PublishDlssMotionVectorsV1`) instead of its optical flow. For every
pixel of an eye's image they say where that point was in the same eye's image
a frame back:
- From the scene's depth and where the eye stood now and a frame back. That is
  exact for everything standing still, through head turns, walking and the
  body's turn.
- Within a metre and a half, what the hero carries goes along with the hero's
  step and turn, as By turns moves it.
- What moves by itself comes from the game's own motion vectors
  (`motion_vectors.cpp`, the target DLSS gets), less the camera's part against
  the render before (the other eye's), twice over. As in By turns' trial
  (#281), only from 2 m out, where a surface's texels agree, above a pixel and a
  half, and only while the head turned less than a quarter of a degree and the
  hero moved less than half a centimetre between renders. Otherwise monsters
  blend.

Each eye's vectors (half the depth's size, R16G16 uv) are drawn as its image is
copied. They are published just before that eye's image is released: the
bridge takes the newest publication for an image of one layer. It copies them
through a list of the mod's that runs before the frame ends. Without a depth
or a frame back the vectors are none, so the bridge turns the image by the
poses alone. Game motion needs Both eyes; By turns, the bridge is asked for
FidelityFX's flow instead, live. The log says every 10 s how many eyes were
drawn and published, and what the bridge did with them (published, matched,
used, its status).

`[render] reshade_depth` in `d2r_pcvr.ini` (not in VR Settings) switches
ReShade's Generic Depth add-on, which looks at every draw call to find the
depth buffer; in VR the fog and sky read the mod's own depth (on the flat menu
panel and outside VR they need ReShade's). The plugin writes it into
ReShade.ini's `[ADDON] DisabledAddons` before ReShade loads, so a change
applies after a restart; without the key ReShade.ini is left as it is.

The choice is stored as `[stereo] rendering` (0 Both eyes, 1 By turns,
2 Replay; missing or invalid is By turns) and applies live. Every 10 seconds the log reports a
`vrcam: eyes by turns` line with frames per second, the time spent in each part
of a frame, how many other eyes were moved with depth or only turned, and how
many frames went as pairs while moving.

**Left X** opens inventory through the game's UI service when no menu is open.
When native menu panels are open, Left X performs the native X action. It
preserves the HUD and map. The native inventory action requires headset focus, so another active
desktop window does not swallow Left X. Explicit menu actions and laser clicks
try to bring the game window forward; OS mouse/keyboard injection still requires
desktop focus. Hovering the laser does not steal desktop focus or an active
native controller selection. The controller menu button pulses Escape during
gameplay and forwards native Start in menus. Menu reports carry a context tag
and UI epoch in BodyWalk's reserved fields; stale world/menu reports are
rejected before semantic actions or camera-relative movement can run. Held
controls must return to neutral across transitions. Full player mappings are
in [CONTROLS.md](../CONTROLS.md).

The [motion gameplay prototype](GAMEPLAY.md) adds right-handed melee
swings, two-hand bow draws, a left-hand selection palette, aimed native skills
and grab-to-mouth belt potions. Fresh installs set `[gameplay] motion_controls=1`,
`[render] fixed_foveation=1` and `eye_tracked_foveation=0`. Set motion controls to
`0` for legacy button controls. See that document for cancellation rules and the distinction
between gesture-triggered native attacks and physical collision combat.

Hold **left grip**, then **click the left stick** to open or close the native
inventory/character/skills menu. This works even when the runtime reserves the
controller's menu button. A plain stick click still toggles that hand's wheel
with motion controls enabled. Release the stick before another menu toggle.
Reset eye height using **VR Settings > Reset eye height**. F11 also resets height
as part of its full camera recenter, including horizontal position and facing.

Each saved-game load calibrates from the current headset position at the first
tracked, focused game frame, then recenters facing. Movement during launch is
not carried into the player's body offset. Opening and closing a dialogue or
inventory does not repeat this calibration. Dialogue panels disable the game's
eye alternation and retain its original camera/projection, giving both eyes the
same stable image on the panel.

After the game process exits, the VR launcher gives Proton three seconds to
finish, then closes helpers belonging to that launch if they are still keeping
Steam's session open. It does not terminate a running game. Normal Steam Play
still runs without this wrapper's VR session monitor.

The installer renders the scene at 100% resolution scale and uses medium
shadows, ambient occlusion, and atmospherics. Update / repair preserves existing
video settings; older installs may still need their scene scale changed to 100%.
Textures stay at their original quality. Both eyes have their own copy command
list, with one GPU completion wait after the pair instead of between the draws.
Native height fog is retained, avoiding the mod's repeated full-memory scans
after area changes. This is a performance baseline and can change atmosphere.
Hitches over 80 ms log game gap, XR begin/wait, both eye draws, XR end/GPU fence
and pipeline creation timing; menu intervals are excluded from gameplay samples.
The routine ten-second `Frame pacing` line also reports the runtime's predicted
application rate and mean XR begin/wait, end/submit and total GPU fence times. Read it
alongside `a pair, ms` and `pairs/s` to distinguish drawing from synchronization.
The predicted application rate can differ from the panel refresh under throttling.
Pipeline creation timing is a clue to compilation costs, not a complete shader
profiler: work deferred until a draw or performed outside ReShade callbacks may
not appear in those creation measurements.

Fixed foveation uses D3D12 Tier 2 VRS with a separate mask for each eye's
asymmetric optical center: 1x1 in the protected center, coarser shading in the
periphery. It applies to single-sample opaque world draws that test scene depth,
leaving transparent passes, menus, labels, post effects and shadow-only passes
at full quality. D2R draws each opaque object twice: a pass that writes depth,
then the pass that shades the visible color with depth testing and no depth
writes. Earlier builds required depth writes, so only the first pass was
reduced and foveation had no visible or GPU effect. Unsupported devices/command
lists fall back to full quality. Immutable masks are uploaded once per viewport;
routine draws do not upload or wait for them.

VR Settings > Perf > **Foveation strength** (available when foveated rendering
is Fixed or Eye tracked) chooses how much detail is reduced:

| Strength | Fixed protected radius | Gaze protected radius | Periphery |
| --- | --- | --- | --- |
| Low | 0.70 | 0.45 | 2x2 |
| Medium (default) | 0.55 | 0.35 | 2x2 |
| High | 0.55 | 0.35 | 4x4 |
| Maximum | 0.35 | 0.30 | 4x4 |

4x4 falls back to 2x2 on GPUs without additional shading rates. A saved
`[render] foveation_strength` (0–3) selects these presets. Profiles without it
keep their `foveation_inner` (default 0.55, range 0.35–0.85) and
`gaze_foveation_inner` radii at 2x2 until a strength is chosen. `[render]
fixed_foveation=0` disables foveation for comparison. Settings reload live.
Logs report mask creation, reduced-shading draw counts and a `Frame foveation
coverage` line that counts full-size scene draws by the reason they stayed at
full quality.

Foveation reduces GPU shading only; it does not reduce the game's CPU work.
On a CPU-limited system the frame rate does not change. Steam Frame's own
eye-tracked foveated streaming already lowers peripheral detail in the stream,
which hides most of this effect in that headset. Compare captured eye images
(`[debug] capture_pair`) or a headset without streaming foveation when judging
visual impact. In the owner's Quest 3 / Virtual Desktop test, Maximum was
visible at the edges and Medium was not noticeable.

### Eye-tracked foveation prototype

An opt-in eye-tracked VRS prototype is available in current source builds:
set `[render] eye_tracked_foveation=1` **before launching** and leave
`fixed_foveation=1` (the VRS master switch). This requests
`XR_EXT_eye_gaze_interaction`, checks `supportsEyeGazeInteraction`, binds its
pose action and locates it once per stereo pair. A runtime advertising the
extension alone does not prove that the connected headset supplies gaze.
The default remains off, including for Quest 3 installations.
SteamVR's Steam Link settings also have **Share eye tracking data to other apps
on this PC**. The default is off; the current Frame session reports the gaze
action inactive with sharing off. This is a general PC-app permission, not one
limited to D2R. Enable it yourself if desired; the installer does not change it.

The prototype projects the combined gaze direction into each rendered eye's
frustum; it does not alter eye poses, camera matrices or the centered lighting
fix. `gaze_foveation_inner=0.35` protects a broad full-quality ellipse, plus a
0.177 radius margin for selecting the nearest point in a 9x9 mask grid. Each
viewport/radius builds the complete immutable grid once, shared by both eyes.
This may cause a setup hitch on first use; subsequent eye movement selects an
existing image, with no texture allocation, upload or added GPU wait. At most
four viewport/radius banks are retained; additional configurations use full
quality until restart. UI/transparency/post-effect exclusions remain unchanged.

Inactive, untracked, untimed, stale (>100 ms relative to predicted display),
out-of-view or invalid gaze uses **full quality**, as do the first recovery
pair and large gaze jumps. No stale gaze position is held. Logs record aggregate
validity counts and bank setup time, not gaze coordinates or gaze trajectories.
Toggle `eye_tracked_foveation=0` live to compare with fixed foveation; set
`fixed_foveation=0` to compare with full shading. Enabling eye tracking after a
launch that did not request it requires a restart. Headset readability and GPU
benefits require a controlled comparison; this feature does not reduce the
game's CPU work or stream bandwidth directly.

Owner validation on Steam Frame (October 8): clear and stable with eye/head
movement and creature labels. A stationary gaze/full/gaze trial (360 pairs each)
averaged 22.566 / 23.240 / 22.619 ms per stereo cycle, a small ~2.8% preliminary
reduction versus full shading. GPU scene spans were similar (16.042 / 16.309 /
16.340 ms, including queue starvation), so no large GPU speedup is established.
Fixed-versus-gaze and heavier scenes still need comparison. These October 8
results predate the coverage fix: foveation then reduced only D2R's depth pass,
not the visible shading pass, so they do not measure real foveated shading.

Windows Steam Frame / SteamVR (October 18, after the coverage fix, strength
Maximum): with the sharing setting enabled, every stereo pair had usable gaze
and all eligible scene draws followed it. The owner confirmed it in the headset.
With sharing off, or under Virtual Desktop, Eye tracked uses full quality.

Host-side geometry/fallback tests:
`g++ -std=c++20 -O2 -Wall -Wextra -Werror steamvr/gaze_test.cpp -o build-frame/gaze-test-host && build-frame/gaze-test-host`.
`openxr-probe.exe OUTPUT_PATH --capabilities` queries device eye-tracking support
without creating a graphics session; run it inside the intended Proton/OpenXR
environment. The full probe still creates and destroys a diagnostic session.

`[hud] stereo_labels=1` projects the game's native unit label anchors through
the current eye's exact scene view and projection, including asymmetric FOV and
physical eye separation. This bypasses the label path's inline flat projection
rebuild and keeps native names, health values and styling. It is scoped to unit
labels; menu UI and shadow fitting retain their original projection behavior.
The native placement also mixed projected foot X with projected head Y. The
updated hook combines the original world coordinates into one head-height anchor
before projection, so pitched/rolled eyes refer to one 3D point. Both the
matrix-only prototype and unified-anchor correction still doubled in the user's
headset tests. Centered projection subsequently improved apparent convergence;
the remaining reversed label disparity was fixed by current-eye draw ordering
below. Disabling the anchor correction alone did not improve depth.
The owner verified the combined fix on Steam Frame. Stereo math checks include
head pitch/roll; bosses and wider scene/headset coverage remain untested.

`[hud] monster_draw_order=1` is enabled by default: paint the
native monster health panel after the current eye's tooltip updates, within the
same UI pass. Hooks are signature-checked on launch; set 0 live
to compare native ordering. Names, health data and native styling are retained.

F8 saves one complete gameplay pair through ReShade, with matching
`Frame-pair-N-left/right` suffixes. This readback briefly stalls: exclude it from
performance measurements. For remote debugging, increment `[debug] capture_pair`
in the installed INI, or set/increment `capture_labels` to wait for a visible
unit label before capturing. Normal play performs no readback or screenshot IO.

The current Proton installer supplies the Microsoft SDK shader compiler privately
under `d2rloader/runtime`. ReShade had remained compiling throughout tests using
Wine's compiler; the SDK version completes the effect. Missing optional ceiling
art uses the procedural fallback. Stock Steam Play does not load this dependency.

## Build

### Native Windows build

From the checkout in PowerShell:

```powershell
.\steamvr\setup_windows_tools.ps1
.\steamvr\build_windows.ps1 -Test
.\deps\python\python.exe -m unittest install_test prepare_icons_test prepare_potions_test prepare_spell_sprites_test profile_report_test stream_report_test
.\deps\python\python.exe steamvr/ngx_abi_test.py --clang .\deps\llvm-mingw-20260616-ucrt-x86_64\bin\clang++.exe
```

The setup downloads pinned official portable tools into ignored `deps/` and
checks both SDK commit IDs. Python uses an explicit isolated import path for
this checkout and needs no pip packages. No system PATH or Visual Studio
changes are required. Clang may need execution permission outside an agent's
restricted sandbox. The LLVM-MinGW compiler produces native Windows binaries;
Proton/Wine are not involved in the build or execution.

Outputs are in `build-frame/windows-native/`. The DLL statically links its C++
runtime. Its patched ReShade interfaces and NGX adapter retain their GNU-target
ABI handling. Native CTest enables assertions in Release and runs 33 fixtures;
the Windows Python suite excludes the Linux `/proc` process monitor tests.
`frame_math.h` avoids shadowing the C library's `math.h` in libc++.

This build **does not install or replace any game DLL**. For first headset
tests use the archived baseline described in
[WINDOWS_HANDOFF.md](../WINDOWS_HANDOFF.md). Swap only the plugin after Save &
Exit when making a controlled comparison. The native source build differs from
the archived release candidate in compiler/runtime, even with equivalent source.

The Windows Steam wrapper accepts `--frame-openxr-runtime=PATH` before
`%command%`. With `--frame-vr` it supplies that manifest as `XR_RUNTIME_JSON`
to D2RLoader; normal Play removes the wrapper option and preserves the original
Steam command/environment. This can select `SteamVR/steamxr_win64.json` for
Frame without changing another app's global OpenXR runtime.

Native Windows menu focus requests are posted to the owning window's message
queue. Activating the game synchronously inside its render callback re-entered
the native focus handler and hung the first Akara dialogue in the baseline.
The regression test verifies activation occurs after a simulated engine lock is
released; expiry/cancellation prevent stale requests. Headset reproduction is
still needed to validate the fix in D2R.

### Linux cross-build

The local dependency checkouts and extracted toolchain are under `~/vrdeps`.
The Android NDK provides Clang's Windows SEH support; it does not make this an
Android or standalone Frame build.

```sh
export MINGW_ROOT=/home/berkybear/vrdeps/mingw/usr
export CLANG_ROOT=/home/berkybear/Android/Sdk/ndk/29.0.14206865/toolchains/llvm/prebuilt/linux-x86_64/bin
export D2RL_SDK=/home/berkybear/vrdeps/D2RLoader-PluginSDK
export OPENXR_SDK=/home/berkybear/vrdeps/OpenXR-SDK
cmake -S steamvr -B build-frame -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/cmake/proton-toolchain.cmake" \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-frame -j4
g++ -std=c++20 -Wall -Wextra -Werror steamvr/math_test.cpp -o /tmp/d2r-frame-math-test
/tmp/d2r-frame-math-test
g++ -std=c++20 -Wall -Wextra -Werror steamvr/deferred_draw_test.cpp -o /tmp/d2r-frame-ui-test
/tmp/d2r-frame-ui-test
python3 steamvr/session_test.py
python3 steamvr/install_test.py
python3 steamvr/ngx_abi_test.py --clang "$CLANG_ROOT/clang++"
```

To build the additional native Windows OpenXR loader for a preview package:

```sh
cmake -S cmake/openxr-loader -B build-frame/windows-loader -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/cmake/openxr-loader-toolchain.cmake" \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-frame/windows-loader -j4
# Commit source first; the package records that commit and file checksums.
python3 steamvr/package.py \
  --openxr-loader build-frame/windows-loader/sdk/src/loader/openxr_loader.dll
```

The install script verifies the game version and the official D2RLoader archive
checksum, backs up saves and any replaced files, installs the plugin and ReShade
assets, and adjusts video settings. It never replaces the original `D2R.exe` or
installs an automatically loaded DXGI proxy.

```sh
python3 steamvr/install.py
```

The initial save and installation snapshot is:
`build-frame/backups/20261008-171211`.
To remove this installed prototype and restore its original video settings:

```sh
python3 steamvr/install.py --restore build-frame/backups/20261008-171211
```

Restore preserves current character progress. Clear Steam's launch options if
removing the wrapper or moving this checkout. Character files can be restored
separately from the snapshot's `saves/` directory if needed.

## Diagnostics and remaining work

The prioritized feature backlog and headset verification checklist live in
[TASKS.md](TASKS.md), including UI, combat gestures and eye-tracked foveation.

For a bounded performance capture, change `[debug] profile_capture` to a new
positive integer in the installed `d2r_pcvr.ini` while gameplay is running. The
next 360 valid stereo pairs record per-eye CPU spans, sampled mod callbacks,
OpenXR wait/locate/input/acquire/copy/fence/release/submit spans, and optional
D3D12 timestamps. The probes then become idle automatically. Use a stationary
capture first, then a separate sequence for walking; retain the same scene and
graphics settings for comparisons. Set the counter to zero after testing so
the request does not repeat on the next launch.

```sh
python3 steamvr/profile_report.py "/path/to/game/mods/D2R-PCVR/d2rloader/logs/d2r-vr-frame.log" \
  --sequence 1 --csv build-frame/profile-stationary.csv
```

`Frame scope` records describe the main stereo drawing thread. The current
development build also emits `Frame hook` records for each recording/input
thread, including guarded memory copy calls/bytes, portal callbacks, VRS cache
misses and the input tick. Counters use thread-owned slots, sample hot timings
1/64 (input 1/16), and become idle automatically. Times are inclusive and worker
threads overlap: adding them together does not give frame time. The bounded
registry reports overflow rather than allocating on a callback path.
Set `[debug] profile_hooks=0` before a new capture for timing comparisons against
older builds; use `1` for a separate all-thread attribution capture. The setting
is latched per capture. The report defaults to a **full-refresh** budget:
11.111 ms for one stereo pair at 90 Hz. Use `--refresh-hz 96` or `108` for the
actual headset rate; `--pair-refreshes 2` explicitly selects a half-rate
comparison. An over-budget pair alone does not prove a compositor frame drop.
GPU scene spans include idle queue
time while CPU commands are prepared; they are not GPU busy-time measurements.
Effects span includes the ReShade overlay. GPU query readback uses the
conservative pair completion fence. Set `[debug] profile_gpu=0` for CPU-only
captures when comparing deferred GPU waits; GPU profiling intentionally waits
for its readback and would hide that optimization. Two timestamp submissions
and scope measurements add diagnostic overhead. Do not compare a profiled
run against an unprofiled run as if all timing changes were optimizations.

The October 10 development performance build adds generation-invalidated
per-thread pipeline lookup caches, Windows SRW locks for VRS/UI callbacks,
local guarded memory copies, per-instance portal batch classification, sparse
routine CPU-time sampling, and cursor queries only when mouse look is active.
The launcher checks known game PIDs every 0.5 s and scans the full process tree
every 5 s during play; startup/exit still get immediate discovery and identity
checks. The memory copy uses the same-process `ReadProcessMemory` destination
buffer instead of `WriteProcessMemory`: [Wine's implementation](https://github.com/ValveSoftware/wine/blob/bleeding-edge/dlls/ntdll/unix/virtual.c)
has a local guarded copy for this case. It never changes page permissions.
Proton tests include invalid/freed/read-only/cross-page buffers and confirm
failures do not enter application exception handlers. The isolated benchmark
does **not** establish game frame-time savings; controlled headset trials do.

The October 12 candidate also skips the HUD mutex for command lists outside
the UI pass and caches immutable resource descriptions per recording thread.
Resource creation/destruction invalidates that cache; RTV/DSV descriptors are
still resolved on every bind because descriptor copies can change their target.
Detailed captures add `hud-scissor`, `hud-lock`, `resource-desc` (cache misses)
and `resource-view` buckets. The lock bucket measures acquisition wait, not
exclusive CPU work. Missing draw-distance keys now use the shipped two rings
and object radius 210 (300 until October 20: see
[Draw distance and the frame](PERFORMANCE.md#draw-distance-and-the-frame-october-20));
explicit settings remain in effect. The object-distance control uses steps of 50.
See [the #20 investigation](PERFORMANCE.md#issue-20-investigation-october-12)
for validation and the remaining stereo architecture question.

Optional UI-trace draw callbacks are selected once at startup. Set
`[debug] uitrace=1` and restart before Ctrl+F10 capture or live shader-skip tests.
A nonempty initial `skip_pipeline`/`skip_shaders` also enables the callbacks.
No callback lists are toggled during rendering. Ordinary F8 stereo capture is
unchanged.

Keep code comparisons at identical graphics settings first. The owner's
October 10 baseline was **100% resolution, shadows 4, AO 4, DLSS off**, with
`model_radius=300`, fog end 200 and `defer_gpu_wait=0`; earlier 80%/medium notes
describe an older preset. The radius-210/deferred-wait trial was rolled back
after severe movement/menu stalls and image degradation. Installed settings are
again radius 300/wait 0 with profiling off, using the pre-performance DLL.
Shadow quality 2 remains untested.
See [PERFORMANCE.md](PERFORMANCE.md) for the 360-pair measurements and scene
matching limitations and the separate streaming faults found afterward.
Code-only A/B did not establish an FPS gain; shorter average cycles in the
deferred-wait trial did not establish smooth headset delivery. Fog distance does
not by itself prove all culled geometry/shadows are invisible, and model count
need not follow radius squared in a real scene. Check distant silhouettes and
lighting. Higher refresh rates, transport lock splitting/dedicated frame calls,
eye texture shape, and shared inter-eye skeleton/shadow work remain separate
experiments. They are not included in the small-fix build.

`[render] symmetric_projection=1` is the default lighting compatibility
setting. D2R's native light-bin shader assumes a centered projection;
the headset can recommend offset frusta. This mode renders a centered FOV that
encloses both recommended views and submits that exact FOV to OpenXR, preserving
the original eye poses and head tracking. It samples the setting once per pair,
so a live change cannot split the eyes between modes. At the same resolution it
reduces angular pixel density. The owner reports the waypoint patches appear gone
on Steam Frame, with a softer image; other scenes/headsets remain unverified.
Set 0 to restore runtime-recommended projections. See the
[OpenXR projection-view contract](https://registry.khronos.org/OpenXR/specs/1.0/man/html/XrCompositionLayerProjectionView.html).

`[render] defer_gpu_wait=1` is set by the installer; a missing key means 0, and
VR Settings > Perf offers it while it is off (see Stereo rendering above). Image
copies and state restoration are submitted on the D3D12 queue bound to OpenXR;
completion is checked before each eye's copy allocator is reused and before
shutdown, instead of blocking the CPU before every image release. Set it to 0
for the original conservative behavior. See the
[OpenXR D3D12 queue/state contract](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XR_KHR_D3D12_enable-swapchain-image-state.html)
and [OpenXR implementer guidance](https://community.khronos.org/t/do-you-have-to-do-cpu-sync-before-xrendframe/110117).
Proton's `wineopenxr` also drains the bound command queue before enqueueing its
release layout transitions. The source default remains 0. The October 10
re-test reduced mean cycle time from 31.69 to 24.60 ms in nearly the same view;
the end stage fell from 5.10 to 2.88 ms, with some fence waiting moving into
runtime submission. Subsequent severe stalls/quality collapse prompted rollback
to 0 and the pre-performance DLL. Streaming receive delays/reset requests were
also found, including resets before this trial; the root cause remains open.
An earlier test was confounded by a competing CPU-heavy project and produced
bursty walking with either setting. See [PERFORMANCE.md](PERFORMANCE.md) and
HANDOFF.md for current evidence; other headsets/runtimes are untested.

Saved SteamVR Link fault histories can be analyzed with no headset connection:

```sh
python3 steamvr/stream_report.py /path/to/vrlink_aftm_*.csv \
  --driver-log /path/to/driver_vrlink.txt --json /tmp/d2r-stream-report.json
```

This merges overlapping histories and handles the driver's 32-bit timestamp
rollover. Missing samples remain unknown. The driver-aligned receive interval
includes delivery/client handling; it is not pure network latency. These are
fault-triggered snapshots, not a session FPS or loss-rate benchmark, and contain
no image-resolution/bitrate measurement. `--since`/`--until` accept ISO timestamps;
include a UTC offset when analyzing logs away from the capture machine's timezone.

- Runtime log: `mods/D2R-PCVR/d2rloader/logs/d2r-vr-frame.log` in the game folder
  (`mods/pcvr-dev/d2rloader/logs/d2r-vr-frame.log` for developer mode). A bounded
  queue writes batches on a worker without forced disk flushes. The old SDK log,
  `d2r-pcvr-vrcam.log` (`d2r-vr-vrcam.log` up to 0.3.15), remains for loader/startup messages. Live syscall sampling
  identified repeated main-thread `fsync()` waits on that old file during hitches.
  The new logger's concurrent-producer and shutdown-drain test passes in Proton.
  The user confirmed improved hitches after deployment. A subsequent 599-sample
  main-thread trace saw no journal/page waits or captured forced log flushes;
  one 475 ms gameplay gap remained in that walking test.
- ReShade log: `ReShade.log` in the game folder.
- Proton log: `build-frame/steam-2536520.log`.
- VR launches retain Wine errors and VKD3D warnings but disable Proton's default exception
  and unwind traces. Those traces produced over 38,000 lines in one second during
  walking tests. Game-side frame/pipeline timing remains enabled. Set
  `D2R_FRAME_WINEDEBUG` to an explicit Wine debug profile for a targeted diagnostic
  launch; normal Steam Play does not receive the VR logging settings.
- Independent probe result: `build-frame/openxr-probe-result.txt`.
- By default capture waits for both GPU copies before releasing eye images.
  The experimental deferred-wait mode above retains completion checks at
  allocator reuse and teardown instead.
- Measure the updated performance preset and test the height chord on Frame.
- Verify laser selection, inventory dragging, menu placement and longer gameplay
  after the checked-read fix on Frame.
- Verify corpse filtering, calibration after loading, dialogue stability and
  Steam returning to Play after the game's own Exit command on Frame.
- Belt, health and mana still use the original screen HUD. A separate native
  toolbar plane is planned; FlatVR's existing shared HUD is not yet consumed.
- ReShade's sky/fog now compiles using the private modern SDK compiler described
  above. Include its GPU cost when profiling; earlier incomplete-compilation
  sessions did not render the same effects.
- Further checks remain for menus, HUD layout, combat and longer play sessions.

Upstream provenance: `natpoh/diablo2-vr-port` at
`10cfb0a2a9080184101794d3ac8c238c35717967`; official D2RLoader 1.3.1-beta and
ReShade 6.8.0 with full add-on support. Existing copyright and license notices
are retained. The original `emmericp/D2R-3D` checkout remains separate.
