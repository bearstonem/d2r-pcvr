#pragma once
#include "../shared/d2r_vr_state.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

// Weapon heft (#443; Discord #ideas, 2026-11-04: "weapon weight - like in blade and sorcery"). Monsters
// have no physical bodies to push against, and every landed stroke must stay one native attack
// (experience, procs, loot), so weight is a rule, not a physics weapon: a heavier weapon needs a longer
// stroke of the hand before it meets the monster to land a full hit, and a shorter one lands lighter
// (Unleashed's damage share, the same scaling swing recovery uses, vr/melee.cpp). Daggers, claws, wands,
// orbs, thrown weapons and fists hit whole with a flick. The drawn weapon never lags the hand: hits and
// aim stay where you see the weapon. [gameplay] weapon_heft (VR Settings > Combat > Melee), off by default.
namespace framevr::heft {
// The hand's travel the heaviest weapon's stroke needs (m), and the share the shortest stroke keeps.
inline constexpr float FullStrokeM=.40f,Floor=.30f;
// How heavy a weapon swings, 0 (a flick lands whole) .. 1 (a great maul): its kind, whether it is
// two-handed (Weapons.txt "2handed", in one hand or both), and its speed (Weapons.txt WSM: + slower,
// - faster; 1% per point, at most 20 either way). Anything that is not a melee weapon weighs nothing.
inline float Weight(uint32_t type,bool twoHanded,int wsm) {
    float base=0;
    switch(type) {
        case D2RVR_TYPE_SWORD:case D2RVR_TYPE_AXE:case D2RVR_TYPE_MACE:case D2RVR_TYPE_SCEPTER:case D2RVR_TYPE_OTHER:
            base=twoHanded?.80f:.40f;break;
        case D2RVR_TYPE_SPEAR:case D2RVR_TYPE_POLEARM:case D2RVR_TYPE_STAFF:base=.70f;break;
        default:return 0;   // fists, daggers, claws, wands, orbs, thrown weapons; bows and crossbows
    }
    return std::clamp(base+float(std::clamp(wsm,-20,20))*.01f,0.f,1.f);
}
// The hand travel (m) a stroke with a weapon of this weight needs for a full hit; 0: any stroke is whole.
inline float Need(float weight){return std::isfinite(weight)?std::clamp(weight,0.f,1.f)*FullStrokeM:0.f;}
// A contact's damage share: the hand's brisk travel in this stroke so far (melee_contact.h Sweep::travel)
// over what the weapon needs, never below Floor. No need, or an unreadable travel, is whole.
inline float Factor(float travel,float need) {
    if(!std::isfinite(need)||need<=0||!std::isfinite(travel)||travel<0)return 1;
    return std::clamp(travel/need,Floor,1.f);
}
}
