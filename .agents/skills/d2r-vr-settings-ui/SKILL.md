---
name: d2r-vr-settings-ui
description: >-
  Add, modify, or resolve merge conflicts for VR Settings keys, menu rows, value words,
  help cards, INI persistence, and settings tests. Use when editing steamvr/vr_settings*
  or vr/settings_menu*.
---

# VR Settings Architecture & Modification Checklist

The in-headset menu is built from data: every row starts from the setting's
`Definitions` entry, and the menu adds only words, icons and page placement.
[`docs/VR_SETTINGS_MENU.md`](../../../docs/VR_SETTINGS_MENU.md) is the reference
for adding, moving or removing a setting; read it first. Always inspect the current
definitions and overrides rather than relying on memorized enum counts, pages or row
counts. Some settings have no row: set by Calibrate body, set from the hand wheel
(`LeftHandStrikes`), legacy, or INI only (`DesktopOneEye`); they are listed with
`.page=NotInMenu`.

---

## 1. File Responsibilities

| File | Responsibility |
| :--- | :--- |
| [`steamvr/vr_settings.h`](../../../steamvr/vr_settings.h) | The settings: `enum Key`, `ChangeMask` (`static_assert(Count <= ChangeMask::Bits)`), `Definitions` (label, detail, INI section/key, default, min, max, step), `Normalize()`, `RowShown()` (hidden), `RowEnabled()` (locked), `Model` (draft, Apply, `Click()` for Normalize's steps and a few keys' own stepping). |
| [`steamvr/vr_settings_content.h`](../../../steamvr/vr_settings_content.h) | The menu's words and placement as **overrides keyed by INI id** (`"fog.end"`): page, section, label, help (<= 160 chars), option names and hints, units, warn lines, `.beside` (a toggle on the previous row's right half), `.instead`; sections; presets; the guide; the merge (`Menu()`). `static_assert`s enforce text lengths, order, at most 8 rows and 3 sections a page. |
| [`steamvr/vr_settings_hooks.h`](../../../steamvr/vr_settings_hooks.h) | The few behaviors that name a `Key` (locked reasons, the body measurements row, keys whose steps `Model::Click` owns). |
| [`steamvr/vr_settings_layout.h`](../../../steamvr/vr_settings_layout.h) | Geometry and control choice from the data (switch, chips, popover, number with a bar, color grid), hit tests, `Press()`, `Step()`. |
| [`steamvr/vr_settings_help.h`](../../../steamvr/vr_settings_help.h) | The long help of each setting; a row without words of its own uses it. |
| [`steamvr/vr_settings_draw.cpp`](../../../steamvr/vr_settings_draw.cpp), [`steamvr/vr_settings_ui.cpp`](../../../steamvr/vr_settings_ui.cpp) | Direct2D / DirectWrite painting on the settings worker thread. |
| [`steamvr/vr_settings_pointer.h`](../../../steamvr/vr_settings_pointer.h) | The laser and trigger: hover, press on release, hold-repeat, drag, the help card's focus, popover point-away close. |
| [`vr/settings_menu.cpp`](../../../vr/settings_menu.cpp) | Win32 INI persistence (`Load`, `Save`, `Submit`, `Poll`), first-edit backup, conflicts, legacy migrations. |
| [`d2r_pcvr.ini`](../../../d2r_pcvr.ini) | Shipped defaults with player-facing comments. |
| [`steamvr/transport.cpp`](../../../steamvr/transport.cpp) | Opens and closes the panel, feeds the pointer, paints on the worker and uploads into the shared panel atlas (`panelatlas::Settings`, `UploadShared`); reads `vrsettings::Read()` for the runtime. |

---

## 2. Adding or Changing a Setting

1. **`vr_settings.h`:** add the `Key` before `Count` and its `Definitions` entry at the same index. Give custom snapping (Off plus a range, percent steps) a branch in `Normalize()`; a key whose stepping is not `Normalize(v + delta * step)` gets a branch in `Model::Click()`, and must handle a drag's `delta` of several steps. Hide with `RowShown()`, lock with `RowEnabled()` (and give the lock words in `hooks::LockedReason`).
2. **`vr_settings_content.h`:** add an override at its spot in page and section order. Never restate a default, min, max or step. A page shows at most 8 rows (a `.beside` setting takes none); when a page is full, move a row or put the setting `.beside` its partner: a toggle flips in its box, a number's box opens the list of its steps, and `.besideLabel` is the short word in that box. Without an override a setting still shows, on its owner page by INI section or on **More**.
3. **`vr_settings_help.h`:** a long help `case` for the card when the override's help is not enough.
4. **`d2r_pcvr.ini`:** document the key under its section, naming the menu page (`VR Settings > Graphics > Environment > Fog distance`).
5. **Runtime:** read the normalized value from the settings snapshot where it is used.

### The rail

The rail is Presets, then a row per group (`GroupTable` in `vr_settings_content.h`: Gameplay, Comfort, Controls, Interface, Graphics); the open group's pages hang under its row. A page joins a group by its `PageTable` line's `.group`; never add rail code for a page. A group's row opens the page last opened there (`Model::groupPage`, memory only, reset by `StartOver`). `GroupsOk()` stops the build when the rail would pass 12 rows. Rules and the add-a-page steps: [`docs/VR_SETTINGS_MENU.md`](../../../docs/VR_SETTINGS_MENU.md#the-rail).

---

## 3. Tests & Merge Conflicts

- Resolve conflicts in `vr_settings.h` toward the data model: page catalogs, `PairOf`, `ValueLabel` and fixed rects are gone. Carry a new setting from the other side as an override, not as layout code.
- [`steamvr/vr_settings_test.cpp`](../../../steamvr/vr_settings_test.cpp): defaults, INI keys, `Normalize`, the page (`content::RowOf(key)->page`), stepping through `layout::Press(model, {Kind::Change, key, delta}, 0)`, locks, and `Apply`'s `ChangeMask` isolation.
- [`steamvr/vr_settings_layout_test.cpp`](../../../steamvr/vr_settings_layout_test.cpp): every setting reachable once, at most 8 rows for every class and state, no overlaps, full-row hit targets; the rail's rows for every open group, every page in 2 presses or fewer, last-page memory.
- `vr-settings-test --tidy` lists settings without words, orphan overrides and a non-empty More page.
- Render the pages and look at them: `vr-settings-preview --pages <folder>`.

---

## 4. Narrowest Relevant Validation Commands

Check [`steamvr/CMakeLists.txt`](../../../steamvr/CMakeLists.txt) for the current settings targets and tests, then build and run only those:

```powershell
.\deps\cmake-4.4.4-windows-x86_64\bin\cmake.exe --build build-frame/windows-native --parallel 4 --target vr-settings-test vr-settings-layout-test vr-settings-pointer-test vr-settings-store-test vr-settings-preview controller-labels-test
.\deps\cmake-4.4.4-windows-x86_64\bin\ctest.exe --test-dir build-frame/windows-native -R "^(vr-settings|settings-release-notes|controller-labels)" --output-on-failure
```
*(If your local `deps/` CMake directory version differs, substitute the `cmake.exe` / `ctest.exe` path provisioned under `deps/`.)*
