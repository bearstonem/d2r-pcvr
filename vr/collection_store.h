#pragma once
// Collection tabs, step 3: affinity.
//
// With affinity on, an item that lands anywhere in the shared stash goes to the
// collection's own tabs: a unique or set item to its category's tab and the
// panel's view follows it; a gem or rune to the Materials tabs without moving
// the view. The stash is swept while it is open, so the move happens right after
// the drop. Only the first copy of a catalog entry is kept: a second one is left
// where the player put it.
//
// The collection's tabs are a range at the end of the shared stash, added by
// D2RLoader's [d2rcore.stash] add_shared_tabs: firstTab is the first one the
// collection owns (the vanilla tabs come before it) and tabs is how many.
#include <cstdint>
#include <string>

#include "collection_model.h"

namespace D2RL {
struct PluginContext;
namespace Items { struct ItemInfo; }
}

namespace framevr::collection {

// Stores the context and the services. affinity comes from the ini
// ([collection] affinity); firstTab and tabs say where the collection's own tabs
// are inside the shared stash.
void InitStore(const D2RL::PluginContext* ctx, bool affinity, int firstTab, int tabs);
void ShutdownStore();

// Called from the update thread: while the stash is open it asks the game thread
// to sweep the shared stash, at most a few times a second.
void TickStore(bool stashOpen);

// Only the hand moves items in and out (owner 2026-11-20). D2RLoader cannot move an item
// to or from the game's cursor in the shared stash, so each move passes through a free
// inventory spot and one click of the game's own there, which the frame thread makes.

// An empty-handed click on a lit card: the stored copy goes into the inventory and the
// game's click lifts it into the hand. identity is the record id for a unique or set, the
// base item code for a material. A card of an item carried elsewhere does nothing.
void RequestSlotAction(Category category, uint32_t identity);

// A click anywhere on the page with an item in the hand: the game's click puts it into a
// free inventory spot, and from there it goes to its own catalog slot (its own category,
// whichever one the page shows; the page then shows it).
void DepositCarried();

// The inventory block the game's click must land in: x, y its top-left cell of the 10 x 4
// grid, w x h its size. The frame thread clicks the block's middle.
struct InventoryClick {
    int x = 0, y = 0, w = 1, h = 1;
    int grid = 0;   // 0 the inventory; 1 the personal stash's 10 x 10 (a take-out with a full inventory)
};
// Frame thread: a click the game must get now (handed out once). After sending the button's
// down and up, call InventoryClickDone.
bool TakeInventoryClick(InventoryClick& click);
void InventoryClickDone();
// An item is on its way in or out: the frame thread leaves the game's cursor alone.
bool HandBusy();
// A take-out found no room in the inventory within the last 3 s (the card says so).
bool NoRoomRecently();
// The inventory block the item is passing through now (to be drawn empty over the UI image,
// so it does not flash there). False when no item is in an inventory cell on its way.
bool PassingBlock(InventoryClick& block);

// One line for the log: whether affinity is on and where the collection's tabs
// are, then what the last sweep did.
void StoreReport();

// Whether a shared-stash page is one of the collection's own tabs. An entry counts as
// found (its card lit) only while a copy is stored there: that is the copy the page
// can take out (owner 2026-11-20: deposit and withdraw are the page's job).
bool InCollectionBand(uint32_t sharedStashPage);
// A completed runeword's Runes row (the catalog's runeword record id), or UINT32_MAX.
// Game or UI thread only (an item iteration callback).
uint32_t RunewordRecordOf(const D2RL::Items::ItemInfo& item);
// A base item's type and every type above it ("swor blde mele weap"), from the game's tables.
std::string ItemTypeChain(uint32_t code);

// What the player holds on the game's cursor, for the page to draw under the laser:
// its icon cache key ("u248", "t12", "i..."; empty when the hand is empty or the item
// is no catalog collectible). Read on the game thread a few times a second while the
// stash is open (TickStore).
std::string CarriedIconKey();
// The hand's item base code, chars packed little-endian ("gcw " etc.); 0 for an empty hand.
// Read with CarriedIconKey.
uint32_t CarriedCode();

}   // namespace framevr::collection
