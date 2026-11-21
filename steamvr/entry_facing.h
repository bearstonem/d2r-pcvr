#pragma once
#include <cmath>
#include <cstdint>

namespace framevr::entryfacing {
// Render-thread state. Never reorient on a fresh game or on stale level zero.
// Only a nearby, unambiguous destination warp can supply an entry direction.
struct State {
    uint64_t session=0,until=0,stableAt=0;
    int level=0;
    uintptr_t tile=0;
    float x=0,y=0;
    bool Observe(uint64_t s,int l,uint64_t now) {
        if(!s){*this={};return false;}
        if(s!=session){*this={};session=s;level=l;return false;}
        if(!l)return false;
        if(!level){level=l;return false;}
        if(l!=level){level=l;until=now+1500;tile=0;stableAt=0;}
        return until&&now<=until;
    }
    bool Choose(uint64_t now,uintptr_t candidate,float px,float py,float tx,float ty,float& heading) {
        if(!until||now>until||!candidate||!std::isfinite(px+py+tx+ty)){tile=0;return false;}
        const float dx=px-tx,dy=py-ty,d=std::hypot(dx,dy);
        if(d<.5f||d>10.f){tile=0;return false;}
        if(candidate!=tile){tile=candidate;stableAt=now;x=px;y=py;return false;}
        // Once walking, do not surprise the player with a late turn.
        if(std::hypot(px-x,py-y)>.5f){until=0;return false;}
        if(now-stableAt<80)return false;
        heading=std::atan2(-dx,-dy)*57.2957795f;
        until=0;return true;
    }
};
inline float TurnDelta(float cameraHeading,float currentYaw,float destinationHeading) {
    return std::remainder(cameraHeading-currentYaw-destinationHeading,360.f);
}
}
