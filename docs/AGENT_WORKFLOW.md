# Agent workflow and session handoff

Last checked: 2026-10-15. This records working procedures, not a claim that the
current checkout, installed DLL and latest release are identical. Verify those
separately. Read [CONTRIBUTING.md](../CONTRIBUTING.md) first.

## Start here

1. Read `AGENTS.md`, this guide and, if it exists,
   `build-frame/AGENT_LOCAL_STATE.md` in the owner's main checkout. The latter is
   ignored, machine-specific state; it can be absent in a fresh clone/worktree.
2. Run `git status --short --branch`, `git worktree list` and inspect recent
   commits. Preserve unfinished work and existing topic branches.
3. Identify the task's production branch, developer integration branch, tested
   commit and installed artifact from deployment receipts. Do not infer that
   a successful build was installed, or that a merged PR requires a reinstall.
4. Use a focused topic branch and a PR against `main`. Never push directly to
   `main`, disable protections, or merge untested changes because a different
   branch passed checks. Recheck the exact PR head before approving/merging.
5. Keep raw logs, extracted tables, saves, dumps, downloaded dependencies and
   build output under ignored local directories. Do not attach them to releases.

## Owner preferences

- Build and install successful requested gameplay changes; installation is part
  of the usual task. Preserve settings and calibration.
- Do not automatically launch the game or VR after each edit. The owner batches
  features, puts on the headset, tests, and reports bugs. Launch when asked.
- All game tests use offline local characters. Developer presets use disposable
  characters in the isolated `pcvr-dev` environment.
- Finish authorized reversible work without repeatedly requesting approval.
  If the game is open, finish the build/checks, then ask the owner to close it
  before replacing a loaded DLL. Do not kill their game to install an update.
- Keep the developer menu available in local test builds. Do not mix developer
  helpers into a focused production bug-fix PR.
- Treat an investigation explicitly put on the shelf as paused work. Record
  the evidence and next dependency instead of restarting it in a new session.

## Build and test

Run from the intended checkout root in PowerShell:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File steamvr/build_windows.ps1 -Test
```

The active project is `steamvr/CMakeLists.txt`, not the historical root project.
Normal output is `build-frame/windows-native/d2rl-vrcam.dll`. Developer-capable
branches also accept:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File steamvr/build_windows.ps1 -Developer -Test
```

Developer output is `build-frame/windows-developer/d2rl-vrcam.dll`. Neither
command installs or launches anything. The developer helpers historically live
on `feat/development-scenarios`; first inspect the current branch and options.
The last checked suites had 67 production and 69 developer tests. Counts change;
report the actual result, and distinguish automated checks from headset testing.

Use existing pinned tools under `deps/` before downloading them again. When
missing, `steamvr/setup_windows_tools.ps1` supplies the pinned toolchain. Use
`deps/python/python.exe` for the repository's Windows Python workflows.

For independent PR testing, create a worktree below `build-frame/worktrees/`.
Its `deps` can be a junction to the original checkout's `deps`. Create a junction
only if the destination is absent; do not delete an existing dependency tree.
Keep each worktree's build output separate. Preserve the PR head unchanged and
use a separate test branch to combine it with developer tools. Record any merge
conflict resolutions and test the combined result too.

For installer/launcher edits, use the appropriate Python tests listed in
CONTRIBUTING. A new native count does not substitute for installer fixture tests.
For UI changes, native previews are useful:

```powershell
.\build-frame\windows-developer\vr-settings-preview.exe build-frame/combat.bmp 4
.\build-frame\windows-developer\interaction-preview.exe build-frame/dev.png --developer-check
```

The second command exists on developer-capable branches. Inspect the image for
clipping and overlap; a successful paint call alone is not visual validation.

## Install an already configured test game

Locate the real game directory, then verify the plugin path and current process
state. The historical plugin is `d2rloader/plugins/d2rl-vrcam.dll`; identity and
filename migration work may change this. Inspect the current installer/manifest
rather than assuming names remain permanent.

For a DLL-only update to the owner's configured installation:

1. Call `install.require_game_closed()` and validate the game with
   `game_install.validate_game()`. Also inspect both `D2R` and `D2RLoader`
   processes. A loader can remain alive after its game window disappears.
2. Hash the selected staged DLL. Capture hashes of saves, configuration,
   calibration, developer mod metadata and relevant shaders.
3. Back up the installed DLL under an ignored timestamped directory. Record
   source commit, any uncommitted patch, source and destination paths, and both
   hashes. Verify the backup before replacing anything.
4. Check again that the game is closed, replace only the intended artifact,
   and verify the installed hash equals the staged hash. Restore the backup
   if copying or verification fails.
5. Check protected-file hashes stayed unchanged and write a deployment receipt
   under `build-frame/`. Report installed versus merely staged accurately.

