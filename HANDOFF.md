# Agent handoff — D2R PCVR

Updated 2026-10-10. Read [INSTALL.md](INSTALL.md) for deployment and
[steamvr/TASKS.md](steamvr/TASKS.md) for the complete user-requested backlog.

**Native Windows setup is complete. Start with
[WINDOWS_HANDOFF.md](WINDOWS_HANDOFF.md)** for exact package choice, installation,
Steam launch commands, controls, baseline settings and the diagnostic sequence.
PR #1 is merged into `main` at `7b69ce7`. Current work continues on
`vr/quest-2-3-support`; see [steamvr/QUEST_TESTING.md](steamvr/QUEST_TESTING.md)
for the Quest 3 Steam Link test and separate Virtual Desktop/Quest 2 coverage.

**Release:** `v0.3.0-windows-quest-preview`, on `vr/quest-2-3-support`, packages
`e12a5e95...` with [INSTALL-WINDOWS.md](INSTALL-WINDOWS.md) and two launch
shortcuts. The final change removes Journal's wheel label/icon, hover and
dispatch; native tabbed menu access remains. All 34 native tests, 24 Python
checks and the generated wheel preview check pass. No new headset session
was run after this wheel cleanup; earlier runs have exact hashes below.
The release DLL is installed locally with INI/video hashes preserved and the
previous DLL backed up. Game remains closed. Deployment record:
`build-frame/windows/latest-windows-release-deployment.json`.

**Owner-confirmed VDXR color build:** `109dfa0e...`, installed at 18:35 PDT. The first
Quest 3 VDXR run reached active tracked gameplay, but the owner reported washed-out
colors. The candidate selects sRGB for copied SDR images even when VDXR also
offers linear UNORM. All 34 native tests pass, including a GPU color regression
that fails with the old selector; typeless sRGB potion stereo/readback also
passes. INI/graphics hashes are preserved and `f559b3e5...` is backed up. The
owner confirms "Yes — colors look normal" after the correction. The run logs
45 accepted native melee reports, zero position mismatches and normal unload
at 18:46:00.553. Game is closed.
See [QUEST_TESTING.md](steamvr/QUEST_TESTING.md) and local deployment record
`build-frame/windows/latest-vdxr-color-deployment.json`.

**Owner-verified Steam Frame/Steam Link build:** `f559b3e5...`, installed at 17:55 PDT. The owner
reports "it all works" after the offline cave retest. Logs confirm accepted
free-swing melee contacts in `act1_caves` without the old position mismatch,
followed by return outdoors and normal game exit at 18:02. Current code includes
T-pose body calibration, dominant-trigger shoulder weapon swapping, native
Windows setup, dialogue focus and clipping/belt/wrist fixes. All 33 native tests,
21 Windows Python tests and both 17-call NGX ABI target checks pass. Use SteamVR
streaming to Steam Frame, local offline characters only. The owner now also
confirms Quest 3 + Steam Link/SteamVR works with the same DLL. Calibration,
outdoor melee and normal exit at 18:16:57 are logged; see
[QUEST_TESTING.md](steamvr/QUEST_TESTING.md) for coverage limits. Virtual Desktop
Streamer 1.34.22 is installed and its stuck service repaired. The owner has
connected Quest 3; the full VDXR graphics probe passes on the RTX 5090 with
VIEW/LOCAL/STAGE spaces. The first game log confirms VirtualDesktopXR, an active
outdoor session and head/both-hand tracking; the color regression is described
above. SteamVR remains closed. `--frame-vr --frame-vdxr` selects VDXR per launch without editing
the system runtime or normal Steam Play; 24 Python tests pass. A controlled native
baseline/candidate performance test and physical Quest 2 test remain pending.

### Earlier Windows debugging record

**Windows setup is now installed locally.** The owner's stock Steam D2R and
Frame dashboard work. Owner verified offline VR gameplay with the baseline,
reporting good subjective performance and a missing torso/belt.
Akara dialogue then froze both desktop and headset. A saved dump points to
synchronous window activation re-entering native game locks. Native performance
candidate `9b3ce9df…` included deferred window activation; its session logged
Akara opening/closing twice and normal Save & Exit. Owner reports performance
is superb, and now requests rendering/dungeon-transition fixes. Follow-up DLL
`996cc430…` fixed belt rendering in the headset, but cave diagnostics showed
fresh UI snapshots with expired native local identity. Current DLL `5847a204…`
refreshes identity from the checked UI-thread local-player accessor and keeps
server position/target/wall checks. Graphics are preserved; belt_forward is
0.06 m for closer placement. Owner confirms cave wrist values now work, but
cave swings still fail: callbacks run and reject a player-position mismatch.
Normal-posture captures plus the owner's clipping-volume observation exposed a
malformed first camera section from the installer. Win32 ignored the real near
setting and used 1.5 world units. Correcting the installed INI live restored 0.2;
the owner confirms torso, arms and shield clipping are fixed. Installer parsing
and a Win32 profile API regression test are fixed too (five installer tests pass).
The SteamVR probe recommends 2160×2160, but actual game eyes are 3840×2160.
User additionally requested first-load/body-menu T-pose calibration and dominant
trigger swapping over either shoulder in place of hip anchors. Build `e27084ed…`
was installed at 17:39 PDT with these features; headset validation is pending.
The owner confirms calibration saves/closes and the VR settings entry works.
Initial resting arms caused a
misleading floor/setup message; corrected DLL `a1e2006a…` was installed at 17:43
after normal exit with 32 native tests passing. Current measurements are
eye height 1.64763 m / grip span 1.50561 m. The next SteamVR run started at 17:49.
Calibration persists stage eye height and grip span and fits per-rig arms while
keeping controllers at physical scale. See steamvr/BODY_CALIBRATION.md. Cave
swings reproduced in the cave. Paired native/render samples show terrain-height
shear on both horizontal axes (sqrt(3/2) times height). Candidate `f559b3e5...`
removes that shear for the unchanged 0.65 m warp check and adds it to enemy contact
capsules. All 33 native tests pass, including the actual captured rejections,
contacts/misses and teleport rejection. Installed at 17:55 after normal exit;
previous DLL/INI/logs/saves backed up and current INI/graphics preserved. SteamVR
offline retest completed; owner reports "it all works", with native cave contacts
and normal exit verified in logs. Latest installed deployment:
`build-frame/windows/latest-cave-coordinates-deployment.json`.
Portable Windows development tools build the candidate natively; 33 C++ tests,
21 Windows Python tests and 17-call NGX ABI checks pass. See the current setup
section in WINDOWS_HANDOFF.md for backups, exact Steam launch routing and the
explicitly authorized loader-file Defender exclusions. Historical statements
below that Windows was untested describe the earlier Linux handoff. Native
baseline/candidate performance measurements remain pending. Latest deployment:
`build-frame/windows/latest-identity-deployment.json`; dump/backtraces:
`build-frame/windows/akara-freeze-20261010-164318/`.

## Objective and boundaries

First-person Diablo II: Resurrected PCVR. Original user reference was
`emmericp/D2R-3D`; the adapted implementation is MIT upstream
`natpoh/diablo2-vr-port`, commit `10cfb0a2a9080184101794d3ac8c238c35717967`.
This repository retains upstream history/notices and adds a native OpenXR
transport, without BodyWalk/FlatVR as runtime dependencies.

Owner tests on Linux with Proton Experimental, SteamVR and Steam Frame. His
offline character is **Nekro**. Friend has Quest 3, likely Linux with **WiVRn**;
Windows with Virtual Desktop is also possible. Both friend paths are untested.
The instructions deliberately identify that rather than promise compatibility.
Earlier DevilutionX standalone/tabletop and first-person work is parked and is
not part of this repository or deployment.

**Offline single-player only.** Never use modded online characters, bypass license
verification, change the stock executable, remove equipment-bearing corpses or
overwrite progress. Stock Steam Play must stay available for legitimate online
license refresh; the explicit `--frame-vr` launch chooses D2RLoader. Save and exit
before replacing DLLs or restoring files. The installer checks for running D2R.

## Latest user evidence and priorities

**Windows migration / commit-and-push handoff, 2026-10-10 afternoon.** User wants
a fresh Windows installation of Steam/D2R and this mod, continuing Steam Frame
PCVR. Explicitly requested all current source, findings and documentation
committed/pushed, with enough instructions for a fresh agent. No more Linux
headset testing is needed for this handoff. Native Windows is still untested.

After a full system reboot and fresh SteamVR, the unchanged baseline DLL again
stalled and lost picture quality; user answered **"Still happening"**. The fresh
driver log recorded **119 reset requests before D2R launched** at 15:41:22.
Sequence 10202 (360 pairs) averaged 22.932 ms, p95 30.004 ms, max 35.161 ms;
that short sample does not encompass all stalls. Pre-reboot restored-baseline
sequence 10201 averaged 23.759 ms, p95 31.631 ms, max 37.521 ms. These moving
scenes are not exact matches. No DLL/game graphics change occurred between them.

An OpenVR background utility then set SteamVR Link to manual 50 Mbps for an
isolation test. **No test result was received** before the user chose Windows.
Restored `automaticBandwidth=true`, `targetBandwidth=200` and verified readback.
All profiling is off. Do not claim 50 Mbps fixed anything, or carry this
temporary test into fresh Windows defaults. Local evidence is named in
`build-frame/latest-post-reboot-baseline.json`; portable aggregate evidence is
committed as `steamvr/evidence/2026-10-10-handoff.json`. Raw logs, game snapshots,
assets and saves remain excluded from GitHub.

Private release tag `v0.2.0-windows-handoff` provides two installation packages:
`d2r-pcvr-baseline.zip` with archived `16e24f55…` DLL and
`d2r-pcvr-candidate.zip` with current-source `bc51fd37…` DLL. Start Windows with
the baseline; candidate adds the CPU/HUD changes and INI reload guard and is
not headset verified as a whole. Both include the native Windows OpenXR loader,
current installer/launch scripts, controls, handoff and checksum manifests.
The baseline manifest explicitly distinguishes packaging source from archived
plugin provenance. The old v0.1 preview predates current gameplay features.

Handoff verification: DLL cross-build, 24 Python tests and eight native test
executables under Proton passed (memory, hook metrics/cache/locks, worker,
portal billboards, interactions, spell aim, gameplay, quick-cast). Native
Windows execution, streaming behavior and all pending user checks remain open.

**Baseline retest, 2026-10-10 15:34; owner rebooting PC.** Owner returned,
connected Frame and tested the restored `16e24f55…` DLL, wait=0/radius=300.
He confirms **"Still stalls and loses image quality"**. Streaming reset bursts
returned, including 71 requests in one observed second. Post-load hitch was
465.7 ms (443.5 ms game gap); another was 147.1 ms (119 ms XR begin, only
2.1 ms game gap). XR begin includes input/other work; do not attribute all of
it to the runtime without the finer profile. Sequence 10201 completed; saved
`build-frame/perf-baseline-retest-moving.log/.json` (GPU/hooks off; capture now
off). Full test logs/stream histories are under the directory named in
`build-frame/latest-baseline-retest.json`. Current sampled PC load was light.
No DLL, graphics, SteamVR or streaming settings were changed for this retest.
The owner said **"let me reboot the system"**; stop testing until he returns.
Next: fresh post-reboot baseline comparison with both game and streaming logs.
Do not install the staged performance candidate first.

**Regression investigation / baseline restored, 2026-10-10 02:35.** Owner now
reports severe random stalls in movement/combat/menus, with the picture becoming
very low quality, stuck frames and catching up. He has gone to bed and explicitly
requested work **without further headset testing**. Game was already closed;
do not relaunch or ask for another capture tonight. Earlier average pair timings
do not establish smoothness or excuse this report.

Restored pre-performance DLL SHA256
`16e24f5564a822531682629a5753c0b5ef248ef48dc33f0fbe358e1e30ed15fc`, plus
`defer_gpu_wait=0`, `model_radius=300`, all profiling off. This includes the belt
follow and spell aim work; those user tests are still pending. Other settings,
game graphics, assets, saves, SteamVR and network settings were preserved.
Game remains closed. This is a conservative comparison baseline, **not a verified
fix**; first-CPU/async-HUD source and DLLs remain intact for later comparisons.
Launcher polling optimization remains in the wrapper. Full pre-rollback logs,
DLL, INI, graphics settings and streaming histories are in
`build-frame/backups/perf-regression-20261010-023124`; current-state record is
`build-frame/latest-perf-regression.json`. This supersedes the two performance
deployment records below; neither candidate is currently installed.

The crucial new evidence is SteamVR Link, previously missing from the frame
profiles. Ten saved fault histories yield 10,711 deduplicated driver rows over
02:20:54–02:24:53 and **147 stream reset requests**. Driver-aligned
send-to-reported-receive p95 is 80.81 ms, maximum 734.13 ms; median 7.77 ms.
Encoding p95 is 8.87 ms, maximum 12.37 ms. For a 674.95 ms receive event, gate to
app submit was 16.40 ms and encoding 7.49 ms. This strongly implicates delivery/
receiver behavior in the freezes, beyond game-render cost. It does not prove
radio interference versus driver/client handling, or measure actual resolution/
bitrate. Reset requests also occurred at 01:52–01:54 **before** the CPU candidate
and deferred-wait trial. Game-side stalls remain independently documented.
Current network inspection found adapter power saving already off; kernel logs
in the investigated interval show no GPU fault/USB reset. No network changes.

Added read-only `steamvr/stream_report.py`, tested for overlapping histories,
missing timestamps, 32-bit clock rollover and separate stage accounting.
Report: `build-frame/perf-stream-report.json`. These histories are fault-triggered,
not an unbiased session FPS/packet-loss benchmark. See PERFORMANCE.md.

One additional source fix is **built/staged only**: unrelated INI reloads no
longer mark render distance dirty, avoiding a forced 500 ms model visibility
re-sort when profiling or changing unrelated VR settings. Actual ring/radius
changes and pending initialization/retries still apply. Cross-build and 14
report/launcher tests pass; this small native scheduling change is not runtime
verified. `build-frame/latest-perf-offline-stage.json` identifies the new DLL;
`build-frame/d2rl-vrcam.dll` is this staged candidate, **not the installed baseline**.
Do not blindly install it over the baseline before the next comparison.

Next session: establish baseline delivery stability first, collecting game
timings and Link histories together. If resets persist, compare a fresh SteamVR/
Frame streaming connection before adding mod optimizations, then a controlled
stream-quality/bandwidth test if needed. Change one variable at a time; do not
raise display refresh while delivery is failing. No new commit/push requested.

**Movement hitches / asynchronous HUD, 2026-10-10 02:20.** Owner reports
definite hitches while running with wait=1/radius=210. Three additional
360-pair movement captures, sequences 10108–10110, are saved as
`build-frame/perf-running-{1,2,3}.log/.json`. They show 20 input/UI spikes above
4 ms (typically 12–13 ms), plus one 173 ms LevelTranslationLayer simulation
tick / 198 ms stereo pair. Earlier live movement logs include simulation ticks
208–753 ms. These large stalls recorded no graphics pipeline creations and
no measured room discovery/extra VR tiling time. Their remaining game-update
cost is not yet isolated. Do not claim all movement hitches fixed or identify
shader compilation as the cause.

Installed SHA256
`d44762d4cc4a9b2cefad350a6ada3b6779b3c9bf83211be835e7b1430b28442f`
after Save & Exit; launched through the Steam wrapper. Record
`build-frame/latest-perf-ui-deployment.json`, ready/backup tag
`perf-ui-20261010-021749`. Runtime verified; headset wheel/wrist feedback pending.
CPU/GDI interaction-atlas painting now runs on a bounded worker (one active,
one replaceable queued job, one result). Inputs are owned snapshots; the
worker touches no game memory, D3D12 or OpenXR objects. Rendering polls for
matching results, uploads RGBA rows with memcpy, and retains the previous image
while painting. Old skill pages/character/slot contents cannot be displayed or
clicked as new contents; hover/vitals-only changes do not hide the wheel.
Shutdown joins the painter before stopping the icon worker or destroying XR.

Host/Proton worker tests cover coalescing, stale results, failure, restart,
shutdown and UI-content guards. Twelve real GDI images with changing vitals,
wheel pages and hover positions exactly match synchronous results under
Proton. Synchronous painting measured 10.427 ms on average, consistent with
the input spikes. Three new moving captures 10111–10113 show **zero input
stages over 4 ms in 1,080 pairs**, maximum 1.811 ms. Two measured refreshes
painted for 10.12/10.28 ms on the worker and uploaded for 1.27/1.16 ms on the
frame thread. Aggregate: `build-frame/perf-ui-comparison.json`; logs are
`build-frame/perf-running-ui-{1,2,3}.log/.json`. Routes/views differ, so this
does not establish an overall FPS improvement or eliminate native stalls.
DLL cross-build and diff checks pass. Profile captures log
`Frame UI refresh: background paint ... frame-thread upload ...` once/second.
Subsequent owner feedback reports severe stalls/quality collapse; this candidate
and wait=1/radius=210 were rolled back as described above. Wheel/wrist checks
remain pending. The larger game-update stalls remain a separate unresolved task.

**CPU performance pass, 2026-10-10 02:03.** Installed candidate SHA256
`a3bc1aeacb1859144d160c3169347b4f8c6286234b518b6bc08885c7cfdd414e`
after the owner Saved & Exited, then launched through `steamvr/run-proton.sh`.
Record: `build-frame/latest-perf-headless-deployment.json`; ready/backup tag
`perf-headless-20261010-015506`. This supersedes the spell-aim DLL below and
includes all pending spell/belt changes. Owner confirmed **"Image normal;
standing still"**. Startup confirms focused XR, both hands/gaze and gameplay.
Walking/menus with the final performance settings are awaiting owner feedback.

Implemented optional all-thread counters; generation-cached VRS/UI pipeline
lookups and SRW callback locks; guarded local memory copies instead of
wineserver writes; bounded per-instance portal batch classification; sparse
routine GetThreadTimes; no idle mouse cursor query; startup-only UI tracing;
and 5-second full process scans with 0.5-second known-game PID checks.
DLL cross-build, Proton memory/exception tests and benchmark, host/Proton
counter/cache/SRW tests, portal mixed-batch tests, five session tests and four
report tests pass. The 64-byte local-copy benchmark was 15.2→0.14 us on one
thread; this is not a measured frame-time saving.

Seven 360-pair captures are saved as `build-frame/perf-*.log/.json/.csv`,
with aggregate `build-frame/perf-comparison.json`. GPU profiling was off in all
runs; all-thread counters were off except sequence 10103. Timing results and
limitations are in [steamvr/PERFORMANCE.md](steamvr/PERFORMANCE.md). Code-only
A/B: 31.38→31.69 ms/pair, **no demonstrated frame-rate gain**; viewpoints and
hero positions differed slightly. In the candidate, deferred waiting reduced
31.69→24.60 ms/pair at nearly the same view, and radius 210 then gave 22.70 ms.
Later repetitions held around 22.5 ms but changed head direction, so they do
not establish a precise radius benefit. Do not compare against the owner's
earlier 22.7 ms review scene as if it were the same baseline.

