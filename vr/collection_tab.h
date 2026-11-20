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
//
// The panel opens three ways: the Collection plaque beside the stash's own tab
// pair, F7, and the Collection tab the mod adds to the stash bar itself
// (prepare_collection_tab.py writes that layout from the player's game). The tab
// sends the game's own PanelManager:TogglePanel message, and gamestate.cpp turns
// it into TabSetWanted(true) here.
#include "collection_model.h"
#include "collection_panel.h"

namespace D2RL {
struct PluginContext;
}

namespace framevr::collection {

class Library;
struct ViewState;

// The stash bar's Collection button (prepare_collection_tab adds it): its widget
// name in the layout, and the target/command the mod routes its click to at
// runtime (WidgetService::setWidgetEventMessage in gamestate.cpp), so choosing it
// keeps the stash open and shows the Collection page in place. The layout carries
// no message of its own.
inline constexpr const char* TabWidgetName = "CollectionTab";
inline constexpr const char* TabTarget     = "PCVRCollection";
inline constexpr const char* TabCommand    = "Open";

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
void            TabSetSort(panel::Sort sort);
void            TabToggleDrop(panel::Drop list);   // the footer's Sort / Show list: open or close
void            TabToggleGroup(int group);   // an item-type tile: filter to it, or let go when picked again

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
void TabMarkFoundCode(Category category, uint32_t code);   // a material (gem or rune), by its code
// The opposite: a stored copy was taken out, so its slot goes back to missing.
void TabMarkMissing(Category category, uint32_t recordId);
void TabMarkMissingCode(Category category, uint32_t code);

// One line for the log: what the catalog holds and how much of it is found.
void TabReport();

}   // namespace framevr::collection
