# D2R Steam Frame PCVR tasks

Updated 2026-10-10. Current target: native Windows D2R, SteamVR streaming to
Steam Frame, offline local characters only. PR #1 is merged into main; current
work on `vr/quest-2-3-support` tests Quest compatibility. Linux/Proton evidence
below is historical.
The DevilutionX standalone/tabletop project remains parked separately.

Unchecked tasks remain open, including implemented changes awaiting headset
verification. Build checks and automated tests do not establish visual correctness.

- [x] **Quest 3 + Steam Link/SteamVR gameplay smoke test.** Owner reports
  "ok it all works" with the unchanged `f559b3e5...` DLL. Probe/session/tracking,
  calibration save/close, accepted outdoor melee and normal exit are logged.
  Cave entry and weapon-set switching are not separately logged in this run;
  overall feedback is not exhaustive feature coverage. See QUEST_TESTING.md.
- [x] **Quest 3 + Virtual Desktop/VDXR smoke test and color correction.** The
  Windows wrapper supports explicit `--frame-vr --frame-vdxr` without changing
  normal Play or the system runtime. All 24 Python tests pass. The connected
  Quest 3 passes the full VDXR D3D12 graphics probe and reaches tracked gameplay,
  with owner-confirmed normal colors after the sRGB correction. All 34 native
  tests and a GPU regression pass; 45 accepted outdoor melee reports, zero
  position mismatches and normal exit are logged. Cave/set-swap/calibration
  coverage is not separately established on VDXR.
- [ ] **Virtual Desktop + SteamVR.** This separate route remains untested.
- [x] **Remove Journal from the wheel.** Label/icon, hover and dispatch removed;
  tabbed game-menu access (Frame Left A / Quest Left X) retained. Native tests
  and generated wheel preview pass; final cleanup has no fresh headset run.
- [ ] **Physical Quest 2 compatibility.** Requires its own headset test;
  do not infer it from Quest 3. See [QUEST_TESTING.md](QUEST_TESTING.md).

- [x] **Owner-requested body calibration and shoulder swapping.**
  Implemented first-load upright T-pose capture with both triggers and
  Left Grip + Y → Calibrate body for repeats. Eye height/span persist atomically;
  height sets world scale and arm bones fit reach while controller motion stays
  physical 1:1. Dominant trigger over either shoulder switches native sets I/II,
  confirmed through the equipment snapshot. Hip anchors are removed. Right/Left
  swap hand is selectable in VR settings. Installed `e27084ed…` at 17:39 PDT,
  with 32 native + five installer tests passing. Live menu calibration saved
  eye height 1.65854 m / controller span 1.50859 m at 17:41 PDT; the INI and
  return to gameplay are verified. Body-fit and shoulder-swap owner questions
  are pending. See BODY_CALIBRATION.md and WINDOWS_HANDOFF.md.
  Owner confirms saving/closing and the VR settings calibration entry work as
  intended, with a misleading initial floor message.
  Short resting arm span was wrongly classified as a floor failure; corrected
  `a1e2006a…` installed at 17:43 after normal exit, preserving saved calibration.
  All 32 native tests pass; corrected feedback awaits a headset retest.
  The combined `f559b3e5...` build subsequently completed the offline retest;
  owner reports "it all works". Automated hand/shoulder and calibration coverage
  remains separate from that overall headset confirmation.

- [x] **Native Windows visual/transition fixes.**
  Owner reports the performance candidate runs superbly. Hands/weapons and feet
  appear, but torso and belt potions do not. A dungeon transition loses wrist
  HP/mana data and free-swing damage; trigger attacks still work. Follow-up DLL
  `996cc430…` is installed with typed views for SteamVR typeless textures,
  recoverable callback tickets, and low-rate wrist/melee/body diagnostics.
  Owner confirms belt models now render; torso and cave swings still fail.
  Cave snapshots remain fresh but native identity expires. Next build `5847a204…`
  refreshes identity from the checked UI-thread local-player accessor; 30 native
  tests pass. Installed at 17:14 PDT after normal Save & Exit; graphics unchanged,
  belt_forward=0.06 for the owner's closer placement preference. Cave retest
  confirms wrist values now survive the cave transition. Swings still fail:
  callbacks run but the player-position check rejects cave requests. Preserve
  that guard while resolving renderer/native coordinate conversion.
  At 17:52 the owner reproduced this in a new cave run. Paired read-only
  positions show X/Z elevation shear of sqrt(3/2) times terrain height. Candidate
  `f559b3e5...` corrects the saved-pose comparison and enemy capsule placement;
  the 0.65 m warp limit remains. All 33 native tests pass, including captured
  cave cases, contact/miss geometry and real warp rejection. Installed at 17:55
  after normal exit with backups and preserved INI/graphics. SteamVR offline
  retest completed: repeated native contacts in `act1_caves` have
  `position_mismatch=0`, followed by return outdoors and normal exit at 18:02.
  Owner reports "it all works" after the cave fix.
  Normal-posture captures reproduced torso/shield clipping. Installer parsing
  emitted a malformed first camera section, so Win32 ignored near=0.2 and used
  1.5. Correcting the installed INI live fixed torso/arms/shield clipping (owner
  confirmed). Installer and Win32 profile regression test fixed; five tests pass.
  SteamVR/Steam Frame, local offline only.
- [ ] **Performance measurement still pending.**
  Native baseline startup worked and felt good, but Akara dialogue froze desktop
  and headset. Dump preserved; candidate `9b3ce9df…` now includes queued window
  activation, CPU caches, background HUD painting and the INI reload guard.
  Akara opened/closed twice in the candidate log, followed by normal exit.
  Collect controlled Windows
  baseline/candidate timings at identical settings. No measured Windows speedup yet.
- [ ] HoverUnit signature-check failures remain unresolved. Keep guards enabled.

- [ ] **Current priority: fresh Windows + Steam Frame setup.** User is switching
  OS and requested a complete GitHub handoff. Start at ../WINDOWS_HANDOFF.md,
  release v0.2.0-windows-handoff baseline package, branch vr/steam-frame-proton.
  Establish native Windows install/launch/controls and streaming stability first.
  Baseline rollback and a full Linux reboot both retained the stalls/quality
  loss. Fresh SteamVR logged 119 reset requests before D2R launched. Manual
  50 Mbps trial was not evaluated; automatic/200 Mbps was restored. Do not
  assume the candidate DLL, Windows or a bandwidth limit has fixed this.
- [ ] Resolve October 10 severe stall/quality regression before more optimization.
  Owner has gone to bed: no further headset asks or launches tonight. Restored
  pre-performance DLL `16e24f55…`, wait=0/radius=300, profiling off; no other
  graphics/network/SteamVR changes. Both CPU candidates remain backed up and
  uncommitted source is retained. This rollback has not been headset verified.
