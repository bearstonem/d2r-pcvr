// collection-preview: paints the Collection panel to a PNG, so the look can be
// checked on a monitor (no headset). It loads the real catalog and the mod's
// own extracted item icons, marks a spread of entries as found, and draws one
// frame with the same painter the game uses.
//
//   collection-preview --catalog <collection.tsv> --icons <d2r_vr_icons> --out <png>
//                      [--category uniques|sets|runewords] [--page N] [--found "122,301"]
#include <windows.h>
#include <wincodec.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "collection_draw.h"
#include "collection_view.h"
#include "vr_settings_draw.h"

using framevr::collection::Category;
using framevr::collection::CategoryFromTag;
using framevr::collection::Library;
using framevr::collection::ViewState;

namespace {

std::wstring Widen(const char* text) {
    const int size = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
    std::wstring out(size_t(size > 0 ? size - 1 : 0), L'\0');
    if (size > 1) MultiByteToWideChar(CP_UTF8, 0, text, -1, out.data(), size);
    return out;
}

// Straight RGBA to a PNG through WIC (the same shape vr_settings_preview uses).
bool SavePng(const char* path, const std::vector<uint8_t>& rgba, int width, int height) {
    IWICImagingFactory*       wic     = nullptr;
    IWICStream*               stream  = nullptr;
    IWICBitmapEncoder*        encoder = nullptr;
    IWICBitmapFrameEncode*    frame   = nullptr;
    WICPixelFormatGUID        format  = GUID_WICPixelFormat32bppBGRA;
    const std::wstring        name    = Widen(path);
    // WIC takes BGRA; the painter hands over straight RGBA.
    std::vector<uint8_t> bgra = rgba;
    for (size_t at = 0; at + 2 < bgra.size(); at += 4) std::swap(bgra[at], bgra[at + 2]);
    const bool ok =
        SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))) &&
        SUCCEEDED(wic->CreateStream(&stream)) &&
        SUCCEEDED(stream->InitializeFromFilename(name.c_str(), GENERIC_WRITE)) &&
        SUCCEEDED(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) &&
        SUCCEEDED(encoder->Initialize(stream, WICBitmapEncoderNoCache)) &&
        SUCCEEDED(encoder->CreateNewFrame(&frame, nullptr)) && SUCCEEDED(frame->Initialize(nullptr)) &&
        SUCCEEDED(frame->SetSize(UINT(width), UINT(height))) && SUCCEEDED(frame->SetPixelFormat(&format)) &&
        format == GUID_WICPixelFormat32bppBGRA &&
        SUCCEEDED(frame->WritePixels(UINT(height), UINT(width) * 4, UINT(bgra.size()),
                                     const_cast<BYTE*>(bgra.data()))) &&
        SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit());
    for (IUnknown* part : {static_cast<IUnknown*>(frame), static_cast<IUnknown*>(encoder), static_cast<IUnknown*>(stream), static_cast<IUnknown*>(wic)})
        if (part) part->Release();
    return ok;
}

// key -> png file name, from the mod's icon cache manifest.
std::map<std::string, std::string> LoadManifest(const std::wstring& folder) {
    std::map<std::string, std::string> files;
    std::ifstream                      input(std::filesystem::path(folder) / L"manifest.tsv");
    std::string                        line;
    while (std::getline(input, line)) {
        const auto tab = line.find('\t');
        if (tab == std::string::npos) continue;
        std::string file = line.substr(tab + 1);
        while (!file.empty() && (file.back() == '\r' || file.back() == '\n')) file.pop_back();
        files[line.substr(0, tab)] = file;
    }
    return files;
}