Historical trial configuration: `defer_gpu_wait=1`, `model_radius=210`, fog end 200,
100% resolution, shadows/AO 4, DLSS off, fixed/eye-tracked foveation on.
`profile_capture=0`, `profile_gpu=0`, `profile_hooks=0`. Graphics JSON was not
edited. Baseline mod settings were wait 0/radius 300; saved in the backup.
Source defaults are unchanged. All-thread capture counted ~19.3k pipeline
binds/pair on 17 threads, only ~277 VRS map lookups/pair, and ~1,368 memory
reads/pair. Input-tick inclusive sampled wall time was 9.8 ms/pair; this includes
blocking/work and overlaps drawing. Do not sum worker estimates as frame cost.
Only two memory writes/pair occurred in this scene; none reached a timing
sample on their worker, so their time is unknown, not zero.

Next: owner movement/menus/scenery feedback; shadows 4→2 is untested. Transport
mutex separation/dedicated XR calls, shared inter-eye work and aspect changes
were intentionally left for separate ownership/lifecycle work. No code was
committed/pushed for this pass. Keep the source/installed DLL distinction when
continuing; only the reporting script/docs changed after this DLL was built.

**Casting-weapon projectile aim, 2026-10-10 01:24.** Owner wants projectile
spells to follow the wand/staff and selected **Teeth** for the first check.
Installed SHA256
`16e24f5564a822531682629a5753c0b5ef248ef48dc33f0fbe358e1e30ed15fc` while D2R
was closed. Record: `build-frame/latest-spell-aim-deployment.json`; ready/backup
tag `spell-aim-20261010-012445`. Includes the belt-follow changes below.
Previous DLL was `58ce0b16…`; settings and assets unchanged. Owner replied
**"I'll test later"**; game left closed. Runtime/headset verification pending.