Existing `build-frame/install-*.py` scripts and `latest-*-install.json` receipts
can document previous installs, but scripts often pin an old source hash.
Inspect and adapt them; never blindly rerun a historical deployment script.
Shader fixes require copying the tested shader as well as any changed DLL;
avoid replacing shaders while the game is running.

For full setup use the supported installer flow. `steamvr/install.py` exposes
`--plugin`, `--game`, `--openxr-loader`, `--preserve-settings` and backup options;
inspect its current help and edition requirements. Avoid reinstalling unrelated
dependencies or resetting the whole profile just to update one DLL.

## Launch only when requested

Use the existing installed Steam shortcut when appropriate, or the repository's
`steamvr/launch_windows.py`. Do not run `D2RLoader.exe` directly as a substitute
for the wrapper's Steam VR path. That bypasses the verified Explorer handoff.

The Steam VR wrapper replaces `D2R.exe` with the loader, sets the real Steam app
ID, removes the inherited overlay hint, and asks Windows Explorer to start a
clean helper chain. This avoids the earlier overlay injection problem. It waits
for that one game's exit and logs the result; a long-running wrapper is normal.
Do not launch a second copy or kill the wrapper just because it is waiting.

PowerShell examples, with machine-specific paths substituted:

```powershell
$gameDir = 'YOUR GAME DIRECTORY'
$steamVrManifest = 'YOUR SteamVR DIRECTORY\steamxr_win64.json'
.\deps\python\python.exe steamvr/launch_windows.py --frame-vr "--frame-openxr-runtime=$steamVrManifest" "$gameDir\D2R.exe"
```

For Quest through Virtual Desktop, `--frame-vdxr` selects its installed runtime:

```powershell
.\deps\python\python.exe steamvr/launch_windows.py --frame-vr --frame-vdxr "$gameDir\D2R.exe"
```

The wrapper uses per-launch `XR_RUNTIME_JSON`; do not change the system-wide
OpenXR runtime. VDXR is supplied by Virtual Desktop Streamer. Steam Frame uses
SteamVR; Quest 3 has been tested through both SteamVR and VDXR. Do not assume the
same FOV, composition placement or haptic response across headsets. Keep Steam
and the selected streaming/runtime software ready. With VDXR, use Virtual Desktop
and keep SteamVR closed as described in the installer guidance.

The Battle.net wrapper path is separate: `--frame-vr`,
`--battlenet-game=GAME_DIRECTORY`, `--experimental-battlenet`, and a runtime
selection. It validates the supported edition/build and clears inherited Steam
IDs. Its current parser rejects arbitrary extra game arguments, including the
Steam example's `-mod` arguments. Do not route Battle.net through Steam-only
handoff assumptions or promise an unimplemented developer launch route.

If using `Start-Process` for a background launcher, use `-WindowStyle Hidden`.
Pass arguments as data with correct quoting. Never interpolate untrusted paths
or PR text into executable shell syntax.

## Isolated developer environment

On a developer-capable branch, read `steamvr/DEVELOPMENT_SCENARIOS.md` and run:

```powershell
.\deps\python\python.exe steamvr/prepare_dev_scenarios.py --game "$gameDir"
```

This creates only `mods/pcvr-dev/pcvr-dev.mpq/modinfo.json`, with exact metadata
`{"name":"pcvr-dev","savepath":"pcvr-dev/"}` followed by a newline. It never
creates or edits characters. Conflicting metadata is intentionally refused.
For Steam, append `-mod pcvr-dev` after `D2R.exe` in the wrapper command. Use a
new offline expansion character in that separate save list.

The dev page is a **DEV tab on either hand's wheel**, beside VR Settings. It is
compiled out of release builds and gated by the exact isolated mod metadata.
The existing owner test character is not permission to alter normal saves.

- Prepare the current class grants test stats/skills; it preserves bindings.
- Gear buttons add items to inventory. The player equips them. Full inventories
  can refuse a kit; existing items must not be deleted to make space.
- Travel uses native console commands. "Commands sent" means dispatch accepted,
  not proven arrival or quest completion. Verify destination in game.
- Tyrael and Duriel use distinct destinations in level 73. The Tyrael shortcut
  goes to his platform; Duriel uses the native entrance. Neither kills the boss
  nor completes the quest.
- Crossbow + bolts supplies Leadcrow and 250 bolts for any class; Blood Moor
  supplies an outdoor test destination. Compare existing versus explicit Attack
  on Right Trigger. Release Right Grip: Right Grip + Right Trigger shows loot.
- Do not hold developer status locks across native dispatch or item operations.
  Those calls can pump rendering/UI; an earlier travel hang came from that lock.

## Logs and interpreting evidence

Look in the **active game's** directory, independent of headset brand:

| File | Purpose |
| --- | --- |
| `d2rloader/logs/d2r-vr-frame.log` | Main buffered PCVR runtime/gameplay/render diagnostics |
| `d2rloader/logs/d2r-vr-vrcam.log` | Plugin/loader startup diagnostics |
| `d2rloader/logs/d2rloader.log` | Loader failures and native assertions |
| `d2rloader/logs/pcvr-launch.log` | Explorer handoff, child PID, exit code |
| `ReShade.log` in the game root | Shader loading/compile/runtime information |

