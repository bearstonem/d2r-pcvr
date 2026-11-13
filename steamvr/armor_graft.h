#pragma once
#include "bone_graft.h"

// Sorceress ice armor graft: all three pieces while any native armor state
// remains active. No duration estimates, spell dispatch or game-stat writes.
// Source::Frozen and the existing INI keys remain compatible with earlier builds;
// they now cover Frozen Armor (10), Shiver Armor (88) and Chilling Armor (20).
namespace framevr::armorgraft {
enum class Source { None, Bone, Frozen };
struct Selection {Source source=Source::None;int layers=0;};
// Ice only: 4 cm radius, 24 mm toward elbow, +10.25% size; outer axes +/-75 degrees.
inline bonegraft::Settings IciclePlacement(bonegraft::Settings s){s.spread=75.f;return s;}
inline bonegraft::Piece IciclePiece(bonegraft::Piece p,float attachmentOut,int piece=1,bool left=false){
    const auto outward=math::Rotate(p.pose.q,{0,0,1});
    const float side=(left?-1.f:1.f)*(piece-1),radians=3.14159265f/180.f;
    const auto center=math::Multiply(p.pose.q,math::Yaw(-side*75.f*radians));
    // Attachment positions sit closer together (40 degrees), independently of the 75 degree facing.
    const auto radial=math::Rotate(math::Multiply(center,math::Yaw(side*40.f*radians)),{0,0,1});
    // Center stays +5 mm; outer pieces extend +20 mm along their own facing.
    p.pose.p=p.pose.p-outward*attachmentOut+radial*.04f+math::Rotate(p.pose.q,{0,.024f,0})+outward*(piece==1?.005f:.020f);
    p.length*=1.1025f;p.width*=1.1025f;return p;
}

inline Selection Select(const interaction::Snapshot& s,uint64_t session,uint64_t now,
                        bool models,bool boneOn,bool frozenOn) {
    if(!models||!interaction::Fresh(s,session,now))return {};
    // Preserve the original absorb-pool path and its priority for every class.
    const int bone=boneOn?bonegraft::Layers(s.boneArmor,s.boneArmorMax):0;
    if(bone)return {Source::Bone,bone};
    // A new UI snapshot must not renew an old game-thread state sample.
    const bool effectsFresh=s.effectsAt&&now>=s.effectsAt&&now-s.effectsAt<=1000;
    if(frozenOn&&s.skillClass==1&&s.actionKnown&&s.actionMode!=0&&s.actionMode!=17&&
       effectsFresh&&(s.effects.Has(10)||s.effects.Has(88)||s.effects.Has(20)))return {Source::Frozen,bonegraft::Pieces};
    return {};
}
}
