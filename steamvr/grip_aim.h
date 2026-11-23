#pragma once
#include <atomic>
#include "spell_aim.h"
#include "aim_reach.h"
namespace framevr::gripaim {
inline std::atomic<unsigned> held{0};
inline std::atomic<uint64_t> updated{0};
inline bool Selected(int mode,int hand){return (mode==1&&hand==1)||(mode==2&&hand==0);}
inline unsigned Active(unsigned bits,uint64_t stamp,uint64_t now){return stamp&&now>=stamp&&now-stamp<250?bits:0;}
enum class Landing {Unknown,Clear,Blocked};
struct LandingSample {
    uint64_t session=0,at=0;int x=0,y=0;Landing state=Landing::Unknown;
    Landing For(uint64_t owner,int cx,int cy,uint64_t now) const {
        return owner&&owner==session&&cx==x&&cy==y&&now>=at&&now-at<250?state:Landing::Unknown;
    }
};
inline Landing Terrain(bool room,uint16_t mask){return !room||(mask&1)?Landing::Blocked:Landing::Clear;}
inline int Element(Landing state){return state==Landing::Blocked?4:state==Landing::Clear?5:2;}
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
