#pragma once
#include "melee_contact.h"
#include "world_coordinates.h"
#include "../vr/target_lock.h"
namespace framevr::clientcontact {
// Feedback only: consumes the SAME sweep sent to the host. No extra detector,
// attack, target ownership or damage decision occurs on the joining client.
inline uint32_t Touched(const melee::Sample& sample,const melee::Sweep& sweep,const targetlock::Snapshot& targets,uint64_t now) {
 if(!sample.active||!sweep.stroke||!targets.at||now<targets.at||now-targets.at>250||
    targets.count<0||targets.count>targetcone::MaxTargets||
    !coordinates::MatchesPlayer({targets.heroX*2,0,targets.heroY*2},sample.player,sample.unitsPerMetre))return 0;
  for(int i=0;i<targets.count;++i){
   const auto& t=targets.targets[i];
   if(!t.id||!t.clear||!t.bodyWidth||t.bodyWidth>8||!t.bodyHeight||!std::isfinite(t.x)||!std::isfinite(t.y))continue;
   // Same native MonStats2 footprint/height conversion as melee::BodyFor.
   const float upm=sample.unitsPerMetre;
   const float radius=std::max(.12f,float(t.bodyWidth)*.7f/upm);
   const float height=std::clamp(float(t.bodyHeight)*.10f/upm,.4f,4.f);
   melee::Body body;body.radius=std::min(radius,height*.45f)+.05f;
   body.bottom=coordinates::RenderGround({t.x*2/upm,0,t.y*2/upm},sample.player.y)+math::Vec{0,body.radius,0};
   body.top=body.bottom;body.top.y=sample.player.y+height-body.radius;
   if(!melee::Touches(sweep.before,sweep.after,melee::Reach(body,sample.reach,sample.shield)))continue;
   return t.id;
  }
 return 0;
}
}
