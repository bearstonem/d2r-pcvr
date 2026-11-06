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
// The Amazon's motions done well, from the resting pose (head-local: +X right, +Y up, -Z forward).
static void InnerSight(Sim& s){s.MoveR({.06f,.04f,-.15f},.35f);s.Hold(.15f);s.MoveR({.10f,0,-.58f},.25f);}
static void SlowMissiles(Sim& s){s.MoveR({.12f,-.32f,-.18f},.35f);s.MoveR({.15f,-.28f,-.60f},.25f);s.Hold(.5f);}
static void Decoy(Sim& s){s.Move({.08f,-.38f,-.25f},{-.08f,-.38f,-.25f},.35f);s.Move({.55f,-.36f,-.30f},{.30f,-.36f,-.30f},.30f);}
static void Valkyrie(Sim& s){s.MoveR({.15f,.32f,-.10f},.40f);s.MoveR({.20f,-.22f,-.55f},.30f);}
// The same paths, lazily: slow sweeps and lowering.
static void LazyInnerSight(Sim& s){s.MoveR({.06f,.04f,-.15f},.35f);s.Hold(.15f);s.MoveR({.10f,0,-.58f},2.f);}
static void LazySlowMissiles(Sim& s){s.MoveR({.12f,-.32f,-.18f},.35f);s.MoveR({.15f,-.28f,-.60f},2.f);s.Hold(.6f);}
static void LazyDecoy(Sim& s){s.Move({.08f,-.38f,-.25f},{-.08f,-.38f,-.25f},.35f);s.Move({.55f,-.36f,-.30f},{.30f,-.36f,-.30f},2.f);}
static void LazyValkyrie(Sim& s){s.MoveR({.15f,.32f,-.10f},.40f);s.MoveR({.20f,-.22f,-.55f},2.f);}
int main() {
    using Do=void(*)(Sim&);
    const struct {Motion motion;Do perform,lazy;const char* name;} all[]={
        {Motion::InnerSight,InnerSight,LazyInnerSight,"Inner Sight"},{Motion::SlowMissiles,SlowMissiles,LazySlowMissiles,"Slow Missiles"},
        {Motion::Decoy,Decoy,LazyDecoy,"Decoy"},{Motion::Valkyrie,Valkyrie,LazyValkyrie,"Valkyrie"}};
    Check(std::size(spellmotion::amazon::Order)==std::size(all),"every Amazon motion is watched");
    for(const auto& m:all) {
        const std::string label=std::string(m.name)+": ";
        Check(std::string(spellmotion::Cue(m.motion))!="no motion",(label+"has a cue").c_str());
        for(float heading:{-2.f,0.f,2.f}){Sim s(m.motion,heading);m.perform(s);
            Check(s.casts==1,(label+"done well, it casts once at any heading").c_str());}
        {Sim s(m.motion);s.in.active=false;m.perform(s);Check(s.casts==0,(label+"the gate closed (in.active false): nothing").c_str());}
        {Sim s(m.motion);m.lazy(s);Check(s.casts==0,(label+"done lazily, nothing").c_str());}
        for(const auto& other:all)if(other.motion!=m.motion) {
            Sim s(m.motion);other.perform(s);
            Check(s.casts==0,(label+"another Amazon motion ("+other.name+") casts nothing").c_str());
        }
        // Not motions: resting, walking with the arms swinging, a belt potion at the hip, drinking it.
        {Sim s(m.motion);s.Hold(2.f);Check(s.casts==0,(label+"resting hands: nothing").c_str());}
        {Sim s(m.motion);s.Drive(3.f,[](float t,math::Vec& r,math::Vec& l){const float w=.25f*std::sin(6.2831853f*t);
             r=RestR+math::Vec{0,0,-w};l=RestL+math::Vec{0,0,w};});
         Check(s.casts==0,(label+"walking, the arms swinging: nothing").c_str());}
        {Sim s(m.motion);s.MoveR({.30f,-.82f,-.02f},.4f);s.Hold(.3f);s.MoveR(RestR,.4f);
         Check(s.casts==0,(label+"a hand to the belt at the hip: nothing").c_str());}
        {Sim s(m.motion);s.MoveR({.30f,-.82f,-.02f},.4f);s.MoveR({.02f,-.12f,-.10f},.45f);s.Hold(.6f);s.MoveR(RestR,.45f);
         Check(s.casts==0,(label+"drinking a potion: nothing").c_str());}
    }
    // Aims: Decoy and Valkyrie go to a spot along the motion; Inner Sight and Slow Missiles set none
    // (Slow Missiles takes the right hand's own ray).
    for(float heading:{-2.f,0.f,2.f}) {
        {Sim s(Motion::InnerSight,heading);InnerSight(s);Check(s.casts==1&&math::Dot(s.aim,s.aim)==0,"Inner Sight: no aim");}
        {Sim s(Motion::SlowMissiles,heading);SlowMissiles(s);Check(s.casts==1&&math::Dot(s.aim,s.aim)==0,"Slow Missiles: no aim (the right hand's ray)");}
        {Sim s(Motion::Decoy,heading);Decoy(s);Check(s.casts==1&&s.aim.x>.2f&&std::fabs(s.aim.x)>std::fabs(s.aim.z),"Decoy: to the right, the way the hands swept");}
        {Sim s(Motion::Valkyrie,heading);Valkyrie(s);Check(s.casts==1&&s.aim.z<-.3f&&std::fabs(s.aim.x)<.15f,"Valkyrie: ahead, where the arm points");}
    }
    {Sim s(Motion::Decoy);s.Move({.08f,-.38f,-.25f},{-.08f,-.38f,-.25f},.35f);s.Move({-.30f,-.36f,-.30f},{-.55f,-.36f,-.30f},.30f);
     Check(s.casts==1&&s.aim.x<-.2f,"Decoy: to the left, the way the hands swept");}
    {Sim s(Motion::Valkyrie);s.MoveR({.15f,.32f,-.10f},.40f);s.MoveR({.55f,-.20f,-.40f},.30f);
     Check(s.casts==1&&s.aim.x>.2f&&s.aim.z<-.2f,"Valkyrie: ahead and to the right, where the arm points");}
    // A tester's first pass (2026-11-05): Decoy and Valkyrie go to a spot, Slow Missiles' push up to eye level stops a moment.
    for(float heading:{-2.f,0.f,2.f}) {
        {Sim s(Motion::Decoy,heading);Decoy(s);Check(s.land==spellmotion::Land::Spot&&s.spot.right>0&&s.spot.ahead>0,"Decoy: she stands ahead on the side pushed");}
        {Sim s(Motion::Valkyrie,heading);Valkyrie(s);Check(s.land==spellmotion::Land::Pointed,"Valkyrie: she lands where the right hand points");}
        {Sim s(Motion::InnerSight,heading);InnerSight(s);Check(s.land==spellmotion::Land::None,"Inner Sight: no spot");}
    }
    {Sim s(Motion::Decoy);s.Move({.08f,-.38f,-.25f},{-.08f,-.38f,-.25f},.35f);s.Move({-.30f,-.36f,-.30f},{-.55f,-.36f,-.30f},.30f);
     Check(s.casts==1&&s.land==spellmotion::Land::Spot&&s.spot.right<0,"Decoy: pushed left, she stands on the left");}
    {Sim s(Motion::SlowMissiles);s.MoveR({.12f,-.20f,-.18f},.35f);s.MoveR({.12f,0,-.55f},.25f);s.Hold(.25f);
     Check(s.casts==1,"Slow Missiles: pushed out at eye level toward the archers and stopped, casts");}
    {Sim s(Motion::Decoy);s.Move({.08f,-.38f,-.25f},{-.08f,-.38f,-.25f},.35f);s.Move({.55f,-.36f,-.30f},{.30f,-.36f,-.30f},.5f);
     Check(s.casts==1,"Decoy: a push to the side in half a second casts");}
    // At an unhurried pace (about 0.6 s for the stroke) each still casts.
    {Sim s(Motion::InnerSight);s.MoveR({.06f,.04f,-.15f},.5f);s.Hold(.2f);s.MoveR({.10f,0,-.58f},.6f);Check(s.casts==1,"Inner Sight: an unhurried sweep casts");}
    {Sim s(Motion::SlowMissiles);s.MoveR({.12f,-.32f,-.18f},.5f);s.MoveR({.15f,-.28f,-.60f},.6f);s.Hold(.6f);Check(s.casts==1,"Slow Missiles: an unhurried push casts");}
    {Sim s(Motion::Decoy);s.Move({.08f,-.38f,-.25f},{-.08f,-.38f,-.25f},.5f);s.Move({.55f,-.36f,-.30f},{.30f,-.36f,-.30f},.6f);Check(s.casts==1,"Decoy: an unhurried sweep casts");}
    {Sim s(Motion::Valkyrie);s.MoveR({.15f,.32f,-.10f},.5f);s.MoveR({.20f,-.22f,-.55f},.6f);Check(s.casts==1,"Valkyrie: an unhurried stroke casts");}
    // Held after the push: Slow Missiles casts once; a push without the hold, or the hold without a push, never.
    {Sim s(Motion::SlowMissiles);SlowMissiles(s);s.Hold(1.f);Check(s.casts==1,"Slow Missiles: holding on casts once");}
    {Sim s(Motion::SlowMissiles);s.MoveR({.12f,-.32f,-.18f},.35f);s.MoveR({.15f,-.28f,-.60f},.25f);s.MoveR({.12f,-.32f,-.18f},.25f);s.Hold(.5f);
     Check(s.casts==0,"Slow Missiles: pushed and drawn straight back, no hold: nothing");}
    {Sim s(Motion::SlowMissiles);s.MoveR({.15f,-.28f,-.60f},1.2f);s.Hold(1.f);
     Check(s.casts==0,"Slow Missiles: the arm raised slowly from the side and held (aiming): nothing");}
    std::puts("motions-amazon-test: ok");
    return 0;
}
