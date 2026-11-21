#pragma once
// Which stash tab is chosen and which shared page is shown, read off the game's own UI image.
//
// D2RLoader 1.3.1 has no call for either, and the tab bar and the page arrows send no message
// that says. The game draws both, though: the chosen tab is a lighter plate with a blue mark
// (its mean brightness ~70 against ~42 for the others in the owner's shots, 2026-11-21), and
// the shared page is the digit in "Page N / 29". The transport reads two small strips of the
// UI image back every frame (CopyUi) and this file reads them.
//
// The collection keeps its items on shared pages 6..29 (D2RLoader's add_shared_tabs), which a
// player must never see: the counter is drawn "Page N / 5" (stash_counter_art.h) and the
// arrows and bumpers stop at page 5.
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "stash_counter_art.h"

namespace framevr::stashnav {

enum Tab : int { Personal = 0, Shared = 1, Gems = 2, Materials = 3, Runes = 4, Collection = 5, TabCount = 6 };

// The tab bar strip in the 2244 x 2352 UI image: six tabs 105 px apart from x 388, their plates
// sampled between y 180 and 222 (inside each tab, 12 px from its sides).
constexpr int TabsX = 388, TabsY = 180, TabStep = 105, TabsW = TabStep * TabCount, TabsH = 42, TabInset = 12;
// The counter strip: stash_counter_art.h's box.
constexpr int CounterX = collection::counter::X, CounterY = collection::counter::Y;
constexpr int CounterW = collection::counter::Width, CounterH = collection::counter::Height;
// Beyond page 5, or a page that could not be read.
constexpr int PageBeyond = 99, PageUnknown = 0;
constexpr int PlayerPages = 5;

// sRGB-encoded half (the game's UI image) to a byte.
inline uint8_t HalfToByte(uint16_t h) {
    const uint32_t sign = (h >> 15) & 1, exp = (h >> 10) & 31, mant = h & 1023;
    float f;
    if (exp == 0) f = std::ldexp(float(mant), -24);
    else if (exp == 31) f = 1.f;
    else f = std::ldexp(float(mant | 1024), int(exp) - 25);
    if (sign) f = 0.f;
    return uint8_t(std::clamp(f, 0.f, 1.f) * 255.f + .5f);
}

// The chosen tab, from the strip's pixels (8-bit RGBA, row stride in pixels), or -1 when no plate
// stands out (a menu over the bar, a frame mid-change).
inline int ActiveTab(const uint8_t* rgba, int w, int h, int stride) {
    if (rgba == nullptr || w < TabsW || h < TabsH) return -1;
    float mean[TabCount] {};
    for (int t = 0; t < TabCount; ++t) {
        double sum = 0; int n = 0;
        for (int y = 0; y < TabsH; y += 2)
            for (int x = t * TabStep + TabInset; x < (t + 1) * TabStep - TabInset; x += 2) {
                const uint8_t* p = rgba + (size_t(y) * size_t(stride) + size_t(x)) * 4;
                sum += (p[0] + p[1] + p[2]) / 3.0; ++n;
            }
        mean[t] = n ? float(sum / n) : 0.f;
    }
    int best = 0;
    for (int t = 1; t < TabCount; ++t) if (mean[t] > mean[best]) best = t;
    float second = 0;
    for (int t = 0; t < TabCount; ++t) if (t != best) second = std::max(second, mean[t]);
    return mean[best] > 55.f && second < 52.f ? best : -1;
}

inline bool Letter(const uint8_t* p) { return p[0] > 120 && p[0] > p[2] + 30; }

// The shared page from the counter strip: 1..5, PageBeyond for a two-digit page or a digit
// past 5, PageUnknown when it does not read as "Page N / 29".
inline int SharedPage(const uint8_t* rgba, int w, int h, int stride) {
    using namespace collection::counter;
    if (rgba == nullptr || w < Width || h < Height) return PageUnknown;
    bool cols[Width] {};
    for (int x = 0; x < Width; ++x)
        for (int y = 0; y < Height && !cols[x]; ++y) cols[x] = Letter(rgba + (size_t(y) * size_t(stride) + size_t(x)) * 4);
    int runs[16][2]; int count = 0, start = -1;
    for (int x = 0; x <= Width; ++x) {
        const bool on = x < Width && cols[x];
        if (on && start < 0) start = x;
        if (!on && start >= 0) { if (count < 16) { runs[count][0] = start; runs[count][1] = x - 1; } ++count; start = -1; }
    }
    if (count == 9) return PageBeyond;   // "Page NN / 29"
    if (count != 8) return PageUnknown;
    const int x0 = runs[4][0], width = runs[4][1] - runs[4][0] + 1;
    int best = PageUnknown; float bestScore = 0;
    for (int d = 0; d < 5; ++d) {
        const Digit& digit = Digits[d];
        int both = 0, any = 0;
        for (int y = 0; y < Height; ++y)
            for (int x = 0; x < std::max(width, digit.width); ++x) {
                const bool a = x < width && Letter(rgba + (size_t(y) * size_t(stride) + size_t(x0 + x)) * 4);
                const bool b = x < digit.width && (digit.rows[y] >> x & 1u);
                both += a && b; any += a || b;
            }
        const float score = any ? float(both) / float(any) : 0.f;
        if (score > bestScore) { bestScore = score; best = d + 1; }
    }
    return bestScore > .6f ? best : PageBeyond;   // a single digit that is none of 1..5: 6..9
}

// What the strips last said, for the frame thread (MenuPointer) and the copy (CopyUi).
struct Reading {
    std::atomic<int>      tab{-1};
    std::atomic<int>      page{PageUnknown};
    std::atomic<uint64_t> at{0};   // GetTickCount64 of the last read
};
inline Reading& Last() { static Reading reading; return reading; }

// One press of a stash control, held as a short pulse the game can poll (no auto-repeat).
struct Pulse {
    uint16_t buttons = 0;
    uint8_t  lt = 0, rt = 0;
};

}   // namespace framevr::stashnav
