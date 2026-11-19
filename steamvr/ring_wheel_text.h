#pragma once
// Lab Ring wheel (T025, build step R5): text as data (plan E2 text, L1 type, L3 the center blurb). Pure and Windows-free:
// the glyph metrics the atlas worker measures (vr/ring_wheel_atlas.cpp), clean strings, widths, the center blurb's
// shaped box (the kit's blurbLines, ported), and glyph quads for a line or an arc. Laid out only when the pointed skill
// changes (BlurbCache); a frame only copies the cached glyph quads.
//
// Units: canvas px at 100% (steamvr/ring_wheel.h); the atlas holds each glyph at the render scale.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace framevr::ringwheel::text {
// Upstream's menu fonts (steamvr/assets/menu/fonts, SIL OFL): Cinzel 700, Alegreya Sans 500 and 700.
// Georgia (feedback 1 item 4, the footer as upstream paints it): the installed Windows font, read at run time from the system
// font collection (no font file shipped; not OFL); Cinzel stands in when it is missing (Metrics::georgia false).
enum Face { Cinzel, AlegreyaMedium, AlegreyaBold, Faces, Georgia = Faces, AllFaces };
// L1 / L3 sizes (critique-vr-ux S2 floors: 22 px running text, 24 px names). The footer: Georgia semibold at every whole px
// from 22 (the floor) down to 17 (upstream's six tabs, the named exception), then 16 and 15 (the 1.0 deg label floor at
// 0.9 m) only when a row keeps no margin at 17 (review-r8 I4-2); one size per row (L1 Footer, FooterRowStyle); no outline,
// as upstream's GDI text. The footer sizes are last and in order (FooterStyleOf).
constexpr int FooterMaxPx = 22, FooterMinPx = 15;
enum Style { Name24, Name22, Body22, Bold22, Label26, Label22, Footer22, Footer21, Footer20, Footer19, Footer18, Footer17, Footer16, Footer15, Styles };
struct StyleSpec { Face face; float px; };
constexpr StyleSpec Specs[Styles] = {{Cinzel, 24}, {Cinzel, 22}, {AlegreyaMedium, 22}, {AlegreyaBold, 22}, {Cinzel, 26}, {Cinzel, 22},
                                     {Georgia, 22}, {Georgia, 21}, {Georgia, 20}, {Georgia, 19}, {Georgia, 18}, {Georgia, 17}, {Georgia, 16}, {Georgia, 15}};
constexpr bool IsFooter(Style s) { return s >= Footer22 && s <= Footer15; }
constexpr bool Outlined(Style s) { return !IsFooter(s); }
constexpr Style FooterStyleOf(int px) { return Style(Footer22 + FooterMaxPx - std::clamp(px, FooterMinPx, FooterMaxPx)); }
constexpr int First = 32, Last = 255, EllipsisSlot = Last - First + 1, Slots = EllipsisSlot + 1;
// The footer sizes hold only space to 'Z' (upstream's labels are capitals); any other character draws as '?'.
constexpr int FooterLastSlot = 'Z' - First;
constexpr float OutlinePx = 2.5f;   // the kit's 5 px stroke, half of it outside the glyph
// One glyph: its advance, its quad from the pen on the baseline (ox right, oy down; the outline included), its atlas UVs.
struct Glyph { float advance = 0, ox = 0, oy = 0, w = 0, h = 0, u0 = 0, v0 = 0, u1 = 0, v1 = 0; };
struct Metrics {
    std::array<std::array<Glyph, Slots>, Styles> g{};
    std::array<float, Styles> ascent{}, descent{};   // px at 100%
    bool ok = false;
    bool georgia = false;   // the footer's Georgia was found (else Cinzel)
};

