# VR performance investigations

## Replay, October 19

**Replay** ports upstream's right-eye command-list replay (see the Issue 20
notes below, where it was first identified as the main architectural
difference). One CPU pass draws the left eye; its lists run again on the GPU
with the right eye's camera constants. Upstream reported 127-159 stereo pairs/s
with it against 108 with two passes on the author's PC, and about 5-6 ms of CPU
per eye pass as the limit. On the owner's PC a pair took about 16.7 ms of work
(left 6.86, right 6.95 ms of it), so a CPU-limited session should gain most of
the right pass; a GPU-limited one gains little, as the GPU still draws both
eyes.

First working run (Act 1 camp, 3072 square per eye, DLSS off, the game's
temporal anti-aliasing on): 446 of 446 pairs from one pass, the game's 35
lists in its 5 calls (0.08 ms to submit), about 94 camera records folded a
pair in 46 slots, none overflowed. The right eye's CPU time fell from about
6.2 to 0.8 ms, but the rate stayed at 44.6 pairs/s: SteamVR paced the game at
half of 90 Hz, and the end-of-frame wait for the GPU rose from about 4.9 to
8.9 ms - the GPU, drawing both eyes in full, became the limit.

The limit was the end-of-frame wait, not the GPU's speed: with the
conservative wait the CPU and GPU take turns, so a frame costs the CPU's time
plus the GPU's. With `defer_gpu_wait=1`, the game's FXAA and Vertical Sync
off (3128 square per eye, Headset; Act 1 camp, standing), Replay drew 889 of
889 pairs from one pass: 88.9 pairs/s with the runtime predicting 88.9 Hz,
against 44-45 before. Leaving the camp, a biome set paused it and it took
about a minute to settle again, at 40-48 pairs/s in two passes meanwhile; the
10-second line now counts what starts the settle again. Replay briefly became
the default stereo rendering, and the installer now sets `defer_gpu_wait=1`.

A later session (3128 square, the deferred wait on, switching modes with
fpsVR running; the owner found By turns best) measured:

| Stereo rendering | Headset frames/s | CPU per frame |
| --- | ---: | --- |
| By turns | 86-88 (each eye drawn 43-44) | game pass 5.7-7.4 ms |
| Replay, replaying | 57-67 | left 7.9-8.3 + right 0.75 ms |
| Replay, paused | about 45 | two passes |
| Both eyes | 44.7 | 6.7 + 6.4 ms |

Outside the camp the game set the same biome 1-13 times in 10 seconds, each
restarting Replay's 240-pair settle, so it drew two passes much of the time.
Noting the left pass's lists costs about 1.5 ms, which left a replayed pair
just over the 11.1 ms budget. By turns needs the deferred wait as much as
Replay: an earlier run at 3072 square without it went 43-46 frames/s, the
frame's end waiting 5.4-6.3 ms for the GPU (1.2-1.4 ms deferred). By turns
became the default, and the Video page offers the deferred wait in every mode.
At 2048 square every mode's pass took 12-14 ms, against 6-7 ms at 3128, for
reasons not yet known; the interaction atlas upload took 5.4-7 ms on the
frame thread, up to once a second (1.2 ms on October 10).

Differences from upstream: the right eye's camera math is a pure header with
tests (the fold's V*P_R matches a true right-eye camera's clip coordinates);
camera positions inside a found record's matrices are not taken for stand-alone
ones; the native command-list hooks and the upload-buffer mappings are made
only once Replay is first wanted; the left eye reaches the headset through the
transport's normal capture at the held present, and the right eye's pose is
the left one moved along its x, so the layer matches what was drawn.

## Eyes by turns, October 19

On the owner's Windows PC a stereo pair took about 16.7 ms of work: game 0.90,
left draw 6.86, right draw 6.95 and OpenXR submission 1.95 ms. That is over the
11.1 ms budget at 90 Hz, so the runtime paced the game at about 44 pairs/s.
These are one session's 10-second averages, not a controlled benchmark.

VR Settings > Video > **Stereo rendering: By turns** (now `[stereo] rendering` 1)
draws one eye per game frame and resubmits the other eye's last image at its
own pose, following upstream's "Native OpenXR - eyes by turns" mode. It removes
the second CPU draw from every headset frame without replaying command lists,
so culling, labels, effects, foveation and DLSS stay per eye. Each eye still
updates at half the headset rate, and the eyes are one game frame apart. The
right-eye replay described below would keep both eyes from one frame but
was then unported (it is now Replay, above). Both eyes each frame was then the
default.

