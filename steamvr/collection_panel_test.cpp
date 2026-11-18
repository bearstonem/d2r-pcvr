// Collection panel: the tab row, the slot grid and the page controls - where
// they land and what the laser hits. Pure geometry, so this runs without D2R.
#include "collection_panel.h"
#include <cassert>
#include <cstdio>

using namespace framevr::collection;
using namespace framevr::collection::panel;

namespace {

float CentreX(const Rect& rect) { return rect.x + rect.width * 0.5f; }
float CentreY(const Rect& rect) { return rect.y + rect.height * 0.5f; }

bool Overlaps(const Rect& a, const Rect& b) {
    return a.x < b.x + b.width && b.x < a.x + a.width && a.y < b.y + b.height && b.y < a.y + a.height;
}

}   // namespace

int main() {
    // Pages hold SlotsPerPage entries each, and never fewer than one page.
    static_assert(SlotsPerPage == 96);
    static_assert(PagesFor(0) == 1);
    static_assert(PagesFor(1) == 1);
    static_assert(PagesFor(96) == 1);
    static_assert(PagesFor(97) == 2);
    static_assert(PagesFor(403) == 5);   // uniques on 3.4.0

    const Layout layout = Build(0, 2, 3);

    // Tabs sit on the tab row, left to right, without touching each other.
    assert(layout.tabCount == 3);
    for (int i = 0; i < layout.tabCount; ++i) {
        const Rect& tab = layout.tabs[i];
        assert(tab.width > 0 && tab.height > 0);
        assert(tab.x >= Margin && tab.x + tab.width <= CanvasWidth);
        assert(tab.y >= 0 && tab.y + tab.height < CanvasHeight);
        if (i > 0) assert(!Overlaps(layout.tabs[i - 1], tab));
    }

    // The grid: every slot inside the canvas, none overlapping, all the same size.
    for (int i = 0; i < SlotsPerPage; ++i) {
        const Rect& slot = layout.slots[i];
        assert(slot.width > 0 && slot.height > 0);
        assert(slot.x >= 0 && slot.x + slot.width <= CanvasWidth);
        assert(slot.y >= 0 && slot.y + slot.height <= CanvasHeight);
        assert(slot.y >= layout.tabs[0].y + layout.tabs[0].height);   // below the tabs
        assert(slot.y + slot.height <= layout.previous.y);            // above the footer
        if (i > 0) assert(layout.slots[i].width == layout.slots[0].width);
        for (int j = 0; j < i; ++j) assert(!Overlaps(layout.slots[j], slot));
    }
    // The first slot is top left of the grid and the last is bottom right.
    assert(layout.slots[0].x < layout.slots[Columns - 1].x);
    assert(layout.slots[0].y < layout.slots[(Rows - 1) * Columns].y);

    // Two pages of a three-tab category: two page numbers, and the arrows clear
    // of the numbers and the grid.
    assert(layout.pageButtonCount == 2);
    for (int i = 0; i < layout.pageButtonCount; ++i) {
        const Rect& button = layout.pageButtons[i];
        assert(button.x >= 0 && button.x + button.width <= CanvasWidth);
        assert(!Overlaps(button, layout.tabs[layout.tabCount - 1]));
        if (i > 0) assert(!Overlaps(layout.pageButtons[i - 1], button));
    }
    assert(layout.previous.x == Margin);
    assert(layout.next.x + layout.next.width == CanvasWidth - Margin);
    assert(!Overlaps(layout.previous, layout.next));

    // Pointing: each control takes the point at its centre and nothing else.
    assert(Pick(layout, CentreX(layout.tabs[1]), CentreY(layout.tabs[1])).kind == HitKind::Tab);
    assert(Pick(layout, CentreX(layout.tabs[1]), CentreY(layout.tabs[1])).index == 1);
    const Hit slot = Pick(layout, CentreX(layout.slots[17]), CentreY(layout.slots[17]));
    assert(slot.kind == HitKind::Slot && slot.index == 17);
    const Hit page = Pick(layout, CentreX(layout.pageButtons[1]), CentreY(layout.pageButtons[1]));
    assert(page.kind == HitKind::Page && page.index == 1);
    assert(Pick(layout, CentreX(layout.previous), CentreY(layout.previous)).kind == HitKind::Previous);
    assert(Pick(layout, CentreX(layout.next), CentreY(layout.next)).kind == HitKind::Next);
    assert(Pick(layout, CentreX(layout.counter), CentreY(layout.counter)).kind == HitKind::None);
    assert(Pick(layout, 0.5f, 0.5f).kind == HitKind::None);
    assert(Pick(layout, CanvasWidth + 10.0f, CanvasHeight + 10.0f).kind == HitKind::None);

    // The page is clamped, and a page number that would not fit is dropped.
    assert(Build(99, 2, 3).page == 1);
    assert(Build(-5, 2, 3).page == 0);
    assert(Build(0, 1, 3).pageButtonCount == 1);
    const Layout many = Build(3, 20, 3);
    assert(many.page == 3 && many.pageButtonCount == 0);
    assert(Build(0, 1, 0).tabCount == 0);

    // Icon keys match the cache the mod already extracts.
    assert(IconKey(Category::Uniques, 122) == "u122");
    assert(IconKey(Category::Sets, 0) == "t0");
    assert(IconKey(Category::Runewords, 7).empty());

    std::puts("Collection panel: tabs, grid, paging, hit testing and icon keys passed");
}
