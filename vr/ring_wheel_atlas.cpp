// Ring skill menu (, build step R5): the atlases on the ring's worker (vr/ring_wheel_atlas.h).
#include "ring_wheel_atlas.h"
#include "../steamvr/spell_sprites.h"   // stage 3a: upstream's spell sprite cache reader (pure)
#include "../steamvr/ring_wheel_unbake.h"   // the clean icon's key (pure)
#include "../steamvr/ring_wheel_tree_art.h"   // a painted tree square's atlas id (pure)
#include "../steamvr/ring_wheel_glyph_art.h"   // Glyph icon: a painted glyph or sigil square's atlas id (pure)
#include <windows.h>
#include <dwrite_3.h>
#include <wrl/client.h>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>

using Microsoft::WRL::ComPtr;
namespace framevr::ringwheel {
namespace {
double Ms(std::chrono::steady_clock::time_point since) { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - since).count(); }
// sRGB <-> linear light: a table each way (the mips average in linear light).
struct Gamma {
    float toLinear[256];
    uint8_t toGamma[4096];
    Gamma() {
        for (int i = 0; i < 256; ++i) { const float c = i / 255.f; toLinear[i] = c <= .04045f ? c / 12.92f : std::pow((c + .055f) / 1.055f, 2.4f); }
        for (int i = 0; i < 4096; ++i) {
            const float l = i / 4095.f, c = l <= .0031308f ? l * 12.92f : 1.055f * std::pow(l, 1 / 2.4f) - .055f;
            toGamma[i] = uint8_t(std::lround(std::clamp(c, 0.f, 1.f) * 255));
        }
    }
    uint8_t Encode(float l) const { return toGamma[size_t(std::clamp(l, 0.f, 1.f) * 4095 + .5f)]; }
};
const Gamma& Lut() { static const Gamma g; return g; }
// A premultiplied-in-gamma pixel to linear premultiplied, and back.
void ToLinear(const uint8_t* p, float out[4]) {
    const float a = p[3] / 255.f;
    out[3] = a;
    for (int k = 0; k < 3; ++k) out[k] = a > 0 ? Lut().toLinear[std::min(255, int(std::lround(p[k] / a)))] * a : 0;
}
void FromLinear(const float in[4], uint8_t* p) {
    const float a = std::clamp(in[3], 0.f, 1.f);
    p[3] = uint8_t(std::lround(a * 255));
    for (int k = 0; k < 3; ++k) p[k] = a > 0 ? uint8_t(std::min<long>(p[3], std::lround(Lut().Encode(in[k] / a) * a))) : 0;
}
}  // namespace

bool FontsFromFolder(const std::wstring& folder, Fonts& out) {
    const wchar_t* names[text::Faces] = {L"Cinzel-wght.ttf", L"AlegreyaSans-Medium.ttf", L"AlegreyaSans-Bold.ttf"};
    for (int i = 0; i < text::Faces; ++i) {
        std::ifstream f(std::filesystem::path(folder) / names[i], std::ios::binary);
        out.face[size_t(i)].assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    }
    return out.Ok();
}
bool FontsFromModule(void* module, Fonts& out) {
    const wchar_t* names[text::Faces] = {L"D2RVR_FONT_CINZEL", L"D2RVR_FONT_ALEGREYA_SANS", L"D2RVR_FONT_ALEGREYA_SANS_BOLD"};
    for (int i = 0; i < text::Faces; ++i) {
        const auto m = static_cast<HMODULE>(module);
        HRSRC r = FindResourceW(m, names[i], MAKEINTRESOURCEW(10));   // RT_RCDATA
        HGLOBAL h = r ? LoadResource(m, r) : nullptr;
        const auto* data = h ? static_cast<const uint8_t*>(LockResource(h)) : nullptr;
        const DWORD size = r ? SizeofResource(m, r) : 0;
        if (data && size) out.face[size_t(i)].assign(data, data + size); else out.face[size_t(i)].clear();
    }
    return out.Ok();
}