The first headset test (SteamVR, Steam Frame, 3072x3072 eyes; DLSS off - the
game creates a DLSS instance at start but never evaluates it - and the game's
temporal anti-aliasing, `Anti Aliasing` 3, on) looked
right with the head still but jittered and lost convergence in every head
turn. Then the other eye went to the runtime at its own older pose, which
SteamVR should reproject per eye. The candidate now turns that eye itself:
each eye's image as shown is kept (`KeepTurn`), and on the frames that draw
the other eye a full-screen GPU pass rotates it to this frame's view
(`TurnEye`, `turn_warp.h`). Both views then carry one frame's poses, as from
a pair. In that session the GPU, not the CPU, limited the rate: both eyes ran
at 25-28 pairs/s, by turns at 42 frames/s (21 per eye), and one eye's pass
took 11-14 ms against about 7 ms per eye in the measurement above.

The second test: head turns were right, but walking left one eye lagging. The
turned eye still showed the world from where the hero stood a frame earlier,
10 cm or more at a run, which a rotation cannot correct; that needs the scene's
depth (a positional reprojection). That candidate became **Hybrid** (since
removed; By turns keeps its rule until both eyes have depth): a pair
from the hero point's first step to 0.3 s after its last and on a snap turn,
by turns only in first person, stick and mouse turns part of the GPU turn.

**By turns** then added the positional reprojection. Each eye's scene depth is
kept with its image (scenedepth's per-present copy, kept even when no effect
wants it), and where the eye stood in the world (View) and its near plane. The
eye not drawn is a grid of 4-pixel cells over the old image, each vertex put
at near / depth along its ray, carried through the world to the current eye
(TurnMove) and rasterised with a reversed depth test, over the rotation-only
turn for the edges. GPU tests check a wall, the sky, occlusion by a nearer post
and DLSS's drawn corner. Not yet measured in the headset: its GPU cost (a 3072
square eye is about 3.5 million grid vertices) and how the stretched
disocclusion edges look.

## Issue 20 investigation, October 12

