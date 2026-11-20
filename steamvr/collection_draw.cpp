// Collection panel painting. See collection_draw.h.
//
// The owner's accepted preview v3 (2026-11-20): a page of the D2R stash in its own
// grey stone, not Path of Exile's brown. A thin gold double line round the content;
// Uniques / Sets / Runewords with their counts on top and the found bar under them;
// cards like stash cells with a name plate (a unique's name in the game's unique
// gold), the found ones in a thin gold frame, the missing ones greyed; a scroll bar
// on the right; the item types with their share found below the grid, their light
// grey silhouettes sized as the items are in the inventory; Size, Sort and Show at
// the bottom. The card under the laser shows the game's item text in the game's own
// tooltip look: black, centred, the properties in magic blue.
#include "collection_draw.h"

#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>

#include "collection_tooltip.h"
#include "vr_settings_draw.h"

namespace framevr::collection::draw {
namespace {

using settings::draw::Align;
using settings::draw::Color;
using settings::draw::Icon;
using settings::draw::Painter;
using settings::draw::RectF;
using settings::draw::TextSize;
using settings::draw::TextStyle;

RectF Box(const panel::Rect& rect) { return {rect.x, rect.y, rect.width, rect.height}; }
RectF Inset(const RectF& r, float by) { return {r.x + by, r.y + by, r.w - 2 * by, r.h - 2 * by}; }

// The item image keeps its own proportions inside its box, centred, never stretched.
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

// A recess cut into the stone, like a stash cell: dark inside, the shadow on its
// upper-left edge and the light on its lower-right.
void Niche(Painter& painter, const RectF& r, float alpha = 0.92f) {
    painter.Fill(r, Color::StashNiche, alpha);
    painter.Line(r.x, r.y + 0.5f, r.Right(), r.y + 0.5f, Color::StashShadow, 2.f);
    painter.Line(r.x + 0.5f, r.y, r.x + 0.5f, r.Bottom(), Color::StashShadow, 2.f);
    painter.Line(r.x, r.Bottom() - 0.5f, r.Right(), r.Bottom() - 0.5f, Color::StashEdge, 1.f);
    painter.Line(r.Right() - 0.5f, r.y, r.Right() - 0.5f, r.Bottom(), Color::StashEdge, 1.f);
}

// A stone button: raised lighter stone when chosen (gold word), a recess when not;
// the pointed one gets a brighter gold ring.
void StoneButton(Painter& painter, const RectF& r, std::string_view word, bool chosen, bool pointed,
                 bool disabled = false, TextStyle style = TextStyle::Small) {
    if (chosen) {
        painter.Fill(r, Color::StashLight, 1.f);
        painter.Line(r.x, r.y + 0.5f, r.Right(), r.y + 0.5f, Color::StashEdge, 1.f);
        painter.Line(r.x, r.Bottom() - 1.f, r.Right(), r.Bottom() - 1.f, Color::StashShadow, 2.f);
    } else {
        Niche(painter, r);
    }
    if (pointed && !disabled) painter.Outline(Inset(r, -2.f), Color::GoldHi, 2.f);
    const Color text = disabled ? Color::Dim : chosen ? Color::Gold : Color::Quiet;
    painter.TextFit(word, style, r, text, Align::Center, false);
}

// The colour a category's names are written in: the game's own item colours.
Color NameColor(Category category) {
    return category == Category::Sets ? Color::Ok : Color::Gold;
}

// Thread-local copies of an icon: greyed (a missing entry) and as a flat light grey
// silhouette (an item-type tile). Kept by cache key; a few hundred small images.
const std::vector<uint8_t>& Greyed(const std::string& key, const IconPixels& icon) {
    thread_local std::unordered_map<std::string, std::vector<uint8_t>> cache;
    auto& out = cache[key];
    if (out.size() != size_t(icon.width) * size_t(icon.height) * 4) {
        out.resize(size_t(icon.width) * size_t(icon.height) * 4);
        for (size_t i = 0; i < out.size(); i += 4) {
            const float l = 0.30f * icon.rgba[i] + 0.59f * icon.rgba[i + 1] + 0.11f * icon.rgba[i + 2];
            const uint8_t v = uint8_t(std::min(255.f, l * 0.55f));
            out[i] = out[i + 1] = out[i + 2] = v;
            out[i + 3] = icon.rgba[i + 3];
        }
    }
    return out;
}

const std::vector<uint8_t>& Silhouette(const std::string& key, const IconPixels& icon) {
    thread_local std::unordered_map<std::string, std::vector<uint8_t>> cache;
    auto& out = cache[key];
    if (out.size() != size_t(icon.width) * size_t(icon.height) * 4) {
        out.resize(size_t(icon.width) * size_t(icon.height) * 4);
        const uint32_t rgb = settings::draw::Rgb(Color::Silhouette);
        for (size_t i = 0; i < out.size(); i += 4) {
            out[i]     = uint8_t(rgb >> 16);
            out[i + 1] = uint8_t(rgb >> 8);
            out[i + 2] = uint8_t(rgb);
            out[i + 3] = icon.rgba[i + 3] > 160 ? 255 : 0;   // the item itself, not its soft drop shadow
        }
    }
    return out;
}

// A runeword's recipe as the catalog keeps it ("r08 r09 r07") in the runes' own
// names ("Ral Ort Tal"); anything else is kept as written.
std::string RuneNames(std::string_view codes) {
    constexpr const char* names[] = {"El", "Eld", "Tir", "Nef", "Eth", "Ith", "Tal", "Ral", "Ort", "Thul", "Amn",
                                     "Sol", "Shael", "Dol", "Hel", "Io", "Lum", "Ko", "Fal", "Lem", "Pul", "Um",
                                     "Mal", "Ist", "Gul", "Vex", "Ohm", "Lo", "Sur", "Ber", "Jah", "Cham", "Zod"};
    std::string out;
    size_t at = 0;
    while (at < codes.size()) {
        size_t end = codes.find(' ', at);
        if (end == std::string_view::npos) end = codes.size();
        const std::string_view word = codes.substr(at, end - at);
        std::string name(word);
        if (word.size() == 3 && word[0] == 'r' && word[1] >= '0' && word[1] <= '9' && word[2] >= '0' && word[2] <= '9') {
            const int number = (word[1] - '0') * 10 + (word[2] - '0');
            if (number >= 1 && number <= 33) name = names[number - 1];
        }
        if (!name.empty()) out += (out.empty() ? "" : " ") + name;
        at = end + 1;
    }
    return out;
}

// One card: a stash-cell recess, the item's picture fitted above its name plate.
void DrawCard(Painter& painter, const RectF& card, const Entry& entry, Category category, bool found, bool pointed,
              const IconLookup& icons) {
    Niche(painter, card);
    const float plateH = std::clamp(card.h * 0.30f, 50.f, 62.f);
    const RectF picture = Inset({card.x, card.y, card.w, card.h - plateH}, std::max(6.f, card.w * 0.06f));
    const RectF plate   = {card.x + 3.f, card.Bottom() - plateH, card.w - 6.f, plateH - 3.f};

    const std::string key    = panel::IconKey(category, entry.recordId, entry.code);
    const IconPixels* pixels = icons && !key.empty() ? icons(key) : nullptr;
    if (pixels != nullptr && pixels->rgba != nullptr) {
        const RectF at = FitImage(picture, pixels->width, pixels->height);
        if (found) painter.Image(at, pixels->rgba, pixels->width, pixels->height);
        else       painter.Image(at, Greyed(key, *pixels).data(), pixels->width, pixels->height, 0.8f);
    } else if (!entry.extra.empty()) {
        // A runeword has no item until it is made: its runes stand for the picture.
        painter.TextFit(RuneNames(entry.extra), TextStyle::Value, picture, found ? Color::Changed : Color::Dim, Align::Center, true, 18.f);
    }

    painter.Fill(plate, Color::Plate, 0.95f);
    painter.TextFit(entry.name, TextStyle::Small, Inset(plate, 3.f), found ? NameColor(category) : Color::Dim, Align::Center, true, 16.f);

    if (found) painter.Outline(Inset(card, 1.f), Color::Gold, 1.5f);
    if (pointed) painter.Outline(Inset(card, -1.f), Color::GoldHi, 3.f);
}

// The card under the laser, in the game's own tooltip look: black, every line
// centred, the name in its item colour, the properties in magic blue. Beside the
// pointed card and never off the page.
void DrawTooltip(Painter& painter, const panel::Rect& anchor, const tooltip::Tooltip& tip, Category category) {
    if (tip.Empty()) return;
    constexpr float Width = 420.0f, Pad = 18.0f, Gap = 6.0f;
    const float     textWidth = Width - 2 * Pad;
    std::vector<std::string> lines;
    std::vector<Color>       colors;
    std::vector<TextStyle>   styles;
    lines.push_back(tip.title);    colors.push_back(NameColor(category)); styles.push_back(TextStyle::Value);
    if (!tip.subtitle.empty()) { lines.push_back(tip.subtitle); colors.push_back(Color::Quiet); styles.push_back(TextStyle::Small); }
    for (const std::string& line : tip.body) { lines.push_back(line.empty() ? " " : line); colors.push_back(Color::Magic); styles.push_back(TextStyle::Small); }
    if (!tip.ranges.empty()) {
        lines.push_back("Possible ranges"); colors.push_back(Color::Dim); styles.push_back(TextStyle::Small);
        for (const std::string& line : tip.ranges) { lines.push_back(line.empty() ? " " : line); colors.push_back(Color::Quiet); styles.push_back(TextStyle::Small); }
    }
    std::vector<float> heights;
    float height = 2 * Pad;
    for (size_t i = 0; i < lines.size(); ++i) {
        const TextSize size = painter.Measure(lines[i], styles[i], textWidth, true);
        heights.push_back(size.height);
        height += size.height + (i == 0 ? Gap : 0.f);
    }
    const tooltip::Box box = tooltip::PlaceTooltip(anchor, Width, height);
    painter.Fill({box.x, box.y, box.w, box.h}, Color::StashShadow, 0.93f);
    painter.Outline({box.x, box.y, box.w, box.h}, Color::StashEdge, 1.f);
    float y = box.y + Pad;
    for (size_t i = 0; i < lines.size(); ++i) {
        painter.Text(lines[i], styles[i], {box.x + Pad, y, textWidth, heights[i]}, colors[i], Align::Center, true);
        y += heights[i] + (i == 0 ? Gap : 0.f);
    }
}

}   // namespace

void Paint(Painter& painter, const Library& library, const ViewState& view, const IconLookup& icons,
           std::string_view gameText, std::string_view rangeText) {
    const auto           visible = VisibleSlots(library, view);
    const int            pages   = panel::PagesFor(int(visible.size()), view.size);
    const panel::Layout  layout  = panel::Build(view.page, pages, int(TabCategoryCount), view.size);
    const RectF          page    = {0, 0, panel::CanvasWidth, panel::CanvasHeight};
    const auto pointed = [&](panel::HitKind kind, int index) { return view.hover.kind == kind && view.hover.index == index; };

    // The stash's stone, a thin gold double line round the content.
    painter.Stone(page, Color::StashStone, 0.35f);
    painter.Outline(Inset(page, 5.f), Color::Gold, 1.5f);
    painter.Outline(Inset(page, 9.f), Color::Bronze, 1.f);

    // Uniques / Sets / Runewords, each with its count.
    for (int i = 0; i < layout.tabCount; ++i) {
        const Category category = static_cast<Category>(i);
        const std::string word  = std::string(CategoryName(category)) + "  " + std::to_string(library.Found(category)) +
                                  "/" + std::to_string(library.Size(category));
        StoneButton(painter, Box(layout.tabs[i]), word, category == view.category, pointed(panel::HitKind::Tab, i),
                    false, TextStyle::Value);
    }

    // The found bar and "Uniques 8 / 403".
    {
        const RectF bar   = Box(layout.progress);
        const float total = float(std::max<uint32_t>(1, library.Size(view.category)));
        Niche(painter, Inset(bar, -2.f));
        painter.Fill({bar.x, bar.y, bar.w * float(library.Found(view.category)) / total, bar.h}, Color::Gold, 0.9f);
        const std::string counter = std::string(CategoryName(view.category)) + "  " + CounterText(library, view);
        painter.TextFit(counter, TextStyle::Value, Box(layout.counter), Color::Gold, Align::Trailing, false);
    }

    // The cards. A page with fewer entries leaves empty cells, so the grid always reads full.
    for (int slot = 0; slot < layout.slotCount; ++slot) {
        const RectF  card  = Box(layout.slots[slot]);
        const Entry* entry = SlotEntry(library, view, visible, slot);
        if (entry == nullptr) {
            Niche(painter, card, 0.6f);
            continue;
        }
        DrawCard(painter, card, *entry, view.category, SlotFound(library, view, visible, slot),
                 pointed(panel::HitKind::Slot, slot), icons);
    }

    // The scroll bar: its arrows, the track, the thumb at this page.
    {
        const bool canBack = view.page > 0, canForward = view.page + 1 < pages;
        StoneButton(painter, Box(layout.previous), "", false, pointed(panel::HitKind::Previous, 0), !canBack);
        painter.IconAt(Icon::ChevronUp, Inset(Box(layout.previous), 6.f), canBack ? Color::Quiet : Color::EdgeOff);
        StoneButton(painter, Box(layout.next), "", false, pointed(panel::HitKind::Next, 0), !canForward);
        painter.IconAt(Icon::ChevronDown, Inset(Box(layout.next), 6.f), canForward ? Color::Quiet : Color::EdgeOff);
        Niche(painter, Box(layout.track));
        const panel::Rect thumb = panel::Thumb(layout);
        painter.Fill(Inset(Box(thumb), 3.f), view.hover.kind == panel::HitKind::Page ? Color::StashEdge : Color::StashLight, 1.f);
    }

    // The item types: each tile's silhouette (the first unique of that type with an
    // icon), sized as the items are in the inventory - one scale for all tiles, so a
    // ring stays small beside an armour - its name and the share found.
    {
        // The silhouette is made at once: the lookup's pixels live only until its next call.
        struct Pick { const std::vector<uint8_t>* pixels = nullptr; int width = 0, height = 0; };
        Pick picks[panel::GroupCount];
        int  tallest = 1, widest = 1;
        for (int group = 0; group < panel::GroupCount && icons; ++group) {
            // The lowest-level unique of the tile's plain type: the plainest shape (a cap,
            // a buckler, a short sword); else of any type of the tile.
            for (int pass = 0; pass < 2 && picks[group].pixels == nullptr; ++pass) {
                const Entry* best = nullptr;
                for (uint32_t slot = 0; slot < library.Size(Category::Uniques); ++slot) {
                    const Entry* entry = library.At(Category::Uniques, slot);
                    if (entry == nullptr || panel::GroupOfType(entry->type) != group) continue;
                    if (pass == 0 && entry->type != panel::GroupMainType(group)) continue;
                    if (best == nullptr || entry->level < best->level) best = entry;
                }
                if (best == nullptr) continue;
                const std::string key = panel::IconKey(Category::Uniques, best->recordId, best->code);
                const IconPixels* icon = icons(key);
                if (icon == nullptr || icon->rgba == nullptr) continue;
                picks[group] = {&Silhouette(key, *icon), icon->width, icon->height};
                tallest = std::max(tallest, icon->height);
                widest  = std::max(widest, icon->width);
            }
        }
        const RectF first = Box(layout.groups[0]);
        const float imageH = first.h - 40.f, imageW = first.w * 0.42f;
        const float scale  = std::min(imageH / float(tallest), imageW / float(widest));
        for (int group = 0; group < panel::GroupCount; ++group) {
            const RectF tile  = Box(layout.groups[group]);
            const auto  share = ShareOf(library, view.category, group);
            const bool  chosen = view.group == group;
            if (chosen) {
                painter.Fill(tile, Color::StashLight, 1.f);
                painter.Outline(Inset(tile, 1.f), Color::Gold, 1.5f);
            } else {
                Niche(painter, tile);
            }
            if (pointed(panel::HitKind::Group, group)) painter.Outline(Inset(tile, -2.f), Color::GoldHi, 2.f);
            if (const Pick& pick = picks[group]; pick.pixels != nullptr) {
                const float w = pick.width * scale, h = pick.height * scale;
                const RectF at = {tile.x + 8.f + (imageW - w) * 0.5f, tile.y + 6.f + (imageH - h) * 0.5f, w, h};
                painter.Image(at, pick.pixels->data(), pick.width, pick.height, share.total ? 0.9f : 0.35f);
            }
            const RectF text = {tile.x + imageW + 12.f, tile.y + 6.f, tile.w - imageW - 18.f, imageH};
            const std::string percent = share.total ? std::to_string(share.found * 100 / share.total) + "%" : "-";
            painter.TextFit(percent, TextStyle::Value, text, share.total ? (chosen ? Color::Gold : Color::Text) : Color::Dim,
                            Align::Center, false);
            painter.TextFit(panel::GroupName(group), TextStyle::Small, {tile.x + 4.f, tile.Bottom() - 34.f, tile.w - 8.f, 30.f},
                            chosen ? Color::Gold : Color::Quiet, Align::Center, false);
        }
    }

    // Size, Sort, Show.
    painter.Text("Size", TextStyle::Small, Box(layout.sizeLabel), Color::Quiet, Align::Trailing);
    for (int i = 0; i < int(panel::Size::Count); ++i)
        StoneButton(painter, Box(layout.sizeSteps[i]), panel::SizeName(static_cast<panel::Size>(i)), i == int(view.size),
                    pointed(panel::HitKind::Size, i));
    painter.Text("Sort", TextStyle::Small, Box(layout.sortLabel), Color::Quiet, Align::Trailing);
    for (int i = 0; i < int(panel::Sort::Count); ++i)
        StoneButton(painter, Box(layout.sortSteps[i]), panel::SortName(static_cast<panel::Sort>(i)), i == int(view.sort),
                    pointed(panel::HitKind::Sort, i));
    painter.Text("Show", TextStyle::Small, Box(layout.showLabel), Color::Quiet, Align::Trailing);
    for (int i = 0; i < int(panel::Show::Count); ++i)
        StoneButton(painter, Box(layout.showSteps[i]), panel::ShowName(static_cast<panel::Show>(i)), i == int(view.show),
                    pointed(panel::HitKind::Show, i));

    // The pointed card's tooltip, last so it sits over everything.
    if (view.hover.kind == panel::HitKind::Slot) {
        if (const Entry* entry = SlotEntry(library, view, visible, view.hover.index)) {
            const bool found = SlotFound(library, view, visible, view.hover.index);
            DrawTooltip(painter, layout.slots[view.hover.index], tooltip::BuildTooltip(*entry, found, gameText, rangeText), view.category);
        }
    }
}

}   // namespace framevr::collection::draw