bool BuildGlyphs(const Fonts& fonts, int scalePct, AtlasImage& out, text::Metrics& m, std::string& error) {
    m = {};
    const float scale = ScalePct(scalePct) / 100.f;
    ComPtr<IDWriteFactory5> dw;
    ComPtr<IDWriteInMemoryFontFileLoader> loader;
    auto fail = [&](const char* what) { error = what; if (dw && loader) dw->UnregisterFontFileLoader(loader.Get()); return false; };
    if (!fonts.Ok()) return fail("fonts missing");
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_ISOLATED, __uuidof(IDWriteFactory5), reinterpret_cast<IUnknown**>(dw.GetAddressOf()))))
        return fail("DirectWrite unavailable");
    if (FAILED(dw->CreateInMemoryFontFileLoader(&loader)) || FAILED(dw->RegisterFontFileLoader(loader.Get()))) { loader.Reset(); return fail("font loader"); }
    ComPtr<IDWriteFontSetBuilder1> builder;
    if (FAILED(dw->CreateFontSetBuilder(&builder))) return fail("font set builder");
    for (const auto& face : fonts.face) {
        ComPtr<IDWriteFontFile> file;
        if (FAILED(loader->CreateInMemoryFontFileReference(dw.Get(), face.data(), UINT32(face.size()), nullptr, &file)) || FAILED(builder->AddFontFile(file.Get())))
            return fail("font file");
    }
    ComPtr<IDWriteFontSet> set;
    ComPtr<IDWriteFontCollection1> collection;
    if (FAILED(builder->CreateFontSet(&set)) || FAILED(dw->CreateFontCollectionFromFontSet(set.Get(), &collection))) return fail("font collection");
    // The weights the menu uses (Cinzel is a variable font: 700 is its Bold named instance).
    const std::pair<const wchar_t*, DWRITE_FONT_WEIGHT> want[text::Faces] = {
        {L"Cinzel", DWRITE_FONT_WEIGHT_BOLD}, {L"Alegreya Sans", DWRITE_FONT_WEIGHT_MEDIUM}, {L"Alegreya Sans", DWRITE_FONT_WEIGHT_BOLD}};
    ComPtr<IDWriteFontFace> faces[text::AllFaces];
    for (int i = 0; i < text::Faces; ++i) {
        UINT32 index = 0; BOOL exists = FALSE;
        ComPtr<IDWriteFontFamily1> family; ComPtr<IDWriteFont> font;
        if (FAILED(collection->FindFamilyName(want[i].first, &index, &exists)) || !exists || FAILED(collection->GetFontFamily(index, &family)) ||
            FAILED(family->GetFirstMatchingFont(want[i].second, DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL, &font)) ||
            font->GetWeight() != want[i].second || FAILED(font->CreateFontFace(&faces[i])))
            return fail("font weight not found");
    }
    {   // The footer's Georgia from the system fonts, semibold as upstream asks GDI for it (Georgia has no 600: Bold answers,
        // as it does for GDI); missing: Cinzel (feedback 1 item 4).
        ComPtr<IDWriteFontCollection> system; UINT32 index = 0; BOOL exists = FALSE;
        ComPtr<IDWriteFontFamily> family; ComPtr<IDWriteFont> font;
        m.georgia = SUCCEEDED(static_cast<IDWriteFactory*>(dw.Get())->GetSystemFontCollection(&system, FALSE)) && SUCCEEDED(system->FindFamilyName(L"Georgia", &index, &exists)) && exists &&
                    SUCCEEDED(system->GetFontFamily(index, &family)) &&
                    SUCCEEDED(family->GetFirstMatchingFont(DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL, &font)) &&
                    SUCCEEDED(font->CreateFontFace(&faces[text::Georgia]));
        if (!m.georgia) faces[text::Georgia] = faces[text::Cinzel];
    }
    // Each glyph: its coverage at the render scale, padded for the outline; packed in rows.
    out = {}; out.kind = AtlasKind::Glyphs; out.px = AtlasTexPx; out.bpp = 2; out.mips = 1;
    out.data.assign(size_t(AtlasTexPx) * AtlasTexPx * 2, 0);
    const float o = text::OutlinePx * scale;
    const int pad = int(std::ceil(o)) + 1;
    struct Tap { int dx, dy; float w; };
    std::vector<Tap> taps;
    for (int dy = -pad; dy <= pad; ++dy) for (int dx = -pad; dx <= pad; ++dx) {
        const float w = std::clamp(o + .5f - std::hypot(float(dx), float(dy)), 0.f, 1.f);
        if (w > 0) taps.push_back({dx, dy, w});
    }
    int penX = 0, penY = 0, rowH = 0;
    constexpr int Gap = 2;
    std::vector<uint8_t> cover;
    for (int st = 0; st < text::Styles; ++st) {
        if (st == text::Label22) { m.g[size_t(st)] = m.g[text::Name22]; m.ascent[size_t(st)] = m.ascent[text::Name22]; m.descent[size_t(st)] = m.descent[text::Name22]; continue; }
        const auto& spec = text::Specs[st];
        IDWriteFontFace* face = faces[spec.face].Get();
        DWRITE_FONT_METRICS fm{};
        face->GetMetrics(&fm);
        const float em = float(fm.designUnitsPerEm);
        m.ascent[size_t(st)] = fm.ascent * spec.px / em; m.descent[size_t(st)] = fm.descent * spec.px / em;
        const bool outlined = text::Outlined(text::Style(st));
        const int sp = outlined ? pad : 1;   // the footer sizes: no outline, 1 px of room
        for (int slot = 0; slot < text::Slots; ++slot) {
            const UINT32 cp = slot == text::EllipsisSlot ? 0x2026u : UINT32(text::First + slot);
            if ((cp >= 127 && cp < 160) || (!outlined && slot > text::FooterLastSlot)) { m.g[size_t(st)][size_t(slot)] = m.g[size_t(st)]['?' - text::First]; continue; }
            UINT16 index = 0;
            if (FAILED(face->GetGlyphIndices(&cp, 1, &index))) continue;
            if (!index && cp != ' ') { m.g[size_t(st)][size_t(slot)] = m.g[size_t(st)]['?' - text::First]; continue; }
            DWRITE_GLYPH_METRICS gm{};
            face->GetDesignGlyphMetrics(&index, 1, &gm, FALSE);
            auto& g = m.g[size_t(st)][size_t(slot)];
            g.advance = gm.advanceWidth * spec.px / em;
            const float advance = 0, offsetZero = 0;
            DWRITE_GLYPH_OFFSET offset{offsetZero, 0};
            DWRITE_GLYPH_RUN run{face, spec.px * scale, 1, &index, &advance, &offset, FALSE, 0};
            ComPtr<IDWriteGlyphRunAnalysis> analysis;
            if (FAILED(dw->CreateGlyphRunAnalysis(&run, nullptr, DWRITE_RENDERING_MODE1_NATURAL_SYMMETRIC, DWRITE_MEASURING_MODE_NATURAL,
                    DWRITE_GRID_FIT_MODE_DISABLED, DWRITE_TEXT_ANTIALIAS_MODE_GRAYSCALE, 0, 0, &analysis))) continue;
            RECT b{};
            if (FAILED(analysis->GetAlphaTextureBounds(DWRITE_TEXTURE_ALIASED_1x1, &b)) || b.right <= b.left || b.bottom <= b.top) continue;   // a space
            const int gw = b.right - b.left, gh = b.bottom - b.top, w = gw + 2 * sp, h = gh + 2 * sp;
            cover.assign(size_t(gw) * gh, 0);
            if (FAILED(analysis->CreateAlphaTexture(DWRITE_TEXTURE_ALIASED_1x1, &b, cover.data(), UINT32(cover.size())))) continue;
            if (penX + w > AtlasTexPx) { penX = 0; penY += rowH + Gap; rowH = 0; }
            if (penY + h > AtlasTexPx) return fail("glyph atlas full");
            for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
                auto at = [&](int cx, int cy) { cx -= sp; cy -= sp; return cx >= 0 && cy >= 0 && cx < gw && cy < gh ? cover[size_t(cy) * gw + size_t(cx)] / 255.f : 0.f; };
                uint8_t* px = &out.data[(size_t(penY + y) * AtlasTexPx + size_t(penX + x)) * 2];
                px[0] = uint8_t(std::lround(at(x, y) * 255));
                if (!outlined) { px[1] = px[0]; continue; }
                float edge = 0;
                for (const auto& t : taps) edge = std::max(edge, at(x + t.dx, y + t.dy) * t.w);
                px[1] = uint8_t(std::lround(edge * 255));
            }
            g.ox = (b.left - sp) / scale; g.oy = (b.top - sp) / scale; g.w = w / scale; g.h = h / scale;
            g.u0 = float(penX) / AtlasTexPx; g.v0 = float(penY) / AtlasTexPx; g.u1 = float(penX + w) / AtlasTexPx; g.v1 = float(penY + h) / AtlasTexPx;
            penX += w + Gap; rowH = std::max(rowH, h);
        }
    }
    dw->UnregisterFontFileLoader(loader.Get());
    m.ok = true;   // keeps m.georgia
    return true;
}