- [ ] Diagnose streaming delivery alongside game stalls. Saved Link histories
  show 147 reset requests in ~4 minutes, reported receive p95 80.81 ms/max
  734.13 ms, while encoder p95/max were 8.87/12.37 ms. Similar resets predate
  the CPU candidate. This does not identify Wi-Fi versus driver/client handling
  or measure actual resolution/bitrate. Use the tested `stream_report.py` on
  fault-history CSVs; see PERFORMANCE.md and latest-perf-regression.json.
- [ ] Runtime-check the staged INI reload guard: profiling/unrelated VR options
  no longer force a half-second model visibility rebuild. Real ring/radius
  changes and startup retries remain. Cross-build passes; candidate is in
  latest-perf-offline-stage.json, not installed. Compare after baseline recovery.
- [ ] Verify asynchronous HUD painting while running and using the wheel:
  owner reports movement hitches after the first performance pass. Sequences
  10108–10110 captured 20 input/UI spikes of ~12–13 ms and one 173 ms game
  update. Real GDI painting costs 10.427 ms in the Proton check. Moved painting
  to a bounded worker; stale clickable contents are guarded, images match
  synchronous output, worker lifecycle tests and DLL build pass. Installed
  `d44762d4…` after Save & Exit and relaunched (subsequently rolled back). Next 1,080 moving pairs had no
  input spikes over 4 ms (maximum 1.811); worker paint 10.1–10.3 ms, frame upload
  1.16–1.27 ms. Owner subsequently reported severe stalls/quality degradation;
  neither that symptom nor wheel/wrist usability is established as fixed.
- [ ] Isolate large LevelTranslationLayer simulation stalls during traversal:
  173–753 ms observed, without recorded pipeline creation or time in measured
  room discovery/extra VR tiling. The asynchronous HUD fix does not address
  these stalls. Inspect native model/room activation/loading next; shader
  compilation has not been established as their cause.

- [ ] Finish movement/menu/scenery checks for October 10 CPU performance pass:
  per-thread hook/memory counters, generation-cached VRS and UI-world pipeline
  lookups, native callback locks, guarded local memory copies, per-instance
  portal batch classification, sparse routine GetThreadTimes, idle mouse-query
  removal, startup-only optional UI-trace callbacks and cheaper launcher polling.
  Memory fault/handler tests and microbenchmark pass on Proton; worker counter,
  cache/lock stress, portal batch and launcher identity/cleanup tests pass.
  Installed; owner confirmed normal image. Seven 360-pair captures saved with
  GPU profiling off. Code-only A/B did not show an FPS gain (31.38→31.69 ms;
  slightly different viewpoint). Deferred wait then gave 24.60 ms, radius 210
  gave 22.70 ms. Later repeats ~22.5 ms changed view, so do not attribute a
  precise percentage to radius. See PERFORMANCE.md for evidence and limits.
  Actual video settings remain 100% resolution, shadows/AO 4, DLSS off.
  Trial wait=1/radius=210 was rolled back; source defaults unchanged.
- [ ] Once baseline delivery is stable, independently test shadows 4→2, radius
  210 and deferred waiting while moving, using menus and viewing scenery. Preserve
  baseline settings (wait=0/radius=300); recheck competing CPU load. Predicted
  radius-squared model counts remain hypotheses, not established savings.
- [ ] Transport lifecycle/state lock separation or a dedicated XR thread:
  current recursive mutex still covers WaitFrame/GPU wait/EndFrame. Nested
  callers and Shutdown share it, so unlocking one lexical scope is insufficient
  and blindly releasing it risks session/resource destruction. Audit ownership
  and test reconnect/shutdown before changing this path.
- [ ] Headset-verify projectile spell aim with **Teeth**: point the visible wand
  left/right while looking elsewhere, then cast with a creature selected off
  that line. Fan should center on the weapon rather than the selected creature.
  Casting shaft pose, native behavior metadata and scoped target/assist override
  are implemented for wheel and quick-cast inputs. Host tests, seven native
  signatures and all 429 local skill-row layouts pass. Also check a summon or
  corpse skill retains its native selection. Full hand-origin/up-down spell
  flight remains a separate pending spell-family adapter task.
- [ ] Headset-verify belt attachment while turning, stepping and crouching:
  waist equipment now follows tracked position and horizontal facing even with
  hands lowered or a bottle held. Pitch/roll stay excluded, vertical gaze keeps
  the last reliable heading, and unused bottles return to the current waist.
  Holster draws use body-relative hand motion. Host regression coverage passes;
  see latest belt-follow deployment in HANDOFF.
- [ ] Headset-verify original 3D potion belt: all 15 native models/textures are
  prepared locally. Right Grip grabs the closest occupied bottle; bringing its
  opening to the mouth while tipped for 0.25 s requests one native use. Release
  returns an unused bottle. No duplicate held/belt model; native confirmation,
  refusal and mixed-column refill retain the captured identity. Host ownership,
  nearest-slot/sip tests, all-asset conversion and D3D12 stereo readback pass;
  synthetic stereo preview inspected. Scene/arm occlusion and transparent liquid
  remain outside this pass. See latest potion deployment in HANDOFF.

- [ ] Match trade menu backgrounds in both eyes: tabbed-menu opaque backing now
  extends to Vendor/Trade/Gamble panels. Build, panel routing and GPU color/alpha
  tests pass; installed/relaunched, awaiting headset verification.
- [x] Laser-activate the vendor item's Buy popup. Native controller cursor
  snapping overwrote correct Yes/No hover with the vendor grid underneath.
  Suppress that snap only during owned laser callbacks, preserving controller
  layout/navigation and skill assignment. Regression tests, live Yes/No hover
  and No cancellation pass; installed after Save & Exit. Owner confirmed
  "it works" in the headset. See `build-frame/latest-buy-popup-deployment.json`.
- [x] Always show native slot shortcuts beside spell wheel icons. Current slots
  are B/X/Y/RB and LT variants. Owner approved Left Trigger + face buttons,
  with Left Grip selecting alternate four. Implemented with separate native/VR
  labels in wheel/editor and guarded input ownership; all four LT slots have
  editor regression coverage. Host tests, preview and cross-build pass. Staged
  in `build-frame/latest-quick-cast-deployment.json`; installed after verifying
  game closed, relaunched. Owner verified shortcut combinations working.
  Alternate-slot assignment has automated coverage; a separate headset
  edit/reload check remains part of broader binding coverage below.

- [x] Owner verified Left A now opens the tabbed menu directly at startup.
- [x] Headset-check laser binding editor: EDIT SKILLS → slot → learned skill;
  owner reports "ok it works great."
- [ ] Broader binding coverage: clear/reassign, both weapon banks, and persistence
  after Save & Exit/reload.
- [x] Verify matching black backgrounds for the tabbed menu in both eyes.
  Owner explicitly chose this over further transparent-backdrop debugging.
- [ ] Deferred by owner: native tabbed-menu button legends should be usable
  without VR shortcuts consuming those buttons or closing a nested panel.
