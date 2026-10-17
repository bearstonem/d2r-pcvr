# Contributing to D2R PCVR

Contributions are welcome through pull requests. **Outside contributors must
fork this repository, create a branch in their fork, and open a pull request
against `bearstonem/d2r-pcvr:main`.** Maintainers may use branches in this
repository, but must also open a pull request. Do not push directly to `main`.

## Before starting

- Search existing issues and pull requests to avoid duplicate work.
- Open an issue before a large feature, architecture change, new dependency,
  or change to supported game builds. Small fixes and documentation corrections
  can go straight to a pull request.
- Keep discussion respectful and focused on the work. Explain disagreements
  with examples; harassment and personal attacks are not welcome.

## Fork and open a pull request

1. Click **Fork** on [the repository](https://github.com/bearstonem/d2r-pcvr).
2. Clone your fork, add the original repository as `upstream`, and create a
   branch from its current `main`:

   ```sh
   git clone https://github.com/YOUR-USERNAME/d2r-pcvr.git
   cd d2r-pcvr
   git remote add upstream https://github.com/bearstonem/d2r-pcvr.git
   git fetch upstream
   git switch -c fix/describe-your-change upstream/main
   ```

3. Make a focused change. Include relevant documentation and regression
   coverage for behavior changes; avoid unrelated cleanup and generated files.
4. Run the checks appropriate to your change and record their results.
5. Commit and push the branch to **your fork**:

   ```sh
   git add PATHS-YOU-CHANGED
   git commit -m "Describe the change"
   git push -u origin fix/describe-your-change
   ```

6. Open a pull request with **base: `bearstonem/d2r-pcvr`, branch: `main`**
   and **head: your fork and branch**. Fill in the pull request template.
   Use a draft PR when the work is not ready for review.
7. Address review feedback in the same branch. Maintainers decide whether and
   when to merge; submitting a PR does not guarantee acceptance.

## Build and validation

The active Windows/OpenXR implementation is built from
[`steamvr/CMakeLists.txt`](steamvr/CMakeLists.txt). The root CMake project also
contains the original upstream implementation; use the native Windows scripts
for changes to D2R PCVR.

From the repository root in PowerShell, with Git installed:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File steamvr/setup_windows_tools.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File steamvr/build_windows.ps1 -Test
```

The setup script downloads pinned build tools and SDKs into `deps/`. The build
script stages its output in `build-frame/windows-native/`; it does not install
the mod into a game. For Windows installer, launcher or uninstaller changes,
run the relevant Python fixture tests, for example:

```powershell
.\deps\python\python.exe -m unittest discover -s steamvr -p setup_windows_test.py
.\deps\python\python.exe -m unittest discover -s steamvr -p install_test.py
.\deps\python\python.exe -m unittest discover -s steamvr -p launch_windows_test.py
.\deps\python\python.exe -m unittest discover -s steamvr -p battlenet_test.py
.\deps\python\python.exe -m unittest discover -s steamvr -p uninstall_windows_test.py
.\deps\python\python.exe -m unittest discover -s steamvr -p setup_native_test.py
.\deps\python\python.exe -m unittest discover -s steamvr -p mod_identity_test.py
```

For isolated gameplay testing, see [developer mode](steamvr/DEVELOPMENT_SCENARIOS.md).
`--frame-dev` selects a separate offline character list; its DEV tools provide
level presets, class gear, travel and the last published release reference.
`build_windows.ps1 -Developer -Test` stages a candidate separately for this mode.
Run `prepare_dev_scenarios_test.py` and `launch_windows_test.py` after changing
developer staging or launch flags. Linux `session_test.py` requires `/proc` and
is not a Windows check.

Report the commands you ran and any failures. Documentation-only changes
normally need a content and link check, not a full native build. If you cannot
run a relevant check, say so in the PR instead of claiming it passed.

For gameplay or rendering changes, describe the headset, connection/runtime,
Steam or Battle.net edition, game build, character class and scenario used.
Separate automated results from actual headset testing. If you have not tested
in a headset, state that explicitly so a maintainer can arrange verification.
Test only with offline characters; do not test this injected mod in online play.

## Files, licensing and player data

- You must own a legitimate copy of Diablo II: Resurrected for game testing.
- Do not commit or attach Blizzard executables, game archives, extracted game
  models/textures/audio, memory dumps, character saves, or the generated
  `d2r_vr_icons` and `d2r_vr_potions` caches. They are not part of the mod's
  distributable source.
- Do not commit downloaded dependencies, build output, release ZIPs, credentials
  or private player/account data. Review logs before sharing them.
- Submit code and assets you have the right to contribute under the project's
  [MIT license](LICENSE). Preserve copyright notices and credit upstream work.
  Document third-party licenses in [THIRD_PARTY.md](THIRD_PARTY.md); do not add
  dependencies whose terms prevent distribution with the mod.
- Installer and uninstaller changes must preserve player saves, support repeat
  runs and restore tracked backups without deleting unrelated files. Keep
  normal unmodded desktop play available.

## Review and protected main

`main` requires pull requests and blocks force pushes and branch deletion.
Contributor PRs require an approving maintainer review; new commits dismiss
stale approvals. Review conversations must be resolved before merging.
[`CODEOWNERS`](.github/CODEOWNERS) routes review requests to `@bearstonem`.

The repository owner may merge their own maintainer PR through GitHub's
PR-only review exception because they cannot approve their own PR. That
exception does not permit direct pushes and does not bypass the separate
main-integrity ruleset. Do not disable protection to land a change.

GitHub's live settings are authoritative. The intended configuration and
maintenance notes are kept in [`.github/rulesets/`](.github/rulesets/README.md).

Release tags matching `v*` are restricted to the repository owner. Future
releases use GitHub release immutability: prepare a draft, attach and verify
all assets, then publish. Published immutable assets require a new release
version for corrections. Existing releases are not made immutable retroactively.

## Reporting problems and suggesting features

Use the [issue forms](https://github.com/bearstonem/d2r-pcvr/issues/new/choose).
For bugs, include reproduction steps, expected and actual behavior, mod/game
versions, headset/runtime and relevant logs or a short recording. Do not post
game assets, saves, account details or memory dumps with a report.
