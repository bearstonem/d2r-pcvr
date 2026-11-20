#pragma once
// Collection panel: where the category tabs, the slot grid and the page
// controls go, and what the laser hits. Geometry only - no drawing and no game
// calls - so the layout is unit tested (collection_panel_test.cpp) and the
// painter and the pointer share one source of truth.
//
// The canvas is the same 1280 x 1024 the VR Settings panel uses, so the same
// painter and pointer machinery can drive it.
//
// The tab shows one slot per catalog entry: an icon when the item is in the
// shared stash, a greyed outline when it is not. Missing entries take no space
// in the stash; only what was found is stored there.
#include <algorithm>
#include <cstdint>
#include <string>

#include "collection_model.h"

namespace framevr::collection::panel {

inline constexpr float CanvasWidth  = 1280.0f;
inline constexpr float CanvasHeight = 1024.0f;

// How big the slots are, from the footer's Size control: fewer, larger slots
// mean more pages. The grid is centred in what is left between the tab row and
// the footer, so the slot side follows on its own.
enum class Size : uint32_t {
    Small  = 0,   // 4 x 3 = 12 slots a page
    Medium = 1,   // 7 x 5 = 35
    Large  = 2,   // 10 x 7 = 70
    Count  = 3,
};

struct Grid {
    int columns = 12;
    int rows    = 8;
};

inline constexpr Grid GridOf(Size size) {
    // The rows come from the Size control: small = the biggest slots (fewest cells), large = the
    // densest (most cells). The columns are proportional, so the grid keeps the panel's shape.
    switch (size) {
    case Size::Small:  return {4, 3};
    case Size::Medium: return {7, 5};
    case Size::Large:  return {10, 7};
    default:           return {4, 3};
    }
}

inline constexpr int SlotsFor(Size size) {
    const Grid grid = GridOf(size);
    return grid.columns * grid.rows;
}

// The word on the Size control's step.
inline constexpr const char* SizeName(Size size) {
    switch (size) {
    case Size::Small:  return "S";
    case Size::Medium: return "M";
    case Size::Large:  return "L";
    default:           return "?";
    }
}

// The most slots any size shows: the layout's slot array is this long.
inline constexpr int MaxSlots = 10 * 7;

// What the grid shows, from the footer's Show control: everything, or only what
// is still missing, or only what the stash already holds.
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

inline constexpr int MaxTabs       = 4;   // uniques, sets, runewords, room for runes later
inline constexpr int MaxPageButtons = 8;  // more pages than this and the number buttons go away

inline constexpr float Margin     = 24.0f;
inline constexpr float TabWidth   = 210.0f;
inline constexpr float TabHeight  = 52.0f;
inline constexpr float TabGap     = 8.0f;
inline constexpr float PageSize   = 40.0f;
inline constexpr float PageGap    = 6.0f;
inline constexpr float ArrowSize  = 56.0f;
inline constexpr float FooterSize = 52.0f;
inline constexpr float SlotGap    = 6.0f;
// A slot is a vertical rectangle, not a square: an item reads taller than wide.
inline constexpr float SlotAspect = 0.78f;   // width / height

struct Rect {
    float x = 0, y = 0, width = 0, height = 0;
};

inline constexpr bool Inside(const Rect& rect, float x, float y) {
    return x >= rect.x && y >= rect.y && x < rect.x + rect.width && y < rect.y + rect.height;
}

struct Layout {
    Rect tabs[MaxTabs] {};
    int  tabCount = 0;
    Rect pageButtons[MaxPageButtons] {};
    int  pageButtonCount = 0;
    Rect previous {}, next {}, counter {};
    Rect sizeLabel {}, sizeSteps[static_cast<int>(Size::Count)] {};   // the footer's Size control
    Rect showLabel {}, showSteps[static_cast<int>(Show::Count)] {};   // and its Show control
    Rect slots[MaxSlots] {};
    int  slotCount = 0;
    int  columns   = 12;
    int  rows      = 8;
    int  page      = 0;
};

// Pages a category needs for its entries at this slot size, at least one.
inline constexpr int PagesFor(int entries, Size size = Size::Small) {
    const int slots = SlotsFor(size);
    return entries <= 0 ? 1 : (entries + slots - 1) / slots;
}

// Places everything for one page. page is clamped to the available pages.
inline Layout Build(int page, int pages, int tabs, Size size = Size::Small) {
    Layout layout;
    layout.tabCount = std::clamp(tabs, 0, MaxTabs);
    const int pageCount = std::max(1, pages);
    layout.page = std::clamp(page, 0, pageCount - 1);

    for (int i = 0; i < layout.tabCount; ++i) {
        layout.tabs[i] = {Margin + i * (TabWidth + TabGap), Margin, TabWidth, TabHeight};
    }

    // Page numbers, right aligned on the tab row. They are dropped when a
    // category has more pages than fit; the arrows still page through them.
    const float available = CanvasWidth - 2 * Margin - (layout.tabCount * (TabWidth + TabGap));
    const int   fits      = int((available + PageGap) / (PageSize + PageGap));
    layout.pageButtonCount = pageCount <= MaxPageButtons && pageCount <= fits ? pageCount : 0;
    for (int i = 0; i < layout.pageButtonCount; ++i) {
        layout.pageButtons[i] = {CanvasWidth - Margin - (layout.pageButtonCount - i) * PageSize - (layout.pageButtonCount - 1 - i) * PageGap,
                                 Margin + (TabHeight - PageSize) * 0.5f, PageSize, PageSize};
    }

    // The grid takes what is left between the tab row and the footer, centred;
    // the slot side follows the size's column and row counts.
    const Grid  grid       = GridOf(size);
    layout.columns         = grid.columns;
    layout.rows            = grid.rows;
    layout.slotCount       = grid.columns * grid.rows;
    const float areaTop    = Margin + TabHeight + 16.0f;
    const float areaBottom = CanvasHeight - Margin - FooterSize - 16.0f;
    const float areaWidth  = CanvasWidth - 2 * Margin;
    const float areaHeight = areaBottom - areaTop;
    const float cell       = std::min(areaWidth / float(grid.columns), areaHeight / float(grid.rows));
    const float height     = std::max(1.0f, cell - SlotGap);
    const float width      = std::max(1.0f, height * SlotAspect);   // a vertical rectangle
    const float gridWidth  = cell * float(grid.columns);
    const float gridHeight = cell * float(grid.rows);
    const float gridLeft   = (CanvasWidth - gridWidth) * 0.5f;
    const float gridTop    = areaTop + (areaHeight - gridHeight) * 0.5f;
    for (int row = 0; row < grid.rows; ++row) {
        for (int column = 0; column < grid.columns; ++column) {
            const float cellX = gridLeft + column * cell;
            const float cellY = gridTop + row * cell;
            layout.slots[row * grid.columns + column] = {cellX + (cell - width) * 0.5f, cellY + (cell - height) * 0.5f, width, height};
        }
    }

    const float footerY = CanvasHeight - Margin - FooterSize;
    layout.previous = {Margin, footerY, ArrowSize, FooterSize};
    layout.next     = {CanvasWidth - Margin - ArrowSize, footerY, ArrowSize, FooterSize};
    layout.counter  = {CanvasWidth * 0.5f - 240.0f, footerY, 240.0f, FooterSize};
    // The Size control: its label, then one step per size, left of the counter;
    // the Show control the same way, right of it.
    layout.sizeLabel = {layout.previous.x + ArrowSize + 28.0f, footerY, 62.0f, FooterSize};
    constexpr float StepWidth = 44.0f, StepGap = 6.0f;
    for (int i = 0; i < static_cast<int>(Size::Count); ++i) {
        layout.sizeSteps[i] = {layout.sizeLabel.x + layout.sizeLabel.width + i * (StepWidth + StepGap),
                               footerY + 6.0f, StepWidth, FooterSize - 12.0f};
    }
    const float showLeft = layout.counter.x + layout.counter.width + 40.0f;
    layout.showLabel = {showLeft, footerY, 62.0f, FooterSize};
    constexpr float ShowStep = 120.0f;
    for (int i = 0; i < static_cast<int>(Show::Count); ++i) {
        layout.showSteps[i] = {layout.showLabel.x + layout.showLabel.width + i * (ShowStep + StepGap),
                               footerY + 6.0f, ShowStep, FooterSize - 12.0f};
    }
    return layout;
}

enum class HitKind {
    None,
    Tab,
    Page,
    Previous,
    Next,
    Slot,
    Size,
    Show,
};

struct Hit {
    HitKind kind  = HitKind::None;
    int     index = -1;
};

// What the laser at (x, y) points at. Slots win over nothing else because the
// grid does not overlap the bars.
inline Hit Pick(const Layout& layout, float x, float y) {
    for (int i = 0; i < layout.tabCount; ++i)
        if (Inside(layout.tabs[i], x, y)) return {HitKind::Tab, i};
    for (int i = 0; i < layout.pageButtonCount; ++i)
        if (Inside(layout.pageButtons[i], x, y)) return {HitKind::Page, i};
    if (Inside(layout.previous, x, y)) return {HitKind::Previous, 0};
    if (Inside(layout.next, x, y)) return {HitKind::Next, 0};
    for (int i = 0; i < static_cast<int>(Size::Count); ++i)
        if (Inside(layout.sizeSteps[i], x, y)) return {HitKind::Size, i};
    for (int i = 0; i < static_cast<int>(Show::Count); ++i)
        if (Inside(layout.showSteps[i], x, y)) return {HitKind::Show, i};
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
