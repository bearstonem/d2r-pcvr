#pragma once
#include <cmath>
#include <cstdint>
namespace framevr::entryfacing {
// Consume a live level change on its first render pass. Never queue a turn
// for a later visible frame, even if entrance geometry cannot be resolved.
struct State {
    uint64_t session=0;
    int level=0;
    bool Observe(uint64_t s,int l,uint64_t) {
        if(!s){*this={};return false;}
        if(s!=session){session=s;level=l;return false;}
        if(!l)return false;
        const int previous=level;level=l;
        return previous&&l!=previous;
    }
};
// Prefer away from the return tile. When the hero overlaps the tile, the
// longest walkable ray identifies the room interior instead of dividing by zero.
template<class Walkable>
bool Direction(float px,float py,float tx,float ty,Walkable walkable,float& heading) {
    if(!std::isfinite(px+py+tx+ty))return false;
    const float dx=px-tx,dy=py-ty,d=std::hypot(dx,dy);
    if(d>20)return false;
    constexpr float pi=3.14159265f;
    const float away=d>=.5f?std::atan2(dy,dx):0.f;
    float best=-1,bestAngle=0;
    for(int i=0;i<17;++i){
        const float angle=i==0?away:(i-1)*pi/8;
        const float fx=std::cos(angle),fy=std::sin(angle);
        const float alignment=d>=.5f?(fx*dx+fy*dy)/d:0;
        if(d>=.5f&&alignment<.25f)continue;
        float reach=0;
        for(int step=1;step<=12;++step){
            const float distance=step*.5f;
            if(!walkable(int(std::lround(px+fx*distance)),int(std::lround(py+fy*distance))))break;
            reach=distance;
        }
        if(reach<1.5f)continue;
        // Facing is not route planning: a longer diagonal must not override
        // a clear direction away from the entrance. Farther furniture/walls
        // otherwise pull the view sideways depending on the spawn position.
        if(i==0&&d>=.5f){
            heading=std::atan2(-fx,-fy)*180/pi;
            return true;
        }
        const float score=reach+alignment*2.f;
        if(score>best){best=score;bestAngle=angle;}
    }
    if(best<0)return false;
    heading=std::atan2(-std::cos(bestAngle),-std::sin(bestAngle))*180/pi;
    return true;
}
inline float TurnDelta(float cameraHeading,float currentYaw,float destinationHeading) {
    return std::remainder(cameraHeading-currentYaw-destinationHeading,360.f);
}
}
