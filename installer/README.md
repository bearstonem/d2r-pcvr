# Native Windows setup EXE

`D2R_PCVR_Setup.iss` builds the graphical installer for the native OpenXR mod.
(The upstream D2R VR installer scripts, `D2R_VR_Setup.iss` and
`build_installer.ps1`, carried D2R VR's own AppId and are no longer kept here.)

Players see it as **D2R VR Unleashed** (the installer's name, Installed apps,
`modinfo.json`'s description); the ids stay D2R PCVR's. It installs as the
D2RLoader mod `D2R-PCVR`: `mods\D2R-PCVR\d2rloader\plugins\d2rl-pcvr.dll` with
`d2r_pcvr.ini` and `openxr_loader.dll`, its ReShade in `mods\D2R-PCVR\reshade\`,
`mods\D2R-PCVR\D2R-PCVR.mpq\modinfo.json`; the shortcuts start
`D2RLoader.exe -mod D2R-PCVR` (`steamvr/mod_identity.py`, `steamvr/launch_windows.py`).
See [migration and validation notes](../docs/MOD_COEXISTENCE.md) for shared
dependency protection, ReShade migration, log paths and remaining gameplay checks.
Releases up to 0.3.16 used D2R VR's names, `d2rl-vrcam.dll` and `d2r_vr.ini`;
D2R VR still uses them. Setup decides whose those files are from this mod's
own install record and snapshots plus the plugin's content, never from the
name (`steamvr/mod_identity.py`):

- An earlier D2R PCVR: Update / repair copies `d2r_vr.ini` (settings and body
  calibration) to `d2r_pcvr.ini`, then removes the old plugin and ini. Both are
  journaled in the new snapshot, so a failed setup or `install.py --restore`
  puts them back, and removal still restores the pre-mod state.
- D2R VR (or a `d2rl-vrcam.dll` no D2R PCVR record lists): setup says another
  mod is installed and stops before changing anything; D2RLoader would load
  both VR plugins. Its files are never changed, moved or deleted, and removal
  of D2R PCVR leaves them in place. The plugin itself also stays off while a
  `d2rl-vrcam.dll` sits beside it.

The wizard and portable CMD setup share `steamvr/setup_windows.py` for the
installation and Steam configuration. `steamvr/setup_native.py` adds unattended
selection validation, ZIP backup migration, per-copy receipts and managed
removal. The EXE includes an isolated CPython runtime; it does not require or
modify system Python. Loader/ReShade downloads retain the existing pinned hashes.

The D2RLoader 1.3.1 URL was repacked upstream on October 13, 2026. Native setup
accepts the two explicitly reviewed archive hashes in `steamvr/install.py`;
every contained file is byte-identical. Existing verified caches remain usable.
Unknown hashes still stop setup before game writes, with expected/received
values in the error. Do not fetch a new trusted digest automatically or bypass
verification. See the [archive and v0.3.10 release audit](../steamvr/evidence/2026-10-13-loader-archive-repack.json).

Backup discovery is read-only. The probe checks the selected game's receipt
and managed backups first, then known backup subfolders near the installer or
the helper referenced by existing PCVR Steam launch settings. Only a complete,
validated chain for that game can prefill the older-backup page; conflicting
locations require a manual choice. The wizard revalidates on Next and clears
the hint when the selected game changes. Successful adoption copies and verifies
the snapshots, then records their managed locations in that game's existing
`d2rloader/pcvr-install.json`; there is no separate global backup-history file.

## Build

Use a clean committed topic branch. Prepare the version, GitHub notes and
in-headset summary with the [release workflow and template](../docs/RELEASING.md).
Build the native plugin as described in `CONTRIBUTING.md`, then package its output:

```powershell
.\deps\python\python.exe steamvr/package.py --build build-frame/windows-native --openxr-loader build-frame/windows-native/openxr_loader.dll --casclib build-frame/casclib-installer/CascLib.dll --release-tag v0.3.18-windows-preview --output build-frame/releases/v0.3.18-windows-preview/d2r-pcvr-windows.zip
```

Use the actual paths from your native and CascLib builds. When reusing an unchanged
published plugin, pass `--plugin-source-commit` with its original source commit;
do not claim that it was rebuilt from the installer change. Its embedded release
summary must still match the source notes and requested tag; otherwise rebuild.

Install [Inno Setup 7.1.0 or newer](https://jrsoftware.org/isdl.php), then run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File installer/build_native_installer.ps1 -PackagePath build-frame/releases/v0.3.18-windows-preview/d2r-pcvr-windows.zip -Version 0.3.18 -ISCCPath 'C:\Program Files (x86)\Inno Setup 7\ISCC.exe'
```

The wrapper verifies the native ZIP manifest and distributable-file allowlist,
downloads official Python 3.12.10 x64 if needed, verifies its pinned archive hash
and Python Software Foundation signature, then compiles a fresh staging folder.
It also checks that `-Version` matches the ZIP's release tag. It never stages a
game installation or generated artwork/model cache. Output:

- `build-frame/installers/D2R-PCVR-Setup-0.3.18.exe`
- matching `.exe.sha256` checksum and `.exe.build.json` provenance report

The EXE is unsigned unless a release maintainer separately signs it. Signing
requires an appropriate publisher certificate; do not label an unsigned EXE as
signed. Build output and downloaded dependencies stay out of source control.
Publish only reviewed distributables, checksums and player-facing documents.

## Validate

From the repository root:

```powershell
.\deps\python\python.exe -m unittest discover -s steamvr -p setup_native_test.py
.\deps\python\python.exe -m unittest discover -s steamvr -p setup_windows_test.py
.\deps\python\python.exe -m unittest discover -s steamvr -p uninstall_windows_test.py
.\deps\python\python.exe -m unittest discover -s installer -p '*_test.py'
```

`steamvr/session_test.py` targets Linux `/proc` and is not a Windows check.
The graphical backend tests use temporary fixture folders, never the player's
Steam profile or game. They cover both editions, connection choices, repeated
repair, missing backups, Unicode INI paths, rollback, and multi-copy removal.

Run the compiled installer lifecycle check separately:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File installer/smoke_native_installer.ps1
```

This compiles the same wizard with a distinct fixture app ID and a stub backend.
It installs only into a fresh `build-frame/installer-qa-...` folder, checks the
Windows Apps entry, forces a removal failure to verify helper retention, then
removes the fixture successfully. It never calls the real installation backend
or changes a game/Steam profile. Keep its logs with the validation record;
it verifies Inno's lifecycle, not a real game install or headset launch.

Check the payload actually embedded in the compiled EXE without installing:

```powershell
.\build-frame\installers\D2R-PCVR-Setup-0.3.18.exe /PAYLOADCHECK=1 /VERYSILENT /SUPPRESSMSGBOXES /LOG=payload-check.log
```

The setup intentionally aborts after validation, so its exit code is nonzero.
Success is the log line `D2R PCVR PAYLOADCHECK PASSED; no game or Steam files changed.`
This check extracts only temporary files, verifies every manifest hash and
does not register an installation or write game/Steam files.

Before a release, use disposable test profiles/installations for a complete
first install, a ZIP upgrade, repeated repair, both editions and Windows Apps
removal. Confirm cancellation and running Steam/D2R stop before changes, then
verify the resulting shortcut in a headset with an offline character. Report
which checks were automated and which were performed on real hardware.

For unattended QA, `/CONFIG="absolute path to choices.ini"` supplies the same
selections as the UI. Silent installation without a config is rejected. Example:

```ini
[setup]
store=steam
headset=Quest 3
connections=steamvr,vdxr
steam=C:\Program Files (x86)\Steam
game=C:\Games\Diablo II Resurrected
saves=C:\Users\PLAYER\Saved Games\Diablo II Resurrected
steamvr=C:\Program Files (x86)\Steam\steamapps\common\SteamVR
profile=LOCAL_NUMERIC_STEAM_ID
mode=repair
backups=
```

Choices accept UTF-8 (with or without BOM) and UTF-16. `store` is `steam` or
`battlenet`; `mode` is `full`, `repair` or `shortcuts`. Use real detected paths
and the local Steam profile directory ID. `backups` is optional: set it to the
original snapshot folder or its parent when upgrading an older mod without a
complete receipt. Do not share player/account config.

## Recovery and removal

Setup copies only its allowlisted helpers into its permanent per-user folder.
Game snapshots and managed receipts are outside Inno's installed-file list;
repair preserves them and removal leaves backups/logs for the player.
The backend journals game updates and restores changes from a failed attempt.
It preserves unrelated Steam configuration edits and never restores character
saves from an older snapshot. ZIP migration checks all copied backup files.

Windows removal first validates every managed copy, then restores mod files and
removes matching Steam entries in one transaction. It runs this operation at
`usUninstall`, before Inno deletes helpers; failure aborts file removal. A stopped
or incomplete setup retains its helpers and Windows uninstall entry for recovery.
No operation disables Windows Security, terminates Steam/D2R or changes the
system OpenXR runtime.