- [x] Correct town portal tilt and displaced center. Stereo screenshots show
  separate dark center/glowing ring despite the active smoke correction. Native
  scripted-mesh facing candidate built/installed; targeted to town-portal asset
  paths, shares center-eye position and keeps the effect upright. Activated, but
  owner video/new stereo pairs still show separated pieces. Second candidate
  recenters the captured native center offsets, narrowly scoped to NewStuff's
  center-position envelope. Owner verified the outside endpoint looks great,
  but the linked town endpoint still separates. A live trace proved the same
  asset has a pitched center offset there: full 3D radius 3 around `(0,4,0)`,
  rather than horizontal radius 3 at Y=4. Third candidate handles the full 3D
  shell and uses one shared authored center, with asset/transform guards intact.
  Captured-coordinate/yaw/pitch tests, 12 signatures and cross-build pass.
  Installed after Save & Exit; owner verified both ends: "ok it all works now."
  Other cosmetic/portal variants remain untested. Record:
  `build-frame/latest-portal-return-deployment.json`. See HANDOFF for evidence.

User verified the changes so far in the 18:20 build, then reported inventory
closing on laser clicks and no visible belt. Individual regression scenarios
below remain explicit so they are not lost after that general check.

Current priority: **motion-controlled VR gameplay**, following the owner-verified
lighting, label and eye-tracked foveation work. See [GAMEPLAY.md](GAMEPLAY.md)
for the first installed prototype, exact controls, limitations and test order.
Broader performance/visual regression checks remain below.

- [ ] Keep ground smoke and other screen-facing particles stable during head
  yaw/pitch/roll: native view-position billboard prototype built; shares the
  center eye position across both eyes. Confirm runtime activation and headset
  appearance, including walking around the effect and checking ordinary flames.
- [ ] Keep the rendered body beneath the tracked neck during 6DoF movement:
  prototype built with anatomical neck offset, stable standing controller scale
  and grounded leg IK on recognized rigs. Body/bow math tests pass. Verify
  leaning, small room steps, crouching, and weapon alignment in the headset.
  Native actor/collision movement remains controlled by the game.
- [x] Correct left-eye shoulder flashing even while still with hands lowered.
  Live Nekro trace found neck-follow disabled in 6/50 left-eye poses while the
  matching right-eye poses stayed aligned. Move transient frame gating from
  the background input timer to the skeleton hook; candidate built, body and
  portal math regressions pass. Installed after Save & Exit; post-install trace
  shows all 160 poses aligned and all 79 complete pairs matching. Owner confirmed
  "Flashing stopped." See `build-frame/latest-body-pose-deployment.json`.
- [x] Recover preset-induced blur/ghosting: owner restored FXAA through Video
  options (`Anti Aliasing` 3 → 1) with DLSS off and 100% resolution scale, and
  reported **"Clearer now"** on October 9. Recheck AA after changing presets.
- [x] Correct DLSS washed-out colors/artifacts with the native depth-pass
  correction installed October 9 at 16:42. Build and 64 GPU shader cases pass;
  owner confirmed **"Only blur remained"** in the Balanced comparison.
- [ ] Improve remaining DLSS clarity: owner reports a low-resolution appearance
  with Balanced and Quality, even stationary; native/FXAA clear. Verified Quality
  capture confirms continuous depth. Shared-range correction now improves
  smoke/effects in Quality; overall clarity versus native remains to be assessed.
- [x] Headset-verify smoke/effects improvement from shared-depth-range correction:
  owner confirmed **"yes it looks clearer around smoke and effects now."** Replaces producer-only
  normalization with coherent frame constants for native depth and particles.
  17:03/17:07 builds installed hooks but did not activate. Bounded debugger trace
  proved the mapped upload write failed with ERROR_NOACCESS (998), not a failed
  projection guard. 17:12 candidate uses the frame-owned mapped pointer directly;
  a D3D12 GPU readback test reproduces the old failure and verifies the new store.
  Build/tests pass; installed/relaunched at 17:13 after Save & Exit.
  Runtime range verified in gameplay at 17:13:32; Quality smoke improvement confirmed.
  Native GPU capture hotkey did not arm; no new depth histogram yet. See HANDOFF.
  This addresses a verified depth-contract mismatch; remaining DLSS blur is not
  yet isolated. Keep DLSS off as the default/clarity baseline.

## In-headset VR settings — owner request, October 9

- [x] Add Controls, VR and Video pages on a room-anchored compositor panel with
  right-hand laser input, draft Apply/Close, live INI reload and eye-height reset.
- [x] Expose motion combat, seated belt/holsters/reach, smooth/snap turn settings,
  fixed/eye-tracked/off foveation, added sky/fog/ceiling and draw distances.
- [x] Add Left Grip + Left Y shortcut independent of motion combat and a wheel
  entry; consume menu input and block world actions while the panel owns input.
- [x] Preserve unrelated INI entries, back up before the first save, reject
  conflicting same-key edits and display runtime gaze/VRS availability.
- [x] Cross-build, test draft/ownership and Win32 INI transactions under Proton,
  and inspect synthetic page previews. Candidate staged separately.
- [ ] Headset-verify pointer alignment, readability, main-menu/in-world access,
  apply/cancel/reopen, motion-off access, live foveation switching, height reset
  and no leaked combat/native-menu input. See GAMEPLAY.md for the test sequence.
- [x] Fix invisible settings panel after Left Grip + Left Y: owner reports
  movement is blocked without a visible panel. Logs show invalid-pose frame
  rejection. The 17:36 laser fix alone did not resolve it. A one-frame debugger
  capture proved Proton's mixed-layer converter overlaps 112-byte quads in
  48-byte allocations. A transparent quad prefix makes its allocation large
  enough; regression and cross-build pass. Installed/relaunched at 17:43:12;
  runtime submission result 0 confirmed; owner verified **"Visible and usable"**
  after testing the panel/laser/tabs. Broader Apply/cancel checks remain above.

Native DLSS/textures/shadows remain in the game Video menu. VR source resolution
now has an independent Video setting; see [VR_RESOLUTION.md](VR_RESOLUTION.md).
The panel is not a binding editor and does not pause simulation.

## Next interaction work — owner request, October 9

- [x] Correct wrist-vitals reveal to OpenXR left grip **-X**, the back of the
  wrist. The owner confirmed a watch-style gesture and reported the initial
  +/-Y inversion invisible. New pose/heading/distance tests cover the actual
  anatomical axis, upright text and lost right-hand tracking. Reach is 75 cm;
  preserve the brief critical-status cue. Owner confirmed visible and upright.
- [x] Generate gothic life/mana orb artwork from a local native HUD reference;
  embed it in the DLL, wire live fill levels/numbers and preserve transparency.
  Full/partial/empty/unavailable-state previews and atlas-isolation checks pass.
- [x] Verify revised wrist gesture, text direction and orb readability in headset:
  owner confirmed **"Visible and upright"** after the -X watch-face correction.
