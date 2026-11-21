// Collection tooltip. See collection_tooltip.h.
#include "collection_tooltip.h"

namespace framevr::collection::tooltip {

PreviewSpec PreviewOf(const Entry& entry) {
    switch (entry.category) {
    case Category::Uniques:   return {PreviewKind::Unique, entry.code, entry.recordId, entry.level};
    case Category::Sets:      return {PreviewKind::Set, entry.code, entry.recordId, entry.level};
    case Category::Materials: return {PreviewKind::Normal, entry.code, 0, entry.level};
    default:                  return {};   // a runeword: no single base code in the catalog
    }
}

std::vector<std::string> SplitLines(std::string_view text) {
    std::vector<std::string> lines;
    std::size_t              at = 0;
    while (at <= text.size()) {
        const std::size_t newline = text.find('\n', at);
        const std::size_t end     = newline == std::string_view::npos ? text.size() : newline;
        std::string       line(text.substr(at, end - at));
        if (!line.empty() && line.back() == '\r') line.pop_back();
        // A sample saved on Windows and a live one split alike: the CR is gone,
        // and trailing blank lines from a final newline do not become an empty row.
        lines.push_back(std::move(line));
        if (newline == std::string_view::npos) break;
        at = newline + 1;
    }
    while (!lines.empty() && lines.back().empty()) lines.pop_back();
    return lines;
}

Tooltip BuildTooltip(const Entry& entry, bool found, std::string_view gameText, std::string_view rangeText) {
    Tooltip tip;
    tip.title    = entry.name;
    tip.subtitle = std::string(CategoryName(entry.category)) + " \xC2\xB7 Level " + std::to_string(entry.level) +
                   (found ? " \xC2\xB7 In the stash" : " \xC2\xB7 Still missing");

    // The catalog's own detail first: a set's name, or the runes a runeword
    // needs. Then whatever the game's own item text says.
    if (!entry.extra.empty()) {
        if (entry.category == Category::Runewords) tip.body.push_back("Runes: " + entry.extra);
        else if (entry.category == Category::Sets)  tip.body.push_back("Set: " + entry.extra);
    }
    for (std::string& line : SplitLines(gameText)) tip.body.push_back(std::move(line));
    // No item text from the loader: the catalog's own stat lines, with their ranges, worded
    // from the game's tables (prepare_collection / collection_stats.py).
    // Only for an item that is in the collection (owner 2026-11-21).
    if (gameText.empty() && found && !entry.stats.empty()) {
        std::size_t at = 0;
        while (at <= entry.stats.size()) {
            const std::size_t bar = entry.stats.find('|', at);
            const std::size_t end = bar == std::string::npos ? entry.stats.size() : bar;
            if (end > at) tip.body.push_back(entry.stats.substr(at, end - at));
            if (bar == std::string::npos) break;
            at = bar + 1;
        }
    }

    if (tip.body.empty() && found) {
        tip.body.push_back(PreviewOf(entry).kind == PreviewKind::Unsupported
                               ? std::string("No preview for this entry.")
                               : std::string("Item text unavailable."));
    }

    // A found item also carries what it could have rolled, so the exact value
    // above can be compared with its range.
    for (std::string& line : SplitLines(rangeText)) tip.ranges.push_back(std::move(line));
    return tip;
}

Box PlaceTooltip(const panel::Rect& anchor, float width, float height) {
    const float maxX = panel::CanvasWidth - TooltipMargin - width;
    const float maxY = panel::CanvasHeight - TooltipMargin - height;

    // Beside the slot it describes: its right, else its left, else clamped.
    float x = anchor.x + anchor.width + TooltipGap;
    if (x > maxX) x = anchor.x - TooltipGap - width;
    if (x < TooltipMargin || x > maxX) x = std::clamp(anchor.x + anchor.width * 0.5f - width * 0.5f,
                                                      TooltipMargin, std::max(TooltipMargin, maxX));

    // Centred on the slot, kept clear of the tab row above and the panel's edges.
    float y = anchor.y + anchor.height * 0.5f - height * 0.5f;
    y = std::clamp(y, TooltipTop, std::max(TooltipTop, maxY));
    return {x, y, width, height};
}

}   // namespace framevr::collection::tooltip
