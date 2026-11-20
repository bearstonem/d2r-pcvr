// collection_ui_image.h: the page converted for the game's UI image.
#include "collection_ui_image.h"
#include <cstdio>
#include <cmath>

using namespace framevr::collection::uiimage;

static float FromHalf(uint16_t h) {
    const int e = (h >> 10) & 31, m = h & 1023;
    if (e == 0) return std::ldexp(float(m), -24);
    return std::ldexp(1.f + m / 1024.f, e - 15);
}
static int failures = 0;
static void Near(const char* what, float got, float want, float tol) {
    if (std::fabs(got - want) > tol) { std::printf("FAIL %s: %f, want %f\n", what, got, want); ++failures; }
}

int main() {
    Near("half 1", FromHalf(Half(1.f)), 1.f, 0);
    Near("half .5", FromHalf(Half(.5f)), .5f, 0);
    Near("half .2140", FromHalf(Half(.2140f)), .2140f, .0002f);
    Near("half tiny", FromHalf(Half(1e-5f)), 1e-5f, 1e-6f);
    Near("half 0", FromHalf(Half(0.f)), 0.f, 0);
    // 2x2 straight RGBA: white opaque, mid grey opaque, transparent, white at half coverage.
    const std::vector<uint8_t> src = {255,255,255,255, 128,128,128,255, 0,0,0,0, 255,255,255,128};
    auto same = Convert(src, 2, 2, 2, 2, 2);
    Near("white r", FromHalf(same[0]), 1.f, .002f);
    Near("white a", FromHalf(same[3]), 1.f, 0);
    Near("grey r", FromHalf(same[4]), .2158f, .002f);   // sRGB 128 -> linear .2158
    Near("clear a", FromHalf(same[11]), 0.f, 0);
    Near("half-covered white r", FromHalf(same[12]), .502f, .003f);   // linear 1, premultiplied by .502
    Near("half-covered white a", FromHalf(same[15]), .502f, .003f);
    auto big = Convert(src, 2, 2, 2, 7, 5);
    if (big.size() != size_t(7 * 5 * 4)) { std::printf("FAIL size\n"); ++failures; }
    for (auto v : big) if (FromHalf(v) > 1.0001f || std::isnan(FromHalf(v))) { std::printf("FAIL range\n"); ++failures; break; }
    // A wider bitmap: only its left columns are the page (stride 3, page 2 wide; the third column is red).
    const std::vector<uint8_t> wide = {255,255,255,255, 255,255,255,255, 255,0,0,255,
                                       255,255,255,255, 255,255,255,255, 255,0,0,255};
    auto page = Convert(wide, 3, 2, 2, 4, 2);
    for (size_t i = 0; i < page.size(); i += 4) Near("no red past the page", FromHalf(page[i + 1]), 1.f, .002f);
    if (!Convert(src, 2, 2, 2, 0, 5).empty() || !Convert({}, 2, 2, 2, 3, 3).empty() || !Convert(src, 1, 2, 2, 3, 3).empty()) {
        std::printf("FAIL guards\n"); ++failures;
    }
    std::printf(failures ? "%d failure(s)\n" : "collection_ui_image: all passed\n", failures);
    return failures ? 1 : 0;
}
