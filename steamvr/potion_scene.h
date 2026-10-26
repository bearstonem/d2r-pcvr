#pragma once
#include "potion_pose.h"
#include "interactions.h"
#include "dropped_bottles.h"
namespace framevr::potions {
struct Instance {interaction::Pose pose;uint32_t code=0;bool empty=false,highlight=false;};
// Four belt bottles, one held, one thrown, the arrow, a trap, a throwing potion
// in flight, the emptied ones let go of.
struct Scene {std::array<Instance,9+bottles::MaxBottles> items{};unsigned count=0;};
// The game's arrow (the cache's arw.pvr), along its pose's +Y, placed by its middle.
inline constexpr uint32_t ArrowCode=interaction::Code('a','r','w');
inline bool AddArrow(Scene& s,interaction::Pose middle) {
    if(s.count>=s.items.size()||!interaction::Valid(middle))return false;
    s.items[s.count++]={middle,ArrowCode,false,false};return true;
}
// An emptied bottle in the world, at its room-space pose.
inline bool AddDropped(Scene& s,interaction::Pose pose,uint32_t code) {
    if(!code||s.count>=s.items.size()||!interaction::Valid(pose))return false;
    s.items[s.count++]={pose,code,true,false};return true;
}
// A gas or oil potion in flight (#292): a full bottle at its room-space pose.
inline bool AddThrown(Scene& s,interaction::Pose pose,uint32_t code) {
    if(!code||s.count>=s.items.size()||!interaction::Valid(pose))return false;
    s.items[s.count++]={pose,code,false,false};return true;
}
// A trap or bomb in the throwing hand (#224, toss::Model), held in the grip like a bottle.
inline bool AddHeld(Scene& s,interaction::Pose hand,uint32_t code) {
    if(!code||s.count>=s.items.size()||!interaction::Valid(hand))return false;
    hand.p=Center(hand.p,hand.q);hand.q=HeldRotation(hand.q);
    s.items[s.count++]={hand,code,false,false};return true;
}
inline Scene MakeScene(const interaction::Input& in,const interaction::Output& out) {
    Scene s;
    if(!in.active||!in.tracked[0]||!in.tracked[1]||!out.beltReady||!interaction::Fresh(in.snapshot,in.session,in.at))return s;
    for(int i=0;i<4;++i) {
        const auto& b=in.snapshot.belt[i];
        if(i==out.bottle||(out.flying&&i==out.flightColumn)||!b.known||!b.count||!b.Next().handle||!interaction::Valid(out.belt[i]))continue;
        s.items[s.count++]={out.belt[i],b.Next().code,false,i==out.nearBelt};
    }
    if(out.bottleHand>=0&&out.bottleHand<2&&out.bottleItem.handle) {
        auto p=in.hand[out.bottleHand];p.p=Center(p.p,p.q);p.q=HeldRotation(p.q);
        s.items[s.count++]={p,out.bottleItem.code,out.bottleEmpty,false};
    }
    if(out.flying&&out.flightItem.handle&&s.count<s.items.size())s.items[s.count++]={out.flight,out.flightItem.code,false,false};
    return s;
}
}
