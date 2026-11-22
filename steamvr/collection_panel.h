#pragma once
// Collection panel: where the kind buttons, the card grid, the scroll bar, the found bar,
// the item-type tiles and the footer go, and what the laser hits. Geometry only - no
// drawing and no game calls - so the layout is unit tested (collection_panel_test.cpp)
// and the painter and the pointer share one source of truth.
//
// The layout is the owner's accepted mock-up v3 (2026-11-20, render2.py / render3.py),
// which was drawn on the owner's own UI shot of the stash. Its page sat at 113,180 -
// 733,926 of that crop (the shot cropped at 280,50): 620 x 746 UI pixels, exactly the
// stash's content under its tab bar. Every place below is the mock-up's, in its own
// pixels (Mx / My), scaled by one factor to this canvas (746 mock-up px = 1024 here).
#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>

#include "collection_model.h"

namespace framevr::collection::panel {

// The mock-up's page, in its own pixels, and the scale to the canvas.
inline constexpr float MockLeft = 113.0f, MockTop = 180.0f, MockRight = 733.0f, MockBottom = 926.0f;
inline constexpr float Scale        = 1024.0f / (MockBottom - MockTop);
inline constexpr float CanvasHeight = 1024.0f;
inline constexpr float CanvasWidth  = 851.0f;   // 620 * Scale
// Everything on the page ends above the stash's carved recesses (owner 2026-11-22: the tab
// plaques fit on the Collection page too); below it the page is bare stone, the plaques on it.
inline constexpr float ContentBottom = 780.0f;
inline constexpr float Mx(float x) { return (x - MockLeft) * Scale; }
inline constexpr float My(float y) { return (y - MockTop) * Scale; }
inline constexpr float Md(float d) { return d * Scale; }

// How big the cards are, from the footer's Size control.
enum class Size : uint32_t {
    Small  = 0,   // 3 x 2 = 6 cards a page
    Medium = 1,   // 4 x 3 = 12 (the mock-up)
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

inline constexpr int MaxSlots = 6 * 4;

// What the grid shows: the footer's Show list.
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

// The order of the cards: the footer's Sort list (owner: name / required level).
enum class Sort : uint32_t {
    Name  = 0,
    Level = 1,
    Count = 2,
};

inline constexpr const char* SortName(Sort sort) {
    return sort == Sort::Level ? "Level" : "Name";
}

// The footer's lists: which one is open.
enum class Drop : uint32_t { None = 0, Sort = 1, Show = 2 };

// The item types under the grid: two rows of six, each with its share found. A tile
// filters the grid to its type (again: all types). The codes are the game's item types
// (ItemTypes.txt) as collection.tsv carries them; types the mock-up has no tile for
// (maces, staves, wands, daggers, javelins, claws, charms, jewels) show under all types.
inline constexpr int GroupCount = 12;
inline constexpr int NoGroup    = -1;
inline constexpr int BandCount  = 4;    // Sets: level ranges, each holding at most GroupCount sets

inline constexpr const char* GroupName(int group) {
    constexpr const char* names[GroupCount] = {"Helms", "Armor", "Shields", "Gloves", "Boots", "Belts",
                                               "Amulets", "Rings", "Axes", "Swords", "Bows", "Polearms"};
    return group >= 0 && group < GroupCount ? names[group] : "";
}

// Each tile's plain type: its silhouette is taken from it first.
inline constexpr const char* GroupMainType(int group) {
    constexpr const char* types[GroupCount] = {"helm", "tors", "shie", "glov", "boot", "belt",
                                               "amul", "ring", "axe", "swor", "bow", "pole"};
    return group >= 0 && group < GroupCount ? types[group] : "";
}

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

inline constexpr int MaxTabs = 3;   // Uniques, Sets, Runewords

struct Rect {
    float x = 0, y = 0, width = 0, height = 0;
};

inline constexpr bool Inside(const Rect& rect, float x, float y) {
    return x >= rect.x && y >= rect.y && x < rect.x + rect.width && y < rect.y + rect.height;
}

// A rect from the mock-up's own corners.
inline constexpr Rect FromMock(float x0, float y0, float x1, float y1) { return {Mx(x0), My(y0), Md(x1 - x0), Md(y1 - y0)}; }

struct Layout {
    Rect tabs[MaxTabs] {};
    int  tabCount = 0;
    Rect grid {};
    Rect slots[MaxSlots] {};
    int  slotCount = 0;
    int  columns   = 4;
    int  rows      = 3;
    Rect scroll {}, previous {}, next {}, track {};   // the scroll bar, its arrows and the track between
    int  page = 0, pages = 1;
    Rect progress {}, counter {};                     // the found bar and "Uniques  81 / 403" under it
    Rect groups[GroupCount] {};
    Rect bands[4] {};                                 // Sets: the level-range buttons right of the count (AddBands)
    Rect sizeLabel {}, sizeSteps[static_cast<int>(Size::Count)] {};
    Rect sortLabel {}, sortBox {}, sortOptions[static_cast<int>(Sort::Count)] {};
    Rect showLabel {}, showBox {}, showOptions[static_cast<int>(Show::Count)] {};
    Drop open = Drop::None;                           // a list that is open shows its options above it
};

inline constexpr int PagesFor(int entries, Size size = Size::Medium) {
    const int slots = SlotsFor(size);
    return entries <= 0 ? 1 : (entries + slots - 1) / slots;
}

// The scroll bar's thumb (the mock-up's: 70 px tall) at this page of pages.
inline Rect Thumb(const Layout& layout) {
    const float height = std::min(layout.track.height, Md(70.0f));
    const float travel = layout.track.height - height;
    const float at     = layout.pages > 1 ? float(layout.page) / float(layout.pages - 1) : 0.0f;
    return {layout.track.x, layout.track.y + travel * at, layout.track.width, height};
}

inline Layout Build(int page, int pages, int tabs, Size size = Size::Medium, Drop open = Drop::None) {
    Layout layout;
    layout.tabCount = std::clamp(tabs, 0, MaxTabs);
    layout.pages    = std::max(1, pages);
    layout.page     = std::clamp(page, 0, layout.pages - 1);
    layout.open     = open;

    // The kind buttons: 186 x 40, 8 apart, from PX0+28, PY0+24.
    for (int i = 0; i < layout.tabCount; ++i) {
        const float x = 141.0f + float(i) * 194.0f;
        layout.tabs[i] = FromMock(x, 204.0f, x + 186.0f, 244.0f);
    }

    // The cards: gx0 141 .. gx1 681, gy0 258 .. gy1 600, 10 apart.
    constexpr float gx0 = 141.0f, gy0 = 258.0f, gx1 = 681.0f, gy1 = 600.0f, gap = 10.0f;
    layout.grid      = FromMock(gx0, gy0, gx1, gy1);
    const Grid grid  = GridOf(size);
    layout.columns   = grid.columns;
    layout.rows      = grid.rows;
    layout.slotCount = grid.columns * grid.rows;
    const float cw = (gx1 - gx0 - gap * float(grid.columns - 1)) / float(grid.columns);
    const float ch = (gy1 - gy0 - gap * float(grid.rows - 1)) / float(grid.rows);
    for (int row = 0; row < grid.rows; ++row)
        for (int column = 0; column < grid.columns; ++column) {
            const float x0 = gx0 + float(column) * (cw + gap), y0 = gy0 + float(row) * (ch + gap);
            layout.slots[row * grid.columns + column] = FromMock(x0, y0, x0 + cw, y0 + ch);
        }

    // The scroll bar: sx0 689 .. sx1 707 beside the cards, an 18 px arrow at each end.
    layout.scroll   = FromMock(689.0f, gy0, 707.0f, gy1);
    layout.previous = FromMock(690.0f, gy0 + 1.0f, 707.0f, gy0 + 19.0f);
    layout.next     = FromMock(690.0f, gy1 - 19.0f, 707.0f, gy1 - 1.0f);
    layout.track    = FromMock(691.0f, gy0 + 22.0f, 706.0f, gy1 - 22.0f);

    // The found bar under the cards, and the count under it.
    layout.progress = FromMock(gx0, 610.0f, 707.0f, 618.0f);
    layout.counter  = FromMock(gx0, 621.0f, 707.0f, 639.0f);

    // The item types: 6 a row, 6 apart, 38 tall, 4 between the rows, from 644.
    const float cbw = (707.0f - gx0 - 6.0f * 5.0f) / 6.0f;
    for (int i = 0; i < GroupCount; ++i) {
        const float x0 = gx0 + float(i % 6) * (cbw + 6.0f), y0 = 644.0f + float(i / 6) * (38.0f + 4.0f);
        layout.groups[i] = FromMock(x0, y0, x0 + cbw, y0 + 38.0f);
    }

    // Footer at 732: Size and its S / M / L, the Sort list (402..542) and the Show list (600..707).
    constexpr float by = 732.0f;
    layout.sizeLabel = FromMock(gx0, by, gx0 + 52.0f, by + 36.0f);
    for (int i = 0; i < static_cast<int>(Size::Count); ++i) {
        const float x0 = gx0 + 56.0f + float(i) * 44.0f;
        layout.sizeSteps[i] = FromMock(x0, by, x0 + 38.0f, by + 36.0f);
    }
    layout.sortLabel = FromMock(342.0f, by, 398.0f, by + 36.0f);
    layout.sortBox   = FromMock(402.0f, by, 542.0f, by + 36.0f);
    layout.showLabel = FromMock(540.0f, by, 596.0f, by + 36.0f);
    layout.showBox   = FromMock(600.0f, by, 707.0f, by + 36.0f);
    // An open list's options stand above its box, the first at the top.
    if (open == Drop::Sort)
        for (int i = 0; i < static_cast<int>(Sort::Count); ++i) {
            const float y1 = by - 2.0f - float(static_cast<int>(Sort::Count) - 1 - i) * 36.0f;
            layout.sortOptions[i] = FromMock(402.0f, y1 - 36.0f, 542.0f, y1);
        }
    if (open == Drop::Show)
        for (int i = 0; i < static_cast<int>(Show::Count); ++i) {
            const float y1 = by - 2.0f - float(static_cast<int>(Show::Count) - 1 - i) * 36.0f;
            layout.showOptions[i] = FromMock(600.0f, y1 - 36.0f, 707.0f, y1);
        }
    return layout;
}

// Sets: four level-range buttons stand at the right of the count line, which
// then keeps its text to the left of them. Only the Sets page has them.
inline void AddBands(Layout& layout) {
    constexpr float w = 56.0f, gap = 4.0f, x1 = 707.0f;
    for (int i = 0; i < BandCount; ++i) {
        const float x0 = x1 - float(BandCount - i) * w - float(BandCount - 1 - i) * gap;
        layout.bands[i] = FromMock(x0, 620.0f, x0 + w, 641.0f);
    }
}
inline float BandsLeft(const Layout& layout) { return layout.bands[0].width > 0 ? layout.bands[0].x : layout.counter.x + layout.counter.width; }

enum class HitKind {
    None,
    Tab,
    Page,       // the scroll track: index = the page at that height
    Previous,
    Next,
    Slot,
    Size,
    Show,       // an option of the open Show list
    Sort,       // an option of the open Sort list
    Group,
    SortBox,    // the Sort list itself: opens / closes it
    ShowBox,
    Band,       // Sets: a level-range button (index 0..3)
};

struct Hit {
    HitKind kind  = HitKind::None;
    int     index = -1;
};

// What the laser at (x, y) points at. An open list's options lie over the tiles,
// so they are asked first.
inline Hit Pick(const Layout& layout, float x, float y) {
    if (layout.open == Drop::Sort)
        for (int i = 0; i < static_cast<int>(Sort::Count); ++i)
            if (Inside(layout.sortOptions[i], x, y)) return {HitKind::Sort, i};
    if (layout.open == Drop::Show)
        for (int i = 0; i < static_cast<int>(Show::Count); ++i)
            if (Inside(layout.showOptions[i], x, y)) return {HitKind::Show, i};
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
    if (Inside(layout.sortBox, x, y)) return {HitKind::SortBox, 0};
    if (Inside(layout.showBox, x, y)) return {HitKind::ShowBox, 0};
    for (int i = 0; i < BandCount; ++i)
        if (Inside(layout.bands[i], x, y)) return {HitKind::Band, i};
    for (int i = 0; i < GroupCount; ++i)
        if (Inside(layout.groups[i], x, y)) return {HitKind::Group, i};
    for (int i = 0; i < layout.slotCount; ++i)
        if (Inside(layout.slots[i], x, y)) return {HitKind::Slot, i};
    return {};
}

// The icon cache key for a catalog entry (interaction_icons.h ItemKey). A runeword has
// no item of its own until it is made, so it has no icon key; a gem or rune is keyed by
// its four-character code.
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
