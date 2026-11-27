#pragma once
// Collection tooltip, plugin side: fetches the game's own item text for the
// pointed catalog entry and keeps it for the painter.
//
// The item service's tooltip calls are UI-thread only, so Request() only records
// what is wanted; Tick() (safe from the render thread) queues the actual read on
// the game's UI thread, and Text() hands the answer to the paint job. A found
// item is read through its live handle (getItemTooltip: its exact rolls); a
// missing one through getItemPreviewTooltip, built from the catalog entry.
//
// Every field is checked before use (SDK 0.4.0): an older loader simply leaves
// the text empty and the card keeps its catalog lines.
#include <cstdint>
#include <string>

#include "collection_view.h"

namespace D2RL {
struct PluginContext;
}

namespace framevr::collection::tooltip {

void InitProvider(const D2RL::PluginContext* ctx);
void ShutdownProvider();

// The scan found this entry's live item: keep its handle for the exact text.
// Called on the UI thread during the scan.
void RememberHandle(Category category, uint32_t recordId, uint64_t handle);
void ForgetHandles();

// Asks for the pointed entry's item text; returns the revision the answer will
// carry. Call only when the pointed entry changed (the transport keeps the key).
uint64_t Request(const Entry& entry);

// The last answer: the exact item text (found) or the catalog preview (missing),
// the found item's possible ranges, and the revision both belong to.
struct Answer {
    std::string text;     // the pointed item's item text, or its catalog preview
    std::string ranges;   // a found item's possible ranges; empty otherwise
    uint64_t    revision = 0;
};
Answer Current();

// The inventory item under the laser while the Collection page covers the stash (the game's own
// tooltips are held back then, and the page draws this one): the item covering cell x, y of the
// 10 x 4 grid. Call only when the pointed cell changed; returns the answer's revision.
uint64_t RequestInventory(int x, int y);
struct InventoryAnswer {
    std::string text;            // the item's own text (getItemTooltip); empty for an empty cell
    int         x = 0, y = 0, w = 0, h = 0;   // the item's block in the grid
    uint32_t    quality = 0;                   // D2RL::Items::Quality
    uint64_t    revision = 0;
};
InventoryAnswer CurrentInventory();

// Queues a pending request on the UI thread. Once a frame is enough.
void Tick();

}   // namespace framevr::collection::tooltip
