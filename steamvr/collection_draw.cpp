// Collection panel painting. See collection_draw.h.
#include "collection_draw.h"

#include <string>

#include "vr_settings_draw.h"

namespace framevr::collection::draw {
namespace {

using settings::draw::Align;
using settings::draw::Color;
using settings::draw::ControlState;
using settings::draw::Icon;
using settings::draw::Painter;
using settings::draw::RectF;
using settings::draw::TextStyle;

RectF Box(const panel::Rect& rect) { return {rect.x, rect.y, rect.width, rect.height}; }

RectF Inset(const panel::Rect& rect, float by) {
    return {rect.x + by, rect.y + by, rect.width - 2 * by, rect.height - 2 * by};
}

// A category's slot colours: the game's own item colours - a unique is gold on
// brown, a set item green, and a runeword is not one item until it is made, so
// it stays stone and quiet.
struct SlotStyle {
    Color fill;
    Color edge;
};

SlotStyle StyleOf(Category category) {
    switch (category) {
    case Category::Uniques:   return {Color::UniqueFill, Color::BronzeHi};
    case Category::Sets:      return {Color::SetFill, Color::Ok};
    default:                  return {Color::Socket, Color::Quiet};
    }
}

// The dark field the whole grid sits on, so the panel reads as a stash page
// rather than loose squares.
RectF GridField(const panel::Layout& layout) {
    const panel::Rect& first = layout.slots[0];
    const panel::Rect& last  = layout.slots[layout.slotCount - 1];
    const float        pad   = first.width * 0.25f;
    return {first.x - pad, first.y - pad,
            (last.x + last.width) - first.x + 2 * pad, (last.y + last.height) - first.y + 2 * pad};
}

}   // namespace

void Paint(Painter& painter, const Library& library, const ViewState& view, const IconLookup& icons) {
    const panel::Layout layout = panel::Build(view.page, view.Pages(library), int(CategoryCount), view.size);

    // The panel's own surface: stone inside a bronze frame.
    painter.Stone({0, 0, panel::CanvasWidth, panel::CanvasHeight}, Color::Stone, 0.30f);
    painter.Outline({3, 3, panel::CanvasWidth - 6, panel::CanvasHeight - 6}, Color::FrameB, 6.f);
    painter.Text("Collection", TextStyle::Small,
                 {panel::CanvasWidth * 0.5f - 200.f, 5.f, 400.f, 18.f}, Color::Section, Align::Center);

    // The category tabs, then the page numbers on the same row.
    for (int i = 0; i < layout.tabCount; ++i) {
        ControlState state;
        state.chosen = static_cast<Category>(i) == view.category;
        painter.Tab(Box(layout.tabs[i]), Icon::None, CategoryName(static_cast<Category>(i)), state);
    }
    for (int i = 0; i < layout.pageButtonCount; ++i) {
        ControlState state;
        state.chosen = i == view.page;
        painter.Socket(Box(layout.pageButtons[i]), std::to_string(i + 1), state);
    }

    // One slot per catalog entry: its icon when found, a dim dashed outline when
    // it is still missing. A unique's slot is gold on brown, a set item's green.
    const SlotStyle style = StyleOf(view.category);
    painter.Fill(GridField(layout), Color::Ink, 0.45f);
    for (int slot = 0; slot < layout.slotCount; ++slot) {
        const Entry* entry = SlotEntry(library, view, slot);
        if (entry == nullptr) continue;
        const bool   found = SlotFound(library, view, slot);
        const RectF  box   = Box(layout.slots[slot]);
        const float  side  = layout.slots[slot].width;
        const RectF  inner = Inset(layout.slots[slot], side * 0.15f);
        if (found) {
            painter.Fill(box, style.fill, 0.96f);
            painter.Outline(box, style.edge, slot == view.hovered ? 4.f : 3.f);
            const IconPixels* pixels = icons ? icons(panel::IconKey(view.category, entry->recordId)) : nullptr;
            if (pixels != nullptr && pixels->rgba != nullptr) {
                painter.Image(inner, pixels->rgba, pixels->width, pixels->height);
            } else {
                // Still decoding: a gold dot stands for the icon for now.
                painter.Fill({inner.x + inner.w * 0.33f, inner.y + inner.h * 0.33f, inner.w * 0.34f, inner.h * 0.34f}, style.edge, 0.9f);
            }
        } else {
            painter.Fill(box, Color::Socket, 0.9f);
            painter.Outline(box, Color::EdgeOff, slot == view.hovered ? 2.f : 1.f, true);
            painter.Outline(inner, Color::EdgeOff, 1.f, true);
        }
    }

    // Footer: the page arrows, the counter for the open category, and the Size
    // control (a bigger slot shows fewer of them, so there are more pages).
    ControlState back;
    back.disabled = view.page <= 0;
    painter.Button(Box(layout.previous), {}, Icon::ChevronLeft, back);
    ControlState forward;
    forward.disabled = view.page + 1 >= view.Pages(library);
    painter.Button(Box(layout.next), {}, Icon::Chevron, forward);
    painter.Text(CounterText(library, view), TextStyle::Value, Box(layout.counter), Color::Text, Align::Center);
    painter.Text("Size", TextStyle::Small, Box(layout.sizeLabel), Color::Quiet, Align::Trailing);
    for (int i = 0; i < int(panel::Size::Count); ++i) {
        ControlState step;
        step.chosen = i == int(view.size);
        painter.Socket(Box(layout.sizeSteps[i]), panel::SizeName(static_cast<panel::Size>(i)), step);
    }
}

}   // namespace framevr::collection::draw
