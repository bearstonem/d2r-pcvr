#pragma once
#include <atomic>
#include "spell_aim.h"
#include "aim_reach.h"
namespace framevr::gripaim {
inline std::atomic<unsigned> held{0};
inline std::atomic<uint64_t> updated{0};
inline bool Selected(int mode,int hand){return (mode==1&&hand==1)||(mode==2&&hand==0);}
inline unsigned Active(unsigned bits,uint64_t stamp,uint64_t now){return stamp&&now>=stamp&&now-stamp<250?bits:0;}
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