Also collect the loader's `d2r-lastresort-crash-report (...)` when produced;
locate it in the user's actual installation/log folders instead of guessing a
headset-specific location. ZIP large text logs. Match the report's actual crash
timestamp/PID to its session; filenames can contain process-start times.

Logs append across sessions. Do not diagnose a recent crash from an older
session's tail. Legacy upstream plugin identity/version strings can be stale;
use a release package version, verified build hash or deployment receipt.
Separate verified assertions, reconstructed call paths, hypotheses and owner
headset observations. A null allocator return does not by itself prove a leak,
physical RAM exhaustion, VRAM exhaustion, or a headset defect.

Known diagnostic lessons:

- Loot label size comes from `[hud] labels_size` in `d2r_vr.ini` for native
  first-person VR; VR Settings > HUD > Loot label size edits the same key.
  Default 45%, range 20-150%, five-point steps; explicit saved sizes are kept.
  The 150% enlargement bypassed this saved value. Changing that override to
  100% did not restore the earlier appearance; remove the override and respect
  the profile. The darker backing and hold-to-show shortcut are independent.
- Render defaults checked in v0.3.15: terrain rings **2**, object radius **300**.
  The reported Act III sessions used 2 / 510: terrain was default, objects were
  above default. 1 / 150 plus a full restart is an unverified mitigation/test,
  not a confirmed crash fix or a promised performance gain.
- The two Act III reports matched a renderer-allocation failure. Room unloading
  remained a hypothesis. Upstream pool-growth integration referenced missing
  implementation files; wait for the requested source/response before claiming
  it has been ported. Pool-related INI keys alone do not mean the code reads them.
- Tyrael's dark pillar silhouettes disappeared with added indoor fog disabled.
  The indoor toggle is hidden, defaults off, and the owner's test INI has
  `[fog] caves=0`. Explicit saved `caves=1` is preserved by upgrades.
- The reported Duriel black room edge was not reproduced in the owner test.
  Do not claim the fog change has proven that separate issue fixed.
- Pose timestamps matter: nocked arrows and melee blade geometry must use the
  input that produced the rendered pose, not the newest controller sample.
- Crossbow basic attacks create native missile 31, not arrow 0. The held item's
  `bolt_helper`, item-local transform and hero weapon attachment define its
  barrel; a manually rotated controller ray can disagree with the actual mesh.
  Check both server/client launch coordinates and the release mesh's current/
  previous transforms when diagnosing an offset or ghost trail. Basic bolts
  use the shared physical-arrow flight path. Learn item rigs independently of
  hero pose availability and refresh them from the completed hero pose; static
  item skeletons may not receive another animation callback. Recognize helper
  bones plus exact owner entity, not an assumed full archive-path model name.
  Check `Frame crossbow` capture/adoption diagnostics before assuming the new
  path ran. The first #106 candidate passed fixtures but never captured a held
  pose in the owner's session; its headset test failed.
  The next candidate captured/adopted shots but still failed placement testing.
  Item-local times wrist is an approximation: the native transform evaluator
  also applies attachment overrides. Capture the rendered item transform after
  evaluation, then retain its relationship to the matching posed wrist for the
  next support-hand pose. The bolt helper's bind origin is an unloaded tail;
  the stock front is located using the limb roots projected onto the bolt axis.
  The owner verified the rendered-muzzle correction. Native trigger attacks
  still lock walking during the attack animation; physical basic crossbow fire
  uses the bow's direct game-thread attack path and consumes the pad attack.
  Check `Frame crossbow free fire` for successful direct shots/cadence. The
  existing class/weapon/IAS cadence table supplies the cycle (skill speed/slow
  modifiers remain unmodeled); unknown or stale data falls back to one second.
- Melee settings are motion speed in m/s, not acceleration. Classic/Unleashed
  are game-rule choices; they do not remove the physical motion threshold.
- The shield follows the final posed left wrist. Arm IK used to clamp that
  wrist to the character's arm length, even with valid controller tracking.
  Native shield reach now extends only that arm when needed (bounded to 2x),
  preserving hand/equipment size and using the same shield pose for contact.
  `Frame shield reach` logs extension and wrist error in model units. The
  geometry fixtures do not replace headset checks of reach, arm skin, wrist
  rotation and Smite. Leave stored body calibration intact during installation.

## End-of-session record

Update the ignored local handoff with: current branch/worktrees, open PR heads,
installed source/hash/receipt, tests actually run, pending headset tests, shelved
investigations and the next useful action. Put reusable, verified workflow changes
in this guide. Keep the local handoff concise enough to read at session start;
link to evidence files rather than copying raw private logs into Markdown.