- [x] Add native icons to the wheels based on the actual spell/item in each
  slot; refresh on assignments, weapon swaps and belt changes. Native cache is
  prepared locally by the installer, not included with distributed artwork.
- [x] Generate and embed a matching gothic wheel frame with eight slots, center
  weapon socket and footer buttons; align ray selection with the painted slots.
- [x] Headset-check themed wheel: owner reports it works; saved Raise Skeleton
  assignment initially appeared late (whole slot, not just its image).
- [x] Read the controller binding table independently of native mouse/keyboard
  mode; retain native active bank and prefetch assigned icons before opening.
- [x] Verify saved Raise Skeleton appears on the first wheel opening after load;
  owner confirmed all fixed with the controller-only reader.

The owner will test the bow-height correction later. Work can proceed without
another headset capture. See [INTERACTIONS.md](INTERACTIONS.md) for the concrete
control design, current code capabilities and implementation order.

Owner scope refinement: equip/select a spell, point the hand and pull the
trigger. Ready projectile spells launch on press, with snappy response and their
full visual effects; no release-to-cast or gesture wind-up. Native resource and
cast-rate gates still apply. Since 2026-10-21, **gesture casts are in scope** for
skills that don't shoot (self buffs, armors, novas, shouts, imbues); see #226.

- [x] Design reachable weapon-set holsters, spell wheel and per-family casting,
  grab/drink potions, wrist gauges and a handheld/pinned quest journal.
- [x] Implement session-tagged equipment/belt/skill/vitals snapshots, per-hand
  ownership, native transaction expiry/confirmation and cancellation/rearm.
- [x] Implement radial wheel with localized names, selection latch and native
  button fallback. Native icons added; charge/readiness metadata remain pending.
- [x] Implement real potion type/count, exact-item grab/drink and quick use,
  authoritative removal acknowledgement, reachable front belt and wrist gauges.
- [x] Implement configurable native-set I/II holsters, seated positions, explicit
  stow/draw and set confirmation; no arbitrary inventory equipment moves.
- [x] Implement quest UI-only extraction, retained stereo scene, held/pinned
  placement and native page-coordinate pointer mapping with flat-panel fallback.
- [ ] Verify all new interactions in headset; candidate is staged, not installed.
  Check empty/identical weapon sets, belt ordering/refill/refusal, UI crop, native
  tab clicks, stow visibility and tracking/recenter cancellation.
- [ ] Add item friendly names and affix colors, charge/cooldown badges, detailed bottle
  geometry, optional scene-depth occlusion and additional journal pages.
- [ ] Implement casting adapters for projectile, channel, ground/summon,
  unit/corpse, self-buff and aura skills behind the same point-and-trigger
  controls. Prioritize a simple projectile: prompt accepted spawn, coherent
  hand-origin flash/trail/light/impact, correct 3D contacts and stereo timing.
  Current native XY aim is the fallback; each family's collision, resources and
  native behavior need separate validation. Measure input-to-visible latency
  before tuning travel speed; do not apply arrow velocity to all spells.

## Current bow test build

- [x] Replace release-button pulses with one queued native basic attack, retaining
  native ammo/damage ownership; expire and cancel stale requests.
- [x] Fix opaque-handle/native-owner confusion that prevented projectile-speed correction.
- [x] Freeze two-hand 6DoF release; pose the flying arrow on its path, shared between eyes.
- [x] Match held-bow input timestamps to rendered attachment poses.
- [x] Sweep the full 3D arrow path against body capsules and native barriers;
  retain native hit/pierce handling, and clean up removed missiles/visuals.
- [x] Correct parent/mesh binding after the owner reported arrows originating
  overhead. Only the release mesh gets the world transform; initializer order,
  authored child height/scale and recycled entity regressions pass. Installed
  at 12:11 on October 9; not relaunched because the owner will test later.
- [ ] Verify the arrow-height correction in the headset before marking physical
  flight verified; see HANDOFF.md for candidate and rollback hashes.
- [ ] Headset-verify immediate release, faster aligned flight, one ammo consumed,
  empty quiver, cancellations, wall/close-range contacts and melee regression.
- [ ] Refine uneven terrain, missile-proxy source positioning/rooms, special arrow
  skills and additional bow rigs. This build covers ordinary basic arrows only.

## 1. Stability and calibration — next headset pass

- [x] Restore first-person stereo after the 20:39 saved mode switched to third
  person. At 21:04, changing only `vr_mode=4` fixed the camera pivot and stereo;
  owner confirmed both normal, ReShade effects still enabled.
- [x] Install the native-only first-person guard: repair old saved modes,
  ignore unsupported F1–F3/F12 camera switches, retain flat-mode selection.
  Installed after Save & Exit at 21:09; live saved-mode repair and F2 rejection
  checks passed. Broader gameplay/flat-mode visual regression remains open.

- [ ] Verify the background scan freeze fixes in a longer play session and in
  a dungeon. Checked memory reads fixed the hero-facing scan crash; the latest
  build also snapshots the fog scan and checks writes. Proton memory tests pass.
- [ ] Verify the corpse at the spawn location no longer drives or duplicates
  the VR body. The latest build filters player skeletons by proximity and original
  posed head height. Selection tests pass; this is a heuristic, not live-unit-ID
  binding. Check death, respawn, crouching and attack animations; retain the
  corpse's normal game behavior and items.
- [ ] Verify automatic recenter on every saved-game load after moving during
  launch. The latest build calibrates on the first focused, tracked world frame
  and recenters facing. Opening inventory/dialogue must not recalibrate.
- [ ] Verify height reset: hold **left grip + click left stick**. Preserve map
  on plain stick click, horizontal room movement, heading and physical eye
  separation. Input/math tests pass. **F11** performs full position/facing reset.
- [ ] Verify Exit Diablo II from the headset returns Steam to **Play**, without
  a separate PC-side Stop. The VR-only launch monitor cleans this launch's
  lingering Proton helpers after the game exits. Three process tests pass;
  leave the Steam client, SteamVR and unrelated programs running.

## 2. Menus and HUD

- [x] Fix ordinary creature name/health stereo depth. Centered projection removed
  large separation; drawing the health panel after current-eye tooltip updates
  fixed reversed disparity. Corrected build installed 23:03; owner **“fixed :)”**.
  Both fixes now default on; DLSS off is retained for clarity. See HANDOFF.md.
