#include "spell_motion.h"
#include "motion_sim.h"
#include <cmath>
#include <cstdio>
#include <string>
using namespace framevr;
using spellmotion::Motion;
using motionsim::Check;
using motionsim::RestL;
using motionsim::RestR;
using motionsim::Sim;
// The Assassin's motions, done well, from the resting pose (head-local: +X right, +Y up, -Z forward).
static void Pump(Sim& s,float seconds){
    for(int i=0;i<4;++i){
        const bool rightAhead=i%2==0;
        const math::Vec ahead{0,-.42f,-.35f},back{0,-.55f,.10f};
        s.Move(math::Vec{.20f,0,0}+(rightAhead?ahead:back),math::Vec{-.20f,0,0}+(rightAhead?back:ahead),seconds);
    }
}
static void BurstOfSpeed(Sim& s){Pump(s,.22f);}
static void Fade(Sim& s){s.Move({.07f,.04f,-.15f},{-.07f,.04f,-.15f},.4f);s.Move({.07f,-.22f,-.17f},{-.07f,-.22f,-.17f},.35f);}
static void CloakOfShadows(Sim& s){s.Move({.38f,-.28f,-.25f},{-.38f,-.28f,-.25f},.4f);s.Move({-.14f,-.32f,-.30f},{.14f,-.32f,-.30f},.4f);}
static void VenomFlick(Sim& s,float seconds){
    s.Move({-.06f,-.35f,-.32f},{.06f,-.35f,-.32f},.45f);s.Hold(.15f);s.Move({.32f,-.32f,-.30f},{-.32f,-.32f,-.30f},seconds);
}
static void Venom(Sim& s){VenomFlick(s,.18f);}
static void Circle(Sim& s,float radius,float turns,float seconds){
    const math::Vec c{.18f,-.30f,-.35f};
    s.MoveR(c+math::Vec{radius,0,0},.3f);
    s.Drive(seconds,[&](float t,math::Vec& r,math::Vec&){const float a=6.2831853f*turns*t/seconds;r=c+math::Vec{radius*std::cos(a),radius*std::sin(a),0};});
}
static void BladeShield(Sim& s){Circle(s,.12f,2,1.2f);}
static void ShadowTo(Sim& s,float side){
    s.Move({.08f,-.30f,-.20f},{-.08f,-.30f,-.20f},.4f);
    s.Move({side>0?.48f:-.28f,-.30f,-.25f},{side>0?.28f:-.48f,-.30f,-.25f},.3f);
}
static void ShadowWarrior(Sim& s){ShadowTo(s,1);}
static void BlastAt(Sim& s,float seconds){
    s.Move({.13f,-.03f,-.06f},{-.13f,-.03f,-.06f},.45f);s.Hold(.1f);s.Move({.13f,-.08f,-.55f},{-.13f,-.08f,-.55f},seconds);
}
static void MindBlast(Sim& s){BlastAt(s,.25f);}
// Not motions: what an Assassin's hands do anyway.
static void WalkSwing(Sim& s){
    for(int i=0;i<6;++i){const float a=i%2?.30f:-.05f,b=i%2?-.05f:.30f;s.Move({.24f,-.76f,-a},{-.24f,-.76f,-b},.5f);}
}
static void ReachHip(Sim& s){s.MoveR({.28f,-.85f,.02f},.4f);s.Hold(.3f);s.MoveR(RestR,.4f);}
static void ClawJabs(Sim& s){
    s.Move({.12f,-.18f,-.18f},{-.12f,-.18f,-.18f},.3f);
    for(int i=0;i<3;++i){
        s.MoveR({.12f,-.15f,-.62f},.15f);s.MoveR({.12f,-.18f,-.18f},.15f);
        s.MoveL({-.12f,-.15f,-.62f},.15f);s.MoveL({-.12f,-.18f,-.18f},.15f);
    }
}
static void TrapThrow(Sim& s){s.MoveR({.30f,-.25f,.25f},.35f);s.MoveR({.25f,.10f,-.55f},.2f);s.MoveR(RestR,.4f);}
static bool Zero(math::Vec v){return v.x==0&&v.y==0&&v.z==0;}

