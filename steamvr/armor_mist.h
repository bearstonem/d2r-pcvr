#pragma once
#include "elemental_hands.h"
#include "arm_occlusion.h"
namespace framevr::armormist {
inline void Occlude(elements::Scene& scene,const elements::Input& in,
        const std::array<math::Vec,2>& arms,const std::array<math::Vec,2>& wrist) {
    for(unsigned i=0;i<scene.count;++i) {
        auto& p=scene.drawn[i];
        if(p.kind!=elements::PsychicMist&&p.kind!=elements::WhiteMist)continue;
        for(unsigned h=0;h<2;++h)if(in.tracked[h]) {
            const auto a=in.hands[h].p+math::Rotate(in.hands[h].q,wrist[h]);
            const auto b=a+math::Rotate(in.hands[h].q,arms[h]);
            if(armocclusion::Hidden(in.head.p,p.p,a,b))p.intensity=0;
        }
    }
}
// Native armor states, independent of class/bindings (including classless mods).
// Never infer protection from skill selection, an absorption pool or a timer.
inline elements::Scene Scene(const elements::Input& in,const interaction::Snapshot& s,bool mist,const std::array<math::Vec,2>& towardElbow={math::Vec{0,0,1},math::Vec{0,0,1}},int state=206,elements::Element element=elements::Element::Psychic,int skill=387,const std::array<math::Vec,2>& wrist={},float along=.04f) {
    elements::Scene scene;
    if(!mist||!in.active||!interaction::Valid(in.head)||
       !interaction::Fresh(s,in.context.session,in.at)||!s.effectsAt||in.at<s.effectsAt||in.at-s.effectsAt>1000||
       !s.effects.Has(state)||!s.vitalsKnown||!std::isfinite(s.health)||s.health<=0||
       !s.actionKnown||s.actionMode==0||s.actionMode==17)return scene;
    scene.session=in.context.session;scene.at=in.at;scene.time=float(in.at%60000)/1000.f;
    for(int h=0;h<2;++h) {
        if(!in.tracked[h]||!interaction::Valid(in.hands[h]))continue;
        auto& hand=scene.hands[h];hand.pose=in.hands[h];hand.element=element;hand.skill=skill;
        // Anchor on the drawn wrist, not the controller origin beside the knuckles.
        hand.pose.p=hand.pose.p+math::Rotate(hand.pose.q,wrist[h]+math::Normalized(towardElbow[h])*along);
        const auto y=math::Normalized(towardElbow[h]);
        const auto x=math::Normalized(math::Cross(y,std::fabs(y.y)<.9f?math::Vec{0,1,0}:math::Vec{1,0,0}));
        hand.pose.q=math::Multiply(hand.pose.q,math::FromAxes(x,y,math::Cross(x,y)));
        const auto delta=hand.pose.p-in.head.p;
        // Mist stays in a 10 cm envelope. Completely fade before it reaches
        // the eyes; also hide near-face particles when only one hand is close.
        hand.opacity=std::clamp((std::sqrt(math::Dot(delta,delta))-.22f)/.18f,0.f,1.f);
    }
    return scene;
}
}
