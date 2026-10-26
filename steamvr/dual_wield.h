#pragma once
#include "../shared/d2r_vr_state.h"
#include <cstdint>

namespace framevr::dualwield {
struct Equipment {
    uint32_t id[2]{},type[2]{},displayed[2]{};
    int BodyHand(int hand) const {return hand>=0&&hand<2&&displayed[hand]?(displayed[hand]==id[0]?0:displayed[hand]==id[1]?1:-1):-1;}
    uint32_t Type(int hand) const {const int body=BodyHand(hand);return body>=0?type[body]:0;}
};
inline bool MeleeType(uint32_t type) {
    switch(type) {
        case D2RVR_TYPE_SWORD:case D2RVR_TYPE_AXE:case D2RVR_TYPE_MACE:
        case D2RVR_TYPE_SCEPTER:case D2RVR_TYPE_WAND:case D2RVR_TYPE_DAGGER:
        case D2RVR_TYPE_THROWING:case D2RVR_TYPE_JAVELIN:return true;
        default:return false;
    }
}
inline bool Eligible(int heroClass,const Equipment& e) {
    if(!e.id[0]||!e.id[1]||e.id[0]==e.id[1])return false;
    // Assassin dual wield is claw-only. Keep the Barbarian's supported
    // weapon types separate so neither class gains invalid equipment pairs.
    if(heroClass==6)return e.type[0]==D2RVR_TYPE_CLAW&&e.type[1]==D2RVR_TYPE_CLAW;
    return heroClass==4&&MeleeType(e.type[0])&&MeleeType(e.type[1]);
}
// Double Throw (#224): a throwing weapon in each of a Barbarian's hands, each
// thrown by its own hand.
inline bool ThrownType(uint32_t type) {return type==D2RVR_TYPE_THROWING||type==D2RVR_TYPE_JAVELIN;}
inline bool ThrownPair(int heroClass,const Equipment& e) {
    return heroClass==4&&Eligible(heroClass,e)&&ThrownType(e.type[0])&&ThrownType(e.type[1]);
}
// Rendered R/L attachments use the native primary/secondary composite slots,
// which can differ from inventory body-slot order after equipping items.
inline bool SamePair(uint32_t right,uint32_t left,const Equipment& e) {
    return right&&left&&right!=left&&((right==e.id[0]&&left==e.id[1])||(right==e.id[1]&&left==e.id[0]));
}
}
