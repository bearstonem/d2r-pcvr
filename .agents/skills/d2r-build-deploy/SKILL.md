---
name: d2r-build-deploy
description: >-
  Build, run targeted or full automated tests, generate UI previews, and safely deploy
  d2rl-pcvr.dll on Windows. Use when compiling C++ or Python changes, running CTest or
  unittest suites, or installing staged builds into a local D2R installation.
---

# D2R PCVR Build, Test & Safe Deployment Workflow

Read [`docs/AGENT_WORKFLOW.md`](../../../docs/AGENT_WORKFLOW.md) and [`CONTRIBUTING.md`](../../../CONTRIBUTING.md) for full policy details.

---

## 1. Tracked Project Tooling vs. Optional Local State

Keep tracked repository tools clearly separated from git-ignored machine-local state under `build-frame/`:

- **Tracked repository build, test, and install scripts:**
  - [`steamvr/CMakeLists.txt`](../../../steamvr/CMakeLists.txt): Active CMake project (the root `CMakeLists.txt` is the historical upstream project; do not use it for D2R PCVR changes).
  - [`steamvr/setup_windows_tools.ps1`](../../../steamvr/setup_windows_tools.ps1): Provisions the pinned local toolchain into `deps/` (`cmake`, `ctest`, `ninja`, `llvm-mingw`, embedded `python`). Always inspect `deps/` for the provisioned tool directories before downloading anything.
  - [`steamvr/build_windows.ps1`](../../../steamvr/build_windows.ps1): Configures, builds, and optionally tests the native (`build-frame/windows-native/d2rl-pcvr.dll`) or developer (`build-frame/windows-developer/d2rl-pcvr.dll`) plugin.
  - [`steamvr/install.py`](../../../steamvr/install.py), [`steamvr/game_install.py`](../../../steamvr/game_install.py), and [`steamvr/prepare_dev_scenarios.py`](../../../steamvr/prepare_dev_scenarios.py): Tracked installation, validation, and developer-mod staging tools.
- **Optional git-ignored local helpers (`build-frame/`):**
  - `build-frame/AGENT_LOCAL_STATE.md`: Optional local handoff file recording machine-specific game paths and the latest verified install state. Check if it exists before rediscovering local paths, but never assume it is present in a clean clone.
  - `build-frame/install_dll.py` and `build-frame/latest-native-install.json`: Optional machine-local deployment helper and receipt present only in already-configured local checkouts.

---

## 2. Iterative Development: Narrowest Relevant Validation First

When iterating on a focused C++ module or unit test after `build-frame/windows-native` has been configured, avoid rebuilding every target and running the entire CTest suite on every edit:

1. **Build only the affected test or library target:**
   ```powershell
   .\deps\cmake-4.4.4-windows-x86_64\bin\cmake.exe --build build-frame/windows-native --parallel 4 --target <target-name>
   ```
   *(Check [`steamvr/CMakeLists.txt`](../../../steamvr/CMakeLists.txt) for exact `add_executable` / `add_library` target names such as `d2rl-pcvr`, `companion-ai-test`, `vr-settings-test`, `melee-contact-test`, or `turn-warp-test`; if your `deps/` CMake directory version differs, use the `cmake.exe` provisioned under `deps/`.)*

2. **Run only the matching CTest check(s):**
   ```powershell
   .\deps\cmake-4.4.4-windows-x86_64\bin\ctest.exe --test-dir build-frame/windows-native -R "<test-regex>" --output-on-failure
   ```
   *(Example: `-R "^companion-ai-test$"` or `-R "^vr-settings"`)*

3. **Run targeted Python fixture tests (when editing installer, launcher, or packaging scripts):**
   ```powershell
   .\deps\python\python.exe -m unittest discover -s steamvr -p install_test.py
   .\deps\python\python.exe -m unittest discover -s steamvr -p launch_windows_test.py
   .\deps\python\python.exe -m unittest discover -s steamvr -p battlenet_test.py
   .\deps\python\python.exe -m unittest discover -s steamvr -p setup_windows_test.py
   .\deps\python\python.exe -m unittest discover -s steamvr -p uninstall_windows_test.py
   .\deps\python\python.exe -m unittest discover -s steamvr -p setup_native_test.py
   .\deps\python\python.exe -m unittest discover -s steamvr -p mod_identity_test.py
   .\deps\python\python.exe -m unittest discover -s steamvr -p prepare_dev_scenarios_test.py
   .\deps\python\python.exe -m unittest discover -s installer -p stage_native_installer_test.py
   ```
   *(Note: `steamvr/session_test.py` requires Linux `/proc` and is not a Windows check.)*

4. **Render off-screen UI previews (when modifying VR Settings or wheel UI layout):**
   ```powershell
   .\build-frame\windows-native\vr-settings-preview.exe --pages build-frame/vr-settings-pages
   .\build-frame\windows-native\interaction-preview.exe build-frame/dev.png --developer-check
   ```
   Inspect the generated images for text clipping or overlap; a passing paint return code alone does not prove visual layout quality.

---

## 3. When a Full Configure, Build & Test Run Is Required

