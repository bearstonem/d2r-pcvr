#include "spell_motion.h"
#include "motion_sim.h"
#include <cmath>
#include <cstdio>
#include <string>
using namespace framevr;
using spellmotion::Motion;
using motionsim::Check;
using motionsim::Sim;
using motionsim::RestR;
using motionsim::RestL;

// The Barbarian's motions (motions_barbarian.h), done well from the resting pose. Head-local points
// (+X right, +Y up from the eyes, -Z forward); hands {right, left}.
static void BattleOrders(Sim& s){s.MoveR({.03f,-.12f,-.18f},.35f);s.Hold(.2f);s.MoveR({.08f,.30f,-.10f},.25f);}
static void Shout(Sim& s){s.Move({.07f,-.12f,-.18f},{-.07f,-.12f,-.18f},.4f);s.Hold(.2f);s.Move({.10f,-.15f,-.58f},{-.10f,-.15f,-.58f},.25f);}
static void BattleCommand(Sim& s) {
    s.MoveR({.02f,-.42f,-.42f},.3f);
    for(int i=0;i<2;++i){s.MoveR({.02f,-.42f,-.10f},.14f);s.Hold(.05f);s.MoveR({.02f,-.42f,-.40f},.18f);}
}
static void WarCry(Sim& s){s.MoveR({.03f,-.12f,-.18f},.35f);s.Hold(.2f);s.MoveR({.10f,-.35f,-.62f},.25f);}
static void Howl(Sim& s){s.Move({.08f,-.40f,-.20f},{-.08f,-.40f,-.20f},.35f);s.Move({.62f,-.35f,-.15f},{-.62f,-.35f,-.15f},.3f);}
static void BattleCry(Sim& s){s.Move({.22f,-.18f,-.05f},{-.22f,-.18f,-.05f},.4f);s.Hold(.1f);s.Move({.24f,-.82f,-.05f},{-.24f,-.82f,-.05f},.3f);}
static void Leap(Sim& s){s.Move({.22f,-.65f,.22f},{-.22f,-.65f,.22f},.4f);s.Move({.22f,.30f,-.15f},{-.22f,.30f,-.15f},.35f);}
static void FindItem(Sim& s){s.MoveR({.20f,-.45f,-.45f},.35f);s.Hold(.25f);s.MoveR({.17f,-.18f,-.05f},.25f);}
// Slow, lazy versions: the same shapes over far longer.
static void SlowBattleOrders(Sim& s){s.MoveR({.03f,-.12f,-.18f},.35f);s.Hold(.2f);s.MoveR({.08f,.30f,-.10f},1.2f);}
static void SlowShout(Sim& s){s.Move({.07f,-.12f,-.18f},{-.07f,-.12f,-.18f},.4f);s.Hold(.2f);s.Move({.10f,-.15f,-.58f},{-.10f,-.15f,-.58f},1.2f);}
static void SlowBattleCommand(Sim& s) {
    s.MoveR({.02f,-.42f,-.42f},.3f);
    for(int i=0;i<2;++i){s.MoveR({.02f,-.42f,-.10f},1.1f);s.Hold(.05f);s.MoveR({.02f,-.42f,-.40f},.5f);}
}
static void SlowWarCry(Sim& s){s.MoveR({.03f,-.12f,-.18f},.35f);s.Hold(.2f);s.MoveR({.10f,-.35f,-.62f},1.2f);}
static void SlowHowl(Sim& s){s.Move({.08f,-.40f,-.20f},{-.08f,-.40f,-.20f},.35f);s.Move({.62f,-.35f,-.15f},{-.62f,-.35f,-.15f},1.5f);}
static void SlowBattleCry(Sim& s){s.Move({.22f,-.18f,-.05f},{-.22f,-.18f,-.05f},.4f);s.Hold(.1f);s.Move({.24f,-.82f,-.05f},{-.24f,-.82f,-.05f},1.5f);}
static void SlowLeap(Sim& s){s.Move({.22f,-.65f,.22f},{-.22f,-.65f,.22f},.6f);s.Move({.22f,.30f,-.15f},{-.22f,.30f,-.15f},1.5f);}
static void SlowFindItem(Sim& s){s.MoveR({.20f,-.45f,-.45f},.35f);s.Hold(.25f);s.MoveR({.17f,-.18f,-.05f},1.f);}
// Not motions: what a Barbarian's hands do all the time.
static void Walk(Sim& s) {
    s.Move({.25f,-.72f,-.05f},{-.25f,-.72f,-.05f},.3f);
    s.Drive(3.f,[](float t,math::Vec& r,math::Vec& l){const float sw=.18f*std::sin(6.2832f*t);r={.25f,-.72f+.03f*std::fabs(sw),sw};l={-.25f,-.72f+.03f*std::fabs(sw),-sw};});
}
static void BeltPotion(Sim& s){s.MoveR({.22f,-.85f,.02f},.4f);s.Hold(.3f);s.MoveR({.18f,-.30f,-.25f},.4f);s.Hold(.2f);s.MoveR(RestR,.4f);}
static void OverheadChop(Sim& s){s.MoveR({.15f,.25f,.05f},.35f);s.Hold(.05f);s.MoveR({.10f,-.55f,-.45f},.18f);s.Hold(.3f);s.MoveR(RestR,.4f);}
static void Slash(Sim& s){s.MoveR({-.35f,-.35f,-.35f},.3f);s.MoveR({.55f,-.40f,-.30f},.2f);s.Hold(.3f);s.MoveR(RestR,.4f);}
static void TwoHandedChop(Sim& s) {
    s.Move({.06f,.28f,.0f},{-.06f,.28f,.0f},.4f);s.Hold(.05f);s.Move({.06f,-.70f,-.40f},{-.06f,-.70f,-.40f},.2f);s.Hold(.3f);
    s.Move({.06f,.28f,.0f},{-.06f,.28f,.0f},.4f);s.Move({.06f,-1.05f,-.35f},{-.06f,-1.05f,-.35f},.22f);s.Hold(.25f);s.Move({.06f,-.5f,-.3f},{-.06f,-.5f,-.3f},.4f);
}
static void ShortSwings(Sim& s){for(int i=0;i<3;++i){OverheadChop(s);Slash(s);}}

