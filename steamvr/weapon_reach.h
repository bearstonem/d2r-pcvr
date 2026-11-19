#pragma once
#include "../shared/d2r_vr_state.h"
#include <cstdint>
#include <string_view>

// A held weapon's melee contact: a capsule from the grip along its attach bone's +Y, sized by the weapon's kind
// (vr/vrcam.cpp MeleeContactHand). It is what strikes monsters, and what knocks bottles and props.
//
// A flail strikes with its head, not its shaft (a player with two flails, 2026-11-19: "flail hit boxes are based on
// the shaft, and not the actual ball/chain"). The game files flails under maces, and a mace's 0.65 m capsule ended
// partway up the chain, short of the ball. The reach is each hand's own, from the item that hand holds: two flails,
// a flail and a sword, or a flail and a shield each reach as far as what is in that hand.
namespace framevr::reach {
struct Capsule {float length=.65f,radius=.065f;};   // metres from the grip, and the half width

// The game's flails, by item code: the five that data/hd/items/items.json gives a flail's model - mace/flail (Flail,
// Knout, Scourge), mace/khalim_flail (Khalim's Flail) and mace/super_khalim_flail (Khalim's Will). No class-only item
// is a flail. Codes are the game's four characters, padded with a space.
inline bool FlailCode(std::string_view code) {
    return code=="fla "||code=="9fl "||code=="7fl "||code=="qf1 "||code=="qf2 ";
}

// m: a flail's capsule, grip to head. Measured from the game's model (data/hd/items/weapon/mace/flail, in the model's
// own units, the heroes' feet): the handle runs to 1.47, the chain from 1.42 to 2.94 and the ball on to 3.56, its
// centre at 3.24; Khalim's flails reach as far. Each class's attachment scales it by 0.9-1.13 (a Barbarian's 1.1),
// and the heroes' eyes, matched to the player's own, stand 6.3-7.1 of those units up (a Barbarian's 7.05): his ball's
// centre comes 0.83 m from the grip for eyes at 1.65 m (0.76-0.91 m for 1.5-1.8 m), the other classes' 0.76-0.88 m.
// 0.9 m reaches the ball's centre, and with the capsule's 6.5 cm half width mostly its far side. Its chain is not
// rigid, so the capsule stays straight along the handle: the head flies out that way in a swing, which is when it hits.
constexpr float FlailLength=.9f;

// The capsule for a weapon kind (D2RVR_TYPE_*), held in both hands or not, a flail or not.
inline Capsule Contact(uint32_t kind,bool twoHandsOn,bool flail) {
    Capsule c;
    if(flail){c.length=FlailLength;return c;}
    switch(kind) {
        case D2RVR_TYPE_UNARMED:c.length=.04f;c.radius=.08f;break;
        case D2RVR_TYPE_DAGGER:case D2RVR_TYPE_CLAW:case D2RVR_TYPE_ORB:c.length=.28f;break;
        case D2RVR_TYPE_WAND:c.length=.38f;break;
        case D2RVR_TYPE_STAFF:c.length=1.2f;break;
        case D2RVR_TYPE_SPEAR:case D2RVR_TYPE_POLEARM:c.length=1.5f;break;
        default:if(twoHandsOn)c.length=1.1f;break;
    }
    return c;
}
}