`skeletons` now publishes the final posed casting shaft in its captured world
frame (wand/scepter/orb +Y, staff's actual chosen shaft/sign); `HandRay` uses it
for selected spells instead of the controller menu-pointer direction. It checks
pose age, hero generation, weapon type and turn epoch; other equipment keeps
controller aim. Nearly vertical shafts do not invent a horizontal heading.
`native_ui` reads compiled Skills flags/server missile/function into each native
slot's `directed` field. Teeth's fan is recognized without a primary missile;
ground placements, buffs, summons and required corpse/unit targets stay native.

Fresh, active wheel/quick-cast inputs can open a thread-local scope around native
cast resolver `0x190440`. Only its exact selected-unit call sites return no
target; its final aim-assist flag is disabled within that call. Its AttackPoint
call uses one sampled shaft direction at 60 grid cells, reducing quantization.
Nested calls restore scope. This does **not** add hand-origin/elevated 3D spell
flight or alter native spread, homing, timing, effects, resource costs or damage.
The native spell plane limitation was explicitly explained to the owner.

DLL build and spell/interaction/quick-cast/gameplay host tests pass.
`steamvr/audit_spell_aim.py` verifies all seven guarded native entry/call sites
against local `build-frame/game-code.bin`, and optional local table inputs verify
all 429 compiled Skills rows. Local game snapshots/tables remain ignored.
New files: `steamvr/spell_aim.h`, `spell_aim_test.cpp`, `audit_spell_aim.py`,
`vr/spell_aim_signatures.h`. No commit/push requested for this change yet.
Next: launch offline, select Teeth, aim wand left/right with head looking
elsewhere and an off-axis creature selected; check the fan follows the shaft.
Also test quick-cast and a summon/corpse skill. Logs: `Frame spell aim:` for
hook readiness/owned resolution, `Frame wheel assignments: ... directed=0xNN`
for native slot classification. Preserve the pending full 3D spell adapter work.

**Belt follows body, 2026-10-10 01:05.** Owner requested the belt always stay
attached in front while moving/turning. Removed the estimated torso's 37-degree
heading deadband, slow catch-up and hand-proximity/grab freezes. `UprightBody`
now uses tracked head position and horizontal facing each frame, with no pitch
or roll; nearly vertical gaze preserves the previous reliable heading. This is
a headset-derived body frame, not a new skeleton/torso-tracker dependency.
The shared hip holsters also follow; pull distance is now body-relative so
turning/stepping with a hand at the hip does not itself swap equipment. Held
bottles remain hand-attached and return to the current waist if unused.

Host interaction regressions cover lowered hands, small and large turns,
translation/crouch, downward gaze/roll, vertical-gaze stability, grab/move/return,
seated reach and holster draw rejection/acceptance. Existing interaction tests
and DLL cross-build pass. Installed while D2R was closed; SHA256
`58ce0b166d7c96027aa45fb14eed53518dd146f831e220c60a3b85b4dca32dd7`.
Record: `build-frame/latest-belt-follow-deployment.json`; ready/backup tag
`belt-follow-20261010-010546`. Previous DLL was the potion build below. No settings
or assets changed. **Not relaunched yet; runtime/headset verification pending.**
Next: load an offline character with belt potions, turn/step/crouch with lowered
hands, grab/drink, turn while holding, then release an unused bottle.

Owner also asked how to stock larger belts and drop inventory items. Explained
four grab columns with native reserve rows/counts; Inventory supplies the items.
Drop mapping is already Left Trigger + right laser/Right Trigger (Ctrl-click).
Native context decides drop vs vendor sell/stash transfer; no new input change.
Ground dropping through this controller-layout panel is **not headset-verified**.

**Original 3D potion belt, 2026-10-10 00:39.** Owner requested actual potion
models on the belt, grabbed/drunk with Right Grip, and is ready for a test.
Implemented and installed SHA256
`5f4f3987677ac8f29fb272fb1a918d5d8e5123232e9a9b0b1328cccd9fbe9979`, then
launched with the offline wrapper. Record:
`build-frame/latest-potion-models-deployment.json`; backup/ready directories
use tag `potion-models-20261010-003944`. No settings, original executable or
character saves changed. **Headset verification pending.** Runtime log at
00:40:03 confirms all 15 models loaded and the D3D12 XR session ready/focused
(3840×2160 per eye, PID 117004). Owner was asked to test visibility, grab and sip.

`prepare_potions.py` resolves all 15 potion codes through the game's own HD item
dictionary and unit definitions. `potion_assets.py` reads bounded Granny 7/LE64
mesh streams and BC3 mipmaps. MIT standalone BitKnit decoder provenance is in
THIRD_PARTY. All original models/textures stay in the local `d2r_vr_potions`
cache; no Blizzard assets are shipped. The installer prepares/backups/restores
this cache and packaging includes the reader/decoder, never the cache itself.

`potion_models.cpp` loads/uploads once per XR session and draws original textured
geometry into each eye after its world copy, using that eye's actual pose/FOV.
There are no added per-frame GPU waits or asset reads. Bottles use their own
depth buffers; world/arm occlusion, native scene lighting and transparent liquid
are not implemented. Original albedo plus simple diffuse/specular lighting is
used. Cache failure retains flat icon previews. All variants normalize to 18 cm
height about the neck axis; the opening anchor is shared with `potion_pose.h`.

`potion_scene.h` places the next native item in each occupied waist column and
moves the exact grabbed item to its grip, without duplicating it on the belt.
Right Grip is the documented control; existing left-hand support remains.
Nearest occupied slot wins overlapping reach spheres. Opening must reach within
10.5 cm of the mouth while tipped for .25 s. Release returns an unused bottle;
native removal acknowledgement darkens it. Exact item/session/revision guards,
refill cancellation, refusal without retries and one use per grab are retained.

Checks: 15-asset conversion; synthetic parser/range/mip/normalization tests;
installer rollback including preexisting cache; interaction ownership, closest
slot, opening-vs-controller contact, interrupted sip, refusal and single use;
quick-cast/motion/native-panel regression tests; DLL cross-build; D3D12 GPU
stereo render/readback with correct disparity. Inspected synthetic preview:
`build-frame/potion-models/preview.png` (not a game/headset screenshot).
GPU test uses `potion-models-test.exe` in isolated `build-frame/ui-preview-prefix`
with local Proton native d3d12/d3d12core/dxgi and Microsoft's d3dcompiler_47 DLLs.
No game memory capture is needed to test this feature. Next: verify reach, grab,
tip/drink, native belt count and vitals, return/cancel and menu recovery in Frame.

**Buy popup verified, 2026-10-10.** Owner confirmed "it works" after installing
the laser-focus correction. Native `FocusManager::UpdateFocus` first hit-tested
Yes/No correctly, then a separate controller cursor-snapping pass replaced the
hover with the vendor grid underneath. This explains why precise head angles
occasionally made Yes clickable. `vr/panel_input.cpp` now hooks the separately
signature-guarded snap getter (RVA `0x14f1d0`, focus call site `0x867bd0`) and
suppresses snapping only inside owned laser-pointer callbacks. Native controller
layout, controller navigation and skill-binding contexts keep their behavior;
no camera or purchase logic changed. `steamvr/panel_input_mode.h` holds the
policy; `steamvr/native_panels_test.cpp` covers Yes/No, misses, nested callbacks,
assignment contexts and unowned input.

Native panel regression tests and DLL cross-build pass. Both signatures matched
the game snapshot and live executable. A bounded live comparison reproduced the
original vendor-grid overwrite and retained Yes/No with the correction. A native
No click successfully cancelled the popup; the assistant made no purchase.
After the owner saved/exited, installed SHA256
`6cffb3120559942472b80d2af8a525628d5b2e555b42c763fe77da81717be7a6`
and relaunched through the offline wrapper. Deployment record:
`build-frame/latest-buy-popup-deployment.json`; previous DLL/config backup:
`build-frame/backups/buy-popup-focus-20261010-000433`.
The owner's latest headset confirmation verifies the Buy-popup correction.

**Quick-cast shortcuts verified.** Owner explicitly confirmed the combinations
work. Wheel/editor show persistent native badges and VR chords. Holding Left
Trigger maps Right A→native B, Right B→X, Left A/X→Y, Left B/Y→RB;
adding Left Grip selects the corresponding native LT variants. `skill_slots.h`
centralizes those mappings; all eight shortcuts and alternate-slot editor paths
have automated coverage. Input ownership prevents overlapping menu, melee,
bow-release, belt or holster actions. `SpellSelected()` includes the active
quick-cast skill even when WEAPON remains selected in the wheel. Broader
assignment/reload verification remains on the task list. Deployment history:
`build-frame/latest-quick-cast-deployment.json`.

**Trade background verification remains pending.** Shared opaque UI conversion
and quad-only composition now cover Vendor/Trade/Gamble as well as the tabbed
menu. Native panel and D3D12 black-backing/color tests pass; the change is included
in the installed DLL. The latest Buy-popup confirmation does not separately
establish that both trade backgrounds are black. Ordinary NPC speech and quest
panels retain their existing composition.

**Body/shoulder flicker investigation, 2026-10-09 22:59.** Owner reports
lower-left geometry flashing in the left eye even standing still with hands
lowered, after tracked-neck alignment centered the body. Bounded read-only
`PoseBody` trace on Nekro captured 100 poses / 50 stereo pairs. Six left-eye
poses had `followTrackedNeck=false` while their right-eye partners had it true;
both eyes had identical center-eye model coordinates and input sample stamps.
This establishes a mode mismatch, not the initially suspected stale camera
position: `d2rcam::Refresh()` already rebuilds the camera before each native draw.
`PushArms` was sampling `framevr::Active()` on its 10 ms timer. Active is false
between pairs, so a timer publication there disabled body alignment until the
next tick, sometimes between eyes. The candidate publishes stable neck-follow
intent and checks current frame availability in the skeleton hook, after the
fresh eye/head callbacks. Invalid tracking, panel/non-world frames and inactive
camera modes still gate alignment. No anatomical, arm IK, head hiding, camera
or near-plane changes. Cross-build and existing body/portal math regressions
pass. Owner saved/exited; verified no game process, backed up the previous DLL,
installed SHA256 `bb460986137a88040cc4e720f4871a3a8995dd05bcc9127c8bc81dab7662d54e`
and relaunched through the normal offline VR wrapper. Deployment record:
`build-frame/latest-body-pose-deployment.json`. Local trace/scripts:
`build-frame/body-stability/`. Post-install read-only trace on Nekro captured
160 poses (80 per eye), all with neck-follow enabled. All 79 complete pairs
matched in follow mode and center-eye coordinates; two edge poses fell outside
complete pairs. Debugger detached normally. Runtime mismatch is resolved;
owner confirmed **"Flashing stopped"** in the hands-lowered/head-turn check.
This fix is headset-verified on Nekro; broader 6DoF/class coverage remains open.

**Return-portal correction, 2026-10-09 22:45.** Live capture of the separated
town endpoint proved it uses the **same NewStuff asset**, mask 19 and identity
orientation/scale (instance 316, translation 8826.0615/.03284/11326.0615).
Its one-element mesh stream contains `(-2.324593,3.434088,1.808851)`:
horizontal radius 2.94545 and Y 3.43409 failed the earlier horizontal/height
check, but its full 3D distance from authored center `(0,4,0)` is 2.99932.
The two-element stream remains near Y=4/radius=3. Both are the same three-unit
offset around the authored center, with different pitch. Captures are in
`build-frame/portal-billboard/return-portal-live.{txt,json}`. The GDB callback
detached normally on bounded stop; JSON was recovered from printed samples
because batch-mode exit preceded its posted save callback.

`portal_billboard.h::CenterOffset` now recognizes the full 3D radius-three shell
(same .025 tolerance), and removes the vertical offset as well as X/Z by using
the common authored anchor `(0,4,0)`. This generalizes direction/pitch, not the
asset allowlist: other variants, native transforms and particle positions
outside the center shell remain untouched. Fresh-head/menu gating is unchanged;
the earlier possible creation-time explanation was not established by this
capture. Captured outside/return samples, yaw/pitch sweeps and outside-shell
rejection tests pass; all 12 native signatures and DLL cross-build pass.
Stage/verification state: `build-frame/latest-portal-return-deployment.json`.
After owner confirmed Save & Exit, verified no game process remained, backed up
the installed DLL and atomically installed the staged binary with matching
SHA256 `c3d5143337ed8d731341865ce2ca1ba595bed5dd7072dfe2b2f0857219cb7882`.
Launched through the normal Steam VR wrapper. Owner subsequently confirmed
**"ok it all works now"** after the requested outside/town endpoint and head
rotation/tilt check. Mark this town-portal fix headset-verified at both ends;
other cosmetic/portal asset variants have not been separately verified.
No native assets, saves, INI or graphics settings changed.

**Portal endpoint comparison, 2026-10-09 22:33.** Owner verified the second
candidate looks great at the portal created outside town. After walking through,
the linked town endpoint still has separated components. This is partial
validation only. The session exited normally at 22:32, so the return endpoint
has not yet been inspected live. Both existing portal hooks activated at
22:29:16.802 before that exit. Facing currently covers the town-portal directory;
center correction only accepts `NewStuff` and the captured radius-3/height-4
envelope. Do not assume the return endpoint uses a different asset: confirm its
asset, transform and local points first. All five local town-portal particle
variants contain the native view/orientation/transform helpers; the unit JSON
offers NewStuff and Chronicle variants. Neither establishes the live return
endpoint identity. A bounded read-only GDB capture helper is prepared at
`build-frame/portal-billboard/capture_portal.py`; attach to the **current** PID,
source it, then continue. It records effect identity/transforms and input points,
and detaches after bounded samples/calls/time. It has not yet been run on the
return endpoint. No new DLL has been installed for this report.

**Town portal facing, 2026-10-09 evening.** Owner verified the skill editor and
black native-menu backgrounds, then reported town portals tilting with the
headset and an offset center. Requested stereo screenshots captured at 21:31:16
show the dark center left of the bright ring, with a horizontal break in the
effect. Local evidence is under `build-frame/portal-billboard/` (ignored).
Existing screen-quad smoke hook was active. Native town-portal `.particles`
assets additionally use mesh effects and script `view.direction_int_SceneCtx`;
these do not go through the screen-quad alignment hook.

Candidate in `vr/billboards.cpp` intercepts native scripted view direction
`0xc04bb0` and position `0xc05a60`. It resolves each script lane's effect instance
through SceneContext +0x28 (constant) or +0x18/+0x20 (indices), the native scene
pool at +0x230, 512-entry pages, and the instance transform at +0x48. Live
read-only inspection verified this route using the native bonfire effect and
transform. Definition +0x40 → compiled effect +0x48 → path string +0x10/+0x20
identifies town-portal assets only; vtable checks and bounded reads fail closed.
Other camera-facing effect scripts keep their native results. Both portal script
queries use the current stereo pair's shared head position, and direction is
horizontal head-to-effect-origin so the portal remains upright. Native view
position must match the active VR camera. Original authored animation remains.
Ten native signatures, host positional/asset/instance tests and cross-build pass.
Installed with game closed, then launched through Steam; record:
`build-frame/latest-portal-facing-deployment.json`. Fresh-session logs confirm
portal correction activated at 21:48:48.644, but the 21:49:52 and 21:57:41 stereo
pairs still show a displaced center. Owner supplied
`/home/berkybear/Videos/framecorder/2026-10-09_21-49-20.mp4`; the clip confirms
separation as the viewpoint changes. Do not mark this fixed.

Bounded read-only worker traces verified the corrected direction reaches native
`scene.orientation_fu_norm_float3_float3` at `0xbf3c90`, with world-up retained.
The native local-position helper `0xbfdd60`, mask 19, still receives different
three-unit offsets at height four for the same portal instance. Captured examples:
`(2.904415,4.005898,.739582)` and `(.773464,4.005898,-2.897778)`.
The return trace confirms the helper adds the instance's translation. Native
portal meshes were decoded locally using the standalone diagnostic reader from
[rustydemon-gr2](https://github.com/HoldMyBeer-gg/rustydemon/tree/main/rustydemon-gr2):
their X/Y bounds are centered, approximately ±.623/±1, rather than containing the
observed lateral displacement. Reader, extracted models and OBJ files stay in
ignored `build-frame/portal-billboard`; none are shipped.

Second candidate adds a scoped local-position hook. For the observed NewStuff
asset only, mask 19, positions within .025 of horizontal radius 3 and height 4
are recentered in X/Z while retaining Y. This is an **experimental envelope
based on the captured center nodes**, not a general particle-position fix.
Other positions/variants stay native. It calls the original transform with a
private copied input stream, including per-lane instance checks for mixed batches.
`[debug] portal_centers=0` disables this part live for comparison (default 1).
The facing fix remains enabled. Additional captured-coordinate/envelope tests,
12 native signatures and cross-build pass. Staged deployment record:
`build-frame/latest-portal-center-deployment.json`. After the owner saved and
exited, verified no game process remained, backed up the previous DLL and
installed the staged binary with a matching checksum; launched through Steam.
Visual verification is pending; check the record for subsequent status.

The first center-candidate session froze on the title screen after Frame woke
from standby (last frame-rate sample 22:23:09, XR state 5 at 22:23:12.959).
Owner reported a crash. PID 29123 was still alive but no longer presenting;
desktop screenshot confirmed the title screen, with no gameplay/portal-hook
activation logged. Read-only thread/stack snapshots were retained; ordinary
GDB unwinding through Wine was incomplete, so these do not establish a cause.
No crash exception or GPU error appeared in the session logs. The process
subsequently closed; no force termination or binary rollback was performed.
Evidence: `build-frame/portal-billboard/hang-20261009-2223/` and adjacent
`hang-*-backtrace.txt`, `hang-stacks.txt`, `suspected-crash-2226.png`.
Same binary relaunched for recovery; this is not a confirmed portal regression
or a diagnosed/fixed standby issue.

**Laser skill editor / tabbed background, 2026-10-09 evening.** Owner verified
Left A opens UISwitcher directly after the controller-mode startup fix. The
transparent background still differed between eyes; owner prefers both black.
Current code submits the tabbed menu's shared opaque quad against black,
omitting the stereo world layer only while that menu is displayed. Journal and
NPC surfaces retain their previous composition.

Wheel **EDIT SKILLS → slot → skill** uses the character's active skill list,
native icons and native setter `0x1185c0` / clear `0x118b40`, with controller
table selection scoped to the synchronous UI-thread call. Readback confirms
assignment; no save editing. Learned/item-granted abilities retain owner IDs;
passives are excluded. Catalog choices freeze per page; apply revalidates
session, weapon bank, original slot and current availability. The common Town
Portal alias is 359 (219/220 are scroll/tome, not shapeshifting).
Input regressions, 24 native signature/address checks, GPU alpha/color and
opaque-backing tests pass; themed two-page preview is inspected. Native binding
and black-background headset tests passed: owner reported "ok it works great."
Full clear/reassign/both-bank/save-reload coverage was not separately reported.
Build installed with the game
closed and launched through Steam; the fresh session verified the assignment,
clear and catalog readers and read Nekro's saved skill 70 in controller bank 0.
Deployment record is `build-frame/latest-skill-editor-deployment.json`. User deferred
native gamepad-menu button conflicts until after this work.

**Saved wheel assignment delay, 2026-10-09.** Owner says wheel works, but saved
Raise Skeleton appeared only after cycling pages. Clarified the entire slot
was unassigned/button fallback, not merely a missing PNG; no reassignment took
place. Read-only live inspection confirmed native controller table RVA
`0x2a48a90` contains skill 70 at slot 2 while all keyboard table slots at
`0x2a488d0` are -1. The generic Binding/Owner accessors select one of these tables
using last input mode (`0x77e10`), so mouse/keyboard menu navigation exposes the
wrong bindings to our VR wheel. Audit: `build-frame/native-controller-bindings-audit.txt`.

`native_ui.cpp` now reads the signature-guarded controller table directly,
retaining native button mapping, active bank (0/16), owning item, empty slots,
skill availability and the combined Town Portal shortcut. That reader never writes
input mode or character data. Added entry/layout guards for bank, owner and
skill-level access. Change-only `Frame wheel assignments:` diagnostics report
the bank and eight skill IDs. Snapshot changes also prefetch icons before any
wheel page is visible; PNG I/O remains on the background worker. Existing
icon revision repaint handles readiness without changing hover or pages.

The portable regression tests model the owner's saved skill with empty keyboard
slots, mode changes, second-bank/item-owner preservation and invalid reads.
The Wine preview now prefetches a snapshot before the first Paint and verifies
native icons without prior page toggles. Build passes; new first-open headset
verification passed: owner confirmed **"yep all fixed now thanks"** after testing
the first opening with the saved assignment. All 16 native UI signatures also
matched the running build. Installed with D2R closed at **2026-10-09T19:31:56.615980** and
launched through Steam; INI and native settings hashes unchanged. DLL SHA256
`3941417560e7a4d349c27d2ae514e05a95cdfe805c713894be5315edef82f461`. Deployment record:
`build-frame/latest-wheel-bindings-deployment.json` (includes prior DLL backup).


**Transient license prompt after wheel install, 2026-10-09 19:20.** Owner saw
the 30-day warning. Read-only comparison confirmed unchanged stock/loader EXEs,
INI, prefix and S: launch routing. Failed Proton log shows services.exe creation
failing (731) and RPC unable to open the service manager. Once the owner exited,
a clean Steam VR relaunch of the **same installed DLL** started services.exe and
rpcss.exe normally. Advancing the title screen reached MainMenuPanel and the
offline character list. No license refresh, registry edits, game rollback or
authentication bypass was used. This is recovery evidence, not a proven fix for
the underlying intermittent Wine startup failure. Preserve logs/screenshots in
`build-frame/license-regression-20261009-192234/`.


**Themed native-icon wheels, 2026-10-09.** Owner confirmed the watch is visible
and upright, then requested actual icons based on each wheel slot plus themed
framing. `interaction_icons.cpp` asynchronously loads a local, validated PNG
manifest beside the plugin, with a revision driving repaint when icons become
ready. Skill IDs map through native SkillDesc/IconCel sheets; items use base
code plus unique/set record and upgraded tier. Equipment and belt snapshots
carry these identities; no saves/gameplay tables are changed. Native art is
read locally by the new standard-library `prepare_icons.py` / `steam_assets.py`
Steam reader. Installer prepares it before any game mutation and journals the
cache for restore; release packages include only the scripts and MIT notice.
New generated `assets/wheel-frame.png` provides stone/bronze framing; labels,
icons, disabled states and hover highlights are composited. Segment centers
now begin upper-right (22.5 degrees clockwise from top), matching actual art;
footer click bounds also match the frame. Original assignment/action ordering
is unchanged. Empty/missing icons retain labels. Affix dyes, charge/cooldown
badges and skill reassignment from VR remain unimplemented.

Build, parser/checksum/PNG tests, installer reversibility, ownership/hit-test
checks and Wine PNG previews pass. The wheel preview checks actual native
skills/items, a live potion identity change and transparent framing. Wrist
full/partial/empty/unknown checks still pass. Headset acceptance for these new
wheel visuals is pending. See `build-frame/wheel-preview.png` and deployment
record `build-frame/latest-wheel-icons-deployment.json`. Installed at
**2026-10-09T19:19:51.813310** after confirming D2R closed, preserving INI/native settings.
DLL SHA256 `fff5d76902885fe484e0074c5f0fa180e93ff236a41de2f5c699206b50e0a78d`.
Cache: 1580 bindings / 661 unique PNGs, no unsupported sprites.
Backup/stage: `build-frame/{backups,ready}/wheel-icons-20261009-191951/`.


**Wrist correction after owner reported no panel, 2026-10-09.** The 17:55
candidate below was **not** headset-verified; owner now says it does not appear.
Owner clarified **back of wrist, like a watch**. The previous +/-Y sign flip
used the wrong anatomical axis. OpenXR left grip +X points out of the palm,
so -X points out of the back of the hand. `WristNormal` now uses -X for both
visibility and the rendered face. `WristPanel(hand,head)` sits 5.5 cm above that
face and 6 cm toward the wrist, retaining the face normal while turning text
upright within its plane. Activation is within 75 cm and normal-to-eyes dot
>0.55. Passive vitals now require only head/left-hand tracking; the right-hand
tracking guard still blocks all combat/actions. No generated artwork changes.
New host tests cover thumb-edge vs palm vs dorsal poses, both tracking-loss
cases, menu suppression and 2,701 wrist/head rotation combinations with valid
unit quaternions and unchanged dorsal normals. Cross-build passes. Rate-limited
`Frame wrist:` logs report requested visibility, tracking, pose validity,
distance/back-facing scalar, atlas, wheel, submitted layer and frame result;
they do not record full poses. Owner confirmed **"Visible and upright"** after
this correction. Runtime at 18:57:42.882 reports wrist wanted=1, backFacing=0.995,
distance=0.413, atlas=1, layer=1, budget=3/16, submit=0; resting poses hide it.
This verifies reveal and readability, not a numeric comparison with native stats.
Installed after confirming D2R closed at **18:57:09**, then launched via Steam.
INI and native settings are unchanged. Stage/backup:
`build-frame/{ready,backups}/wrist-watch-20261009-185709/`.
DLL SHA256 `15d62644582d69c33ecb43d0c99d4953386668ff1b7db7c0eecbd77e4741afb9`.
Deployment record: `build-frame/latest-wrist-watch-deployment.json`.

**Next owner request:** use proper native icons in the wheels, selected from the
actual spell/item assigned to each slot. Implementation starting; current wheel
still has names only. Keep original game assets local or load from the installed
game; do not package extracted Blizzard assets in the repository/release.

**Wrist gesture and gothic vitals artwork, 2026-10-09.** Owner reports the panel
appears with the wrist resting naturally and disappears when deliberately
presented. `interaction::WristPanel` now defines the opposite (-Y) controller
face and offset, and `WristPresented` uses that same face for visibility; the
transport uses the shared pose. Distance remains 65 cm; the brief critical cue
is retained. New generated artwork in `steamvr/assets/wrist-vitals.png` uses the
native red/blue orb and stone-figure HUD as style reference. It is embedded via
`interaction_assets.rc` in both DLL and preview, decoded/cached once with WIC,
and shown at 24 × 12 cm. Native vitals drive liquid height and life/mana numbers;
stale/unknown values show empty glass and `-- / --`. Alpha survives GDI text
composition. See the asset README and exact prompt for provenance/layout.
Portable interaction tests cover natural vs rolled wrist, panel face/offset,
world heading, distance and invalid tracking. The Windows build and Wine preview
checks pass; full/partial/empty/unknown states have been visually inspected.
Other atlas tiles are checked unchanged. Headset verification of this change
is pending; do not infer it from the preceding settings-menu confirmation.
Installed with D2R closed at **17:55:11**; INI and native settings unchanged.
Stage/backup: `build-frame/{ready,backups}/wrist-vitals-20261009-175511/`.
DLL SHA256: `4a9c7d72c712e2cdaa08a1ed1b76220b67ee492e8729974b59a850543b809414`.
Deployment record: `build-frame/latest-wrist-vitals-deployment.json`.

**Invisible settings menu fixed and headset-verified, 2026-10-09 17:44.** The 17:36 laser
candidate did not fix the menu; the owner reported **"Still invisible"**. Its
diagnostics show valid settings/laser poses but rejected frames (-25 invalid
rectangle and -39 invalid pose). A bounded debugger capture of one open-menu
frame compared the mod's input layers with Proton's converted layers and
detached successfully (TracerPid 0). The mod's layers were intact; in the host
array the settings quad started at `0x1000ff4a8` and the laser at `0x1000ff4d8`,
only **48 bytes apart**, although quads occupy **112 bytes**. The laser overwrote
the settings rectangle and pose. Disassembly of the installed WineOpenXR
`convert_XrCompositionLayerBaseHeader_pointer_array_win64_to_host` confirms it
chooses one allocation size from `layers[0]->type`, then reuses it for all layers.
Our first projection layer is 48 bytes. Local evidence (not committed):
`build-frame/settings-layer-debug/trace.log`, `build-frame/openxr-quad-convert.asm`.
The old source snapshot under `~/vrdeps/proton-openxr/` predates this converter.

The built correction in `steamvr/composition_layers.h` prepends a zero-alpha
quad behind the scene for mixed projection/quad frames on WineOpenXR. This
makes the bridge reserve 112 bytes per layer without patching Proton. Scene,
menu and pointer ordering is retained; a layer slot is reserved within runtime
limits. Quad-first flat menus, projection-only frames and native Windows need
no prefix. The standalone regression reproduces the overlap and validates the
workaround, ordering and capacity. Windows cross-build passes. Retain the laser
math fix too, but do not call it the root cause of the invisible settings panel.
Installed after Save & Exit at **17:43:12**, with INI/settings unchanged.
Stage/backup `build-frame/{ready,backups}/settings-layers-20261009-174312/`,
SHA256 `9ee182cb4657427a70c26b358801188e320bd0f0a98648a09a9a3cc0e09990df`.
Relaunched through Steam; prefix activation logged at 17:43:44.009. The settings
panel opened at 17:43:51.996; submissions succeeded (`result=0`) with both panel
and pointer, including tab changes and close. No end-frame errors occurred in
this launch through the check. Owner confirmed **"Visible and usable"**. This
verifies panel visibility and laser/tab interaction, not every setting's Apply
behavior. Record: `build-frame/latest-settings-layers-deployment.json`.

**Invisible VR settings menu investigation, 2026-10-09 17:37.** Owner reports
Left Grip + Left Y blocks movement until toggled again but shows no panel.
Historical settings-open intervals (including 17:19:22–26) coincide with
`xrEndFrame` failing `XR_ERROR_POSE_INVALID (-39)`. The exact offending layer
has not yet been established. A host repro found the laser basis can lose
orthogonality when the head/beam vectors are nearly collinear, producing
quaternion norm-squared 0.62645. `math::BeamOrientation` now rebuilds the
perpendicular axes and normalizes the result; laser/dot layers also use the
same guarded quad submission as the other overlays. A deterministic regression
and camera tests pass; a two-million-case diagnostic scan's maximum norm error
is now 3.58e-7. Added settings submission and rate-limited invalid-quad diagnostics.

Candidate installed at **17:36:55**, after verifying the game closed normally
at 17:32:40. Stage/backup `build-frame/{ready,backups}/settings-pose-20261009-173655/`,
SHA256 `e0b229de7fc672ed5fee9ed2a9265879be7d4098d57ab02a635f8f3abeeebfc4`.
INI/settings unchanged; cross-build passed and Steam VR launch requested.
Owner asked to open the settings panel and test tabs. **Visual fix unverified**;
see `build-frame/latest-settings-pose-deployment.json` for subsequent status.
Earlier complete source/docs checkpoint was pushed as `a0473eb` to
`origin/vr/steam-frame-proton`; this investigation follows that checkpoint.

**Tracked body and stable particles candidate, 2026-10-09.** Owner reports
ground smoke tilting/pivoting with head rotation and the body separating from
the headset during room movement. `vr/billboards.cpp` redirects signature-checked
PopcornFX screen-aligned quads through its native view-position alignment, with
a copied view context containing the same cyclopean position for both eyes.
Native world-up, particle rotation/animation and other billboard modes remain.
The hook is limited to first-person gameplay and passes through if the native
context position does not match the current VR camera. Installation alone does
not establish activation: look for the **screen-facing quads now face** log.

`vr/skeletons.cpp` now aligns the rendered body to a neck target derived from
tracked eye position/orientation and the rig's bind-pose eye/neck offset. The
headset remains the camera authority; native animation does not drive it.
Recognized leg chains bend to preserve animated floor height during crouches;
unknown chains retain their original lower-body height. Controller/weapon scale
uses standing bind height, not crouched eye height. This is visual body alignment,
not roomscale movement of the game actor/collision. Runtime confirmation is
**VR body: tracked-neck alignment active**, with class and leg-chain count.
Body math and existing bow-pose host tests pass; cross-build passes. Both visual
changes still need headset validation. No performance gain has been established.
Local native-code evidence stays in ignored `build-frame/particle-facing-20261009/`;
five billboard signatures match the decrypted snapshot. New source/tests:
`steamvr/body_tracking.h`, `steamvr/body_tracking_test.cpp`,
`vr/billboards.{h,cpp}`, `vr/billboard_signatures.h`.

Installed at **17:29:19**, after normal game exit at 17:20:55 and a fresh
process-closure check. Stage/backup: `build-frame/{ready,backups}/body-billboards-20261009-172919/`.
DLL SHA256 `134187bfa5b57e0087b8a78d964f76b0aa1e0887a4fd4750108b2c128b7bc6aa`.
INI and game settings unchanged. Relaunched through the Steam VR wrapper;
billboard hook installation confirmed at 17:29:27.250. See
`build-frame/latest-body-billboards-deployment.json` for runtime/headset status.
**Runtime activation confirmed:** tracked-neck body at 17:30:00.602 (hero
class 2, both leg chains found), position-facing billboards at 17:30:01.655.
OpenXR reached focused state 5. Awaiting the owner's smoke rotation/body
lean/crouch check; activation logs alone do not establish visual correctness.

**Smoke/effects improvement headset-verified, 2026-10-09.** After the 17:13
shared-range build was installed and confirmed active with DLSS Quality, the
owner reported: **"yes it looks clearer around smoke and effects now."** Mark
the smoke/effects improvement verified. This does not establish native-equivalent
overall clarity, a performance gain, or validation of Balanced and every effect.
Keep the installed correction and DLSS-off installation default. No additional
capture or restart is needed solely to reconfirm this reported improvement.

**Shared-range write failure diagnosed, 2026-10-09 17:12.** Both candidates
installed at 17:03 and 17:07 remained inactive. The earlier projection-cache
explanation was an unconfirmed hypothesis, not the cause established by testing.
In PID4033863, a bounded one-frame debugger trace proved the VR/gameplay gates
were true, projection/range reads succeeded, and `PerspectiveRange` produced
(1059.8, 1/1059.8, .2, 0). `WriteProcessMemory` returned false, zero bytes copied,
**ERROR_NOACCESS (998)** writing the mapped NVIDIA Vulkan upload allocation.
The debugger detached automatically; nothing was patched in the live process.
Local evidence: `build-frame/shared-range-debug/{trace,write}.log`.

The installed build uses a direct 12-byte CPU store to that live mapped allocation,
which the signature-checked native frame builder has just allocated/written and
whose lifetime extends beyond the hook. No generic guarded memory helper was
relaxed. Reads/VR projection checks remain; the reserved fourth lane is preserved.
`steamvr/dlss_depth_upload_test.cpp` reproduces the exact WriteProcessMemory
failure under the same Proton/vkd3d, then verifies the production store through
D3D12 GPU copy/readback, including reserved-lane preservation. Test and cross-build
pass. Prior 64 GPU encode/decode/fade cases remain valid (math unchanged).
Test output: `build-frame/dlss-shared-range-upload-test.log`.

Stage `build-frame/ready/dlss-shared-range-20261009-171203/d2rl-vrcam.dll`, SHA256
`ae312490c36431c9546039502de0c77cf18d207a157647224f4caee450d06273`.
**Installed 17:13:04** after the owner saved/exited and process closure was
verified. Backup: `build-frame/backups/dlss-shared-range-20261009-171304/`.
INI/Settings.json hashes unchanged (DLSS Quality). Relaunched through Steam;
PID4037524, OpenXR session5 focused at 17:13:24. **Runtime activation verified**:
17:13:32.282 active-range log; repeated CPU samples show Frame[53] =
(1059.80005, .00094357424, .2, 0), actual VR projection and 2560x1440 Quality
viewport. Latest records: `build-frame/latest-dlss-shared-range-{deployment,runtime}.json`.
Capture folders `build-frame/dlss-shared-range-verified-20261009-171353/` and
`...-171424/` contain monitor screenshots and 80 CPU constant samples each, but
**no new native GPU trace**: Ctrl+F10 did not arm on either attempt. Do not claim
these contain a validated native depth histogram. No temporary debug settings
were changed. Owner subsequently confirmed clearer smoke/effects with Quality
(see above). A fresh native depth capture remains unavailable; it is not required
to record the owner's visual confirmation.

Previous inactive DLL (17:07:18): SHA256
`8702f0273baa8ee4f195a83e171de700bcd1dea44bc2fcd9b4292b966094d698`,
backup `build-frame/backups/dlss-shared-range-20261009-170718/`. INI and native
Settings.json hashes unchanged; saved DLSS mode2 (Quality). Session5 focused,
PID4033863, but Frame[53] still native (510,1/510,550,0). Shader-only replacement
has been removed, so this inactive build has the original clamped depth.
All five native signature guards match. Uploaded unjittered camera projection
at renderer+11A08 / offset D0 is valid VR infinite reverse-Z.

**Initial shared-range candidate, 2026-10-09 17:03 (superseded above).** Owner
now suspects billboarded smoke/other effects undermine the DLSS scene. Read-only
shader inspection found a concrete contract mismatch in our 16:42 depth fix:
it renormalized the shared R16 depth producer to .2..1060, but native soft-particle
shaders still decoded it with Frame CB10[53] = (510, 1/510, 550, 0).
Particle shader hashes `f03b49085dded655` and `f4843ce390dd3196` sample t38,
then reconstruct `sample * Frame[53].x + Frame[53].z` before fading against
particle view-space Z. This verifies a producer/consumer mismatch, **not that
it explains all remaining DLSS blur**. No live smoke exclusion test was run.

Candidate replaces the shader-only override with a checked native frame-builder
hook, RVA E3B590, signature checks also at E3B63D/E3BE07/E3BE56. ABI is
(renderer, frame, camera, float time), verified at both native call sites.
After the native builder fills a fresh mapped upload allocation, before its
caller binds/submits passes, update only Frame[53].xyz to the VR interval.
`vr/dlss_depth_range.h` validates infinite reverse-Z projection, finite reciprocal
range and preserved far endpoint. Gate requires active VR gameplay and validates
the actual uploaded camera projection; menus/finite/orthographic projections
pass through. Camera near/far/type fields stay untouched. All native shaders
remain intact, with no extra GPU pass/wait. Install retries on the slow tick
when renderer code is decrypted. Unsupported signatures leave the native pipeline
unchanged instead of applying a partial producer-only change.

Candidate: `build-frame/ready/dlss-shared-range-20261009-170209/d2rl-vrcam.dll`,
SHA256 `9e304e3d1093d8003df76d3453007f9d45e26c78452fa24ead49b489fa320492`.
Pointer: `build-frame/latest-dlss-shared-range-stage.json`. Build passed; four
signatures match PID4019750; GPU test covers 64 encode/decode/particle-fade cases
including R16 quantization, plus CPU invalid-input/fallback/idempotence checks.
Owner saved/exited; verified no D2R process, backed up to
`build-frame/backups/dlss-shared-range-20261009-170301/`, installed the candidate
and relaunched via Steam. INI and native Settings.json hashes were unchanged
(DLSS saved as Balanced/mode3). Deployment record:
`build-frame/latest-dlss-shared-range-deployment.json` now describes the latest
17:13 deployment. Runtime activation and smoke improvement were subsequently
verified as recorded above. Do not declare a full DLSS fix yet.

Verified Quality capture: `build-frame/dlss-quality-verified-20261009-165022/`.
Both before/after checks confirmed mode2, 2560x1440 active scene and VR projection.
R16 depth contains 12,317 distinct values, no zero geometry; original depth fix
is measured working. Owner was told they could turn DLSS off after this capture;
later Settings.json had mode3, so always check state before further comparisons.
Fullscreen Camera CB at renderer+11A00 is intentionally unjittered. Geometry
Camera CB at +11940 is distinct; zero jitter in fullscreen constants does not
prove missing render jitter. No motion-vector/jitter change was made here.
Local proprietary shader dumps and game-code snippets stay ignored in build-frame.

**DLSS artifacts corrected; clarity still poor, 2026-10-09.** Owner
reported washed-out colors/artifacts with Balanced DLSS after changing Video
settings, and confirmed **"Yes, DLSS off clears them"**. Do not treat the earlier
October 8 stability report as current validation. FXAA + DLSS off + scale 100
remains the safe visual baseline. All temporary capture settings were restored.

Captures/code evidence is local and ignored under
`build-frame/dlss-artifacts-20261009-162519/` (stereo pair, native render targets,
DLSS-off comparison and isolated sky/fog/MV/foveation trials). The brief isolation
trials were screenshot comparisons, not owner-confirmed headset A/B results.
The native R16_UNORM depth target contained only 0 and 1. Its producing pixel
shader is LinearizeDepthPass, 628 bytes, uitrace hash `4c551117939402ea` in game
3.3.93787. Read-only native inspection found camera CB3[37] = (.2, 0, 30, 0),
which correctly describes our perspective depth. Frame CB10[53] =
(510, 1/510, 550, 0) still normalizes around the distant original camera:
`saturate((distance - 550) / 510)`. Nearby VR surfaces therefore all become zero.
This is a verified depth defect, not yet proof it explains every color artifact.
Do not change Camera's original near/far/type/FOV fields globally based on this.

`vr/dlss_depth.cpp` replaces only that exact pixel shader through ReShade pipeline
creation. For infinite reverse-Z perspective it starts normalization at the
actual near plane and retains the native far limit; finite perspective and
orthographic paths retain native behavior. It uses existing buffers/bindings,
without a new per-frame pass or waits. Hash/compiler mismatch retains the native
shader. No original game shader bytecode is distributed. GPU tests execute the
production HLSL helper: 64 cases passed, including native integer/float projection
flags, finite/orthographic fallback, sky, and 31 distinct nearby R16 depth values.
Pixel shader creation and the Windows cross-build also passed.

Installed DLL SHA256:
`54ecf1d077d713bd475a480215c27cf9b639b10d7cf793520ac8e5fe6d2dcfde`.
Stage `build-frame/ready/dlss-depth-20261009-164227`; backup
`build-frame/backups/dlss-depth-20261009-164227`; deployment record
`build-frame/latest-dlss-depth-deployment.json`. Previous DLL hash
`a2f3e8e1481ac4cba4e654bbef980cd07d2a7501153e2d30218e7e9194b88cfe`.
Game was observed closed before replacement. INI and native Settings.json stayed
unchanged (DLSS off, FXAA, scale100); relaunched through the authorized Steam
wrapper. Shader match logged at 16:42:40.151; SteamVR/D3D12 session ready at
16:42:46.553 and title screen captured. **Owner tested Balanced and confirmed
"Only blur remained"**: the washed-out colors/scattered artifacts were gone,
but the image looked low-resolution; disabling DLSS restored clarity. Logs show
2227x1253 scene pixels for 3840x2160 output with Balanced (about 34% of native
pixel count). Owner also tried Quality and reported **"Still low-resolution even
when still"**, then switched DLSS off. `build-frame/dlss-quality-current.png` and
`quality-cb-samples.json` were captured after that switch and are NATIVE references;
do not infer missing DLSS jitter from those samples. The subsequent verified Quality capture above confirms continuous native depth. Existing motion
vector correction/history remains unchanged: near correction is disabled, so
most geometry retains native vectors against the prior render/other eye. That
is a remaining temporal-reconstruction suspect, not yet isolated as this blur's
cause. Don't conflate the verified color/artifact improvement with a full DLSS
motion-vector or clarity fix. Keep DLSS off as the baseline.

**Graphics preset / anti-aliasing regression resolved in headset, 2026-10-09.**
Owner said the game initially looked and worked well, then became blurry with
ghosting (especially distant scenery) after trying presets, DLSS and AA, even
after returning to Very High with DLSS off. Settings.json had `Anti Aliasing=3`,
`NVIDIA DLSS=0`, `Resolution Scale=100`, dynamic resolution off. The saved good
configuration used `Anti Aliasing=1` (FXAA). Asked owner to select FXAA in the
native Video menu; verified the game saved value 1 and owner reported **"Clearer
now"**. Texture/shadow quality remained 4. Keep FXAA + DLSS off + 100% scale for
this setup, and recheck AA after selecting a quality preset. No DLL, shader,
save or direct JSON edits were needed. Pre-test settings and comparison:
`build-frame/backups/graphics-ghosting-20261009-161707/`. Mode 3's exact menu
label was not captured; don't confuse the old upstream TAA=2 note with a
verified current enum mapping. This validates the AA recovery, not every new
interaction or the intermittent XR submission issue documented below.

**Self-service Steam VR launcher added, 2026-10-09.** Owner asked whether an
agent must launch the game. Added **Diablo II VR** through Steam's Add Non-Steam
Game UI, targeting `/home/berkybear/diablo2-vr-port/steamvr/run-proton.sh`, then
enabled **Include in VR Library** in Properties. Verified persisted shortcut
`appid=4289304862`, `OpenVR=1`, correct target and empty launch options. Normal
D2R Play remains stock. Also added `~/.local/share/applications/diablo-ii-vr.desktop`
and a Desktop copy. This used Steam's UI, not an external write to its running
shortcuts database. Shortcut selection itself has not been launch-tested; its
target is the same wrapper already used successfully for game launches. No
game launch occurred during shortcut setup. INSTALL.md documents this route.

**Settings/interaction candidate installed and launched, 2026-10-09 15:49.**
Owner connected Frame and requested this test. Installed the staged
`vr-settings-20261009-133023` DLL (SHA256
`a2f3e8e1481ac4cba4e654bbef980cd07d2a7501153e2d30218e7e9194b88cfe`)
with D2R closed, backing up the previous DLL and INI to
`build-frame/backups/vr-settings-20261009-154928/`. Preserved the live INI exactly,
including enabled motion controls and eye tracking. Launched through
`steamvr/run-proton.sh`; logs confirm SteamVR/OpenXR, D3D12 session creation and
focused XR state 5. Focused the desktop game for native menu input. User-visible
settings/interaction validation is still pending. Current deployment record:
`build-frame/latest-vr-settings-deployment.json`. Earlier staging descriptions
below record the state before this authorized installation.
Startup follow-up: gameplay entered at 15:50:10; intermittent `xrEndFrame`
`XR_ERROR_POSE_INVALID (-39)` begins at 15:50:11. SteamVR's XR client says
`compositor failed EndFrame()`. Head/hand/gaze tracking remains active; bow
character equipped. Cause is unconfirmed, no corrective setting changes made.
Asked owner what the headset displays; response pending. New interaction
compositor layers need investigation if the user reports freezing/distortion.

**Fresh-install defaults updated at the owner's request, 2026-10-09:**
`steamvr/install.py` now writes `motion_controls=1`, `fixed_foveation=1` and
`eye_tracked_foveation=0`. Documentation reflects these defaults. This is an
installer-only change after the settings candidate was staged; no DLL rebuild
or deployment is needed for it. The owner's installed settings still have
motion controls and eye tracking enabled. Existing release ZIPs remain older.

**VR settings candidate, 2026-10-09; latest build, not installed or headset-tested.**
Candidate: `build-frame/ready/vr-settings-20261009-133023/d2rl-vrcam.dll`
(SHA256 `a2f3e8e1481ac4cba4e654bbef980cd07d2a7501153e2d30218e7e9194b88cfe`).
Pointer: `build-frame/latest-vr-settings-stage.json`. This includes all of the
interaction candidate and deferred bow-height correction below. It adds the
owner-requested controls reference and live VR/video settings panel. Open with
**Left Grip + Left Y** even with motion combat off, or **VR SETTINGS** in the
wheel. Right laser/trigger selects; Right B/Close/the chord dismisses. See
GAMEPLAY.md for settings and the first headset check. The world keeps running
while gameplay inputs are blocked. No install, game launch, commit or push was
performed for this candidate; the installed bow-height DLL remains unchanged.

- `steamvr/vr_settings.h`: shared definitions, hit regions, editable draft,
  Apply/Close state, sequence acknowledgement and held-input/tracking gate.
- `vr/settings_menu.cpp`: update-thread-only profile reads/writes; only changed
  keys are merged into a temporary INI and atomically replaced, first-save
  `.before-vr-menu` backup retained, same-key external changes rejected. Poll
  checks timestamps and avoids rereading unchanged values. Config is not read
  or written on the XR/render thread.
- `steamvr/vr_settings_ui.cpp` and `transport.cpp`: room-anchored compositor
  quad, local laser input (no native mouse clicks), max 10 Hz changed-only atlas
  uploads retained through GPU completion, no input against an unpublished or
  stale page. Open/close cancels combat and pending interactions. Character
  transitions close the panel. Controls, VR and Video tabs plus height reset.
- Optional gaze extension/action is bound at XR setup whenever supported, so
  Off/Fixed/Eye tracked can switch live; samples are read only when enabled.
  This changes no runtime privacy permission. Base-game DLSS/textures/shadows
  and SteamVR resolution are deliberately outside this panel.
- Validation: DLL cross-build; 12 host regression suites; Windows settings
  model/store plus interaction/journal tests in isolated Proton prefix; three
  GDI page previews inspected. Real headset laser alignment, stereo, live
  settings changes and motion-off reopening still need verification. No new
  game addresses or hooks were introduced by the settings work.

**Interaction implementation candidate, 2026-10-09; not installed or headset-tested.**
Candidate: `build-frame/ready/interactions-20261009-130513/d2rl-vrcam.dll`
(SHA256 `c92c59b6cc75dd798e42f14877a696c9e693900a2e2c5bb1a42f2349eb99edf7`).
Build pointer: `build-frame/latest-interactions-stage.json`; logs, synthetic atlas
previews, complete source patch and test notes are adjacent to the candidate.
The owner asked to implement [steamvr/INTERACTIONS.md](steamvr/INTERACTIONS.md),
with no spell gestures. New controls/test order are in GAMEPLAY.md. The installed
DLL remains the deferred bow-height correction described next. Do not silently
replace it or launch D2R while the owner has deferred testing. The separately
staged candidate also contains that bow correction. No commit/push requested.

Implemented source in this turn:

- `steamvr/interactions.h`: copied snapshots, stable item identity, per-hand
  input ownership, radial hit testing, native set requests, torso/hip/belt
  anchors, either-hand grab/drink, quick use, stow, wrist status and brief
  critical cues. `Input::at` uses GetTickCount64 for native freshness, while
  `time` uses QPC for motion intervals. Mixing those clocks was caught/fixed.
  Reset/recenter/tracking/menu/cancel signals discard queued native work.
- `vr/interactionstate.cpp`: reads SDK equipment/belt on the UI thread,
  validates captured item plus column revision immediately before a native
  command, expires requests at 250 ms, observes removal/set changes for success.
  **A handled native command alone is not consumption/equip confirmation.**
  Existing weapon-set inference remains a runtime limitation, especially an
  initially empty/identical set. No save/inventory edits.
- `vr/native_ui.cpp` / `native_ui_signatures.h`: guarded local client-player,
  controller assignment, owning-item skill lookup and HP/mana readers. Skill
  list +0x100 checked before lookup to avoid load/teardown assertion. Names use
  native localization, with `Skill N` fallback for unsupported keys. Guard
  failure leaves native button labels/status unavailable; no guessed stats.
  A present skill is not a mana/cooldown/charge readiness guarantee. There are
  **no original skill icons, charge counts or complete new-class name metadata**.
- `steamvr/interaction_ui.cpp`: synthetic GDI radial atlas, potion types/counts,
  set labels (base item codes), wrist gauges. Held bottle tile keeps captured
  identity across refill, becomes Used only after confirmation. Dynamic XR
  atlas only updates on visible changes, at most 10 Hz; uploads survive until
  their GPU fence. Runtime composition-layer limit respected. Surfaces remain
  flat compositor quads, without world-depth occlusion or detailed asset meshes.
- `steamvr/journal.h`, `vr/journal_ui.h`, `vr/hud.cpp`, `transport.cpp`: native
  quest UI-only capture from the existing R16F HUD layer, shared across eyes;
  native quest root rectangle converted from HD 2160/SafeScreenPercent layout;
  stereo retained, native UI drawn once, left-hand follow / grip-release pin /
  grip-near-cover pickup and ray UV back to native screen coordinates. Ordinary
  menus stay unchanged. Missing UI layer/rect/format/capture falls back to the
  normal menu. **Crop/native act-tab clicks/stereo need the first headset test.**
  Tracking loss pins the last pose and requires a fresh grab. Quest page only.
- `vr/skeletons.cpp`: presentation-only weapon subtree hiding for stow/bottle/
  book, accounting for free staffs on an opposite authored attachment.
- Selected native spell aim now bypasses the old bow/staff-only weapon filter
  in `HookAttackPoint`. Simple select/point/press works through native input;
  **full 3D spell flight/contact and immediate accepted native spawn remain
  unimplemented**. Do not transplant the basic-arrow hook onto arbitrary spells.
  Native animation, costs, charges, repeat cadence and effects remain active.

Validation: host interaction/journal/gameplay, bow-pose/flight, melee contact,
projectile/combat rules, camera math, gaze and deferred-draw regressions; Windows
cross-build; native reader signature audit against local `build-frame/game-code.bin`.
Synthetic GDI atlas rendered under an isolated Proton prefix and visually checked;
that is not a game/headset screenshot. Art/data extraction under `build-frame/`
is local only and must never be included in a release. New host tests and native
ABI audit scripts are in `steamvr/`; runtime flags/values still need verification.

Next runtime pass: wheel names and selection release gate → utility set swap →
physical holsters → belt cancel/drink/refill/refusal → wrist values → held/pinned
quest crop/click/reopen → inventory/labels/melee/bow/recenter regressions. Then
continue the single-projectile spell adapter, including native one-time cost/
cast-rate acknowledgment, hand-origin effect ownership and full 3D contact.
The detailed target design explicitly distinguishes that remaining work.

**Arrow-height correction installed 2026-10-09 12:11:48; headset test deferred by owner.**
The first physical-arrow test produced arrows originating far above the player.
Asset/code inspection found `Release` runs for both the unit root and visible
mesh, and `Visual` could overwrite its entity binding with the root. The mesh
then adds its authored +5 Y / 1.25 scale to the physical world transform.
The correction requires a validated TrackReleasePoint component, binds its full
entity generation once, and ignores later root/helper callbacks. Both initializer
orders, extra callbacks, entity reuse, and a two-node height reproduction pass
the host regression; the DLL cross-build and diff check pass. Actual runtime
binding order and the visual result still need the owner's test.

- Installed/staged DLL: `build-frame/ready/physical-bow-height-20261009-121148/d2rl-vrcam.dll`.
- SHA256: `ca4eb3ffab5e4e550b59c83767652dea8790f7a9ef4d94d849f20583fcee824d`.
- Previous DLL/INI backup: `build-frame/backups/physical-bow-height-20261009-121148`.
- Live INI preserved exactly; the game was closed and has **not been relaunched**.
- Source remains uncommitted. No new commit/push request for this bow iteration.
- Prior priority (now implemented in the separately staged candidate above): design weapon holsters, spell wheel/casting, physical potions,
  wrist status and a journal in space. [steamvr/INTERACTIONS.md](steamvr/INTERACTIONS.md)
  contains the code-grounded proposal, dependencies and implementation order.
  Those interactions are **not in the installed bow-height DLL**; do not claim current XY spell aim is
  full 6DoF casting or that the current whole-scene menu capture is a 3D journal.
- Owner refinement: **spell gestures are out of scope for now**. Equip a spell,
  point the right hand and pull trigger; projectile spells fire on press when
  ready. Prioritize snappy accepted release and impressive, coherent hand-origin
  flash/trail/light/impact. Keep native mana/charges/cast rate and spell behavior;
  measure launch delay separately from velocity. Target-family adapters are
  internal plumbing, not different gestures or release-to-cast controls. This
  applies to the interaction candidate; the installed arrow-height DLL is unchanged.
- When the owner returns, check arrows leave the held string at hand height,
  then flight direction/contact/ammo. The user will test later; no launch or
  further headset capture is pending during the interaction design work.

### First physical-arrow candidate and observed issue

**Physical basic-arrow build installed 2026-10-09T11:59:14.478362-07:00; height defect reported.**
User authorized implementing the complete bow responsiveness proposal and preparing
a build, then confirmed Frame is on and ready. The candidate is now installed
and the offline VR launch is authorized; no new captures have been requested.
Source changes are uncommitted on `vr/steam-frame-proton`, based on pushed
`94a6f89eac083a47e45f3ff30fa04270d40d2b56`.

- Candidate DLL: `build-frame/ready/physical-bow-20261009-115600/d2rl-vrcam.dll`
- SHA256: `420e6719a6745b08fc80ba2facee9bad91633be7481a3796d313f9e49b54548f`
- Build record, source patch and test notes are adjacent to that DLL.
- `build-frame/d2rl-vrcam.dll` now contains the interaction candidate as well
  as the height correction; the installed DLL remains the height-only build. The retained revision-4 DLL
  remains `6b284346ce23a1118db3122eecb5b2fa59351274df7413d5193e968eb48d88c0`.
  Pre-install DLL/INI backup: `build-frame/backups/physical-bow-20261009-115914`.
- Runtime at 11:59:25 confirmed physical bow, projectile, combat and melee hooks
  ready. XR reached focused state 5 and loaded-game calibration at 11:59:54;
  user subsequently reported the above-player arrow origin. Startup evidence is
  adjacent to the staged DLL.
- First three observed releases at 12:00:22–33 each created one native missile
  and one client visual, with 2x native velocity (4608→9216), 27–46 ms queue
  latency and 35.2–36.6 m/s draw-scaled speed. At least one native body contact
  was recorded; the reported height defect prevents calling this flight verified.
  See adjacent `runtime-bow-test.log`.
  Keep `build-frame/ready/free-swing4-20261009-023651/` for rollback.
- Build log: `build-frame/build-physical-bow.log`.

Implemented bow path:

1. `gameplay.h` emits a single `bowRelease` event, never an A pulse. Transport
   freezes the right string-hand position and full right-to-left hand direction,
   then resolves them once in the same stage/world frame as the view. It carries
   session, equipment, recenter/turn epoch, sequence and 150 ms expiry.
2. `vr/bow.cpp` schedules native basic attack `42FEB0` on the authoritative game
   thread. A narrow TLS scope redirects `4333F0`'s spawn/target arguments; native
   creation still owns ammo, weapon damage and procs. Only basic missile id 0.
3. `projectiles.cpp` now checks `combat::LocalPlayerId` plus owner type/id. The
   previous code wrongly dereferenced the SDK's opaque session handle, explaining
   why successful hooks could log no accelerated arrows.
4. `463100` is replaced only for our registered server missiles: straight 3D
   flight at native 25Hz, continuous tip/body-capsule contacts, supercover native
   barrier checks, closest-first hits and native `4639A0` damage/pierce. No second
   ground-grid collision. At full draw/default multiplier 2, nominal speed is
   40 m/s; native path velocity/Slow Missiles and native lifetime remain inputs.
5. Client visuals are created on the UI thread using its local owner pointer,
   not a server pointer. Native client allocation `1B7760`; `1B5380` skips the
   duplicate client collision/prediction for our arrows, and `1B8500` cleanup is
   tracked. Periodic game-thread id lookup retires externally removed missiles.
6. Release hook associates only the validated release mesh's full generational
   HD entity with its client arrow; root/helper callbacks cannot rebind it.
   `E3A8C0` supplies a frozen-path world transform, shared by both eyes and with
   coherent previous matrices. Mesh -Z is the measured basic-arrow long axis;
   final model/skin alignment still needs runtime confirmation.
7. Held-bow posing uses a 16-sample stamp history matched to the completed hero
   attachment, including that pose's hand offset. Verified melee logic is retained;
   its module only gains validated game-thread server-lookup helpers.

Validation: six host suites (physical flight, gestures, bow rig, projectile rules,
combat rules, melee contact), cross-build, `git diff --check`, and 20 native
entry/call-site/layout guards against the owner's ignored local snapshot.
Reproduce the latter with `python3 steamvr/audit_bow_signatures.py
build-frame/game-code.bin` (one command). Code snapshot/extracted model and
reference reverse-engineering files stay ignored and must not be distributed.

**Runtime limits:** body capsules and flat launch-floor collision are approximations;
barriers extend vertically. The native damage/lifetime proxy retains its launch
grid position, while custom collision/rendering follows the physical path. Native
proc-source position and room-transition behavior still need refinement. Room
queries reacquire the proxy's native room each tick and fail closed for unavailable
cells. Special basic-arrow conversions are refused before attack; selected skill
buttons retain the native route. Crossbows/spells are unchanged. A signature failure
leaves physical shots disabled, with counters in the log; no double-fire fallback.

At the owner's deferred test, use the bow character outside town. Check
immediate release, faster flight along the shaft, pitch-sensitive misses, creature
contacts without targeting, one ammo per release, empty quiver, cancellation,
wall/close-range shots, stereo, repeated draws and a melee regression. Read
[steamvr/GAMEPLAY.md](steamvr/GAMEPLAY.md) for the exact test sequence. Do not call
this candidate owner-verified before that test, or replace the verified rollback.

### Previously verified melee baseline


**Revision 4 verified in Steam Frame on 2026-10-09.** After the final comparison
request (fast slashes, backhands and swinging while walking), the owner reported
**"works great"** and requested commit/push. This validates the tested sword
setup; it does not establish every weapon/creature combination. No further
captures or headset runs are requested tonight.

Installed 2026-10-09 02:39:27 with the game closed. DLL SHA256:
`6b284346ce23a1118db3122eecb5b2fa59351274df7413d5193e968eb48d88c0`.
The same build is retained at
`build-frame/ready/free-swing4-20261009-023651/d2rl-vrcam.dll`;
adjacent `build.json` records validation.
Backup `build-frame/backups/free-swing4-20261009-023927/` contains revision 3
DLL/INI. Build log: `build-frame/build-contact-melee4.log`. Host melee, gameplay,
combat and bow tests and the Windows cross-build pass.

Existing runtime logs confirmed combat/contact hooks ready at 02:39:36. Through
02:47:22 they recorded 58 physical contacts / 58 native accepted hits, 0 rejected,
0 missing-server failures, 1 pose reset and 482 recovery samples. These are
diagnostic counters, not a measured percentage of the owner's swings that hit.
No additional recording was made for this final run.

Code-level defect addressed: `MeleeContactTick` paired the latest XR hand/time
with a cached older `GunBone`, then reconstructed that older bone using newer
camera yaw/eye position. The engine could consume the timestamp before the blade
caught up, or reject contact after the latest hand had stopped. `PoseHistory`
now retains 16 timestamp-keyed input samples; the completed skeleton pose carries
its original shared input stamp. Melee gets that exact sample (max 100 ms), with
current action, equipment and recenter gates still mandatory. World origin/+Y axis
and player placement come directly from final attachment × the hero's same-pose
root transform, not from a later camera frame. Hero generation and snap-turn
world epoch reject obsolete geometry. Bow/debug `GunBone` behavior is unchanged.

Synthetic regression demonstrates the old asynchronous order loses a short fast
slash, while matching pose timestamps retains the contact after the hand stops.
History tests cover unknown/expired stamps, menu/trigger cancellation, equipment,
recenter/tracking changes and zero stamp after wrap. The subsequent owner test
confirmed the improvement. Generic capsule/weapon-fit limitations remain:
cross-weapon/boss/slope checks, attack-speed scaling, offhand contacts and parries
are still open. Bow draw/string posing is verified; flying-arrow orientation and
speed tuning stay parked. Palette, potion and spell ergonomics still need their
own validation. The published preview ZIP predates this motion-combat work;
build from source for these changes.

### Historical motion-combat diagnostics

The entries below preserve earlier investigations and rollback artifacts. Their
pending-test and installation notes describe those older builds; the verified
revision 4 status above is current.

**Revision 3 installed 2026-10-09: slashes were failing despite reliable thrusts.**
The owner reported thrusts work but swings in any direction often miss. A read-only
40-second live capture (1,698 input samples, `build-frame/melee-motion.jsonl`) and
host replay reproduced a **35-second lockout**: real slashes reached 53 rad/s,
tripping the old 35 rad/s guard; the required 80 ms of almost stationary motion
never occurred during continued fighting. The separate 18 m/s derived blade-speed
ceiling also rejected the strongest portions of legitimate swings. Only 119
powered samples / 4 distinct target-stroke contacts survived the old replay.
The first test session recorded 38 contacts / 38 native acceptances / 0 rejections,
so the damage dispatch was not the primary cause of this failure.

`steamvr/melee_contact.h` now recovers after 80 ms of coherent fresh samples,
without requiring stillness. It drops the discontinuity itself; preserves menu,
session, equipment, snap-turn, stale-input and teleport cancellation; uses
rate-aware 10 m/s hand / 60 rad/s rotation guards; permits blade travel justified
by physical hand motion; removes the derived upper-energy cutoff. Minimum energy
and excursion remain 1.2 m/s and 8 cm. Fast sweep interpolation preserves the
blade's length along its rotational arc instead of collapsing its tip onto a
chord. A second lockout came from unbounded jitter/windup accumulation suppressing the
net-excursion ratio. Stroke onset now uses a bounded 120 ms / 32-sample history;
jitter still cannot qualify, but cannot poison the next deliberate slash. Final
replay: 1,413 powered samples / 48 distinct target-stroke contacts (289 intersecting
samples powered, 28 unpowered; previous build 18 powered / 299 unpowered).
This is **offline replay evidence, not headset validation of the update**.
Synthetic committed-source tests cover fast swings in four planes, jump/hitch
recovery while continuing to move, outer-arc contact, and misses beyond reach.
Native logs now include pose-reset/recovery counts.

A stereo screenshot with `[debug] bone_axes=1` confirmed +Y follows the visible
short sword. Read-only hero/weapon matrices likewise agreed with collider origin
and direction. Model bounds were inspected with an ignored Rusty Demon GR2 reader;
no game assets, third-party reader or captured player poses added to source.
Temporary bone axes have been turned off. Bow flight stays parked.
Cross-build and host melee/gameplay/combat/bow tests pass. Installed with game
confirmed closed 2026-10-09 02:21:55, DLL SHA256
`44d7f136ba2eda16b926f1e8a7dcd72778aa8f1a718b780e4a25e0b843597ea5`; backup `build-frame/backups/free-swing3-20261009-022155/`.
Runtime confirms the contact hooks ready at 02:22:03. Headset comparison requested;
owner reported much better but fast motion still misses. No commit/push.


**Owner feedback on the first free-swing build: "hit or miss at best."**
Revision 2 installed 2026-10-09 02:03:56, SHA256 `b4542fcba5c87fca39ad44752a31640d5cc6c9be80dcdb9c4c8d6433aa2fd752`; backup `build-frame/backups/free-swing2-20261009-020355/`. It refreshes native local-player
identity from the revalidated hero each active sample (not only old controller
aim traffic); rotation-aware reversal permits wrist-led repeated slashes; initial
acceleration geometry joins the first powered sweep; speed/excursion threshold
1.2 m/s and 8 cm; +2 cm weapon radius and +5 cm creature radius tolerance.
Ordinary get-hit/block animations no longer discard physical swings (death/town
still gated). Added intersection/stale/wall counts and tests for four consecutive
wrist-only reversals and onset-path inclusion. Owner saved/exited before deployment; revision 2 is launching for comparison.

**Installed 2026-10-09 01:54:19: free-swing contact melee, awaiting headset validation.**
DLL `f2d056b1a72f7742d4bf51cce5f4aa6163fcc7d8badf4ca4f55819656bed5971`;
backup `build-frame/backups/free-swing-20261009-015419/` holds deterministic-hit
baseline DLL/INI. Deployed with game closed and launched via run-proton.sh.
Host melee/gameplay/bow/combat tests and snapshot signature audit pass; cross-build
log `build-frame/build-contact-melee.log`. Runtime confirmed all hooks ready at
01:54:27. At 01:55 the server game/player/session identities matched and strong
swings queued successfully with no missing-server failures; no geometric contact
had yet occurred (nearest creature was ~15 game units away). No commit/push yet.
Owner rejected grip-to-arm + controller targeting. New `vr/melee.{cpp,h}` and
`steamvr/melee_contact.h` replace melee A pulses with swept weapon capsules and
exact contacted-target damage through native basic attack. No grip/trigger or
preselected target. Trigger fallback and bow/spell paths remain separate.

- `42FEB0` native basic-attack Do function owns the damage object and complete
  combat effects. TLS overrides `48FE20` GetTarget **only** at its two call sites
  inside that function (`42FFAA`, `43010A`). No path/selection fields are written.
- `48FE80` server lookup and normal GetTarget calls identify the local authoritative
  game using native type/id, unit+0xD8 backpointer and opaque SDK session identity.
  Requests run through `ThreadService::runOnGameThread`; incoming pointer identities
  are resolved there. Monster hash buckets at game+(0x4C6+i)*8, link unit+0x158.
- Native `44B720` result flags record success. `348650` melee range only for that
  scoped contact uses native `3502A0` line collision; physical geometry replaces
  old grid reach. `48E460` relationship, death/town/get-hit/block modes gate dispatch.
- Body capsules infer height from native MonStats2 `0975B0` pixHeight +0xD and
  footprint +8/+9. This is approximate artwork geometry, not mesh collision.
  Weapon category lengths use posed GunBone +Y; generic but needs physical tests.
- Physical room-space motion supplies energy; camera motion alone cannot hit.
  One target per stroke; quiet rearm or deliberate reversal; 150ms queue expiry;
  session/equip, menus, tracking resets and snap/recenter cancel pending contacts.
  Haptics mean accepted native hit flags, not proof of damage through immunity.
- **Native animation cadence is replaced by physical stroke cadence.** IAS scaling,
  per-model dimensions, offhand collisions and shield/parry behavior remain open.
- `steamvr/melee_contact_test.cpp` tests sweeps/misses, strength, jitter, frame rates,
  repeated contacts and cancellation. Gameplay tests assert no melee A pulses.

Live test at 01:57–01:58 recorded 8 geometric contacts, 8 accepted native
hits, 0 rejections and 4 missing-server failures. Runtime input shows grip=0,
trigger=0 with melee contact enabled. Player/session and MonStats2 footprint/height
fields were verified by read-only inspection. Owner collider-fit feedback pending.

Previous installed baseline (retain rollback details):

**Latest installed: deterministic outgoing hit test, 2026-10-09 01:30:35.**
Owner confirmed both right-grip and right-trigger melee work, then explicitly
requested removal of dice-roll misses for melee **and arrows/magic**, with strong
physical swings connecting on contact. Native hit probability is now overridden
for the local player's hits against creatures; full swept-blade contact is still
outstanding, and that distinction has been explained in commentary/test question.
DLL `b6edab6541cbb19784e90c594f54dadd5fcd5d7dcbfefff077558872524d0640`;
backup `build-frame/backups/contact-hits-20261009-013035/` contains the prior melee
DLL/INI. Game was closed before deployment. INI adds `[gameplay] contact_hits=1`.
Disable that setting to restore native miss rolls; motion_controls must be on.

New `vr/combat.{cpp,h}`, `vr/combat_rules.h`, `steamvr/combat_rules_test.cpp`:
- Native IsHitSuccessful `44BA60` wraps a TLS scope for the identified player.
  Seed getter `34A1E0` supplies a zero-valued scratch seed only at return address
  `44BD6D` within that eligible accuracy check; native minimum chance is 5%.
  The real seed is advanced normally once (low*0x6AC690C5+high) before scratch is
  supplied, preserving the following attacker damage/proc roll. Rest of the
  function runs normally, including Prevent Monster Heal on the accepted hit.
- `44B930` ApplyBlockOrDodge and `44EF70` ApplyDodge return no random block/dodge
  only for that player attacking a monster. Native collision/range/relation,
  damage/resistances/immunities, ammo/mana and timings still apply.
- All four prologues plus exact inline-roll bytes must match; no overrides until
  all hooks install. Full native owner type/id matching excludes enemies, pets,
  other players, PvP, scenery. Projectile collision code calls these functions
  with its native owner; spells without an accuracy roll keep their collision path.
- **SDK PlayerHandle is opaque, never a pointer** (handles.h explicitly says so).
  `HookAttackTarget`, after IsHeroUnit validation, publishes native id+freshness
  and opaque session token to combat. Read-only live inspection confirms id=1,
  native unit type=0 and matching SDK session. The older parked projectile-speed
  module incorrectly tries SDK handles as pointers, explaining its absent counts;
  this was discovered during combat work but NOT changed (arrow tuning parked).

Host rule tests, snapshot signature audit, build (`build-frame/build-contact-hits.log`)
and diff checks pass. At 01:30:43 runtime confirmed all hooks ready. At 01:32:57,
01:32:59 and 01:33:03: three sword melee checks, three accepted accuracy rolls,
three block bypasses, zero failures. Owner visual feedback and live ranged/spell
checks remain pending. Async question asks whether hits are consistent.
By 01:36 the session totals were 19 melee checks / 19 accepted rolls / 19 block
bypasses, zero failures, and no projectile checks yet. Game remains running.

Research files in ignored `build-frame/combat-reference/`: primary D2MOO source
and license (read for control-flow reference), native disassembly. Temporary
read-only extraction of monstats2/weapons tables and short-sword asset stays in
ignored game-assets. No game assets/stock executable/saves/inventory edited.
Physical contact needs a proper weapon sweep, creature bounds and exact target
dispatch; do not silently substitute an arbitrary hand-radius check and call it
mesh collision. All motion/combat work remains uncommitted.

**Current priority: generic melee across characters and melee weapons.** Owner
tested the flight build below: "somewhat aligned and not that much faster" and
explicitly parked arrows to work on melee. Latest session (01:03–01:06) has hook
startup messages but no flight creation/velocity counters. Speed changes remain
unverified; do not continue arrow tuning unless requested. Game was closed when
this melee work began.

New shared main-hand melee changes in `steamvr/gameplay.h` / `transport.cpp`:
weapon-category sensitivity (unarmed/short/one-handed/long), translation and
rotation recognition, net-excursion guard against rapid jitter, quiet ready
haptic, one request per stroke, and 250 ms aim latch toward the swept hand's
ground bearing. Laser direction no longer redirects a motion slash; trigger
fallback still uses live aim. Tracking orientation jumps require releasing grip.
New melee equipment resets palette selection to Attack; ordinary tracking/menu
interruptions preserve deliberate skill selection. Claws/orbs/javelins/throwing
weapons enter this generic path as well: A must be bound to basic Attack for
melee. This dispatches the assigned action, not an invented damage call.
`HookAttackPoint` skips long-range staff/thrown aim extension during motion melee.
It retains the original game's reach, target selection, timing and damage rules.
No character or rig is special-cased; no physical blade collision, confirmed-hit
haptic or independent left-hand attack yet. Owner wants a generic solution,
not one restricted to Nekro or a particular test weapon.

Strict host gesture tests pass, including all four profiles, slashes/chops/
thrusts at 36/72/120 Hz, direction latching, menus/equipment cancellation,
orientation jumps and sign-equivalent quaternions. Bow pose / projectile helper
regressions and cross-build pass (`build-frame/build-melee-generic.log`).
Installed and launched 2026-10-09 01:13:53, game confirmed closed first:
DLL SHA256 `cc4eb561f2275ff1a32f0cefabe8e6980302e9ca9b03283958c843f4aa1d478a`.
Backup `build-frame/backups/melee-generic-20261009-011353/` holds the prior
flight-test DLL/INI. INI and saves unchanged. Headset validation pending. Async
question asks the owner to try slash/chop/thrust with any melee weapon outside
town, right grip held, basic Attack on A. Check `Frame melee: swing requested`
and `Frame gameplay ready ... melee ready=... grip=... aiming=...`; these prove
gesture dispatch, not native damage. All motion changes remain uncommitted.
Runtime 01:15–01:16: owner switched from hunter's bow to short sword (`ssd`),
recognized as melee type 4. Ready pulses logged; a motion swing at 01:16:00.898
was followed by native pad A attack at 01:16:00.916. Trigger fallback attacks
also reached native hand-aim hooks. This establishes equipped-type detection,
gesture and native input delivery; target accuracy/damage/comfort await feedback.

**Previous flight test installed/launched 2026-10-09 01:03:05:**
`d60ed9893e83d68753b34e009bc4feb66f2884f1b1c810cfbba920723c357a21`.
Backup `build-frame/backups/bow-flight-20261009-010305/` contains the
owner-verified two-hand bow DLL and INI. User had quit after firing; the timed
F10 recorder produced an empty file. Do not claim a flight capture succeeded.
The combat log did capture those shots. Read-only extraction from the owner's
Steam archives revealed the flying arrow is a **static model with no skeleton**,
with `TrackReleasePointDefinitionComponent.transitionTime=0.5`. Extracted game
assets and the temporary D2RExtractor storage reader stay ignored in build-frame.

New `vr/projectiles.cpp` isolates flight from the working held-bow pose:
- `[gameplay] arrow_speed=2` scales native PATH velocity only within local-player
  basic-arrow creation on both client RVA `1B7760` and server `5371A0`. The path
  setter is `342AE0`; type/id, unit↔path backlink and owner checks fail closed.
  TLS creation scopes restore on exit and mask nested unrelated creation. Speed
  activates only when all three signatures/hooks match. Native lifetime remains,
  so range can increase. Elemental/skill arrows and other owners are unchanged.
- `arrow_direct_release=1` bypasses the half-second HD handoff by removing only
  that arrow entity's TrackReleasePoint component with native operation `70DC70`
  before initializer `771A10`. Full entity generation and sparse/packed storage
  membership validated. Original native arrow path/transform remains. This is a
  targeted **test**, not proof that every flight orientation issue is solved.
- Owner lookup `09A7B0`, component storage `6E3CD0`, registry `2677170`; expected
  bytes checked. No game tables, stock executable, assets, saves or inventory
  edited. Motion controls must be enabled for either change.

Strict host gesture/pose and new ownership/velocity tests pass. Cross-build
passes. Flight-test log confirms client/server velocity hooks and release
handoff hook ready at 01:03:14. Actual arrow events were not logged; owner
reported only partial improvement and parked this (see current priority above).
Look for `Frame bow flight: created client/server ... accelerated ... direct
handoffs ...` if investigation resumes. Both settings are live-reloadable for comparison.
Uncommitted motion source now includes projectiles.{cpp,h}, projectile_rules.h
and steamvr/projectile_rules_test.cpp. Do not publish local game assets/dumps.


**New priority after the eye-tracking commit:** motion gameplay. Owner requested
real bows/melee, VR potion handling and weapon/skill selection/casting, with
6DoF controllers, and selected right-handed controls. The first gesture-driven
native-combat prototype is implemented, built and installed; see
[steamvr/GAMEPLAY.md](steamvr/GAMEPLAY.md) for exact behavior, controls and limits.
This is not completed collision/physics combat. Native skill metadata, accurate
surface targeting, item meshes, counts/health/mana and physical collisions remain.

Latest installed **left-attachment correction, 2026-10-09**, game confirmed closed:
DLL SHA256 `00ed67676d2959dbe86ebdd422911687947807499c302b6c4dafeca9ba4e5378`.
Rollback DLL/INI: `build-frame/backups/bow-left-anchor-20261009-004022/` (this contains the failed two-anchor build).
Last owner-verified draw build: `build-frame/backups/bow-two-anchors-20261009-003455/d2rl-vrcam.dll`.
Only the DLL changed. Motion controls remain locally on; retained DLSS off,
centered projection, working labels and eye-tracked foveation. At this historical
checkpoint the installer defaulted motion controls to **0**; the owner later
requested **1**, now applied. Motion source is uncommitted; published ZIP remains old.
No save edits or synthetic equipment were made.

Owner acquired/equipped hunter's bow (`hbw`) and arrows (`aqv`) on an offline
Amazon. Initial left-grip grab failed. The 00:01:59 build removed that prerequisite
and added trigger hysteresis, pre-squeeze entry, a larger grab region, arrow/
V-string hints, and diagnostics. Owner confirmed draw and shooting work, but
actual native bow pull/rumble happened after trigger release. Runtime captured
13 nocks / 12 ready / 12 release events, and native attack/aim pulses.

Read-only bone captures in ignored `build-frame/bow-poses-motion-20261009.json`
confirmed the native wind-up began after release. Current build adds
`vr/bow_pose.h`: string/limb/arrow deformation from runtime bind geometry,
constant string/limb lengths, draw amount tied to hand separation, snap-back on
release, hide arrow until nocked. `PoseMotionBow` validates the 13-bone rig's
names/hierarchy/bind data and **full parent entity == current hero entity**;
NPC/dropped bows must not be changed. Only the nine deforming matrices after
cog are written. Valid input is copied through a separate lock; skeleton workers
never take the transport mutex. Input older than 150 ms fails closed. Unsupported
rigs keep native animation and quad guides. Successful posing hides duplicate
quad string/arrow. Native timing/damage/ammo are unchanged; immediate projectile release is not solved.

New haptics check OpenXR return codes, stronger grab/release pulses, both hands,
and light draw-progress pulses before release. Correct Frame controller bindings
already include the feedback action. `frame_hmd` has no hand haptics by design;
do not mistake that headset binding for missing controller bindings.

Validation: strict host gameplay and bow-pose suites pass; cross-build succeeds
(`build-frame/build-bow-pose.log`). Captured hunter's bow bind geometry passes
all draw fractions; procedural full draw closely reproduces the observed native
limb-tip/string positions. Runtime capture and generated validation data stay
ignored, not bundled game assets. **Owner verified the 00:23 native pose/haptics: “yes it all works.”**
Then reported arrows flying sideways and slowly, requesting exact two-hand
anchors: right hand at arrow tail, left at bow, visible arrow along their vector.
The 00:34 build adds that visual pose: full attachment-to-head transform from
hero posing, composed with the bow entity's local transform; inverse maps both
hand anchors into cog space. String follows right hand, arrow rotates along
right-to-left vector, limb tips preserve string/limb lengths. Unreachable draws
are constrained. Invalid/stale attachment data falls back to normalized draw.
`anchored=1` in readiness logs confirms the new path while drawing. Synthetic
mirrored/scaled attachment and exact world-direction tests pass. Native aim uses
the same two-hand line and retains it for 400 ms after release (attack pulse
still 65 ms). Vertical projectile motion and speed remain native/ground-based.
The 00:34 two-anchor build failed the owner's visual test: draw disappeared and
arrow pointed right at nock. Root cause found in attachment selection: generic
`axesSide` follows **inventory slot**, which selected the right wrist, although
ECS world-matrix comparison confirms the actual bow hangs on L_weapon_attach
(error ~0.0002 units). Inverting the right-hand attachment cancels right-hand
pull motion! The latest correction publishes a dedicated `leftToHead` matrix
from L_weapon_attach and uses that for the two-anchor conversion. Built and
installed after game closed; the owner subsequently confirmed the held arrow and
string follow both hands (see measurements below).

Latest 00:40:22 build now has runtime geometric evidence: ignored read-only
`build-frame/bow-anchor-measure.py` measured 734 live draw samples, comparing
posed string/arrow bones through the actual item and left attachment transforms
to the tracked anchors. String-to-hand p50/p90 errors ~0 / 0.00326 m; arrow versus
hand-line p50/p90 angles 0.0335 / 0.4519 degrees. No `xrEndFrame` errors since this
restart. Haptic/nock/ready/release events continue. This checks draw geometry,
not visual comfort, native projectile direction or velocity. Owner confirmed the corrected draw follows both hands. Latest unresolved
request is specifically **flying arrow model angled across its flight path**
(not the held bow twisting; explicitly clarified). Flight path is roughly
straight. Arrow must also travel much faster. Plan a 2x native travel-speed
test without accelerating game time; velocity has NOT been changed yet.
Need identify the native flying-arrow asset/transform separately from held bow.
Game was saved/exited; relaunched current good nock build for inspection. Check `Frame gameplay ready` for `native=1` and draw
values while holding trigger; haptic acceptance does not prove felt feedback.

During 00:25–00:26 tests `native=1`, changing draw amounts and successful
both-hand OpenXR haptic submission are confirmed. Repeated `xrEndFrame -39`
(`XR_ERROR_POSE_INVALID`) correlate with the idle nock marker, clearing during
physical draw. The same errors existed in the 00:01 guide build. The 00:34 build omits that redundant marker once native bow posing is active
and normalizes/validates optional guide poses. Logs show zero frame submission
errors through the subsequent loaded-game bow test (00:36 onward).
Investigate before treating perceived arrow speed as solely native velocity;
no game-table speed/balance change has been made. User nevertheless confirms the live draw.

Other motion implementation: `steamvr/gameplay.h` recognizer, `transport.cpp`
OpenXR grip/aim/haptics, static atlas and stereo palette/bottle/guides, native
controller skill routing; `vr/gamecmd.cpp` timestamped native belt requests on
UI thread; `vr/vrcam.cpp::HandRay` uses motion aim. Right A's duplicate raw pad
mirror was removed so semantic no-target protection applies. Melee, potions,
skill selection and broader regression scenarios still need individual evidence.

Motion controls retain left stick walk/right snap/Left X inventory. Left Y
holds palette, right trigger selects eight B/X/Y/RB and LT-variant native skills
(no rebinding/names yet), attack mode or weapon swap. Grip over belt column
grabs a neutral numbered bottle guide; hold, raise to mouth, tip and dwell .25s
to request its normal belt command. Swing detection requires right grip and
quiet reset. Bow uses right trigger to nock within .28m, then at least .12m additional
draw, .28m total hand separation and release; no left grip is required. Select Melee/Bow for
gestures. Both tracked hands required. Physical collision, free 3D projectile
origins and pitch-sensitive terrain targeting are explicitly not implemented.

Older performance/visual evidence below remains useful but is superseded as
the immediate work priority by motion gameplay.

Latest performance evidence: after another competing Iron Signal workload was
stopped around 21:46, the owner reported much better movement with slight residual
bursts. The earlier 21:39 **“Smoother, bursts gone”** report was followed by more
contention from a new process. Check current host load, not just the original PID.
At 21:48 the owner enabled **Balanced DLSS** and reported distorted visuals and
a temporary pause. NGX parameter ABI and correction-size-limit bugs were repaired;
the **22:04:32** build logs correction submission for both eyes. The owner then
confirmed **“looks good”** with Balanced DLSS. They also reported subtle left/right
differences in emissive/other effects that may predate DLSS (details below).
Keep `[render] defer_gpu_wait=0`; the experimental synchronization change did
not explain the bursty motion. See the capture results and limitations below.
After the 22:42 installation, the centered-projection lighting trial received the owner report:
**“patches seem to be gone. Though the image is a bit softer.”** Keep the installed
`[render] symmetric_projection=1`. This supports the native light-bin off-axis
projection diagnosis; broader scene/clothing checks remain. The wider rendered
FOV at unchanged texture resolution explains an expected sharpness tradeoff.
After the 22:48 relaunch the owner turned DLSS off and reported **“much clearer.”**
Current visual baseline is therefore **DLSS off**, centered projection 1, fixed
foveation 1 and conservative synchronization 0. Do not restore Balanced on a
future relaunch; the earlier Balanced preference is superseded. Settings.json
confirms NVIDIA DLSS=0; the scene log switched from 0.580 to 1.000 at 22:49:51.
The comparison shows DLSS contributed to softness; do not attribute all blur
to the centered projection or treat this as proof the DLSS correction is flawless.
The user also reported **“Smoother”** for the 19:49 build on 2026-10-08, which
caches foveation command-list/pipeline state, defers redundant
VRS binding, batches diagnostic draw counts and fixes CPU accounting on Proton.
The user explicitly prioritized **performance first, creature labels second**.
The source checkpoint includes these fixes. The published v0.1.0 preview ZIP
still predates them; build current source or publish a new preview before telling
a friend the downloadable binary contains the lighting/label corrections.

1. **Residual performance hitches and CPU rendering overhead.** The logger's
   disk stalls and repeated foveation callback work have been reduced. Do not
   claim all remaining stalls are shader compilation.
2. **Broader creature-label regression checks.** After centered projection and
   current-eye health-panel draw ordering, the owner confirmed **“fixed :)”**.
   Both fixes are now enabled by default. Bosses, rapid target changes and a
   wider range of distances still deserve checks; no need to repeat failed
   anchor-only experiments. Details and limitations below.
3. **Validate Quest 3 + WiVRn**, or Windows + Virtual Desktop, using the friend's
   agent and real headset. Check runtime extensions before changing the transport.
4. Finish menu regressions, targeting/loot feedback, on-demand belt/health/mana,
   weapon/spell selection and optional melee/bow mechanics from TASKS.md.

## Implementation map

| Area | Files |
|---|---|
| Native OpenXR, eye copies, controllers, recenter, menu laser | `steamvr/transport.cpp`, `frame_math.h`, `input.h` |
| Fixed foveation / D3D12 Tier 2 VRS | `steamvr/foveation.cpp`, `foveation_math.h`, `vrs_compat.h` |
| Buffered runtime logger | `steamvr/logging.cpp`, `logging.h`, `logging_test.cpp` |
| First-person camera, per-eye label hooks | `cleanroom/camera/d2rcam.cpp`, `mat4.h`, `game_layout.h` |
| Native UI panel toggles / game state | `vr/gamestate.cpp`, `vr/vrcam.cpp` |
| Body selection / checked scanning | `vr/skeletons.cpp`, `vr/probe.cpp`, `steamvr/hero_selection.h`, `memory.h` |
| Room/draw-distance and fog scans | `cleanroom/drawdist/drawdist.cpp` |
| MinGW/MSVC ReShade aggregate-return ABI adaptation | `external/reshade/include/reshade_api*.hpp` |
| Install/restore, Linux and Windows Steam wrappers | `steamvr/install.py`, `launch.sh`, `session.py`, `launch_windows.py` |
| Portable binary preview packaging | `steamvr/package.py`, `cmake/openxr-loader/` |

## Creature-label investigation

**Verified result:** the owner confirmed **“fixed :)”** after the corrected
draw-order build at 23:03. Centered projection and current-eye health-panel draw
ordering are enabled by default for native OpenXR and in the installer. DLSS
remains off. A subsequent 23:05:10 pair confirms both eye paint callbacks execute
after UI updates, but caught no creature, so it is not a quantitative post-fix
label-disparity measurement. Capture counters are reset to 0.

