// Collection panel (the accepted mock-up v3): the kind buttons, the card grid, the scroll
// bar, the found bar, the item-type tiles and the footer with its Sort / Show lists -
// where they land and what the laser hits. Pure geometry, so this runs without D2R.
#include "collection_panel.h"
#include "stash_tab_highlight.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <iterator>

using namespace framevr::collection;
using namespace framevr::collection::panel;

namespace {

float CentreX(const Rect& rect) { return rect.x + rect.width * 0.5f; }
float CentreY(const Rect& rect) { return rect.y + rect.height * 0.5f; }

bool Overlaps(const Rect& a, const Rect& b) {
    return a.x < b.x + b.width && b.x < a.x + a.width && a.y < b.y + b.height && b.y < a.y + a.height;
}

bool OnCanvas(const Rect& r) {
    return r.width > 0 && r.height > 0 && r.x >= 0 && r.y >= 0 && r.x + r.width <= CanvasWidth + 0.01f &&
           r.y + r.height <= CanvasHeight + 0.01f;
}

void CheckGrid(Size size) {
    const Layout layout = Build(0, 2, 3, size);
    const int    slots  = SlotsFor(size);
    assert(layout.slotCount == slots);
    for (int i = 0; i < slots; ++i) {
        const Rect& slot = layout.slots[i];
        assert(OnCanvas(slot));
        assert(slot.y >= layout.tabs[0].y + layout.tabs[0].height);   // below the kind buttons
        assert(slot.y + slot.height <= layout.progress.y);            // above the found bar
        assert(slot.x + slot.width <= layout.scroll.x);               // left of the scroll bar
        for (int j = 0; j < i; ++j) assert(!Overlaps(layout.slots[j], slot));
    }
    for (int i = slots; i < MaxSlots; ++i) assert(layout.slots[i].width == 0);
}

}   // namespace

