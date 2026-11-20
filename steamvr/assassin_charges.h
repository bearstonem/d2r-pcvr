#pragma once
#include "armor_mist.h"
namespace framevr::assassincharges {
// Compact original art, driven only by verified native state + charge counters.
// Each active skill gets a ring along each forearm; 1/2/3 orbs show its count.
inline elements::Scene Scene(const elements::Input& in,const interaction::Snapshot& s,int mode,
        const std::array<math::Vec,2>& arms,const std::array<math::Vec,2>& wrist={}) {
    elements::Scene out;
    if(mode!=2||!s.effects.chargesKnown)return out;
    out.chargeOnly=true;
    for(unsigned skill=0;skill<6;++skill) {
        const auto n=s.effects.charges[skill];if(!n||n>3)continue;
        auto ring=armormist::Scene(in,s,true,arms,effects::ChargeStates[skill],elements::Element::Psychic,387,wrist);
        if(!ring.Visible())continue;
        out.session=ring.session;out.at=ring.at;out.time=ring.time;
        for(unsigned h=0;h<2;++h) {
            const auto& hand=ring.hands[h];if(hand.opacity<=0)continue;
            const auto axis=math::Normalized(arms[h]);
            const auto center=hand.pose.p+math::Rotate(in.hands[h].q,axis*(float(skill)*.025f));
            const auto toHead=center-in.head.p;
            const float opacity=std::clamp((std::sqrt(math::Dot(toHead,toHead))-.22f)/.18f,0.f,1.f);
            // Derive perpendicular axes from the actual forearm, not room up.
            auto x=math::Normalized(math::Cross(axis,std::fabs(axis.y)<.9f?math::Vec{0,1,0}:math::Vec{1,0,0}));
            const auto z=math::Cross(axis,x);
            for(unsigned charge=0;charge<n&&out.count<elements::MaxDrawn;++charge) {
                const float a=out.time*(h?-2.2f:2.2f)+float(charge)*6.2831853f/float(n)+float(skill)*.6f;
                elements::Particle p;
                p.p=center+math::Rotate(in.hands[h].q,(x*std::cos(a)+z*std::sin(a))*.045f);
                p.kind=elements::ChargeOrb;p.frame=skill;
                p.size=.013f;p.intensity=opacity*.65f;p.life=.5f;p.blend=1;
                for(unsigned arm=0;arm<2;++arm)if(in.tracked[arm]) {
                    const auto a=in.hands[arm].p+math::Rotate(in.hands[arm].q,wrist[arm]);
                    const auto b=a+math::Rotate(in.hands[arm].q,arms[arm]);
                    if(armocclusion::Hidden(in.head.p,p.p,a,b))p.intensity=0;
                }
                out.drawn[out.count++]=p;
            }
        }
    }
    return out;
}
}
