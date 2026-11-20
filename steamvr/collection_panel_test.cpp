// Collection panel (preview v3): the category buttons, the found bar, the card
// grid, the scroll bar, the item-type tiles and the Size / Sort / Show footer -
// where they land and what the laser hits. Pure geometry, so this runs without D2R.
#include "collection_panel.h"
#include "stash_tab_highlight.h"
#include <cassert>
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
    return r.width > 0 && r.height > 0 && r.x >= 0 && r.y >= 0 && r.x + r.width <= CanvasWidth && r.y + r.height <= CanvasHeight;
}

void CheckGrid(Size size) {
    const Layout layout = Build(0, 2, 3, size);
    const int    slots  = SlotsFor(size);
    assert(layout.slotCount == slots);
    assert(layout.columns == GridOf(size).columns && layout.rows == GridOf(size).rows);
    for (int i = 0; i < slots; ++i) {
        const Rect& slot = layout.slots[i];
        assert(OnCanvas(slot));
        assert(slot.y >= layout.counter.y + layout.counter.height);        // below the found bar
        assert(slot.y + slot.height <= layout.groups[0].y);                 // above the type tiles
        assert(slot.x + slot.width <= layout.track.x);                      // left of the scroll bar
        if (i > 0) assert(slot.width == layout.slots[0].width);
        for (int j = 0; j < i; ++j) assert(!Overlaps(layout.slots[j], slot));
    }
    for (int i = slots; i < MaxSlots; ++i) assert(layout.slots[i].width == 0);
}

}   // namespace

int main() {
    static_assert(SlotsFor(Size::Small) == 6);
    static_assert(SlotsFor(Size::Medium) == 12);
    static_assert(SlotsFor(Size::Large) == 24);
    static_assert(MaxSlots == SlotsFor(Size::Large));
    for (int i = 0; i < int(Size::Count); ++i) CheckGrid(static_cast<Size>(i));

    static_assert(PagesFor(0) == 1);
    static_assert(PagesFor(12) == 1);
    static_assert(PagesFor(13) == 2);
    static_assert(PagesFor(403) == 34);                       // the uniques on 3.4.0, M
    static_assert(PagesFor(403, Size::Small) == 68);
    static_assert(PagesFor(403, Size::Large) == 17);

    const Layout layout = Build(0, 5, 3, Size::Medium);
    // Everything on the canvas, nothing overlapping its neighbours.
    for (int i = 0; i < layout.tabCount; ++i) {
        assert(OnCanvas(layout.tabs[i]));
        if (i > 0) assert(!Overlaps(layout.tabs[i - 1], layout.tabs[i]));
    }
    for (const Rect* r : {&layout.progress, &layout.counter, &layout.previous, &layout.next, &layout.track}) assert(OnCanvas(*r));
    assert(!Overlaps(layout.previous, layout.track) && !Overlaps(layout.track, layout.next));
    for (int i = 0; i < GroupCount; ++i) {
        assert(OnCanvas(layout.groups[i]));
        for (int j = 0; j < i; ++j) assert(!Overlaps(layout.groups[j], layout.groups[i]));
        assert(layout.groups[i].y + layout.groups[i].height <= layout.sizeSteps[0].y);   // above the footer
    }
    const Rect* footer[] = {&layout.sizeSteps[0], &layout.sizeSteps[1], &layout.sizeSteps[2], &layout.sortSteps[0],
                            &layout.sortSteps[1], &layout.showSteps[0], &layout.showSteps[1], &layout.showSteps[2]};
    for (size_t i = 0; i < std::size(footer); ++i) {
        assert(OnCanvas(*footer[i]));
        for (size_t j = 0; j < i; ++j) assert(!Overlaps(*footer[j], *footer[i]));
    }

    // Hit testing.
    assert(Pick(layout, CentreX(layout.tabs[1]), CentreY(layout.tabs[1])).kind == HitKind::Tab);
    assert(Pick(layout, CentreX(layout.tabs[1]), CentreY(layout.tabs[1])).index == 1);
    const Hit slot = Pick(layout, CentreX(layout.slots[5]), CentreY(layout.slots[5]));
    assert(slot.kind == HitKind::Slot && slot.index == 5);
    assert(Pick(layout, CentreX(layout.previous), CentreY(layout.previous)).kind == HitKind::Previous);
    assert(Pick(layout, CentreX(layout.next), CentreY(layout.next)).kind == HitKind::Next);
    // The track: its top is the first page, its bottom the last.
    const Hit top = Pick(layout, CentreX(layout.track), layout.track.y + 1.0f);
    const Hit bottom = Pick(layout, CentreX(layout.track), layout.track.y + layout.track.height - 1.0f);
    assert(top.kind == HitKind::Page && top.index == 0);
    assert(bottom.kind == HitKind::Page && bottom.index == 4);
    const Hit step = Pick(layout, CentreX(layout.sizeSteps[2]), CentreY(layout.sizeSteps[2]));
    assert(step.kind == HitKind::Size && step.index == 2);
    const Hit sort = Pick(layout, CentreX(layout.sortSteps[1]), CentreY(layout.sortSteps[1]));
    assert(sort.kind == HitKind::Sort && sort.index == 1);
    const Hit show = Pick(layout, CentreX(layout.showSteps[2]), CentreY(layout.showSteps[2]));
    assert(show.kind == HitKind::Show && show.index == 2);
    const Hit group = Pick(layout, CentreX(layout.groups[7]), CentreY(layout.groups[7]));
    assert(group.kind == HitKind::Group && group.index == 7);
    assert(Pick(layout, CentreX(layout.counter), CentreY(layout.counter)).kind == HitKind::None);
    assert(Pick(layout, 0.5f, 0.5f).kind == HitKind::None);
    assert(Pick(layout, CanvasWidth + 10.0f, CanvasHeight + 10.0f).kind == HitKind::None);

    // The thumb sits at the page's share of the track.
    const Rect first = Thumb(Build(0, 5, 3)), last = Thumb(Build(4, 5, 3));
    assert(first.y == Build(0, 5, 3).track.y);
    assert(last.y + last.height <= layout.track.y + layout.track.height + 0.01f && last.y > first.y);

    // The page is clamped.
    assert(Build(99, 2, 3).page == 1);
    assert(Build(-5, 2, 3).page == 0);
    assert(Build(0, 1, 0).tabCount == 0);

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

    // The active-tab mark: the frame is painted and the game's own gem (tab_gem.h) blitted
    // onto a straight-RGBA canvas of the atlas region's size.
    {
        const auto mark = framevr::tabhighlight::Paint();
        assert(mark.size() == size_t(framevr::tabhighlight::Width) * framevr::tabhighlight::Height * 4);
        int opaque = 0, gold = 0, blue = 0;
        for (size_t i = 0; i + 3 < mark.size(); i += 4) {
            if (mark[i + 3] == 0) continue;
            ++opaque;
            if (mark[i] > mark[i + 2]) ++gold;
            if (mark[i + 2] > mark[i] && mark[i + 2] > 90) ++blue;
        }
        assert(opaque > 0 && gold > 0 && blue > 0);
        assert(framevr::tabhighlight::GemWidth > 0 && framevr::tabhighlight::GemHeight > 0);
    }

    std::puts("Collection panel: sizes, tabs, grid, scroll bar, type tiles, footer, hit testing, icon keys and the tab mark passed");
}