- [ ] Broaden label checks to close/distant creatures, head movement, bosses,
  rapid target changes, NPCs and ground items. Historical investigation follows.
  Earlier evidence: user reported doubled labels on normal monsters (name and health in one label).
  The new unit-label projection hook retains native world anchors and uses the
  exact current-eye scene matrices, avoiding inline flat-frustum rebuilding.
  Stereo anchor/ray convergence checks pass; they alone did not fix the visuals.
  The first matrix-only prototype still doubled in the user's test (native
  projected positions differed by less than a pixel). The next build combines
  the original foot X/Z and head height in world space before projecting once,
  replacing native projected-foot-X/projected-head-Y mixing. Verify this new
  anchor with head pitch/roll; the user tested it and **labels still double**.
  Stop treating projection math tests as visual proof. Next inspect single-eye
  label draws, cached UI layout, rectangle placement and eye/frame pairing.
  The 20:16 paired Fallen Shaman capture shows one box per image at the other
  eye's position. TooltipsPanel updates the separate MonsterHealthPanel during
  drawing, after it may have painted already. The 20:33 deferred-paint prototype
  was withdrawn during camera diagnosis and archived in the stereo-regression
  backup. The camera failure was subsequently isolated to saved third-person
  mode; this does not validate the prototype. Instrument actual paint/event
  ordering and renderer state in first person before retrying. Check target
  changes, NPCs, bosses and menus as well as convergence.

- [ ] Verify inventory stays open when selecting/dragging items with the laser.
  User found that the first trigger click closed it. Logs show controller-layout
  inventory opening then closing immediately on laser mouse down, with neutral
  mirrored pad input. Native inventory now opens with the mouse-layout `I` key;
  suppress stale gamepad actions in menus and consume a menu trigger until release
  so closing a panel cannot attack. The 18:27 virtual-key toggle failed to open
  inventory in the user's test; logs confirmed button detection without a panel
  event. Correct injection to scan-code down, hold across frames, then key up;
  release on focus/tracking loss. Corrected build installed and launched at 18:34;
  live logs confirmed initial opening, but the user found that Left X stopped
  opening menus after switching to other panels and closing them. Replace the
  keyboard toggle with native UI-thread PanelManager actions: close open menu
  panels, otherwise open inventory. Keep HUD/map alive. Test repeated inventory
  -> other shortcut panels -> Left X close -> Left X reopen cycles and item dragging.
  The 18:45 build's live logs confirm inventory reopened after closing inventory
  and character panels; visual confirmation and repeated cycles remain pending.
  At 21:35 another failure was isolated to desktop focus: a terminal was active,
  while movement continued through XR. Restoring game focus restored Left X
  (owner confirmed). Remove that gate from the native D2R UI action; keep OS
  mouse/key injection gated, request focus only for deliberate menu actions.
  The fix was installed at 21:40 after Save & Exit. Intentional focus loss now
  permits native inventory open/close, verified by the owner and logs. Laser
  input still needs desktop focus: the OS rejected programmatic focus recovery.
  The owner accepts this requirement and explicitly asked not to pursue it
  further. Documented in INSTALL/README; keep D2R focused for laser clicks.
- [ ] Verify NPC conversation panels are stable and identical for both eyes.
  The user reported double vision and choppiness. The latest build disables
  eye alternation and VR camera/projection for panel frames; headset check pending.
- [ ] Verify the implemented right-controller cyan laser in title, dialogue,
  inventory, character, skill and pause menus. Right trigger clicks/drags;
  right B backs out. Test item dragging, tooltips, focus loss and panel placement.
  Menu intersection math checks pass; the user has used dialogue selection.
- [ ] Design health/mana and belt presentation for VR. User did not see the belt
  in the latest build and prefers avoiding a constantly floating panel. Prioritize
  an on-demand wrist/waist belt and quick potion controls; compare compact wrist
  health/mana, situational feedback and an optional persistent display. Use original
  icons and keep potion counts readable. Wrist vitals are user-verified;
  the 3D potion belt is now installed and awaits the check at the top of this list.
- [ ] Extract the real UI texture with correct alpha, rather than cropping a
  backbuffer containing scenery. Upstream HUD sharing exists; native OpenXR
  transport does not consume it yet. Keep menus/text at full quality.
- [ ] Provide pointer selection and quick use of belt potions without opening
  inventory; verify tooltips, potion types, hotkeys and feedback.

## 3. Weapon and spell interaction

- [x] Owner verified right-grip/button attacks, then rejected the interaction:
  no grip-to-arm step, preselected target or button simulation for physical melee.
- [x] Implement free-swing swept-capsule contacts with native exact-target basic
  damage on the authoritative thread, per-stroke deduplication and contact haptics.
  Category weapon volumes / inferred creature capsules are approximate.
- [x] Fix captured strong-slash lockout: bounded recovery without a still-hand
  requirement, rate-aware tracking guards, no derived upper-speed cutoff, and
  length-preserving arc sweeps and bounded stroke-onset history. Host replay
  improves 4 to 48 target/stroke contacts. Owner reported improved slashes;
  remaining fast-motion misses were addressed in revision 4 below.
- [x] Match melee input/render timestamps and use same-pose world
  geometry; synthetic delayed-render regression reproduces and fixes lost fast
  slashes. Revision 4 installed and owner verified **"works great"** on October 9.
- [x] Verify the free-swing sword baseline in Steam Frame. Final run recorded
  58 physical contacts / 58 native accepted hits, with no rejections.
- [ ] Extend headset checks across weapons/creatures, damage/procs and resets;
  refine dimensions/axes and add attack-speed scaling.
- [x] Implement the requested removal of random misses for the local player's
  melee, arrows and magic against creatures: exact hit-roll override plus random
  block/dodge bypass, with native eligibility/range/collision and damage/costs.
  `contact_hits=1`, dependent on motion controls; incoming attacks and summons
  retain native rules. Tests/build pass; three live melee overrides confirmed.
- [ ] Verify outgoing arrow/spell hit checks for deterministic hits. Preserve
  critical/damage/progression and resource systems; physical bow flight is now prepared for testing.
- [x] Implement the first right-handed motion-input prototype: deliberate melee
  swings, bow nock/draw/release, hand palette for eight native skill assignments
  and weapon swap, belt grab/raise/tip-to-drink, hand aim and gesture haptics.
  Portable gesture tests and cross-build pass. Bow drawing and free-swing sword
  combat are verified; palette, potion and spell interactions still need checks.
- [ ] Complete the remaining headset scenarios in GAMEPLAY.md. The owner has an
  offline bow-test character. Keep Nekro and both characters' inventory/progress.
  First bow test: equipped hunter's bow (`hbw`) and arrows (`aqv`) were detected,
  but owner could not nock/draw/fire. No successful gesture events were logged.
  Revised build installed 00:01:59 October 9: removed left-grip prerequisite,
  uses a larger visible string-side grab region, allows squeeze-before-approach,
  trigger hysteresis, explicit B cancel, automatic bow mode on equip, a visible
  arrow/V-string guide and grab/draw haptics. Added rate-limited readiness logs
  to distinguish tracking/input gates from failed gestures. Tests/build passed;
  Owner subsequently confirmed drawing and shooting work, but the native bow
  draw and rumble happened only after trigger release. Read-only capture caught
  that delayed native wind-up. New procedural pose test drives the existing
  string/limbs/arrow while trigger is held; guarded to the hero's attached bow,
  fixed string/limb lengths from runtime bind geometry, unknown-rig fallback.
  Added stronger checked haptics and draw-progress pulses. Geometry/timeline tests
  and cross-build pass. Owner verified live native pose and haptics work.
