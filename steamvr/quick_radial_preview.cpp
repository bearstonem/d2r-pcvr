// Quick cast radial: the render sheet. Paints the ring with the radial's own
// painter (vr/quick_radial_paint.cpp) and lays its quads out as the headset shows them (the engine's Draw list, in
// degrees), for 4, 6 and 12 items, Open by Hold, an empty ring and items, each a PNG and one sheet with gaps and captions.
// Usage: quick-radial-preview [--icons <d2r_vr_icons folder>] [--font <Cinzel ttf>] [--out <folder>]
// Without --out it only checks that every picture paints (the build's test); icons missing: the plates show initials.
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "interaction_icons.h"
#include "quick_radial.h"
#include "quick_radial_paint.h"
#include <wincodec.h>

// An RGBA8 image written as a PNG file with WIC (Windows' own codecs).
static bool SavePng(const wchar_t* file, const std::vector<uint8_t>& rgba, UINT width, UINT height) {
    if (!file || !width || !height || rgba.size() < size_t(width) * height * 4) return false;
    const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IWICImagingFactory* wic = nullptr; IWICStream* stream = nullptr; IWICBitmapEncoder* encoder = nullptr; IWICBitmapFrameEncode* frame = nullptr;
    auto bgra = rgba; for (size_t i = 0; i + 3 < bgra.size(); i += 4) std::swap(bgra[i], bgra[i + 2]);
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    const bool ok = SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))) &&
        SUCCEEDED(wic->CreateStream(&stream)) && SUCCEEDED(stream->InitializeFromFilename(file, GENERIC_WRITE)) &&
        SUCCEEDED(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) &&
        SUCCEEDED(encoder->Initialize(stream, WICBitmapEncoderNoCache)) && SUCCEEDED(encoder->CreateNewFrame(&frame, nullptr)) &&
        SUCCEEDED(frame->Initialize(nullptr)) && SUCCEEDED(frame->SetSize(width, height)) &&
        SUCCEEDED(frame->SetPixelFormat(&format)) && IsEqualGUID(format, GUID_WICPixelFormat32bppBGRA) &&
        SUCCEEDED(frame->WritePixels(height, width * 4, UINT(size_t(width) * height * 4), bgra.data())) &&
        SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit());
    for (IUnknown* p : {static_cast<IUnknown*>(frame), static_cast<IUnknown*>(encoder), static_cast<IUnknown*>(stream), static_cast<IUnknown*>(wic)}) if (p) p->Release();
    if (SUCCEEDED(co)) CoUninitialize();
    return ok;
}
namespace qr = framevr::quickradial;
namespace icons = framevr::interaction::icons;
namespace {
// Sorceress skills (ids and names as the game's): the sheet's rings.
struct Named { int id; const char* name; int level; };
constexpr Named Sorc[] = {{64, "Frozen Orb", 20}, {54, "Teleport", 1}, {59, "Blizzard", 18}, {53, "Chain Lightning", 12},
                          {47, "Fire Ball", 9}, {58, "Energy Shield", 6}, {56, "Meteor", 14}, {62, "Hydra", 8},
                          {42, "Static Field", 5}, {48, "Nova", 4}, {44, "Frost Nova", 3}, {60, "Chilling Armor", 2},
                          {51, "Fire Wall", 1}, {57, "Thunder Storm", 1}, {45, "Ice Blast", 7}};
std::map<int, std::shared_ptr<const icons::Image>> cache;
bool realIcons = false;
qr::paint::Icon IconOf(int id) {
    if (!realIcons) return {};
    auto it = cache.find(id);
    if (it == cache.end()) {
        auto im = icons::Get("s" + std::to_string(id));
        for (int i = 0; !im && i < 40; ++i) { icons::Wait(100); im = icons::Get("s" + std::to_string(id)); }
        it = cache.emplace(id, im).first;
    }
    return it->second ? qr::paint::Icon{it->second->rgba.data(), int(it->second->width), int(it->second->height)} : qr::paint::Icon{};
}
// One straight-alpha canvas the size of a cell, the quads drawn into it from the premultiplied regions (nearest-texel
// bilinear: enough to see the layout and the art).
struct Image { int w, h; std::vector<float> px; Image(int w_, int h_) : w(w_), h(h_), px(size_t(w_) * size_t(h_) * 4, 0.f) {} };
void Backdrop(Image& im) {   // a dark, slightly warm ground with a gradient: the game's dungeon behind the ring
    for (int y = 0; y < im.h; ++y) for (int x = 0; x < im.w; ++x) {
        float* p = &im.px[(size_t(y) * size_t(im.w) + size_t(x)) * 4];
        const float g = .10f + .08f * float(y) / float(im.h);
        p[0] = g * 1.1f; p[1] = g * .95f; p[2] = g * .8f; p[3] = 1;
    }
}
// rect of region pixels (premultiplied RGBA, width rw) drawn into im at (cx, cy) sized w x h px.
void Blit(Image& im, const std::vector<uint8_t>& region, int rw, qr::atlas::Rect r, float cx, float cy, float w, float h) {
    for (int y = int(cy - h / 2); y < int(cy + h / 2); ++y) for (int x = int(cx - w / 2); x < int(cx + w / 2); ++x) {
        if (x < 0 || y < 0 || x >= im.w || y >= im.h) continue;
        const float u = (x + .5f - (cx - w / 2)) / w * r.w, v = (y + .5f - (cy - h / 2)) / h * r.h;
        const int u0 = std::clamp(int(u - .5f), 0, r.w - 1), v0 = std::clamp(int(v - .5f), 0, r.h - 1);
        const int u1 = std::min(u0 + 1, r.w - 1), v1 = std::min(v0 + 1, r.h - 1);
        const float fu = std::clamp(u - .5f - u0, 0.f, 1.f), fv = std::clamp(v - .5f - v0, 0.f, 1.f);
        float s[4] = {0, 0, 0, 0};
        const int us[2] = {u0, u1}, vs[2] = {v0, v1};
        for (int j = 0; j < 2; ++j) for (int i = 0; i < 2; ++i) {
            const float wgt = (i ? fu : 1 - fu) * (j ? fv : 1 - fv);
            const uint8_t* p = &region[(size_t(r.y + vs[j]) * size_t(rw) + size_t(r.x + us[i])) * 4];
            for (int k = 0; k < 4; ++k) s[k] += p[k] / 255.f * wgt;
        }
        float* d = &im.px[(size_t(y) * size_t(im.w) + size_t(x)) * 4];
        for (int k = 0; k < 3; ++k) d[k] = s[k] + d[k] * (1 - s[3]);   // premultiplied over
    }
}
// The case: a mode, the items and what is pointed; painted, then its Draw list laid out with degrees as pixels.
struct Case { const char* file; const char* caption; int items; int pick; int demo = 0; bool hold = false; bool fixed = false; };   // fixed: Quick cast layout Fixed places (items at 3 and 9 o'clock)
// Items (2026-11-15): a ring of 3 skills and 5 items (the game's own item pictures by code).
struct Demo { const char* name; uint32_t code; int count; bool dim; const char* why; };
using framevr::interaction::Code;
const Demo RingItems[] = {{"Healing (best)", Code('h', 'p', '4'), 9, false, ""}, {"Full Rejuvenation Potion", Code('r', 'v', 'l'), 2, false, ""},
                          {"Town Portal", Code('t', 'b', 'k'), 14, false, ""}, {"Stamina Potion", Code('v', 'p', 's'), 0, true, "none left"},
                          {"Super Mana Potion", Code('m', 'p', '5'), 1, true, "only higher up a belt column"}};
qr::paint::Icon ItemIcon(uint32_t code) {
    if (!realIcons) return {};
    static std::map<uint32_t, std::shared_ptr<const icons::Image>> items;
    auto it = items.find(code);
    if (it == items.end()) {
        framevr::interaction::Item i; i.handle = 1; i.code = code; const auto key = icons::ItemKey(i);
        auto im = icons::Get(key);
        for (int k = 0; !im && k < 40; ++k) { icons::Wait(100); im = icons::Get(key); }
        it = items.emplace(code, im).first;
    }
    return it->second ? qr::paint::Icon{it->second->rgba.data(), int(it->second->width), int(it->second->height)} : qr::paint::Icon{};
}
constexpr float PxPerDeg = 18.f;
Image Render(const Case& c, std::vector<uint8_t>& outA, std::vector<uint8_t>& outB, double& paintMs, bool& fontOk) {
    qr::Tuning t;
    qr::Ring ring;
    for (int i = 0; i < c.items; ++i) ring.Add({Sorc[i].id, -1});
    if (c.demo == 1) for (int i = 0; i < int(std::size(RingItems)); ++i) ring.Add({qr::ExactId(RingItems[i].code), -1});
    t.toggle = !c.hold;
    t.fixed = c.fixed;
    if (c.fixed) { ring = {}; ring.Set(3, {Sorc[3].id, -1}); ring.Set(9, {Sorc[9].id, -1}); }   // 2026-11-17: places 3 and 9 only
    const auto es = qr::EntriesFor(qr::Mode::Cast, ring, c.fixed);
    qr::paint::Job job; job.t = t; job.srgb = false; job.key = 1;
    for (int i = 0; i < es.n; ++i) {
        qr::paint::PEntry e;
        const int idx = es.e[size_t(i)].index;
        if (idx < 0) { e.empty = true; job.entries.push_back(e); continue; }   // an empty place: its socket
        const Demo* d = c.demo == 1 && idx >= c.items ? &RingItems[idx - c.items] : nullptr;
        if (d) { e.name = d->name; e.icon = ItemIcon(d->code); e.count = d->count; e.dim = d->dim; e.why = d->why; }
        else {
            const auto& s = Sorc[size_t(idx)];
            e.name = s.name; e.level = s.level; e.icon = IconOf(s.id);
            e.dim = c.items >= 12 && i == 10;   // one dim item on the full ring: a skill the game refuses now
        }
        job.entries.push_back(e);
    }
    qr::paint::Picture pic;
    qr::paint::Paint(job, pic);
    paintMs = pic.ms; fontOk = pic.fontOk;
    outA = pic.a; outB = pic.b;
    // The headset's view: the Draw list's quads (plane degrees) at PxPerDeg, the ring's center in the middle of the cell.
    qr::Plane plane; plane.dist = 1;   // only the degrees are used below
    const auto d = qr::Draw(plane, qr::Mode::Cast, es, c.pick, t);
    const float half = qr::PictureHalfDeg(t);
    Image im(int(half * 2 * PxPerDeg) + 40, int(half * 2 * PxPerDeg) + 40);
    Backdrop(im);
    const float cx = im.w * .5f, cy = im.h * .5f;
    for (int i = 0; i < d.n; ++i) {
        const auto& q = d.q[size_t(i)];
        // back to degrees: the quad's meters at dist 1 are 2 tan(deg / 2); its center from the plane's position
        const float wDeg = 2 * std::atan(q.w / 2) * 57.29578f, hDeg = 2 * std::atan(q.h / 2) * 57.29578f;
        const float xDeg = std::atan(q.pose[0] - plane.center.x) * 57.29578f, yDeg = std::atan(q.pose[1] - plane.center.y) * 57.29578f;
        const auto& region = q.region == qr::atlas::A ? pic.a : pic.b;
        Blit(im, region, q.region == qr::atlas::A ? qr::atlas::AW : qr::atlas::BW, q.rect, cx + xDeg * PxPerDeg, cy - yDeg * PxPerDeg, wDeg * PxPerDeg, hDeg * PxPerDeg);
    }
    return im;
}
std::vector<uint8_t> Bytes(const Image& im) {
    std::vector<uint8_t> b(size_t(im.w) * size_t(im.h) * 4);
    for (size_t i = 0; i < size_t(im.w) * size_t(im.h); ++i) {
        for (int k = 0; k < 3; ++k) b[i * 4 + k] = uint8_t(std::lround(std::clamp(im.px[i * 4 + k], 0.f, 1.f) * 255.f));
        b[i * 4 + 3] = 255;
    }
    return b;
}
// A caption under a cell (GDI, white on the sheet's gray).
void Caption(std::vector<uint8_t>& sheet, int sw, int x0, int y0, int w, int h, const char* text) {
    HDC dc = CreateCompatibleDC(nullptr);
    BITMAPINFO bi{}; bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bmp || !bits) { DeleteDC(dc); return; }
    HGDIOBJ old = SelectObject(dc, bmp);
    std::memset(bits, 0, size_t(w) * size_t(h) * 4);
    HFONT f = CreateFontW(-20, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    HGDIOBJ oldF = SelectObject(dc, f);
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(255, 255, 255));
    RECT r{4, 0, w - 4, h};
    std::wstring wt(text, text + std::strlen(text));
    DrawTextW(dc, wt.c_str(), int(wt.size()), &r, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
    GdiFlush();
    const auto* b = static_cast<const uint8_t*>(bits);
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        const float a = b[(size_t(y) * size_t(w) + size_t(x)) * 4 + 1] / 255.f;
        uint8_t* p = &sheet[(size_t(y0 + y) * size_t(sw) + size_t(x0 + x)) * 4];
        for (int k = 0; k < 3; ++k) p[k] = uint8_t(std::lround(p[k] * (1 - a) + 235 * a));
    }
    SelectObject(dc, oldF); DeleteObject(f); SelectObject(dc, old); DeleteObject(bmp); DeleteDC(dc);
}
}  // namespace

