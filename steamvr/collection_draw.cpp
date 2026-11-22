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
#include <array>
#include <cmath>
#include <cstring>
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

// A set's name on its small tile: the owner's part before "'s" ("Tal Rasha's
// Wrappings" -> "Tal Rasha", "Bul-Kathos' Children" -> "Bul-Kathos"), else whole.
std::string ShortSetName(std::string_view name) {
    for (std::string_view mark : {std::string_view("'s "), std::string_view("' ")})
        if (const size_t at = name.find(mark); at != std::string_view::npos && at > 2) return std::string(name.substr(0, at));
    return std::string(name);
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

// A silhouette turned by deg degrees about its middle (nearest pixel), for the runeword cards'
// crossed sword and axe. Kept by key, colour and angle.
struct Turned { std::vector<uint8_t> px; int w = 0, h = 0; };
const Turned& TurnedSilhouette(const std::string& key, const IconPixels& icon, uint32_t rgb, int deg);

// What a runeword can be made in, by its item types (runes.txt itype1..6): the card draws one
// grey picture for each kind (owner 2026-11-22: melee a crossed sword and axe, a bow beside it
// when missile weapons take it too).
enum Kind { Melee, Missile, Armor, Helm, Shield, Kinds };
std::array<bool, Kinds> KindsOf(std::string_view types) {
    std::array<bool, Kinds> out {};
    size_t at = 0;
    while (at < types.size()) {
        size_t end = types.find(' ', at);
        if (end == std::string_view::npos) end = types.size();
        const std::string_view t = types.substr(at, end - at);
        if (t == "weap") out[Melee] = out[Missile] = true;
        else if (t == "miss" || t == "bow" || t == "xbow") out[Missile] = true;
        else if (t == "tors") out[Armor] = true;
        else if (t == "helm" || t == "circ" || t == "pelt" || t == "phlm") out[Helm] = true;
        else if (t == "shld" || t == "ashd" || t == "head" || t == "pala" || t == "grim" || t == "shie") out[Shield] = true;
        else if (!t.empty()) out[Melee] = true;   // swords, axes, maces, polearms, staves, claws, wands ...
        at = end + 1;
    }
    return out;
}
// The item-type tiles' pictures (the plainest unique of each type), set by Paint.
std::array<std::string, panel::GroupCount> g_groupKeys;

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
        // a runeword is no item until it is made: its runes stand for the picture, and under them
        // what it can be made in, in grey (owner 2026-11-22)
        const auto kinds = KindsOf(entry.base);
        int count = 0;
        for (bool k : kinds) count += k ? 1 : 0;
        const float split = count > 0 && icons ? 0.45f : 1.0f;
        const RectF text = {area.x, area.y, area.w, area.h * split};
        p.TextRgb(RuneNames(entry.extra), Caps, Md(13.f), text, found ? GoldHi : DimT, Align::Center, true);
        if (split < 1.0f) {
            const uint32_t rgb = found ? 0x8C8C86 : 0x55554F;
            const float top = area.y + area.h * split, h = area.h * (1.0f - split);
            const float s = std::min(h, area.w / float(count));
            float x = area.x + (area.w - s * float(count)) * 0.5f;
            const auto draw = [&](int group, int deg, float scale) {
                const std::string& key = g_groupKeys[size_t(group)];
                const IconPixels* icon = key.empty() ? nullptr : icons(key);
                if (icon == nullptr || icon->rgba == nullptr) return;
                const Turned& t = TurnedSilhouette(key, *icon, rgb, deg);
                const RectF box = {x + s * (1 - scale) * 0.5f, top + (h - s * scale) * 0.5f, s * scale, s * scale};
                p.Image(FitImage(box, t.w, t.h), t.px.data(), t.w, t.h);
            };
            const int groupOf[Kinds] = {9, 10, 1, 0, 2};   // swords, bows, armor, helms, shields
            for (int k = 0; k < Kinds; ++k) {
                if (!kinds[size_t(k)]) continue;
                if (k == Melee) { draw(9, 45, 1.3f); draw(8, -45, 1.3f); }   // the sword and the axe crossed
                else draw(groupOf[k], 0, 0.8f);
                x += s;
            }
        }
    }
}

