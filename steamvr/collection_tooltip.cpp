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

// The set bonus lines of a piece (Entry::bonus), each with what it asks: "i3" / "s3" three pieces,
// "sF" the whole set (count 0 then).
struct BonusLine { bool set; int pieces; std::string text; };
static std::vector<BonusLine> BonusLines(const std::string& bonus) {
    std::vector<BonusLine> out;
    std::size_t at = 0;
    while (at < bonus.size()) {
        const std::size_t bar = bonus.find('|', at);
        const std::size_t end = bar == std::string::npos ? bonus.size() : bar;
        const std::string part = bonus.substr(at, end - at);
        const std::size_t colon = part.find(':');
        if (colon == 2 && (part[0] == 'i' || part[0] == 's'))
            out.push_back({part[0] == 's', part[1] == 'F' ? 0 : part[1] - '0', part.substr(3)});
        if (bar == std::string::npos) break;
        at = bar + 1;
    }
    return out;
}

Tooltip SetTooltip(const std::string& setName, const std::string& bonus, int pieces, int total) {
    Tooltip tip;
    tip.title    = setName;
    tip.subtitle = "Set \xC2\xB7 " + std::to_string(pieces) + " / " + std::to_string(total) + " pieces in the stash";
    // the whole set's bonus only, gold as the game shows it (owner 2026-11-22)
    for (const BonusLine& line : BonusLines(bonus))
        if (line.set && line.pieces == 0) tip.body.push_back(std::string(1, kFullSetMark) + line.text);
    if (tip.body.empty()) tip.body.push_back("No set bonuses.");
    return tip;
}

Tooltip BuildTooltip(const Entry& entry, bool found, std::string_view gameText, std::string_view rangeText,
                     int setPieces, int setTotal) {
    Tooltip tip;
    tip.title    = entry.name;
    tip.subtitle = std::string(CategoryName(entry.category)) + " \xC2\xB7 Level " + std::to_string(entry.level) +
                   (found ? " \xC2\xB7 In the stash" : " \xC2\xB7 Still missing");

    // The catalog's own detail first: a set's name, or the runes a runeword
    // needs. Then whatever the game's own item text says.
    if (!entry.extra.empty()) {
        // a made runeword's runes in the runes' own colour (RunesMark: DrawTooltip)
        if (entry.category == Category::Runewords)
            tip.body.push_back((found ? std::string(1, kRunesMark) : std::string()) + "Runes: " + RuneNames(entry.extra));
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

    // A set piece: the set's bonuses it would bring with the pieces the collection holds, green as
    // the game shows them (owner 2026-11-22): its own with 2, 3 ... pieces, the set's, the whole set's.
    if (entry.category == Category::Sets && setPieces > 0)
        // the piece's own lines only (owner 2026-11-22: as Sigon's boots gain them, piece by piece)
        for (const BonusLine& line : BonusLines(entry.bonus))
            if (!line.set && line.pieces <= setPieces) tip.body.push_back(std::string(1, kSetBonusMark) + line.text);

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
