#pragma once
#include "shield_guard.h"
#include <string_view>

namespace framevr::guard {
// Runtime Granny mesh names need not include their archive path. Recognize
// shield families, then require exact hero ownership before using their bounds.
inline bool ShieldMesh(std::string_view name) {
    constexpr std::string_view labels[]={"shield","buckler","targe","rondache",
        "bverrit_keep","taebaek","lance_guard","lidless_wall","mosers_blessed_circle",
        "pelta_lunata","steelclash","stormchaser","stormguild","swordback_hold",
        "the_ward","umbral_disk","wall_of_the_eyeless"};
    for(const auto label:labels)if(name.find(label)!=name.npos)return true;
    return false;
}
inline Vec Vector(const float* m,Vec p) {
    return {p.x*m[0]+p.y*m[4]+p.z*m[8],p.x*m[1]+p.y*m[5]+p.z*m[9],p.x*m[2]+p.y*m[6]+p.z*m[10]};
}
// A Granny mesh bone binding stores its OBB in that bone's coordinates.
// world is the current bone pose times the fully evaluated item transform.
// Its thin axis determines the face; neither wrist axes nor model names do.
inline bool FromBounds(const float* world,Vec low,Vec high,Vec player,const Pose& context,Pose& out) {
    if(!Valid(context)||!Finite(low)||!Finite(high)||!Finite(player))return false;
    for(int i=0;i<16;++i)if(!std::isfinite(world[i]))return false;
    if(std::fabs(world[3])+std::fabs(world[7])+std::fabs(world[11])>.001f||std::fabs(world[15]-1)>.001f)return false;
    const Vec size=high-low;
    if(size.x<=0||size.y<=0||size.z<=0||size.x>100||size.y>100||size.z>100)return false;
    const Vec axes[]={Vector(world,{size.x*.5f,0,0}),Vector(world,{0,size.y*.5f,0}),Vector(world,{0,0,size.z*.5f})};
    const float length[]={math::Dot(axes[0],axes[0]),math::Dot(axes[1],axes[1]),math::Dot(axes[2],axes[2])};
    int thin=0;for(int i=1;i<3;++i)if(length[i]<length[thin])thin=i;
    const int a=(thin+1)%3,b=(thin+2)%3;
    if(length[thin]>.5f*std::min(length[a],length[b]))return false;
    const float scale=1/context.unitsPerMetre;
    Pose p=context;
    p.player=player;
    p.center=(Vector(world,(low+high)*.5f)+Vec{world[12],world[13],world[14]}-player)*scale;
    p.halfRight=axes[a]*scale;p.halfUp=axes[b]*scale;
    const float r=math::Dot(p.halfRight,p.halfRight),u=math::Dot(p.halfUp,p.halfUp);
    if(r<.0025f||u<.0025f||r>2.25f||u>2.25f)return false; // hidden or invalid item
    p.normal=math::Normalized(math::Cross(p.halfRight,p.halfUp));
    const Vec delta=p.center-context.center;
    if(math::Dot(delta,delta)>1.f||!Valid(p))return false;
    out=p;return true;
}
}