const Turned& TurnedSilhouette(const std::string& key, const IconPixels& icon, uint32_t rgb, int deg) {
    thread_local std::unordered_map<std::string, Turned> cache;
    Turned& out = cache[key + "/" + std::to_string(rgb) + "/" + std::to_string(deg)];
    if (!out.px.empty()) return out;
    const std::vector<uint8_t>& flat = Silhouette(key, icon, rgb);
    const float a = float(deg) * 3.14159265f / 180.f, c = std::cos(a), s = std::sin(a);
    out.w = int(std::ceil(std::fabs(icon.width * c) + std::fabs(icon.height * s)));
    out.h = int(std::ceil(std::fabs(icon.width * s) + std::fabs(icon.height * c)));
    out.px.assign(size_t(out.w) * size_t(out.h) * 4, 0);
    const float cx = out.w * 0.5f, cy = out.h * 0.5f, ix = icon.width * 0.5f, iy = icon.height * 0.5f;
    for (int y = 0; y < out.h; ++y)
        for (int x = 0; x < out.w; ++x) {
            const float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
            const int sx = int(std::floor(c * dx + s * dy + ix)), sy = int(std::floor(-s * dx + c * dy + iy));
            if (sx < 0 || sy < 0 || sx >= icon.width || sy >= icon.height) continue;
            std::memcpy(&out.px[(size_t(y) * out.w + x) * 4], &flat[(size_t(sy) * icon.width + sx) * 4], 4);
        }
    return out;
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

// A stat line with its roll ranges ("(5-10)", "(1.5 per Level)") in their own colour, as the
// game's Chronicle shows them (owner 2026-11-22): the line centred in the box, each piece placed
// at the width of the text before it.
constexpr uint32_t RangeColor = 0x7CDDCC;   // provisional: the profile's FontColorLightTeal
void RichLine(Painter& p, const std::string& text, float size, const RectF& box, uint32_t rgb) {
    struct Piece { size_t at, length; bool range; };
    std::vector<Piece> pieces;
    size_t from = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '(') continue;
        const size_t close = text.find(')', i);
        if (close == std::string::npos) break;
        const std::string inner = text.substr(i + 1, close - i - 1);
        const bool range = inner.find_first_of("0123456789") != std::string::npos &&
                           (inner.find('-', 1) != std::string::npos || inner.find(" to ") != std::string::npos ||
                            inner.find("per Level") != std::string::npos);
        if (!range) continue;
        if (i > from) pieces.push_back({from, i - from, false});
        pieces.push_back({i, close + 1 - i, true});
        from = close + 1;
        i = close;
    }
    if (pieces.empty()) { p.TextRgb(text, Plain, size, box, rgb, Align::Center); return; }
    if (from < text.size()) pieces.push_back({from, text.size() - from, false});
    // widths measured with a marker after them, so a trailing space counts
    const auto width = [&](std::string_view s) {
        return p.Measure(std::string(s) + "|", Plain, 4096.f, false, 0, size).width - p.Measure("|", Plain, 4096.f, false, 0, size).width;
    };
    const float total = width(text);
    const float left = box.x + (box.w - total) * 0.5f;
    for (const Piece& piece : pieces) {
        const float x = left + width(std::string_view(text).substr(0, piece.at));
        p.TextRgb(text.substr(piece.at, piece.length), Plain, size, {x, box.y, box.w * 2, box.h}, piece.range ? RangeColor : rgb,
                  Align::Leading);
    }
}

