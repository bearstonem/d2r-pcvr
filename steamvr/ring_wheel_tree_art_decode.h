#pragma once
// The painted tree sheet's decode (T025; the pure side: steamvr/ring_wheel_tree_art.h): the embedded PNG decoded once on the atlas worker's
// thread, never the frame thread, and the atlas id's picture cut from it. Kept apart so ring_wheel_draw.h and the unit tests stay free of WIC.
#include <windows.h>
#include <wincodec.h>
#include <mutex>
#include "lab_kick_icon.h"   // kickicon::DecodePng: the WIC decode as upstream's icon worker does it
#include "ring_wheel_tree_art.h"

namespace framevr::ringwheel::treeart {
// The sheet's bytes in `module` (the plugin, or ring-wheel-preview, which links the same .rc). False: not there.
inline bool Bytes(HMODULE module, const void*& data, size_t& size) {
    data = nullptr; size = 0;
    HRSRC found = module ? FindResourceW(module, ResourceName, MAKEINTRESOURCEW(10)) : nullptr;   // RT_RCDATA
    HGLOBAL loaded = found ? LoadResource(module, found) : nullptr;
    data = loaded ? LockResource(loaded) : nullptr;
    size = found ? SizeofResource(module, found) : 0;
    return data && size;
}
struct Decoded { std::shared_ptr<const Image> image; size_t bytes = 0; bool found = false; };
// The sheet decoded once (the first ask), then shared. The atlas worker's thread (never the frame thread); WIC on the caller's
// thread (COM entered and left here when the thread had none). Unscaled: the sheet is 864 x 576, past the icon decode's 160 px.
inline Decoded& SheetRef() { static Decoded d; return d; }   // the preview's stand-in sheet (SetSheet) sits here before the first ask
inline void SetSheet(std::shared_ptr<const Image> image) { auto& d = SheetRef(); d.image = std::move(image); d.found = d.image != nullptr; d.bytes = 0; }
inline Decoded Get(HMODULE module) {
    static std::mutex m;
    static bool tried = false;
    Decoded& cache = SheetRef();
    std::lock_guard lock(m);
    if (cache.image) return cache;   // a stand-in set by SetSheet
    if (tried) return cache;
    tried = true;
    const void* data = nullptr;
    cache.found = Bytes(module, data, cache.bytes);
    if (!cache.found) return cache;
    const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IWICImagingFactory* wic = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)))) {
        try { cache.image = kickicon::DecodePng(wic, data, cache.bytes, unsigned(SheetW)); } catch (...) { cache.image = nullptr; }
        wic->Release();
    }
    if (SUCCEEDED(co)) CoUninitialize();
    return cache;
}

// The atlas id's picture for the worker: the slot's cell of the decoded sheet (null: not painted, or no sheet).
inline std::shared_ptr<const Image> Picture(HMODULE module, int id) {
    const int slot = SlotOfArt(id);
    if (slot < 0 || !Painted(slot)) return nullptr;
    const auto d = Get(module);
    return d.image ? Slice(*d.image, slot) : nullptr;
}
}  // namespace framevr::ringwheel::treeart
