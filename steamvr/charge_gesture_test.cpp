#include "charge_gesture.h"
#include "spell_motion.h"
#include <cstdio>
#include <cstdlib>
#include <string>
using namespace framevr;
static void Check(bool b,const char* label){if(!b){std::fprintf(stderr,"FAIL: %s\n",label);std::exit(1);}}
// Body frame (yaw-local: +X right, +Y up, -Z forward) relative to the head.
// The weapon hand moves between keyframes at 50 Hz, so velocity comes from motion.
struct Sim {
    // The motions' gate (spell_motion.h, owner 2026-11-04: "Both grips, like Sorceress"): gated, in.active is it.
    bool gated=false;spellmotion::Gate gate;float grips[2]{};
    chargepunch::Engine g;chargepunch::Input in;chargepunch::Output out;
    math::Quat yaw{0,0,0,1};math::Vec head{0,1.7f,0};
    int casts=0,drops=0;std::string lastDrop;
    // natural: minimum-jerk easing at 90 Hz, as a real arm moves (accelerates
    // from rest, peaks mid-stroke, slows to a stop). The 2026-10-27 headset
    // test rejected every real punch that constant-speed keyframes passed.
    bool natural=false;float frame=.02f;
    void Natural(){natural=true;frame=1/90.f;}
    float Ease(float t)const{return natural?t*t*t*(10+t*(-15+6*t)):t;}
    Sim(float heading=0,float span=0){yaw=math::Yaw(heading);in.time=1;in.session=7;in.epoch=1;in.active=true;in.tracked=true;in.armSpan=span;
        in.head=head;in.headRotation=yaw;Place({.25f,-.6f,-.1f});Step();}
    void Place(math::Vec p){in.hand=head+math::Rotate(yaw,p);}
    void Move(math::Vec to,float seconds) {
        const math::Vec a=in.hand,b=head+math::Rotate(yaw,to);
        const int steps=std::max(1,int(seconds/frame));
        for(int i=1;i<=steps;++i){in.hand=a+(b-a)*Ease(float(i)/steps);Step();}
    }
    void Hold(float seconds){for(int i=0;i<int(seconds/frame);++i)Step();}
    void Step(){in.time+=frame;if(gated)in.active=gate.Step(grips,true).watch;out=g.Step(in);if(out.cast)++casts;if(out.dropped){++drops;lastDrop=out.dropped;}}
    // Fist drawn back at the right shoulder, then punched straight out ahead.
    void Punch(float seconds=.15f,math::Vec from={.2f,-.25f,-.15f},math::Vec to={.15f,-.2f,-.62f}) {
        in.grip=1;Move(from,.25f);Hold(.1f);Move(to,seconds);Hold(.1f);
    }
};
int main(){
    {   // Under the motions' gate (both grips, owner 2026-11-04): the weapon hand's grip alone no longer charges.
        Sim one;one.gated=true;one.grips[0]=1;one.Punch();Check(one.casts==0,"gated, the weapon hand's grip alone no longer charges");
        Sim both;both.gated=true;both.grips[0]=both.grips[1]=1;both.Punch();Check(both.casts==1,"gated, both grips held: the punch charges");
    }
    for(float heading:{-2.f,0.f,2.f}){Sim s(heading);s.Punch();Check(s.casts==1,"a fist punched straight out ahead casts Charge at any heading");}
    {Sim s;s.Punch();s.Hold(1);Check(s.casts==1,"an arm held out never repeats");
     s.Punch();Check(s.casts==2,"drawing the fist back in loads the next punch");}
    {Sim s;s.Move({.2f,-.25f,-.15f},.25f);s.Hold(.1f);s.Move({.15f,-.2f,-.62f},.15f);Check(s.casts==0,"an open hand punching is a swing, not Charge");}
    {Sim s;s.in.busy=true;s.Punch();Check(s.casts==0,"a busy hand (trigger squeezed, bottle) never charges");}
    {Sim s;s.Punch(.6f);Check(s.casts==0,"a slow reach is not a punch");}
    {Sim s;s.Punch(.06f,{.2f,-.25f,-.15f},{.15f,-.2f,-.34f});Check(s.casts==0,"a half punch does not cast");
     Check(s.drops==1&&s.lastDrop.find("stalled")!=std::string::npos,"the half punch logs once");}
    {Sim s;s.Punch(.15f,{.2f,-.25f,-.15f},{.2f,.35f,-.35f});Check(s.casts==0,"punching up to the sky is the hammer, not Charge");}
    {Sim s;s.Punch(.15f,{.2f,.0f,-.15f},{.2f,-.7f,-.45f});Check(s.casts==0,"a downward chop is not a punch");}
    {Sim s;s.Punch(.15f,{.35f,-.25f,-.2f},{-.35f,-.25f,-.5f});Check(s.casts==0,"a horizontal slash across the body is not a punch");}
    {Sim s;s.in.grip=1;s.Move({.15f,-.2f,-.62f},.8f);s.Hold(.2f);s.Move({.15f,-.2f,-.4f},.2f);s.Move({.15f,-.2f,-.65f},.1f);
     Check(s.casts==0,"jabbing from an arm already out never loads");}
    {Sim s;s.in.grip=1;s.Move({.2f,-.25f,-.15f},.25f);s.Hold(.1f);s.Move({.17f,-.22f,-.35f},.06f);s.in.grip=0;s.Move({.15f,-.2f,-.62f},.08f);
     Check(s.casts==0&&s.lastDrop.find("opened")!=std::string::npos,"opening the fist mid-punch abandons it");}
    {Sim tall(0,2.1f);tall.Punch(.15f,{.2f,-.25f,-.15f},{.15f,-.2f,-.55f});Check(tall.casts==0,"a tall player's half extension is not enough");
     tall.Punch(.15f,{.2f,-.25f,-.15f},{.15f,-.2f,-.66f});Check(tall.casts==1,"a tall player's full extension casts");
     Sim small(0,1.3f);small.Punch(.15f,{.2f,-.25f,-.1f},{.15f,-.2f,-.42f});Check(small.casts==1,"a small player's full extension casts");}
    {Sim s;s.Punch();s.in.session=8;s.Hold(.1f);s.Punch();Check(s.casts==2,"a new session starts clean");}
    // Real arm motion (minimum-jerk, 90 Hz): travel counts from the drawn-back point.
    for(float heading:{-2.f,0.f,2.f}){Sim s(heading);s.Natural();s.Punch(.25f);Check(s.casts==1,"a natural punch casts Charge");}
    {Sim s;s.Natural();s.Punch(.25f,{.2f,-.25f,-.28f},{.15f,-.2f,-.62f});
     Check(s.casts==1,"a natural punch from the edge of the drawn-back zone casts (it leaves the zone before full speed)");}
    {Sim s;s.Natural();for(int i=0;i<5;++i)s.Punch(.25f);Check(s.casts==5,"five natural punches, five charges");}
    {Sim s;s.Natural();s.Punch(.25f,{.2f,-.25f,-.15f},{.15f,-.2f,-.38f});Check(s.casts==0,"a natural half punch does not cast");}
    {Sim s;s.Natural();s.Punch(.9f);Check(s.casts==0,"a natural slow reach is not a punch");}
    {Sim tall(0,2.1f);tall.Natural();tall.Punch(.28f,{.2f,-.25f,-.15f},{.15f,-.2f,-.66f});Check(tall.casts==1,"a tall player's natural full punch casts");}
    std::puts("charge gesture tests passed");
}
