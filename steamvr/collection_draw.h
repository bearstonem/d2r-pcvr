#pragma once
// Collection panel painting: the tab row, the slot grid and the footer, drawn
// through the VR Settings painter (vr_settings_draw.h) so the panel matches the
// rest of the mod's interface.
//
// One slot per catalog entry: the item's own icon when it is in the shared
// stash, a dim dashed outline when it is still missing. Missing entries take no
// room in the stash - only what was found is stored there.
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "collection_view.h"

namespace framevr::settings::draw {
class Painter;
}

namespace framevr::collection::draw {

// Decoded icon pixels, straight RGBA: a view, not an owner, so the caller keeps
// the buffer alive for the paint. The mod points it at its icon cache
// (interaction_icons.h); the preview at its own decoded copy.
struct IconPixels {
    const uint8_t* rgba   = nullptr;
    int            width  = 0;
    int            height = 0;
};

// One icon by cache key ("u122" for a unique, "t0" for a set item). Null when
// the icon is not available yet.
using IconLookup = std::function<const IconPixels*(const std::string& key)>;

// Paints one frame. heading is drawn in the top frame (the category name).
// gameText is the game's own item text for the pointed slot (empty when it has
// not been fetched yet); rangeText is the same item's possible ranges, shown
// under it so the roll can be compared. The card is drawn only while a slot is
// pointed.
// The item in the player's hand while the laser is on the page: the game's own cursor
// (with the item on it) is kept under the page there, so the page draws the item itself,
// at the laser, from the icon cache. x, y are canvas pixels; empty key: nothing in hand.
// The inventory item under the laser while the page covers the stash: the game's own tooltips
// are held back then ([collection] game_tooltips=0), so the page draws this one at its right
// edge, beside the item's row. text: the game's item text (first line its name); x, y, w, h:
// the item's block in canvas pixels (right of the page); nameRgb: its quality's colour.
struct InventoryTip {
    std::string text;
    float       x = 0, y = 0, w = 0, h = 0;
    uint32_t    nameRgb = 0xFFFFFF;
};
struct Carried {
    std::string key;
    float       x = -1, y = -1;
};

void Paint(settings::draw::Painter& painter, const Library& library, const ViewState& view, const IconLookup& icons,
           std::string_view gameText = {}, std::string_view rangeText = {}, const Carried& carried = {},
           const struct InventoryTip& inventory = InventoryTip {});

}   // namespace framevr::collection::draw
