#pragma once
#include "ally_health.h"
#include "world_coordinates.h"
namespace framevr::allyhealth {
inline constexpr float WristWidth=.16f;
inline float WristHeight(unsigned rows){return WristWidth*std::min(rows,Capacity)*RowHeight/Width;}
inline math::Vec WristOffset(float mapSize,float companionSpace,unsigned rows){return {0,-(mapSize+WristHeight(rows))*.5f-companionSpace-.006f,.002f};}
// Constant apparent width: retained text no longer shrinks with distance.
inline float OverheadWidth(math::Vec at,math::Vec head){
 const auto d=at-head;const float distance=std::sqrt(math::Dot(d,d));
 return std::isfinite(distance)&&distance>0?.16f*distance:0.f;
}

// Dynamic paths provide grid position; use the local ground elevation like
// the existing nearby-object guides. This is a HUD marker, not depth-tested geometry.
inline bool Overhead(const Entry& e,math::Vec origin,math::Vec hero,math::Quat base,float upm,float eyeHeight,math::Vec head,math::Vec& out){
 if(!e.Valid()||!e.positionKnown||!coordinates::Finite(e.world)||!coordinates::Finite(origin)||!coordinates::Finite(hero)||!coordinates::Finite(head)||!std::isfinite(upm)||upm<1||upm>12||!std::isfinite(eyeHeight)||eyeHeight<=0)return false;
 const float n=base.x*base.x+base.y*base.y+base.z*base.z+base.w*base.w;if(!std::isfinite(n)||std::abs(n-1)>.01f)return false;
 const auto world=e.gridPosition?coordinates::RenderGround({2*e.world.x/upm,0,2*e.world.z/upm},hero.y/upm):e.world*(1/upm);
 const math::Vec eye={hero.x/upm,(hero.y+eyeHeight)/upm,hero.z/upm};
 out=origin+math::Rotate(math::Conjugate(base),world-eye+math::Vec{0,2.f,0});
 const auto d=out-head;return coordinates::Finite(out)&&math::Dot(d,d)<=25.f*25.f;
}
}
