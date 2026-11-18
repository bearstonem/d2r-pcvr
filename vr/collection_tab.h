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
#include "collection_panel.h"

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
void            TabSetPage(int page);
void            TabSetSize(panel::Size size);
void            TabSetShow(panel::Show show);

// What the laser is over: the slot or control the picture lights up.
void            TabSetHover(panel::Hit hit);

// Brings the entry with this record id into view: its category, then the page it
// lands on under the current filter. Does nothing when it is not in the catalog.
void            TabShowEntry(Category category, uint32_t recordId);

// Paints the panel once with the loaded catalog and the mod's own icons, and
// writes it beside the plugin as collection-panel.bmp. The panel is a VR layer,
// so this is how it can be looked at on a monitor.
void            TabDumpPanel();

// Whether the panel is open. The stash's Collection tab sets it; the transport
// shows the panel while it is true.
bool TabWanted();
void TabSetWanted(bool wanted);

// Called by the scan: the found flags start empty for the character's stash.
void TabClearFound();
void TabMarkFound(Category category, uint32_t recordId);

// One line for the log: what the catalog holds and how much of it is found.
void TabReport();

}   // namespace framevr::collection
