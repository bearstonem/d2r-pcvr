#pragma once
// Quick cast radial: where its two regions sit in upstream's shared dynamic
// image (steamvr/panel_atlas.h: VR Settings, the glyph trail, the blood visor, the strain light, the quest card, the bow laser,
// arcane decay). The ring's pictures change with the character and the
// edits, so they cannot live in the static image (painted once a session); the shared image is re-uploaded by region, as
// upstream's own panels are (UploadShared), and needs no swapchain of its own.
#include "quick_radial.h"
#include "panel_atlas.h"

namespace framevr::quickradial::shared {
namespace pa = framevr::panelatlas;
// Region A: right of arcane decay, under the bow laser, to the image's right edge. Region B: under arcane decay.
constexpr pa::Region A{pa::ArcaneDecay.x + pa::ArcaneDecay.w + pa::Gap, pa::BowLaser.y + pa::BowLaser.h + pa::Gap, atlas::AW, atlas::AH};
constexpr pa::Region B{pa::ArcaneDecay.x, pa::ArcaneDecay.y + pa::ArcaneDecay.h + pa::Gap, atlas::BW, atlas::BH};
constexpr bool ApartFromShared(pa::Region r) {
    return pa::Apart(r, pa::Settings) && pa::Apart(r, pa::Glyph) && pa::Apart(r, pa::Blood) && pa::Apart(r, pa::Strain) &&
           pa::Apart(r, pa::Notice) && pa::Apart(r, pa::BowLaser) && pa::Apart(r, pa::ArcaneDecay);
}
static_assert(pa::Inside(A, pa::SharedWidth, pa::SharedHeight) && pa::Inside(B, pa::SharedWidth, pa::SharedHeight), "both regions inside the shared image");
static_assert(ApartFromShared(A) && ApartFromShared(B) && pa::Apart(A, B), "both regions a Gap from every upstream panel and from each other");
inline constexpr pa::Region Of(atlas::Region r) { return r == atlas::A ? A : B; }
}  // namespace framevr::quickradial::shared
