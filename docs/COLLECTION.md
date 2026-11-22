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

- A row of category tabs - Uniques, Sets, Runewords - with the page numbers beside
  them. (A Materials category stays in the catalog, but the owner does not want its
  tab.)
- One slot per catalog entry: the item's own icon when the shared stash holds it,
  a dim dashed outline when it is still missing. A unique's slot is gold on brown,
  a set item's green. A page the filter leaves short is filled with grey sockets, so
  the grid always reads full.
- A footer: the page arrows, the counter for the open category, a **Size** control
  (S / M / L = 70 / 35 / 12 slots a page, so a larger slot means fewer entries) and a
  **Show** filter (All / Missing / Found).

While the page is open the Collection tab on the stash bar carries the game's own
active-tab mark: a gold frame on the tab's top and sides, and the game's blue diamond
at its bottom (`steamvr/stash_tab_highlight.h`; the diamond itself is a sprite cut from
a UI shot, `steamvr/tab_gem.h`). The mod cannot set the game's chosen tab, so it draws
the mark itself.

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

Click the page:

- **with an item in your hand** (on the game's cursor) the click **stores it in the
  collection**, in its own slot by the item's own identity, wherever on the page it was
  clicked (`DepositCarried`). The page takes no game click while an item is held.
- **empty-handed**, a click on a slot acts on that slot's item: a stored copy is taken
  out (onto the cursor, like a native pick-up; the inventory is the fallback if the
  loader refuses), and its slot goes grey at once (`Library::MarkMissing`); a copy you
  merely carry (in the inventory or cube) is stored in the collection.

While an item is held the page's own quad is not drawn, so the dragged item (which the
game draws onto the stash) is not hidden under it; the pointer still works over the
page's place, so the click still stores.

A runeword slot has no single base item in the catalog yet, so it can be read but
not stored.

## Where the items live

The collection's tabs are a range at the end of the shared stash, added by
D2RLoader's `[d2rcore.stash] add_shared_tabs`. The installer sets it (40 tabs) to
match the mod's `[collection] tabs`. The collection's tabs come after the vanilla
ones: `[collection] first_tab` (default 5) is the first the collection owns.

**Affinity** (`[collection] affinity`, **off by default**): an item dropped anywhere in
the shared stash goes to its own collection tab. A unique or set item brings itself
into view; a gem or rune does not. Only the first copy of an entry is kept, so a
second one stays where you put it. Turn it off so a drop into the personal or shared
stash stays where you put it. Note the installer never overwrites your `d2r_pcvr.ini`,
so an older install can leave `affinity=1` behind - check the file if items move on
their own.

## Settings

`[collection]` in `d2r_pcvr.ini`:

| Key | Meaning |
| --- | --- |
| `affinity` | 1 = sort items dropped in the shared stash into the collection's tabs; 0 = off (default) |
| `first_tab` | the first shared stash tab the collection owns (the vanilla tabs come before it; default 5) |
| `tabs` | how many shared stash tabs the collection owns (default 40) |

`first_tab` and `tabs` must match `[d2rcore.stash] add_shared_tabs` in the mod's own
`d2rloader/config/d2rloader.toml`; the installer keeps them in step.