bool DecodeIcon(IWICImagingFactory* wic, const std::wstring& path, int& outWidth, int& outHeight, std::vector<uint8_t>& outRgba) {
    IWICBitmapDecoder*      decoder   = nullptr;
    IWICBitmapFrameDecode*  frame     = nullptr;
    IWICBitmapScaler*       scaler    = nullptr;
    IWICFormatConverter*    converter = nullptr;
    UINT                    width = 0, height = 0;
    bool                    ok = false;
    if (wic && SUCCEEDED(wic->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder)) &&
        SUCCEEDED(decoder->GetFrame(0, &frame)) && SUCCEEDED(frame->GetSize(&width, &height)) && width && height &&
        SUCCEEDED(wic->CreateBitmapScaler(&scaler))) {
        const float scale = width > height ? 128.f / width : 128.f / height;
        outWidth  = int(width  > 128 ? UINT(width * scale)  : width);
        outHeight = int(height > 128 ? UINT(height * scale) : height);
        if (SUCCEEDED(scaler->Initialize(frame, UINT(outWidth), UINT(outHeight), WICBitmapInterpolationModeFant)) &&
            SUCCEEDED(wic->CreateFormatConverter(&converter)) &&
            SUCCEEDED(converter->Initialize(scaler, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) {
            outRgba.resize(size_t(outWidth) * size_t(outHeight) * 4);
            ok = SUCCEEDED(converter->CopyPixels(nullptr, UINT(outWidth) * 4, UINT(outRgba.size()), outRgba.data()));
        }
    }
    for (IUnknown* part : {static_cast<IUnknown*>(converter), static_cast<IUnknown*>(scaler), static_cast<IUnknown*>(frame), static_cast<IUnknown*>(decoder)})
        if (part) part->Release();
    return ok;
}

std::vector<uint32_t> ParseIds(const char* text) {
    std::vector<uint32_t> ids;
    if (text == nullptr) return ids;
    const char* at = text;
    while (*at) {
        while (*at == ',' || *at == ' ') ++at;
        if (!*at) break;
        ids.push_back(uint32_t(std::strtoul(at, nullptr, 10)));
        while (*at && *at != ',') ++at;
    }
    return ids;
}

}   // namespace

