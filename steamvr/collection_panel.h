#pragma once
// Collection panel: where the category buttons, the card grid, the scroll bar,
// the item-type tiles and the footer controls go, and what the laser hits.
// Geometry only - no drawing and no game calls - so the layout is unit tested
// (collection_panel_test.cpp) and the painter and the pointer share one source
// of truth.
//
// The look is the owner's accepted preview v3 (2026-11-20): a page of the D2R
// stash in grey stone - category buttons with counters on top, a progress bar,
// cards with a name plate, a scroll bar on the right, the item types with their
// share found below the grid, and Size / Sort / Show at the bottom.
//
// The canvas is the page's own shape: the page is drawn into the stash's own
// picture over its grid (u .135-.495, v .105-.455 of the 2244 x 2352 UI image =
// 808 x 823 px), so 1004 x 1024 keeps its proportions. The painter's bitmap is
// wider (1280); only the left CanvasWidth columns are the page.
#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>

#include "collection_model.h"

namespace framevr::collection::panel {

inline constexpr float CanvasWidth  = 1004.0f;
inline constexpr float CanvasHeight = 1024.0f;

// How big the cards are, from the footer's Size control: fewer, larger cards
// mean more pages.
enum class Size : uint32_t {
    Small  = 0,   // 3 x 2 = 6 cards a page
    Medium = 1,   // 4 x 3 = 12
    Large  = 2,   // 6 x 4 = 24
    Count  = 3,
};

struct Grid {
    int columns = 4;
    int rows    = 3;
};

inline constexpr Grid GridOf(Size size) {
    switch (size) {
    case Size::Small:  return {3, 2};
    case Size::Medium: return {4, 3};
    case Size::Large:  return {6, 4};
    default:           return {4, 3};
    }
}

inline constexpr int SlotsFor(Size size) {
    const Grid grid = GridOf(size);
    return grid.columns * grid.rows;
}

inline constexpr const char* SizeName(Size size) {
    switch (size) {
    case Size::Small:  return "S";
    case Size::Medium: return "M";
    case Size::Large:  return "L";
    default:           return "?";
    }
}

// The most cards any size shows: the layout's slot array is this long.
inline constexpr int MaxSlots = 6 * 4;

// What the grid shows, from the footer's Show control.
enum class Show : uint32_t {
    All     = 0,
    Missing = 1,
    Found   = 2,
    Count   = 3,
};

inline constexpr const char* ShowName(Show show) {
    switch (show) {
    case Show::Missing: return "Missing";
    case Show::Found:   return "Found";
    default:            return "All";
    }
}

// The order of the cards, from the footer's Sort control (owner: Name / Level).
enum class Sort : uint32_t {
    Name  = 0,
    Level = 1,
    Count = 2,
};

inline constexpr const char* SortName(Sort sort) {
    return sort == Sort::Level ? "Level" : "Name";
}

// The item types under the grid: two rows of six, each with its share found.
// A tile filters the grid to its type (again: all types). The codes are the
// game's item types (ItemTypes.txt) as collection.tsv carries them; types the
// owner's preview has no tile for (maces, staves, wands, daggers, javelins,
// claws, charms, jewels) show only under "all types".
inline constexpr int GroupCount = 12;
inline constexpr int NoGroup    = -1;

inline constexpr const char* GroupName(int group) {
    constexpr const char* names[GroupCount] = {"Helms", "Armor", "Shields", "Gloves", "Boots", "Belts",
                                               "Amulets", "Rings", "Axes", "Swords", "Bows", "Polearms"};
    return group >= 0 && group < GroupCount ? names[group] : "";
}

// Each tile's own plain type (a plain helm, not a circlet): its silhouette is taken from it first.
inline constexpr const char* GroupMainType(int group) {
    constexpr const char* types[GroupCount] = {"helm", "tors", "shie", "glov", "boot", "belt",
                                               "amul", "ring", "axe", "swor", "bow", "pole"};
    return group >= 0 && group < GroupCount ? types[group] : "";
}

// The tile an item type belongs to, or NoGroup.
inline int GroupOfType(std::string_view type) {
    struct Pair { const char* type; int group; };
    constexpr Pair pairs[] = {
        {"helm", 0}, {"phlm", 0}, {"pelt", 0}, {"circ", 0},
        {"tors", 1},
        {"shie", 2}, {"ashd", 2}, {"head", 2}, {"shld", 2},
        {"glov", 3}, {"boot", 4}, {"belt", 5}, {"amul", 6}, {"ring", 7},
        {"axe", 8},  {"taxe", 8},
        {"swor", 9},
        {"bow", 10}, {"abow", 10}, {"xbow", 10},
        {"pole", 11}, {"spea", 11}, {"aspe", 11},
    };
    for (const Pair& pair : pairs)
        if (type == pair.type) return pair.group;
    return NoGroup;
}

inline constexpr int MaxTabs = 3;   // Uniques, Sets, Runewords (Materials keeps no tab)

inline constexpr float Margin      = 16.0f;
inline constexpr float TabHeight   = 60.0f;
inline constexpr float TabWidth    = 236.0f;
inline constexpr float TabGap      = 10.0f;
inline constexpr float ScrollWidth = 34.0f;
inline constexpr float CardGap     = 10.0f;

struct Rect {
    float x = 0, y = 0, width = 0, height = 0;
};

inline constexpr bool Inside(const Rect& rect, float x, float y) {
    return x >= rect.x && y >= rect.y && x < rect.x + rect.width && y < rect.y + rect.height;
}

struct Layout {
    Rect tabs[MaxTabs] {};
    int  tabCount = 0;
    Rect progress {}, counter {};                 // the found bar and "Uniques 8 / 403"
    Rect grid {};                                 // the card field
    Rect slots[MaxSlots] {};
    int  slotCount = 0;
    int  columns   = 4;
    int  rows      = 3;
    Rect previous {}, next {}, track {};          // the scroll bar: its arrows and the track between
    int  page = 0, pages = 1;
    Rect groups[GroupCount] {};
    Rect sizeLabel {}, sizeSteps[static_cast<int>(Size::Count)] {};
    Rect sortLabel {}, sortSteps[static_cast<int>(Sort::Count)] {};
    Rect showLabel {}, showSteps[static_cast<int>(Show::Count)] {};
};

// Pages a category needs for its entries at this card size, at least one.
inline constexpr int PagesFor(int entries, Size size = Size::Medium) {
    const int slots = SlotsFor(size);
    return entries <= 0 ? 1 : (entries + slots - 1) / slots;
}

// The scroll bar's thumb for this page of pages, inside the track.
inline Rect Thumb(const Layout& layout) {
    const float height = std::max(28.0f, layout.track.height / float(std::max(1, layout.pages)));
    const float travel = layout.track.height - height;
    const float at     = layout.pages > 1 ? float(layout.page) / float(layout.pages - 1) : 0.0f;
    return {layout.track.x, layout.track.y + travel * at, layout.track.width, height};
}

// Places everything for one page. page is clamped to the available pages.
inline Layout Build(int page, int pages, int tabs, Size size = Size::Medium) {
    Layout layout;
    layout.tabCount = std::clamp(tabs, 0, MaxTabs);
    layout.pages    = std::max(1, pages);
    layout.page     = std::clamp(page, 0, layout.pages - 1);
    const float right = CanvasWidth - Margin;

    for (int i = 0; i < layout.tabCount; ++i)
        layout.tabs[i] = {Margin + i * (TabWidth + TabGap), Margin, TabWidth, TabHeight};

    const float progressY = Margin + TabHeight + 14.0f;
    layout.progress = {Margin, progressY + 9.0f, 560.0f, 14.0f};
    layout.counter  = {Margin + 580.0f, progressY, right - (Margin + 580.0f), 32.0f};

    // The card field, the scroll bar beside it.
    const float gridTop    = progressY + 32.0f + 12.0f;
    const float gridBottom = 690.0f;
    layout.grid = {Margin, gridTop, right - Margin - ScrollWidth - 10.0f, gridBottom - gridTop};
    const float scrollX = right - ScrollWidth;
    layout.previous = {scrollX, gridTop, ScrollWidth, ScrollWidth};
    layout.next     = {scrollX, gridBottom - ScrollWidth, ScrollWidth, ScrollWidth};
    layout.track    = {scrollX, gridTop + ScrollWidth + 4.0f, ScrollWidth, gridBottom - gridTop - 2 * ScrollWidth - 8.0f};

    const Grid grid  = GridOf(size);
    layout.columns   = grid.columns;
    layout.rows      = grid.rows;
    layout.slotCount = grid.columns * grid.rows;
    const float cellW = layout.grid.width / float(grid.columns);
    const float cellH = layout.grid.height / float(grid.rows);
    for (int row = 0; row < grid.rows; ++row)
        for (int column = 0; column < grid.columns; ++column)
            layout.slots[row * grid.columns + column] = {layout.grid.x + column * cellW + CardGap * 0.5f,
                                                         layout.grid.y + row * cellH + CardGap * 0.5f,
                                                         cellW - CardGap, cellH - CardGap};

    // Footer: Size, Sort, Show - a label, then one stone step per choice.
    const float footerY = CanvasHeight - Margin - 52.0f;

    // The item types: two rows of six filling what is left between the grid and the footer.
    constexpr float GroupGap = 8.0f;
    const float groupTop    = gridBottom + 12.0f;
    const float groupHeight = (footerY - 12.0f - groupTop - GroupGap) * 0.5f;
    const float groupW      = (right - Margin - 5 * GroupGap) / 6.0f;
    for (int i = 0; i < GroupCount; ++i)
        layout.groups[i] = {Margin + (i % 6) * (groupW + GroupGap), groupTop + (i / 6) * (groupHeight + GroupGap), groupW, groupHeight};
    constexpr float StepH = 44.0f, LabelW = 62.0f, StepGap = 6.0f, Between = 26.0f;
    float x = Margin;
    layout.sizeLabel = {x, footerY, LabelW, 52.0f};
    x += LabelW + 8.0f;
    for (int i = 0; i < static_cast<int>(Size::Count); ++i, x += 46.0f + StepGap)
        layout.sizeSteps[i] = {x, footerY + 4.0f, 46.0f, StepH};
    x += Between;
    layout.sortLabel = {x, footerY, LabelW, 52.0f};
    x += LabelW + 8.0f;
    for (int i = 0; i < static_cast<int>(Sort::Count); ++i, x += 96.0f + StepGap)
        layout.sortSteps[i] = {x, footerY + 4.0f, 96.0f, StepH};
    x += Between;
    layout.showLabel = {x, footerY, LabelW + 6.0f, 52.0f};
    x += LabelW + 14.0f;
    const float showW = (right - x - 2 * StepGap) / 3.0f;
    for (int i = 0; i < static_cast<int>(Show::Count); ++i, x += showW + StepGap)
        layout.showSteps[i] = {x, footerY + 4.0f, showW, StepH};
    return layout;
}

enum class HitKind {
    None,
    Tab,
    Page,       // the scroll track: index = the page at that height
    Previous,
    Next,
    Slot,
    Size,
    Show,
    Sort,
    Group,
};

struct Hit {
    HitKind kind  = HitKind::None;
    int     index = -1;
};

// What the laser at (x, y) points at.
inline Hit Pick(const Layout& layout, float x, float y) {
    for (int i = 0; i < layout.tabCount; ++i)
        if (Inside(layout.tabs[i], x, y)) return {HitKind::Tab, i};
    if (Inside(layout.previous, x, y)) return {HitKind::Previous, 0};
    if (Inside(layout.next, x, y)) return {HitKind::Next, 0};
    if (Inside(layout.track, x, y)) {
        const float share = (y - layout.track.y) / std::max(1.0f, layout.track.height);
        return {HitKind::Page, std::clamp(int(share * float(layout.pages)), 0, layout.pages - 1)};
    }
    for (int i = 0; i < static_cast<int>(Size::Count); ++i)
        if (Inside(layout.sizeSteps[i], x, y)) return {HitKind::Size, i};
    for (int i = 0; i < static_cast<int>(Sort::Count); ++i)
        if (Inside(layout.sortSteps[i], x, y)) return {HitKind::Sort, i};
    for (int i = 0; i < static_cast<int>(Show::Count); ++i)
        if (Inside(layout.showSteps[i], x, y)) return {HitKind::Show, i};
    for (int i = 0; i < GroupCount; ++i)
        if (Inside(layout.groups[i], x, y)) return {HitKind::Group, i};
    for (int i = 0; i < layout.slotCount; ++i)
        if (Inside(layout.slots[i], x, y)) return {HitKind::Slot, i};
    return {};
}

// The icon cache key for a catalog entry: the mod already extracts exactly
// these (interaction_icons.h ItemKey). A runeword has no item of its own until
// it is made, so it has no icon key; a gem or rune is a base item, keyed by its
// four-character code.
inline std::string IconKey(Category category, uint32_t recordId, uint32_t code) {
    if (category == Category::Uniques) return "u" + std::to_string(recordId);
    if (category == Category::Sets) return "t" + std::to_string(recordId);
    if (category == Category::Materials) {
        constexpr char hex[] = "0123456789abcdef";
        std::string    key   = "i00000000";
        for (int i = 0; i < 8; ++i) key[8 - i] = hex[(code >> (i * 4)) & 15];
        return key;
    }
    return {};
}

}   // namespace framevr::collection::panel
