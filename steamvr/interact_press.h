#pragma once
#include <cstdint>

namespace framevr::interaction {
// One target per physical press (CalMac91, PR #70), including across native
// menus that bypass gameplay translation. Observe releases there as well.
class InteractPress {
    bool wasHeld=false,primed=false,blocked=false;
    uintptr_t latch=0;
    uint64_t session=0;
    uint32_t epoch=0;
public:
    struct Result {bool down=false,miss=false,lost=false;};
    Result Poll(bool held,uintptr_t target,bool hooked,bool active,uint64_t nextSession,uint32_t nextEpoch) {
        const bool changed=primed&&(session!=nextSession||epoch!=nextEpoch);
        primed=true;session=nextSession;epoch=nextEpoch;
        if(changed){latch=0;if(wasHeld&&held)blocked=true;}
        if(!active) {
            // A held button cannot carry an old interaction into another
            // context. A release observed in a menu rearms the next press.
            latch=0;wasHeld=held;blocked=held;return {};
        }
        Result out;
        if(!held){latch=0;blocked=false;}
        else if(!wasHeld){latch=target;out.miss=!latch&&hooked;}
        else if(latch&&target!=latch){latch=0;out.lost=true;}
        wasHeld=held;
        out.down=held&&!blocked&&(latch!=0||!hooked);
        return out;
    }
};
// Pad A's interact-on-attack, the game's allowInteractOnDefaultAttack (RVA 0x22BD840), which vrcam's
// ApplyActions sets at every pad poll: on, A next to a target (an item, a chest, a door, an NPC) picks it up,
// opens it or talks instead of casting the A slot's skill; off, A only casts that skill.
// 1 on, 0 off, -1 the game's own value.
//  - interact: the Interact action holds A (it presses A only onto a target): on.
//  - attackOnly ([input] a_attack_only) or attack (the Attack action): off.
//  - gestureGrips: both grips held for the class motions, or let go under 0.4 s ago (transport's
//    SpellGripsHeld): off. A motion casts through its skill's hand slot, so a skill on A (the Sorceress's
//    Nova in the owner's 2026-11-11 session: "Nova cast by its motion with both grips held, via its hand
//    slot", then pad A) was taken by the game as A on the loot beside the hero, and picked it up (owner,
//    2026-11-11: "Holding both grip buttons next to loot should not trigger a loot pickup. It's for
//    gestures execution only"). The Interact action itself still interacts: no motion casts while it is held.
inline int InteractOnA(bool interact,bool attackOnly,bool attack,bool gestureGrips) {
    if(interact)return 1;
    return attackOnly||attack||gestureGrips?0:-1;
}
}