Earlier diagnosis: centered projection removed the large separation in the owner's
view, but they clarified **label depth seems off**. Capture **22:54:42**, pair 1,
shows a Zombie label centered at **X=2030 left / 2049 right** in 3840x2160 images:
the box shifts right while the creature shifts left. Corresponding native unit
anchor X values were 2037.20 left / 2021.59 right. Thus large apparent doubling
has improved while the remaining disparity still has the wrong sign.

A live comparison with only `stereo_labels=0` produced **“About the same.”**
Restored 1, reset capture_labels=0. Second local pair is **22:56:27**. This
does not implicate the unified-head correction as the source of residual depth.
Keep centered projection 1 and DLSS off through this investigation.

A fresh draw-order fix is built, now default `[hud] monster_draw_order=1`.
It reinstates the narrow deferred-health approach after read-only verification:
native PanelManager::Draw RVA **0x89F2E0** delegates to Widget::Draw **0x855B00**
(visibility and child traversal); it does not end the caller's UI render pass.
D2RLoader's vtable thunk is retained. Native panel-draw signature is checked in
addition to widget/tooltip signatures and both expected vtable entries. The
trial is scoped to the render thread's current stereo gameplay eye, queues no
widgets across traversals, and can be disabled live after installation. It is
independent of the anchor-correction setting. A capture logs health-panel paint
after native tooltip updates for each eye. No label textures/health values are
replaced. Local disassembly evidence is ignored `build-frame/{widget_draw,
panel-manager-native,panel-manager-loader,tooltip_draw}.asm`.