- [ ] **Resumed October 9; new build awaiting test:** improve bow aim alignment and perceived arrow travel speed. Earlier investigation:
  idle bow-marker frame submission errors (`xrEndFrame -39`, present in prior
  guide build too). Distinguish attack wind-up, travel velocity, ground-only aim
  and model-vs-hand-line alignment before changing combat values. Owner clarified
  sideways shots and requested a visible arrow exactly along both hand anchors.
  00:34 build implements native 3D nock/arrow posing through the full attachment
  transform, 400 ms release-direction latch, and omits native idle nock marker.
  First anchor build failed visually: generic attachment follows inventory slot
  (right), not the actual left bow. Corrected to explicit L_weapon_attach;
  built/installed after game closed and owner verified hand following. Idle-marker frame
  errors stopped in the 00:34 build's live test. Latest corrected build has 734
  live draw samples: string-to-hand p90 3.26 mm, arrow-to-hand-line p90 0.452 deg;
  no frame-submit errors. Owner confirmed both-hand following. Remaining issue
  clarified: flying arrow model angled across its roughly straight flight path,
  not the bow twisting. Add native projectile orientation correction and 2x
  travel-speed test. Flight test feedback: somewhat aligned, not much faster.
  Hooks installed but no creation/velocity counters appeared in the latest
  session; do not claim verified 2x speed. Leave flight tuning parked for melee.
- [x] Implement initial generic main-hand melee recognition and aim for every character
  class: weapon-category sensitivity, translation/rotation strokes, thrusts,
  net-excursion jitter rejection, ready haptic, 250 ms swing-bearing latch, and
  native-reach guard for staff/javelin swings. Host tests cover all four gesture
  profiles, three stroke directions and 36/72/120 Hz input. No class/rig special
  cases. This grip-armed prototype was superseded by the free-swing contacts above.
- [ ] Verify generic melee in headset across characters and weapon categories:
  correct target, damage, reach, timing, held model and menu/swap recovery.
  Sword contact and native accepted-hit feedback are verified; independent
  off-hand strikes/shield collisions remain open.
- [ ] Replace prototype slot text/flat bottle/draw-line guides with actual skill
  metadata and 3D item presentation. Native names/belt counts/wrist vitals are
  implemented; original 3D potion models are now installed with GPU validation.
  Headset potion verification remains; this is not general inventory item meshing.
- [ ] Add pitch-sensitive spell placement, parries, crossbow loading and throwing.
  Refine held-bow presentation across rigs and melee collider dimensions.
  Damage/resources stay native, with the explicitly requested deterministic hit
  exception above. These remaining interactions need individual validation.

- [ ] Make world interaction obvious and precise: right-hand aim ray, highlight
  and readable name/action for the actual selected target, then **Right A** to
  interact/loot. Current Right A uses the game's native interaction target;
  that is not yet a verified hand-ray hit-selection system. Support items, doors,
  NPCs and corpses; prevent selecting through walls or distant scenery, add target
  stability and clear range feedback, and suppress targeting while menus are open.
- [ ] Route Right A exclusively through the semantic interaction action: its
  current raw XInput A mirror can bypass the no-target guard and attack on a miss.
  Keep trigger attack separate; test that pointing at empty ground and pressing
  loot cannot attack, walk or choose a different nearby target.
- [ ] Test recovery of Nekro's own equipment-bearing corpse in camp. User sees
  an existing corpse and an unequipped live character. Keep native corpse recovery
  and inventory-capacity rules, with visible success/failure feedback. Do not remove
  corpses or modify save/inventory data to work around this. The cause of that
  corpse was not established; the body-rendering filter is separate from looting.
- [ ] Document and verify current controller attack, interaction, weapon swap,
  selected skill and aim bindings. Current trigger path invokes game actions;
  tracked weapon presentation does not establish gesture combat support.
- [ ] Add a controller radial selector for weapons and assigned spells/skills,
  with clear equipped/selected feedback and comfortable casting controls.
- [x] Superseded initial optional gesture-to-button approach: swing triggered the game's
  native attack, with thresholds/debounce and trigger fallback. Respect weapon
  reach, game attack speed, hit rules and cooldowns; no attacks from menu gestures.
- [ ] Add optional two-hand bow mechanics: nock/draw/release gesture and controller
  aim trigger the native shot. Preserve ammunition, native damage and attack timing.
- [ ] Implement target resolution for aimed spells, self buffs and summons
  behind the same right-trigger casting control; preserve mana/cast-rate rules.
  Spell gestures are explicitly out of scope for this phase.

## 4. Performance and eye-tracked foveation

- [ ] Parked by owner: SteamVR GPU failure after Frame standby/reconnect at
  22:45 on October 8 (NVIDIA Xid 32 in vrserver, followed by watchdog/compositor
  failures). Evidence in HANDOFF.md. Owner requested visuals take priority;
  no crash-specific code/driver changes applied.
- [ ] Validate DLSS stereo reconstruction before recommending it. Owner enabled
  Balanced at 21:48 and reported distorted visuals plus a temporary pause. Found
  and fixed a MinGW/MSVC NGX overload-vtable mismatch: resource/scalar reads and
  vector replacement were calling incorrect slots. All 17 adapter calls now
  match compiler-generated MSVC SDK calls; built and installed at 21:55.
  Live test confirmed valid input reads, then exposed a second blocker: the
  diagnostic statistics size limit skipped correction at 4K Balanced. Fixed
  that guard to apply only to statistics; installed and relaunched at 22:04.
  At 22:05:39 logs confirmed correction submission for both eyes at 2227x1253.
  Owner confirmed **“looks good”** with Balanced after the second fix. Compare
  performance with matched scenes before claiming a gain. Preserve
  installer default DLSS off. Correction compilation was separated from diagnostic
  reduction at 22:30: isolated compilation dropped from 22.9 s to 33 ms with
  identical correction instructions and no group-shared allocation. Installed;
  live 22:31:57 correction compilation/pipeline creation took 67 ms. Measure
  steady-state results separately from ongoing walking hitches. Following the
  centered-projection fix, the owner turned DLSS off and reported **much clearer**.
  Preserve DLSS off locally as well as in installer defaults; sharpness/temporal
  reconstruction still needs work before recommending DLSS.
- [ ] Fix distant square lighting/highlight patches differing between eyes on
  waypoints and clothing. Not visible close up. Still visible with DLSS off,
  added sky/fog off, and bloom upsampling suppressed; all diagnostics restored.
  Native light-bin shader ignores off-axis projection terms; a live centered
  projection fix is installed at 22:42 (`render.symmetric_projection`, now default 1).
  Owner reports **patches seem gone, image softer** after the waypoint test.
  Subsequently disabling DLSS made the image **much clearer**.
  Keep local setting 1; validate clothing/other scenes and improve sharpness
  within the performance budget before closing. See HANDOFF.md. Preserve
  view-dependent highlights and right-eye particle timing while investigating.
