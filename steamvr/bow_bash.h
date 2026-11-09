#pragma once
#include "melee_contact.h"
#include <array>
#include <cstdint>

// Bow bash (#443; Discord #ideas, 2026-11-04: "add some melee damage even very low to bow itself, so i can
// swing it and hit monster giving me time to step back and aim"). The game's basic attack with a bow fires
// an arrow, so a bow swing has no native melee attack to borrow. It staggers instead, through the native
// hit recovery a perfect parry uses (vr/melee.cpp), and deals no damage, spends no arrow and gives no
// experience or loot. The bow in the left hand, its string not drawn and no trigger held; any class.
// [gameplay] bow_bash (VR Settings > Combat > Melee, beside Weapon heft), off by default.
namespace framevr::bowbash {
using math::Vec;
inline constexpr uint64_t CooldownMs=1500;      // between two bashes, whichever monsters
inline constexpr uint64_t SameMonsterMs=3000;   // before the same monster staggers again
// The bow's capsule: limb tip to limb tip about 1.1 m, centred on the grip.
inline constexpr float HalfLengthM=.55f,RadiusM=.06f;
// The bow as a melee capsule (world metres): its grip is the left weapon attachment's origin and its
// limbs run both ways along that attachment's +Y, as a held weapon's blade does.
inline melee::Blade Blade(Vec grip,Vec limbAxis) {
    const Vec a=math::Normalized(limbAxis);
    return {grip-a*HalfLengthM,grip+a*HalfLengthM,RadiusM};
}
// A contact's outcome: the stagger, or why not.
enum class Verdict:uint8_t {Stagger,ActBoss,TooSoon,SameMonster,Recovering,Refused};
inline const char* Reason(Verdict v) {
    switch(v) {
        case Verdict::Stagger:return "staggered";
        case Verdict::ActBoss:return "act boss";
        case Verdict::TooSoon:return "too soon after the last bash";
        case Verdict::SameMonster:return "this monster was bashed lately";
        case Verdict::Recovering:return "already staggered (in hit recovery)";
        default:return "the game refused the stagger";
    }
}
// The bashes that landed this game: the latest of any monster, and each monster's latest (16 kept).
// Only a landed stagger starts a wait; a refusal never does.
class Board {
    uint64_t session=0,last=0;
    struct Entry {uint32_t id=0;uint64_t at=0;};
    std::array<Entry,16> recent{};unsigned next=0;
    const Entry* Of(uint64_t s,uint32_t monster) const {
        if(s!=session)return nullptr;
        for(const auto& e:recent)if(e.at&&e.id==monster)return &e;
        return nullptr;
    }
public:
    // In this order: an act boss never staggers; then the 1.5 s between bashes; then 3 s per monster;
    // then a monster already in its hit recovery. Stagger: the caller asks the game for it.
    Verdict Judge(uint64_t s,uint32_t monster,uint64_t now,bool actBoss,bool recovering) const {
        if(actBoss)return Verdict::ActBoss;
        if(Wait(s,now))return Verdict::TooSoon;
        if(WaitFor(s,monster,now))return Verdict::SameMonster;
        if(recovering)return Verdict::Recovering;
        return Verdict::Stagger;
    }
    // ms until the next bash may land (0: now), and until this monster may stagger again.
    uint64_t Wait(uint64_t s,uint64_t now) const {
        return s==session&&last&&now>=last&&now-last<CooldownMs?CooldownMs-(now-last):0;
    }
    uint64_t WaitFor(uint64_t s,uint32_t monster,uint64_t now) const {
        const auto* e=Of(s,monster);
        return e&&now>=e->at&&now-e->at<SameMonsterMs?SameMonsterMs-(now-e->at):0;
    }
    // The game staggered this monster now. A new game (session) forgets the old one's bashes.
    void Landed(uint64_t s,uint32_t monster,uint64_t now) {
        if(s!=session){*this=Board{};session=s;}
        last=now;
        for(auto& e:recent)if(e.id==monster&&e.at){e.at=now;return;}
        recent[next++%recent.size()]={monster,now};
    }
};
}
