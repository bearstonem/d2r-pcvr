#pragma once
#include "bone_graft.h"

// Sorceress ice armor graft: all three pieces while any native armor state
// remains active. No duration estimates, spell dispatch or game-stat writes.
// Source::Frozen and the existing INI keys remain compatible with earlier builds;
// they now cover Frozen Armor (10), Shiver Armor (88) and Chilling Armor (20).
namespace framevr::armorgraft {
enum class Source { None, Bone, Frozen };
struct Selection {Source source=Source::None;int layers=0;};
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
