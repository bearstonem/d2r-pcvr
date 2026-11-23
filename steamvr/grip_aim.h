#pragma once
#include <atomic>
#include "spell_aim.h"
#include "aim_reach.h"
namespace framevr::gripaim {
inline std::atomic<unsigned> held{0};
inline std::atomic<uint64_t> updated{0};
inline bool Selected(int mode,int hand){return (mode==1&&hand==1)||(mode==2&&hand==0);}
inline unsigned Active(unsigned bits,uint64_t stamp,uint64_t now){return stamp&&now>=stamp&&now-stamp<250?bits:0;}
struct LandingResult {
    bool known=false,found=false;float x=0,y=0;
};
// Search backward from the requested point. Only landing cells are tested:
// Teleport may cross a wall, but must never finish inside it.
template<class Safe> LandingResult Backtrack(float px,float py,float tx,float ty,Safe safe){
    LandingResult result{true,false,px,py};
    const float dx=tx-px,dy=ty-py,length=std::hypot(dx,dy);
    if(!std::isfinite(length)||length<2||length>100)return result;
    const int steps=int(std::ceil(std::max(std::fabs(dx),std::fabs(dy))*2));
    int lastX=-1,lastY=-1;
    for(int i=steps;i>0;--i){
        const float t=float(i)/steps;
        const int x=int(px+dx*t),y=int(py+dy*t);
        if(x==lastX&&y==lastY)continue;
        lastX=x;lastY=y;
        if(x<1||y<1||x>65534||y>65534||std::hypot(float(x)-px,float(y)-py)<2)continue;
        if(safe(x,y))return {true,true,float(x),float(y)};
    }
    return result;
}
// Native commands truncate to integer subtiles. Preview exactly that same cell.
inline bool Cell(float& x,float& y){
    if(!std::isfinite(x+y)||x<1||y<1||x>65534||y>65534)return false;
    x=float(int(x));y=float(int(y));return true;
}
inline bool CastAt(int skill,int mode,bool grip,float x,float y,spellaim::Cast& cast){
    if(skill!=54||(mode!=1&&mode!=2)||!grip||!std::isfinite(x+y))return false;
    cast={true,x,y,true};return true;
}
inline bool Delta(math::Vec o,math::Vec d,float floor,float px,float py,float& dx,float& dy){
    float x=0,y=0;
    if(spellaim::GroundPoint(o,d,floor,.5f,px,py,x,y,aimreach::CornerCells)){dx=x-px;dy=y-py;}
    else {if(!spellaim::Horizontal(d,dx,dy))return false;dx*=aimreach::CornerCells*2;dy*=aimreach::CornerCells*2;}
    const float length=std::hypot(dx,dy);
    if(length>.001f&&length<spellaim::GroundMinDistance){dx*=spellaim::GroundMinDistance/length;dy*=spellaim::GroundMinDistance/length;}
    return aimreach::Clamp(dx,dy);
}
}
