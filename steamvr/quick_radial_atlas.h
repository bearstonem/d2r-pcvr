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
constexpr pa::Region A = pa::QuickRingA, B = pa::QuickRingB;   // steamvr/panel_atlas.h: the registry checks them against every other region
static_assert(A.w == atlas::AW && A.h == atlas::AH && B.w == atlas::BW && B.h == atlas::BH, "the painter's pictures equal their regions");
static_assert(pa::Inside(A, pa::SharedWidth, pa::SharedHeight) && pa::Inside(B, pa::SharedWidth, pa::SharedHeight), "both regions inside the shared image");
inline constexpr pa::Region Of(atlas::Region r) { return r == atlas::A ? A : B; }
}  // namespace framevr::quickradial::shared
