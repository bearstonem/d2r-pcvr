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

inline constexpr int Columns      = 12;
inline constexpr int Rows         = 8;
inline constexpr int SlotsPerPage = Columns * Rows;

inline constexpr int MaxTabs       = 4;   // uniques, sets, runewords, room for runes later
inline constexpr int MaxPageButtons = 8;  // more pages than this and the number buttons go away

inline constexpr float Margin     = 24.0f;
inline constexpr float TabWidth   = 176.0f;
inline constexpr float TabHeight  = 52.0f;
inline constexpr float TabGap     = 8.0f;
inline constexpr float PageSize   = 40.0f;
inline constexpr float PageGap    = 6.0f;
inline constexpr float ArrowSize  = 56.0f;
inline constexpr float FooterSize = 52.0f;
inline constexpr float SlotGap    = 6.0f;

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
    Rect slots[SlotsPerPage] {};
    int  page = 0;
};

// Pages a category needs for its entries, at least one.
inline constexpr int PagesFor(int entries) {
    return entries <= 0 ? 1 : (entries + SlotsPerPage - 1) / SlotsPerPage;
}

// Places everything for one page. page is clamped to the available pages.
inline Layout Build(int page, int pages, int tabs) {
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

    // The grid takes what is left between the tab row and the footer, centred.
    const float areaTop    = Margin + TabHeight + 16.0f;
    const float areaBottom = CanvasHeight - Margin - FooterSize - 16.0f;
    const float areaWidth  = CanvasWidth - 2 * Margin;
    const float areaHeight = areaBottom - areaTop;
    const float cell       = std::min(areaWidth / Columns, areaHeight / Rows);
    const float side       = std::max(1.0f, cell - SlotGap);
    const float gridWidth  = cell * Columns;
    const float gridHeight = cell * Rows;
    const float gridLeft   = (CanvasWidth - gridWidth) * 0.5f;
    const float gridTop    = areaTop + (areaHeight - gridHeight) * 0.5f;
    for (int row = 0; row < Rows; ++row) {
        for (int column = 0; column < Columns; ++column) {
            const float cellX = gridLeft + column * cell;
            const float cellY = gridTop + row * cell;
            layout.slots[row * Columns + column] = {cellX + (cell - side) * 0.5f, cellY + (cell - side) * 0.5f, side, side};
        }
    }

    const float footerY = CanvasHeight - Margin - FooterSize;
    layout.previous = {Margin, footerY, ArrowSize, FooterSize};
    layout.next     = {CanvasWidth - Margin - ArrowSize, footerY, ArrowSize, FooterSize};
    layout.counter  = {CanvasWidth * 0.5f - 160.0f, footerY, 320.0f, FooterSize};
    return layout;
}

enum class HitKind {
    None,
    Tab,
    Page,
    Previous,
    Next,
    Slot,
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
    for (int i = 0; i < SlotsPerPage; ++i)
        if (Inside(layout.slots[i], x, y)) return {HitKind::Slot, i};
    return {};
}

// The icon cache key for a catalog entry: the mod already extracts exactly
// these (interaction_icons.h ItemKey). A runeword has no item of its own until
// it is made, so it has no icon key.
inline std::string IconKey(Category category, uint32_t recordId) {
    if (category == Category::Uniques) return "u" + std::to_string(recordId);
    if (category == Category::Sets) return "t" + std::to_string(recordId);
    return {};
}

}   // namespace framevr::collection::panel