int main(int argc, char** argv) {
    std::filesystem::path out, iconDir, font;
    for (int i = 1; i + 1 < argc; ++i) {
        if (!std::strcmp(argv[i], "--out")) out = argv[++i];
        else if (!std::strcmp(argv[i], "--icons")) iconDir = argv[++i];
        else if (!std::strcmp(argv[i], "--font")) font = argv[++i];
    }
    std::vector<char> fontBytes;
#ifdef QUICK_RADIAL_FONT
    if (font.empty()) font = QUICK_RADIAL_FONT;
#endif
    if (!font.empty()) {
        std::ifstream f(font, std::ios::binary);
        fontBytes.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
        if (!fontBytes.empty()) qr::paint::LoadFont(fontBytes.data(), fontBytes.size());
    }
    if (!iconDir.empty() && std::filesystem::exists(iconDir / "manifest.tsv")) { icons::Directory(iconDir.wstring()); realIcons = true; }
    const Case cases[] = {
        {"ring-4.png", "4 items: quarters. Stick left: the 9 o'clock item (Chain Lightning) is picked, its name in the center.", 4, 3},
        {"ring-6.png", "6 items: sixths, re-spaced (two added). Stick at 4 o'clock: Blizzard (item 3).", 6, 2},
        {"ring-12.png", "12 items (the most): twelfths, smaller plates. Nothing picked: the hint (Toggle: Point, back: cast). Item 11 dim.", 12, -1},
        {"ring-12-hold.png", "Open by Hold: the same ring, its hint Point the stick (the release casts).", 12, -1, 0, true},
        {"ring-empty.png", "An empty ring: where to fill it (ring menu > Quick cast: its 12 places, 2026-11-16).", 0, -1},
        {"ring-items.png", "Items: Healing (best) picked, 9 in all; Stamina dim (none left), Super Mana dim (only up a belt column).", 3, 3, 1},
        {"ring-fixed-3-9.png", "Quick cast layout Fixed places (2026-11-17): items at 3 and 9 o'clock only, the 10 empty places faint sockets; 9 picked.", 0, 9, 0, false, true},
    };
    struct Done { Image im; const char* caption; };
    std::vector<Done> done;
    int failed = 0;
    for (const auto& c : cases) {
        std::vector<uint8_t> a, b; double ms = 0; bool fontOk = false;
        Image im = Render(c, a, b, ms, fontOk);
        const bool ok = a.size() == size_t(qr::atlas::AW) * qr::atlas::AH * 4 && b.size() == size_t(qr::atlas::BW) * qr::atlas::BH * 4;
        size_t lit = 0; for (size_t i = 3; i < a.size(); i += 4) lit += a[i] > 0;
        std::printf("%s: %s, paint %.1f ms, %s, %s icons, %zu texels drawn in region A\n", c.file, ok && lit ? "ok" : "FAIL", ms,
                    fontOk ? "Cinzel" : "Georgia", realIcons ? "real" : "no", lit);
        failed += !(ok && lit);
        if (!out.empty()) {
            std::filesystem::create_directories(out);
            SavePng((out / c.file).wstring().c_str(), Bytes(im), UINT(im.w), UINT(im.h));
            if (&c == &cases[0]) {   // the regions themselves, once (what goes into the shared image)
                SavePng((out / "atlas-region-a.png").wstring().c_str(), a, qr::atlas::AW, qr::atlas::AH);
                SavePng((out / "atlas-region-b.png").wstring().c_str(), b, qr::atlas::BW, qr::atlas::BH);
            }
        }
        done.push_back({std::move(im), c.caption});
    }
    if (!out.empty() && !done.empty()) {   // the sheet: 3 a row, a 24 px gap, a caption band under each
        const int cw = done[0].im.w, ch = done[0].im.h, gap = 24, cap = 64, cols = 3, rows = int((done.size() + cols - 1) / cols);
        const int sw = cols * cw + (cols + 1) * gap, sh = rows * (ch + cap) + (rows + 1) * gap;
        std::vector<uint8_t> sheet(size_t(sw) * size_t(sh) * 4, 0);
        for (size_t i = 0; i < sheet.size(); i += 4) { sheet[i] = sheet[i + 1] = sheet[i + 2] = 58; sheet[i + 3] = 255; }
        for (size_t k = 0; k < done.size(); ++k) {
            const int x0 = gap + int(k % cols) * (cw + gap), y0 = gap + int(k / cols) * (ch + cap + gap);
            const auto px = Bytes(done[k].im);
            for (int y = 0; y < ch; ++y) std::memcpy(&sheet[(size_t(y0 + y) * size_t(sw) + size_t(x0)) * 4], &px[size_t(y) * size_t(cw) * 4], size_t(cw) * 4);
            Caption(sheet, sw, x0, y0 + ch + 4, cw, cap - 6, done[k].caption);
        }
        SavePng((out / "sheet-quick-radial.png").wstring().c_str(), sheet, UINT(sw), UINT(sh));
        std::printf("sheet: %s (%d x %d)\n", (out / "sheet-quick-radial.png").string().c_str(), sw, sh);
    }
    if (realIcons) icons::Shutdown();
    std::printf("quick-radial-preview: %d of %zu pictures painted\n", int(std::size(cases)) - failed, std::size(cases));
    return failed ? 1 : 0;
}
