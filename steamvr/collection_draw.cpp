// Collection panel painting. See collection_draw.h.
//
// A port of the owner's accepted mock-up v3 (2026-11-20, render2.py + render3.py), element
// by element, with its own colours, line widths and text sizes, in its own pixels scaled to
// the canvas (collection_panel.h Mx / My / Md). The page is the stash's own stone (a patch
// cut out of the owner's UI shot, collection_art.h), its buttons that stone lighter or
// darker with a bevel, its labels in small capitals like the game's (Cinzel here), the item
// pictures from the mod's icon cache, missing items greyed.
#include "collection_draw.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>

#include "collection_art.h"
#include "collection_tooltip.h"
#include "vr_settings_draw.h"

namespace framevr::collection::draw {
namespace {

using settings::draw::Align;
using settings::draw::Painter;
using settings::draw::RectF;
using settings::draw::TextSize;
using settings::draw::TextStyle;
using panel::Md;
using panel::Mx;
using panel::My;

// The mock-up's colours (0xRRGGBB).
constexpr uint32_t Unique  = 0xC7B377;   // the game's unique-item gold (199,179,119)
constexpr uint32_t SetName = 0x4ED24E;   // the game's set-item green
constexpr uint32_t Txt     = 0xCDCDC6;   // the game's light grey labels (205,205,198)
constexpr uint32_t DimT    = 0x787874;   // (120,120,116)
constexpr uint32_t Gold    = 0x967A46;   // the thin gold rule of the stash's inner frames (150,122,70)
constexpr uint32_t GoldHi  = 0xCDAA64;   // (205,170,100)
constexpr uint32_t OnText  = 0xEEEEE8, OffText = 0xA0A09B;   // a chosen / an idle button's word
constexpr uint32_t Blue    = 0x6969FF, White = 0xFFFFFF, Grey = 0xAAAAAA;   // the tooltip's lines

// Small capitals like the game's labels: Cinzel (the Section style's family) at the
// mock-up's size, scaled.
constexpr TextStyle Caps = TextStyle::Section;
constexpr TextStyle Plain = TextStyle::Value;

RectF R(float x0, float y0, float x1, float y1) { return {Mx(x0), My(y0), Md(x1 - x0), Md(y1 - y0)}; }
RectF Box(const panel::Rect& r) { return {r.x, r.y, r.width, r.height}; }

// ---- the stash's stone: the patch mirror-tiled (the mock-up's tile), in mock-up pixels ----
uint32_t StoneAt(int x, int y) {   // straight RGB of the tile at mock-up pixel x, y
    const int w = art::StoneWidth, h = art::StoneHeight;
    x = ((x % (2 * w)) + 2 * w) % (2 * w);
    y = ((y % (2 * h)) + 2 * h) % (2 * h);
    if (x >= w) x = 2 * w - 1 - x;
    if (y >= h) y = 2 * h - 1 - y;
    const uint8_t* p = &art::Stone[(size_t(y) * w + size_t(x)) * 4];
    return (uint32_t(p[0]) << 16) | (uint32_t(p[1]) << 8) | p[2];
}

// Stone of w x h canvas pixels at the given brightness, from the tile's origin (as the
// mock-up's stone() does for each button). Kept by size and brightness.
const std::vector<uint8_t>& StonePixels(int w, int h, float bright) {
    thread_local std::unordered_map<uint64_t, std::vector<uint8_t>> cache;
    const uint64_t key = (uint64_t(w) << 40) | (uint64_t(h) << 20) | uint64_t(bright * 1000.f);
    auto& out = cache[key];
    if (out.empty()) {
        out.resize(size_t(w) * size_t(h) * 4);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                const uint32_t c = StoneAt(int(float(x) / panel::Scale), int(float(y) / panel::Scale));
                uint8_t* o = &out[(size_t(y) * w + size_t(x)) * 4];
                o[0] = uint8_t(std::min(255.f, float((c >> 16) & 255) * bright));
                o[1] = uint8_t(std::min(255.f, float((c >> 8) & 255) * bright));
                o[2] = uint8_t(std::min(255.f, float(c & 255) * bright));
                o[3] = 255;
            }
    }
    return out;
}

void StoneFill(Painter& p, const RectF& r, float bright) {
    const int w = std::max(1, int(std::lround(r.w))), h = std::max(1, int(std::lround(r.h)));
    p.Image(r, StonePixels(w, h, bright).data(), w, h);
}

// The mock-up's button: stone, lighter when on, with its dark outline and bevel.
void Button(Painter& p, const RectF& r, bool on) {
    StoneFill(p, r, on ? 1.18f : 0.62f);
    const float t = Md(1.f);
    p.OutlineRgb(r, 0x080808, t);
    p.LineRgb(r.x + t, r.y + t * 1.5f, r.Right() - t, r.y + t * 1.5f, on ? 0x6E6E68 : 0x4E4E4A, t);
    p.LineRgb(r.x + t * 1.5f, r.y + t, r.x + t * 1.5f, r.Bottom() - t, on ? 0x60605A : 0x42423E, t);
    p.LineRgb(r.x + t, r.Bottom() - t * 1.5f, r.Right() - t, r.Bottom() - t * 1.5f, 0x141414, t);
    p.LineRgb(r.Right() - t * 1.5f, r.y + t, r.Right() - t * 1.5f, r.Bottom() - t, 0x141414, t);
}

// Small-caps label centred on the mock-up point (cx, cy) at its size.
void CapsAt(Painter& p, float cx, float cy, std::string_view text, float size, uint32_t rgb) {
    const float s = Md(size);
    p.TextRgb(text, Caps, s, {Mx(cx) - Md(150.f), My(cy) - s, Md(300.f), s * 2.f}, rgb, Align::Center);
}

// The largest size, from the mock-up's start down to its floor, at which the text fits.
float FitSize(Painter& p, std::string_view text, TextStyle style, float size, float floor, float width) {
    for (; size > floor; size -= 0.5f)
        if (p.Measure(text, style, 4096.f, false, 0, Md(size)).width <= width) break;
    return size;
}

// ---- the item pictures ----
RectF FitImage(const RectF& box, int width, int height) {
    if (width <= 0 || height <= 0 || box.w <= 0 || box.h <= 0) return box;
    const float s = std::min(box.w / float(width), box.h / float(height));
    const float w = float(width) * s, h = float(height) * s;
    return {box.x + (box.w - w) * 0.5f, box.y + (box.h - h) * 0.5f, w, h};
}

// A missing item: grey and dark, a little see-through (the mock-up's ghost()).
const std::vector<uint8_t>& Ghost(const std::string& key, const IconPixels& icon) {
    thread_local std::unordered_map<std::string, std::vector<uint8_t>> cache;
    auto& out = cache[key];
    if (out.size() != size_t(icon.width) * size_t(icon.height) * 4) {
        out.resize(size_t(icon.width) * size_t(icon.height) * 4);
        for (size_t i = 0; i < out.size(); i += 4) {
            const float l = 0.299f * icon.rgba[i] + 0.587f * icon.rgba[i + 1] + 0.114f * icon.rgba[i + 2];
            out[i] = out[i + 1] = out[i + 2] = uint8_t(std::min(255.f, l * 0.38f));
            out[i + 3] = uint8_t(icon.rgba[i + 3] * 0.8f);
        }
    }
    return out;
}

// An item-type tile's silhouette: the icon's shape in one flat grey (on / off).
const std::vector<uint8_t>& Silhouette(const std::string& key, const IconPixels& icon, uint32_t rgb) {
    thread_local std::unordered_map<std::string, std::vector<uint8_t>> cache;
    auto& out = cache[key + "/" + std::to_string(rgb)];
    if (out.size() != size_t(icon.width) * size_t(icon.height) * 4) {
        out.resize(size_t(icon.width) * size_t(icon.height) * 4);
        for (size_t i = 0; i < out.size(); i += 4) {
            out[i] = uint8_t(rgb >> 16); out[i + 1] = uint8_t(rgb >> 8); out[i + 2] = uint8_t(rgb);
            out[i + 3] = icon.rgba[i + 3];
        }
    }
    return out;
}

// A runeword's recipe ("r08 r09 r07") in the runes' own names ("Ral Ort Tal").
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

uint32_t NameColor(Category category) { return category == Category::Sets ? SetName : Unique; }

// ---- one card (the mock-up's card loop) ----
void DrawCard(Painter& p, const panel::Rect& at, const Entry& entry, Category category, bool found, bool pointed,
              const IconLookup& icons, bool plate) {
    // back to mock-up pixels: everything below is the mock-up's own arithmetic
    const float x0 = at.x / panel::Scale + panel::MockLeft, y0 = at.y / panel::Scale + panel::MockTop;
    const float x1 = x0 + at.width / panel::Scale, y1 = y0 + at.height / panel::Scale;
    p.FillRgb(R(x0, y0, x1, y1), 0x0E0E0E, 235.f / 255.f);
    p.OutlineRgb(R(x0, y0, x1, y1), 0x060606, Md(2.f));
    p.LineRgb(Mx(x0 + 2), My(y1 - 1), Mx(x1 - 1), My(y1 - 1), 0x464642, Md(1.f));
    p.LineRgb(Mx(x1 - 1), My(y0 + 2), Mx(x1 - 1), My(y1 - 1), 0x464642, Md(1.f));
    if (found) {
        p.OutlineRgb(R(x0 + 3, y0 + 3, x1 - 3, y1 - 3), Gold, Md(1.f));
        p.OutlineRgb(R(x0 + 5, y0 + 5, x1 - 5, y1 - 5), 0x463822, Md(1.f));
    }
    if (pointed) p.OutlineRgb(R(x0 + 2, y0 + 2, x1 - 2, y1 - 2), GoldHi, Md(2.f));
    // the name plate; not on the small cards of size L, where a name does not fit at a size
    // still readable (owner 2026-11-20): there the picture takes the card and the name is
    // in the tooltip
    float py1 = y0;
    if (plate) {
        py1 = y0 + 26;
        p.FillRgb(R(x0 + 7, y0 + 7, x1 - 7, py1), 0x1E1E1D);
        p.LineRgb(Mx(x0 + 7), My(py1), Mx(x1 - 7), My(py1), 0x383834, Md(1.f));
        const float size = FitSize(p, entry.name, Caps, 13.f, 9.f, Md(x1 - x0 - 20));
        CapsAt(p, (x0 + x1) / 2, (y0 + 7 + py1) / 2 + 1, entry.name, size, found ? NameColor(category) : DimT);
    }
    // the picture, scaled to the card
    const RectF area = R(x0 + 12, py1 + 8, x1 - 12, y1 - 8);
    const std::string key    = panel::IconKey(category, entry.recordId, entry.code);
    const IconPixels* pixels = icons && !key.empty() ? icons(key) : nullptr;
    if (pixels != nullptr && pixels->rgba != nullptr) {
        const RectF fit = FitImage(area, pixels->width, pixels->height);
        if (found) p.Image(fit, pixels->rgba, pixels->width, pixels->height);
        else       p.Image(fit, Ghost(key, *pixels).data(), pixels->width, pixels->height);
    } else if (!entry.extra.empty()) {
        // a runeword is no item until it is made: its runes stand for the picture
        p.TextRgb(RuneNames(entry.extra), Caps, Md(13.f), area, found ? GoldHi : DimT, Align::Center, true);
    }
}

// ---- the tooltip in the game's own style (the mock-up's: black, every line centred) ----
// The base item's own lines are white, its magic properties blue (as the game draws them):
// a base line starts with its label ("Defense: 141"), a property does not ("+141% Enhanced Defense").
bool BaseLine(const std::string& line) {
    for (const char* label : {"Defense:", "One-Hand Damage:", "Two-Hand Damage:", "Throw Damage:", "Kick Damage:",
                              "Smite Damage:", "Durability:", "Required ", "Chance to Block:", "Quantity:", "Item Level:"})
        if (line.rfind(label, 0) == 0) return true;
    return false;
}

void DrawTooltip(Painter& p, const panel::Rect& card, const tooltip::Tooltip& tip, Category category, const std::string& base) {
    if (tip.Empty()) return;
    struct Line { std::string text; uint32_t rgb; float size; };
    std::vector<Line> lines;
    lines.push_back({tip.title, NameColor(category), 16.f});
    // the kind of item, grey, between the name and its text (owner: Harlequin Crest - Shako)
    if (!base.empty()) lines.push_back({base, Grey, 13.f});
    for (const std::string& line : tip.body) {
        if (line == tip.title || (!base.empty() && line == base)) continue;   // the game's text repeats both
        lines.push_back({line.empty() ? " " : line, BaseLine(line) ? White : Blue, 13.f});
    }
    if (!tip.ranges.empty()) {
        lines.push_back({"Possible ranges", Grey, 12.f});
        for (const std::string& line : tip.ranges) lines.push_back({line, Grey, 12.f});
    }
    if (!tip.subtitle.empty()) lines.push_back({tip.subtitle, Grey, 12.f});
    constexpr float tw = 250.f, step = 21.f;
    const float th = 14.f + step * float(lines.size());
    // beside the card, in mock-up pixels; to its left when the right would leave the page
    const float cx0 = card.x / panel::Scale + panel::MockLeft, cy0 = card.y / panel::Scale + panel::MockTop;
    const float cw  = card.width / panel::Scale;
    float tx = cx0 + cw + 8, ty = cy0 + 20;
    if (tx + tw > panel::MockRight - 10) tx = cx0 - tw - 8;
    tx = std::max(panel::MockLeft + 10, tx);
    ty = std::min(ty, panel::MockBottom - 10 - th);
    p.FillRgb(R(tx, ty, tx + tw, ty + th), 0x000000, 215.f / 255.f);
    for (size_t i = 0; i < lines.size(); ++i) {
        const float size = FitSize(p, lines[i].text, Plain, lines[i].size, 9.f, Md(tw - 12));
        p.TextRgb(lines[i].text, Plain, Md(size), R(tx + 4, ty + 7 + float(i) * step, tx + tw - 4, ty + 7 + float(i + 1) * step),
                  lines[i].rgb, Align::Center);
    }
}

}   // namespace

