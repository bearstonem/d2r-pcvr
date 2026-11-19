// Quick cast radial: the CPU painter (steamvr/quick_radial_paint.h).
// Everything is drawn here with anti-aliased discs, bands and strokes and GDI text; no picture file. A worker thread's call
// (about 10-25 ms for both regions), never a frame thread's.
#include <windows.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include "../steamvr/quick_radial_paint.h"

namespace framevr::quickradial::paint {
namespace {
// Premultiplied RGBA in floats, in the picture's encoded (sRGB) space; converted once at the end.
struct Canvas {
    int w = 0, h = 0;
    std::vector<float> px;
    Canvas(int w_, int h_) : w(w_), h(h_), px(size_t(w_) * size_t(h_) * 4, 0.f) {}
    // Straight color c (0-1) with coverage a, over what is there.
    void Over(int x, int y, const float c[3], float a) {
        if (x < 0 || y < 0 || x >= w || y >= h || !(a > 0)) return;
        a = std::min(a, 1.f);
        float* p = &px[(size_t(y) * size_t(w) + size_t(x)) * 4];
        for (int k = 0; k < 3; ++k) p[k] = c[k] * a + p[k] * (1 - a);
        p[3] = a + p[3] * (1 - a);
    }
};
struct Rgb { float c[3]; };
constexpr Rgb Bronze{{.69f, .52f, .29f}}, BronzeDark{{.36f, .25f, .13f}}, BronzeLight{{.88f, .72f, .44f}};
constexpr Rgb Plate{{.075f, .06f, .045f}}, Track{{.11f, .085f, .06f}}, Gold{{.93f, .78f, .47f}}, GoldBright{{1.f, .85f, .5f}};
constexpr Rgb Ink{{.03f, .02f, .015f}}, Gray{{.62f, .6f, .56f}};
inline float Smooth(float edge0, float edge1, float x) {
    const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.f, 1.f);
    return t * t * (3 - 2 * t);
}
// Coverage of a disc of radius r at distance d (1 px soft edge).
inline float DiscCov(float d, float r) { return std::clamp(r - d + .5f, 0.f, 1.f); }
void Disc(Canvas& cv, float cx, float cy, float r, const Rgb& c, float alpha) {
    const int x0 = int(std::floor(cx - r - 1)), x1 = int(std::ceil(cx + r + 1)), y0 = int(std::floor(cy - r - 1)), y1 = int(std::ceil(cy + r + 1));
    for (int y = y0; y <= y1; ++y) for (int x = x0; x <= x1; ++x) {
        const float d = std::hypot(x + .5f - cx, y + .5f - cy);
        cv.Over(x, y, c.c, DiscCov(d, r) * alpha);
    }
}
// An annulus rIn..rOut, its color from inner to outer (a lit band).
void Band(Canvas& cv, float cx, float cy, float rIn, float rOut, const Rgb& in, const Rgb& out, float alpha) {
    const int x0 = int(std::floor(cx - rOut - 1)), x1 = int(std::ceil(cx + rOut + 1)), y0 = int(std::floor(cy - rOut - 1)), y1 = int(std::ceil(cy + rOut + 1));
    for (int y = y0; y <= y1; ++y) for (int x = x0; x <= x1; ++x) {
        const float d = std::hypot(x + .5f - cx, y + .5f - cy);
        const float cov = std::min(DiscCov(d, rOut), 1.f - DiscCov(d, rIn));
        if (cov <= 0) continue;
        const float t = std::clamp((d - rIn) / std::max(1.f, rOut - rIn), 0.f, 1.f);
        float c[3];
        for (int k = 0; k < 3; ++k) c[k] = in.c[k] + (out.c[k] - in.c[k]) * t;
        cv.Over(x, y, c, cov * alpha);
    }
}
// The cache's skill picture with its game frame (132 x 130 or 130 x 130): the part the ring wheel shows, 104 px from (14, 13).
void CropOf(const Icon& ic, int& sx, int& sy, int& sw, int& sh) {
    sx = 0; sy = 0; sw = ic.w; sh = ic.h;
    if (ic.w >= 126 && ic.w <= 136 && ic.h >= 126 && ic.h <= 136) { sx = 14; sy = 13; sw = 104; sh = 104; }
}
// The icon box-filtered into a disc of radius r at (cx, cy) (premultiplied average, straight again); dim: gray and faint.
void IconDisc(Canvas& cv, float cx, float cy, float r, const Icon& ic, bool dim) {
    if (!ic.rgba || ic.w <= 0 || ic.h <= 0 || r < 1) return;
    int sx, sy, sw, sh; CropOf(ic, sx, sy, sw, sh);
    const float scale = float(sw) / (2 * r), scaleY = float(sh) / (2 * r);
    const int x0 = int(std::floor(cx - r)), y0 = int(std::floor(cy - r)), x1 = int(std::ceil(cx + r)), y1 = int(std::ceil(cy + r));
    for (int y = y0; y < y1; ++y) for (int x = x0; x < x1; ++x) {
        const float cov = DiscCov(std::hypot(x + .5f - cx, y + .5f - cy), r);
        if (cov <= 0) continue;
        const float u0 = (x - (cx - r)) * scale, u1 = u0 + scale, v0 = (y - (cy - r)) * scaleY, v1 = v0 + scaleY;
        float sum[4] = {0, 0, 0, 0}; int n = 0;
        for (int v = int(v0); v < std::max(int(v0) + 1, int(std::ceil(v1))); ++v) for (int u = int(u0); u < std::max(int(u0) + 1, int(std::ceil(u1))); ++u) {
            const int ux = std::clamp(sx + u, 0, ic.w - 1), vy = std::clamp(sy + v, 0, ic.h - 1);
            const uint8_t* p = ic.rgba + (size_t(vy) * size_t(ic.w) + size_t(ux)) * 4;
            const float a = p[3] / 255.f;
            for (int k = 0; k < 3; ++k) sum[k] += p[k] / 255.f * a;
            sum[3] += a; ++n;
        }
        if (!n || sum[3] <= 0) continue;
        float c[3];
        for (int k = 0; k < 3; ++k) c[k] = sum[k] / sum[3];
        float a = sum[3] / float(n);
        if (dim) { const float l = .3f * c[0] + .59f * c[1] + .11f * c[2]; for (float& k : c) k = l * .5f; a *= .8f; }
        cv.Over(x, y, c, a * cov);
    }
}

// ---- Text: GDI into a coverage mask (grayscale antialiasing), then outline and fill ----
std::mutex fontLock;
HANDLE fontHandle = nullptr;
bool fontLoaded = false;
std::wstring Wide(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(std::max(0, n)), L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
    return w;
}
// Coverage (w x h) of the text centered, at most px tall, shrunk until it fits w - 10.
std::vector<float> TextMask(const std::wstring& text, int w, int h, int px, bool& cinzel) {
    std::vector<float> m(size_t(w) * size_t(h), 0.f);
    HDC dc = CreateCompatibleDC(nullptr);
    if (!dc) return m;
    BITMAPINFO bi{}; bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bmp || !bits) { if (bmp) DeleteObject(bmp); DeleteDC(dc); return m; }
    HGDIOBJ oldBmp = SelectObject(dc, bmp);
    std::memset(bits, 0, size_t(w) * size_t(h) * 4);
    bool loaded; { std::lock_guard lock(fontLock); loaded = fontLoaded; }
    cinzel = loaded;
    HFONT font = nullptr;
    for (int size = px; size >= 10; size -= 1) {
        if (font) DeleteObject(font);
        font = CreateFontW(-size, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                           ANTIALIASED_QUALITY, DEFAULT_PITCH, loaded ? L"Cinzel" : L"Georgia");
        SelectObject(dc, font);
        SIZE ext{};
        GetTextExtentPoint32W(dc, text.c_str(), int(text.size()), &ext);
        if (ext.cx <= w - 10) break;
    }
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(255, 255, 255));
    RECT r{0, 0, w, h};
    DrawTextW(dc, text.c_str(), int(text.size()), &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    GdiFlush();
    const auto* b = static_cast<const uint8_t*>(bits);
    for (size_t i = 0; i < m.size(); ++i) m[i] = std::max({b[i * 4], b[i * 4 + 1], b[i * 4 + 2]}) / 255.f;
    SelectObject(dc, oldBmp);
    if (font) DeleteObject(font);
    DeleteObject(bmp); DeleteDC(dc);
    return m;
}
// A label strip at (ox, oy): a soft dark pill, the dark outline, the fill.
void Label(Canvas& cv, int ox, int oy, const std::string& text, const Rgb& fill, bool& cinzel) {
    using namespace atlas;
    const float rr = LabelH * .5f - 1;
    for (int y = 0; y < LabelH; ++y) for (int x = 0; x < LabelW; ++x) {   // the pill
        const float qx = std::clamp(x + .5f, rr + 1, LabelW - rr - 1), qy = LabelH * .5f;
        const float d = std::hypot(x + .5f - qx, y + .5f - qy);
        cv.Over(ox + x, oy + y, Ink.c, DiscCov(d, rr) * .62f);
    }
    if (text.empty()) return;
    const auto m = TextMask(Wide(text), LabelW, LabelH, 26, cinzel);
    for (int y = 0; y < LabelH; ++y) for (int x = 0; x < LabelW; ++x) {   // outline: the mask grown by 2 px
        float o = 0;
        for (int dy = -2; dy <= 2; ++dy) for (int dx = -2; dx <= 2; ++dx) {
            if (dx * dx + dy * dy > 5) continue;
            const int u = x + dx, v = y + dy;
            if (u >= 0 && v >= 0 && u < LabelW && v < LabelH) o = std::max(o, m[size_t(v) * LabelW + size_t(u)]);
        }
        cv.Over(ox + x, oy + y, Ink.c, o * .95f);
    }
    for (int y = 0; y < LabelH; ++y) for (int x = 0; x < LabelW; ++x) cv.Over(ox + x, oy + y, fill.c, m[size_t(y) * LabelW + size_t(x)]);
}
// Straight sRGB <-> linear (the premultiply's).
struct Lut {
    float toLinear[256];
    uint8_t toSrgb[4097];
    Lut() {
        for (int i = 0; i < 256; ++i) { const float c = i / 255.f; toLinear[i] = c <= .04045f ? c / 12.92f : std::pow((c + .055f) / 1.055f, 2.4f); }
        for (int i = 0; i <= 4096; ++i) {
            const float l = i / 4096.f, c = l <= .0031308f ? l * 12.92f : 1.055f * std::pow(l, 1 / 2.4f) - .055f;
            toSrgb[i] = uint8_t(std::lround(std::clamp(c, 0.f, 1.f) * 255.f));
        }
    }
};
void Emit(const Canvas& cv, bool srgb, std::vector<uint8_t>& out) {
    static const Lut lut;
    out.assign(size_t(cv.w) * size_t(cv.h) * 4, 0);
    for (size_t i = 0; i < size_t(cv.w) * size_t(cv.h); ++i) {
        const float* p = &cv.px[i * 4];
        const float a = std::clamp(p[3], 0.f, 1.f);
        if (a <= 0.f) continue;
        uint8_t* q = &out[i * 4];
        q[3] = uint8_t(std::lround(a * 255.f));
        for (int k = 0; k < 3; ++k) {
            const float straight = std::clamp(p[k] / a, 0.f, 1.f);
            if (!srgb) { q[k] = uint8_t(std::lround(straight * a * 255.f)); continue; }
            const float lin = lut.toLinear[std::clamp(int(std::lround(straight * 255.f)), 0, 255)] * a;
            q[k] = lut.toSrgb[std::clamp(int(lin * 4096.f + .5f), 0, 4096)];
        }
    }
}
}  // namespace

