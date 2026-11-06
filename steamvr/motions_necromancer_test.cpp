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
static void RaiseSkeleton(Sim& s){s.Move({.15f,-.62f,-.20f},{-.15f,-.62f,-.20f},.3f);s.Move({.15f,-.25f,-.28f},{-.15f,-.25f,-.28f},.3f);s.Hold(.2f);}
static void Point(Sim& s){s.MoveR({.15f,-.30f,-.45f},.35f);s.Hold(.35f);}
static void CorpseExplosion(Sim& s){Point(s);s.MoveR({.15f,-.30f,-.62f},.15f);s.Hold(.2f);}
static void Golem(Sim& s){s.Move({.15f,.20f,-.15f},{-.15f,.20f,-.15f},.45f);s.Move({.12f,-.45f,-.40f},{-.12f,-.45f,-.40f},.25f);s.Hold(.2f);}
static void AmplifyDamage(Sim& s){Point(s);s.MoveR({.15f,-.30f,-.25f},.15f);s.Hold(.2f);}
static void LowerResist(Sim& s){Point(s);s.MoveR({.15f,-.55f,-.43f},.18f);s.Hold(.2f);}
static void PoisonNova(Sim& s){s.Move({.08f,-.38f,-.25f},{-.08f,-.38f,-.25f},.35f);s.Move({.55f,-.60f,-.20f},{-.55f,-.60f,-.20f},.25f);s.Hold(.2f);}
static void BonePrison(Sim& s){s.Move({.35f,-.30f,-.40f},{-.35f,-.30f,-.40f},.4f);s.Hold(.1f);s.Move({.06f,-.30f,-.42f},{-.06f,-.30f,-.42f},.3f);s.Hold(.2f);}
// Things a Necromancer does with both grips held that are none of them.
static void Rest(Sim& s){s.Hold(2.f);}
static void Walk(Sim& s){s.Drive(3.f,[](float t,math::Vec& r,math::Vec& l){const float w=std::sin(t*6.2832f);
    r={.25f,-.6f+.03f*w*w,-.15f-.2f*w};l={-.25f,-.6f+.03f*w*w,-.15f+.2f*w};});}