// UTF-8 to a code point (a stray byte reads as Latin-1, so a Latin-1 string still shows).
inline uint32_t Next(std::string_view s, size_t& i) {
    const auto b = uint8_t(s[i]);
    auto cont = [&](size_t k) { return i + k < s.size() && (uint8_t(s[i + k]) & 0xC0) == 0x80; };
    if (b >= 0xC2 && b < 0xE0 && cont(1)) { const uint32_t c = (b & 0x1Fu) << 6 | (uint8_t(s[i + 1]) & 0x3Fu); i += 2; return c; }
    if (b >= 0xE0 && b < 0xF0 && cont(1) && cont(2)) {
        const uint32_t c = (b & 0x0Fu) << 12 | (uint8_t(s[i + 1]) & 0x3Fu) << 6 | (uint8_t(s[i + 2]) & 0x3Fu); i += 3; return c;
    }
    ++i; return b;
}
inline int SlotOf(uint32_t c) {
    if (c == 0x2026) return EllipsisSlot;
    if (c == 0x2019 || c == 0x2018) c = '\'';
    if (c == 0x2013 || c == 0x2014) c = '-';
    return c >= uint32_t(First) && c <= uint32_t(Last) ? int(c) - First : '?' - First;
}
// The game's color codes ("\xFF" "c" and one character, UTF-8 or Latin-1) out; line breaks and runs of spaces to one space.
inline std::string Clean(std::string_view s) {
    std::string out;
    for (size_t i = 0; i < s.size();) {
        const size_t at = i;
        const uint32_t c = Next(s, i);
        if (c == 0xFF && i < s.size() && s[i] == 'c') { i += i + 1 < s.size() ? 2 : 1; continue; }
        if (c == '\n' || c == '\r' || c == '\t' || c == ' ') { if (!out.empty() && out.back() != ' ') out += ' '; continue; }
        out.append(s.substr(at, i - at));
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}
inline float Width(const Metrics& m, Style st, std::string_view s) {
    float w = 0;
    for (size_t i = 0; i < s.size();) w += m.g[size_t(st)][size_t(SlotOf(Next(s, i)))].advance;
    return w;
}
// The baseline under a line centered on y (the kit draws with textBaseline 'middle').
inline float Baseline(const Metrics& m, Style st, float y) { return y + (m.ascent[size_t(st)] - m.descent[size_t(st)]) / 2; }

// ---- The center blurb (L3; kit blurbLines): a shaped box inside the r 124 circle ----
enum Kind { KName, KSub, KDesc, KHelp };
struct Line { std::string text; Style style = Body22; Kind kind = KDesc; };
// whole (designer gate D3, the Commander orders): every line shows as written: the description is never shortened and the
// line under the name never dropped. What does not fit is reported in Blurb::over (WholeBlurb then picks what gives way).
struct BlurbText { std::string name, sub, desc, help; bool whole = false; };
struct Blurb { std::vector<Line> lines; std::vector<std::string> over; int maxLines = 0; };
constexpr float BlurbR = 80 + 56 - 12, LineH = 26;
constexpr float OverSlackPx = .5f, ExactSlackPx = .45f;   // a line may pass its chord by half a pixel (LayBlurb's check); the exact wrap stays just inside that
// The chord of the r 124 circle at line k of N, at the glyphs' far edge (12 px past the line's middle).
inline float Chord(int k, int n) {
    const float y = std::fabs((k - (n - 1) / 2.f) * LineH) + 12;
    return 2 * std::sqrt(std::max(0.f, BlurbR * BlurbR - y * y));
}
// The width line k may take: the narrowest chord over every line count the blurb may end with, minus the 5 px outline.
inline float WrapAt(int k, int n) {
    float m = 1e9f;
    for (int c = k + 1; c <= n; ++c) m = std::min(m, Chord(k, c));
    return std::max(40.f, m - 5);
}
inline std::vector<std::string> Words(const std::string& s) {
    std::vector<std::string> w; size_t i = 0;
    while (i < s.size()) { const size_t j = s.find(' ', i); w.push_back(s.substr(i, j == std::string::npos ? std::string::npos : j - i)); if (j == std::string::npos) break; i = j + 1; }
    return w;
}
inline Blurb LayBlurb(const Metrics& m, const BlurbText& in) {
    struct Seg { std::string text; Style style; Kind kind; };
    std::vector<Seg> segs;
    for (const Seg& s : {Seg{Clean(in.name), Name24, KName}, Seg{Clean(in.sub), Body22, KSub}, Seg{Clean(in.desc), Body22, KDesc}, Seg{Clean(in.help), Bold22, KHelp}})
        if (!s.text.empty()) segs.push_back(s);
    // exact: line k may take its own chord of an n-line blurb, to the half pixel the check below allows (the answer counts
    // only when it comes to n lines exactly).
    auto lay = [&](int n, bool exact = false) {
        std::vector<Line> lines;
        auto room = [&] { const int k = int(lines.size()); return exact ? Chord(k, n) - 5 + ExactSlackPx : WrapAt(k, n); };
        for (const auto& seg : segs) {
            Style st = seg.style;
            if (seg.kind == KName) {
                if (Width(m, Name24, seg.text) <= room()) { lines.push_back({seg.text, Name24, KName}); continue; }
                st = Name22;   // a long name: 22 px, and it may take two lines (then the tree line drops)
            }
            std::string line;
            for (const auto& w : Words(seg.text)) {
                const std::string test = line.empty() ? w : line + " " + w;
                if (Width(m, st, test) > room() && !line.empty()) { lines.push_back({line, st, seg.kind}); line = w; }
                else line = test;
            }
            lines.push_back({line, st, seg.kind});
        }
        return lines;
    };
    auto settle = [&] { std::vector<Line> l; for (int n = 1; n <= 14; ++n) { l = lay(n); if (int(l.size()) <= n) return l; } return l; };
    Blurb b;
    b.maxLines = int(std::floor(2 * BlurbR * .84f / LineH));
    b.lines = settle();
    // A whole blurb gives no line away, so it looks for the fewest lines whose own chords hold it: WrapAt keeps room for
    // every count the blurb might still end with, which costs a line where the text is long (designer gate D3).
    if (in.whole) for (int n = 1; n <= b.maxLines && n < int(b.lines.size()); ++n) { auto l = lay(n, true); if (int(l.size()) == n) { b.lines = std::move(l); break; } }
    auto count = [&](Kind k) { return int(std::count_if(b.lines.begin(), b.lines.end(), [&](const Line& l) { return l.kind == k; })); };
    const auto di = std::find_if(segs.begin(), segs.end(), [](const Seg& s) { return s.kind == KDesc; });
    if (di != segs.end() && !in.whole) {
        auto words = Words(di->text);
        while ((count(KDesc) > 2 || int(b.lines.size()) > b.maxLines) && words.size() > 1) {
            words.pop_back();
            std::string t;
            for (const auto& w : words) t += (t.empty() ? "" : " ") + w;
            while (!t.empty() && (t.back() == ',' || t.back() == ';' || t.back() == ':' || t.back() == '.')) t.pop_back();
            di->text = t + "\xE2\x80\xA6";
            b.lines = settle();
        }
    }
    const auto si = std::find_if(segs.begin(), segs.end(), [](const Seg& s) { return s.kind == KSub; });
    if (!in.whole && (int(b.lines.size()) > b.maxLines || count(KName) > 1) && si != segs.end()) { segs.erase(si); b.lines = settle(); }
    const int n = int(b.lines.size());
    for (int k = 0; k < n; ++k) {
        const auto& l = b.lines[size_t(k)];
        const float w = Width(m, l.style, l.text) + 5;
        if (w > Chord(k, n) + OverSlackPx) b.over.push_back(l.text + " " + std::to_string(int(w)) + " > " + std::to_string(int(Chord(k, n))) + " px");
    }
    if (n > b.maxLines) b.over.push_back(std::to_string(n) + " lines > " + std::to_string(b.maxLines));
    if (count(KName) > 2) b.over.push_back("name over two lines");
    return b;
}
// A blurb shown whole (designer gate D3): the long title when every line fits the circle, else the short one (the order
// row's own name already says "army"); if that does not fit either, the line under the name goes, as it does for a skill
// whose name takes two lines. The description and the last line never give way: what still does not fit is in Blurb::over.
inline BlurbText WholeBlurb(const Metrics& m, BlurbText in, const std::string& shortName) {
    in.whole = true;
    if (LayBlurb(m, in).over.empty()) return in;
    in.name = shortName;
    if (LayBlurb(m, in).over.empty()) return in;
    BlurbText bare = in; bare.sub.clear();
    return LayBlurb(m, bare).over.empty() ? bare : in;
}
// Lays a blurb out only when its key (the pointed seat, its help line) changes: a 1000-frame hover is one layout.
class BlurbCache {
    uint64_t key = ~0ull;
    Blurb blurb;
public:
    unsigned layouts = 0;
    const Blurb& Get(const Metrics& m, uint64_t k, const BlurbText& in) {
        if (k != key) { key = k; blurb = LayBlurb(m, in); ++layouts; }
        return blurb;
    }
    void Reset() { key = ~0ull; blurb = {}; }
};
}  // namespace framevr::ringwheel::text
