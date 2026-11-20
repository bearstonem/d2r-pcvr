// Collection panel painting. See collection_draw.h.
#include "collection_draw.h"

#include <string>
#include <vector>

#include "collection_tooltip.h"
#include "vr_settings_draw.h"

namespace framevr::collection::draw {
namespace {

using settings::draw::Align;
using settings::draw::Color;
using settings::draw::ControlState;
using settings::draw::Icon;
using settings::draw::Painter;
using settings::draw::RectF;
using settings::draw::TextSize;
using settings::draw::TextStyle;

RectF Box(const panel::Rect& rect) { return {rect.x, rect.y, rect.width, rect.height}; }

RectF Inset(const panel::Rect& rect, float by) {
    return {rect.x + by, rect.y + by, rect.width - 2 * by, rect.height - 2 * by};
}

// The item image keeps its own proportions inside the slot: a wide belt stays wide
// and short, a tall armour fills the height rather than being stretched to the box.
// Centred in the box; never stretched, never enlarged past it.
RectF FitImage(const RectF& box, int width, int height) {
    if (width <= 0 || height <= 0 || box.w <= 0 || box.h <= 0) return box;
    const float aspect = float(width) / float(height);
    if (aspect > box.w / box.h) {
        const float h = box.w / aspect;
        return {box.x, box.y + (box.h - h) * 0.5f, box.w, h};
    }
    const float w = box.h * aspect;
    return {box.x + (box.w - w) * 0.5f, box.y, w, box.h};
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

// The card under the laser: a dark stone box in a bronze frame, the entry's name
// in its category's colour, one quiet line, then the game's own item text wrapped
// to the box. It sits beside the pointed slot and never leaves the panel.
void DrawTooltip(Painter& painter, const panel::Rect& anchor, const tooltip::Tooltip& tip, Category category) {
    if (tip.Empty()) return;
    constexpr float Width = 384.0f, Pad = 18.0f, TitleGap = 6.0f, DividerGap = 10.0f;
    const float     textWidth = Width - 2 * Pad;

    const TextSize title    = painter.Measure(tip.title, TextStyle::Value, textWidth, true);
    const TextSize subtitle = painter.Measure(tip.subtitle, TextStyle::Small, textWidth, true);
    std::vector<TextSize> body;
    float                 bodyHeight = 0;
    for (const std::string& line : tip.body) {
        const TextSize size = painter.Measure(line.empty() ? " " : line, TextStyle::Help, textWidth, true);
        bodyHeight += size.height;
        body.push_back(size);
    }
    std::vector<TextSize> ranges;
    float                 rangesHeight = 0;
    for (const std::string& line : tip.ranges) {
        const TextSize size = painter.Measure(line.empty() ? " " : line, TextStyle::Help, textWidth, true);
        rangesHeight += size.height;
        ranges.push_back(size);
    }
    const TextSize rangeLabel = painter.Measure("Possible ranges", TextStyle::Small, textWidth, false);
    const float    rangesBlock = tip.ranges.empty() ? 0
        : DividerGap + 1 + DividerGap + rangeLabel.height + rangesHeight;
    const float    height = Pad + title.height + TitleGap + subtitle.height + DividerGap + 1 + DividerGap +
                            bodyHeight + rangesBlock + Pad;
    const tooltip::Box box = tooltip::PlaceTooltip(anchor, Width, height);

    Color titleColor = Color::Text;
    switch (category) {
    case Category::Uniques:   titleColor = Color::GoldHi; break;
    case Category::Sets:      titleColor = Color::Ok; break;
    case Category::Runewords: titleColor = Color::Section; break;
    default: break;
    }

    painter.Fill({box.x, box.y, box.w, box.h}, Color::Ink, 0.94f);
    painter.Outline({box.x, box.y, box.w, box.h}, Color::Bronze, 3.0f);

    float y = box.y + Pad;
    painter.Text(tip.title, TextStyle::Value, {box.x + Pad, y, textWidth, title.height}, titleColor, Align::Leading, true);
    y += title.height + TitleGap;
    painter.Text(tip.subtitle, TextStyle::Small, {box.x + Pad, y, textWidth, subtitle.height}, Color::Dim, Align::Leading, true);
    y += subtitle.height + DividerGap;
    painter.Divider(box.x + Pad, box.x + box.w - Pad, y);
    y += 1 + DividerGap;
    for (std::size_t i = 0; i < body.size(); ++i) {
        const std::string& line = tip.body[i];
        painter.Text(line.empty() ? " " : line, TextStyle::Help, {box.x + Pad, y, textWidth, body[i].height},
                     Color::TipBody, Align::Leading, true);
        y += body[i].height;
    }

    // A found item's possible ranges, under a rule, so the exact roll above can be
    // compared with what the item could have rolled.
    if (!tip.ranges.empty()) {
        y += DividerGap;
        painter.Divider(box.x + Pad, box.x + box.w - Pad, y);
        y += 1 + DividerGap;
        painter.Text("Possible ranges", TextStyle::Small, {box.x + Pad, y, textWidth, rangeLabel.height},
                     Color::Quiet, Align::Leading, false);
        y += rangeLabel.height;
        for (std::size_t i = 0; i < ranges.size(); ++i) {
            const std::string& line = tip.ranges[i];
            painter.Text(line.empty() ? " " : line, TextStyle::Help, {box.x + Pad, y, textWidth, ranges[i].height},
                         Color::Magic, Align::Leading, true);
            y += ranges[i].height;
        }
    }
}

}   // namespace

void Paint(Painter& painter, const Library& library, const ViewState& view, const IconLookup& icons,
           std::string_view gameText, std::string_view rangeText) {
    const panel::Layout layout = panel::Build(view.page, view.Pages(library), int(TabCategoryCount), view.size);

    // The panel's own surface: stone inside a bronze frame.
    painter.Stone({0, 0, panel::CanvasWidth, panel::CanvasHeight}, Color::Stone, 0.30f);
    painter.Outline({3, 3, panel::CanvasWidth - 6, panel::CanvasHeight - 6}, Color::FrameB, 6.f);

    // The category tabs, then the page numbers on the same row.
    for (int i = 0; i < layout.tabCount; ++i) {
        ControlState state;
        state.chosen  = static_cast<Category>(i) == view.category;
        state.pointed = view.hover.kind == panel::HitKind::Tab && view.hover.index == i;
        painter.Tab(Box(layout.tabs[i]), Icon::None, CategoryName(static_cast<Category>(i)), state);
    }
    for (int i = 0; i < layout.pageButtonCount; ++i) {
        ControlState state;
        state.chosen  = i == view.page;
        state.pointed = view.hover.kind == panel::HitKind::Page && view.hover.index == i;
        painter.Socket(Box(layout.pageButtons[i]), std::to_string(i + 1), state);
    }

    // One slot per catalog entry: its icon when found, a dim dashed outline when
    // it is still missing. A unique's slot is gold on brown, a set item's green.
    const SlotStyle style = StyleOf(view.category);
    painter.Fill(GridField(layout), Color::Ink, 0.45f);
    const auto visible = VisibleSlots(library, view);
    for (int slot = 0; slot < layout.slotCount; ++slot) {
        const Entry* entry = SlotEntry(library, view, visible, slot);
        if (entry == nullptr) {
            // The page (or a filter such as Found) holds fewer entries than the grid
            // has slots: the rest stay grey sockets, so the grid always reads full.
            const RectF empty = Box(layout.slots[slot]);
            painter.Fill(empty, Color::Socket, 0.9f);
            painter.Outline(empty, Color::EdgeOff, 1.f, true);
            continue;
        }
        const bool   found = SlotFound(library, view, visible, slot);
        const bool   pointed = view.hover.kind == panel::HitKind::Slot && view.hover.index == slot;
        const RectF  box   = Box(layout.slots[slot]);
        const float  side  = layout.slots[slot].width;
        const RectF  inner = Inset(layout.slots[slot], side * 0.15f);
        if (found) {
            painter.Fill(box, style.fill, 0.96f);
            painter.Outline(box, style.edge, pointed ? 4.f : 3.f);
            const IconPixels* pixels = icons ? icons(panel::IconKey(view.category, entry->recordId, entry->code)) : nullptr;
            if (pixels != nullptr && pixels->rgba != nullptr) {
                painter.Image(FitImage(inner, pixels->width, pixels->height), pixels->rgba, pixels->width, pixels->height);
            } else {
                // Still decoding: a gold dot stands for the icon for now.
                painter.Fill({inner.x + inner.w * 0.33f, inner.y + inner.h * 0.33f, inner.w * 0.34f, inner.h * 0.34f}, style.edge, 0.9f);
            }
        } else {
            painter.Fill(box, Color::Socket, 0.9f);
            painter.Outline(box, Color::EdgeOff, pointed ? 2.f : 1.f, true);
            painter.Outline(inner, Color::EdgeOff, 1.f, true);
        }
    }

    // Footer: the page arrows, the counter for the open category, and the Size
    // control (a bigger slot shows fewer of them, so there are more pages).
    ControlState back;
    back.disabled = view.page <= 0;
    back.pointed  = view.hover.kind == panel::HitKind::Previous;
    painter.Button(Box(layout.previous), {}, Icon::ChevronLeft, back);
    ControlState forward;
    forward.disabled = view.page + 1 >= view.Pages(library);
    forward.pointed  = view.hover.kind == panel::HitKind::Next;
    painter.Button(Box(layout.next), {}, Icon::Chevron, forward);
    painter.Text(CounterText(library, view), TextStyle::Value, Box(layout.counter), Color::Text, Align::Center);
    painter.Text("Size", TextStyle::Small, Box(layout.sizeLabel), Color::Quiet, Align::Trailing);
    for (int i = 0; i < int(panel::Size::Count); ++i) {
        ControlState step;
        step.chosen  = i == int(view.size);
        step.pointed = view.hover.kind == panel::HitKind::Size && view.hover.index == i;
        painter.Socket(Box(layout.sizeSteps[i]), panel::SizeName(static_cast<panel::Size>(i)), step);
    }
    painter.Text("Show", TextStyle::Small, Box(layout.showLabel), Color::Quiet, Align::Trailing);
    for (int i = 0; i < int(panel::Show::Count); ++i) {
        ControlState step;
        step.chosen  = i == int(view.show);
        step.pointed = view.hover.kind == panel::HitKind::Show && view.hover.index == i;
        painter.Socket(Box(layout.showSteps[i]), panel::ShowName(static_cast<panel::Show>(i)), step);
    }

    // The pointed slot's card, last so it sits over the grid.
    if (view.hover.kind == panel::HitKind::Slot) {
        if (const Entry* entry = SlotEntry(library, view, visible, view.hover.index)) {
            const bool found = SlotFound(library, view, visible, view.hover.index);
            DrawTooltip(painter, layout.slots[view.hover.index], tooltip::BuildTooltip(*entry, found, gameText, rangeText), view.category);
        }
    }
}

}   // namespace framevr::collection::draw