static void BeltReach(Sim& s){s.MoveR({.22f,-.78f,.05f},.4f);s.Hold(.2f);s.MoveR(RestR,.4f);s.MoveL({-.22f,-.78f,.05f},.4f);s.Hold(.2f);s.MoveL(RestL,.4f);s.Hold(.3f);}
static void WandFlick(Sim& s){s.MoveR({.20f,-.40f,-.35f},.25f);s.MoveR({.20f,-.38f,-.55f},.12f);s.MoveR(RestR,.3f);s.Hold(.3f);}
// Lazy versions of the motions, every one of which must be quick (a tester, 2026-11-05: "All motions should be
// quick and repeatable sequences that don't need you to move slow").
static void SlowGolem(Sim& s){s.Move({.15f,.20f,-.15f},{-.15f,.20f,-.15f},.45f);s.Move({.12f,-.45f,-.40f},{-.12f,-.45f,-.40f},1.2f);s.Hold(.2f);}
static void SlowNova(Sim& s){s.Move({.08f,-.38f,-.25f},{-.08f,-.38f,-.25f},.35f);s.Move({.55f,-.60f,-.20f},{-.55f,-.60f,-.20f},2.f);s.Hold(.2f);}
static void SlowJab(Sim& s){Point(s);s.MoveR({.15f,-.30f,-.62f},.6f);s.Hold(.2f);}
static void SlowYank(Sim& s){Point(s);s.MoveR({.15f,-.30f,-.25f},.6f);s.Hold(.2f);}
static void SlowDrop(Sim& s){Point(s);s.MoveR({.15f,-.58f,-.43f},.9f);s.Hold(.4f);}
static void SlowPrison(Sim& s){s.Move({.35f,-.30f,-.40f},{-.35f,-.30f,-.40f},.4f);s.Hold(.1f);s.Move({.06f,-.30f,-.42f},{-.06f,-.30f,-.42f},1.5f);s.Hold(.2f);}
static void SlowRaise(Sim& s){s.Move({.15f,-.62f,-.20f},{-.15f,-.62f,-.20f},.3f);s.Move({.15f,-.25f,-.28f},{-.15f,-.25f,-.28f},1.2f);s.Hold(.2f);}
static void ArmsOverhead(Sim& s){s.Move({.15f,.20f,-.15f},{-.15f,.20f,-.15f},.45f);s.Hold(.3f);}   // past the chest without stopping
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
        {Motion::Golem,SlowGolem,"Golem: the fists lowered slowly from overhead cast nothing"},
        {Motion::PoisonNova,SlowNova,"Poison Nova: the arms spread slowly cast nothing"},
        {Motion::CorpseExplosion,SlowJab,"Corpse Explosion: a slow push casts nothing"},
        {Motion::AmplifyDamage,SlowYank,"Amplify Damage: a slow pull back casts nothing"},
        {Motion::LowerResist,SlowDrop,"Lower Resist: the hand lowered slowly casts nothing"},
        {Motion::BonePrison,SlowPrison,"Bone Prison: the hands closed slowly cast nothing"},
        {Motion::RaiseSkeleton,SlowRaise,"Raise Skeleton: a slow lift casts nothing"},
        {Motion::RaiseSkeleton,ArmsOverhead,"Raise Skeleton: the arms raised on overhead (a golem's start) cast nothing"},
        {Motion::BoneArmor,ShortArmor,"Bone Armor: crossed for a moment casts nothing"}};
    for(const auto& l:lazy){Sim s(l.motion);l.perform(s);Check(s.casts==0,l.label);}
    {   // the owner's headset session (2026-11-05): what was missed then casts now
        Sim wide(Motion::RaiseSkeleton);wide.Move({.29f,-.62f,-.15f},{-.29f,-.62f,-.15f},.3f);wide.Move({.29f,-.22f,-.10f},{-.29f,-.22f,-.10f},.3f);wide.Hold(.2f);
        Check(wide.casts==1,"Raise Skeleton: lifted 58 cm apart casts");
        Sim high(Motion::RaiseSkeleton);high.Move({.20f,-.62f,-.15f},{-.20f,-.62f,-.15f},.3f);high.Move({.24f,0,-.05f},{-.24f,0,-.05f},.3f);high.Hold(.2f);
        Check(high.casts==1,"Raise Skeleton: lifted to eye level casts");
        Sim close(Motion::RaiseSkeleton);close.Move({.10f,-.62f,-.15f},{-.10f,-.62f,-.15f},.3f);close.Move({.08f,-.25f,-.15f},{.09f,-.25f,-.15f},.3f);close.Hold(.2f);
        Check(close.casts==1,"Raise Skeleton: the hands overlapping a centimetre cast");
        Sim crossed(Motion::RaiseSkeleton);crossed.Move({.10f,-.62f,-.15f},{-.10f,-.62f,-.15f},.3f);crossed.Move({-.10f,-.30f,-.15f},{.10f,-.30f,-.15f},.3f);crossed.Hold(.2f);
        Check(crossed.casts==0,"Raise Skeleton: crossed is Bone Armor's, not it");
        Sim nova(Motion::PoisonNova);nova.Move({.08f,-.38f,-.25f},{-.08f,-.38f,-.25f},.35f);nova.Move({.70f,-.15f,-.10f},{-.68f,-.12f,-.15f},.25f);nova.Hold(.2f);
        Check(nova.casts==1,"Poison Nova: flung 138 cm wide at shoulder height casts");
        Sim side(Motion::Golem);side.Move({.15f,.20f,-.15f},{-.15f,.20f,-.15f},.45f);side.Move({.36f,-.75f,.29f},{-.36f,-.75f,.16f},.25f);side.Hold(.2f);
        Check(side.casts==1,"Golem: slammed down to the sides casts");
        Sim chop(Motion::LowerResist);chop.MoveR({.18f,.08f,-.45f},.35f);chop.Hold(.3f);chop.MoveR({.18f,-.55f,-.30f},.2f);chop.Hold(.2f);
        Check(chop.casts==1,"Lower Resist: a big chop from a hand raised over the eyes, arcing in, casts");
    }
    {   // nothing low (a tester, 2026-11-05: "Please no floor stuff"), and the golem's spot ahead
        for(const auto m:{Motion::RaiseSkeleton,Motion::Golem}) {
            Sim s(m);(m==Motion::Golem?Golem:RaiseSkeleton)(s);
            Check(s.casts==1&&s.g.Stats().low>-.65f,"Raise Skeleton and Golem: cast with the hands never below the waist");
        }
        Sim g(Motion::Golem);g.Move({.15f,.20f,-.15f},{-.15f,.20f,-.15f},.45f);
        bool land=false;
        for(int i=0;i<40&&!g.casts;++i){g.Move({.12f,.20f-.65f*float(i+1)/40,-.15f-.25f*float(i+1)/40},{-.12f,.20f-.65f*float(i+1)/40,-.15f-.25f*float(i+1)/40},.25f/40);
            if(g.out.cast)land=g.out.land==spellmotion::Land::Spot&&g.out.spot.ahead==spellmotion::necromancer::tuning.golemAhead&&g.out.spot.right==0;}
        Check(g.casts==1&&land,"Golem: it stands on its spot ahead");
    }
    std::puts("motions-necromancer-test: ok");
    return 0;
}
