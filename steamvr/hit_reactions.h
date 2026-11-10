#pragma once
#include "guard_strain.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

// Hit reactions (owner, 2026-11-09: "Monsters that react to where you hit them ... a brief slow-down on
// heavy hits, knockback scaled to weapon heft"). The game makes a struck monster flinch only when one hit
// takes a large share of its life, so most solid strokes of a real arm drew no reaction at all. Here a
// committed stroke that lands staggers the monster (the game's own hit recovery), and a heavy weapon's
// full stroke knocks it back (the game's own knockback, away from the hero). Both are the result flags of
// that stroke's own native attack (vr/combat.cpp ApplyDamage): no extra damage, attack or roll, and a
// stroke the Unleashed rules keep quiet (a would-have-missed contact, a flail between cycles, a cleave
// follow-up) stays quiet. Each monster waits before it reacts again, so a fast weapon cannot hold one
// in place. [gameplay] hit_stagger and hit_knockback (VR Settings > Gameplay > Physics > Struck monsters).
namespace framevr::hitreact {
using strain::Rank;
// The hand's brisk travel (melee_contact.h Sweep::travel) that makes a stroke fully committed.
inline constexpr float FullStrokeM=.35f;
// Force a stroke needs to stagger, and to knock back.
inline constexpr float StaggerForce=.5f,KnockbackForce=.8f;
// Before the same monster staggers, or is knocked back, again.
inline constexpr uint64_t StaggerMs=1200,KnockbackMs=2500;
enum Wanted:uint8_t {None=0,Stagger=1,Knockback=2};   // bits: what the player has switched on
// A landed stroke's force, 0..1: how committed the hand's stroke was, carried by the weapon's weight
// (weapon_heft.h Weight: 0 fists, daggers and claws .. 1 a great maul), and scaled by the damage share
// the stroke kept (swing recovery, cleave). A committed dagger stroke just staggers (.5); a one-handed
// sword reaches .7; knockback needs a weight of .6 or more: two-handed weapons, spears, polearms, staves.
inline float Force(float travel,float weight,float share) {
    if(!std::isfinite(travel)||!std::isfinite(weight)||!std::isfinite(share))return 0;
    const float commit=std::clamp(travel/FullStrokeM,0.f,1.f);
    return commit*(.5f+.5f*std::clamp(weight,0.f,1.f))*std::clamp(share,0.f,1.f);
}
// What a monster of this rank may do: act bosses never react; a unique or a champion staggers but
// stands its ground.
inline uint8_t Allowed(Rank rank) {
    switch(rank) {
        case Rank::PrimeEvil:return None;
        case Rank::Unique:case Rank::Champion:return Stagger;
        default:return Stagger|Knockback;
    }
}
// Each monster's latest stagger and knockback this game (32 kept, the oldest replaced).
class Board {
    uint64_t session=0;
    struct Entry {uint32_t id=0;uint64_t stagger=0,knockback=0;};
    std::array<Entry,32> recent{};unsigned next=0;
    const Entry* Of(uint64_t s,uint32_t monster) const {
        if(s!=session)return nullptr;
        for(const auto& e:recent)if(e.id==monster&&(e.stagger||e.knockback))return &e;
        return nullptr;
    }
    static bool Waiting(uint64_t at,uint64_t now,uint64_t wait){return at&&now>=at&&now-at<wait;}
public:
    // The reactions (Wanted bits) this stroke asks of this monster: knockback when the force and the
    // monster allow it, else the stagger. A knockback is a stagger too for the stagger's wait.
    uint8_t Judge(uint64_t s,uint32_t monster,uint64_t now,float force,uint8_t wanted,Rank rank) const {
        const uint8_t may=wanted&Allowed(rank);
        const auto* e=Of(s,monster);
        if((may&Knockback)&&force>=KnockbackForce&&!(e&&Waiting(e->knockback,now,KnockbackMs)))return Knockback;
        if((may&Stagger)&&force>=StaggerForce&&!(e&&Waiting(e->stagger,now,StaggerMs)))return Stagger;
        return None;
    }
    // The game took the reaction. A new game (session) forgets the old one's.
    void Landed(uint64_t s,uint32_t monster,uint64_t now,uint8_t reaction) {
        if(!reaction)return;
        if(s!=session){*this=Board{};session=s;}
        Entry* e=nullptr;
        for(auto& r:recent)if(r.id==monster&&(r.stagger||r.knockback)){e=&r;break;}
        if(!e){e=&recent[next++%recent.size()];*e={monster,0,0};}
        e->stagger=now;
        if(reaction&Knockback)e->knockback=now;
    }
};
// The damage record's result flags (+4) the game's executor reads after the hit is applied
// (vr/combat.cpp ApplyDamage): will die 0x2, hit recovery 0x4, knockback 0x8, soft hit 0x4000.
inline constexpr uint16_t FlagDies=0x2,FlagGetHit=0x4,FlagKnockback=0x8,FlagSoftHit=0x4000;
// Those flags with the reaction asked for: nothing for a dying monster or one the game already
// knocks back; a stagger replaces the soft hit; a knockback is added to whatever the game chose.
inline uint16_t Flags(uint16_t flags,uint8_t reaction) {
    if((flags&FlagDies)||!reaction)return flags;
    if(reaction&Knockback)return uint16_t((flags|FlagKnockback)&~FlagSoftHit);
    if(flags&(FlagGetHit|FlagKnockback))return flags;
    return uint16_t((flags|FlagGetHit)&~FlagSoftHit);
}
}
