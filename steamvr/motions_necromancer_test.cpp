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
static void RaiseSkeleton(Sim& s){s.Move({.18f,-.62f,-.15f},{-.18f,-.62f,-.15f},.3f);s.Move({.22f,.18f,-.22f},{-.22f,.18f,-.22f},.3f);s.Hold(.2f);}
static void Point(Sim& s){s.MoveR({.15f,-.30f,-.45f},.35f);s.Hold(.35f);}
static void CorpseExplosion(Sim& s){Point(s);s.MoveR({.15f,-.30f,-.62f},.15f);s.Hold(.2f);}
static void Golem(Sim& s){s.MoveR({.22f,.28f,-.05f},.3f);s.Hold(.2f);}
static void GolemLeft(Sim& s){s.MoveL({-.22f,.28f,-.05f},.3f);s.Hold(.2f);}
static void AmplifyDamage(Sim& s){s.MoveR({.15f,-.30f,-.08f},.35f);s.Hold(.1f);s.MoveR({.15f,-.28f,-.55f},.15f);s.Hold(.2f);}
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
static void SlowGolem(Sim& s){s.MoveR({.22f,.28f,-.05f},1.4f);s.Hold(.2f);}
static void SlowNova(Sim& s){s.Move({.08f,-.38f,-.25f},{-.08f,-.38f,-.25f},.35f);s.Move({.55f,-.60f,-.20f},{-.55f,-.60f,-.20f},2.f);s.Hold(.2f);}
static void SlowJab(Sim& s){Point(s);s.MoveR({.15f,-.30f,-.62f},.6f);s.Hold(.2f);}
static void SlowPunch(Sim& s){s.MoveR({.15f,-.30f,-.08f},.35f);s.Hold(.1f);s.MoveR({.15f,-.28f,-.55f},.8f);s.Hold(.2f);}
static void SlowDrop(Sim& s){Point(s);s.MoveR({.15f,-.58f,-.43f},.9f);s.Hold(.4f);}
static void SlowPrison(Sim& s){s.Move({.35f,-.30f,-.40f},{-.35f,-.30f,-.40f},.4f);s.Hold(.1f);s.Move({.06f,-.30f,-.42f},{-.06f,-.30f,-.42f},1.5f);s.Hold(.2f);}
static void SlowRaise(Sim& s){s.Move({.18f,-.62f,-.15f},{-.18f,-.62f,-.15f},.3f);s.Move({.22f,.18f,-.22f},{-.22f,.18f,-.22f},1.4f);s.Hold(.2f);}
static void PointHigh(Sim& s){s.MoveR({.18f,.10f,-.45f},.3f);s.Hold(.3f);}   // Lower Resist's start, raised out in front
static void ShortArmor(Sim& s){s.Move({-.10f,-.35f,-.10f},{.10f,-.35f,-.10f},.4f);s.Hold(.2f);s.Move(RestR,RestL,.4f);}
static void PointBack(Sim& s){Point(s);s.MoveR(RestR,.5f);s.Hold(.3f);}   // held out, then lowered back to rest
int main() {
    using Do=void(*)(Sim&);
    const struct {Motion motion;Do perform;const char* name;bool aimed;} all[]={
        {Motion::BoneArmor,BoneArmor,"Bone Armor",false},{Motion::RaiseSkeleton,RaiseSkeleton,"Raise Skeleton",false},
        {Motion::CorpseExplosion,CorpseExplosion,"Corpse Explosion",false},{Motion::Golem,Golem,"Golem",true},
        {Motion::GolemLeft,GolemLeft,"Golem (left arm)",true},
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
            // a wand flicked forward is a punch: Amplify Damage's since the owner made it one (2026-11-05), its
            // grips both held
            if(m.motion==Motion::AmplifyDamage&&none==WandFlick)continue;
            Sim s(m.motion);none(s);
            Check(s.casts==0,(label+"resting, walking, a belt reach, a wand flick or lowering the arm casts nothing").c_str());
        }
    }
    const struct {Motion motion;Do perform;const char* label;} lazy[]={
        {Motion::Golem,SlowGolem,"Golem: the arm raised slowly casts nothing"},
        {Motion::Golem,PointHigh,"Golem: the right hand raised to point out in front (Lower Resist) casts nothing"},
        {Motion::PoisonNova,SlowNova,"Poison Nova: the arms spread slowly cast nothing"},
        {Motion::CorpseExplosion,SlowJab,"Corpse Explosion: a slow push casts nothing"},
        {Motion::AmplifyDamage,SlowPunch,"Amplify Damage: a slow push out casts nothing"},
        {Motion::LowerResist,SlowDrop,"Lower Resist: the hand lowered slowly casts nothing"},
        {Motion::BonePrison,SlowPrison,"Bone Prison: the hands closed slowly cast nothing"},
        {Motion::RaiseSkeleton,SlowRaise,"Raise Skeleton: a slow lift casts nothing"},
        {Motion::BoneArmor,ShortArmor,"Bone Armor: crossed for a moment casts nothing"}};
    for(const auto& l:lazy){Sim s(l.motion);l.perform(s);Check(s.casts==0,l.label);}
    {   // the owner's headset session (2026-11-05): what was missed then casts now, and their calls
        Sim nova(Motion::PoisonNova);nova.Move({.08f,-.38f,-.25f},{-.08f,-.38f,-.25f},.35f);nova.Move({.70f,-.15f,-.10f},{-.68f,-.12f,-.15f},.25f);nova.Hold(.2f);
        Check(nova.casts==1,"Poison Nova: flung 138 cm wide at shoulder height casts");
        Sim chop(Motion::LowerResist);chop.MoveR({.18f,.08f,-.45f},.35f);chop.Hold(.3f);chop.MoveR({.18f,-.55f,-.30f},.2f);chop.Hold(.2f);
        Check(chop.casts==1,"Lower Resist: a big chop from a hand raised over the eyes, arcing in, casts");
        Sim wide(Motion::RaiseSkeleton);wide.Move({.30f,-.62f,-.10f},{-.30f,-.62f,-.10f},.3f);wide.Move({.45f,.25f,-.15f},{-.45f,.25f,-.15f},.3f);
        Check(wide.casts==1,"Raise Skeleton: both arms swept up wide past the face cast, no stop");
        Sim on(Motion::RaiseSkeleton);on.Move({.18f,-.62f,-.15f},{-.18f,-.62f,-.15f},.3f);on.Move({.18f,.40f,-.10f},{-.18f,.40f,-.10f},.3f);on.Move({.18f,-.62f,-.15f},{-.18f,-.62f,-.15f},.3f);
        Check(on.casts==1,"Raise Skeleton: swept on overhead and back down, once");
        for(const auto m:{Motion::Golem,Motion::GolemLeft}){Sim both(m);RaiseSkeleton(both);Check(both.casts==0,"a golem: both arms up is Raise Skeleton's");}
        {Sim one(Motion::RaiseSkeleton);Golem(one);GolemLeft(one);Check(one.casts==0,"Raise Skeleton: one arm up, then the other, is no Raise Skeleton");}
        {Sim left(Motion::Golem);GolemLeft(left);Check(left.casts==0,"the right arm's golem: the left arm up is the other card's");}
        Sim punch(Motion::AmplifyDamage);punch.MoveR({.20f,-.20f,-.05f},.35f);punch.MoveR({.18f,-.18f,-.50f},.15f);punch.Hold(.2f);
        Check(punch.casts==1,"Amplify Damage: a punch straight from the chest, no pause, casts");
        Sim jab(Motion::CorpseExplosion);AmplifyDamage(jab);Check(jab.casts==0,"Corpse Explosion: the punch from the chest is not its jab");
        Sim held(Motion::AmplifyDamage);CorpseExplosion(held);Check(held.casts==0,"Amplify Damage: the jab from the arm held out is not its punch");
    }
    {   // nothing low (a tester, 2026-11-05: "Please no floor stuff"), and the golem's spot ahead
        for(const auto m:{Motion::RaiseSkeleton,Motion::Golem,Motion::GolemLeft}) {
            Sim s(m);(m==Motion::Golem?Golem:m==Motion::GolemLeft?GolemLeft:RaiseSkeleton)(s);
            Check(s.casts==1&&s.g.Stats().low>-.65f,"Raise Skeleton and the golems: cast with the hands never below the waist");
        }
        Sim g(Motion::Golem);bool land=false;
        for(int i=0;i<40&&!g.casts;++i){g.MoveR({.22f,-.62f+.90f*float(i+1)/40,-.15f+.10f*float(i+1)/40},.3f/40);
            if(g.out.cast)land=g.out.land==spellmotion::Land::Spot&&g.out.spot.ahead==spellmotion::necromancer::tuning.golemAhead&&g.out.spot.right==0;}
        Check(g.casts==1&&land,"Golem: it stands on its spot ahead");
    }
    std::puts("motions-necromancer-test: ok");
    return 0;
}
