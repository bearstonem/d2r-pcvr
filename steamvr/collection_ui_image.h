#pragma once
// The Collection page in the game's own UI image (variant B): the painter's picture
// (8-bit RGBA, sRGB-encoded, premultiplied) resampled to the page's rectangle of the
// UI image and converted to that image's own format - R16G16B16A16_FLOAT, linear light,
// premultiplied, alpha = coverage (as UiBlit writes it). Done on the background painter,
// so the render thread only copies finished rows (transport.cpp CopyUi).
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace framevr::collection::uiimage {

// IEEE half from a float in [0, 1] (no infinities or NaN reach here).
inline uint16_t Half(float f) {
    if (!(f > 0.f)) return 0;
    if (f >= 65504.f) return 0x7BFF;
    uint32_t bits;
    std::memcpy(&bits, &f, 4);
    const int exp = int((bits >> 23) & 0xFF) - 127 + 15;
    if (exp <= 0) {   // subnormal half
        if (exp < -10) return 0;
        const uint32_t mant = (bits & 0x7FFFFF) | 0x800000;
        return uint16_t((mant >> (14 - exp)) + ((mant >> (13 - exp)) & 1));
    }
    const uint32_t mant = bits & 0x7FFFFF;
    uint32_t h = (uint32_t(exp) << 10) | (mant >> 13);
    if (mant & 0x1000) ++h;   // round half up
    return uint16_t(h);
}

// sRGB-encoded [0, 1] to linear light.
inline float Linear(float e) {
    return e <= 0.04045f ? e / 12.92f : std::pow((e + 0.055f) / 1.055f, 2.4f);
}

// src: srcW x srcH RGBA bytes, premultiplied, sRGB-encoded. Out: outW x outH x 4 halves.
// Bilinear on the premultiplied bytes (no dark fringes), then each pixel unpremultiplied,
// decoded to linear and premultiplied again. A 4096-entry table keeps it cheap.
inline std::vector<uint16_t> Convert(const std::vector<uint8_t>& src, int srcW, int srcH, int outW, int outH) {
    std::vector<uint16_t> out;
    if (srcW <= 0 || srcH <= 0 || outW <= 0 || outH <= 0 || src.size() < size_t(srcW) * size_t(srcH) * 4) return out;
    out.resize(size_t(outW) * size_t(outH) * 4);
    static const std::vector<float> table = [] {
        std::vector<float> t(4097);
        for (int i = 0; i <= 4096; ++i) t[i] = Linear(float(i) / 4096.f);
        return t;
    }();
    const float sx = float(srcW) / float(outW), sy = float(srcH) / float(outH);
    for (int y = 0; y < outH; ++y) {
        const float fy = std::fmax((float(y) + .5f) * sy - .5f, 0.f);
        const int y0 = int(fy) < srcH - 1 ? int(fy) : srcH - 1, y1 = y0 + 1 < srcH ? y0 + 1 : srcH - 1;
        const float ty = fy - float(y0);
        const uint8_t* r0 = src.data() + size_t(y0) * srcW * 4;
        const uint8_t* r1 = src.data() + size_t(y1) * srcW * 4;
        uint16_t* o = out.data() + size_t(y) * outW * 4;
        for (int x = 0; x < outW; ++x, o += 4) {
            const float fx = std::fmax((float(x) + .5f) * sx - .5f, 0.f);
            const int x0 = int(fx) < srcW - 1 ? int(fx) : srcW - 1, x1 = x0 + 1 < srcW ? x0 + 1 : srcW - 1;
            const float tx = fx - float(x0);
            float c[4];
            for (int k = 0; k < 4; ++k) {
                const float top = r0[x0 * 4 + k] + (r0[x1 * 4 + k] - float(r0[x0 * 4 + k])) * tx;
                const float bot = r1[x0 * 4 + k] + (r1[x1 * 4 + k] - float(r1[x0 * 4 + k])) * tx;
                c[k] = (top + (bot - top) * ty) / 255.f;
            }
            const float a = c[3];
            if (a <= 1.f / 512.f) { o[0] = o[1] = o[2] = o[3] = 0; continue; }
            for (int k = 0; k < 3; ++k) {
                const float e = std::fmin(c[k] / a, 1.f);
                o[k] = Half(table[int(e * 4096.f + .5f)] * a);
            }
            o[3] = Half(a);
        }
    }
    return out;
}

}  // namespace framevr::collection::uiimage