Host camera and deferred-order tests plus the cross build pass. New DLL SHA256:
`477d2b2d55f7440b297e36909c22e37ea486f9ede38c699740a6f6066d5c513b`.
Installed after Save & Exit at **23:00:55**, then relaunched. Backup:
`build-frame/backups/label-order-centered-20261008-230055`. Installed setting
monster_draw_order=1; centered projection 1, stereo_labels 1 and DLSS off retained.
Startup at 23:01:03 kept native paint order because the added PanelDraw signature
omitted its leading 0x40 prefix. No draw-order correction ran in that launch.
Corrected the byte pattern and verified all three complete signatures against
the live process (including the prefix), then rebuilt successfully. Corrected
DLL SHA256: `2eb6287946b1eeee02868cfb63c0baff936cc3625b10fd168f7d54100d1d7781`.
Installed after Save & Exit at **23:03:15**, backup
`build-frame/backups/label-order-signature-20261008-230315`.
Startup **23:03:23.269** confirms the hook installed; stereo gameplay resumed at
23:03:43. The owner then confirmed the depth fixed. Camera/queue logic is
unchanged; do not describe the first launch as a label-depth comparison.
The subsequent source build changes only missing-setting defaults to match the
already tested installed values. The running DLL need not be replaced for that.

Game build **3.3.93787**. Signature-guarded optional hooks:

- UnitToScreen RVA **0x76A7D0**, signature
  `4c 8b dc 55 56 57 41 57 49 8d 6b a8 48 81 ec 38 01 00 00`;
  `bool(context, unit, float output[2], uint8_t above)`.
- Camera::WorldToScreen RVA **0xED5EB0**, signature
  `48 8b c4 48 89 58 10 57 48 81 ec 40 01 00 00`;
  `bool(camera, float point[3], float output[2])`.

The native unit path resolves the HD model transform/bounds, projects head and
feet, then combines **projected foot X with projected head Y**. Inline camera
projection rebuilding can bypass the normal projection hook. The first fix used
the exact eye scene matrices in WorldToScreen, scoped only to unit labels:
native/exact projections differed by under one pixel and labels still doubled.
The second fix captures original head/base world points, combines base X/Z with
head Y into one world anchor, projects once per eye, and replaces output XY.
It corrected horizontal placement by roughly 20–40 px but **still doubles**.

`[hud] stereo_labels=1` enables these current experimental hooks. Behind/near/
invalid points are culled. Menu and shadow fitting projections are unaffected.
Full viewport dimensions, asymmetric FOV, row-vector / row-major / right-handed
-Z / reverse-Z math are covered by tests. Passing them is not visual proof.

F8 requests the next complete stereo gameplay pair through ReShade screenshot
capture; both images share a `Frame-pair-N` suffix with left/right labels. The
log records bounded unit-anchor data, eye FOV and pose for that pair. A remote
agent can instead increment positive `[debug] capture_pair` in the installed
INI while the game runs. A request is consumed only during active stereo
gameplay. A positive `[debug] capture_labels` arms a one-shot capture after a
visible native above-unit label is observed; changing that counter rearms it.
Readback causes a diagnostic pause: exclude that pair from benchmarks.
Screenshots contain game imagery and must remain local/ignored.

