# Preparing consistent releases

Start from [the GitHub release template](../.github/RELEASE_TEMPLATE.md). GitHub
does not apply this repository file automatically; maintainers copy it into
`RELEASE_NOTES.md` and use that file as the release body. Keep its section order,
remove empty change categories, and fill every placeholder. Feature bullets
describe player-facing game changes, with contributor credits beside the work.
Do not add template maintenance, release tooling or test infrastructure as
release features. Verification belongs in its separate section.

Select the main features, meaningful gameplay changes and major bug fixes;
release notes are not a list of every merged PR. Group related implementation
steps and follow-up corrections into the feature's finished player experience.
Leave out tiny fixes and revisions to unreleased attempts unless they have a
significant separate impact. Use the same broad selection for the in-headset
feature summary and announcement drafts; the source comparison provides the
complete change history.

## Prepare the content and version

1. Choose the next version and review the changes merged since the previous tag.
   Keep unrelated experiments and unverified claims out of the release.
2. Update `vr/pcvr_identity.h`, including the numeric resource version, the version in
   `vrmod-manifest.json`, and the installer defaults in `installer/D2R_PCVR_Setup.iss`
   and `installer/build_native_installer.ps1`. Existing identity tests check agreement.
3. Fill the template in `RELEASE_NOTES.md`, including its versioned heading and
   links. Update the `PCVR_IN_HEADSET_NOTES` comment at the end with the same
   release tag, current contributor credits, and 1-72 short feature bullets (the
   headset's Notes tab shows 12 a page, with Previous / Next).
   Each summary line must be at most 100 characters and fit the UI preview.
   This comment is hidden in rendered GitHub notes. Its summaries are curated
   during release preparation; the build does not invent summaries from commits.
4. Commit the release preparation on a topic branch and review it through a PR.
   Do not change a published immutable release's assets or tag to fix a binary.

## Build and validate

`steamvr/CMakeLists.txt` generates `release_notes_generated.h` from the Markdown
summary before compiling either the plugin or settings preview. Changes to the
notes, plugin version or generator invalidate the generated file automatically.
Missing summaries and mismatched heading/plugin/summary versions stop the build.
The UI displays the generated version, author, GitHub URL, features and credits.

Run the native build and appropriate release checks. These checks specifically
cover release metadata and packaging:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File steamvr/build_windows.ps1 -Test
.\deps\python\python.exe -m unittest discover -s steamvr -p release_notes_test.py
.\deps\python\python.exe -m unittest discover -s steamvr -p mod_identity_test.py
.\deps\python\python.exe -m unittest discover -s installer -p stage_native_installer_test.py
```

Inspect `build-frame/windows-native/release-notes.bmp` for the title, author,
GitHub URL, credits and readable one-line features. The native preview test checks
text fit. Record real headset verification separately from automated tests.

Package the clean committed source using [the installer build instructions](../installer/README.md).
For current-source native Windows packages, `steamvr/package.py` checks the
requested tag and a summary fingerprint embedded in the actual DLL. It refuses
an old DLL even if someone copied it into a newly named build folder. Rebuild
after changing the summary; `--plugin-source-commit` does not bypass this check.
The archive manifest records the summary and its fingerprint. Archived Linux
baseline packages retain their separate historical workflow.

The installer wrapper passes its version to the staging validator. A package
with another release tag is rejected before staging files are written. Do not
hand-edit manifests or generated headers to bypass a mismatch.

## Publish

After the release PR is merged, verify the exact source and artifact hashes and
create a draft release using the agreed version tag and `RELEASE_NOTES.md` body
(`gh release create ... --draft --notes-file RELEASE_NOTES.md`). Attach the
installer, portable ZIP and checksums, inspect the rendered notes and assets,
then publish after the required review/testing. Follow the repository's
[tag and immutable-release rules](../.github/rulesets/README.md).

The in-headset page is bundled with the installed build and works offline. It
updates when players install the next release; it does not fetch changing GitHub
content while the game runs. During development it continues to show the last
prepared release until release preparation updates the Markdown summary.