void DrawTooltip(Painter& p, const panel::Rect& card, const tooltip::Tooltip& tip, Category category, const std::string& base) {
    if (tip.Empty()) return;
    // 10% and then 20% larger than the first cut, box and text alike (owner 2026-11-22: the game's
    // own tooltips are much bigger)
    constexpr float K = 1.1f * 1.2f;
    struct Line { std::string text; uint32_t rgb; float size; };
    std::vector<Line> lines;
    lines.push_back({tip.title, NameColor(category), 16.f * K});
    // the kind of item, grey, between the name and its text (owner: Harlequin Crest - Shako)
    if (!base.empty()) lines.push_back({base, Grey, 13.f * K});
    for (const std::string& line : tip.body) {
        if (line == tip.title || (!base.empty() && line == base)) continue;   // the game's text repeats both
        lines.push_back({line.empty() ? " " : line, BaseLine(line) ? White : Blue, 13.f * K});
    }
    if (!tip.ranges.empty()) {
        lines.push_back({"Possible ranges", Grey, 12.f * K});
        for (const std::string& line : tip.ranges) lines.push_back({line, Grey, 12.f * K});
    }
    if (!tip.subtitle.empty()) lines.push_back({tip.subtitle, Grey, 12.f * K});
    constexpr float tw = 250.f * K, step = 21.f * K;
    const float th = 14.f * K + step * float(lines.size());
    // beside the card, in mock-up pixels; to its left when the right would leave the page
    const float cx0 = card.x / panel::Scale + panel::MockLeft, cy0 = card.y / panel::Scale + panel::MockTop;
    const float cw  = card.width / panel::Scale;
    float tx = cx0 + cw + 8, ty = cy0 + 20;
    if (tx + tw > panel::MockRight - 10) tx = cx0 - tw - 8;
    tx = std::max(panel::MockLeft + 10, tx);
    ty = std::max(panel::MockTop + 10, std::min(ty, panel::ContentBottom - 10 - th));   // above the tab plaques
    p.FillRgb(R(tx, ty, tx + tw, ty + th), 0x000000, 215.f / 255.f);
    for (size_t i = 0; i < lines.size(); ++i) {
        const float size = FitSize(p, lines[i].text, Plain, lines[i].size, 9.f * K, Md(tw - 12 * K));
        RichLine(p, lines[i].text, Md(size), R(tx + 4 * K, ty + 7 * K + float(i) * step, tx + tw - 4 * K, ty + 7 * K + float(i + 1) * step),
                 lines[i].rgb);
    }
}

}   // namespace

