#pragma once
#include "potion_pose.h"
#include "interactions.h"
#include "dropped_bottles.h"
#include "ice_shard_model.h"
namespace framevr::potions {
// scale: the model's size multiplier (0 = as cached, 1). occA/occB/occR: a
// capsule (room space) that hides the model's pixels behind it (occR 0: none).
// width: the size across the model's X and Z (0 = scale).
struct Instance {interaction::Pose pose;uint32_t code=0;bool empty=false,highlight=false;float scale=0;
    math::Vec occA{},occB{};float occR=0;float width=0;bool mirrorX=false;};
// Four belt bottles, one held, one thrown, the arrow, a trap, a throwing potion
// in flight, the emptied ones let go of; Bone Armor's forearm grafts (three per
// arm) and up to six knocked off and falling (bone_graft.h).
inline constexpr unsigned BoneItems=12;
struct Scene {std::array<Instance,9+bottles::MaxBottles+BoneItems> items{};unsigned count=0;};
// The cached Bone Armor pieces (prepare_potions.py): bn1..bn3, each 1 m long
// along +Y, +Z out of the arm; drawn at length metres.
inline uint32_t BoneCode(int piece){return interaction::Code('b','n',char('1'+(piece<0?0:piece>2?2:piece)));}
// width: its thickness (metres; 0 = as long as it is thick, the model's own
// proportions). arm: the forearm capsule the piece hides behind (radius 0: never).
inline bool AddBone(Scene& s,interaction::Pose pose,int piece,float length,float width=0,math::Vec armA={},math::Vec armB={},float armR=0) {
    if(s.count>=s.items.size()||!interaction::Valid(pose)||!(length>.005f&&length<1.f))return false;
    const bool arm=std::isfinite(armA.x+armA.y+armA.z+armB.x+armB.y+armB.z+armR)&&armR>0&&armR<.2f;
    if(!(std::isfinite(width)&&width>0&&width<1.f))width=0;
    s.items[s.count++]={pose,BoneCode(piece),false,false,length,armA,armB,arm?armR:0,width};return true;
}
inline bool AddIce(Scene& s,interaction::Pose pose,int piece,float length,float width=0,math::Vec armA={},math::Vec armB={},float armR=0,bool left=false) {
    if(!AddBone(s,pose,piece,length,width,armA,armB,armR))return false;
    s.items[s.count-1].code=ice::Code(piece);s.items[s.count-1].mirrorX=left;return true;
}
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
