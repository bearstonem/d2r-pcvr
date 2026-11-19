# Collection

The stash's own page for what you have collected: one slot per unique, set item,
runeword, gem and rune, with a greyed outline for the ones still missing. The
stored items live in extra shared stash tabs, so the collection is shared by the
account and touches no character save.

## Opening it

- The **Collection** tab the mod adds to the stash's own tab bar - the sixth tab
  beside Personal, Shared, Gems, Materials and Runes. The installer prepares that
  layout from your own game (`steamvr/prepare_collection_tab.py`); without it the
  stash keeps its five tabs.
- The **Collection** plaque beside the stash's Previous / Next tab.
- **F7**, without the developer console.

## The panel

- A row of category tabs - Uniques, Sets, Runewords, Materials - with the page
  numbers beside them.
- One slot per catalog entry: the item's own icon when the shared stash holds it,
  a dim dashed outline when it is still missing. A unique's slot is gold on brown,
  a set item's green.
- A footer: the page arrows, the counter for the open category, a **Size** control
  (small / medium / large slots - larger means more pages) and a **Show** filter
  (All / Missing / Found).

The catalog comes from your own game's tables (`steamvr/prepare_collection.py`),
built at install time and never shipped.

## Reading an item

Hover a slot: a card shows the item's name, its category, its level and whether the
stash holds it, then the game's own item text.

- A **found** unique or set shows the item's own text, then, under a rule, its
  **possible ranges**, so the roll it has can be compared with what it could have
  rolled.
- A **missing** item shows the ranges the game would give it.

The text comes from the game itself (`getItemTooltip` for a stored item,
`getItemPreviewTooltip` for one you do not have), so it reads exactly as the game
does. It needs a D2RLoader build that offers those calls (PluginSDK 0.4.0); on an
older loader the card keeps its catalog lines and says the text is unavailable.

## Storing and taking out

Click a slot:

- a stored copy goes back to your inventory, or
- a carried copy (in your inventory or cube) is stored in the collection's own tab
  for its category, or
- the log says the item is not held.

A runeword slot has no single base item in the catalog yet, so it can be read but
not stored.

## Where the items live

The collection's tabs are a range at the end of the shared stash, added by
D2RLoader's `[d2rcore.stash] add_shared_tabs`. The installer sets it (24 tabs) to
match the mod's `[collection] tabs`. The collection's tabs come after the vanilla
ones: `[collection] first_tab` (default 5) is the first the collection owns.

**Affinity** (`[collection] affinity`, off by default): an item dropped anywhere in
the shared stash goes to its own collection tab. A unique or set item brings itself
into view; a gem or rune does not. Only the first copy of an entry is kept, so a
second one stays where you put it.

## Settings

`[collection]` in `d2r_pcvr.ini`:

| Key | Meaning |
| --- | --- |
| `affinity` | 1 = sort items dropped in the shared stash into the collection's tabs; 0 = off (default) |
| `first_tab` | the first shared stash tab the collection owns (the vanilla tabs come before it; default 5) |
| `tabs` | how many shared stash tabs the collection owns (default 24) |

`first_tab` and `tabs` must match `[d2rcore.stash] add_shared_tabs` in the mod's own
`d2rloader/config/d2rloader.toml`; the installer keeps them in step.