int main() {
    // The canvas keeps the mock-up page's shape (620 x 746).
    assert(std::fabs(CanvasWidth / CanvasHeight - 620.0f / 746.0f) < 0.002f);
    assert(std::fabs(Mx(MockRight) - CanvasWidth) < 1.0f && std::fabs(My(MockBottom) - CanvasHeight) < 0.01f);

    static_assert(SlotsFor(Size::Small) == 6 && SlotsFor(Size::Medium) == 12 && SlotsFor(Size::Large) == 24);
    static_assert(MaxSlots == SlotsFor(Size::Large));
    for (int i = 0; i < int(Size::Count); ++i) CheckGrid(static_cast<Size>(i));
    static_assert(PagesFor(0) == 1 && PagesFor(12) == 1 && PagesFor(13) == 2);
    static_assert(PagesFor(403) == 34 && PagesFor(403, Size::Small) == 68 && PagesFor(403, Size::Large) == 17);

    const Layout layout = Build(0, 5, 3, Size::Medium);
    for (int i = 0; i < layout.tabCount; ++i) {
        assert(OnCanvas(layout.tabs[i]));
        if (i > 0) assert(!Overlaps(layout.tabs[i - 1], layout.tabs[i]));
    }
    for (const Rect* r : {&layout.scroll, &layout.previous, &layout.next, &layout.track, &layout.progress, &layout.counter,
                          &layout.sortBox, &layout.showBox}) assert(OnCanvas(*r));
    assert(!Overlaps(layout.previous, layout.track) && !Overlaps(layout.track, layout.next));
    for (int i = 0; i < GroupCount; ++i) {
        assert(OnCanvas(layout.groups[i]));
        for (int j = 0; j < i; ++j) assert(!Overlaps(layout.groups[j], layout.groups[i]));
        assert(layout.groups[i].y >= layout.counter.y + layout.counter.height);   // under the count
        assert(layout.groups[i].y + layout.groups[i].height <= layout.sizeSteps[0].y);   // above the footer
    }
    const Rect* footer[] = {&layout.sizeSteps[0], &layout.sizeSteps[1], &layout.sizeSteps[2], &layout.sortBox, &layout.showBox};
    for (size_t i = 0; i < std::size(footer); ++i)
        for (size_t j = 0; j < i; ++j) assert(!Overlaps(*footer[j], *footer[i]));

    // Hit testing.
    assert(Pick(layout, CentreX(layout.tabs[1]), CentreY(layout.tabs[1])).kind == HitKind::Tab);
    const Hit slot = Pick(layout, CentreX(layout.slots[5]), CentreY(layout.slots[5]));
    assert(slot.kind == HitKind::Slot && slot.index == 5);
    assert(Pick(layout, CentreX(layout.previous), CentreY(layout.previous)).kind == HitKind::Previous);
    assert(Pick(layout, CentreX(layout.next), CentreY(layout.next)).kind == HitKind::Next);
    const Hit top = Pick(layout, CentreX(layout.track), layout.track.y + 1.0f);
    const Hit bottom = Pick(layout, CentreX(layout.track), layout.track.y + layout.track.height - 1.0f);
    assert(top.kind == HitKind::Page && top.index == 0 && bottom.kind == HitKind::Page && bottom.index == 4);
    assert(Pick(layout, CentreX(layout.sizeSteps[2]), CentreY(layout.sizeSteps[2])).index == 2);
    assert(Pick(layout, CentreX(layout.sortBox), CentreY(layout.sortBox)).kind == HitKind::SortBox);
    assert(Pick(layout, CentreX(layout.showBox), CentreY(layout.showBox)).kind == HitKind::ShowBox);
    const Hit group = Pick(layout, CentreX(layout.groups[7]), CentreY(layout.groups[7]));
    assert(group.kind == HitKind::Group && group.index == 7);
    assert(Pick(layout, CentreX(layout.counter), CentreY(layout.counter)).kind == HitKind::None);
    assert(Pick(layout, 0.5f, 0.5f).kind == HitKind::None);

    // An open list: its options stand above its box, over the tiles, and win the laser there.
    const Layout sorting = Build(0, 5, 3, Size::Medium, Drop::Sort);
    for (int i = 0; i < int(Sort::Count); ++i) {
        assert(OnCanvas(sorting.sortOptions[i]));
        assert(sorting.sortOptions[i].y + sorting.sortOptions[i].height <= sorting.sortBox.y);
        const Hit option = Pick(sorting, CentreX(sorting.sortOptions[i]), CentreY(sorting.sortOptions[i]));
        assert(option.kind == HitKind::Sort && option.index == i);
    }
    const Layout showing = Build(0, 5, 3, Size::Medium, Drop::Show);
    for (int i = 0; i < int(Show::Count); ++i) {
        assert(OnCanvas(showing.showOptions[i]));
        const Hit option = Pick(showing, CentreX(showing.showOptions[i]), CentreY(showing.showOptions[i]));
        assert(option.kind == HitKind::Show && option.index == i);
    }
    // closed, the same place is a tile again
    assert(Pick(layout, CentreX(showing.showOptions[0]), CentreY(showing.showOptions[0])).kind != HitKind::Show);

    // The thumb at the page's share of the track.
    const Rect first = Thumb(Build(0, 5, 3)), last = Thumb(Build(4, 5, 3));
    assert(first.y == layout.track.y && last.y > first.y && last.y + last.height <= layout.track.y + layout.track.height + 0.01f);
    assert(Build(99, 2, 3).page == 1 && Build(-5, 2, 3).page == 0 && Build(0, 1, 0).tabCount == 0);

    // Item types to tiles.
    assert(GroupOfType("helm") == 0 && GroupOfType("circ") == 0 && GroupOfType("tors") == 1);
    assert(GroupOfType("ashd") == 2 && GroupOfType("ring") == 7 && GroupOfType("xbow") == 10 && GroupOfType("spea") == 11);
    assert(GroupOfType("mace") == NoGroup && GroupOfType("jewl") == NoGroup && GroupOfType("") == NoGroup);

    // Icon keys match the cache the mod already extracts.
    assert(IconKey(Category::Uniques, 122, 0) == "u122");
    assert(IconKey(Category::Sets, 0, 0) == "t0");
    assert(IconKey(Category::Runewords, 7, 0).empty());
    const uint32_t gem = uint32_t('g') | (uint32_t('c') << 8) | (uint32_t('v') << 16) | (uint32_t(' ') << 24);
    assert(IconKey(Category::Materials, 0, gem) == "i20766367");

    // The active-tab mark (still drawn when the page floats, in_ui=0).
    {
        const auto mark = framevr::tabhighlight::Paint();
        assert(mark.size() == size_t(framevr::tabhighlight::Width) * framevr::tabhighlight::Height * 4);
    }

    std::puts("Collection panel: the mock-up's layout, the lists, hit testing, icon keys and the tab mark passed");
}
