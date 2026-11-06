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
// The Necromancer's motions, done well, from the resting pose (head-local: +X right, +Y up, -Z forward).
static void BoneArmor(Sim& s){s.Move({-.10f,-.35f,-.10f},{.10f,-.35f,-.10f},.4f);s.Hold(.6f);}
static void RaiseSkeleton(Sim& s){s.Move({.15f,-.68f,-.20f},{-.15f,-.68f,-.20f},.4f);s.Hold(.3f);s.Move({.15f,-.36f,-.25f},{-.15f,-.36f,-.25f},.8f);s.Hold(.2f);}
static void Point(Sim& s){s.MoveR({.15f,-.30f,-.45f},.35f);s.Hold(.35f);}
static void CorpseExplosion(Sim& s){Point(s);s.MoveR({.15f,-.30f,-.62f},.15f);s.Hold(.2f);}
static void Golem(Sim& s){s.Move({.18f,-.50f,-.30f},{-.18f,-.50f,-.30f},.35f);s.Move({.18f,-.82f,-.30f},{-.18f,-.82f,-.30f},.3f);
    s.Move({.18f,-.55f,-.30f},{-.18f,-.55f,-.30f},.3f);s.Hold(.2f);}
static void AmplifyDamage(Sim& s){Point(s);s.MoveR({.15f,-.30f,-.25f},.15f);s.Hold(.2f);}
static void LowerResist(Sim& s){Point(s);s.MoveR({.15f,-.58f,-.43f},.6f);s.Hold(.2f);}
static void PoisonNova(Sim& s){s.Move({.08f,-.38f,-.25f},{-.08f,-.38f,-.25f},.35f);s.Move({.55f,-.60f,-.20f},{-.55f,-.60f,-.20f},.25f);s.Hold(.2f);}
static void BonePrison(Sim& s){s.Move({.35f,-.30f,-.40f},{-.35f,-.30f,-.40f},.4f);s.Hold(.1f);s.Move({.06f,-.30f,-.42f},{-.06f,-.30f,-.42f},.3f);s.Hold(.2f);}
// Things a Necromancer does with both grips held that are none of them.
static void Rest(Sim& s){s.Hold(2.f);}
static void Walk(Sim& s){s.Drive(3.f,[](float t,math::Vec& r,math::Vec& l){const float w=std::sin(t*6.2832f);
    r={.25f,-.6f+.03f*w*w,-.15f-.2f*w};l={-.25f,-.6f+.03f*w*w,-.15f+.2f*w};});}
static void BeltReach(Sim& s){s.MoveR({.22f,-.78f,.05f},.4f);s.Hold(.2f);s.MoveR(RestR,.4f);s.MoveL({-.22f,-.78f,.05f},.4f);s.Hold(.2f);s.MoveL(RestL,.4f);s.Hold(.3f);}
static void WandFlick(Sim& s){s.MoveR({.20f,-.40f,-.35f},.25f);s.MoveR({.20f,-.38f,-.55f},.12f);s.MoveR(RestR,.3f);s.Hold(.3f);}
// Lazy versions of the motions that must be quick, and a quick one of the one that must be slow.
static void SlowGolem(Sim& s){s.Move({.18f,-.50f,-.30f},{-.18f,-.50f,-.30f},.35f);s.Move({.18f,-.82f,-.30f},{-.18f,-.82f,-.30f},1.2f);
    s.Move({.18f,-.55f,-.30f},{-.18f,-.55f,-.30f},1.2f);s.Hold(.2f);}
