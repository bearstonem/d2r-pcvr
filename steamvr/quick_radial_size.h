#pragma once
// The two pictures Quick cast paints into the shared dynamic image (steamvr/panel_atlas.h QuickRingA, QuickRingB): region A the ring, the pick's rim and labels 0-9, region B labels 10-19. Sizes only, so panel_atlas.h needs nothing else.
namespace framevr::quickradialsize {
constexpr int AW = 480, AH = 848, BW = 512, BH = 232;
}