Run the full script from the repository root before concluding a task, after resolving branch merges, when `build-frame/windows-native` is absent/stale, or after changing [`steamvr/CMakeLists.txt`](../../../steamvr/CMakeLists.txt) or [`RELEASE_NOTES.md`](../../../RELEASE_NOTES.md):

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File steamvr/build_windows.ps1 -Test
```

For isolated developer-mode builds:
```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File steamvr/build_windows.ps1 -Developer -Test
```

---

## 4. Safe Local DLL Deployment

Building stages `d2rl-pcvr.dll` in `build-frame/`; it does **not** install it into the game.

### Strict Prohibition on Improvised Copies
**Never** use raw `Copy-Item` / `cp` / `shutil.copy` commands or write ad-hoc deployment scripts to copy `d2rl-pcvr.dll` into the game directory. Improvised copies bypass `require_game_closed()`, skip `validate_game()`, overwrite the working plugin without a verified backup, and risk corrupting user settings (`d2r_pcvr.ini`) or shaders (`D2R_DepthFog.fx`).

### Supported Deployment Routes

1. **Route A — Optional Local Helper (`build-frame/install_dll.py`, when present in an already-configured checkout):**
   If `build-frame/install_dll.py` exists locally, inspect it to verify its `staged_dll` and `game_dir` match the intended build, then run:
   ```powershell
   .\deps\python\python.exe build-frame/install_dll.py
   ```
   This helper enforces `install.require_game_closed()` and `game_install.validate_game()`, records before/after SHA-256 hashes of protected files (`d2r_pcvr.ini`, `d2r_pcvr.ini.before-vr-menu`, `vrmod-manifest.json`, `modinfo.json`, `D2R_DepthFog.fx`), writes a verified timestamped backup under `build-frame/backups/<UTC-timestamp>/d2rl-pcvr.dll.bak`, installs the staged DLL, restores the backup on failure, and updates `build-frame/latest-native-install.json`.

2. **Route B — Tracked Developer Mod Staging (`steamvr/prepare_dev_scenarios.py`, when `mods/D2R-PCVR` is already installed):**
   In a clean clone where `build-frame/install_dll.py` is absent but the target `<GAME_DIR>` already has a valid `mods/D2R-PCVR` installation, update or stage `mods/pcvr-dev` using the tracked script:
   ```powershell
   .\deps\python\python.exe steamvr/prepare_dev_scenarios.py --game "<GAME_DIR>" --plugin build-frame/windows-native/d2rl-pcvr.dll
   ```
   *(Add `--experimental-battlenet` when targeting an opted-in Battle.net install; use `build-frame/windows-developer/d2rl-pcvr.dll` when staging a `-Developer` build.)*
   [`steamvr/prepare_dev_scenarios.py`](../../../steamvr/prepare_dev_scenarios.py) enforces `require_game_closed()` and `validate_game()`, creates a hash-named backup (`d2rl-pcvr.dll.before-dev-<sha256>`), performs an atomic temp-file swap (`os.replace`), and verifies the installed bytes.

3. **Route C — Tracked Full Mod Installation (`steamvr/install.py`, for initial setup or full package install):**
   To run a full installation via [`steamvr/install.py`](../../../steamvr/install.py), all required runtime artifacts must be supplied explicitly:
   ```powershell
   .\deps\python\python.exe steamvr/install.py --game "<GAME_DIR>" --plugin build-frame/windows-native/d2rl-pcvr.dll --openxr-loader "<PATH_TO_OPENXR_LOADER_DLL>" --deps deps/vrdeps --preserve-settings
   ```
   *(On native Windows, `--openxr-loader` is mandatory; for Battle.net, `--experimental-battlenet` and `--casclib <PATH_TO_CASCLIB_DLL>` are also required, and pinned runtime archives must be present in `--deps` or fetched with `--download-deps` when authorized.)*

4. **When to Stop and Report (No Improvisation):**
   - If `D2R.exe` or `D2RLoader.exe` is running: do **not** terminate the user's game process. Leave the built DLL staged in `build-frame/` and ask the user to close the game first.
   - If `build-frame/install_dll.py` is absent and the prerequisites for Route B or Route C are not available (for example, `<GAME_DIR>` is unknown, `mods/D2R-PCVR` is not yet installed for Route B, or `openxr_loader.dll` / `CascLib.dll` / `deps/vrdeps` are absent for Route C): **stop immediately**, keep the built DLL staged in `build-frame/`, and report the exact missing prerequisite to the user. Never improvise a manual file copy to work around a missing installer prerequisite.

---

## 5. Verification Reporting Levels

Always report verification status accurately across these five distinct levels (and never claim a level was verified unless you actually ran it):
1. **Compiled:** Which target(s) built cleanly.
2. **Automated Tests:** Exact CTest / Python unittest counts run and passed.
3. **Staged:** Path and SHA-256 of the staged DLL under `build-frame/`.
4. **Installed:** Route used (`build-frame/install_dll.py`, `steamvr/prepare_dev_scenarios.py`, or `steamvr/install.py`), installed path, SHA-256, and backup path (or state explicitly that deployment was skipped and why).
5. **In-Headset Validation:** State explicitly that headset runtime behavior has **not** been tested by the agent and requires offline character testing by the user.
