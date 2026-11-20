#pragma once
// Collection tooltip: the card under the laser when it rests on a slot.
//
// Pure data and text - no game calls, no drawing, no I/O - so the card's shape
// and where it sits are unit tested (collection_tooltip_test.cpp). The game's
// own item text (getItemTooltip for a live item, getItemPreviewTooltip for a
// catalog entry that is not in storage) is fetched on the UI thread by
// vr/collection_tooltip.cpp and handed in here; this header only decides how
// that text is titled, split and placed.
//
// The catalog carries what a preview needs for a unique, a set or a material:
// the base code, the quality row and the item level. A runeword line names only
// its base type ("axe") and its runes, not one base item code, so a runeword
// preview is left unsupported rather than guessed.
#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "collection_view.h"

namespace framevr::collection::tooltip {

// What Items::ItemPreviewSpec is built from, without naming SDK types here.
enum class PreviewKind {
    Unsupported,   // a runeword: the catalog has no single base item code
    Unique,
    Set,
    Normal,        // a material: a plain base item, no quality row
};

// One catalog entry reduced to the fields a preview needs. The SDK layer
// (vr/collection_tooltip.cpp) turns this into Items::ItemPreviewSpec.
struct PreviewSpec {
    PreviewKind kind    = PreviewKind::Unsupported;
    uint32_t    code    = 0;            // the base item code, as Entry::code
    uint32_t    recordId = 0;           // UniqueItems / SetItems row for Unique / Set
    uint32_t    level   = 0;
};

// The kind of preview this entry supports, and the row it selects.
PreviewSpec PreviewOf(const Entry& entry);

// One line of the game's UTF-8 item text. The service joins its lines with
// '\n'; '\r' is dropped so a saved sample and a live one split alike.
std::vector<std::string> SplitLines(std::string_view text);

// The card: a title, one quiet line under it, the game's own lines, and - for a
// found item - the possible ranges under them, so the roll can be compared with
// what the item could have rolled.
struct Tooltip {
    std::string              title;
    std::string              subtitle;
    std::vector<std::string> body;
    std::vector<std::string> ranges;   // a found item's possible ranges; empty otherwise
    bool Empty() const { return title.empty() && body.empty() && ranges.empty(); }
};

// Builds the card for an entry. gameText is the item text the UI thread fetched
// (empty until it arrives, or when the provider is unavailable); rangeText is the
// same item's possible ranges (only a found item has both). extra is carried into
// the subtitle/body for the catalog's own detail (a set's name, a runeword's runes).
Tooltip BuildTooltip(const Entry& entry, bool found, std::string_view gameText, std::string_view rangeText = {});

// A tooltip rectangle in canvas space, placed beside the slot it belongs to and
// kept inside the canvas. Prefers the right of the slot, then the left; drops
// below or above to keep the whole card on the panel.
struct Box {
    float x = 0, y = 0, w = 0, h = 0;
};

inline constexpr float TooltipMargin = 18.0f;
inline constexpr float TooltipGap    = 14.0f;
// The card starts below the tab row, so a top-row slot never covers the tabs.
inline constexpr float TooltipTop    = panel::My(258.0f);   // the cards' top (the mock-up's gy0)

Box PlaceTooltip(const panel::Rect& anchor, float width, float height);

}   // namespace framevr::collection::tooltip