[#20](https://github.com/bearstonem/d2r-pcvr/issues/20) reports roughly 60 FPS
and less than 50% GPU utilization on an RTX 5080, compared with 90 FPS and
better clarity in another mod. The CPU, headset/runtime, refresh rate, exact
mod versions, frame metric and comparable settings are still unknown. No
reporter capture has arrived. This candidate addresses costs found in our
code; it does **not** establish that the RTX 5080 report is fixed.

The saved October 10 native Windows log contains a ten-second window with
left/right draw means of 6.77/6.12 ms, including Present means of 0.50/0.38 ms.
The two draws alone exceed the 11.11 ms full-90-Hz budget. A separate 109.5 ms
hitch spent 89.1 ms in XR begin, showing why a long frame cannot always be
attributed to drawing. These are local historical captures, not the reporter's
hardware, matched A/B measurements, or evidence of a specific FPS cap.

Current upstream at
[`4b6284c0766c089139693a504333b9a19201cdd2`](https://github.com/natpoh/diablo2-vr-port/tree/4b6284c0766c089139693a504333b9a19201cdd2)
has a significant architectural difference: its
[`replay_right=1` default](https://github.com/natpoh/diablo2-vr-port/blob/4b6284c0766c089139693a504333b9a19201cdd2/d2r_vr.ini#L91)
records the engine's CPU draw once, then replays GPU commands with right-eye
camera constants. Our build still invokes the engine draw twice. If the
reporter's comparison version uses this path, it is a plausible larger source
of the difference. Its identity/version must be confirmed.

A replay port needs per-eye view/projection/history buffers, direct/compute/copy
queue ordering and resource lifetimes, correct right-eye depth for effects,
and one desktop Present per pair. Our native OpenXR capture, VRS masks,
physical controls and spatial UI also need integration. The upstream path has
fallbacks and recent area-transition/depth fixes; copying it wholesale would
not validate those behaviors here. No replay code is included in this candidate.

Implemented changes:

- HUD bind/scissor/reset callbacks reject command lists outside the UI pass
  before taking the shared HUD mutex. A bounded atomic membership filter allows
  false positives only; collisions still consult the locked authoritative map.
  UI-list reset/destruction removes membership. UI capture, hiding and map
  redirection retain their existing critical sections.
- Per-thread resource-description caches avoid repeated native metadata calls
  in VRS, depth and HUD bindings. Resource creation/destruction and device
  destruction invalidate them. RTV/DSV-to-resource resolution is deliberately
  uncached: D3D12 descriptor copies can replace those mappings without an
  `init_resource_view` event. A HUD exit reuses its resolved metadata locally.
- Missing `[render] rings` and `model_radius` keys use the shipped defaults
  (2/300), rather than 4/1000. Camera and settings UI share those defaults;
  explicit INI values are preserved. Object-distance adjustments use steps of
  50 instead of jumping directly from 300 to 50.
- Routine ten-second logs add `Frame pacing`: predicted runtime application rate,
  XR begin/wait, end/submit and total GPU fence averages alongside the existing draw
  timings. Bounded hook captures include resource lookups and HUD lock waits.
- The report defaults to `1000 / refresh_hz`, one complete stereo pair per
  refresh. `--pair-refreshes 2` retains an explicit half-rate comparison. Older
  CSV/log formats still parse. Historical late-pair counts below used the old
  half-rate budget and must not be read as full-refresh success rates.

Validation on Windows, Ryzen 9 5950X (local machine, not the reporter's CPU):

- `powershell -NoProfile -ExecutionPolicy Bypass -File steamvr/build_windows.ps1 -Test`:
  native DLL builds, **42/42 CTest cases pass**. New cases cover description
  lifetime/device invalidation, descriptor rebinding, worker isolation,
  membership collisions, removal/reuse and concurrent membership. Existing
  settings fixtures cover partial INIs and retention of explicit distances.
- `.\deps\python\python.exe -m unittest discover -s steamvr -p profile_report_test.py -v`:
  **7 tests pass**, including a synthetic 13 ms pair that misses full 90 Hz but
  passes an explicitly requested half-rate comparison.
- `resource-descriptions-test.exe --benchmark`: native D3D12 metadata query
  versus cache hit, 30.10 versus 2.58 ns/query with one thread. Absolute cost
  is small and cannot explain the reported FPS gap by itself.
- `active-keys-test.exe --benchmark`: isolated empty HUD registry, 5.70 versus
  0.71 aggregate wall ns/query with one thread; 38.91 versus 0.20 with eight
  workers. These are synthetic contention measurements, not game frame-time
  savings. Real UI work can still wait on the HUD lock.
- Candidate runtime commit `f014e6d` was installed into the local Steam and
  Battle.net copies after backing up the previous DLLs, settings and saves.
  Installation preserved their settings and player data. The Steam edition
  (game 3.3.93787) was launched and measured in offline Paladin gameplay on a
  Ryzen 9 5950X / RTX 5090, using SteamVR OpenXR and Steam Link to Steam Frame
  at 90 Hz. Battle.net gameplay and the reporter's RTX 5080 remain untested.

Local headset measurements, October 12 evening:

| Capture | Scene scale / terrain rings | Mean pair ms | Throughput FPS | Pair p95 ms | Left + right draw ms |
| --- | --- | ---: | ---: | ---: | ---: |
| Moving, sequence 10121839 | 133% / 4 | 25.887 | 38.6 | 32.665 | 16.395 |
| Standing, sequence 10121844 | 100% / 2 | 24.779 | 40.4 | 32.451 | 15.898 |

Each capture contains 360 stereo pairs, with `profile_gpu=0` and
`profile_hooks=0`. GPU timestamp queries and all-thread hook attribution were
disabled; main-eye scopes remained active. Routine ten-second windows ranged
from 41.8-44.1 FPS early in gameplay and 33.2-39.0 FPS during movement. GPU
utilization spot samples were 29-39% on the RTX 5090, including 34% after the
second capture. These are application stereo-pair rates, not headset delivery
or compositor reprojection measurements.

The user changed both scene scale and terrain distance before the planned
terrain-only comparison could start. Position/view and motion also differ
between the captures; this table does **not** isolate either setting's effect
or establish a gain from the candidate DLL. There is no matched previous-DLL
capture. The bounded captures cannot establish the reporter's bottleneck.

Eye images remain 3840 x 2160. Both runs used fixed foveation and symmetric
projection, object radius 300, and `defer_gpu_wait=0`; shadows, AO and
atmospherics were 2, with DLSS and dynamic resolution disabled. In the second
capture, average XR begin/end were 2.708/4.721 ms, with 3.216 ms of GPU fence
waiting included in XR end. Coarse per-thread CPU totals averaged 14.714 ms
across the two eye draws, consistent with CPU draw work and synchronization
limiting throughput rather than sustained GPU saturation. They are not
exclusive high-resolution CPU profiles. Periodic roughly 80-90 ms hitches
included an eye draw around 62 ms without pipeline creation; their cause is
unresolved. Neither capture reaches the full 90 Hz budget of 11.111 ms.

Temporary profiling keys were removed after each capture, with the INI verified
byte-identical to its pre-capture contents. The user's final 100% scale and
two-ring setting were retained. Raw logs, CSVs, DLLs and backups remain local
under ignored `build-frame/` paths; only aggregate results are recorded here.
Deferred GPU waiting, scene resolution and projection source defaults were
not changed by this candidate.

Remaining validation needs the same offline character/scene, runtime and
settings before/after a DLL swap, with routine logs plus separate 360-pair
stationary and walking captures (`profile_gpu=0`, `profile_hooks=0`). An
attribution capture can then enable hooks. Test the map tab, labels and spatial
NPC/inventory menus as well; those UI regressions have not been checked here.
Record actual eye dimensions, scene scale, symmetric projection and VRS when
comparing clarity. Fresh/full installs use 100% scene scale from v0.3.10;
the previous 80% default rendered only 64% as many pixels. Update / repair
preserves existing video settings. Lower resolution does not remove CPU draw/submission work.
Keep #20 open until the reporter's bottleneck and improvement are measured.

### Follow-up: periodic diagnostic stalls

The ten-second hitches in the `f014e6d` session line up with
`OnReshadePresent` calling `BusyThreads` synchronously. Its Toolhelp snapshot
enumerates system threads before querying this process's threads. The native
standalone scanner benchmark measured 52.078-53.467 ms (median 52.850 ms),
consistent with the roughly 54 ms added to an otherwise 8 ms eye draw.

Native builds now start a bounded diagnostics worker during plugin load.
Present submits a value-only request and polls for the completed report;
snapshot creation, thread queries, allocation and sorting run on the worker.
Addon callbacks are unregistered before the worker is joined at unload. The
worker reads OS thread counters only and never follows game pointers or uses
XR/render resources. Percentages use elapsed thread CPU time, retaining thread
creation identities to reject recycled IDs, instead of a cycle/CPU-time ratio
dependent on clock speed. Worker startup failure disables the scan.

`steamvr/build_windows.ps1 -Test` passes **43/43** native checks. The new fixture
covers CPU percentages, reused IDs, absent/invalid counters, restart and
render-side polling while the scanner is deliberately blocked. In its native
benchmark the background scan took 53.182 ms while request submission took
0.300 microseconds.

Runtime commit `918bd5e` was installed and tested with the same local Windows
5950X / RTX 5090 / Steam Frame setup, Steam game 3.3.93787 and offline Paladin.
Three 360-pair captures with hooks/GPU queries disabled contained diagnostics
scans of 57.63, 62.61 and 60.14 ms on the worker. Their maximum complete pair
times were 34.944, 28.648 and 34.026 ms respectively: the scan no longer
appears as a roughly 62 ms eye draw. The session's remaining logged >80 ms
hitch at entry spent 417 ms in the game gap while loading. No average FPS gain
from the diagnostics change is established by a matched previous-DLL run.

A separate native Windows trial changed only the existing GPU wait option,
keeping 100% scale, two rings, radius 300, fixed foveation and symmetric
projection. Each row is 360 pairs with hooks and GPU timestamps disabled:

| Sequence | `defer_gpu_wait` | Mean pair ms | Application FPS | Pair p95 ms | Eye draws ms | GPU fence ms |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| 1791856478 | 0 | 25.302 | 39.5 | 32.594 | 15.750 | 3.337 |
| 1791856539 | 1 | 22.378 | 44.7 | 25.436 | 15.930 | 0.000 |
| 1791856550 | 0, restored | 24.005 | 41.7 | 31.543 | 16.018 | 3.219 |

The character stayed at one position, but head yaw changed within and between
rows (124-153, 111-124 and 124-131 degrees). These are not exact view matches.
Eye draw times remain similar; with overlap enabled XR end fell to 1.234 ms,
while XR begin rose to 3.936 ms. This supports a local pacing benefit, not
eliminated rendering work or a universal percentage gain. Copy allocator
reuse and shutdown still wait for GPU completion. The INI was restored
byte-for-byte after this trial; source defaults remain conservative because
earlier Linux deferred-wait testing did not establish stable streaming.

A subsequent one-minute walking/turning trial held 44.7-45.0 FPS in complete
ten-second windows with GPU overlap enabled. These moving views are not
matched before/after benchmarks. The owner confirmed the image, tracking and
controls looked normal. After the automatic rollback, `defer_gpu_wait=1` was
enabled as a local preference on this PC; 100% scale, two rings and fixed
foveation were retained. The shipped default remains 0. Longer sessions,
area transitions and the broader UI regression checks remain outstanding.

Attribution sequence 1791856607 (`profile_hooks=1`, GPU queries off) covered
18 threads. Approximately 22,565 pipeline binds/pair reached callbacks;
sampled VRS pipeline/draw and UI world-bind costs on the main draw thread were
0.130/0.131/0.120 ms per pair, while its two eye draws totalled 16.359 ms.
Skeleton game/mod estimates peaked at 0.088/0.034 ms per worker per pair.
These inclusive concurrent estimates cannot be added into a frame cost.
The input-tick estimate includes transport-lock waiting and is not exclusive
CPU work. Sparse resource/HUD-lock timing rounded to zero and must not be
treated as proof of zero cost. The capture's 36.2 FPS includes attribution
overhead and changing head direction; it is not a comparable throughput run.
Profiling keys were restored byte-for-byte afterward. The larger
two-engine-draw cost remains the primary architectural follow-up.

## Native Windows continuation, October 10 afternoon

Native SteamVR streaming to Steam Frame now launches the mod and an offline
character successfully. The owner reports the native performance candidate runs
"superbly". This is subjective feedback; no bounded native baseline/candidate
comparison has completed. Windows uses 80% resolution scale and shadows/AO/atmospherics 2,
fixed foveation on, gaze off, wait 0 and radius 300. Actual game eye images are
3840×2160; the standalone probe's 2160×2160 recommendation is a separate value.
The scene/settings differ from Linux; no cross-OS performance gain is inferred.

Build `e27084ed…`, installed at 17:39 PDT, adds the owner's requested body
calibration and shoulder swap controls (32 native tests pass). The settings-menu
calibration saved eye height 1.65854 m and controller span 1.50859 m live.
That changes world scale and arm proportions: any later baseline/candidate
comparison must hold this calibration and the corrected camera INI constant.
Do not include calibration/INI writes or coordinate probes in timed captures.
The new shoulder gesture uses the existing confirmed native equipment request;
hip overlay anchors are removed. Cave swing mismatch diagnostics are expanded,
but collision/warp guards are retained and the cave issue remains open.

The 17:52 cave reproduction supplied paired native/render positions confirming
terrain elevation shear on X/Z. Candidate `f559b3e5...` corrects melee's player
comparison and enemy capsules without changing the 0.65 m warp limit. All 33
native tests pass; installed at 17:55 after normal exit. The owner reports "it all
works" after the offline retest. Logs confirm accepted cave melee contacts with
no position mismatches, return outdoors, and normal exit at 18:02. This validates
the gameplay correction, not a performance improvement.
The bounded read-only coordinate capture is diagnostic evidence, not a timed run.

Before a bounded baseline capture began, Akara dialogue froze desktop and
headset. Dump analysis points to synchronous `SetForegroundWindow` inside the
mod's render/input callback, re-entering a game focus path waiting on an SRW
lock. The performance candidate now defers activation through the window queue.
25 native C++ tests pass, including the focus regression. Candidate SHA256:
`9b3ce9df5ba307222c5d1d04848ed74ab2aeec83a6f1e4325155268a55f075f7`.
Its log records Akara dialogue opening/closing twice, followed by gameplay and
normal Save & Exit. Original baseline and saves remain backed up.

The owner now requests fixes for missing torso/belt potions and loss of wrist
vitals/free-swing damage after a dungeon transition. A follow-up DLL
`996cc430ca276d65a1aaa877de960b2c95a8dd4ea2c5ae2d1062cdabcfbe134d`
is installed, preserving the performance changes and current graphics/INI.
SteamVR's native probe confirms requested formats 29/10 use resource formats
27/9 (typeless). Potion and UI conversion GPU tests failed with typeless
textures before the fix and pass with explicit typed views afterward. All 29
native tests pass, plus potion stereo/readback tests including SteamVR's sRGB
format. Callback tickets recover discarded game/UI work without executing late
callbacks against newer requests. Headset validation and torso diagnosis remain
pending; the callback cause of the reported transition issue is not yet proven.
Diagnostic stereo captures must be excluded from performance measurements.
Owner testing confirms belt models render with `996cc430…`; torso and cave
swings still fail. Cave diagnostics show fresh UI snapshots but expired native
local-player identity, ruling out a stalled UI queue for that wrist failure.
Next candidate `5847a204…` is installed with UI-thread identity refresh, closer
belt configuration support and stable empty-set detection; 30 native tests pass.
Normal game closure and backups preceded installation at 17:14 PDT. Graphics
are unchanged; belt_forward is now 0.06 m at the owner's request. Headset cave
validation confirms wrist HP/mana survives the cave transition. Cave swings
still fail: callbacks run, but the server/render player-position guard rejects
the requests. Normal-posture captures confirmed torso/shield clipping. The
installer had emitted a malformed first camera section, so Win32 ignored the
intended near=0.2 and used the default 1.5. Correcting the installed INI live
fixed torso/arms/shield clipping (owner-confirmed); installer parsing and its
Win32 API regression test now pass. The actual camera settings changed to the
intended values at 17:24:50 PDT. Future controlled comparisons must use the same
corrected configuration for both DLLs. No performance measurement is inferred.
See the current [Windows handoff](../WINDOWS_HANDOFF.md) for local evidence.

## Afternoon retest and Windows handoff

The owner returned and tested the restored baseline: **"Still stalls and loses
image quality"**. After a full PC reboot, fresh SteamVR and Frame connection,
the same baseline still failed: **"Still happening"**. The fresh driver log
recorded **119 stream reset requests before D2R launched**, from SteamVR startup
at 15:40:34 to the launch at 15:41:22 PDT. At least one failure exists outside
the mod; separate native game-update stalls remain unresolved.

| Restored-baseline capture | Pairs | Mean ms | p95 ms | Max ms |
| --- | ---: | ---: | ---: | ---: |
| Before reboot / 10201 | 360 | 23.759 | 31.631 | 37.521 |
| After reboot / 10202 | 360 | 22.932 | 30.004 | 35.161 |

Views/routes differ, and these short windows miss larger intermittent stalls
visible elsewhere in the logs. They do not demonstrate a reboot performance
gain or refute the owner's feedback. Graphics/DLL settings were unchanged.

A manual 50 Mbps streaming test was started but **not evaluated**: the owner
chose a clean Windows installation before providing a comparison. SteamVR Link
was restored to automatic bandwidth, target 200 Mbps, with OpenVR readback
verified. No conclusion can be drawn about the 50 Mbps setting.

Current next step is [WINDOWS_HANDOFF.md](../WINDOWS_HANDOFF.md), baseline first;
native Windows is untested. Portable numeric evidence, DLL hashes, validation
and limitations are in [evidence/2026-10-10-handoff.json](evidence/2026-10-10-handoff.json).
Raw logs are retained locally, excluded from GitHub. The handoff release keeps
the baseline distinct from the current-source experimental performance DLL.

## Current disposition: rollback after severe stalls

After the captures below, the owner reported random stalls during movement,
combat and menus, stuck/catching-up frames and severe temporary image-quality
loss. He requested further investigation without headset use and went to bed.
The game was closed; the pre-performance DLL `16e24f55…` was restored with
`defer_gpu_wait=0`, `model_radius=300` and all profiling off. Other settings,
game graphics, saves and SteamVR/network configuration were preserved. No game
launch or headset test was performed during this follow-up. The rollback is a
comparison baseline, not a demonstrated cure. Both candidates and all source
changes are preserved; `build-frame/d2rl-vrcam.dll` now contains the later,
**staged-only** `bc51fd37…` candidate, not the installed DLL.

Records: `build-frame/latest-perf-regression.json` and
`build-frame/latest-perf-offline-stage.json`. Original logs, ten Link histories,
configuration and replaced DLL are preserved under
`build-frame/backups/perf-regression-20261010-023124`.

### Streaming was also failing

Saved SteamVR Link histories contain 10,711 unique driver rows from
02:20:54.816 to 02:24:53.801 PDT, after merging 18,993 overlapping input rows.
The Link log records **147 video stream reset requests** in that interval,
including bursts of retransmission requests for messages no longer retained.

| Driver stage | Median ms | p95 ms | Maximum ms |
| --- | ---: | ---: | ---: |
| Gate release → app submit | 17.799 | 23.226 | 126.808 |
| Encode start → end | 7.576 | 8.866 | 12.371 |
| Send start → end | 0.528 | 0.795 | 1.564 |
| Send start → reported receive | 7.772 | 80.810 | 734.133 |
| Receive → decode start | 0.059 | 0.189 | 146.378 |
| Decode start → end | 5.560 | 7.129 | 26.534 |

These stages have different valid sample counts. Missing decode/receive fields
are unknown, not zero work; some frames never acquired those timestamps. The
driver clocks wrapped during this interval and must be handled as 32-bit
microseconds. Do not subtract remote-clock timestamps from host timestamps.
The displayed receive values are the driver's already-aligned fields, not a
direct measurement of pure radio latency.

For example, the frame released at 02:23:54.746 had gate→app submission of
16.401 ms and encode time of 7.493 ms, but send→reported receive of 674.954 ms.
At 02:23:55.546, the same measurements were 16.240, 6.319 and 734.133 ms;
that row had no decode completion. Game rendering alone does not explain those
delivery delays. The evidence implicates transport/receiver handling, but
does not distinguish wireless problems, driver queuing/retransmission or client
handling. Actual bitrate/resolution is absent, so the reported picture collapse
cannot be quantified from these logs.

Reset requests also occurred at 01:52–01:54 before installing the first CPU
candidate, and before enabling deferred wait at 02:01:10. Thus the streaming
problem cannot be attributed solely to the new DLL or wait setting. The last
session's compositor summary records 14,376 presents and 733 drops, but includes
startup/menus and uses different accounting from stereo-pair profiles; it is
not a like-for-like FPS benchmark against the earlier sessions.

Current adapter inspection found Wi-Fi power saving already off. Kernel logs
covering 01:45–02:26 showed no GPU error or USB reset; their connection-time WMM
parameter warnings do not establish a stall cause. No network tuning or global
driver changes were made. D2R settings remain resolution scale 100, FXAA,
DLSS off and dynamic resolution off. The owner briefly tried room rings 4 and 1
in the VR settings during the last session and returned to 2; final INI changes
from baseline were only wait/radius and disabled hook profiling. The later
menu/settings intervals should not be treated as identical gameplay scenes.

`steamvr/stream_report.py` reproduces these results offline. It merges repeated
histories, retains repeated present IDs at distinct times, prefers completed
records, excludes absent/invalid timings, handles timestamp rollover and scopes
reset counts to the selected interval. Five tests pass; saved full report is
`build-frame/perf-stream-report.json`. These auto-dumps are **fault-triggered
histories**, not an unbiased whole-session FPS or packet-loss survey.

### Additional code finding, staged only

Every INI reload previously marked render distance dirty, even when changing
only a profiler switch, foveation option or unrelated control. Applying model
radius forces the game's visibility sort every tick for 500 ms. The new guard
marks it dirty only for actual ring/radius changes, retaining initial and failed
application retries. This also removes a confound at the start/end of future
profiling captures. It does not fix the independent Link delivery failures or
establish a cure for large native simulation ticks. The candidate cross-build
and 14 report/launcher tests pass; this guard is not runtime/headset verified.

Next session should first establish the restored baseline's delivery behavior,
collecting app timings and Link telemetry together. If resets persist, compare
a fresh streaming session before adding DLL optimizations; then isolate stream
quality/bandwidth if needed. Avoid changing rendering, wait mode, radio settings
and refresh rate together. No further headset testing requested tonight.

## Earlier comparison and its limitations

Linux, Proton Experimental, SteamVR, Steam Frame at 90 Hz; offline Nekro in
town. Owner confirmed the installed candidate's image looked normal, then
reported definite hitches while running. No game graphics settings or
saves were modified. Resolution stayed 100%, shadows/AO 4, DLSS off, fixed and
eye-tracked foveation on, fog end 200.

Each row contains 360 stereo pairs with `profile_gpu=0`. `profile_hooks=0`
except the attribution row. Total includes game update, eye rendering and XR
pacing/waiting; reciprocal throughput is not a compositor drop/reprojection
measurement. GPU profiling was deliberately disabled because it forces the
completion fence being compared.

| Capture / sequence | Wait deferred | Model radius | Mean pair ms | Eye passes ms | Pair p95 ms |
| --- | --- | --- | ---: | ---: | ---: |
| Previous DLL / 10101 | No | 300 | 31.38 | 18.03 | 35.40 |
| Candidate / 10102 | No | 300 | 31.69 | 17.57 | 34.96 |
| Candidate + thread counters / 10103 | No | 300 | 31.72 | 17.42 | 35.28 |
| Deferred wait / 10104 | Yes | 300 | 24.60 | 17.74 | 32.31 |
| Shorter radius / 10105 | Yes | 210 | 22.70 | 15.33 | 27.41 |
| Radius repeat / 10106 | Yes | 300 | 22.53 | 15.38 | 26.41 |
| Radius repeat / 10107 | Yes | 210 | 22.46 | 14.75 | 25.87 |

The code-only comparison did **not** demonstrate increased frame rate. It also
was not an exact scene match: baseline hero position was (8791.1,11261.1),
candidate (8794.6,11255.8), and head direction changed. Launcher polling changes
were already used by both runs, so this does not measure that improvement.

Deferred waiting produced the clearest mean-cycle reduction in this session,
but was subsequently rolled back after the owner's severe-stall report. Candidate and
deferred-wait views were close (125–127 and 128 degrees). The explicit fence
fell from 4.29 ms to zero; `xrEndFrame` grew from 0.68 to 2.68 ms and
`xrWaitFrame` fell from 7.19 to 2.00 ms. This is improved overall pacing, not
7 ms of eliminated rendering work. Image copies still wait before allocator
reuse and shutdown.

The first radius comparison kept nearly the same view (128–129 degrees) and
reduced eye-pass time by 2.40 ms. During the repeats head direction changed to
154–163, then 118–155 degrees. Those repeats cannot establish a clean percentage
gain from radius alone. Fog coverage does not prove all distant models/shadows
are invisible; walk/turn and check silhouettes before calling this verified.

## What the worker counters found

Sequence 10103 covered 18 threads including input. Roughly 19,291 pipeline
binds/pair reached 17 render threads, but only 277 VRS pipeline-map lookups/pair
remained after the existing same-pipeline fast path and new generation cache.
It counted 1,368 guarded memory reads/pair. Only two memory writes/pair occurred
in this scene (~23.8 KB); none of those worker buckets reached a timing sample.
The reporter displays missing samples as `n/a`, never as evidence of free work.

The input tick's sampled inclusive wall time was 9.81 ms/pair, including waiting
and work. The transport lock still spans blocking XR/fence calls. This is a
useful follow-up target, but the number is not exclusive CPU time or a measured
saving available from lock removal. Worker/hook scopes overlap and nest; do
not sum their estimates into a frame cost.

## Candidate changes and validation (now rolled back)

- Generation-aware thread-local pipeline caches and native SRW callback locks.
- Guarded local copies instead of writes routed through wineserver; no page
  permission changes. Invalid, freed, read-only and crossing-page buffers fail
  without entering application exception handlers in the Proton test.
- Bounded per-instance portal classification reused within particle batches,
  retaining mixed portal/nonportal lanes and the verified return-portal math.
- Sparse routine CPU-time sampling; idle mouse cursor queries skipped.
- Optional UI tracing decided at startup, with no live callback-list mutation.
- Full launcher process scans every five seconds during play; known-game PID
  identity/exit checks still run every half second.
- Bounded per-thread hook/memory diagnostics with backward-compatible reports.

The DLL cross-build, host/Proton counter/cache/lock tests, Proton memory tests,
portal tests, five launcher tests and four report tests passed. An isolated
64-byte copy microbenchmark improved from 15.2 to 0.14 microseconds on one
thread. This does not imply an equivalent game frame-time saving.

Candidate SHA256:
`a3bc1aeacb1859144d160c3169347b4f8c6286234b518b6bc08885c7cfdd414e`.
Previous DLL:
`16e24f5564a822531682629a5753c0b5ef248ef48dc33f0fbe358e1e30ed15fc`.
Local ignored evidence: `build-frame/perf-*.log/.json/.csv`,
`build-frame/perf-comparison.json`, and
`build-frame/latest-perf-headless-deployment.json`. The deployment's ready and
backup directories retain both DLLs and settings. Replace DLLs only after
Save & Exit. Settings-only rollback is `defer_gpu_wait=0`, `model_radius=300`.

Shadow quality 2, transport lock separation, a dedicated XR frame thread,
shared inter-eye work, different eye image dimensions and 96/108 Hz remain
untested. Source defaults were not changed by the owner's live settings trial.

## Movement follow-up

The owner ran for three further 360-pair captures with the original candidate,
deferred waiting on and radius 210. These are separate moving scenes, not a
matched static A/B.

| Sequence | Mean pair ms | p95 ms | Maximum ms | Input stage over 4 ms |
| --- | ---: | ---: | ---: | ---: |
| 10108 | 22.94 | 24.88 | 197.56 | 4 |
| 10109 | 22.82 | 28.22 | 35.88 | 8 |
| 10110 | 23.45 | 32.22 | 36.54 | 8 |

The 20 input-stage spikes were typically 12–13 ms. CPU/GDI painting of the
1024-square interaction atlas still ran synchronously in that stage. A local
Proton test measured 10.427 ms per repaint with actual artwork and icons.
The new candidate paints on a bounded worker and uploads completed pixels on
the frame thread, with a fast RGBA row copy. No game pointers, D3D12 objects or
XR calls are passed to the worker. Changed skill/character/page contents cannot
be clicked using an obsolete image. Vitals and hover refreshes retain the wheel.

Host/Proton tests cover latest-only work, failures, shutdown/restart and stale
UI contents. Twelve worker-painted images exactly matched the synchronous
images with varying pages, hover and vitals. Installed after Save & Exit:
`d44762d4cc4a9b2cefad350a6ada3b6779b3c9bf83211be835e7b1430b28442f`.
Record: `build-frame/latest-perf-ui-deployment.json`. Subsequent moving captures
10111–10113 had **zero input stages over 4 ms in 1,080 pairs** (maximum 1.811 ms).
Two HUD refreshes measured background paint 10.12/10.28 ms and frame-thread
upload 1.27/1.16 ms. Mean pair times were 23.21, 23.62 and 22.46 ms; the routes
and views differ from the earlier runs, so no overall FPS gain is inferred.
Evidence: `build-frame/perf-running-ui-{1,2,3}.log/.json` and
`build-frame/perf-ui-comparison.json`. Headset wheel/wrist usability feedback
is pending. The owner subsequently reported severe global stalls/quality loss,
and this candidate was rolled back; the narrow UI result does not establish
overall smoothness.

The 198 ms pair included a 173 ms LevelTranslationLayer simulation tick;
other traversal ticks in the live log reached 208–753 ms. Their recorded
pipeline creations, measured room discovery and extra VR tiling were zero.
The remaining game-update cost is unresolved; native room/model activation
or loading is a follow-up hypothesis, not a demonstrated diagnosis. The
asynchronous atlas change does not address these larger stalls.