void BuildIcons(const int* ids, int n, int scalePct, const IconSource& source, const std::function<void()>& settle, AtlasImage& out, IconPack& pack, uint32_t variant) {
    const int pct = ScalePct(scalePct), cellPx = CellPx(pct), art = ArtPx(pct);
    out = {}; out.kind = AtlasKind::Icons; out.px = AtlasTexPx; out.bpp = 4; out.mips = IconMips;
    out.data.assign(out.MipOffset(IconMips), 0);
    pack = {}; pack.scalePct = pct; pack.cellOf.assign(size_t(std::max(n, 0)), -1);
    std::vector<int> unique;
    for (int i = 0; i < n; ++i) if (ids[i] >= 0 && std::find(unique.begin(), unique.end(), ids[i]) == unique.end()) unique.push_back(ids[i]);
    pack.wanted = int(unique.size());
    std::vector<std::shared_ptr<const interaction::icons::Image>> images(unique.size());
    bool pending = false;
    for (size_t i = 0; i < unique.size(); ++i) if (!(images[i] = source ? source(unique[i]) : nullptr)) pending = true;
    if (pending && settle) {   // the icon cache decodes on its own worker: wait once, then ask again
        settle();
        for (size_t i = 0; i < unique.size(); ++i) if (!images[i] && source) images[i] = source(unique[i]);
    }
    std::vector<float> acc(size_t(art) * art * 5), cellPx4;
    int cells = 0;
    for (size_t u = 0; u < unique.size(); ++u) {
        const auto& im = images[u];
        if (!im || !im->width || !im->height || im->rgba.size() < size_t(im->width) * im->height * 4 || cells >= AtlasCells * AtlasCells) {
            pack.missing.push_back(unique[u]);
            continue;
        }
        const int cell = cells++;
        ++pack.found;
        for (int i = 0; i < n; ++i) if (ids[i] == unique[u]) pack.cellOf[size_t(i)] = int16_t(cell);
        // Area-average the icon in linear light, premultiplied. R8 match stage 1: the kit's crop (wheel.js drawIcon: 104 px from
        // (14, 13) of the 132 x 130 skill frame), as fractions of the frame upstream's cache stores, fills the art square.
        // a sub-menu's own picture (ring_wheel_menu_art.h) is framed already: the whole of it.
        const bool whole = MenuOfEntry(unique[u]) >= 0 || treeart::SlotOfArt(unique[u]) >= 0 || glyphart::SlotOfArt(unique[u]) >= 0;   // a painted tree square is the whole plate (the face crops its inside)
        const float cx0 = whole ? 0.f : im->width * (14.f / 132), cy0 = whole ? 0.f : im->height * (13.f / 130);
        const float kx = (whole ? float(im->width) : im->width * (104.f / 132)) / art, ky = (whole ? float(im->height) : im->height * (104.f / 130)) / art;
        // (ring_wheel_unbake.h): the clean icon, the glyph keyed out of the game's plate per source pixel, its tint pulled
        // to gray; our own art and a remapped icon (Kick while ring Kick icon is On) stay whole.
        const bool key = unbake::Keyed(unique[u], variant);
        if (key) ++pack.keyed;
        std::fill(acc.begin(), acc.end(), 0.f);
        for (unsigned sy = 0; sy < im->height; ++sy) for (unsigned sx = 0; sx < im->width; ++sx) {
            const uint8_t* p = &im->rgba[(size_t(sy) * im->width + sx) * 4];
            float a = p[3] / 255.f;
            uint8_t c8[3] = {p[0], p[1], p[2]};
            if (key) {
                float r = p[0] / 255.f, g = p[1] / 255.f, b = p[2] / 255.f;
                a *= unbake::Alpha(r, g, b);
                unbake::Gray(r, g, b);
                c8[0] = uint8_t(std::lround(std::clamp(r, 0.f, 1.f) * 255)); c8[1] = uint8_t(std::lround(std::clamp(g, 0.f, 1.f) * 255)); c8[2] = uint8_t(std::lround(std::clamp(b, 0.f, 1.f) * 255));
            }
            const float lin[4] = {Lut().toLinear[c8[0]] * a, Lut().toLinear[c8[1]] * a, Lut().toLinear[c8[2]] * a, a};
            // The source pixel's square in art px, split over the art pixels it covers (outside the crop: none).
            const float x0 = (sx - cx0) / kx, x1 = (sx + 1 - cx0) / kx, y0 = (sy - cy0) / ky, y1 = (sy + 1 - cy0) / ky;
            if (x1 <= 0 || y1 <= 0 || x0 >= art || y0 >= art) continue;
            for (int ty = std::max(0, int(y0)); ty < art && ty < int(std::ceil(y1)); ++ty) for (int tx = std::max(0, int(x0)); tx < art && tx < int(std::ceil(x1)); ++tx) {
                const float w = (std::min(x1, tx + 1.f) - std::max(x0, float(tx))) * (std::min(y1, ty + 1.f) - std::max(y0, float(ty)));
                if (w <= 0) continue;
                float* q = &acc[(size_t(ty) * art + size_t(tx)) * 5];
                for (int c = 0; c < 4; ++c) q[c] += lin[c] * w;
                q[4] += w;
            }
        }
        const int ox = (cell % AtlasCells) * cellPx + (cellPx - art) / 2, oy = (cell / AtlasCells) * cellPx + (cellPx - art) / 2;
        cellPx4.assign(size_t(art) * art * 4, 0.f);
        for (int y = 0; y < art; ++y) for (int x = 0; x < art; ++x) {
            const float* q = &acc[(size_t(y) * art + size_t(x)) * 5];
            const float area = std::max(q[4], 1e-6f);
            for (int c = 0; c < 4; ++c) cellPx4[(size_t(y) * art + size_t(x)) * 4 + size_t(c)] = q[c] / area;
        }
        if (key) pack.despeckled += unbake::Despeckle(cellPx4, art, art);   // the key's lone flecks and pinholes, once
        for (int y = 0; y < art; ++y) for (int x = 0; x < art; ++x)
            FromLinear(&cellPx4[(size_t(y) * art + size_t(x)) * 4], &out.data[(size_t(oy + y) * AtlasTexPx + size_t(ox + x)) * 4]);
    }
    // Mips: 2 x 2 boxes in linear light.
    for (int mip = 1; mip < IconMips; ++mip) {
        const int sp = AtlasTexPx >> (mip - 1), dp = AtlasTexPx >> mip;
        const uint8_t* src = out.data.data() + out.MipOffset(mip - 1);
        uint8_t* dst = out.data.data() + out.MipOffset(mip);
        for (int y = 0; y < dp; ++y) for (int x = 0; x < dp; ++x) {
            float sum[4] = {0, 0, 0, 0}, l[4];
            for (int k = 0; k < 4; ++k) {
                ToLinear(&src[(size_t(2 * y + k / 2) * sp + size_t(2 * x + k % 2)) * 4], l);
                for (int c = 0; c < 4; ++c) sum[c] += l[c] / 4;
            }
            FromLinear(sum, &dst[(size_t(y) * dp + size_t(x)) * 4]);
        }
    }
}

