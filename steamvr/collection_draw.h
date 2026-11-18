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
#include <vector>

#include "collection_view.h"

namespace framevr::settings::draw {
class Painter;
}

namespace framevr::collection::draw {

// Decoded icon pixels, straight RGBA. The mod fills these from its own icon
// cache (interaction_icons.h); the preview decodes the PNGs itself.
struct IconPixels {
    int                  width  = 0;
    int                  height = 0;
    std::vector<uint8_t> rgba;
};

// One icon by cache key ("u122" for a unique, "t0" for a set item). Null when
// the icon is not available yet.
using IconLookup = std::function<const IconPixels*(const std::string& key)>;

// Paints one frame. heading is drawn in the top frame (the category name).
void Paint(settings::draw::Painter& painter, const Library& library, const ViewState& view, const IconLookup& icons);

}   // namespace framevr::collection::draw
