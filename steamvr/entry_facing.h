#pragma once
#include <cmath>
#include <cstdint>
namespace framevr::entryfacing {
inline bool Town(int level) {
    return level==1||level==40||level==75||level==103||level==109;
}
// MonStats IDs from the stock game. Keep the two traders separate so their
// midpoint is independent of room/unit iteration order.
struct TownTargets {
    struct Point {float x=0,y=0;bool valid=false;};
    Point first,second;
    void Add(int level,uint32_t id,float x,float y) {
        if(!std::isfinite(x+y)||x<=0||y<=0)return;
        if((level==1&&(id==146||id==265))||(level==40&&id==178)||
           (level==75&&id==245)||(level==103&&id==257)||(level==109&&id==513))
            first={x,y,true};
        if(level==103&&id==405)second={x,y,true};
    }
    bool Heading(int level,float px,float py,float& heading) const {
        if(!Town(level)||(!first.valid&&!second.valid))return false;
        Point target=first.valid?first:second;
        if(first.valid&&second.valid)target={(first.x+second.x)*.5f,(first.y+second.y)*.5f,true};
        float dx=target.x-px,dy=target.y-py;
        if(!std::isfinite(dx+dy)||std::hypot(dx,dy)<.5f)return false;
        // Harrogath: turn perpendicular to Malah, placing her to the right
        // while looking into town instead of up toward her elevated shop.
        if(level==109){const float x=dx;dx=dy;dy=-x;}
        heading=std::atan2(-dx,-dy)*57.2957795f;
        return true;
    }
};
inline bool TownPortalArrival(int from,int to,float portalDistance) {
    // Blue portals have a separate landing area from the town's warp tiles.
    // Proximity is checked only on the first frame of a non-town -> town change.
    return from>0&&!Town(from)&&Town(to)&&std::isfinite(portalDistance)&&portalDistance>=0&&portalDistance<=4.f;
}
// The relevant NPC may be beyond the player's immediately adjacent room.
// Traverse loaded, same-level rooms once at arrival, with hard bounds and
// deduplication; do not load rooms or retain native pointers across frames.
template<class Read,class Level,class Visit>
void WalkTown(uintptr_t room,int level,Read read,Level levelOf,Visit visit) {
    uintptr_t rooms[128]{room};int count=1,visited=0;
    for(int r=0;r<count&&visited<2048;++r){
        if(rooms[r]<0x10000||levelOf(rooms[r])!=level)continue;
        uintptr_t nearby=0,unit=0;uint32_t n=0;
        if(read(rooms[r],nearby)&&read(rooms[r]+0x40,n)&&n<=64&&nearby>=0x10000)
            for(uint32_t i=0;i<n&&count<128;++i){
                uintptr_t next=0;if(!read(nearby+i*8,next))break;
                bool seen=false;for(int j=0;j<count;++j)if(rooms[j]==next){seen=true;break;}
                if(next>=0x10000&&!seen)rooms[count++]=next;
            }
        if(!read(rooms[r]+0xa8,unit))continue;
        for(int chain=0;unit>=0x10000&&chain<256&&visited<2048;++chain){
            ++visited;uintptr_t next=0;uint32_t header[4]{};
            if(!read(unit,header)||!read(unit+0x160,next))break;
            visit(unit,header);
            if(next==unit)break;
            unit=next;
        }
    }
}
// The stock LvlWarp records for Jail Up (13) and Jail Down (14) both
// specify ExitWalkX=0, ExitWalkY=5. Their doorway faces +grid Y, regardless
// of the fractional landing position beside the warp tile.
inline bool NativeExitHeading(uint32_t warp,float& heading) {
    if(warp!=13&&warp!=14)return false;
    heading=180.f;
    return true;
}
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
// Native OpenXR composes Yaw(camera - body - recenter) * head orientation.
// TargetYaw uses the legacy opposite head sign, so using it here leaves an
// error of twice the physical head yaw. All inputs/outputs are degrees.
inline float NativeTurnDelta(float cameraHeading,float bodyYaw,float headHeading,float recenter,float destinationHeading) {
    return std::remainder(cameraHeading-bodyYaw-recenter+headHeading-destinationHeading,360.f);
}
}
