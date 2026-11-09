#pragma once
#include "elemental_hands.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

// The elemental hands' particles (owner, 2026-11-05: "Start the hand effects renderer"; a tester the same
// day: "the implementation is very very rudimentary ... Asset effects from the game could be reused/
// reapplied to the hands in some cool ways"). Eight sprites placed by a formula each frame became effects
// that live: each hand's look (SpellLook) runs a few emitters that spawn particles in the room (XR stage
// metres), and each particle then moves, grows, turns, animates and fades on its own, so a hand that moves
// leaves its fire, frost or sparks behind it a moment. A few more stay on the hand (a glow, a fireball's
// core, the lightning ball, the Frozen Orb). The game drawing its own spell effects at the hand was tried
// on 2026-10-21 and was "much too big" (its particles could not be scaled), so these are the game's
// textures (prepare_spell_sprites.py, spell_sprites.h) on effects of the hand's size: a flame cupped in the
// fist from its fire flipbook, embers and a little smoke; a churning fireball; a crackling ball with arcs
// and sparks from its lightning flipbooks; cold mist sinking with snowflakes circling and glints; the Frozen
// Orb with ice chunks circling. A confirmed cast (Hand::pulse) throws a burst. The torch club's flame is the
// fire look, larger, at the club's head.
//
// Step once a frame, after the scene's hands are set (the transport, after the torch); it writes
// Scene::drawn. Pure (no Direct3D, no game): steamvr/elemental_particles_test.cpp.
namespace framevr::elements {
class Effects {
public:
    static constexpr unsigned MaxLive=140;     // a hand's free particles at most
    static constexpr unsigned MaxAttached=4;   // a hand's particles that stay on it
    static_assert(2*(MaxLive+MaxAttached)<=MaxDrawn,"both hands fit one pass");
    static constexpr float MaxStep=.05f;       // s: a longer frame steps this much (a hitch never jumps them far)
    static constexpr uint64_t GapMs=250;       // a gap this long starts the effects over (a load, a menu)
private:
    struct Emitter {
        Kind kind;float rate;          // per second
        float lifeMin,lifeMax;         // s
        float radius;                  // m: spawned this far from the centre at most (a ball; a shell when shell)
        float lift;                    // m: the spawn point raised this much
        float speedMin,speedMax;       // m/s: up (Flame, Ember, Smoke), out (Spark, cast bursts), down (Mist: negative)
        float scatter;                 // m/s: random sideways
        float rise,drag,swirl;         // m/s2 up (negative sinks); 1/s; m/s2 of turbulence
        float size0,size1;             // m: half height at birth and at death (eased)
        float aspect,intensity,blend;
        float stretch;                 // s of velocity drawn along (sparks)
        uint32_t frames;               // variants to pick from (Smoke, Mist, Flake, Chunk: 4; Arc, Bolt: 8)
        float orbit;                   // rad/s: circles the hand's centre about the room's up, kept with the hand
        bool shell;                    // spawned on the sphere of radius, not inside it
        bool axis;                     // Arc and Bolt: drawn from the centre along a random way
    };
    struct Live {
        Particle p;float age=0,lifetime=1,size0=0,size1=0,spin=0,rise=0,drag=0,swirl=0,peak=1;
        float orbit=0,angle=0,radius=0,height=0;   // orbiting: around its hand's centre
        int hand=0;bool flipbook=false;
    };
    struct HandState {
        std::array<Live,MaxLive> live{};unsigned count=0;
        std::array<float,6> owed{};   // emitters' fractional spawns carried to the next frame
        math::Vec centre{};bool had=false;float pulse=0;int look=-1;
    };
    std::array<HandState,2> hands{};
    uint64_t at=0;uint32_t random=0x9E3779B9u;
    float Uniform(){random^=random<<13;random^=random>>17;random^=random<<5;return float(random>>8)*(1.f/16777216.f);}
    float Range(float a,float b){return a+(b-a)*Uniform();}
    math::Vec Ball(float radius,bool shell) {
        for(int tries=0;tries<8;++tries) {
            const math::Vec v{Range(-1,1),Range(-1,1),Range(-1,1)};
            const float d=math::Dot(v,v);
            if(d>1e-4f&&d<=1)return shell?v*(radius/std::sqrt(d)):v*radius;
        }
        return {0,radius,0};
    }
    static constexpr math::Vec Up{0,1,0};
    // Each look's emitters (sizes in metres: a hand's effect stays about a fist across, its embers and mist
    // drifting a little further). Torch: the fire look, scaled.
    static const Emitter* Emitters(Look look,unsigned& count) {
        //                               kind  rate  life       radius lift  speed       scat  rise  drag swirl size0  size1  asp  int  blend stretch fr orbit shell axis
        static const Emitter fire[]={{Flame,26,  .50f,.75f, .012f,.004f, .06f,.12f, .015f,.20f,1.5f,.10f,.026f,.040f,.80f,1.0f,.15f,0,    1, 0,    false,false},
                                     {Ember, 9,  .70f,1.3f, .015f,.010f, .10f,.25f, .060f,.15f, .8f,.35f,.0035f,.002f,1,  2.2f,0,   .035f,1, 0,    false,false},
                                     {Smoke,2.5f,1.1f,1.6f, .010f,.050f, .06f,.08f, .010f,.03f, .5f,.05f,.020f,.060f,1,   .9f,1,   0,    4, 0,    false,false}};
        static const Emitter fireball[]={{Flame,34,.40f,.60f,.022f,0,    .07f,.11f, .030f,.25f,1.5f,.12f,.022f,.036f,.80f,1.1f,.15f,0,    1, 0,    true, false},
                                     {Ember,12,  .60f,1.1f, .020f,.008f, .12f,.30f, .080f,.15f, .8f,.40f,.0035f,.002f,1,  2.4f,0,   .035f,1, 0,    false,false},
                                     {Smoke, 3,  1.0f,1.5f, .012f,.050f, .06f,.08f, .010f,.03f, .5f,.05f,.022f,.065f,1,   .9f,1,   0,    4, 0,    false,false}};
        static const Emitter lightning[]={{Arc,40,.05f,.10f, .004f,0,    0,0,       0,    0,    0,   0,   .026f,.036f,.40f,2.4f,0,   0,    8, 0,    false,true},
                                     {Bolt,4.5f, .07f,.11f, .006f,0,     0,0,       0,    0,    0,   0,   .040f,.050f,.30f,2.2f,0,   0,    8, 0,    false,true},
                                     {Spark,14,  .12f,.25f, .006f,0,     .50f,1.0f, 0,    0,    4,   0,   .0025f,.0018f,1,  2.4f,0,   .030f,1, 0,    false,false}};
        static const Emitter cold[]={{Mist,  5,  1.2f,1.8f, .020f,0,    -.02f,-.01f,.020f,-.03f,.8f,.04f,.025f,.070f,1,   .8f,.9f, 0,    4, 0,    false,false},
                                     {Flake, 6,  .90f,1.5f, .040f,.005f,-.01f,0,    0,    0,    0,   0,   .006f,.004f,1,   1.2f,.6f, 0,    4, 1.2f, true, false},
                                     {Flare, 5,  .15f,.25f, .030f,0,     0,0,       0,    0,    0,   0,   .010f,.004f,1,   1.6f,0,   0,    1, 0,    true, false}};
        static const Emitter orb[]={{Chunk,  7,  .80f,1.2f, .040f,0,     0,0,       0,    0,    0,   0,   .007f,.006f,1,   1.3f,.5f, 0,    4, 2.0f, true, false},
                                     {Mist,  4,  1.2f,1.8f, .020f,0,    -.02f,-.01f,.020f,-.03f,.8f,.04f,.025f,.065f,1,   .8f,.9f, 0,    4, 0,    false,false},
                                     {Flare, 6,  .15f,.25f, .032f,0,     0,0,       0,    0,    0,   0,   .010f,.004f,1,   1.6f,0,   0,    1, 0,    true, false},
                                     {Flake, 3,  .90f,1.5f, .045f,.005f,-.01f,0,    0,    0,    0,   0,   .006f,.004f,1,   1.2f,.6f, 0,    4,-1.4f, true, false}};
        switch(look) {
        case Look::Fireball:count=unsigned(std::size(fireball));return fireball;
        case Look::ChargedBolt:count=unsigned(std::size(lightning));return lightning;
        case Look::IceBolt:count=unsigned(std::size(cold));return cold;
        case Look::IceBlast:count=unsigned(std::size(orb));return orb;
        default:count=unsigned(std::size(fire));return fire;   // Fire Bolt, the torch
        }
    }
    // The particles that stay on the hand, drawn this frame.
    void Attached(const Hand& hand,Look look,math::Vec centre,float scale,float t,int h,Scene& scene) {
        const float pulse=std::clamp(hand.pulse,0.f,1.f),seed=float(h)*17.3f;
        auto add=[&](Kind kind,float size,float intensity,float blend,uint32_t frame,float rotation) {
            if(scene.count>=MaxDrawn)return;
            Particle p;p.p=centre;p.size=size*scale*(1+.35f*pulse);p.kind=kind;p.frame=frame;p.rotation=rotation;
            p.intensity=intensity*(1+.8f*pulse)*std::clamp(hand.opacity,0.f,1.f);p.blend=blend;p.seed=seed;p.life=.5f;
            scene.drawn[scene.count++]=p;
        };
        const float flicker=.85f+.15f*std::sin(t*13.1f+seed)*std::sin(t*7.7f+seed*.3f);
        switch(look) {
        case Look::Fireball:add(Glow,.075f,.60f*flicker,0,0,0);add(Core,.028f,1.4f,.3f,0,t*.9f);break;
        case Look::ChargedBolt: {
            const float crackle=std::floor(t*24+seed);   // a new brightness every 40 ms
            const float jitter=.55f+.45f*(.5f+.5f*std::sin(crackle*12.9898f+seed));
            add(Glow,.060f,.38f*jitter,0,1,0);add(Plasma,.026f,1.5f,0,0,t*2.3f);break;
        }
        case Look::IceBolt:add(Glow,.050f,.38f,0,2,0);break;
        case Look::IceBlast:add(Glow,.065f,.40f,0,3,0);add(Orb,.026f,1.2f,.4f,0,t*.6f);break;
        default:add(Glow,.055f,.55f*flicker,0,0,0);break;   // Fire Bolt, the torch
        }
    }
    void Spawn(HandState& s,const Emitter& e,Look look,math::Vec where,float scale,int h,math::Vec handVelocity,float speedUp=1) {
        if(s.count>=MaxLive)return;
        Live l;l.hand=h;l.lifetime=Range(e.lifeMin,e.lifeMax);l.age=0;
        const auto offset=Ball(e.radius*scale,e.shell);
        l.p.kind=e.kind;l.p.aspect=e.aspect;l.p.intensity=e.intensity;l.p.blend=e.blend;l.p.stretch=e.stretch;
        l.p.seed=Uniform()*100;l.p.frame=e.frames>1?uint32_t(Uniform()*float(e.frames))%e.frames:0;
        l.size0=e.size0*scale*Range(.8f,1.2f);l.size1=e.size1*scale*Range(.8f,1.2f);l.rise=e.rise*scale;l.drag=e.drag;l.swirl=e.swirl*scale;
        l.flipbook=e.kind==Flame;
        if(l.flipbook)l.p.frame=uint32_t(Uniform()*6);   // where in the flame's 64 frames it starts
        l.p.rotation=e.kind==Flame||e.axis?0:Range(0,6.2831853f);l.spin=e.kind==Smoke||e.kind==Mist||e.kind==Flake||e.kind==Chunk?Range(-.8f,.8f):0;
        if(e.kind==Flare){l.p.frame=look==Look::IceBlast?3u:2u;l.spin=Range(-2,2);}
        if(e.axis) {   // an arc from the centre out along a random way
            const auto way=math::Normalized(Ball(1,true));
            const float half=Range(e.size0,e.size1)*scale;
            l.p.v=way;l.size0=l.size1=half;l.p.p=where+way*(half+e.radius*scale);
        } else if(e.orbit!=0) {   // circling the hand's centre, kept with it
            l.orbit=e.orbit*Range(.7f,1.3f);l.angle=Range(0,6.2831853f);l.radius=e.radius*scale*Range(.7f,1.f);
            l.height=Range(-.5f,.5f)*e.radius*scale+e.lift*scale;
            l.p.v={0,e.speedMin*scale,0};l.p.p=where+math::Vec{std::cos(l.angle)*l.radius,l.height,std::sin(l.angle)*l.radius};
        } else {
            l.p.p=where+offset+Up*(e.lift*scale);
            const float speed=Range(e.speedMin,e.speedMax)*scale*speedUp;
            const math::Vec side{Range(-1,1)*e.scatter*scale,0,Range(-1,1)*e.scatter*scale};
            const bool outward=e.kind==Spark||speedUp>1;
            l.p.v=(outward?math::Normalized(math::Dot(offset,offset)>1e-10f?offset:Ball(1,true))*speed:Up*speed)+side+handVelocity*.2f;
        }
        l.peak=e.intensity;l.p.size=l.size0;
        s.live[s.count++]=l;
    }
public:
    void Reset(){const auto r=random;*this=Effects{};random=r;}
    void Step(Scene& scene) {
        scene.count=0;
        if(!scene.Visible()||!scene.at||!std::isfinite(scene.time)){Reset();return;}
        if(!at||scene.at<at||scene.at-at>GapMs)Reset();
        const float dt=at?std::min(float(scene.at-at)/1000.f,MaxStep):0.f;
        at=scene.at;
        const float t=scene.time;
        for(int h=0;h<2;++h) {
            auto& s=hands[size_t(h)];const auto& hand=scene.hands[size_t(h)];
            const bool on=hand.element!=Element::None&&hand.opacity>0&&interaction::Valid(hand.pose);
            const Look look=on?SpellLook(hand.skill,hand.element):Look::FireBolt;
            const bool torch=on&&look==Look::Torch;
            const float scale=torch?2.2f:1.f;
            const math::Vec centre=on?(torch?hand.pose.p:hand.pose.p+math::Rotate(hand.pose.q,{0,0,-.03f})):s.centre;
            const math::Vec moved=s.had&&dt>0?(centre-s.centre)*(1/dt):math::Vec{};
            const math::Vec velocity=math::Dot(moved,moved)<9?moved:math::Vec{};   // a jump (a recentre) is no motion
            // the free particles: move, age, die
            unsigned kept=0;
            for(unsigned i=0;i<s.count;++i) {
                auto l=s.live[i];
                l.age+=dt;
                if(l.age>=l.lifetime||!std::isfinite(l.p.p.x+l.p.p.y+l.p.p.z))continue;
                if(l.orbit!=0) {   // kept with the hand as it circles
                    l.angle+=l.orbit*dt;l.height+=l.p.v.y*dt;
                    l.p.p=centre+math::Vec{std::cos(l.angle)*l.radius,l.height,std::sin(l.angle)*l.radius};
                } else if(l.p.kind!=Arc&&l.p.kind!=Bolt) {
                    const auto& p=l.p.p;const float w=l.p.seed;
                    const math::Vec swirl{std::sin(p.y*41+t*3.1f+w),0,std::cos(p.x*37+t*2.7f+w*1.3f)};
                    l.p.v=l.p.v+(Up*l.rise+swirl*l.swirl)*dt;
                    l.p.v=l.p.v*std::max(0.f,1-l.drag*dt);
                    l.p.p=l.p.p+l.p.v*dt;
                }
                l.p.rotation+=l.spin*dt;
                s.live[kept++]=l;
            }
            s.count=kept;
            // new ones, spread over the frame along the hand's way (a moving hand leaves a trail)
            if(on) {
                if(s.look!=int(look))s.owed={};
                unsigned n=0;const Emitter* emitters=Emitters(look,n);
                const float rate=std::clamp(hand.opacity,0.f,1.f)*(torch?1.5f:1.f);
                for(unsigned k=0;k<n&&k<s.owed.size();++k) {
                    const auto& e=emitters[k];
                    s.owed[k]+=e.rate*rate*dt;
                    const int spawns=int(s.owed[k]);s.owed[k]-=float(spawns);
                    for(int j=0;j<spawns;++j) {
                        const float f=(float(j)+Uniform())/float(spawns);
                        const auto from=s.had?s.centre+(centre-s.centre)*f:centre;
                        Spawn(s,e,look,from,scale,h,velocity);
                    }
                }
                // a confirmed cast: a burst flung out
                const float pulse=std::clamp(hand.pulse,0.f,1.f);
                if(pulse>s.pulse+.5f) {
                    const Kind burst=look==Look::ChargedBolt?Spark:look==Look::IceBolt||look==Look::IceBlast?Flake:Ember;
                    for(unsigned k=0;k<n;++k)if(emitters[k].kind==burst||(burst==Flake&&emitters[k].kind==Chunk)) {
                        Emitter e=emitters[k];e.orbit=0;e.speedMin=.5f;e.speedMax=1.1f;e.drag=3;e.rise=0;e.stretch=.03f;e.lifeMin=.25f;e.lifeMax=.45f;
                        for(int j=0;j<14;++j)Spawn(s,e,look,centre,scale,h,velocity,2);
                    }
                    if(look==Look::ChargedBolt)for(int j=0;j<5;++j)Spawn(s,emitters[1],look,centre,scale,h,velocity);
                }
                s.pulse=pulse;s.look=int(look);
                s.centre=centre;s.had=true;
            } else {s.had=false;s.pulse=0;}
            // drawn: those on the hand, then the free ones
            if(on)Attached(hand,look,centre,scale,t,h,scene);
            const float fade=on?std::clamp(hand.opacity,0.f,1.f):1.f;
            for(unsigned i=0;i<s.count&&scene.count<MaxDrawn;++i) {
                const auto& l=s.live[i];
                const float life=std::clamp(l.age/l.lifetime,0.f,1.f),ease=life*life*(3-2*life);
                Particle p=l.p;p.life=life;p.size=l.size0+(l.size1-l.size0)*ease;
                p.intensity=l.peak*fade;
                if(l.flipbook)p.frame=l.p.frame;   // the shader runs the 64 frames over its life from here
                scene.drawn[scene.count++]=p;
            }
        }
    }
    unsigned Count(int hand)const{return hand>=0&&hand<2?hands[size_t(hand)].count:0;}   // a hand's free particles
};
}