- [ ] Investigate residual bursts after repeated background-load interference
  ended around 21:46. Owner said movement felt much better with a slight burst
  remaining. D2R CPU-affinity trials 40–43 were confounded by scene/load changes;
  original affinities restored. Sequence 44 actually captured DLSS startup and
  must not be presented as an affinity result or matched performance comparison.
- [x] Identify the current bursty walking regression's CPU contention. A parallel
  Iron Signal test started at 21:23:45 and used about 18 logical CPUs. The owner
  authorized a 45-second pause with automatic resume and asked its agent to stop
  performance work. After it stopped, the owner confirmed **“Smoother, bursts
  gone.”** Walking sequence 31 averaged 29.4 ms, p95 37.1, maximum 41.8 over 360
  pairs; this is not a matched-scene percentage benchmark. Original sync remains
  enabled (`defer_gpu_wait=0`). No process was left paused. Inspect competing
  workloads before future comparisons; do not pause other projects unasked.
- [x] Add bounded stage/callback/GPU profiling and capture stationary and moving
  gameplay. Sequences 1/3 show mean cycles of 27.0/48.5 ms, with 16.3/25.0 ms of
  drawing-thread CPU per pair. Effects cost about 1.4 ms and copies 0.05 ms per
  pair; GPU scene spans include CPU starvation. These are different workloads,
  not a before/after benchmark. Worker threads remain outside callback probes.
- [ ] Validate deferred GPU completion waits during gameplay, menus and exit.
  Installed at 21:27; copy allocator reuse and shutdown retain completion checks.
  Repeated 1/0/1/0 comparison (sequences 20–23) has mean cycles 35.8/39.7 ms and
  XR end 3.85/5.72 ms. Head directions differed, so do not claim a controlled
  percentage gain. Some removed fence wait moves into runtime submission.
  Owner reported bursty walking with this enabled; restoring 0 did not change
  movement. Leave owner configuration and source default at 0. An unrelated
  Iron Signal test was subsequently found using about 18 logical CPUs; exclude
  competing workloads before drawing performance conclusions.
  Use `profile_gpu=0` for comparisons, as GPU readback deliberately forces a wait.
- [ ] Attribute engine scene preparation and worker-thread work inside the two
  eye passes. Sampled main-thread mod callbacks explain only a small fraction;
  that does not exclude unmeasured mod work. Keep the long pre-render hitch
  investigation separate from steady-state drawing cost.

Next investigation order after the camera repair (21:11 measurements):

1. Attribute both eye passes' CPU cost before changing visual settings. Recent
   windows use 21–22 ms of render-thread CPU per pair, with 28–30 ms total eye
   pass wall time. The RTX 5090 was at 30% utilization in one spot sample (not
   a benchmark). Time mod callbacks/scans and engine scene preparation separately.
2. Measure GPU work and OpenXR synchronization, including the now-active
   ReShade effects, eye acquire/copy/fence/release and compositor submission.
   Recent hitch records include 10–15 ms around XR end, but these are not
   representative average costs. Do not remove resource completion guarantees.
3. Attribute long pre-render stalls separately: at 21:11:30 a 633 ms cycle
   spent 581 ms before stereo drawing, near native UI/input events, with zero
   tracked pipeline creations. Loading, UI and mod work need separate timing;
   neither shader compilation nor asset loading is established by this sample.
4. Once measured, investigate sharing eye-independent scene preparation or
   shadow work across the two eye draws. This is substantial engine work;
   never skip animation/UI updates blindly or reuse eye-dependent culling.
5. Compare shadows/VFX/atmospherics, eye resolution and foveation at a fixed
   scene only if GPU timing identifies them as limiting. Preserve a visual
   baseline and measure both frame-time tails and average pair time.


- [ ] Investigate movement micro-freezes/skips, now the next priority after the
  menu reopening fix. Separate game/asset loading, pipeline compilation, mod
  memory scans, rendering and OpenXR/copy waits. New logs break down >80 ms hitches,
  time native pipeline creation and exclude menu gaps from gameplay statistics.
- [ ] Compare walking after disabling Proton exception/unwind trace logging.
  The 18:45 session recorded pauses up to 728 ms, largely before stereo rendering;
  none of the captured hitches recorded pipeline creation. Its Proton log grew
  to 31 MB in about three minutes, peaking above 38,000 trace lines per second.
  The launcher now retains warnings/errors and game-side timing without those
  trace channels. This is a suspected source of overhead, awaiting comparison.
  Global Wine warnings also produced routine file/debug-string messages; the
  next profile keeps Wine errors and native VKD3D warnings only. The first quieter
  walking test still recorded 30 hitches, up to 1,313 ms, with about 95% of recorded
  hitch time before stereo drawing and no tracked pipeline creation in those
  hitch samples. Continue investigating game loading and CPU-side stalls.
- [x] Verify the buffered logger removes the measured main-thread filesystem
  waits while walking/turning. Live Linux samples showed `fsync()` on the mod's
  `d2r-vr-vrcam.log` during journal/page waits spanning hundreds of milliseconds;
  sampled long pauses had no physical disk reads. Runtime logging now uses a
  bounded queue and worker batches in `d2r-vr-frame.log`, with no forced flushes.
  Concurrent producers preserve their records/order and shutdown drains them
  in the Proton test. User confirmed **hitches improved, labels still doubled**.
  A subsequent 599-sample trace saw no journal/page waits or blocked log paths.
  Typical 10-second timing windows had longest pairs of 38–60 ms; one 475 ms
  gameplay gap remained. Continue investigating residual hitches separately.
- [x] Reduce foveation CPU callback overhead. Cache command-list and repeated
  pipeline state, defer VRS changes until a draw, batch draw counters and handle
  indirect/mesh draws conservatively. User's 19:49 test: **“Smoother”**. CPU time
  now measures correctly under Proton; observed render passes are 94–97% CPU
  busy while GPU utilization has a median around 43%. No precise gain claimed.
- [x] Resolve ReShade's persistent shader compilation and verify its CPU cost.
  A worker used one core throughout the prior session. Trial the current SDK
  shader compiler privately under the mod, retaining stock launch and prefix
  libraries. The older Winetricks compiler does not support ReShade's SM 5.1.
  Current SDK compiler completed the full effect in 10.56 seconds, sky now renders,
  and the sustained compiler worker is gone in the subsequent thread sample.
  Previously missing effects now run, so this is not an equal-quality FPS test.
- [ ] Attribute the remaining half-second gameplay stalls. One 504.7 ms cycle
  had a 474.1 ms pre-render gap, main-thread CPU activity and ~27 MB cached reads,
  without physical reads. Asset processing is a candidate, not established cause.
