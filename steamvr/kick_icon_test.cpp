// Kick (generic skill 1, icon key "s1") through the real icon cache (interaction_icons.cpp) and the art embedded by
// interaction_assets.rc. A fake d2r_vr_icons folder holds a stand-in for the game's empty Kick plate under s1, plus s2,
// s10 and an item: s1 alone reads the embedded art, every other key reads the cache exactly as before. A cache without
// s1 (or none at all) still gets the art.
#include <windows.h>
#include <wincodec.h>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include "interaction_icons.h"

static std::vector<std::string> g_lines;
namespace framevr::logging { void Write(const char* text) { g_lines.emplace_back(text); } }

namespace {
namespace icons = framevr::interaction::icons;
int failures = 0;
void Check(bool ok, const char* what) {
    std::printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++failures;
}
IWICImagingFactory* Wic() {
    static IWICImagingFactory* wic = [] {
        IWICImagingFactory* f = nullptr;
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f));
        return f;
    }();
    return wic;
}
// A w x h PNG of one straight RGBA color (the fake cache's pictures), written as 32 bpp BGRA (the PNG encoder's own).
bool WritePng(const std::filesystem::path& file, unsigned w, unsigned h, uint32_t rgba) {
    std::vector<uint8_t> px(size_t(w) * h * 4);
    const uint8_t r = uint8_t(rgba >> 24), g = uint8_t(rgba >> 16), b = uint8_t(rgba >> 8), a = uint8_t(rgba);
    for (size_t i = 0; i < px.size(); i += 4) { px[i] = b; px[i + 1] = g; px[i + 2] = r; px[i + 3] = a; }
    IWICStream* stream = nullptr; IWICBitmapEncoder* enc = nullptr; IWICBitmapFrameEncode* frame = nullptr;
    WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
    bool ok = Wic() && SUCCEEDED(Wic()->CreateStream(&stream)) && SUCCEEDED(stream->InitializeFromFilename(file.c_str(), GENERIC_WRITE)) &&
              SUCCEEDED(Wic()->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc)) && SUCCEEDED(enc->Initialize(stream, WICBitmapEncoderNoCache)) &&
              SUCCEEDED(enc->CreateNewFrame(&frame, nullptr)) && SUCCEEDED(frame->Initialize(nullptr)) && SUCCEEDED(frame->SetSize(w, h)) &&
              SUCCEEDED(frame->SetPixelFormat(&fmt)) && fmt == GUID_WICPixelFormat32bppBGRA &&
              SUCCEEDED(frame->WritePixels(h, w * 4, UINT(px.size()), px.data())) && SUCCEEDED(frame->Commit()) && SUCCEEDED(enc->Commit());
    for (IUnknown* p : {static_cast<IUnknown*>(frame), static_cast<IUnknown*>(enc), static_cast<IUnknown*>(stream)}) if (p) p->Release();
    return ok;
}
// The cached picture for key, after the worker had its turn (null: none).
std::shared_ptr<const icons::Image> Load(const std::string& key) {
    icons::Get(key);
    icons::Wait(5000);
    return icons::Get(key);
}
bool Solid(const std::shared_ptr<const icons::Image>& im, uint32_t rgba) {
    if (!im || im->width != 4 || im->height != 4 || im->rgba.size() != 64) return false;
    for (size_t i = 0; i < im->rgba.size(); i += 4) for (int k = 0; k < 4; ++k) if (im->rgba[i + k] != uint8_t(rgba >> (24 - 8 * k))) return false;
    return true;
}
bool Same(const std::shared_ptr<const icons::Image>& a, const std::shared_ptr<const icons::Image>& b) {
    return a && b && a->width == b->width && a->height == b->height && a->rgba == b->rgba;
}
size_t Told(const char* what) { size_t n = 0; for (const auto& l : g_lines) n += l.find(what) != std::string::npos; return n; }
}  // namespace

