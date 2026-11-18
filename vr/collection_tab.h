#pragma once
// Collection tabs, step 2: the plugin side.
//
// Loads the catalog (collection.tsv beside the plugin, written by
// prepare_collection.py) and keeps which entries the loaded character's shared
// stash already holds. The panel painter reads this; the storage layer will
// deposit into and withdraw from the same shared stash.
//
// The catalog is the list; the stash only holds what was found, so a missing
// entry costs no room.
#include "collection_model.h"

namespace D2RL {
struct PluginContext;
}

namespace framevr::collection {

class Library;
struct ViewState;

// Reads the catalog beside the plugin and stores the context. Safe when the
// file is missing: the tab stays empty and the log says so.
void InitTab(const D2RL::PluginContext* ctx);
void ShutdownTab();

// The loaded catalog and the open tab. Read from the frame thread.
const Library& TabLibrary();
const ViewState& TabView();
void            TabSelectCategory(Category category);
void            TabStep(int pages);

// Called by the scan: the found flags start empty for the character's stash.
void TabClearFound();
void TabMarkFound(Category category, uint32_t recordId);

// One line for the log: what the catalog holds and how much of it is found.
void TabReport();

}   // namespace framevr::collection
