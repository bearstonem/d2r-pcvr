#pragma once
#include "shield_guard.h"

namespace framevr::guard {
struct Debug {
    Pose pose{};bool raised=false;
    Vec hitFrom{},hitTo{}; // render-world points at the last check's shield height
    uint64_t hitAt=0;
    int hit=0; // 1 native block, 2 rejected guard, 3 native skipped block roll
};
struct DebugDraw {
    Vec center{},right{},up{},normal{},color{},hitFrom{},hitTo{};
    bool hit=false;
};
inline bool DrawDebug(const Debug& d,Vec camera,uint64_t now,DebugDraw& out) {
    out={};const auto& p=d.pose;
    if(!Valid(p)||!Finite(p.player)||!Finite(camera)||now<p.at||now-p.at>150)return false;
    const bool measured=Measured(p);
    Vec r=p.halfRight,u=p.halfUp;
    if(!measured) {
        // Amber reference plane: a proxy, not an invented measured mesh.
        r=math::Normalized(math::Cross(std::fabs(p.normal.y)<.9f?Vec{0,1,0}:Vec{0,0,1},p.normal))*.45f;
        u=math::Normalized(math::Cross(p.normal,r))*.30f;
    }
    out.center=p.player+p.center*p.unitsPerMetre-camera;
    out.right=r*p.unitsPerMetre;out.up=u*p.unitsPerMetre;
    out.normal=p.normal*(.35f*p.unitsPerMetre);
    out.color=d.raised?(measured?Vec{.05f,1.f,1.f}:Vec{1.f,.65f,.05f}):Vec{.5f,.5f,.5f};
    out.hit=d.hitAt&&now>=d.hitAt&&now-d.hitAt<=600&&d.hit>=1&&d.hit<=3&&Finite(d.hitFrom)&&Finite(d.hitTo);
    if(out.hit) {
        out.color=d.hit==1?Vec{.1f,1.f,.15f}:d.hit==2?Vec{1.f,.1f,.1f}:Vec{1.f,.15f,1.f};
        out.hitFrom=d.hitFrom-camera;out.hitTo=d.hitTo-camera;
    }
    return true;
}
}
