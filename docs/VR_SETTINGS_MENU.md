# VR Settings menu: how to add, change or remove a setting

The in-headset VR Settings menu is built from data. Every row starts from the
setting's `Definitions` entry in `steamvr/vr_settings.h` (ini section and key,
default, min, max, step, label, detail). The menu's own words and placement
live in `steamvr/vr_settings_content.h` as **overrides**, keyed by the
setting's ini id (`"gameplay.highlight_left_hue"`), never by its `Key`.

| File | What it holds |
|---|---|
| `vr_settings.h` | `Key`, `Definitions`, `Normalize`, which rows can show (`RowShown`, `RowEnabled`): the settings themselves |
| `vr_settings_help.h` | the long help of each setting; a row without words of its own uses it |
| `vr_settings_content.h` | overrides (page, section, label, help, option names and hints, units, warn lines), sections, presets (by ini id; off until `PresetsReady` is true: the Presets tab is grayed out and nothing applies one), the guide, the merge (`Menu()`) |
| `vr_settings_hooks.h` | the few behaviors that name a setting by `Key` (the only place the menu can stop building when a setting is removed) |
| `vr_settings_layout.h` | geometry and control choice, from the data |

## Add a setting

1. Add its `Key` and `Definitions` entry in `vr_settings.h`. Name the ini key
   with a unit suffix where it has one: `_pct`, `_cm`, `_deg`, `_m`, `_ms`, or
   `_hue` for a color.
2. That is enough for it to show. With no override it gets a row with its
   `Definitions` label and the long help from `vr_settings_help.h` (else its
   detail), on its owner page by ini section, in a section titled "New" at the
   bottom:

   | ini section | page |
   |---|---|
   | `turn`, `calibration` | Body |
   | `bow` | Targets |
   | `hud` | Interface |
   | `sky`, `fog`, `ceiling`, `torch` | World |
   | `haptics` | Vibration |
   | `render`, `stereo` | Performance |
   | `gameplay`, `interactions`, anything else | More |

   A page that already shows 8 rows, or 3 sections without a "New" one, sends
   it to **More**.
3. To give it proper words, add an override in `vr_settings_content.h` at the
   spot on its page: `.id`, `.page`, `.section` (a title from `SectionTable`),
   `.label`, `.help`, and option names and hints for a list. Every field but
   `.id` is optional. Never restate a default, min, max or step.
4. Run `vr-settings-test` and `vr-settings-test --tidy`, and look at the page
   with `vr-settings-preview --pages <folder>`.

## Change or remove

- **Rename what players read:** change the override's `.label`, never the ini key.
- **Move:** change `.page` / `.section` and move the entry in the table (the
  table is in page and section order).
- **Keep a setting off the menu:** `{.id="...",.page=NotInMenu}` with a comment.
- **Remove a setting:** remove it from `vr_settings.h`. The menu still builds:
  its override and preset entries are skipped and listed by `--tidy`; then
  delete them.

## The control comes from the data

| Data | Control |
|---|---|
| 0 / 1 | an On / Off switch |
| a number | - value + with a bar and the default mark; drag the value to scrub (a stepper without a bar when there is no upper limit) |
| named choices, up to 6 that fit beside the label | chips, one press |
| named choices otherwise (up to 24) | the value between < and >, which step and go round; pressing the value opens a popover: a list up to 8, a grid of 2 or 3 columns up to 24 |
| a `_hue` key | a color dot and its name; pressing it opens the color grid |
| more than 24 named choices | not allowed: the number control, and `--tidy` asks to split the setting |

The choice is made per row and character, never per value. Named choices
missing some names read "Choice n" for those values. Every control takes a
press anywhere in its row's height.

**Popover:** opens over the page beside its row without moving any row, the
rest dimmed. One press on an option picks it and closes; options take presses
300 ms after it opens; a press outside it, Right B, or pointing away for
450 ms closes it without a change.

**Color grid:** named values outside the hue wheel (By range, Off) are chips
above; the 24 hues are dots, 6 a row in hue order (Red first, Crimson last),
46 px dots in 66 px cells, drawn in the color the game draws. The chosen dot is
ringed and the pointed one is named. Where it fits neither below nor above its
row it drops its title, then goes compact (64 px cells, 8 a row).

## The --tidy report

`vr-settings-test --tidy` prints, and every run of `vr-settings-test` ends with,
a report that never fails the build: settings without words or a page, orphan
overrides and preset entries, partly named lists, and a non-empty More page.

## More

More is a header view like Guide and What's new. Its band link ("2 more")
shows only while it has rows, 8 a page with Previous / Next. Today it holds
NVIDIA's flow quality and scale while Frame generation is NVIDIA, since
Performance has no room for two more rows; otherwise it is a safety net for
settings added since the menu was last sorted.