int main() {
    using Do=void(*)(Sim&);
    const struct {Motion motion;Do perform,slow;int skill;bool aimed;const char* name;} all[]={
        {Motion::BattleOrders,BattleOrders,SlowBattleOrders,149,false,"Battle Orders"},{Motion::Shout,Shout,SlowShout,138,false,"Shout"},
        {Motion::BattleCommand,BattleCommand,SlowBattleCommand,155,false,"Battle Command"},{Motion::WarCry,WarCry,SlowWarCry,154,false,"War Cry"},
        {Motion::Howl,Howl,SlowHowl,130,false,"Howl"},{Motion::BattleCry,BattleCry,SlowBattleCry,146,false,"Battle Cry"},
        {Motion::Leap,Leap,SlowLeap,132,true,"Leap"},{Motion::FindItem,FindItem,SlowFindItem,142,false,"Find Item"}};
    const struct {Do perform;const char* name;} never[]={
        {[](Sim& s){s.Hold(2.f);},"resting"},{Walk,"a walking arm swing"},{BeltPotion,"a reach to the belt"},
        {OverheadChop,"an overhead chop"},{Slash,"a horizontal slash"},{TwoHandedChop,"a two-handed chop"},{ShortSwings,"a run of swings"}};
    for(const auto& m:all) {
        const std::string label=std::string(m.name)+": ";
        Check(spellmotion::For(m.skill)==m.motion&&std::string(spellmotion::Cue(m.motion))!="no motion",(label+"its skill has the motion and a cue").c_str());
        Check(std::count(std::begin(spellmotion::barbarian::Order),std::end(spellmotion::barbarian::Order),m.motion)==1,(label+"watched once").c_str());
        for(float heading:{-2.f,0.f,2.f}) {
            Sim s(m.motion,heading);m.perform(s);s.Hold(.5f);
            Check(s.casts==1,(label+"done well, it casts once, at any heading").c_str());
            const bool aim=std::fabs(s.aim.x)+std::fabs(s.aim.y)+std::fabs(s.aim.z)>0;
            Check(aim==m.aimed,(label+(m.aimed?"it aims":"it leaves the aim to the hand")).c_str());
            if(m.aimed)Check(-s.aim.z>.05f&&s.aim.y==0,(label+"aimed ahead and level").c_str());
        }
        {Sim s(m.motion);s.in.active=false;m.perform(s);Check(s.casts==0,(label+"with the gate closed, nothing").c_str());}
        {Sim s(m.motion);m.slow(s);s.Hold(.5f);Check(s.casts==0,(label+"slow and lazy, nothing").c_str());}
        for(const auto& o:all)if(o.motion!=m.motion) {
            Sim s(m.motion);o.perform(s);s.Hold(.5f);
            Check(s.casts==0,(label+"another motion ("+o.name+") does not cast it").c_str());
        }
        for(const auto& x:never) {
            Sim s(m.motion);x.perform(s);s.Hold(.3f);
            Check(s.casts==0,(label+x.name+" casts nothing").c_str());
        }
    }
    {   // Leap: the swing's way, here a little to the right of ahead
        Sim s(Motion::Leap);s.Move({.10f,-.65f,.22f},{-.34f,-.65f,.22f},.4f);s.Move({.34f,.30f,-.15f},{-.10f,.30f,-.15f},.35f);
        Check(s.casts==1&&s.aim.x>.05f&&-s.aim.z>.05f,"Leap: it goes the way the arms swung");
        Sim up(Motion::Leap);up.Move({.22f,-.65f,.05f},{-.22f,-.65f,.05f},.4f);up.Hold(.1f);
        up.Move({.22f,-.70f,.10f},{-.22f,-.70f,.10f},.15f);up.Move({.22f,.30f,.12f},{-.22f,.30f,.12f},.35f);
        Check(up.casts==1&&std::fabs(up.aim.x)<1e-6f&&up.aim.z==-1,"Leap: swung straight up, straight ahead");
    }
    {   // Battle Command: one beat alone is not two; two beats too far apart are not either
        Sim one(Motion::BattleCommand);one.MoveR({.02f,-.42f,-.42f},.3f);one.MoveR({.02f,-.42f,-.10f},.14f);one.Hold(1.f);
        Check(one.casts==0,"Battle Command: one beat is not a command");
        Sim apart(Motion::BattleCommand);apart.MoveR({.02f,-.42f,-.42f},.3f);
        apart.MoveR({.02f,-.42f,-.10f},.14f);apart.MoveR({.02f,-.42f,-.40f},.18f);apart.Hold(1.2f);apart.MoveR({.02f,-.42f,-.10f},.14f);apart.Hold(.3f);
        Check(apart.casts==0,"Battle Command: beats more than beatWindow apart are not");
        Sim lean(Motion::BattleCommand);lean.MoveR({.02f,-.42f,-.10f},.6f);lean.Hold(1.f);
        Check(lean.casts==0,"Battle Command: a fist laid on the chest is no beat");
    }
    {   // the warcries' wind-up: a fist swung past the mouth without stopping starts none
        for(const auto m:{Motion::BattleOrders,Motion::WarCry}) {
            Sim s(m);s.MoveR({.03f,-.12f,-.18f},.18f);s.MoveR(m==Motion::WarCry?math::Vec{.10f,-.35f,-.62f}:math::Vec{.08f,.30f,-.10f},.2f);s.Hold(.3f);
            Check(s.casts==0,"a fist swung through the mouth, no rest there: no warcry");
        }
        Sim both(Motion::BattleOrders);both.Move({.03f,-.12f,-.18f},RestL,.35f);both.Hold(.2f);both.Move({.08f,.30f,-.10f},{-.2f,.25f,-.1f},.25f);
        Check(both.casts==0,"Battle Orders: both hands up is no Battle Orders");
    }
    {   // the order: Shout before War Cry, Leap before Battle Orders
        const auto& o=spellmotion::barbarian::Order;
        const auto at=[&](Motion m){return std::find(std::begin(o),std::end(o),m)-std::begin(o);};
        Check(at(Motion::Shout)<at(Motion::WarCry)&&at(Motion::Leap)<at(Motion::BattleOrders),"Shout before War Cry, Leap before Battle Orders");
        Check(spellmotion::OrderOf(4).count==8&&spellmotion::PageOf(4).count==8,"the Barbarian's page: eight cards, each watched");
    }
    {   // Find Item: nothing low (a tester, 2026-11-05: "Please no floor stuff"); drawn back with no rest out first, or to the mouth, none
        Sim s(Motion::FindItem);FindItem(s);
        Check(s.casts==1&&s.g.Stats().low>-.65f,"Find Item: cast with the hands never below the waist");
        Sim quick(Motion::FindItem);quick.MoveR({.20f,-.45f,-.45f},.2f);quick.MoveR({.17f,-.18f,-.05f},.2f);quick.Hold(.3f);
        Check(quick.casts==0,"Find Item: a jab out and straight back, no rest at the corpse, is none");
        Sim mouth(Motion::FindItem);mouth.MoveR({.20f,-.45f,-.45f},.35f);mouth.Hold(.25f);mouth.MoveR({.02f,-.12f,-.18f},.25f);mouth.Hold(.3f);
        Check(mouth.casts==0,"Find Item: snatched back to the mouth (a warcry's start) is none");
    }
    {   // the owner's Howls (2026-11-05): the hands together at the face, flung out 1.1 m apart at eye level
        Sim face(Motion::Howl);face.Move({.07f,.05f,-.25f},{-.07f,.05f,-.25f},.35f);face.Hold(.1f);face.Move({.50f,.05f,-.28f},{-.50f,.0f,-.32f},.25f);face.Hold(.2f);
        Check(face.casts==1,"Howl: flung wide from the hands together at the face casts");
        Sim shout(Motion::Howl);shout.Move({.07f,-.08f,-.10f},{-.07f,-.08f,-.10f},.35f);shout.Hold(.2f);shout.Move({.08f,-.10f,-.55f},{-.08f,-.10f,-.55f},.2f);shout.Hold(.2f);
        Check(shout.casts==0,"Howl: both fists pushed forward together from the mouth (Shout) is no Howl");
    }
    {   // the owner's Battle Cries (2026-11-05): from by the head down to the hips, ending 61-67 cm under the eyes, wide
        Sim pull(Motion::BattleCry);pull.Move({.25f,.08f,-.10f},{-.25f,.06f,-.10f},.35f);pull.Hold(.1f);pull.Move({.50f,-.64f,.07f},{-.45f,-.62f,.0f},.25f);pull.Hold(.2f);
        Check(pull.casts==1,"Battle Cry: fists from by the head pulled down to 62-64 cm under the eyes casts");
        Sim back(Motion::BattleCry);back.Move({.25f,.08f,-.10f},{-.25f,.06f,-.10f},.35f);back.Hold(.1f);back.Move({.43f,-.68f,.31f},{-.49f,-.66f,.07f},.25f);back.Hold(.2f);
        Check(back.casts==1,"Battle Cry: pulled down and back, 31 cm behind the eyes, casts");
        Sim howl(Motion::BattleCry);howl.Move({.07f,.05f,-.25f},{-.07f,.05f,-.25f},.35f);howl.Hold(.1f);howl.Move({.50f,.05f,-.28f},{-.50f,.0f,-.32f},.25f);howl.Hold(.2f);
        Check(howl.casts==0,"Battle Cry: the owner's Howl, flung wide at eye level, is not its pull");
    }
    std::puts("motions-barbarian-test: ok");
    return 0;
}
