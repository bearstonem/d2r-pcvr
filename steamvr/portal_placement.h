#pragma once
#include <cmath>
#include <cstdint>
#include <initializer_list>
namespace framevr::portalplacement {
struct Point {int x=0,y=0;};
inline bool InFront(Point p,float x,float y,float fx,float fy) {
    const float dx=p.x-x,dy=p.y-y,d=std::hypot(dx,dy);
    return p.x>0&&p.x<65535&&p.y>0&&p.y<65535&&d>=1.5f&&d<=6.f&&(dx*fx+dy*fy)>=d*.70710678f;
}
// Search closest forward positions first. Native placement may shift each
// candidate, so validate its actual output, not just the requested point.
template<class Find,class Reachable>
bool Choose(float x,float y,float fx,float fy,Find find,Reachable reachable,Point& chosen,uintptr_t& room) {
    const float length=std::hypot(fx,fy);
    if(!std::isfinite(x+y+length)||length<.05f||x<1||y<1||x>65534||y>65534)return false;
    fx/=length;fy/=length;
    for(float distance:{3.f,2.f,4.f})for(float angle:{0.f,.2617994f,-.2617994f,.5235988f,-.5235988f}){
        const float c=std::cos(angle),s=std::sin(angle);
        Point seed{int(std::lround(x+distance*(fx*c-fy*s))),int(std::lround(y+distance*(fx*s+fy*c)))};
        if(!InFront(seed,x,y,fx,fy))continue;
        Point result=seed;const uintptr_t found=find(seed,result);
        if(found&&InFront(result,x,y,fx,fy)&&reachable(result)){chosen=result;room=found;return true;}
    }
    return false;
}
}