void Paint(Painter& painter, const Library& library, const ViewState& view, const IconLookup& icons,
           std::string_view gameText, std::string_view rangeText, const Carried& carried) {
    Painter& p = painter;
    const auto          visible = VisibleSlots(library, view);
    const int           pages   = panel::PagesFor(int(visible.size()), view.size);
    const panel::Layout layout  = panel::Build(view.page, pages, int(TabCategoryCount), view.size, view.open);
    const auto pointed = [&](panel::HitKind kind, int index) { return view.hover.kind == kind && view.hover.index == index; };
    constexpr float PX0 = panel::MockLeft, PY0 = panel::MockTop, PX1 = panel::MockRight, PY1 = panel::MockBottom;

    // The page: the stash's stone (0.92), the game's thin gold double rule round the content.
    StoneFill(p, {0, 0, panel::CanvasWidth, panel::CanvasHeight}, 0.92f);
    p.OutlineRgb(R(PX0 + 14, PY0 + 8, PX1 - 14, PY1 - 10), 0x28221A, Md(1.f));
    p.OutlineRgb(R(PX0 + 15, PY0 + 9, PX1 - 15, PY1 - 11), Gold, Md(1.f));
    p.OutlineRgb(R(PX0 + 18, PY0 + 12, PX1 - 18, PY1 - 14), 0x1C1814, Md(1.f));

    // Uniques / Sets / Runewords: the name in small caps, the count under it.
    for (int i = 0; i < layout.tabCount; ++i) {
        const Category category = static_cast<Category>(i);
        const bool     on       = category == view.category;
        const RectF    r        = Box(layout.tabs[i]);
        Button(p, r, on);
        if (pointed(panel::HitKind::Tab, i)) p.OutlineRgb(r, GoldHi, Md(1.5f));
        const float kx = r.x / panel::Scale + PX0, ky = r.y / panel::Scale + PY0;
        CapsAt(p, kx + 93, ky + 15, CategoryName(category), 16.f, on ? OnText : OffText);
        const std::string count = std::to_string(library.Found(category)) + "/" + std::to_string(library.Size(category));
        p.TextRgb(count, Plain, Md(11.f), R(kx, ky + 24, kx + 186, ky + 38), on ? 0xBEBEB8 : DimT, Align::Center);
    }

    // The cards.
    for (int slot = 0; slot < layout.slotCount; ++slot) {
        const Entry* entry = SlotEntry(library, view, visible, slot);
        if (entry == nullptr) {   // past the last entry: an empty recess, so the grid reads full
            const RectF r = Box(layout.slots[slot]);
            p.FillRgb(r, 0x0E0E0E, 0.55f);
            p.OutlineRgb(r, 0x060606, Md(2.f));
            continue;
        }
        DrawCard(p, layout.slots[slot], *entry, view.category, SlotFound(library, view, visible, slot),
                 pointed(panel::HitKind::Slot, slot), icons, view.size != panel::Size::Large);
    }

    // The scroll bar: the dark track, a stone arrow at each end, the lit stone thumb.
    {
        p.FillRgb(Box(layout.scroll), 0x0C0C0C);
        p.OutlineRgb(Box(layout.scroll), 0x3C3C38, Md(1.f));
        for (const bool up : {true, false}) {
            const RectF r = Box(up ? layout.previous : layout.next);
            Button(p, r, false);
            if (pointed(up ? panel::HitKind::Previous : panel::HitKind::Next, 0)) p.OutlineRgb(r, GoldHi, Md(1.f));
            p.IconAt(up ? settings::draw::Icon::ChevronUp : settings::draw::Icon::ChevronDown,
                     {r.x + Md(2.f), r.y + Md(2.f), r.w - Md(4.f), r.h - Md(4.f)}, settings::draw::Color::Quiet);
        }
        const panel::Rect thumb = panel::Thumb(layout);
        Button(p, {thumb.x + Md(1.f), thumb.y, thumb.width - Md(1.f), thumb.height}, true);
    }

    // The found bar, and "Uniques  81 / 403" under it.
    {
        const RectF bar   = Box(layout.progress);
        const float share = float(library.Found(view.category)) / float(std::max<uint32_t>(1, library.Size(view.category)));
        p.FillRgb(bar, 0x0A0A0A);
        p.OutlineRgb(bar, 0x3C3C38, Md(1.f));
        p.FillRgb({bar.x + Md(2.f), bar.y + Md(2.f), (bar.w - Md(4.f)) * share, bar.h - Md(4.f)}, 0xA08448);
        CapsAt(p, (PX0 + PX1) / 2, layout.counter.y / panel::Scale + PY0 + 12,
               std::string(CategoryName(view.category)) + "  " + CounterText(library, view), 15.f, Txt);
    }

    // The item types: the silhouette (36 px, a ring or an amulet 22 - they are small items)
    // with the share found beside it; the chosen tile lit.
    {
        for (int group = 0; group < panel::GroupCount; ++group) {
            const RectF r  = Box(layout.groups[group]);
            const bool  on = view.group == group;
            Button(p, r, on);
            if (pointed(panel::HitKind::Group, group)) p.OutlineRgb(r, GoldHi, Md(1.5f));
            const float x0 = r.x / panel::Scale + PX0, y0 = r.y / panel::Scale + PY0;
            const float x1 = x0 + r.w / panel::Scale, cbh = r.h / panel::Scale;
            // the plainest unique of the tile's own type (lowest level), else of any of its types
            const Entry* best = nullptr;
            for (int pass = 0; pass < 2 && best == nullptr; ++pass)
                for (uint32_t slot = 0; slot < library.Size(Category::Uniques); ++slot) {
                    const Entry* e = library.At(Category::Uniques, slot);
                    if (e == nullptr || panel::GroupOfType(e->type) != group) continue;
                    if (pass == 0 && e->type != panel::GroupMainType(group)) continue;
                    if (best == nullptr || e->level < best->level) best = e;
                }
            if (best != nullptr && icons) {
                const std::string key = panel::IconKey(Category::Uniques, best->recordId, best->code);
                if (const IconPixels* icon = icons(key); icon != nullptr && icon->rgba != nullptr) {
                    const float most = (group == 6 || group == 7) ? 22.f : 36.f;
                    const float s = most / float(std::max(icon->width, icon->height));
                    const float w = float(icon->width) * s, h = float(icon->height) * s;
                    const float ix = (x0 + x1) / 2 - w / 2 - 18, iy = y0 + (cbh - h) / 2;
                    const uint32_t rgb = on ? 0xE1E1DA : 0xA5A5A0;
                    p.Image(R(ix, iy, ix + w, iy + h), Silhouette(key, *icon, rgb).data(), icon->width, icon->height);
                }
            }
            const auto share = ShareOf(library, view.category, group);
            const std::string percent = share.total ? std::to_string(share.found * 100 / share.total) + "%" : "-";
            p.TextRgb(percent, Plain, Md(15.f), R((x0 + x1) / 2 + 4, y0 + cbh / 2 - 11, x1 - 2, y0 + cbh / 2 + 11),
                      on ? OnText : 0xAFAFAA, Align::Leading);
        }
    }

    // Footer: Size and S / M / L; the Sort and Show lists.
    {
        const float by = layout.sizeLabel.y / panel::Scale + PY0;
        CapsAt(p, 141 + 26, by + 18, "Size", 15.f, Txt);
        for (int i = 0; i < int(panel::Size::Count); ++i) {
            const RectF r  = Box(layout.sizeSteps[i]);
            const bool  on = i == int(view.size);
            Button(p, r, on);
            if (pointed(panel::HitKind::Size, i)) p.OutlineRgb(r, GoldHi, Md(1.5f));
            CapsAt(p, r.x / panel::Scale + PX0 + 19, by + 18, panel::SizeName(static_cast<panel::Size>(i)), 15.f, on ? OnText : OffText);
        }
        const auto list = [&](const panel::Rect& boxRect, const char* label, const char* value, bool lit) {
            const RectF r = Box(boxRect);
            const float x0 = r.x / panel::Scale + PX0, x1 = x0 + r.w / panel::Scale;
            CapsAt(p, x0 - 29, by + 18, label, 13.f, Txt);   // 13: Cinzel's spacing is wider than the mock-up's font
            p.FillRgb(r, 0x0A0A0A);
            p.OutlineRgb(r, lit ? GoldHi : Gold, Md(1.f));
            p.TextRgb(value, Plain, Md(15.f), R(x0 + 10, by, x1 - 34, by + 36), 0xE6E6E0, Align::Leading);
            const RectF arrow = R(x1 - 32, by + 2, x1 - 2, by + 34);
            Button(p, arrow, false);
            p.IconAt(settings::draw::Icon::ChevronDown, {arrow.x + Md(5.f), arrow.y + Md(5.f), arrow.w - Md(10.f), arrow.h - Md(10.f)},
                     settings::draw::Color::Quiet);
        };
        list(layout.sortBox, "Sort", panel::SortName(view.sort),
             view.open == panel::Drop::Sort || pointed(panel::HitKind::SortBox, 0));
        list(layout.showBox, "Show", panel::ShowName(view.show),
             view.open == panel::Drop::Show || pointed(panel::HitKind::ShowBox, 0));
        // An open list: its options above its box, the chosen one in gold.
        const auto options = [&](const panel::Rect* rects, int count, int chosen, panel::HitKind kind, auto name) {
            for (int i = 0; i < count; ++i) {
                const RectF r = Box(rects[i]);
                p.FillRgb(r, pointed(kind, i) ? 0x2A2A28 : 0x0A0A0A, 0.97f);
                p.OutlineRgb(r, Gold, Md(1.f));
                const float x0 = r.x / panel::Scale + PX0, y0 = r.y / panel::Scale + PY0;
                p.TextRgb(name(i), Plain, Md(15.f), R(x0 + 10, y0, x0 + r.w / panel::Scale - 6, y0 + 36),
                          i == chosen ? Unique : 0xE6E6E0, Align::Leading);
            }
        };
        if (view.open == panel::Drop::Sort)
            options(layout.sortOptions, int(panel::Sort::Count), int(view.sort), panel::HitKind::Sort,
                    [](int i) { return panel::SortName(static_cast<panel::Sort>(i)); });
        if (view.open == panel::Drop::Show)
            options(layout.showOptions, int(panel::Show::Count), int(view.show), panel::HitKind::Show,
                    [](int i) { return panel::ShowName(static_cast<panel::Show>(i)); });
    }

    // The pointed card's tooltip (not with an item in hand: the item is what is looked at then).
    if (view.hover.kind == panel::HitKind::Slot && carried.key.empty()) {
        if (const Entry* entry = SlotEntry(library, view, visible, view.hover.index)) {
            const bool found = SlotFound(library, view, visible, view.hover.index);
            DrawTooltip(p, layout.slots[view.hover.index], tooltip::BuildTooltip(*entry, found, gameText, rangeText),
                        view.category, entry->base);
        }
    }

    // The item in the hand, at the laser, over everything (as the game draws a carried item):
    // at the size it has in the stash (an inventory cell is 57 UI pixels; the icons are the
    // game's own art at 98 per cell), centred on the pointer.
    if (!carried.key.empty() && carried.x >= 0 && icons) {
        if (const IconPixels* icon = icons(carried.key); icon != nullptr && icon->rgba != nullptr) {
            const float s = Md(57.f / 98.f);
            const float w = float(icon->width) * s, h = float(icon->height) * s;
            p.Image({carried.x - w / 2, carried.y - h / 2, w, h}, icon->rgba, icon->width, icon->height);
        }
    }
}

}   // namespace framevr::collection::draw
