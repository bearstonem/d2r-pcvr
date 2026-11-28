#pragma once
// The mark the game puts on its chosen stash tab: its background lifts, a gold frame runs along
// the top and both sides (the bottom is left open) and the game's own blue diamond sits near the
// bottom. Redrawn by the mod on its own sixth tab while the Collection page is open, because the
// mod cannot set the game's chosen tab (no widget API).
//
// Read off the owner's UI shot (2026-11-19/20): the lift is a warm lightening of the tab, the
// frame is gold and inside the tab's edge, and the gem is the game's own sprite (tab_gem.h, cut
// from a UI shot), a good deal bigger and higher than a first pass had it.
//
// Straight RGBA, the convention stashbuttons::PaintFace uses for the static atlas (the
// transport's quads blend it with SOURCE_ALPHA).
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "tab_gem.h"

namespace framevr::tabhighlight {

// Painted once into the static atlas; scaled to the tab's own size on screen.
constexpr int Width = 160, Height = 120;

inline std::vector<uint8_t> Paint(int width = Width, int height = Height) {
    std::vector<uint8_t> pixels;
    if (width <= 0 || height <= 0) return pixels;
    pixels.assign(size_t(width) * size_t(height) * 4, 0);

    const float w = float(width), h = float(height);
    const float cx = w * 0.5f;
    const float inset = std::max(2.0f, h * 0.09f);   // the frame's band, inside the tab's edge

    // The lift: a warm lightening over the whole tab, so the chosen tab reads lighter than its
    // neighbours (the game does the same to its own active tab).
    for (int py = 0; py < height; ++py) {
        for (int px = 0; px < width; ++px) {
            uint8_t* out = &pixels[(size_t(py) * width + px) * 4];
            out[0] = 0x6E; out[1] = 0x62; out[2] = 0x4C; out[3] = 0x70;   // ~44% warm grey
        }
    }

    // The frame: the top edge and both sides, supersampled; the bottom edge stays open.
    const auto channel = [](uint32_t rgb, int shift) { return float((rgb >> shift) & 0xFF) / 255.0f; };
    constexpr uint32_t goldTop = 0xF0D68Cu, goldMid = 0xD0AC5Eu, goldLow = 0x94783Au;
    for (int py = 0; py < height; ++py) {
        for (int px = 0; px < width; ++px) {
            float r = 0, g = 0, b = 0;
            int   covered = 0;
            for (int sy = 0; sy < 3; ++sy) {
                for (int sx = 0; sx < 3; ++sx) {
                    const float x = px + (sx + 0.5f) / 3.0f, y = py + (sy + 0.5f) / 3.0f;
                    if (!(x < inset || x > w - inset || y < inset)) continue;
                    const float t = std::clamp(y / h, 0.0f, 1.0f);
                    const uint32_t rgb = t < 0.45f ? goldTop : t < 0.8f ? goldMid : goldLow;
                    r += channel(rgb, 16); g += channel(rgb, 8); b += channel(rgb, 0);
                    ++covered;
                }
            }
            if (covered == 0) continue;
            const float inv = 1.0f / float(covered);
            uint8_t*    out = &pixels[(size_t(py) * width + px) * 4];
            out[0] = uint8_t(std::clamp(r * inv, 0.0f, 1.0f) * 255.0f + 0.5f);
            out[1] = uint8_t(std::clamp(g * inv, 0.0f, 1.0f) * 255.0f + 0.5f);
            out[2] = uint8_t(std::clamp(b * inv, 0.0f, 1.0f) * 255.0f + 0.5f);
            out[3] = uint8_t((float(covered) / 9.0f) * 255.0f + 0.5f);
        }
    }

    // The gem: the game's own diamond, scaled up and centred a little above the tab's bottom.
    constexpr float scale = 1.7f;
    const int gw = std::max(1, int(GemWidth * scale)), gh = std::max(1, int(GemHeight * scale));
    const int gx = int(std::lround(cx - gw * 0.5f));
    const int gy = int(std::lround(h * 0.78f - gh * 0.5f));
    for (int j = 0; j < gh; ++j) {
        for (int i = 0; i < gw; ++i) {
            const int x = gx + i, y = gy + j;
            if (x < 0 || x >= width || y < 0 || y >= height) continue;
            const uint8_t* src = &Gem[(size_t(j * GemHeight / gh) * GemWidth + i * GemWidth / gw) * 4];
            if (src[3] == 0) continue;
            uint8_t* out = &pixels[(size_t(y) * width + x) * 4];
            out[0] = src[0]; out[1] = src[1]; out[2] = src[2]; out[3] = src[3];
        }
    }
    return pixels;
}

}   // namespace framevr::tabhighlight
