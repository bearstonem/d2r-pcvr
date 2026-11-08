#pragma once
#include "interactions.h"
#include <algorithm>
#include <cmath>

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
    bool active=false,tracked[2]{},occupied[2]{};
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
// One particle as the renderer draws it (its StructuredBuffer element, 64 bytes), in the room (XR stage
// metres): the effects' simulation (elemental_particles.h) writes them, both eyes draw the same.
enum Kind : uint32_t {
    Flame,   // a flame from the game's fire flipbook (frame: its first of 64), upright
    Ember,   // a hot spark, drawn along its velocity
    Glow,    // a soft glow (frame: the ramp - 0 flame, 1 lightning, 2 ice, 3 frost)
    Smoke,   // a dark smoke puff (frame: of 4), alpha-blended
    Arc,     // a short lightning arc along v (frame: of 8)
    Bolt,    // a longer lightning bolt along v (frame: of 8)
    Spark,   // an electric spark, drawn along its velocity
    Mist,    // a cold mist wisp (frame: of 4), alpha-blended
    Flake,   // a snowflake (frame: of 4)
    Chunk,   // an ice chunk (frame: of 4)
    Orb,     // the Frozen Orb's spinning shards
    Flare,   // a star flare (frame: the ramp, as Glow)
    Core,    // a fireball's churning core
    Plasma,  // Charged Bolt's own flash, two layers turning: the lightning ball under its arcs
    KindCount
};
struct Particle {
    math::Vec p{};float size=0;   // where, and the quad's half height (m)
    math::Vec v{};float life=0;   // its velocity, or for Arc and Bolt its axis (m/s); age over lifetime, 0..1
    float aspect=1,rotation=0;    // width over height; the quad's turn (rad)
    float intensity=1,blend=0;    // brightness; 0 adds light, 1 covers what is behind (smoke, mist)
    uint32_t kind=Flame,frame=0;
    float seed=0,stretch=0;       // stretch: seconds of v the quad is drawn along (0: facing the eye)
};
static_assert(sizeof(Particle)==64,"the renderer's StructuredBuffer stride");
constexpr unsigned MaxDrawn=320;   // both hands' particles, the most drawn in one pass
struct Scene {
    std::array<Hand,2> hands{};
    uint64_t session=0,at=0;
    float time=0; // frozen once for the entire stereo pair
    std::array<Particle,MaxDrawn> drawn{};unsigned count=0;   // the effects' particles this frame (elemental_particles.h)
    bool Visible()const {return hands[0].opacity>0||hands[1].opacity>0;}
};
// A spell hand's light on the world (vrcam lays it among the game's lights, as the held torch's; [torch]
// spell_hands): its element's colour, how strong (0..1: fire flickers, lightning crackles, frost stays
// even) and how far it reaches (world units, as [torch] radius). opacity: the hand's (Hand::opacity).
struct Light {float rgb[3]{};float strength=0,radius=0;};
inline Light HandLight(Element element,float opacity,uint64_t ms,int hand) {
    Light l;
    const float o=std::isfinite(opacity)?std::clamp(opacity,0.f,1.f):0.f,t=float(ms%600000)/1000.f,seed=float(hand)*1.7f;
    auto colour=[&](float r,float g,float b){l.rgb[0]=r;l.rgb[1]=g;l.rgb[2]=b;};
    switch(element) {
    case Element::Fire:colour(1,.55f,.22f);l.radius=11;
        l.strength=o*(.80f+.12f*std::sin(t*9.1f+seed)*std::sin(t*5.3f+seed*2)+.06f*std::sin(t*23.9f+seed));break;
    case Element::Lightning: {   // a new brightness every 40 ms
        const float k=std::floor(t*25)+seed*13,crackle=.5f+.5f*std::sin(k*12.9898f);
        colour(.55f,.70f,1);l.radius=10;l.strength=o*(.30f+.65f*crackle*crackle);break;
    }
    case Element::Cold:colour(.45f,.78f,1);l.radius=8;l.strength=o*(.45f+.05f*std::sin(t*1.7f+seed));break;
    default:break;
    }
    return l;
}
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
        if(!in.active||!interaction::Valid(in.head)||
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
