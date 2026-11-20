// Collection tooltip: the preview spec, the game text split and the card's
// placement. Pure data, so this runs without D2R.
#include "collection_tooltip.h"

#include <cassert>
#include <string>
#include <vector>

using namespace framevr::collection;
using namespace framevr::collection::tooltip;

namespace {

Entry Make(Category category, uint32_t code, uint32_t recordId, uint32_t level, const char* name,
           const char* type, const char* extra = "") {
    Entry entry;
    entry.category = category;
    entry.code     = code;
    entry.recordId = recordId;
    entry.level    = level;
    entry.name     = name;
    entry.type     = type;
    entry.extra    = extra;
    return entry;
}

bool Contains(const std::string& haystack, const char* needle) {
    return haystack.find(needle) != std::string::npos;
}

}   // namespace

int main() {
    // Only a unique, a set and a material can be previewed from the catalog: each
    // names one base code and one quality row. A runeword names its base type and
    // runes, not one code, so it is left unsupported rather than guessed.
    const Entry unique = Make(Category::Uniques, Library::CodeOf("am7"), 282, 50, "Lycander's Aim", "abow");
    const Entry set    = Make(Category::Sets, Library::CodeOf("lrg"), 56, 3, "Civerb's Ward", "shie", "Civerb's Vestments");
    const Entry rune   = Make(Category::Runewords, 0, 3, 0, "Beast", "axe", "r30 r03 r22 r23 r17");
    const Entry gem    = Make(Category::Materials, Library::CodeOf("gsv"), 0, 0, "Amethyst", "gema");

    const PreviewSpec up = PreviewOf(unique);
    assert(up.kind == PreviewKind::Unique && up.code == unique.code && up.recordId == 282 && up.level == 50);
    const PreviewSpec sp = PreviewOf(set);
    assert(sp.kind == PreviewKind::Set && sp.code == set.code && sp.recordId == 56 && sp.level == 3);
    const PreviewSpec gp = PreviewOf(gem);
    assert(gp.kind == PreviewKind::Normal && gp.code == gem.code && gp.recordId == 0);
    assert(PreviewOf(rune).kind == PreviewKind::Unsupported);

    // The game's text splits on LF, drops the CR of a Windows sample, and does not
    // turn a final newline into an empty row.
    const auto lines = SplitLines("Damage: 20-40\r\n+2 to Skills\n\nRequired Level: 50\n");
    assert(lines.size() == 4);
    assert(lines[0] == "Damage: 20-40");
    assert(lines[1] == "+2 to Skills");
    assert(lines[2].empty());
    assert(lines[3] == "Required Level: 50");
    assert(SplitLines("").empty());
    assert(SplitLines("\r\n\r\n").empty());

    // A found item says so; a missing one says it is still missing.
    const Tooltip found = BuildTooltip(unique, true, "Damage: 20-40\n+2 to Skills");
    assert(found.title == "Lycander's Aim");
    assert(Contains(found.subtitle, "Uniques"));
    assert(Contains(found.subtitle, "Level 50"));
    assert(Contains(found.subtitle, "In the stash"));
    assert(found.body.size() == 2 && found.body[0] == "Damage: 20-40" && found.body[1] == "+2 to Skills");
    assert(Contains(BuildTooltip(unique, false, "").subtitle, "Still missing"));

    // A found item carries its possible ranges under the exact text, so the roll
    // can be compared; a missing one has no ranges of its own.
    const Tooltip rolled = BuildTooltip(unique, true, "20% Increased Attack Speed\n+20 to Dexterity",
                                        "15-20% Increased Attack Speed\n+15-20 to Dexterity");
    assert(rolled.ranges.size() == 2 && rolled.ranges[0] == "15-20% Increased Attack Speed");
    assert(rolled.body.size() == 2 && rolled.body[0] == "20% Increased Attack Speed");
    assert(BuildTooltip(unique, false, "x").ranges.empty());

    // The catalog's own detail leads the body: a set's name, a runeword's runes.
    const Tooltip setTip = BuildTooltip(set, false, "");
    assert(!setTip.body.empty() && setTip.body[0] == "Set: Civerb's Vestments");
    const Tooltip runeTip = BuildTooltip(rune, false, "");
    assert(!runeTip.body.empty() && runeTip.body[0] == "Runes: r30 r03 r22 r23 r17");

    // No text and no catalog detail still leaves one honest line.
    const Tooltip empty = BuildTooltip(gem, false, "");
    assert(empty.body.size() == 1 && empty.body[0] == "Item text unavailable.");
    assert(!empty.Empty());

    // Beside the slot it describes: to the right when it fits...
    const panel::Rect middle{100, 100, 50, 50};
    const Box right = PlaceTooltip(middle, 300, 200);
    assert(right.x == 164 && right.y == TooltipTop);   // below the tab row, not over it

    // ...to the left when the right would leave the panel...
    const panel::Rect nearRight{1200, 100, 50, 50};
    const Box left = PlaceTooltip(nearRight, 300, 200);
    assert(left.x == 886 && left.y == TooltipTop);

    // ...and kept clear of the top and bottom edges.
    const Box low = PlaceTooltip(panel::Rect{100, 1000, 50, 50}, 300, 200);
    assert(low.y == panel::CanvasHeight - TooltipMargin - 200);

    // The whole card stays inside the canvas whatever the slot.
    for (int slot = 0; slot < panel::SlotsFor(panel::Size::Small); ++slot) {
        const panel::Layout layout = panel::Build(0, 1, int(TabCategoryCount), panel::Size::Small);
        const Box box = PlaceTooltip(layout.slots[slot], 360, 240);
        assert(box.x >= TooltipMargin && box.x + box.w <= panel::CanvasWidth - TooltipMargin + 0.001f);
        assert(box.y >= TooltipTop && box.y + box.h <= panel::CanvasHeight - TooltipMargin + 0.001f);
    }

    return 0;
}