int main(int argc, char** argv) {
    const char* catalogPath = nullptr;
    const char* iconsPath   = nullptr;
    const char* outPath     = "collection.png";
    const char* foundText   = nullptr;
    const char* categoryName = nullptr;
    const char* sizeName     = nullptr;
    const char* tooltipText  = nullptr;
    const char* rangesText   = nullptr;
    int         page        = 0;
    int         hover       = -1;
    int         kind        = -1, sub = -1;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const auto next = [&](const char*& into) { if (i + 1 < argc) into = argv[++i]; };
        if (arg == "--catalog") next(catalogPath);
        else if (arg == "--icons") next(iconsPath);
        else if (arg == "--out") next(outPath);
        else if (arg == "--found") next(foundText);
        else if (arg == "--category") next(categoryName);
        else if (arg == "--size") next(sizeName);
        else if (arg == "--tooltip") next(tooltipText);
        else if (arg == "--ranges") next(rangesText);
        else if (arg == "--page") page = std::atoi(i + 1 < argc ? argv[++i] : "0");
        else if (arg == "--hover") hover = std::atoi(i + 1 < argc ? argv[++i] : "-1");
        else if (arg == "--kind") kind = std::atoi(i + 1 < argc ? argv[++i] : "-1");
        else if (arg == "--sub") sub = std::atoi(i + 1 < argc ? argv[++i] : "-1");
    }
    if (catalogPath == nullptr) {
        std::fprintf(stderr, "usage: collection-preview --catalog <collection.tsv> [--icons <folder>] [--out <png>] [--category uniques|sets|runewords] [--page N] [--found \"122,301\"]\n");
        return 2;
    }

    Library library;
    std::ifstream input(catalogPath);
    if (!input) { std::fprintf(stderr, "cannot read %s\n", catalogPath); return 3; }
    std::string line;
    while (std::getline(input, line)) {
        if (!library.LoadLine(line)) { std::fprintf(stderr, "malformed line: %s\n", line.c_str()); return 4; }
    }
    library.Finish();
    std::printf("catalog: %u uniques, %u sets, %u runewords\n", library.Size(Category::Uniques),
                library.Size(Category::Sets), library.Size(Category::Runewords));

    // A spread of found entries so the picture shows both states: every fifth
    // entry, plus any ids named on the command line.
    for (std::size_t i = 0; i < framevr::collection::CategoryCount; ++i) {
        const auto category = static_cast<Category>(i);
        for (uint32_t slot = 0; slot < library.Size(category); slot += 5) {
            const auto* entry = library.At(category, slot);
            if (entry) library.MarkFound(category, entry->recordId);
        }
    }
    for (uint32_t id : ParseIds(foundText)) {
        library.MarkFound(Category::Uniques, id);
        library.MarkFound(Category::Sets, id);
    }

    ViewState view;
    view.page = page;
    if (sizeName != nullptr) {
        const char first = sizeName[0];
        if (first == 's' || first == 'S') view.size = framevr::collection::panel::Size::Small;
        else if (first == 'm' || first == 'M') view.size = framevr::collection::panel::Size::Medium;
        else if (first == 'l' || first == 'L') view.size = framevr::collection::panel::Size::Large;
        else std::fprintf(stderr, "unknown size %s\n", sizeName);
    }
    if (categoryName != nullptr) {
        Category wanted = Category::Count;
        if (CategoryFromTag(categoryName, wanted)) view.SelectCategory(library, wanted);
        else std::fprintf(stderr, "unknown category %s\n", categoryName);
    }
    if (kind >= 0) view.ToggleKind(library, kind);   // Runewords: a top-row tile, and its sub-tile
    view.page = page;   // after the category, which starts at its first page
    if (sub >= 0) view.ToggleGroup(library, sub);
    view.Clamp(library);
    if (hover >= 0) view.hover = {framevr::collection::panel::HitKind::Slot, hover};   // the pointed slot, for the card

    const auto                       co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IWICImagingFactory*              wic = nullptr;
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic));
    const std::wstring               icons = iconsPath ? Widen(iconsPath) : L"";
    const auto                       manifest = icons.empty() ? std::map<std::string, std::string>{} : LoadManifest(icons);
    std::map<std::string, std::vector<uint8_t>>                  owned;   // the pixels the views point into
    std::map<std::string, framevr::collection::draw::IconPixels> views;
    const framevr::collection::draw::IconLookup lookup =
        [&](const std::string& key) -> const framevr::collection::draw::IconPixels* {
            if (manifest.empty()) return nullptr;
            if (auto it = views.find(key); it != views.end()) return it->second.rgba ? &it->second : nullptr;
            std::vector<uint8_t> pixels;
            int                  width = 0, height = 0;
            const auto           file = manifest.find(key);
            if (file != manifest.end()) DecodeIcon(wic, icons + L"\\" + Widen(file->second.c_str()), width, height, pixels);
            auto& store = owned[key];
            store       = std::move(pixels);
            framevr::collection::draw::IconPixels view;
            if (!store.empty()) {
                view.rgba   = store.data();
                view.width  = width;
                view.height = height;
            }
            views[key] = view;
            return view.rgba ? &views[key] : nullptr;
        };

    framevr::settings::draw::Painter painter;
    if (!painter.Ready()) { std::fprintf(stderr, "painter: %s\n", painter.Error().c_str()); return 5; }
    if (!painter.Begin()) { std::fprintf(stderr, "begin: %s\n", painter.Error().c_str()); return 6; }
    framevr::collection::draw::Paint(painter, library, view, lookup, tooltipText ? tooltipText : "",
                                     rangesText ? rangesText : "");
    const auto pixels = painter.End();
    if (pixels.empty()) { std::fprintf(stderr, "paint: %s\n", painter.Error().c_str()); return 7; }

    if (!SavePng(outPath, pixels, int(framevr::settings::draw::CanvasWidth), int(framevr::settings::draw::CanvasHeight))) {
        std::fprintf(stderr, "cannot write %s\n", outPath);
        return 8;
    }
    std::printf("wrote %s (%dx%d), %s page %d of %d, found %u / %u\n", outPath, int(framevr::settings::draw::CanvasWidth),
                int(framevr::settings::draw::CanvasHeight), framevr::collection::CategoryName(view.category),
                view.page + 1, view.Pages(library), library.Found(view.category), library.Size(view.category));
    if (wic) wic->Release();
    if (SUCCEEDED(co)) CoUninitialize();
    return 0;
}
