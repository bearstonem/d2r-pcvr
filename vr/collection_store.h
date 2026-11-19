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

#include "collection_model.h"

namespace D2RL {
struct PluginContext;
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

// A click on a panel slot: the stored copy goes back to the inventory, or a
// carried copy is stored in the collection's tab. identity is the record id for
// a unique or set, the base item code for a material (the panel's slot identity).
// Called from the update thread; the move itself runs on the game thread.
void RequestSlotAction(Category category, uint32_t identity);

// One line for the log: whether affinity is on and where the collection's tabs
// are, then what the last sweep did.
void StoreReport();

}   // namespace framevr::collection
