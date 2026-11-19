#pragma once
// The ring menu's own PNG decode (its pictures are embedded as PNG bytes): as upstream's icon worker decodes a cache file
// (interaction_icons.cpp Decode): the long side at most maxSide px, Fant scaling, straight 32 bpp RGBA. Null on any failure.
// wic: an IWICImagingFactory* (the worker's own). The tree and glyph sheets pass their own width (past the 160 px icon default).
#include <windows.h>
#include <wincodec.h>
#include <algorithm>
#include <memory>
#include "interaction_icons.h"

namespace framevr::ringwheel::png {
using Image = interaction::icons::Image;
constexpr unsigned MaxSide = 160;
inline std::shared_ptr<const Image> Decode(IWICImagingFactory* wic, const void* data, size_t size, unsigned maxSide = MaxSide) {
    if (!wic || !data || !size || size > 64u * 1024 * 1024) return nullptr;
    IWICStream* stream = nullptr; IWICBitmapDecoder* decoder = nullptr; IWICBitmapFrameDecode* frame = nullptr;
    IWICBitmapScaler* scaler = nullptr; IWICFormatConverter* converter = nullptr;
    auto result = std::make_shared<Image>();
    UINT w = 0, h = 0;
    if (SUCCEEDED(wic->CreateStream(&stream)) &&
        SUCCEEDED(stream->InitializeFromMemory(static_cast<BYTE*>(const_cast<void*>(data)), DWORD(size))) &&
        SUCCEEDED(wic->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnLoad, &decoder)) &&
        SUCCEEDED(decoder->GetFrame(0, &frame)) && SUCCEEDED(frame->GetSize(&w, &h)) && w && h && w <= 8192 && h <= 8192 &&
        SUCCEEDED(wic->CreateBitmapScaler(&scaler))) {
        const float scale = std::min(1.f, float(maxSide) / float(std::max(w, h)));
        result->width = std::max(1u, unsigned(w * scale));
        result->height = std::max(1u, unsigned(h * scale));
        if (SUCCEEDED(scaler->Initialize(frame, result->width, result->height, WICBitmapInterpolationModeFant)) &&
            SUCCEEDED(wic->CreateFormatConverter(&converter)) &&
            SUCCEEDED(converter->Initialize(scaler, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) {
            result->rgba.resize(size_t(result->width) * result->height * 4);
            if (FAILED(converter->CopyPixels(nullptr, result->width * 4, UINT(result->rgba.size()), result->rgba.data()))) result->rgba.clear();
        }
    }
    for (IUnknown* p : {static_cast<IUnknown*>(converter), static_cast<IUnknown*>(scaler), static_cast<IUnknown*>(frame),
                        static_cast<IUnknown*>(decoder), static_cast<IUnknown*>(stream)})
        if (p) p->Release();
    if (result->rgba.empty()) return nullptr;
    return result;
}
}  // namespace framevr::ringwheel::png