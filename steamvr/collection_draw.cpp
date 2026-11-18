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

}   // namespace

void Paint(Painter& painter, const Library& library, const ViewState& view, const IconLookup& icons) {
    const panel::Layout layout = panel::Build(view.page, view.Pages(library), int(CategoryCount));

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
    // it is still missing.
    for (int slot = 0; slot < panel::SlotsPerPage; ++slot) {
        const Entry* entry = SlotEntry(library, view, slot);
        if (entry == nullptr) continue;
        const bool found   = SlotFound(library, view, slot);
        ControlState state;
        state.chosen  = found;
        state.pointed = slot == view.hovered;
        painter.Socket(Box(layout.slots[slot]), {}, state);

        const float side  = layout.slots[slot].width;
        const RectF inner = Inset(layout.slots[slot], side * 0.15f);
        if (found) {
            const IconPixels* pixels = icons ? icons(panel::IconKey(view.category, entry->recordId)) : nullptr;
            if (pixels != nullptr && !pixels->rgba.empty()) {
                painter.Image(inner, pixels->rgba.data(), pixels->width, pixels->height);
            } else {
                // Still decoding: a gold dot stands for the icon for now.
                painter.Fill({inner.x + inner.w * 0.33f, inner.y + inner.h * 0.33f, inner.w * 0.34f, inner.h * 0.34f}, Color::Gold, 0.9f);
            }
        } else {
            painter.Outline(inner, Color::EdgeOff, 2.f, true);
        }
    }

    // Footer: the two page arrows and the counter for the open category.
    ControlState back;
    back.disabled = view.page <= 0;
    painter.Button(Box(layout.previous), {}, Icon::ChevronLeft, back);
    ControlState forward;
    forward.disabled = view.page + 1 >= view.Pages(library);
    painter.Button(Box(layout.next), {}, Icon::Chevron, forward);
    painter.Text(CounterText(library, view), TextStyle::Value, Box(layout.counter), Color::Text, Align::Center);
}

}   // namespace framevr::collection::draw