void Paint(Painter& painter, const Library& library, const ViewState& view, const IconLookup& icons,
           std::string_view gameText, std::string_view rangeText, const Carried& carried) {
    Painter& p = painter;
    const auto          visible = VisibleSlots(library, view);
    const int           pages   = panel::PagesFor(int(visible.size()), view.size);
    panel::Layout layout = panel::Build(view.page, pages, int(TabCategoryCount), view.size, view.open);
    const bool    onSets = view.category == Category::Sets;
    if (onSets) panel::AddBands(layout);
    if (view.category == Category::Runewords) panel::UseKindRows(layout);
    const auto pointed = [&](panel::HitKind kind, int index) { return view.hover.kind == kind && view.hover.index == index; };
    constexpr float PX0 = panel::MockLeft, PY0 = panel::MockTop, PX1 = panel::MockRight, PY1 = panel::MockBottom;

    // The page: the stash's stone (0.92), the game's thin gold double rule round the content.
    StoneFill(p, {0, 0, panel::CanvasWidth, panel::CanvasHeight}, 0.92f);
    p.OutlineRgb(R(PX0 + 14, PY0 + 8, PX1 - 14, PY1 - 10), 0x28221A, Md(1.f));
    p.OutlineRgb(R(PX0 + 15, PY0 + 9, PX1 - 15, PY1 - 11), Gold, Md(1.f));
    p.OutlineRgb(R(PX0 + 18, PY0 + 12, PX1 - 18, PY1 - 14), 0x1C1814, Md(1.f));

    // Uniques / Sets / Runewords: the name in small caps (the count is the one under the cards -
    // the same number twice, owner 2026-11-22).
    for (int i = 0; i < layout.tabCount; ++i) {
        const Category category = static_cast<Category>(i);
        const bool     on       = category == view.category;
        const RectF    r        = Box(layout.tabs[i]);
        Button(p, r, on);
        if (pointed(panel::HitKind::Tab, i)) p.OutlineRgb(r, GoldHi, Md(1.5f));
        const float kx = r.x / panel::Scale + PX0, ky = r.y / panel::Scale + PY0;
        CapsAt(p, kx + 93, ky + 20, CategoryName(category), 16.f, on ? OnText : OffText);
    }
    // The item-type tiles' pictures, also the runeword cards' (DrawCard).
    for (int group = 0; group < panel::GroupCount; ++group) {
        const Entry* best = nullptr;
        for (int pass = 0; pass < 2 && best == nullptr; ++pass)
            for (uint32_t slot = 0; slot < library.Size(Category::Uniques); ++slot) {
                const Entry* e = library.At(Category::Uniques, slot);
                if (e == nullptr || panel::GroupOfType(e->type) != group) continue;
                if (pass == 0 && e->type != panel::GroupMainType(group)) continue;
                if (best == nullptr || e->level < best->level) best = e;
            }
        g_groupKeys[size_t(group)] = best ? panel::IconKey(Category::Uniques, best->recordId, best->code) : std::string();
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
        // In the category's own colour (owner 2026-11-20): unique gold, set green, runeword grey.
        const uint32_t fill = view.category == Category::Sets      ? SetName
                            : view.category == Category::Runewords ? Grey
                                                                   : Unique;
        p.FillRgb({bar.x + Md(2.f), bar.y + Md(2.f), (bar.w - Md(4.f)) * share, bar.h - Md(4.f)}, fill);
        if (!onSets) {
            CapsAt(p, (PX0 + PX1) / 2, layout.counter.y / panel::Scale + PY0 + 12,
                   std::string(CategoryName(view.category)) + "  " + CounterText(library, view), 15.f, Txt);
        } else {
            // "Sets 12 / 135   Complete 1 / 34" left of the four level-range buttons.
            const auto all      = AllSets(library);
            const auto complete = std::count_if(all.begin(), all.end(), [](const SetInfo& s) { return s.total && s.found == s.total; });
            const float left = layout.counter.x / panel::Scale + PX0, right = panel::BandsLeft(layout) / panel::Scale + PX0;
            CapsAt(p, (left + right) / 2, layout.counter.y / panel::Scale + PY0 + 12,
                   "Sets  " + std::to_string(library.Found(Category::Sets)) + " / " + std::to_string(library.Size(Category::Sets)) +
                       "    Complete  " + std::to_string(complete) + " / " + std::to_string(all.size()),
                   14.f, Txt);
            for (int band = 0; band < panel::BandCount; ++band) {
                const RectF r  = Box(layout.bands[band]);
                const bool  on = view.band == band;
                Button(p, r, on);
                if (pointed(panel::HitKind::Band, band)) p.OutlineRgb(r, GoldHi, Md(1.5f));
                CapsAt(p, (r.x + r.w / 2) / panel::Scale + PX0, (r.y + r.h / 2) / panel::Scale + PY0 + 1, BandName(band), 13.f,
                       on ? OnText : OffText);
            }
        }
    }

    // Sets: the band's sets, a tile each - the name over a dot a piece (green found),
    // all gold with a gold frame once the set is complete; the picked one lit.
    if (onSets) {
        const auto sets = SetsInBand(library, view.band);
        for (int tile = 0; tile < panel::GroupCount; ++tile) {
            const RectF r = Box(layout.groups[tile]);
            if (tile >= int(sets.size())) {
                p.FillRgb(r, 0x0E0E0E, 0.55f);
                p.OutlineRgb(r, 0x060606, Md(2.f));
                continue;
            }
            const SetInfo& set      = sets[size_t(tile)];
            const bool     on       = view.group == tile;
            const bool     complete = set.total && set.found == set.total;
            Button(p, r, on);
            if (complete) p.OutlineRgb(r, Gold, Md(1.5f));
            if (pointed(panel::HitKind::Group, tile)) p.OutlineRgb(r, GoldHi, Md(1.5f));
            const float x0 = r.x / panel::Scale + PX0, y0 = r.y / panel::Scale + PY0;
            const float tw = r.w / panel::Scale;
            // the whole name when it fits at 10 px, else the owner's part
            std::string name(set.name);
            float       size = FitSize(p, name, Plain, 13.f, 10.f, Md(tw - 6.f));
            if (p.Measure(name, Plain, 4096.f, false, 0, Md(size)).width > Md(tw - 6.f)) {
                name = ShortSetName(set.name);
                size = FitSize(p, name, Plain, 13.f, 8.f, Md(tw - 6.f));
            }
            p.TextRgb(name, Plain, Md(size), R(x0 + 2, y0 + 2, x0 + tw - 2, y0 + 21),
                      complete ? Unique : on ? OnText : Txt, Align::Center);
            constexpr float dot = 6.f, gap = 3.f;
            const float row = float(set.total) * dot + float(std::max<uint32_t>(1, set.total) - 1) * gap;
            float dx = x0 + (tw - row) / 2;
            const float dy = y0 + 25.f;
            for (uint32_t piece = 0; piece < set.total; ++piece, dx += dot + gap) {
                const bool have = piece < set.found;
                p.FillRgb(R(dx, dy, dx + dot, dy + dot), complete ? GoldHi : have ? SetName : 0x1A1A18);
                if (!have) p.OutlineRgb(R(dx, dy, dx + dot, dy + dot), 0x5E5E58, Md(1.f));
            }
        }
    }
    // Runewords: what they are made in on top, the kinds of the chosen one in the two rows
    // under it (empty recesses when it has none); each tile its name and found / all.
    else if (view.category == Category::Runewords) {
        const auto tileAt = [&](const panel::Rect& rect, const RunewordTile& tile, bool on, bool lit) {
            const RectF r = Box(rect);
            Button(p, r, on);
            if (lit) p.OutlineRgb(r, GoldHi, Md(1.5f));
            const float x0 = r.x / panel::Scale + PX0, y0 = r.y / panel::Scale + PY0;
            const float tw = r.w / panel::Scale, th = r.h / panel::Scale;
            const GroupShare share = RunewordShare(library, tile.accepts);
            const std::string count = std::to_string(share.found) + "/" + std::to_string(share.total);
            const float countW = p.Measure(count, Plain, 4096.f, false, 0, Md(11.f)).width / panel::Scale;
            const float nameW  = tw - countW - 15.f;
            const float size   = FitSize(p, tile.name, Plain, 13.f, 8.f, Md(nameW));
            const bool  done   = share.total && share.found == share.total;
            p.TextRgb(tile.name, Plain, Md(size), R(x0 + 4, y0, x0 + 4 + nameW, y0 + th), on ? OnText : Txt, Align::Leading);
            p.TextRgb(count, Plain, Md(11.f), R(x0 + tw - countW - 5, y0, x0 + tw - 3, y0 + th),
                      done ? Unique : on ? OnText : 0x9A9A94, Align::Leading);
        };
        for (int kind = 0; kind < panel::KindCount; ++kind)
            tileAt(layout.kinds[kind], RunewordKinds[kind], view.kind == kind, pointed(panel::HitKind::Kind, kind));
        const int subs = RunewordSubCount(view.kind);
        for (int sub = 0; sub < panel::GroupCount; ++sub) {
            if (sub >= subs) {
                const RectF r = Box(layout.groups[sub]);
                p.FillRgb(r, 0x0E0E0E, 0.55f);
                p.OutlineRgb(r, 0x060606, Md(2.f));
                continue;
            }
            tileAt(layout.groups[sub], *RunewordSub(view.kind, sub), view.group == sub, pointed(panel::HitKind::Group, sub));
        }
    }
    // The item types: the silhouette (36 px, a ring or an amulet 22 - they are small items)
    // with the share found beside it; the chosen tile lit.
    else {
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
                    const float most = std::min((group == 6 || group == 7) ? 22.f : 36.f, cbh - 6.f);   // the tiles are 38 tall now
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
        // Runewords are made, not found (owner 2026-11-22): the Show list reads All / Missing / Made.
        const auto showName = [&](panel::Show show) {
            return view.category == Category::Runewords && show == panel::Show::Found ? "Made" : panel::ShowName(show);
        };
        list(layout.showBox, "Show", showName(view.show),
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
                    [&](int i) { return showName(static_cast<panel::Show>(i)); });
    }

    // The pointed card's tooltip (not with an item in hand: the item is what is looked at then).
    if (view.hover.kind == panel::HitKind::Slot && carried.key.empty()) {
        if (const Entry* entry = SlotEntry(library, view, visible, view.hover.index)) {
            const bool found = SlotFound(library, view, visible, view.hover.index);
            DrawTooltip(p, layout.slots[view.hover.index], tooltip::BuildTooltip(*entry, found, gameText, rangeText),
                        view.category, view.category == Category::Runewords ? TypeNames(entry->base) : entry->base);
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