int main() {
    using Do=void(*)(Sim&);
    const struct {Motion motion;Do perform;int skill;const char* name;} all[]={
        {Motion::BurstOfSpeed,BurstOfSpeed,258,"Burst of Speed"},{Motion::Fade,Fade,267,"Fade"},
        {Motion::CloakOfShadows,CloakOfShadows,264,"Cloak of Shadows"},{Motion::Venom,Venom,278,"Venom"},
        {Motion::BladeShield,BladeShield,277,"Blade Shield"},{Motion::ShadowWarrior,ShadowWarrior,268,"Shadow Warrior"},
        {Motion::MindBlast,MindBlast,273,"Mind Blast"}};
    const struct {Do perform;const char* name;} other[]={
        {[](Sim& s){s.Hold(2);},"resting"},{WalkSwing,"a walking arm swing"},{ReachHip,"a reach to the hip (a belt potion)"},
        {ClawJabs,"claw jabs"},{TrapThrow,"a trap throw"}};
    for(const auto& m:all) {
        const std::string label=std::string(m.name)+": ";
        Check(spellmotion::For(m.skill)==m.motion&&std::string(spellmotion::Cue(m.motion))!="no motion",(label+"its skill has the motion and a cue").c_str());
        for(float heading:{-2.f,0.f,2.f}){Sim s(m.motion,heading);m.perform(s);s.Hold(.5f);
            Check(s.casts==1,(label+"done well, it casts once, at any heading").c_str());
            Check(m.motion==Motion::ShadowWarrior?s.aim.x>.2f&&std::fabs(s.aim.y)<1e-6f:Zero(s.aim),(label+"its aim: the side pushed, or none").c_str());}
        {Sim s(m.motion);s.in.active=false;m.perform(s);Check(s.casts==0,(label+"the gate closed (one grip, a menu, a bottle): nothing").c_str());}
        for(const auto& o:all)if(o.motion!=m.motion){Sim s(m.motion);o.perform(s);
            Check(s.casts==0,(label+"another motion ("+o.name+") does not cast it").c_str());}
        for(const auto& o:other){Sim s(m.motion);o.perform(s);
            Check(s.casts==0,(label+o.name+" does not cast it").c_str());}
    }
    // Shadow Warrior goes where the hands pushed, to either side; Mind Blast takes the right hand's ray.
    {Sim s(Motion::ShadowWarrior);ShadowTo(s,-1);Check(s.casts==1&&s.aim.x<-.2f,"Shadow Warrior: pushed to the left, she appears on the left");}
    // Slow, lazy and small versions.
    {Sim s(Motion::BurstOfSpeed);Pump(s,.6f);Check(s.casts==0,"Burst of Speed: slow pumps are a walk, not a sprint");}
    {Sim s(Motion::Venom);VenomFlick(s,.8f);Check(s.casts==0,"Venom: wrists drawn apart slowly are no flick");}
    {Sim s(Motion::MindBlast);BlastAt(s,1.f);Check(s.casts==0,"Mind Blast: hands drifting forward are no blast");}
    {Sim s(Motion::BladeShield);const math::Vec c{.15f,-.35f,-.35f};s.MoveR(c+math::Vec{.12f,0,0},.3f);
     s.Drive(1.2f,[&](float t,math::Vec& r,math::Vec&){const float a=6.2831853f*2*t/1.2f;r=c+math::Vec{.12f*std::cos(a),0,.12f*std::sin(a)};});
     Check(s.casts==1,"Blade Shield: two circles stirred flat count too");}
    {Sim s(Motion::BladeShield);Circle(s,.12f,1,.6f);Check(s.casts==0,"Blade Shield: one circle is not two");}
    {Sim s(Motion::BladeShield);Circle(s,.03f,2,1.2f);Check(s.casts==0,"Blade Shield: a tiny circle is a wobble");}
    {Sim s(Motion::Fade);s.Move({.07f,.04f,-.15f},{-.07f,.04f,-.15f},.4f);s.Move({.07f,-.06f,-.15f},{-.07f,-.06f,-.15f},.3f);
     Check(s.casts==0,"Fade: hands lowered to the nose only are not drawn to the chin");}
    {Sim s(Motion::CloakOfShadows);s.Move({.38f,-.28f,-.25f},{-.38f,-.28f,-.25f},.4f);s.Move({.04f,-.32f,-.30f},{-.04f,-.32f,-.30f},.4f);
     Check(s.casts==0,"Cloak of Shadows: arms brought together but not crossed");}
    // What a tester could not get to register (2026-11-05): pumps in front of the body, the hands low;
    // Fade from the eyes, hands a little apart.
    {Sim s(Motion::BurstOfSpeed);
     for(int i=0;i<4;++i){const bool r=i%2==0;s.Move({.22f,r?-.50f:-.80f,r?-.32f:-.05f},{-.22f,r?-.80f:-.50f,r?-.05f:-.32f},.25f);}
     Check(s.casts==1,"Burst of Speed: two pumps a hand in front of the hips, low, cast");}
    {Sim s(Motion::Fade);s.Move({.14f,-.04f,-.25f},{-.14f,-.04f,-.25f},.4f);s.Move({.14f,-.26f,-.28f},{-.14f,-.26f,-.28f},.4f);
     Check(s.casts==1,"Fade: from the eyes to the chin, the hands 28 cm apart, casts");}
    // Done again after its wait, it casts again.
    {Sim s(Motion::Venom);Venom(s);s.Hold(.8f);Venom(s);Check(s.casts==2,"Venom: again after the wait, again");}
    {   // the owner's Shadow Warrior pushes (2026-11-05): the trailing hand 1-8 cm short of the middle
        Sim push(Motion::ShadowWarrior);push.Move({.15f,-.33f,-.22f},{-.15f,-.32f,-.20f},.35f);push.Hold(.1f);push.Move({.40f,-.40f,-.30f},{-.04f,-.37f,-.24f},.3f);push.Hold(.2f);
        Check(push.casts==1,"Shadow Warrior: pushed right, the left hand 4 cm short of the middle, casts");
    }
    std::puts("motions-assassin-test: ok");
    return 0;
}