static void SlowNova(Sim& s){s.Move({.08f,-.38f,-.25f},{-.08f,-.38f,-.25f},.35f);s.Move({.55f,-.60f,-.20f},{-.55f,-.60f,-.20f},2.f);s.Hold(.2f);}
static void SlowJab(Sim& s){Point(s);s.MoveR({.15f,-.30f,-.62f},.6f);s.Hold(.2f);}
static void SlowYank(Sim& s){Point(s);s.MoveR({.15f,-.30f,-.25f},.6f);s.Hold(.2f);}
static void QuickDrop(Sim& s){Point(s);s.MoveR({.15f,-.58f,-.43f},.15f);s.Hold(.4f);}
static void SlowPrison(Sim& s){s.Move({.35f,-.30f,-.40f},{-.35f,-.30f,-.40f},.4f);s.Hold(.1f);s.Move({.06f,-.30f,-.42f},{-.06f,-.30f,-.42f},1.5f);s.Hold(.2f);}
static void QuickRaise(Sim& s){s.Move({.15f,-.68f,-.20f},{-.15f,-.68f,-.20f},.4f);s.Hold(.3f);s.Move({.15f,-.36f,-.25f},{-.15f,-.36f,-.25f},.25f);s.Hold(.2f);}
static void ShortArmor(Sim& s){s.Move({-.10f,-.35f,-.10f},{.10f,-.35f,-.10f},.4f);s.Hold(.2f);s.Move(RestR,RestL,.4f);}
static void PointBack(Sim& s){Point(s);s.MoveR(RestR,.5f);s.Hold(.3f);}   // held out, then lowered back to rest
int main() {
    using Do=void(*)(Sim&);
    const struct {Motion motion;Do perform;const char* name;bool aimed;} all[]={
        {Motion::BoneArmor,BoneArmor,"Bone Armor",false},{Motion::RaiseSkeleton,RaiseSkeleton,"Raise Skeleton",false},
        {Motion::CorpseExplosion,CorpseExplosion,"Corpse Explosion",false},{Motion::Golem,Golem,"Golem",true},
        {Motion::AmplifyDamage,AmplifyDamage,"Amplify Damage",false},{Motion::LowerResist,LowerResist,"Lower Resist",false},
        {Motion::PoisonNova,PoisonNova,"Poison Nova",false},{Motion::BonePrison,BonePrison,"Bone Prison",false}};
    Check(std::size(all)==std::size(spellmotion::necromancer::Order),"every motion of the Necromancer's is tested");
    for(const auto& m:all) {
        const std::string label=std::string(m.name)+": ";
        Check(std::string(spellmotion::Cue(m.motion))!="no motion",(label+"it has a cue").c_str());
        for(float heading:{-2.f,0.f,2.f}) {
            Sim s(m.motion,heading);m.perform(s);
            Check(s.casts==1,(label+"done well, it casts once, at any heading").c_str());
            if(m.aimed)Check(s.aim.z<-.99f&&std::fabs(s.aim.x)<.01f&&std::fabs(s.aim.y)<.01f,(label+"it goes to the spot ahead, level").c_str());
            else Check(s.aim.x==0&&s.aim.y==0&&s.aim.z==0,(label+"no way of its own: the right hand's ray aims it").c_str());
        }
        {Sim s(m.motion);s.in.active=false;m.perform(s);Check(s.casts==0,(label+"with the gate closed, nothing casts").c_str());}
        for(const auto& other:all)if(other.motion!=m.motion) {
            Sim s(m.motion);other.perform(s);
            Check(s.casts==0,(label+"another motion ("+other.name+") does not cast it").c_str());
        }
        for(const auto& none:{Rest,Walk,BeltReach,WandFlick,PointBack}) {
            Sim s(m.motion);none(s);
            Check(s.casts==0,(label+"resting, walking, a belt reach, a wand flick or lowering the arm casts nothing").c_str());
        }
    }
    const struct {Motion motion;Do perform;const char* label;} lazy[]={
        {Motion::Golem,SlowGolem,"Golem: a slow press and lift casts nothing"},
        {Motion::PoisonNova,SlowNova,"Poison Nova: the arms spread slowly cast nothing"},
        {Motion::CorpseExplosion,SlowJab,"Corpse Explosion: a slow push casts nothing"},
        {Motion::AmplifyDamage,SlowYank,"Amplify Damage: a slow pull back casts nothing"},
        {Motion::LowerResist,QuickDrop,"Lower Resist: a quick drop casts nothing"},
        {Motion::BonePrison,SlowPrison,"Bone Prison: the hands closed slowly cast nothing"},
        {Motion::RaiseSkeleton,QuickRaise,"Raise Skeleton: a quick lift casts nothing"},
        {Motion::BoneArmor,ShortArmor,"Bone Armor: crossed for a moment casts nothing"}};
    for(const auto& l:lazy){Sim s(l.motion);l.perform(s);Check(s.casts==0,l.label);}
    std::puts("motions-necromancer-test: ok");
    return 0;
}
