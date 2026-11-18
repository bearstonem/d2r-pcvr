// Collection pointer: the laser, the trigger and the clock become hover and
// clicks. Pure logic, so this runs without D2R.
#include "collection_pointer.h"
#include <cassert>
#include <cstdio>

using namespace framevr::collection;
using namespace framevr::collection::panel;
using framevr::collection::pointer::Filter;

namespace {

float CentreX(const Rect& rect) { return rect.x + rect.width * 0.5f; }
float CentreY(const Rect& rect) { return rect.y + rect.height * 0.5f; }

bool Is(const Hit& hit, HitKind kind, int index) { return hit.kind == kind && hit.index == index; }

}   // namespace

int main() {
    const Layout layout = Build(0, 3, 3);
    const float slotX   = CentreX(layout.slots[10]);
    const float slotY   = CentreY(layout.slots[10]);
    const float tabX    = CentreX(layout.tabs[1]);
    const float tabY    = CentreY(layout.tabs[1]);

    // Hover follows the laser, and leaving the slot takes the hover with it.
    {
        Filter filter;
        assert(filter.Step(slotX, slotY, layout, false, 0).kind == HitKind::None);
        assert(Is(filter.Hover(), HitKind::Slot, 10));
        filter.Step(1.0f, 1.0f, layout, false, 16);
        assert(filter.Hover().kind == HitKind::None);
    }

    // Hysteresis: just outside the slot the hover stays; away from the grid it goes.
    {
        Filter filter;
        filter.Step(slotX, slotY, layout, false, 0);
        const float edge = layout.slots[10].x - Filter::HysteresisPx * 0.5f;
        filter.Step(edge, slotY, layout, false, 16);
        assert(Is(filter.Hover(), HitKind::Slot, 10));
        filter.Step(5.0f, 5.0f, layout, false, 32);   // the top left corner: over nothing
        assert(filter.Hover().kind == HitKind::None);
    }

    // A press and a release on the same slot is a click on it.
    {
        Filter filter;
        filter.Step(slotX, slotY, layout, false, 0);
        assert(filter.Step(slotX, slotY, layout, true, 16).kind == HitKind::None);   // the press itself clicks nothing
        assert(filter.Held());
        const Hit click = filter.Step(slotX, slotY, layout, false, 200);
        assert(Is(click, HitKind::Slot, 10));
        assert(!filter.Held());
        // A held trigger does not click twice.
        assert(filter.Step(slotX, slotY, layout, false, 216).kind == HitKind::None);
    }

    // Releasing far from the pressed slot is not a click.
    {
        Filter filter;
        filter.Step(slotX, slotY, layout, false, 0);
        filter.Step(slotX, slotY, layout, true, 16);
        const Hit click = filter.Step(tabX, tabY, layout, false, 200);
        assert(click.kind == HitKind::None);
    }

    // The pre-trigger lock: the hand dips onto the neighbour as the trigger goes
    // down, but the release comes back on the slot that was aimed at 90 ms
    // earlier, so that slot is what clicks.
    {
        Filter filter;
        filter.Step(slotX, slotY, layout, false, 0);                       // aimed at slot 10
        const float neighbourX = CentreX(layout.slots[9]);
        filter.Step(neighbourX, slotY, layout, false, 40);                 // dipped onto slot 9
        assert(filter.Step(neighbourX, slotY, layout, true, 100).kind == HitKind::None);
        const Hit released = filter.Step(slotX, slotY, layout, false, 300);   // back on the target
        assert(Is(released, HitKind::Slot, 10));
    }

    // A trigger already down when the panel opens presses nothing; it must be
    // released first, and then it works.
    {
        Filter filter;
        filter.Reset(true);
        assert(filter.Step(slotX, slotY, layout, true, 0).kind == HitKind::None);
        assert(filter.Step(slotX, slotY, layout, true, 16).kind == HitKind::None);
        assert(filter.Step(slotX, slotY, layout, false, 32).kind == HitKind::None);
        assert(filter.Step(slotX, slotY, layout, true, 48).kind == HitKind::None);
        assert(Is(filter.Step(slotX, slotY, layout, false, 64), HitKind::Slot, 10));
    }

    // Tabs and page arrows click like slots.
    {
        Filter filter;
        filter.Step(tabX, tabY, layout, false, 0);
        filter.Step(tabX, tabY, layout, true, 16);
        assert(Is(filter.Step(tabX, tabY, layout, false, 200), HitKind::Tab, 1));

        Filter arrows;
        const float nextX = CentreX(layout.next);
        const float nextY = CentreY(layout.next);
        arrows.Step(nextX, nextY, layout, false, 0);
        arrows.Step(nextX, nextY, layout, true, 16);
        assert(Is(arrows.Step(nextX, nextY, layout, false, 200), HitKind::Next, 0));
    }

    // The counter is not pressable: a press there never clicks.
    {
        Filter filter;
        const float x = CentreX(layout.counter);
        const float y = CentreY(layout.counter);
        filter.Step(x, y, layout, false, 0);
        filter.Step(x, y, layout, true, 16);
        assert(filter.Step(x, y, layout, false, 200).kind == HitKind::None);
    }

    std::puts("Collection pointer: hover, hysteresis, lock, click on release and the held-trigger guard passed");
}