The automatic capture at 20:13:51 caught **Kashya**, one clear label per eye.
At **20:16:28/29**, pair 2 caught **Fallen Shaman**: the creature shifts left in
the right eye, but its health box shifts right. UnitToScreen returns native X
2429.48 (left) / 1779.95 (right) on a 3840-wide viewport. The rendered box instead
tracks the preceding eye's position. Each image contains one box, not two.

Code evidence: `TooltipsPanel::Draw` **0x880550** calls the target-name routine
**0xC0420** at 0x880AA5/0x880AE3/0x880B0C. That calls helper **0xCE850** and
monster information routine **0xCEA50**, which sends a UI event carrying the
current anchor. `MonsterHealthPanel::OnEvent` **0x1F8E60** updates its
`TargetAttached` and `ScreenAnchored` children via **0x1F84E0**. The attached
child receives the packed anchor at event payload +0x30 through **0x857050**.
The independent health panel can already have painted before TooltipsPanel.

The **20:33 draw-order prototype was withdrawn**, not validated. It patched
PanelManager paint (0x1D79CE8, original D2RLoader thunk 0x3E2B7F6) and
MonsterHealthPanel paint (0x1CDF3D0, generic 0x855B00), queuing the health widget
until the root traversal returned. A CPU queue test and startup signature checks
passed, but do not establish renderer-state or ABI correctness. Source and DLL
are archived locally in `build-frame/backups/stereo-regression-20261008-205705`;
source was removed from the working tree. Do not reinstall that DLL blindly.
The preceding DLL/effect were restored from
`build-frame/backups/monster-ui-order-20261008-203340` at 21:00.

The owner reported broken stereo, incorrect head pivot and third-person view.
Restoring that DLL did **not** fix it. The log shows camera mode changed to
third person at **20:39:05.943**, persisted as `[mode] vr_mode=2`, and reloaded
at 20:53 and 21:00. At **21:04:35**, changing only `vr_mode=4` restored first
person. The owner confirmed **“Both are normal now”** for camera pivot and
stereo convergence, with ReShade effects still enabled. The input that triggered
the mode change is unknown; F2 and shared legacy view actions both reach SetView.
Only the first-person camera uses native OpenXR eye poses; other modes had
submitted incompatible camera images with the headset poses/FOV.

A native-only guard now repairs saved VR modes to first person and rejects
legacy mode-switch shortcuts, preserving flat mode and the FlatVR build.
The guard DLL was installed after Save & Exit at **21:09:14** and relaunched.
Backup: `build-frame/backups/camera-guard-20261008-210914`. Live regression
checks passed: writing `vr_mode=2` was repaired automatically at 21:09:50, and
F2 was ignored at 21:09:51 with the saved mode still 4. Startup confirmed native
OpenXR and compiled ReShade effects. DLL SHA256:
`78dcb5a53b688310a26e442812997a071b63b0b8093f51efeb36971e7b5ea140`.
The installer explicitly writes mode order 2.
No ReShade-effects A/B was necessary after this controlled mode repair.
Scroll Lock remains configured as the global ReShade effects toggle but was
not pressed. Debug capture counters are reset to zero.

At that earlier stage creature labels remained unresolved. The mode error means the withdrawn build
was not evaluated in the intended camera mode; it neither proves nor disproves
the draw-order approach. Instrument health widget paint/event ordering in first
person before reintroducing it, including validation of UI renderer state.

UnitToScreen callers found: 0xCE877, 0x198E90, 0x198FCE, 0x1FABDF (items),
0x6D7B15, 0x1519482, 0x1519875. Helper 0xCE850 applies scaled UI offsets.
The ReShade UI trace (`vr/uitrace.cpp`, Ctrl+F10) can dump one GPU frame and raw
targets. It causes a capture pause; do not mix it into a performance comparison.
UI target is R16G16B16A16_FLOAT with **inverse alpha**, cleared black/alpha 1.
Do not infer duplication merely from different left/right pixel positions:
the headset's asymmetric optical centers produce large legitimate differences.

## Performance findings

A bounded profiler was installed at **21:19** (backup
`build-frame/backups/frame-profile-20261008-211901`). It leaves all graphics
settings unchanged. Set `[debug] profile_capture`
to a new positive integer for 360 active stereo pairs; reset to zero afterwards.
`steamvr/profile_report.py` parses the log and exports CSV. See README for
measurement limits: main render thread only, inclusive callback scopes (hot
callbacks sampled 1/64), GPU scene queue spans include CPU starvation. No
worker-thread attribution is provided by these callback measurements.

The first captures contain 360 valid GPU samples each. Sequence 1 was standing
still; sequence 3 was genuine movement (65 distinct logged hero positions).
Sequence 2, misleadingly saved as `profile-walking.csv`, was stationary: do not
use it as a walking benchmark. Local artifacts are under ignored `build-frame/`.

| Measurement (ms per stereo pair unless stated) | Standing, sequence 1 | Moving, sequence 3 |
|---|---:|---:|
| Mean full cycle | 27.027 | 48.474 |
| 95th percentile cycle | 34.823 | 80.985 |
| Both eye passes, elapsed | 16.938 | 34.913 |
| Main drawing thread CPU in both passes | 16.306 | 25.000 |
| GPU completion wait | 3.721 | 3.534 |
| ReShade effects, both eyes | 1.416 | 1.454 |
| Eye copies, both eyes | 0.046 | 0.048 |

These are different scene/motion workloads, not an optimization A/B. Eye drawing
dominates, while copies and effects are comparatively small. Sampled main-thread
VRS callbacks total roughly 0.6–0.9 ms per pair. The rest is not automatically
engine-only cost: uninstrumented mod work and worker threads remain possible.
The GPU scene timestamp span includes queue starvation and is not GPU busy time.
No half-second stall occurred in these bounded captures; the separate 581 ms
pre-render stall still needs attribution. Read-only process sampling is saved as
`frame-profile-process.csv`; kernel perf stack sampling was unavailable under the
current `perf_event_paranoid=4` setting. No global permissions were changed.

A synchronization comparison DLL was installed after Save & Exit at **21:27:59**:
SHA256 `1727c9f4fe30f8f7571605797e85dc38a5175e9d35c3e8d0205bf889bd092f19`.
Backup: `build-frame/backups/deferred-wait-20261008-212759`.
`[render] defer_gpu_wait=1` submits image copies/state transitions on the D3D12
queue bound to OpenXR and checks each copy allocator's previous fence before
reuse, instead of unconditionally blocking before image release. Shutdown still
waits for resource completion. The runtime orders access on the bound queue;
Proton's bridge drains pending submissions before its release barriers. Source
default remains **0**. Active GPU timestamp profiling deliberately retains the
old completion wait; use `[debug] profile_gpu=0` for this synchronization A/B.

Sequences 10–13 first compared 0/1/0/1. The owner reported moving at the start,
confirmed in sequence 10, so the test was repeated as **1/0/1/0**, sequences
20–23, from 21:31:25 to about 21:32:30. Each sequence contains 360 pairs, with
the hero remaining at `(8740.6, 11277.0)`. Viewing direction varied, particularly
in the conservative runs; do not present the aggregate difference as a controlled
percentage speedup.

| Repeat captures, 720 pairs per mode | Conservative (21, 23) | Deferred (20, 22) |
|---|---:|---:|
| Mean cycle, ms | 39.671 | 35.831 |
| Median cycle, ms | 36.451 | 32.123 |
| 95th percentile cycle, ms | 63.505 | 59.101 |
| Mean XR end, ms | 5.721 | 3.846 |
| Mean GPU completion wait, ms | 3.418 | 0.000 |
| Mean runtime submission, ms | 1.705 | 3.082 |

Some removed wait reappears in runtime submission; the full 3.4 ms is not a net
saving. The combined XR end stage is about 1.9 ms shorter in this sample. This
supports a modest improvement, not a solution to rendering cost or long hitches.
No new XR/D3D12 failure or copy timeout was logged during the comparisons.
At 21:32 the owner tested `defer_gpu_wait=1` and reported bursty slow/fast movement
and Left X not opening inventory. The setting was restored to **0** live; the
owner said movement was unchanged. Do not treat deferred waits as a validated
improvement or enable them by default. Subsequent inspection found an unrelated
Iron Signal test consuming roughly **18 logical CPUs on this 32-thread host**,
another substantial confound for the comparisons. Its executable was
`/tmp/iron-x11-target/release/iron-signal-desktop`, PID 3286587. The owner explicitly
authorized a 45-second pause/resume comparison; results follow below.
No graphics settings, effects, saves or game executables were changed for the test.

The Iron Signal workload began at **21:23:45**, just before the synchronization
comparisons. At 21:37:33 it was paused with an independent 45-second resume
watchdog. The owner also asked the other agent to stop its performance work.
At 21:38:18 the pause ended; the original process subsequently exited. No process
was left stopped. New/lighter Iron Signal and compiler jobs were still observed,
so sequence 31 is **not** a repeat with the same heavy workload resumed.

| Capture (360 pairs each, conservative sync) | Paused, sequence 30 | Heavy process gone, sequence 31 |
|---|---:|---:|
| Logged hero positions | 1 (stationary) | 42 (walking) |
| Mean cycle, ms | 33.519 | 29.400 |
| 95th percentile, ms | 37.923 | 37.120 |
| Maximum, ms | 55.213 | 41.831 |

The owner then confirmed **“Smoother, bursts gone.”** CPU contention was a major
contributor to this regression. Scene/motion and competing workloads changed,
so do not derive a precise percentage gain. Local CSVs:
`build-frame/profile-contention-30.csv` and `profile-contention-31.csv`.
All capture counters are reset to zero. Check live competing CPU/GPU workloads
before any future VR benchmark; request approval before pausing another project.

The inventory regression was desktop focus: `_NET_ACTIVE_WINDOW` was a terminal,
and native Left X was unnecessarily gated on Win32 foreground focus, despite the
XR session remaining focused. Restoring the game window using `xdotool
windowactivate --sync` restored inventory; owner confirmed it and logs show
successful open/close cycles. `wmctrl -ia` alone did not actually focus this window.
The fix was installed at **21:40:18**, after the owner saved and exited, then
D2R was relaunched. Backup: `build-frame/backups/menu-focus-20261008-214018`.
Current DLL SHA256:
`357a5a0aa18f5c37dba624423ef53c9cc53ddee38992ae15c8bfbb178961a64e`.
It removes desktop-focus gating only from the native D2R menu action.
Explicit menu actions/clicks also try to focus the game;
OS mouse/keyboard injection still requires actual foreground focus, and passive
laser hover never steals focus. Build and existing input/math checks pass;
the intentional terminal-focus test confirmed native inventory opens/closes.
Windows/Proton rejected `SetForegroundWindow` in this test, so laser input was
correctly withheld while the terminal retained focus. The owner confirmed the
behavior and explicitly said **not to pursue a further fix**: requiring the game
window to stay focused is acceptable. INSTALL/README now state that requirement.
The game window was restored to the foreground after the test. Respect this
scope decision; do not add a focus broker or another workaround unasked.
Later at 21:40, different Iron Signal runtime/test processes again consumed
substantial CPU. Do not assume the host is idle because the original PID exited;
the owner has asked the other agent to stop performance work. No additional
processes were paused.

At 21:46 the heavy workload was gone and the owner reported much better movement,
with a slight residual burst. Temporary D2R-only CPU affinity trials (sequences
40–43, one 5950X L3 group versus original affinity) were confounded by changing
movement and the other workload stopping. Do not claim an affinity gain. All
original thread affinities were restored; no launch affinity change was made.
Sequence 44 started later than intended, after DLSS was enabled, and is saved as
`build-frame/profile-dlss-broken-44.csv`, not an affinity comparison. It includes
a 22.7-second first-eye CPU stall ending at the first DLSS correction shader
build; steady-state median is 30.75 ms, p95 34.53, but the mean includes that
startup pause. Do not compare this different scene directly with DLSS-off runs.

### DLSS compatibility trial, 21:55

The owner enabled Balanced through D2R's options (`NVIDIA DLSS: 3`); do not
silently overwrite it while the game runs. Per-eye NGX features were created,
but the motion-vector correction logged unreadable Depth/MotionVectors inputs.
`vr/dlss_mv.cpp` redeclared the SDK's overloaded virtual interface. MSVC reverses
each Set/Get overload group; our MinGW build does not. The old D3D12 resource
getter dispatched to byte offset 112 instead of 72, and its setter to 48 instead
of 8. `vr/ngx_parameter.h` now supplies unique virtual names in MSVC slot order
with nonvirtual typed wrappers. `steamvr/ngx_abi_test.py` compiles all 17 calls
against an independent SDK-order declaration with both Windows compiler targets;
the fixed code matches MSVC, and the old MinGW layout fails all 16 overloaded
calls. No external C parameter wrappers are exported by this `_nvngx.dll`.

Built and installed SHA256
`0a9df2d19fdff8a57f065567df5ec142a63752f6bc936e85dbbd42d9f0a1dd3d`.
Backup: `build-frame/backups/dlss-abi-20261008-215509`. Relaunched offline VR;
Balanced retained, `defer_gpu_wait=0`, profiling idle (`profile_capture=0`).
New logs report NGX input failure result codes or a correction submission for
each eye. This proves neither correct vector math nor acceptable visuals until
checked in the headset. Near-parallax correction remains upstream-default off.
The original shader still compiles synchronously on first DLSS use; the previous
session used the private Microsoft compiler and paused about 23 seconds here.
Installer default remains DLSS off until the trial is validated.

The owner restarted SteamVR during the first launch; relaunched at 21:57 with
Frame reconnected. At 21:58:18 the fixed ABI read valid resources: depth
3840x2160 R16_UNORM, vectors 3840x2160 R16G16_FLOAT, scene 2227x1253, MV scale
-2227/-1253. The first shader build again stalled about 22.5 seconds. Correction
still did not submit because the diagnostic statistics limit (900,000 bytes)
was incorrectly applied to mode 2, which never writes those statistics. The
next build moves that guard to the diagnostic path only. After the owner saved
and exited, installed at **22:04:32**, SHA256
`d8c298c663e10f261272a407af4bb4498bd535807529a5eb70d0b4048f1dcacd`.
Backup: `build-frame/backups/dlss-size-20261008-220432`. Relaunched with Balanced
retained. At **22:05:39** the log confirms valid inputs and correction submission
for **both eye 0 and eye 1**, scene 2227x1253. The initial shader build stalled
22.3 seconds again. The owner subsequently confirmed **“looks good.”** This is a separate
bug from the ABI.
A stereo capture at 21:59:14 is in the game directory (`Frame-pair-1-left/right`);
it is not a performance sample. `capture_pair` was reset to 0 afterwards.

Post-compilation sequence 50 (`build-frame/profile-dlss-corrected-50.csv`) has
360 pairs: mean cycle **25.946 ms**, median 23.282, p95 33.643, max 35.431.
Both eye passes total 16.832 ms wall / 15.973 ms render-thread CPU; XR wait
3.102 ms and GPU fence 3.477 ms per pair. No GPU timestamp capture was enabled.
This is a short current-scene observation, not a matched DLSS on/off benchmark.
`profile_capture`, `capture_pair` and `capture_labels` are all 0 again.

### DLSS correction specialization, 22:30

Separated the production correction shader from the diagnostic statistics shader
at compile time in `vr/dlss_mv.cpp`. Pipelines are created on demand and retained
independently, so normal mode 2 never compiles the diagnostic reduction. The old
uniform branch reserved 27 KiB of group-shared memory even for correction-only
work. The new correction declares no group-shared storage or group barriers.

An isolated Microsoft SDK compiler probe under a separate Wine prefix measured
**33 ms / 4,632 bytes** for correction versus **22,880 ms / 52,028 bytes** for the
old combined shader. Diagnostics still compile in about 22.7 seconds, only when
explicitly selected. Compiling the old shader with its correction branch fixed
true produces **exactly the same shader instructions** as the new production
shader. Artifacts: ignored `build-frame/dlss-shader-probe.log` and
`dlss-{old-0,new-0,new-1,reference-0}.asm`. The cross build passed. This establishes
shader compilation improvement, not a measured in-game FPS gain.

Installed after Save & Exit at **22:30:59**, backup
`build-frame/backups/dlss-specialized-20261008-223059`. DLL SHA256:
`4471de6d4b0de9c9d153bbeccf7dbbf941fdcacf427fb224dc2ef3f92bc457dd`.
The owner had left DLSS off after the diagnostic comparison; restored only that
setting to their original Balanced (3), then relaunched using `--frame-vr`.
Live validation at **22:31:57.798** logged the 4,632-byte correction pipeline
ready in **67 ms**, followed by correction submission for both eyes at
2227x1253. The initial pair was 95.8 ms rather than the earlier roughly 23-second
stall. This confirms the startup fix; steady-state FPS has not been compared.

### Distant stereo lighting patches (initial centered-projection check passed)

The owner clarified: glow/emitted light around waypoints, portals and lamps,
also highlights on clothing, show different square patches in each eye and
flicker with head turns. Flames themselves look fine. The fault disappears
close up and is visible at a distance. Controlled observations this session:

- Added sky/fog/ceiling disabled together: **still flickers**. Restored all 1.
- Fixed foveation temporarily off: owner still described the different square
  patches. Restored 1. The later capture was after its automatic restore, so do
  not describe the screenshots as foveation off.
- Owner switched DLSS off in game at the same distance: **still present**.
- Temporarily suppressed bloom upsample pipeline `25811850` in the then-running
  process (PS hash `ffda83db6037f452`): **still visible**. Restored skip_pipeline=0
  after the response; watchdog finished. This reduced soft glow, not all bloom.

Stereo pair at **22:16:14** (left/right PNGs in the game directory) and a native
GPU trace at **22:16:24** capture the affected distance. Trace and 23 decoded
render targets are in ignored `build-frame/lighting-capture-20261008-221614/`;
raw targets remain beside the INI. Targets are overwritten at successive uses,
so one final resource dump does not establish which draw introduced an artifact.
The trace records command-list recording order, not global GPU execution order.
The centered-projection experiment below is installed and passed the owner's
initial waypoint check; wider validation is still needed.

Read-only native inspection found `LightBinPass::Render` at RVA `0xFEA150`,
called by lighting task `0xE02920`; the light bins use a 32x20 grid. This is a
candidate for square lighting artifacts, **not a confirmed cause**. Renderer
singleton is `*(base + 0x34F6480)`; its LightBinPass is +0x1BD58, its clip-plane
buffer member +0x78, and global-frame constant-buffer CPU pointer +0x11BB8
(size 0x5A0). Setup at 0xFE9760, compute shaders `light_bin_c` and
`light_bin_depth_bounds_c`. Global constants built at 0xE3B590. Local assembly
notes are ignored `build-frame/lighting-*.asm`; do not publish raw game code.

