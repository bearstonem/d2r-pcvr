#pragma once
// The painted glyph sheet's decode (the pure side: steamvr/ring_wheel_glyph_art.h): the embedded PNG decoded once on the atlas worker's
// thread, never the frame thread, and the atlas id's picture cut from it. Same shape as ring_wheel_tree_art_decode.h.
#include <windows.h>
#include <wincodec.h>
#include <mutex>
#include "ring_wheel_png.h"   // png::Decode: the WIC decode as upstream's icon worker does it
#include "ring_wheel_glyph_art.h"

namespace framevr::ringwheel::glyphart {
inline bool Bytes(HMODULE module, const void*& data, size_t& size) {
    data = nullptr; size = 0;
    HRSRC found = module ? FindResourceW(module, ResourceName, MAKEINTRESOURCEW(10)) : nullptr;   // RT_RCDATA
    HGLOBAL loaded = found ? LoadResource(module, found) : nullptr;
    data = loaded ? LockResource(loaded) : nullptr;
    size = found ? SizeofResource(module, found) : 0;
    return data && size;
}
struct Decoded { std::shared_ptr<const Image> image; size_t bytes = 0; bool found = false; };
inline Decoded& SheetRef() { static Decoded d; return d; }   // the preview's stand-in sheet (SetSheet) sits here before the first ask
inline void SetSheet(std::shared_ptr<const Image> image) { auto& d = SheetRef(); d.image = std::move(image); d.found = d.image != nullptr; d.bytes = 0; }
// The sheet decoded once (the first ask), then shared. The atlas worker's thread; WIC on the caller's thread (COM entered and left
// here when the thread had none). Unscaled: the sheet is 576 x 432, past the icon decode's 160 px.
inline Decoded Get(HMODULE module) {
    static std::mutex m;
    static bool tried = false;
    Decoded& cache = SheetRef();
    std::lock_guard lock(m);
    if (cache.image) return cache;
    if (tried) return cache;
    tried = true;
    const void* data = nullptr;
    cache.found = Bytes(module, data, cache.bytes);
    if (!cache.found) return cache;
    const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IWICImagingFactory* wic = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)))) {
        try { cache.image = png::Decode(wic, data, cache.bytes, unsigned(SheetW)); } catch (...) { cache.image = nullptr; }
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
}  // namespace framevr::ringwheel::glyphart