void AtlasWorker::Start(Compose* c, ID3D12Device* d, ID3D12CommandQueue* q, int f, Fonts fo, IconSource s, std::function<void()> se, void (*sa)(const char*)) {
    if (thread.joinable()) return;
    compose = c; device = d; queue = q; format = f; fonts = std::move(fo); source = std::move(s); settle = std::move(se); say = sa;
    stop = false; wanted = false; latest.reset(); failed = false; exited = false; onExit = nullptr;
    thread = std::thread([this] { Run(); });
}
void AtlasWorker::Retire(std::function<void()> exit) {
    { std::lock_guard lock(mutex); onExit = std::move(exit); stop = true; }
    wake.notify_all();
}
void AtlasWorker::Run() {
    Loop();
    std::function<void()> exit;
    { std::lock_guard lock(mutex); exit = std::move(onExit); onExit = nullptr; }
    if (exit) exit();
    exited = true;
}
void AtlasWorker::Want(const Request& r) {
    { std::lock_guard lock(mutex); want = r; wanted = true; }
    wake.notify_one();
}
std::shared_ptr<const AtlasWorker::Built> AtlasWorker::Current(const Compose& c) {
    std::lock_guard lock(mutex);
    return latest && c.Flips(AtlasKind::Icons) >= latest->iconFlip && c.Flips(AtlasKind::Glyphs) >= latest->glyphFlip ? latest : nullptr;
}
void AtlasWorker::Stop() {
    { std::lock_guard lock(mutex); stop = true; }
    wake.notify_all();
    if (thread.joinable()) thread.join();
}