int main() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    HRSRC found = FindResourceW(nullptr, L"D2RVR_KICK_ICON", MAKEINTRESOURCEW(10));
#ifdef KICK_ICON_TEST_NO_ART
    // Built without interaction_assets.rc: Kick falls back to the cache's own s1, as before this change.
    Check(!found, "no embedded Kick art in this build");
    const auto root = std::filesystem::current_path() / "kick-icon-no-art-test-cache";
    std::error_code ec; std::filesystem::remove_all(root, ec); std::filesystem::create_directories(root);
    const std::string file = std::string(64, 'b') + ".png";
    Check(WritePng(root / file, 4, 4, 0x403020FFu), "the fake cache's plate written");
    { std::ofstream m(root / "manifest.tsv", std::ios::binary); m << "s1\t" << file << "\n"; }
    icons::Directory(root.wstring());
    Check(Solid(Load("s1"), 0x403020FFu), "art missing: s1 is the cache's own picture");
    Check(Told("Frame kick icon: the built-in art is missing; skill 1 (Kick) uses the local icon cache") == 1 && g_lines.size() == 1, "one line says so");
    icons::Shutdown();
    std::filesystem::remove_all(root, ec);
    std::printf("%s (%d failure%s)\n", failures ? "kick-icon-no-art-test FAILED" : "kick-icon-no-art-test: all passed", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
#else
    // The art itself, as embedded in this module by interaction_assets.rc.
    const DWORD bytes = found ? SizeofResource(nullptr, found) : 0;
    Check(found && bytes > 1000 && bytes < 64 * 1024, "the Kick art is embedded (D2RVR_KICK_ICON), under 64 KB");

    // A fake icon cache in this test's working folder, read by the real worker.
    const auto root = std::filesystem::current_path() / "kick-icon-test-cache";
    const auto dir = root / "with-s1", bare = root / "without-s1";
    std::error_code ec; std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(dir); std::filesystem::create_directories(bare);
    const std::string hex(64, 'a');
    auto name = [&](char c) { std::string n = hex; n[0] = c; return n + ".png"; };
    const uint32_t blank = 0x403020FFu, two = 0x00FF00FFu, ten = 0x0000FFFFu, item = 0xFF00FFFFu;
    Check(WritePng(dir / name('1'), 4, 4, blank) && WritePng(dir / name('2'), 4, 4, two) && WritePng(dir / name('3'), 4, 4, ten) &&
          WritePng(dir / name('4'), 4, 4, item) && WritePng(bare / name('2'), 4, 4, two), "the fake caches' pictures written");
    {
        std::ofstream m(dir / "manifest.tsv", std::ios::binary);
        m << "s1\t" << name('1') << "\n" << "s2\t" << name('2') << "\n" << "s10\t" << name('3') << "\n" << "i20202020\t" << name('4') << "\n";
        std::ofstream b(bare / "manifest.tsv", std::ios::binary);
        b << "s2\t" << name('2') << "\n";
    }

    // A cache that holds the game's empty plate under s1.
    icons::Directory(dir.wstring());
    const auto art = Load("s1");
    Check(art && !Solid(art, blank), "s1 is not the cache's empty plate");
    Check(art && art->width == 128 && art->height == 128 && art->rgba.size() == 128u * 128 * 4, "s1 is the 128 x 128 art (under the worker's 160 px cap)");
    if (art) {
        Check(art->rgba[3] == 0 && art->rgba[art->rgba.size() - 1] == 0, "its corners are transparent (real alpha outside the plate)");
        const size_t mid = (64 * 128 + 64) * 4;
        Check(art->rgba[mid + 3] >= 250, "its middle is the plate, opaque");
    }
    Check(Solid(Load("s2"), two) && Solid(Load("s10"), ten) && Solid(Load("i20202020"), item), "s2, s10 and an item as the cache has them");
    Check(!Load("s7") && !Load("s11") && !Load("s100"), "keys the cache lacks stay empty (no other key gets the art)");
    Check(Told("Frame kick icon: skill 1 (Kick) uses the built-in art, 128x128") == 1 && g_lines.size() == 1, "one line, for the one decode");
    Load("s1"); Load("s1");
    Check(Same(icons::Get("s1"), art) && g_lines.size() == 1, "repeat lookups: the cached art, no second decode or line");
    icons::Shutdown();

    // A cache without s1: Kick still gets the art; s2 is unchanged.
    icons::Directory(bare.wstring());
    Check(Same(Load("s1"), art), "no s1 in the cache: s1 is still the art");
    Check(Solid(Load("s2"), two) && !Load("s10"), "the other cache's keys as it has them");
    icons::Shutdown();

    // No cache at all: the art alone.
    icons::Directory((root / "missing").wstring());
    Check(Same(Load("s1"), art) && !Load("s2"), "no cache folder: s1 is the art, s2 empty");
    Check(Told("uses the built-in art") == 3 && Told("built-in art is") == 0, "one decode line per worker session, no failure line");
    icons::Shutdown();

    std::filesystem::remove_all(root, ec);
    std::printf("%s (%d failure%s)\n", failures ? "kick-icon-test FAILED" : "kick-icon-test: all passed", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
#endif
}