- [ ] Compare the same walking route twice and after another launch without
  clearing caches. Shader compilation is a candidate, not an established cause.
  Latest Proton log confirms an active shader cache with 1,820 existing entries
  and 8 merged additions. Pipeline creation callbacks do not capture every form
  of lazy compilation or driver work.
- [ ] Measure retaining native height fog (`game_height_fog=1`) as the new PCVR
  preset. This disables full-memory fog scans scheduled 1/4/12 seconds after
  area changes. Atmosphere may differ; compare hitches and decide whether a
  bounded replacement is worthwhile. Keep scene resolution/other quality fixed.
- [ ] Measure CPU, GPU scene draws, effects, copies and compositor timing in the
  same camp/dungeon scenes. Current pipeline renders two scenes per game update.
  Observed pair rates after the earlier 80% preset were about 31–35 Hz; this is
  not a controlled benchmark or the headset display refresh rate.
- [ ] Validate the optional 80% scene-resolution/medium AO, atmospherics and
  shadows preset and per-eye copy command lists with a single pair completion
  wait. Preserve textures; compare frame time and visual quality at fixed settings.
- [x] Query **actual PC-streaming** OpenXR capabilities through WineOpenXR and
  D3D12 VRS through vkd3d-proton. Launch at 18:20 reports eye-gaze extension
  advertised, D3D12 VRS Tier 2, 16-pixel tiles and additional shading rates.
  `XR_FB_foveation` and `XR_META_foveation_eye_tracked` are not advertised here.
  Capability logging does not enable foveation or prove valid gaze samples.
- [ ] If gaze is exposed, bind `XR_EXT_eye_gaze_interaction`, query eye-gaze system
  support, and verify tracked samples while worn. Handle calibration, permissions,
  stale/invalid gaze and head movement. Avoid storing raw gaze in routine logs.
  October 8 23:21 pilot: system support and tracked timestamped samples confirmed
  in the actual SteamVR/Proton session after the owner approved enabling
  `driver_vrlink/shareEyeTrackingData`. The setting defaults off; never change it
  silently on a friend's machine. Binding, validity/freshness/recovery fallback
  are implemented; calibration/loss/reconnect coverage still needs testing.
- [ ] Prototype fixed foveation with a D3D12 Tier 2 shading-rate image before
  making its center follow gaze. Hook eligible world rendering passes, reset state
  for UI and incompatible passes, and verify both eye projections. This requires
  renderer integration; adding blur after rendering will not save scene shading.
  The first pilot is implemented and running: separate immutable per-eye masks,
  optical centers from asymmetric FOV, 1x1 center / 2x2 opaque-world periphery,
  and full quality for UI/transparency/post effects. Live reduced-shading draw
  counts confirm usage; user reports similar perceived performance. Verify
  artifacts/menus/dungeons and benchmark enabled/disabled at identical settings.
- [ ] Add gaze-driven full-quality center with a conservative margin for latency,
  coarser peripheral shading, configurable strength and safe tracking-loss fallback.
  Protect UI readability; test particles, transparency, post effects and rapid gaze.
  Prototype installed: 9x9 immutable mask bank, gaze radius .35 + .177 grid margin,
  shared between eyes and cached per viewport/radius, one-time 16 ms setup in the
  owner's 3840x2160 scene. Valid gaze and reduced-shading draws are confirmed;
  owner confirmed clear/stable eye/head movement and labels. `[render]
  eye_tracked_foveation=1` opts in before launch;
  defaults off for distribution. Invalid gaze uses full-quality shading.
- [ ] Benchmark foveation on/off with identical settings and document GPU and
  end-to-end frame-time changes. CPU work, geometry and full-size eye copies may
  limit gains. Do not infer performance from theoretical shaded pixel counts.
  First stationary gaze/full/gaze comparison (360 pairs each) averaged
  22.566 / 23.240 / 22.619 ms per pair, ~2.8% lower with gaze VRS. GPU scene spans
  were similar and include CPU queue starvation; this is a small preliminary
  result. Compare fixed versus gaze and test dungeons/heavier scenes next.
- [ ] Check Frame's foveated streaming settings separately: streaming quality/
  bandwidth optimization and game rendering optimization are distinct stages.
- [ ] Diagnose the ReShade sky/fog effect that has stayed at one remaining compile;
  restore sky/fog quality and measure its cost before setting the final preset.

Technical references checked for this investigation:

- [Valve custom-engine eye tracking and foveation extensions](https://partner.steamgames.com/doc/steamhardware/steamframe/engines/custom).
  These documents include standalone guidance; verify the streamed PC runtime.
- [Microsoft D3D12 VRS tiers and shading-rate images](https://learn.microsoft.com/en-us/windows/win32/direct3d12/vrs).
- [Proton WineOpenXR generator](https://github.com/ValveSoftware/Proton/blob/proton_11.0/wineopenxr/make_openxr).
  Source compatibility is not a substitute for querying the installed runtime.

## Established baseline

- [x] Native OpenXR D3D12 stereo transport works through Proton/SteamVR to Frame;
  user confirmed offline first-person gameplay and captures show Nekro in camp.
- [x] Normal Steam Play launches stock D2R; VR is an explicit separate launch.
- [x] Preserve Steam authentication/runtime context and the original executable.
  Legitimate unmodded online license refresh is separate from offline mod testing.
- [x] Back up replaced files and saves; retain a documented restore path.
- [x] Preserve upstream MIT attribution and keep the original D2R-3D checkout separate.

## 6. Headset recordings

- [x] Install Framecorder 0.1.1 on Frame and verify a short recording. Dashboard
  service works; 4-second 1080p/90 fps HEVC headset-view recording was inspected.
  Game audio is enabled; microphone and instant replay are off for performance
  testing. Record/Stop are in the Framecorder SteamVR dashboard tab.
- [ ] Optional direct-panel capture: owner enters Frame administrator password
  locally for `sudo -v && ~/.local/bin/framecorder-setup --unlock`. Current
  headset-view recording works without this. Keep passwords out of chat/logs.

## Desktop presentation

- [ ] Investigate an optional disabled desktop mirror and measure its actual
  overhead. Current monitor flicker comes from alternating left/right eye
  backbuffers; there is no separate monitor scene render. ReShade addon Present
  callbacks cannot cancel the OS presentation. Preserve game frame advancement,
  headset capture, menus and controller focus; minimizing is not a substitute.
  A stable single-eye mirror is an alternative but adds a GPU copy in the
  current design. No display-path changes have been installed.

### Basic-arrow flight test, 2026-10-09

- [x] Locate static flying-arrow model and native half-second release-point blend.
- [x] Build local-player-only basic-arrow 2x velocity hooks on client and server;
  no game table/save/asset edits. Native lifetime means possible longer range.
- [x] Build selective native release-point component removal before handoff.
- [ ] Verify actual client/server velocity events and direct handoff in game.
- [ ] Headset check: arrow axis along flight, faster flight, normal enemy hits.
- [ ] Extend to elemental/skill arrows after basic-arrow test; current scope is id 0.
