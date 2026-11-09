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
| `vr_settings_content.h` | overrides (page, section, label, help, option names and hints, units, warn lines), pages and the rail's groups, sections, presets (by ini id; off until `PresetsReady` is true: the Presets tab is grayed out and nothing applies one), the guide, the merge (`Menu()`) |
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
   | `turn` | Moving |
   | `calibration` | Body |
   | `bow` | Targeting |
   | `hud` | Menus |
   | `sky`, `fog`, `ceiling`, `torch` | Environment |
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
| `.beside` an on / off setting | a box on the right of the row before it, "<word> On", flipped by a press |
| `.beside` a number | a box on the right of the row before it, "<word> <value>"; pressing it opens the list of its steps (`Normalize`'s distinct values, at most 24) |
| more than 24 named choices | not allowed: the number control, and `--tidy` asks to split the setting |
| `RowKind::Filter` on a number, `.inFilter` on the on / off settings after it | one row: its number as - value + with a bar, then a **Filter** button; pressing it opens a card of the On / Off switches |

A `.beside` setting shares its partner's row and takes none of the page's 8
(main's paired rows: Menu distance | Hold
menu, Distance fog | Fog distance, Scenery distance | Big objects). Its box
reads `.besideLabel` ("Big:", "Ends at", "Torches"), else its label; its card
title is its label.

The choice is made per row and character, never per value. Named choices
missing some names read "Choice n" for those values. Every control takes a
press anywhere in its row's height.

A Filter row and its `.inFilter` settings (Loot beams: its brightness, 10% to
100%; then Unique, Set, Rare, Runes) take one of the page's 8 rows, on the same
page and section, 1 to 6 switches. The row's line under its label names the
switches that are On ("Unique, Set, Runes"; "Off: none"). Each `.inFilter`
setting's label is its word in the card (14 characters at most). The pending
list names them "Loot beams (Set): On -> Off", Reset page resets them all, and
the row's help card's Undo and Default take the number and every switch.

**Popover:** opens over the page beside its row without moving any row, the
rest dimmed. One press on an option picks it and closes; options take presses
300 ms after it opens; a press outside it, Right B, or pointing away for
450 ms closes it without a change. A Filter card is the exception: a press
flips one switch and the card stays open, so several can change before it
closes; every line is that switch's press target.

**Color grid:** named values outside the hue wheel (By range, Off) are chips
above; the 24 hues are dots, 6 a row in hue order (Red first, Crimson last),
46 px dots in 66 px cells, drawn in the color the game draws. The chosen dot is
ringed and the pointed one is named. Where it fits neither below nor above its
row it drops its title, then goes compact (64 px cells, 8 a row).

## The rail

The left rail is Presets, then one row per group in `GroupTable`
(`vr_settings_content.h`): Gameplay, Comfort, Controls, Interface and Graphics. A page belongs
to a group by its `PageTable` line (`.group`); the open group's pages hang
under its row, indented, in `PageTable` order. A group's pages follow one
another in `View` order.

- **Open group:** the shown page's. On Guide, What's new and More it stays
  the last page's group, so the rail holds still while a band link is open.
- **A group's row:** opens the page last opened in that group (memory only,
  like the open view; a new game goes back to each group's first page).
  Pressing the open group's row changes nothing. Any page is at most two
  presses away.
- **Changed mark:** a group's row shows it while any of its pages has a
  change not applied.
- **A group's row moves only when a group above it opens or closes.** Rows above
  both groups stay put.
- **Press guard:** for 300 ms after the rows move, a new pull on a rail row does
  nothing, with no buzz and one log line, since a row other than the one pressed can
  have slid under a laser that has not moved. The rest of the panel stays live.
- **Fit:** Presets, the group rows and the largest group's pages fit 12 rows
  (78 px apart). A compile-time check in `vr_settings_content.h` stops the
  build when a page would not fit. A group has at most 6 pages, and every settings
  page but More holds at least 3 settings. `--fit-check` measures the names: a group
  in its row's 162 px at 28 px, a page in its 172 px at 32 px.

To add a page: a `View`, its `PageTable` line with its group, its
`SectionTable` entries and the `.page` of its rows. No layout code.

## One setting on two pages

`Override::alsoOn` draws a row on a second, later page as well, at its top with
no section: Eyes drawn per frame on Graphics > Performance and Graphics > Frames. It
stays one setting, one ini key and one `Row(key)`, so a press on either page changes
the same value and both show it at once. Each page counts it as a change of its own;
its group's card counts it once. Frames' rows gray while One by turns is on
(`RowEnabled`), `hooks::PageReason` says why above them, and `Reset page` resets a row
from another page last. A page with that line draws no intro, so its rows stay put.

## The --tidy report

`vr-settings-test --tidy` prints, and every run of `vr-settings-test` ends with,
a report that never fails the build: settings without words or a page, orphan
overrides and preset entries, partly named lists, and a non-empty More page.

## More

More is a header view like Guide and What's new. Its band link ("2 more")
shows only while it has rows, 8 a page with Previous / Next. In the shipped menu
it is empty, and a test says so: it is a safety net for settings added since the
menu was last sorted.
