#pragma once
#include "interactions.h"
#include <algorithm>

namespace framevr::elements {
enum class Element { None, Fire, Cold, Lightning };
// Explicit Sorceress active skills. Utility skills (including Teleport and
// Energy Shield) and passives deliberately have no elemental hand cue.
inline Element Classify(int skill) {
    switch(skill) {
    case 36: case 41: case 46: case 47: case 51: case 52: case 56: case 62:
        return Element::Fire;
    case 39: case 40: case 44: case 45: case 50: case 55: case 59: case 60: case 64:
        return Element::Cold;
    case 38: case 42: case 48: case 49: case 53: case 57:
        return Element::Lightning;
    default: return Element::None;
    }
}
// The spell effect whose own textures a hand shows (the renderer's sprite
// looks). Other skills take the closest look of their element.
enum class Look { FireBolt, Fireball, ChargedBolt, IceBolt, IceBlast, Torch };
inline Look SpellLook(int skill,Element element) {
    if(skill==999)return Look::Torch;
    switch(skill) {
    case 47: case 56: return Look::Fireball;
    case 45: case 55: case 59: case 64: return Look::IceBlast;
    default: break;
    }
    return element==Element::Fire?Look::FireBolt:element==Element::Cold?Look::IceBolt:Look::ChargedBolt;
}
struct Context {
    uint64_t session=0,epoch=0,binding=0;
    uint32_t equipment=0;
    bool operator==(const Context&)const=default;
};
struct Input {
    Context context;
    uint64_t at=0;
    bool enabled=false,active=false,tracked[2]{},occupied[2]{};
    interaction::Pose head,hands[2];
    int casting[2]{-1,-1};
    uint64_t activation[2]{};
};
struct Cast {
    Context context;
    uint64_t at=0,activation=0;
    int slot=-1,skill=-1,owner=-1;
};
struct Hand {
    interaction::Pose pose;
    Element element=Element::None;
    float opacity=0,pulse=0;
    int skill=-1;
};
struct Scene {
    std::array<Hand,2> hands{};
    uint64_t session=0,at=0;
    float time=0; // frozen once for the entire stereo pair
    bool Visible()const {return hands[0].opacity>0||hands[1].opacity>0;}
};
inline bool Current(const Scene& scene,uint64_t session,uint64_t now) {
    return session&&scene.session==session&&now>=scene.at&&now-scene.at<=150&&scene.Visible();
}
class State {
    struct Previous {int slot=-1,skill=-1,owner=-1;uint64_t since=0;};
    Context context;
    uint64_t at=0;
    Previous previous[2];
    Cast cast[2];
public:
    void Reset(){*this={};}
    // Called only after a native hand-spell dispatch succeeds. Both this and
    // Step are protected by the transport mutex; no game pointers cross over.
    void Accepted(int hand,const Cast& event) {
        if(hand>=0&&hand<2&&event.context.session&&event.slot>=0&&event.slot<skills::Count&&
           skills::Hand(event.slot)==hand&&Classify(event.skill)!=Element::None)cast[hand]=event;
    }
    Scene Step(const Input& in,const interaction::Snapshot& snapshot) {
        Scene scene;
        if(!in.enabled||!in.active||!interaction::Valid(in.head)||
           !interaction::Fresh(snapshot,in.context.session,in.at)||snapshot.skillClass!=1||
           !snapshot.vitalsKnown||!std::isfinite(snapshot.health)||snapshot.health<=0||
           !snapshot.actionKnown||snapshot.actionMode==0||snapshot.actionMode==17) {
            Reset();return scene;
        }
        if(context!=in.context||in.at<at||in.at-at>150){previous[0]=previous[1]={};cast[0]=cast[1]={};}
        context=in.context;at=in.at;
        scene.session=in.context.session;scene.at=in.at;
        scene.time=float(in.at%60000)/1000.f;
        for(int h=0;h<2;++h) {
            const int slot=in.casting[h]>=0?in.casting[h]:skills::TriggerSlot(h);
            if(!in.tracked[h]||in.occupied[h]||!interaction::Valid(in.hands[h])||
               slot<0||slot>=skills::Count||skills::Hand(slot)!=h) {
                previous[h]={};cast[h]={};continue;
            }
            const auto& skill=snapshot.skills[slot];
            const auto element=skill.known&&skill.available?Classify(skill.id):Element::None;
            if(element==Element::None){previous[h]={};cast[h]={};continue;}
            auto& old=previous[h];
            if(old.slot!=slot||old.skill!=skill.id||old.owner!=skill.owner)old={slot,skill.id,skill.owner,in.at};
            auto& hand=scene.hands[h];hand.pose=in.hands[h];hand.element=element;hand.skill=skill.id;
            const auto delta=hand.pose.p-in.head.p;
            const float distance=std::sqrt(math::Dot(delta,delta));
            // Fade out near the face before the aura can fill the view.
            hand.opacity=std::clamp((distance-.15f)/.15f,0.f,1.f)*
                std::clamp(float(in.at-old.since)/120.f,0.f,1.f);
            const auto& event=cast[h];
            if(event.context==in.context&&event.slot==slot&&event.skill==skill.id&&event.owner==skill.owner&&
               event.activation==in.activation[h]&&in.casting[h]==slot&&in.at>=event.at&&in.at-event.at<220)
                hand.pulse=1-float(in.at-event.at)/220.f;
            else cast[h]={};
        }
        return scene;
    }
};
}
