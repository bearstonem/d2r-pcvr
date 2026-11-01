#pragma once
#include "../steamvr/loot_beams.h"
#include <array>
#include <cstddef>
#include <cstdint>
namespace D2RL {struct PluginContext;}

// Loot beams (steamvr/loot_beams.h): which items on the floor get a beam. Four
// times a second the game thread walks the local game's item units and keeps the
// beam-worthy ones near the hero; vrcam's effect setup reads the latest list and
// D2R_DepthFog.fx draws the beams. Local games only (offline and hosted): a
// joined game has no server units here, so it shows none.
namespace lootscan {
struct Shown {
    std::array<framevr::lootbeams::Lying,framevr::lootbeams::MaxBeams> items{};
    size_t count=0;
    uint64_t at=0;   // GetTickCount64 of the scan; 0 = none yet
    float strength=1.0f;   // the brightness slider (lootbeams::Strength): the shader's BeamShape.w
};
// [loot] beams (on: the master switch, not in the menu), rune_min (26 = Vex). Which
// rarities (VR Settings > Interface > Loot beams: [loot] beams_unique, beams_set,
// beams_rare, beams_runes) is read live on each scan.
void Init(const D2RL::PluginContext* ctx,bool on,const framevr::lootbeams::Wanted& wanted);
// vrcam's update thread: posts the scan to the game thread at most every 250 ms.
void Tick(uint64_t now);
// The latest list, if no older than maxAgeMs (else empty).
Shown Latest(uint64_t now,uint64_t maxAgeMs=1000);
}