Further read-only shader inspection found a concrete projection incompatibility:
`light_bin_c` reconstructs tile rays using only inverse-projection diagonal
CB3[17].x / CB3[18].y, and projects light bounds using CB3[13].x / CB3[14].y.
It never includes the off-axis projection terms. Frame's captured FOVs have
substantial horizontal and vertical asymmetry. Local-only disassemblies are
`build-frame/native-light-bin.asm` and `native-light-bounds.asm`. Shader asset
handles are LightBinPass +0x88/+0x98; object at asset +0x40, DXBC pointer at object
+0x38. This is evidence of an incompatibility, **not yet proof of the visual cause**.

A live `[render] symmetric_projection=1` mode is installed and now the default.
Once per successful XR LocateViews, it encloses both eyes'
recommended FOVs in a common centered frustum. It leaves poses/IPD/head pivot
unchanged and uses that same FOV for projection, VRS center, captures and
xrEndFrame submission. OpenXR supports submitted FOVs differing from the
recommended ones. At equal texture dimensions it loses some angular resolution;
it is a compatibility test, not a performance optimization. Revert live with 0.
Host math tests cover actual Frame FOV containment, diagonal-only ray
reconstruction, compositor mapping and stereo world-anchor convergence under
both projection modes. Source/installer defaults are 1 following the owner's
successful waypoint and subsequent creature-label checks; other headsets remain untested.
Cross build and host math tests passed. Installed after Save & Exit at
**22:42:35**, then relaunched. Backup:
`build-frame/backups/centered-lighting-20261008-224235`. DLL SHA256:
`1dccb3ff0f3e8e8337d91b8e3fc5689668a7c8e9e1ef4701663db46166b79806`.
Only the DLL and this one INI setting changed; Balanced DLSS and prior diagnostics
defaults were preserved. The live INI has legacy malformed header/comment lines,
so strict Python ConfigParser cannot read it; use section-scoped minimal edits.
Runtime **22:42:54.681** confirms symmetric FOV `[-1.03239, +1.03239,
+1.04476, -1.04476]`. Offline gameplay resumed at 22:43:03; both DLSS eye
corrections submitted successfully and the correction shader compiled in 35 ms.
The eye projection log has proj[8]=0 as intended.
Stereo capture **22:44:18** (Frame-pair-1 left/right PNGs in game directory)
confirms both eyes use the centered FOV with distinct positions about 6 cm apart.
The individual images show a coherent first-person scene facing the waypoint;
they cannot establish whether the moving-light artifact is gone. Capture request
reset to 0. The owner's subsequent response was **“patches seem to be gone.
Though the image is a bit softer.”** No new stereo/camera failure was reported.
Keep centered projection enabled locally. Do not compensate by increasing
render resolution or DLSS quality without considering the performance priority.
A more targeted light-bin correction could retain asymmetric projection and
pixel density, but has not been implemented or validated.
The subsequent DLSS-off visual check was much clearer. Recent unprofiled samples
at 22:50:03–22:50:33 measured 30.0–36.7 pairs/s and 17.0–21.2 ms for both eye
draws combined. These were moving/unmatched scenes, not a DLSS performance A/B;
do not infer a percentage performance cost from them. No graphics setting was
changed by the agent after the owner's DLSS-off choice.

### Standby/reconnect crash after lighting trial (parked by owner)

After Frame woke/reconnected at **22:45:24**, the kernel logged NVIDIA **Xid 32**
on RTX 5090 PCI 0c:00 in `vrserver` at **22:45:25**. SteamVR then failed to acquire
backbuffers; D2R's XR submit blocked for roughly 10 seconds repeatedly. SteamVR's
connection watchdog aborted at **22:46:06**, followed by a compositor segfault
at 22:46:07. D2R's 22:46:08 shutdown access violation at module RVA 0x530ED
matches its pre-existing 20:33:23 report. This establishes ordering, not the
ultimate cause or proof that the new projection is unrelated. NVIDIA describes
Xid 32 as a command-stream/DMA fault; do not diagnose hardware failure from this
single event: https://docs.nvidia.com/deploy/xid-errors/analyzing-xid-catalog.html

The owner confirmed it happened after waking/reconnecting, then explicitly said
**ignore this issue for now and focus on visuals**, and requested another launch.
No crash-related code, driver or runtime settings changed. SteamVR restarted
successfully; D2R relaunched with the same lighting fix and Balanced. The XR
session reached focused state at 22:48:57 and stereo gameplay resumed at
22:49:05 with automatic headset calibration. The owner is evaluating visuals.
Evidence preserved locally under
`build-frame/backups/crash-centered-20261008-224642/`: kernel/GPU records,
SteamVR logs, D2R/ReShade/Proton logs and minidumps. Do not upload raw dumps.

`HookDrawGameScreen` holds one headset pose per pair, right_dt_ms=0.01 preserves
particles. Native frame counters/temporal histories and camera-derived lighting
constants still deserve investigation. Do not zero dt or force identical
view-dependent highlights. The negative sky/fog test makes ReShade Timer changes
a poor first candidate. Original sync stays defer_gpu_wait=0; camera vr_mode=4.

Owner additionally reports a fuzzy, ghosted, vibrating desktop mirror. Each
native Present alternates eyes; this can explain mirror vibration/doubling but
not automatically all fuzziness. Individual eye captures at **22:33:06** do not
show large duplicated scene edges; a partially open/transitioning UI is present.
One still pair cannot rule out temporal reconstruction artifacts.

At 21:33:34 another 614 ms cycle contained a **565 ms pre-render gap**. It followed
`InputMethodChanged` and panel-layout changes. This is a clue to UI/input-layout
rebuilding, not proof of cause, and is separate from the usual 80–100 ms eye-draw
spikes. Do not blame shader compilation without more evidence.

Next performance work should attribute engine scene preparation and worker-thread
cost within each eye draw, then investigate reuse of genuinely eye-independent
work. Preserve separate eye cameras, culling, UI and resource lifetimes. Avoid
reducing image quality or removing effects based only on low spot GPU utilization.


The user also reports desktop mirror flicker. `HookDrawGameScreen` runs a full
left/right pair; each native Present displays that eye's backbuffer. The XR
capture is copied separately after effects; there is no third scene render for
the monitor. The ReShade addon `present`/`reshade_present` callbacks return void
and cannot cancel OS presentation. A stable one-eye mirror would require an
extra copy with the current path. No mirror change has been made. Investigate
safe optional display suppression and measure it separately; do not promise
an FPS gain or minimize the game and disrupt focus/input. Recent Present spans
are roughly 0.8–1.1 ms per eye, including more than display-only cost.


Old SDK runtime logging forced synchronous `fsync()` on the game main thread.
Live `/proc/PID/syscall` samples identified syscall 74 and the exact descriptor
path: **`d2rloader/logs/d2r-vr-vrcam.log`**. Several 540–784 ms pauses had repeated
ext4 journal/page waits, no physical disk reads, and almost all time before
stereo drawing. This was our diagnostic logger, not demonstrated asset loading.

`steamvr/logging.cpp` now queues up to 1024 records of 640 characters and writes
worker batches about every 250 ms, ordinary append, **no forced flushes**. Runtime
logs moved to `d2r-vr-frame.log`; rare SDK startup messages remain in the old file.
Queue overflow drops diagnostic messages instead of blocking gameplay on disk.
Concurrent producer/order/drain test passes under installed Proton.

After deployment the user confirmed improved hitches. 599 main-thread samples
showed no journal/page waits or blocked log file paths. Walking timing windows
had maxima around 38–60 ms; one **475 ms** cycle remained, about 445 ms before
stereo draws. Frame-pair throughput varies by scene (roughly 30–53 pairs/s in
observed windows), not a controlled benchmark or headset refresh rate.

Quiet Proton profile retains Wine errors and VKD3D warnings while dropping
exception/unwind tracing (previously >38,000 lines/second). Do not clear shader
caches. Existing VKD3D cache had about 1820 entries plus additions. Most recorded
hitches had no tracked pipeline creation, but deferred driver/compiler work can
escape those callbacks. Shader compilation remains a candidate, not proven cause.
Native height fog avoids repeated mod fog scans; video preset uses 80% resolution
and medium AO/shadows/atmospherics. ReShade sky/fog compilation was subsequently resolved with the private compiler below.

The 19:49 walking test measured about 42–45 stereo pairs/s in several windows,
with the drawing thread using CPU for 94–97% of its render-pass wall time.
RTX 5090 utilization had a median near 43%. Scene variation prevents claiming a
precise percentage gain; user feedback was “Smoother”. A remaining 504.7 ms cycle
spent 474.1 ms before stereo drawing, concurrent with roughly 27 MB of cached
file reads and a busy main thread. Asset processing is suspected, not proven.
Physical disk reads did not increase in the sampled interval.

Local evidence: `build-frame/foveation-cpu-process-samples.csv`,
`build-frame/foveation-cpu-gpu.csv`, backup
`build-frame/backups/foveation-cpu-20261008-194900`. A continuously busy worker
and ReShade's persistent “Compiling (1 effects remaining)” prompted compiler
investigation. An optional compiler can be preloaded from
`GAME/d2rloader/runtime/d3dcompiler_47.dll` before ReShade, only under the mod.
Do not use the old Windows 8.1/Winetricks compiler: it rejects `ps_5_1`/`vs_5_1`.
The Microsoft Windows SDK 10.0.26100.9169 x64 redist compiler passes a `ps_5_1`
probe on Proton. The full effect compiled in 10.56 seconds; subsequent samples
showed no continuously busy compiler worker. Sky rendering now works. This
adds previously missing effects, so do not treat it as a quality-matched FPS test.
Unfilled ceiling texture slots now use an empty source; the existing procedural
stone fallback remains and ReShade no longer reports missing optional textures.
The prefix's system compiler and stock executable remain unchanged.

## Foveation status

This checkpoint adds an opt-in **eye-tracked VRS prototype** after `b96718a`.
Installed October 8 at **23:18:47**, DLL SHA256
`981e4a19898316b02a729ebc97cbb3ccec8415ea2b0b12ba5cdf3283e8c19875`.
Backup: `build-frame/backups/gaze-pilot-20261008-231847/`. D2R was closed before
replacement; launched normally via `steamvr/run-proton.sh`, offline Nekro.
This supersedes the installed-build information earlier in this document.

Runtime evidence: 23:19:07 `supportsEyeGazeInteraction` succeeded, but the gaze
action remained inactive. The vrlink driver defaults
`shareEyeTrackingData=false`. The owner explicitly approved enabling SteamVR's
**Share eye tracking data to other apps on this PC** setting, including its
general PC-app scope. Set through the running `IVRSettings_003` API, read-back
false -> true, at ~23:21:53. **Do not request this permission again.**
OSC remains off. No headset restart was needed. The next sample reports
171 usable gaze pairs, 1 recovery pair, 2 inactive; the complete shared 9x9 bank
for 3840x2160 took 16 ms to prepare once. Reduced-shading draw counts are nonzero.
This confirms gaze input/VRS use, not a measured speedup or headset quality pass.

Implementation: `steamvr/gaze.h`, `gaze_test.cpp`, optional extension/system
query/action/space in transport, bounded immutable gaze banks in foveation.
`eye_tracked_foveation=1` must be set before launch; `fixed_foveation=1` is still
the VRS master switch. `gaze_foveation_inner=.35` plus a .177 grid quantization
margin; no camera or projection changes. Invalid/inactive/untimed/stale gaze
uses full quality; recovery and large jumps get one full-quality pair. Gaze
direction is projected separately using each rendered eye's rotation/frustum.
No measured fixation depth; generous margin covers close-object parallax.
The last gaze position is never held after tracking loss. Routine logs contain
validity counts only, not gaze coordinates/trajectories. Bank cache is capped at
four viewport/radius configurations; overflow safely shades at full quality.
Default installer remains `eye_tracked_foveation=0`, including Quest 3.

Cross build, host gaze/protected-region and existing camera-math tests, installer
tests and diff checks passed. The standalone probe now has `OUTPUT --capabilities`
to query support without a graphics session. The isolated shader-test Wine
prefix cannot load WineOpenXR (126); **the actual game's Proton session is the
verified capability/sample source**, not that failed standalone probe attempt.
The owner then confirmed **"Clear and stable"** while looking with their eyes,
turning their head and checking creature labels, followed by **"everything seems
good"**. A stationary gaze/full/gaze comparison captured 360 pairs per mode, all
with valid GPU timestamps. Gaze sampling stayed active even in full-quality
mode; only the VRS master switch changed. DLSS stayed off, centered projection
and the working label fixes remained enabled. One hero position was logged in
each capture; view/gaze and ambient animation are not perfectly repeatable.

| Mode | Mean pair cycle | p95 cycle | Mean GPU scene span, both eyes |
| --- | ---: | ---: | ---: |
| Gaze VRS | 22.566 ms | 26.855 ms | 16.042 ms |
| Full shading | 23.240 ms | 29.730 ms | 16.309 ms |
| Gaze VRS repeat | 22.619 ms | 27.026 ms | 16.340 ms |

Mean cycle was ~2.8% lower with gaze VRS, a small preliminary result. GPU scene
spans include CPU queue starvation and did not show a consistent isolated GPU
speedup; CPU time stayed around 14 ms/pair. Do not sell this as a large gain or
as an improvement over fixed foveation, which was not compared here. Artifacts:
ignored `build-frame/gaze-comparison-20261008/{gaze,full,gaze-repeat}.log` and
`summary.json`, sequences 901–903 at ~23:24:10–23:24:38.
The script restored `eye_tracked_foveation=1`, `fixed_foveation=1`,
`profile_capture=0`, `profile_gpu=0` afterward. Game remains running for the owner.
Longer/dungeon tests, tracking-loss behavior and fixed-versus-gaze comparison
remain open; the earlier Frame wake/reconnect crash remains explicitly parked.

Fixed foveation is available: Tier 2 VRS, per-eye optical centers, protected 1x1
ellipse and 2x2 peripheral **opaque** geometry shading. UI, transparency, effects,
shadow-only and unsupported passes stay full quality. Immutable masks are
uploaded once and cached; draw-path lookup uses an epoch to avoid repeated work.
Observed device: Tier 2, 16-pixel tiles; logs confirm reduced-shading draw counts.
User first comparison felt “about the same”; **no measured GPU speedup** claimed.
`[render] fixed_foveation=0` disables; `foveation_inner=0.55` default (0.35–0.85).
Quest 3 does not require gaze for this fixed technique. The eye-tracking source,
tests, settings and validation notes are included in this checkpoint. The
published v0.1.0 preview ZIP still predates these changes.

## Build and verification

Clang 21 with Windows SEH support, MinGW headers/static libraries, CMake 3.28+,
Ninja, OpenXR SDK 1.1.62 and D2RLoader Plugin SDK. See steamvr/README.md for commands.
SDK revisions used:

- D2RLoader PluginSDK: `03a5af9e0feb3f95f95e148e330e01f6635950df` (0.4.0).
- Khronos OpenXR-SDK: `57af7fc61f9f2d492580cb28aab6d0ea59d8d417`.

Run native camera math and deferred-draw tests, `python3 steamvr/session_test.py`,
`python3 steamvr/install_test.py`, shell syntax and diff checks. Windows executable
tests for checked memory reads/writes and buffered logging run through Proton.
The checkpoint verification passed those tests, the 17-call NGX ABI comparison
under both MSVC and MinGW code generation, the profile report on recorded frames,
the cross build and shell/diff checks. The rebuilt DLL with defaults matching
the tested settings has SHA256
`02ed0a87c998cba34303027488672c417ca15746a22be491e07effe8ae2e34c0`.
That build was not installed just to change defaults. The newer gaze pilot
installed later is identified in the foveation section above.
The Windows OpenXR loader is cross-built via `cmake/openxr-loader` with
`cmake/openxr-loader-toolchain.cmake` and packaged with its license notices.
Loader build/export/dependency checks do not establish native Windows gameplay.

## Owner-machine paths and diagnostics

These are local investigation references; friend setup must use its own paths.

- Checkout: `/home/berkybear/diablo2-vr-port`, branch `vr/steam-frame-proton`.
- Steam: `~/.local/share/Steam`, app **2536520**.
- Game: `steamapps/common/Diablo II Resurrected`; prefix:
  `steamapps/compatdata/2536520/pfx`.
- Dependencies: `~/vrdeps`; compiler:
  `~/Android/Sdk/ndk/29.0.14206865/toolchains/llvm/prebuilt/linux-x86_64/bin`.
- Initial complete install/save backup: `build-frame/backups/20261008-171211`.
- Latest pre-deployment snapshot:
  `build-frame/backups/label-order-signature-20261008-230315`.
- Logs: `GAME/d2rloader/logs/d2r-vr-frame.log`, startup `d2r-vr-vrcam.log`,
  `GAME/ReShade.log`, `build-frame/steam-2536520.log` and routing log.
- Local-only profiling: `build-frame/walking-process-samples.csv`,
  `blocking-file-operations.jsonl`, `buffered-logging-process-samples.csv`.
- Game-memory snapshots, saves, account data, logs, builds and captures are
  excluded from git and the release. Do not publish the local game-code dump.

For desktop captures find the current game window with xdotool; IDs/PIDs change.
Frame capture helper in the separate DevilutionX checkout returned a plain blue
image during the latest PCVR session, so do not claim it captured these labels.
Read `/proc` for diagnostic sampling; kernel perf policy blocks normal perf.
Avoid global kernel changes and intrusive tracing while measuring frame times.

## Friend agent's runtime checklist

Read the current WiVRn dashboard setup and compose any required environment
prefix with our Steam wrapper, retaining one `%command%` and the same prefix.
Check a stock OpenXR game first, then our `openxr-probe.exe` / native logs.
The transport requires **XR_KHR_D3D12_enable**, translated by WineOpenXR under
Proton. WiVRn being OpenXR-compatible alone does not prove the game binding works.
Do not switch the owner's active runtime or start another headset stream during
an existing Frame test. Keep Windows launcher and Quest controller mappings
marked untested until verified. No friend invitation is sent without a username.

## Owner headset recording tool

Framecorder 0.1.1 from https://framecorder.coah80.com/ is now installed on the
owner's Frame, using its checksum-verified released ARM64 package. Dashboard,
sync service and automatic update timer are active. The SteamVR dashboard's
Framecorder tab provides Record/Stop; saved videos go to `~/Videos/framecorder`.
Manual recording is configured, game audio on, microphone and instant replay off
while diagnosing D2R performance. Left-stick replay clipping is therefore off.
A four-second headset-source recording was verified: 1920x1080 HEVC, 90 fps,
361 frames and visible passthrough imagery. Temporary test files were removed.

It currently uses SteamVR's headset-view fallback. Direct panel capture needs a
one-time administrator password entered locally on Frame (sudo is not passwordless):
`sudo -v && ~/.local/bin/framecorder-setup --unlock`. Do not request/store the
password in chat or restart the tab during an active recording. The official
installer also exposes the unlock step. No Framecorder source was modified.