void DecodeBc4Block(const uint8_t* b, uint8_t out[16]) {
    const int a0 = b[0], a1 = b[1];
    int pal[8] = {a0, a1};
    if (a0 > a1) for (int i = 0; i < 6; ++i) pal[2 + i] = ((6 - i) * a0 + (i + 1) * a1) / 7;
    else { for (int i = 0; i < 4; ++i) pal[2 + i] = ((4 - i) * a0 + (i + 1) * a1) / 5; pal[6] = 0; pal[7] = 255; }
    uint64_t bits = 0;
    for (int i = 0; i < 6; ++i) bits |= uint64_t(b[2 + i]) << (8 * i);
    for (int i = 0; i < 16; ++i) out[i] = uint8_t(pal[(bits >> (3 * i)) & 7]);
}
std::filesystem::path SpellSpriteCache() {
    HMODULE module = nullptr; wchar_t path[32768]{};
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(&SpellSpriteCache), &module) ||
        !GetModuleFileNameW(module, path, 32768)) return {};
    return std::filesystem::path(path).parent_path() / L"d2r_vr_spell_sprites" / L"sprites.ssp";
}
bool BuildFxTex(const std::filesystem::path& cacheFile, AtlasImage& out, int& served, std::string& why) {
    namespace sp = elements::sprites;
    served = 0;
    try {
        if (cacheFile.empty()) throw std::runtime_error("no cache path");
        std::ifstream in(cacheFile, std::ios::binary | std::ios::ate);
        if (!in) throw std::runtime_error("no spell sprite cache (the installer makes it)");
        const auto size = in.tellg();
        if (size <= 12 || size > 32 * 1024 * 1024) throw std::runtime_error("invalid cache size");
        std::vector<uint8_t> data(static_cast<size_t>(size)); in.seekg(0); in.read(reinterpret_cast<char*>(data.data()), size);
        if (!in) throw std::runtime_error("truncated cache");
        const auto tex = sp::Parse(data);   // upstream's checks: version, every slot's format, size and mips
        out = {}; out.kind = AtlasKind::FxTex; out.px = FxTexPx; out.bpp = 1; out.mips = FxTexMips;
        out.data.assign(out.MipOffset(FxTexMips), 0);
        // place: the slot's mip 0 decoded, box-shrunk by `shrink` (1 or 2), into (x, y) of mip 0
        auto place = [&](unsigned slot, int x, int y, int shrink) {
            const auto& t = tex[slot];
            if (t.format != 80) throw std::runtime_error("an effect texture is not BC4");   // proven formats only (step 0)
            const int w = int(t.width), h = int(t.height), bw = (w + 3) / 4;
            std::vector<uint8_t> full(size_t(w) * h);
            for (int by = 0; by < (h + 3) / 4; ++by)
                for (int bx = 0; bx < bw; ++bx) {
                    uint8_t v[16]; DecodeBc4Block(&t.mips[0][(size_t(by) * bw + bx) * 8], v);
                    for (int i = 0; i < 16; ++i) { const int px = bx * 4 + i % 4, py = by * 4 + i / 4; if (px < w && py < h) full[size_t(py) * w + px] = v[i]; }
                }
            const int dw = w / shrink, dh = h / shrink;
            if (x + dw > FxTexPx || y + dh > FxTexPx) throw std::runtime_error("effect atlas layout");
            for (int py = 0; py < dh; ++py)
                for (int px = 0; px < dw; ++px) {
                    int sum = 0;
                    for (int j = 0; j < shrink; ++j) for (int i = 0; i < shrink; ++i) sum += full[size_t(py * shrink + j) * w + px * shrink + i];
                    out.data[size_t(y + py) * FxTexPx + x + px] = uint8_t(sum / (shrink * shrink));
                }
        };
        // The shader's cells (FxFire03, FxFire04, FxSmoke, FxArcs, FxGlow in uv of the 1024 atlas).
        place(sp::FireballPlate, 0, 0, 1);    // t_fx_fire_plate_med_03 (the kit's 'fire')
        place(sp::FirePlate, 512, 0, 1);      // t_fx_fire_plate_med_04 ('fire2')
        place(sp::Smoke, 0, 512, 2);          // the smoke atlas, 2 x 2 puffs (stands in for 'cloud' and 'smoke')
        place(sp::Arcs, 512, 512, 2);         // the arc flipbook, 4 x 2 frames along x (stands in for 'arc')
        place(sp::Glow, 512, 640, 1);         // t_fx_soft_glowcard ('glowcard')
        for (int mip = 1; mip < FxTexMips; ++mip) {   // box mips
            const int sp2 = FxTexPx >> (mip - 1), dp = FxTexPx >> mip;
            const uint8_t* s = out.data.data() + out.MipOffset(mip - 1);
            uint8_t* d = out.data.data() + out.MipOffset(mip);
            for (int y = 0; y < dp; ++y)
                for (int x = 0; x < dp; ++x)
                    d[size_t(y) * dp + x] = uint8_t((s[size_t(2 * y) * sp2 + 2 * x] + s[size_t(2 * y) * sp2 + 2 * x + 1] + s[size_t(2 * y + 1) * sp2 + 2 * x] + s[size_t(2 * y + 1) * sp2 + 2 * x + 1] + 2) / 4);
        }
        served = 7;   // arc, fire, fire2, glowcard, orb (in the cache, unused yet), smoke, cloud; not the curved shockwave
        return true;
    } catch (const std::exception& e) {
        why = e.what();
        return false;
    }
}
void AtlasWorker::Loop() {
    if (!compose || !compose->Setup(device, queue, format, say)) { failed = true; return; }
    int glyphScale = -1;
    text::Metrics metrics;
    unsigned glyphStaged = 0, iconStaged = 0;
    Request last; bool any = false, fxTexTried = false;
    auto stopping = [&] { std::lock_guard lock(mutex); return stop; };
    // The staging takes one image at a time: wait for the frame thread's Pump to finish the last one.
    auto stageAll = [&](const AtlasImage& image) {
        while (!compose->Stage(image)) { if (stopping()) return false; std::this_thread::sleep_for(std::chrono::milliseconds(2)); }
        return true;
    };
    for (;;) {
        Request r;
        {
            std::unique_lock lock(mutex);
            wake.wait(lock, [&] { return stop || wanted; });
            if (stop) return;
            r = want; wanted = false;
        }
        if (any && r == last) continue;
        auto b = std::make_shared<Built>();
        b->request = r;
        if (ScalePct(r.scalePct) != glyphScale) {
            const auto t = std::chrono::steady_clock::now();
            AtlasImage image; std::string error;
            if (BuildGlyphs(fonts, r.scalePct, image, metrics, error)) { if (!stageAll(image)) return; ++glyphStaged; }
            else if (say) say(("Frame ring wheel: glyph atlas failed: " + error + "; no text").c_str());
            glyphScale = ScalePct(r.scalePct);
            b->glyphMs = Ms(t);
        }
        b->metrics = metrics;
        if (stopping()) return;   // a Retire during the glyph build: no icon build
        const auto t = std::chrono::steady_clock::now();
        AtlasImage image;
        BuildIcons(r.ids.data(), r.n, r.scalePct, source, settle, image, b->icons, r.iconVariant);
        b->iconMs = Ms(t);
        if (!stageAll(image)) return;
        ++iconStaged;
        b->iconFlip = iconStaged; b->glyphFlip = glyphStaged;
        if (say) {
            std::string missing;
            for (size_t i = 0; i < b->icons.missing.size() && i < 8; ++i) missing += (i ? "," : "") + std::to_string(b->icons.missing[i]);
            char line[256];
            std::snprintf(line, sizeof line, "Frame ring wheel: atlas icons=%d/%d missing=%s glyphs=%s footer=%s scale=%d%% worker=%.0f ms (glyphs %.0f)",
                b->icons.found, b->icons.wanted, missing.empty() ? "none" : missing.c_str(), metrics.ok ? "ok" : "none",
                !metrics.ok ? "none" : metrics.georgia ? "Georgia" : "Cinzel (Georgia missing)", glyphScale,
                b->glyphMs + b->iconMs, b->glyphMs);
            say(line);
        }
        { std::lock_guard lock(mutex); latest = b; }
        ++builds;
        last = r; any = true;
        if (compose->EffectsPending()) compose->FinishEffects(say);   // no offline blob: the effects' compile, after the first atlas
        if (!fxTexTried) {   // stage 3a: the game effect textures, once per On (ring On only: this worker runs only then)
            fxTexTried = true;
            const auto t = std::chrono::steady_clock::now();
            AtlasImage fx; int served = 0; std::string why;
            const bool ok = BuildFxTex(fxTexSet ? fxTexFile : SpellSpriteCache(), fx, served, why) && stageAll(fx);
            if (say) {
                char line[240];
                if (ok) std::snprintf(line, sizeof line, "Frame ring wheel: fx textures %d/8 from the game's spell sprite cache in %.0f ms (no curved shockwave: cold stays drawn)", served, Ms(t));
                else std::snprintf(line, sizeof line, "Frame ring wheel: fx textures 0/8 (%s): the pointed effects stay drawn", why.empty() ? "stopped" : why.c_str());
                say(line);
            }
        }
    }
}
}  // namespace framevr::ringwheel
