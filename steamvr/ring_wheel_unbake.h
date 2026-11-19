#pragma once
// Lab Ring wheel (T036, Jeff 2026-11-15: "clean icons only (silver glyph on our dark disc; game plate gone)"): the clean icon,
// keyed out of the cached game picture once per icon on the atlas worker (vr/ring_wheel_atlas.cpp BuildIcons), never per
// frame. Pure and Windows-free (ring-wheel-test).
//
// The game's skill picture is one opaque cell: a rusted iron plate (bevel, rivets) with the silver glyph stamped in; no layer
// of its own. The plate is a saturated rust
// brown, the glyph near-gray silver, so the key is the mockup's (mock.js unbake): alpha = (saturation under 0.18 to 0.32) x
// (value over 0.14 to 0.24), soft edges, and the glyph's leftover rust tint pulled 65% toward gray. The compose shader draws
// our own dark disc under it. A per-class median plate was tried as the key and kept too much rust.
//
// Cleanup (speckles): the key leaves lone gray flecks on the plate (rivet glints, the bevel's highlight) and pinholes in the
// glyph (rust grain inside it). One pass at the art's size after the area average: a pixel whose 8 neighbors hold under
// IsolatedSum of alpha in all is dropped; a pixel whose 8 neighbors average over FillAvg is raised to that average.
//
// Skipped (Keyed false): our own art (ids 9000 and up: Draw glyph, Lab Army X menu, the sub-menu entries) and an icon a Lab
// remaps while its remap is On (Lab Kick icon's s1, its variant bit): any colored part of those would be keyed out, and Kick's
// art is not proven against the key (Jeff: skip it until checked).
#include <algorithm>
#include <cstdint>
#include <vector>

namespace framevr::ringwheel::unbake {
constexpr float SatLo = .18f, SatHi = .32f, ValLo = .14f, ValHi = .24f, GrayPull = .65f;
constexpr float IsolatedSum = .9f, FillAvg = .8f;
constexpr int OurArtFirst = 9000;
constexpr uint32_t KickVariantBit = 1;   // lab_kick_icon.h kickicon::VariantBit (its remap of generic skill 1)
constexpr int KickSkill = 1;
// Gamma (straight) rgb 0-1 of one picture pixel: how much of it is glyph.
inline float Alpha(float r, float g, float b) {
    const float mx = std::max({r, g, b}), mn = std::min({r, g, b}), sat = (mx - mn) / std::max(mx, 1e-3f);
    return std::clamp((SatHi - sat) / (SatHi - SatLo), 0.f, 1.f) * std::clamp((mx - ValLo) / (ValHi - ValLo), 0.f, 1.f);
}
// The glyph keeps its own silver; any rust tint left in its soft edge is pulled toward gray.
inline void Gray(float& r, float& g, float& b) {
    const float lum = .3f * r + .59f * g + .11f * b, k = 1 - GrayPull;
    r = lum + (r - lum) * k; g = lum + (g - lum) * k; b = lum + (b - lum) * k;
}
inline bool Keyed(int id, uint32_t variant) { return id >= 0 && id < OurArtFirst && !(id == KickSkill && (variant & KickVariantBit)); }
// px: w x h premultiplied RGBA, 4 floats a pixel (any linear space). Returns how many pixels it changed.
inline int Despeckle(std::vector<float>& px, int w, int h) {
    if (w < 3 || h < 3 || px.size() < size_t(w) * size_t(h) * 4) return 0;
    const std::vector<float> in = px;
    int changed = 0;
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        float sum = 0, rgb[3] = {0, 0, 0}; int nb = 0;
        for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
            if (!dx && !dy) continue;
            const int nx = x + dx, ny = y + dy;
            if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
            const float* q = &in[(size_t(ny) * w + size_t(nx)) * 4];
            sum += q[3]; for (int c = 0; c < 3; ++c) rgb[c] += q[c]; ++nb;
        }
        float* p = &px[(size_t(y) * w + size_t(x)) * 4];
        const float a = p[3], avg = nb ? sum / nb : 0;
        if (a > 0 && sum < IsolatedSum) { p[0] = p[1] = p[2] = p[3] = 0; ++changed; }
        else if (nb == 8 && avg > FillAvg && a < avg) { for (int c = 0; c < 3; ++c) p[c] = rgb[c] / nb; p[3] = avg; ++changed; }
    }
    return changed;
}
}  // namespace framevr::ringwheel::unbake
