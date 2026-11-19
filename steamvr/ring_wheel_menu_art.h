#pragma once
// Ring skill menu (the player "the Gestures, Glyphs and Quick cast icons show gold; they must match the Diablo
// silver-on-rust set"): the three sub-menus' own pictures, our ChatGPT art (the design kit, icon-glyphs-v3b,
// icon-gestures-v2, icon-quick-cast-v2), made offline the way the ring shows a game icon after the key: the silver motif
// keyed out of its rust plate, its tint pulled to gray, on transparent, 144 px (the atlas art square at 150%), framed so the motif
// fills the disc like a game glyph (make-ring-menu-icons.py there). Embedded by steamvr/ring_wheel_menu_art.rc
// (steamvr/assets/ring-wheel/ring-menu-*.png). The atlas worker packs them whole (no game-frame crop, no key: ids 9000 and up), and the
// compose shader draws our dark disc under them and cuts them round, as every seat. Drawn on the center's thirds (their marks) and
// on the sub-menus' General shelf entries (and ring Quick cast radial's entry); a picture that fails to decode keeps the drawn
// mark in silver (ring_wheel_draw.h PushSigil).
// Decoded on the ring's atlas worker (never the frame thread), once a process each, then shared.
#include <windows.h>
#include <wincodec.h>
#include <array>
#include <memory>
#include <mutex>
#include "ring_wheel_png.h"   // png::Decode / the WIC decode as upstream's icon worker does it (long side <= 160 px)
#include "ring_wheel.h"

namespace framevr::ringwheel::menuart {
using Image = interaction::icons::Image;
inline constexpr const wchar_t* ResourceName[ThirdCount] = {L"D2RVR_RING_MENU_GLYPHS", L"D2RVR_RING_MENU_GESTURES", L"D2RVR_RING_MENU_QUICK_CAST"};
// The atlas id of each sub-menu's picture: its shelf entry's id (MenuEntryId; Quick cast's 9103 is asked for the radial's entry
// and the center's third, never a shelf seat of its own).
inline int ArtId(int third) { return MenuEntryId(third); }
inline int ThirdOfArt(int id) { return MenuOfEntry(id); }

// The picture's bytes in `module` (the plugin, or ring-wheel-preview, which links the same.rc). False: not there.
inline bool Bytes(HMODULE module, int third, const void*& data, size_t& size) {
    data = nullptr; size = 0;
    if (third < 0 || third >= ThirdCount) return false;
    HRSRC found = module ? FindResourceW(module, ResourceName[third], MAKEINTRESOURCEW(10)) : nullptr;   // RT_RCDATA
    HGLOBAL loaded = found ? LoadResource(module, found) : nullptr;
    data = loaded ? LockResource(loaded) : nullptr;
    size = found ? SizeofResource(module, found) : 0;
    return data && size;
}

struct Decoded { std::shared_ptr<const Image> image; size_t bytes = 0; bool found = false; };
// Each picture decoded once (the first ask), then the same image. Any thread; WIC on the caller's thread (COM entered and left
// here when the thread had none).
inline Decoded Get(HMODULE module, int third) {
    static std::mutex m;
    static std::array<Decoded, ThirdCount> cache{};
    static std::array<bool, ThirdCount> tried{};
    if (third < 0 || third >= ThirdCount) return {};
    std::lock_guard lock(m);
    if (tried[size_t(third)]) return cache[size_t(third)];
    tried[size_t(third)] = true;
    Decoded& d = cache[size_t(third)];
    const void* data = nullptr;
    d.found = Bytes(module, third, data, d.bytes);
    if (!d.found) return d;
    const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IWICImagingFactory* wic = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)))) {
        try { d.image = png::Decode(wic, data, d.bytes); } catch (...) { d.image = nullptr; }
        wic->Release();
    }
    if (SUCCEEDED(co)) CoUninitialize();
    return d;
}
}  // namespace framevr::ringwheel::menuart