bool LoadFont(const void* data, size_t bytes) {
    std::lock_guard lock(fontLock);
    if (fontLoaded) return true;
    if (!data || !bytes) return false;
    DWORD count = 0;
    fontHandle = AddFontMemResourceEx(const_cast<void*>(data), DWORD(bytes), nullptr, &count);
    fontLoaded = fontHandle && count > 0;
    return fontLoaded;
}

std::string SkillLine(const PEntry& e) {
    std::string s = e.name.empty() ? std::string("Skill") : e.name;
    char lv[24] = "";
    if (e.count >= 0) std::snprintf(lv, sizeof lv, "  x%d", e.count);
    else if (e.level > 0) std::snprintf(lv, sizeof lv, "  %d", e.level);
    s += lv;
    if (e.dim) s += e.why.empty() ? std::string("  (not now)") : "  (" + e.why + ")";
    return s;
}
const char* FixedLabel(int label, bool toggle) {
    switch (label) {
        // Toggle: the stick back in the middle casts, a tap of the button shuts
        case atlas::LabelHint: return toggle ? "Point, back: cast" : "Point the stick";
        // where an empty ring is filled
        case atlas::LabelEmpty: return "Fill: ring menu > Quick cast";
        default: return "";
    }
}

void Paint(const Job& job, Picture& out) {
    using namespace atlas;
    const auto t0 = std::chrono::steady_clock::now();
    Canvas a(AW, AH), b(BW, BH);
    bool cinzel = false;
    const Tuning& t = job.t;
    const int n = int(job.entries.size());
    // ---- The ring picture: PictureHalfDeg each way across RingPx ----
    const float half = PictureHalfDeg(t), k = RingPx / (2 * half), c = RingPx * .5f;
    const float icon = IconDegFor(n, t), rMid = t.radiusDeg * k;
    const float trackW = std::max(6.f, icon * k * .62f);
    const float rIn = rMid - trackW * .5f, rOut = rMid + trackW * .5f;
    Disc(a, c, c, rIn - 2, Plate, .55f);                                  // the dark middle under the name
    Band(a, c, c, rIn, rOut, Track, Track, .88f);                         // the leather track
    Band(a, c, c, rOut, rOut + 3, BronzeLight, BronzeDark, 1.f);          // its bronze edges
    Band(a, c, c, rIn - 3, rIn, BronzeDark, BronzeLight, 1.f);
    Band(a, c, c, rIn - 5, rIn - 3, Ink, Ink, .6f);
    for (int i = 0; i < n; ++i) {
        const PEntry& e = job.entries[size_t(i)];
        const Vec2f at = AtClock(ItemClock(i, n), t.radiusDeg);
        const float px = c + at.x * k, py = c - at.y * k, r = icon * k * .5f;
        const float rim = std::max(2.f, r * .1f);
        if (e.empty) {   // Fixed places: an empty place keeps its spot on the clock, a faint socket (never picked)
            Disc(a, px, py, r * .7f, Plate, .45f);
            Band(a, px, py, r * .7f - std::max(1.5f, rim * .6f), r * .7f, BronzeDark, BronzeDark, .55f);
            continue;
        }
        Disc(a, px, py, r, Plate, .96f);
        Band(a, px, py, r - rim, r, e.dim ? BronzeDark : BronzeLight, e.dim ? BronzeDark : Bronze, 1.f);
        const float ri = r - rim - 1;
        {
                if (e.icon.rgba) IconDisc(a, px, py, ri, e.icon, e.dim);
                else {   // no picture (not cached): the name's first letters
                    std::string ini;
                    bool space = true;
                    for (char ch : e.name) { if (space && std::isalpha(uint8_t(ch)) && ini.size() < 2) ini += ch; space = ch == ' '; }
                    const int w = int(ri * 1.6f), h = int(ri * 1.1f);
                    if (w > 6 && h > 6) {
                        const auto m = TextMask(Wide(ini.empty() ? "?" : ini), w, h, int(ri * .9f), cinzel);
                        for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x)
                            a.Over(int(px - w * .5f) + x, int(py - h * .5f) + y, e.dim ? Gray.c : Gold.c, m[size_t(y) * size_t(w) + size_t(x)]);
                    }
                }
                if (e.count >= 0) {   // an item's count on its plate, lower right
                    const float br = std::max(5.f, ri * .36f), bx = px + ri * .62f, by = py + ri * .62f;
                    Disc(a, bx, by, br, Ink, .92f);
                    Band(a, bx, by, br - 1.5f, br, Bronze, Bronze, 1.f);
                    const int w = int(br * 2.2f), h = int(br * 1.7f);
                    if (w > 6 && h > 6) {
                        const auto m = TextMask(Wide(std::to_string(std::min(e.count, 99))), w, h, int(br * 1.3f), cinzel);
                        for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x)
                            a.Over(int(bx - w * .5f) + x, int(by - h * .5f) + y, e.dim ? Gray.c : GoldBright.c, m[size_t(y) * size_t(w) + size_t(x)]);
                    }
                }
        }
    }
    // ---- The pick's rim: a gold ring with a soft glow, 0.84 of its quad's half ----
    {
        const float rc = RimPx * .5f, rr = rc * .84f;
        for (int y = 0; y < RimPx; ++y) for (int x = 0; x < RimPx; ++x) {
            const float d = std::hypot(x + .5f - rc, y + .5f - rc);
            const float ring = std::clamp(3.2f - std::fabs(d - rr), 0.f, 1.f);
            const float glow = Smooth(rc - 1, rr, d) * Smooth(rr - 9, rr, d) * .55f + Smooth(rr + 10, rr, d) * (d > rr ? .45f : 0.f);
            a.Over(RimRect.x + x, RimRect.y + y, Gold.c, glow * .6f);
            a.Over(RimRect.x + x, RimRect.y + y, GoldBright.c, ring);
        }
    }
    // ---- The labels: each item's in its place, then the hint and the empty ring's ----
    const auto put = [&](int label, const std::string& text, const Rgb& fill) {
        const Cell cell = LabelCell(label);
        Label(cell.region == A ? a : b, cell.r.x, cell.r.y, text, fill, cinzel);
    };
    for (int i = 0; i < n && i < LabelHint; ++i) { const PEntry& e = job.entries[size_t(i)]; if (!e.empty) put(i, SkillLine(e), e.dim ? Gray : Gold); }
    for (const int label : {LabelHint, LabelEmpty}) put(label, FixedLabel(label, job.t.toggle), Gold);
    Emit(a, job.srgb, out.a);
    Emit(b, job.srgb, out.b);
    out.key = job.key;
    out.fontOk = cinzel;
    out.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}
}  // namespace framevr::quickradial::paint
