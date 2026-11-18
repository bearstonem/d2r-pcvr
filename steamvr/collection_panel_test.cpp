// Collection panel: the tab row, the slot grid, the footer and the Size control
// - where they land and what the laser hits. Pure geometry, so this runs
// without D2R.
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

void CheckGrid(Size size) {
    const Layout layout = Build(0, 2, 3, size);
    const int    slots  = SlotsFor(size);

    assert(layout.slotCount == slots);
    assert(layout.columns == GridOf(size).columns && layout.rows == GridOf(size).rows);
    for (int i = 0; i < slots; ++i) {
        const Rect& slot = layout.slots[i];
        assert(slot.width > 0 && slot.height > 0);
        assert(slot.x >= 0 && slot.x + slot.width <= CanvasWidth);
        assert(slot.y >= 0 && slot.y + slot.height <= CanvasHeight);
        assert(slot.y >= layout.tabs[0].y + layout.tabs[0].height);   // below the tabs
        assert(slot.y + slot.height <= layout.previous.y);            // above the footer
        if (i > 0) assert(slot.width == layout.slots[0].width);
        for (int j = 0; j < i; ++j) assert(!Overlaps(layout.slots[j], slot));
    }
    // First slot top left of the grid, last bottom right.
    assert(layout.slots[0].x < layout.slots[layout.columns - 1].x);
    assert(layout.slots[0].y < layout.slots[(layout.rows - 1) * layout.columns].y);
}

}   // namespace

int main() {
    // The three sizes and their slot counts.
    static_assert(SlotsFor(Size::Small) == 96);
    static_assert(SlotsFor(Size::Medium) == 60);
    static_assert(SlotsFor(Size::Large) == 40);
    static_assert(MaxSlots == SlotsFor(Size::Small));
    static_assert(GridOf(Size::Medium).columns == 10 && GridOf(Size::Medium).rows == 6);
    static_assert(GridOf(Size::Large).columns == 8 && GridOf(Size::Large).rows == 5);

    // Pages hold SlotsFor(size) entries each, and never fewer than one page.
    static_assert(PagesFor(0) == 1);
    static_assert(PagesFor(1) == 1);
    static_assert(PagesFor(96) == 1);
    static_assert(PagesFor(97) == 2);
    static_assert(PagesFor(403) == 5);                                        // the uniques on 3.4.0
    static_assert(PagesFor(403, Size::Medium) == 7);
    static_assert(PagesFor(403, Size::Large) == 11);

    // Every size keeps its grid inside the panel and out of the bars.
    CheckGrid(Size::Small);
    CheckGrid(Size::Medium);
    CheckGrid(Size::Large);

    const Layout layout = Build(0, 2, 3, Size::Small);

    // Tabs sit on the tab row, left to right, without touching each other.
    assert(layout.tabCount == 3);
    for (int i = 0; i < layout.tabCount; ++i) {
        const Rect& tab = layout.tabs[i];
        assert(tab.width > 0 && tab.height > 0);
        assert(tab.x >= Margin && tab.x + tab.width <= CanvasWidth);
        if (i > 0) assert(!Overlaps(layout.tabs[i - 1], tab));
    }

    // Two pages of a three-tab category: two page numbers, clear of the tabs.
    assert(layout.pageButtonCount == 2);
    for (int i = 0; i < layout.pageButtonCount; ++i) {
        assert(!Overlaps(layout.pageButtons[i], layout.tabs[layout.tabCount - 1]));
        if (i > 0) assert(!Overlaps(layout.pageButtons[i - 1], layout.pageButtons[i]));
    }

    // The footer: the arrows at the corners, the counter centred, and the Size
    // control between the previous arrow and the counter, its steps in a row.
    assert(layout.previous.x == Margin);
    assert(layout.next.x + layout.next.width == CanvasWidth - Margin);
    assert(!Overlaps(layout.previous, layout.next));
    assert(layout.sizeSteps[0].x > layout.previous.x + layout.previous.width);
    assert(layout.sizeSteps[static_cast<int>(Size::Count) - 1].x + layout.sizeSteps[static_cast<int>(Size::Count) - 1].width
           < layout.counter.x);
    for (int i = 0; i < static_cast<int>(Size::Count); ++i) {
        assert(layout.sizeSteps[i].width > 0 && layout.sizeSteps[i].height > 0);
        assert(layout.sizeSteps[i].y + layout.sizeSteps[i].height <= CanvasHeight);
        assert(!Overlaps(layout.sizeSteps[i], layout.sizeLabel));
        if (i > 0) assert(!Overlaps(layout.sizeSteps[i - 1], layout.sizeSteps[i]));
    }
    assert(!Overlaps(layout.sizeLabel, layout.previous));

    // Pointing: each control takes the point at its centre and nothing else.
    assert(Pick(layout, CentreX(layout.tabs[1]), CentreY(layout.tabs[1])).kind == HitKind::Tab);
    assert(Pick(layout, CentreX(layout.tabs[1]), CentreY(layout.tabs[1])).index == 1);
    const Hit slot = Pick(layout, CentreX(layout.slots[17]), CentreY(layout.slots[17]));
    assert(slot.kind == HitKind::Slot && slot.index == 17);
    const Hit page = Pick(layout, CentreX(layout.pageButtons[1]), CentreY(layout.pageButtons[1]));
    assert(page.kind == HitKind::Page && page.index == 1);
    assert(Pick(layout, CentreX(layout.previous), CentreY(layout.previous)).kind == HitKind::Previous);
    assert(Pick(layout, CentreX(layout.next), CentreY(layout.next)).kind == HitKind::Next);
    const Hit step = Pick(layout, CentreX(layout.sizeSteps[2]), CentreY(layout.sizeSteps[2]));
    assert(step.kind == HitKind::Size && step.index == 2);
    assert(Pick(layout, CentreX(layout.counter), CentreY(layout.counter)).kind == HitKind::None);
    assert(Pick(layout, CentreX(layout.sizeLabel), CentreY(layout.sizeLabel)).kind == HitKind::None);
    assert(Pick(layout, 0.5f, 0.5f).kind == HitKind::None);
    assert(Pick(layout, CanvasWidth + 10.0f, CanvasHeight + 10.0f).kind == HitKind::None);

    // A slot past this size's count is not pressable: the layout leaves the rest
    // of the array zeroed, so nothing is under the laser there.
    const Layout medium = Build(0, 2, 3, Size::Medium);
    assert(medium.slotCount == 60);
    assert(Pick(medium, CentreX(medium.slots[0]), CentreY(medium.slots[0])).kind == HitKind::Slot);
    assert(Pick(medium, CentreX(medium.slots[59]), CentreY(medium.slots[59])).index == 59);
    for (int i = medium.slotCount; i < MaxSlots; ++i) assert(medium.slots[i].width == 0 && medium.slots[i].height == 0);

    // The page is clamped, and page numbers that would not fit are dropped.
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

    std::puts("Collection panel: sizes, tabs, grid, footer, Size control, hit testing and icon keys passed");
}
