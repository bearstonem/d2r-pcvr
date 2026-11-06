#include "spell_motion.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <initializer_list>
#include <string>
using namespace framevr;
using spellmotion::Motion;
static void Check(bool b,const char* label){if(!b){std::fprintf(stderr,"FAIL: %s\n",label);std::exit(1);}}
// Head-local points (+X right, +Y up, -Z forward). Hands {right, left}; a move eases between points
// (minimum jerk) at 90 Hz.
static const math::Vec RestR{.25f,-.6f,-.15f},RestL{-.25f,-.6f,-.15f};
struct Sim {
    spellmotion::Engine g;spellmotion::Input in;spellmotion::Output out;
    math::Quat yaw{0,0,0,1};math::Vec head{0,1.7f,0};
    int casts=0,drops=0,began=0;std::string lastDrop;
    math::Vec aim{};   // the last cast's way
    float frame=1/90.f;
    Sim(Motion m,float heading=0){yaw=math::Yaw(heading);in.time=1;in.session=3;in.epoch=1;in.active=true;in.motion=m;
        in.tracked[0]=in.tracked[1]=true;in.head=head;in.headRotation=yaw;Place(0,RestR);Place(1,RestL);Step();}
    void Place(int h,math::Vec p){in.hand[h]=head+math::Rotate(yaw,p);}
    static float Ease(float t){return t*t*t*(10+t*(-15+6*t));}
    // Both hands to their points together (a hand given as itself stays).
    void Move(math::Vec r,math::Vec l,float seconds) {
        const math::Vec a[2]={in.hand[0],in.hand[1]},b[2]={head+math::Rotate(yaw,r),head+math::Rotate(yaw,l)};
        const int steps=std::max(1,int(seconds/frame));
        for(int i=1;i<=steps;++i){for(int h=0;h<2;++h)in.hand[h]=a[h]+(b[h]-a[h])*Ease(float(i)/steps);Step();}
    }
    void MoveR(math::Vec r,float seconds){Move(r,Local(1),seconds);}
    void MoveL(math::Vec l,float seconds){Move(Local(0),l,seconds);}
    math::Vec Local(int h)const{return math::Rotate(math::Conjugate(yaw),in.hand[h]-head);}
    // Each hand where f puts it at t seconds in.
    void Drive(float seconds,const std::function<void(float,math::Vec&,math::Vec&)>& f) {
        for(int i=1;i<=int(seconds/frame);++i){math::Vec r=Local(0),l=Local(1);f(i*frame,r,l);Place(0,r);Place(1,l);Step();}
    }
    void Hold(float s){for(int i=0;i<int(s/frame);++i)Step();}
    void Trigger(bool down){in.held=down;Step();}
    void Step(){in.time+=frame;out=g.Step(in);if(out.cast){++casts;aim=out.aim;}if(out.began)++began;if(out.dropped){++drops;lastDrop=out.dropped;}}
};
// The motions, done well, from the resting pose; the trigger is the caller's.
static void Nova(Sim& s){s.Move({.08f,-.40f,-.30f},{-.08f,-.40f,-.30f},.35f);s.Move({.62f,-.35f,-.15f},{-.62f,-.35f,-.15f},.25f);}
static void FrostNova(Sim& s){s.Move({.15f,-.35f,-.30f},{-.15f,-.35f,-.30f},.35f);s.Move({.15f,-.78f,-.30f},{-.15f,-.78f,-.30f},.30f);}
static void StaticField(Sim& s) {
    s.Move({.045f,-.45f,-.30f},{-.045f,-.45f,-.30f},.35f);
    s.Drive(.6f,[](float t,math::Vec& r,math::Vec&){r.z=-.30f+.05f*std::sin(t*6.2832f*4);});   // four rubs
    s.Move({.38f,-.45f,-.25f},{-.38f,-.45f,-.25f},.15f);
}
static void EnergyShield(Sim& s){s.Move({-.10f,-.12f,-.30f},{.10f,-.12f,-.30f},.4f);s.Hold(.5f);}
static void ThunderStorm(Sim& s){s.MoveR({.15f,.40f,-.10f},.45f);s.Hold(.5f);}
static void Blaze(Sim& s){s.MoveR({.24f,-.55f,0},.3f);s.MoveR({.24f,-.95f,0},.2f);}
static void FrozenArmor(Sim& s){s.Move({.45f,-.35f,-.35f},{.32f,-.35f,-.32f},.45f);s.MoveL({.17f,-.24f,.05f},.45f);}
static void ShiverArmor(Sim& s) {
    s.Move({.15f,-.40f,-.30f},{-.15f,-.40f,-.30f},.35f);
    s.Drive(1.f,[](float t,math::Vec& r,math::Vec& l){const float y=-.40f+.06f*std::sin(t*6.2832f*4);r.y=l.y=y;});
}
static void Hug(Sim& s){s.Move({-.18f,-.32f,-.05f},{.18f,-.32f,-.05f},.45f);s.Hold(.5f);}
static void Teleport(Sim& s){s.MoveR({.15f,-.32f,-.12f},.35f);s.Hold(.1f);s.MoveR({.17f,-.28f,-.62f},.2f);}

int main() {
    using Do=void(*)(Sim&);
    const struct {Motion motion;Do perform;int skill;const char* name;} all[]={
        {Motion::FlingOut,Nova,48,"Nova"},{Motion::PushDown,FrostNova,44,"Frost Nova"},{Motion::RubSnap,StaticField,42,"Static Field"},
        {Motion::CrossGuard,EnergyShield,58,"Energy Shield"},{Motion::ArmUp,ThunderStorm,57,"Thunder Storm"},{Motion::MatchStrike,Blaze,46,"Blaze"},
        {Motion::ForearmSweep,FrozenArmor,40,"Frozen Armor"},{Motion::Shake,ShiverArmor,50,"Shiver Armor"},{Motion::Hug,Hug,60,"Chilling Armor"},
        {Motion::Punch,Teleport,54,"Teleport"}};
    for(const auto& m:all) {
        std::string label=std::string(m.name)+": ";
        Check(spellmotion::For(m.skill)==m.motion&&std::string(spellmotion::Cue(m.motion))!="no motion",(label+"its skill has the motion and a cue").c_str());
        for(float heading:{-2.f,0.f,2.f}){Sim s(m.motion,heading);s.Trigger(true);m.perform(s);
            Check(s.casts==1&&s.began==1,(label+"the trigger held and the motion casts once, at any heading").c_str());}
        {Sim s(m.motion);m.perform(s);Check(s.casts==0&&s.began==0,(label+"the motion without the trigger casts nothing").c_str());}
        {Sim s(m.motion);s.Trigger(true);s.Hold(.3f);s.Trigger(false);
         Check(s.casts==0&&s.drops==1&&s.lastDrop.find("let go")!=std::string::npos,(label+"let go without the motion: dropped, logged").c_str());}
        {Sim s(m.motion);s.in.active=false;s.Trigger(true);m.perform(s);Check(s.casts==0,(label+"inactive (a wheel, a menu, a bottle) casts nothing").c_str());}
        // Every other motion, done well, is not this spell's. Only the readied spell's motion is
        // watched, so one overlap is allowed: Static Field's snap apart is as wide as a Nova fling.
        for(const auto& other:all)if(other.motion!=m.motion&&!(m.motion==Motion::FlingOut&&other.motion==Motion::RubSnap)) {
            Sim s(m.motion);s.Trigger(true);other.perform(s);
            Check(s.casts==0,(label+"another spell's motion ("+other.name+") casts nothing").c_str());
        }
    }
    {   // Every class's page (owner, 2026-11-04: "Both grips, like Sorceress"): the Sorceress's bits as saved
        // since 2026-11-03, the Paladin's and the Druid's by card; a name and a cue on every card.
        Check(spellmotion::PageOf(1).count==spellmotion::SorceressCount&&spellmotion::PageOf(3).count==4&&spellmotion::PageOf(5).count==2,
              "the Sorceress's 10, the Paladin's 4, the Druid's 2");
        Check(spellmotion::PageOf(0).count==4&&spellmotion::PageOf(4).count==8&&spellmotion::PageOf(2).count==8&&spellmotion::PageOf(6).count==7,
              "the Amazon's 4, the Barbarian's 8, the Necromancer's 8, the Assassin's 7 (the owner's table)");
        for(int c:{7,-1})Check(!spellmotion::HasMotions(c)&&spellmotion::PageOf(c).count==0,"no motions for the Warlock or an unknown class");
        for(int c:{0,2,4,6}) {   // their cards' motions are the engine's, each watched in the class's order exactly once
            const auto page=spellmotion::PageOf(c);const auto order=spellmotion::OrderOf(c);
            Check(page.count<=spellmotion::MaxCards&&order.count==page.count,"a watcher per card");
            for(int i=0;i<page.count;++i) {
                const auto m=spellmotion::For(page.skills[i]);
                Check(m>spellmotion::Motion::Punch&&std::count(order.motions,order.motions+order.count,m)==1,"each card's motion watched once");
            }
        }
        {   // a family's card casts its strongest learned skill
            const auto known=[](std::initializer_list<int> ids){return [ids](int id){return std::find(ids.begin(),ids.end(),id)!=ids.end();};};
            Check(spellmotion::CastSkillOf(75,known({75}))==75&&spellmotion::CastSkillOf(75,known({75,85}))==85&&spellmotion::CastSkillOf(75,known({75,85,94}))==94,
                  "the golems: Clay, then Blood, then Fire");
            Check(spellmotion::CastSkillOf(268,known({268}))==268&&spellmotion::CastSkillOf(268,known({268,279}))==279&&spellmotion::CastSkillOf(149,known({}))==149,
                  "the shadows; any other card its own skill");
        }
        for(const int id:spellmotion::PageSkills)Check(spellmotion::Bit(1,id)==spellmotion::Bit(id)&&spellmotion::On(1,id,0)&&!spellmotion::On(1,id,1<<spellmotion::Bit(id)),
              "the Sorceress's bits are her motions' as before");
        for(int c:{0,1,2,3,4,5,6}) {
            const auto page=spellmotion::PageOf(c);
            for(int i=0;i<page.count;++i) {
                const int id=page.skills[i];
                if(c!=1)Check(spellmotion::Bit(c,id)==i,"another class's bit is its card's");
                Check(spellmotion::SpellName(id)[0]&&std::string(spellmotion::CueOf(id))!="no motion","every card has a name and a cue");
                Check(!spellmotion::On(c,id,1<<spellmotion::Bit(c,id))&&spellmotion::On(c,id,~(1<<spellmotion::Bit(c,id))&1023)==true,"its bit alone turns it off");
            }
        }
        Check(spellmotion::Bit(3,48)==-1&&!spellmotion::On(3,48,0)&&spellmotion::Bit(1,107)==-1,"a skill is on its own class's page only");
    }
    {   // The grips' hold every class's motions take: both above .6, kept down to .35, never one alone.
        spellmotion::GripGate g;
        const float none[2]{0,0},right[2]{.9f,0},left[2]{0,.9f},both[2]{.9f,.9f},dip[2]{.5f,.4f},low[2]{.3f,.9f},nan[2]{std::nanf(""),.9f};
        Check(!g.Step(none)&&!g.Step(right)&&!g.Step(left),"one grip alone never opens it");
        Check(g.Step(both)&&g.Step(dip),"both open it, and a squeeze that dips keeps it");
        Check(!g.Step(low),"one let go below .35 closes it");
        Check(!g.Step(dip),"closed, a half squeeze does not open it again");
        Check(g.Step(both)&&!g.Step(nan),"an unknown grip closes it");
        g.Step(both);g.Reset();Check(!g.Held(),"a reset lets go");
        spellmotion::Gate gate;
        Check(!gate.Step(both,false).watch&&!gate.Step(both,false).open,"busy hands: never watched");
        auto st=gate.Step(both,true);Check(st.open&&st.watch,"both grips, hands free: watched");
        st=gate.Step(none,true);Check(!st.open&&st.watch&&!st.held,"the frame the grips open is still watched (a gesture may end on it)");
        st=gate.Step(none,true);Check(!st.open&&!st.watch,"then not");
    }
    Check(spellmotion::For(36)==Motion::None&&spellmotion::For(117)==Motion::None,
        "Fire Bolt and a Paladin skill have no motion: their glyph casts as before");
    {Sim s(Motion::Punch);s.Trigger(true);s.MoveR({.15f,-.32f,-.12f},.35f);s.Hold(.1f);s.MoveR({.17f,-.28f,-.62f},.9f);
     Check(s.casts==0,"Teleport: the arm pushed out slowly casts nothing");}
    {Sim s(Motion::Punch);s.Trigger(true);s.MoveR({.15f,-.32f,-.12f},.35f);s.Hold(.1f);s.MoveR({.15f,.20f,-.50f},.2f);
     Check(s.casts==0,"Teleport: an uppercut is not a punch");}
    for(float heading:{-2.f,1.f}){Sim s(Motion::Punch,heading);s.Trigger(true);Teleport(s);
     Check(s.casts==1&&s.aim.z<-.25f&&std::fabs(s.aim.x)<.1f&&std::fabs(s.aim.y)<.1f,"Teleport: it goes the way of the punch, head-local (straight ahead at any heading)");}
    {Sim s(Motion::Punch);s.Trigger(true);s.MoveR({.05f,-.32f,-.12f},.35f);s.Hold(.1f);s.MoveR({.25f,-.30f,-.58f},.2f);
     Check(s.casts==1&&s.aim.x>.1f&&s.aim.z<-.25f,"Teleport: a punch out to the right goes right of ahead");}
    {Sim s(Motion::FlingOut);s.Trigger(true);Nova(s);Check(s.casts==1&&math::Dot(s.aim,s.aim)==0,"Nova: no way to go");}
    {Sim s(Motion::FlingOut);s.Trigger(true);Nova(s);s.Move(RestR,RestL,.4f);s.Hold(.4f);Nova(s);
     Check(s.casts==2,"still held, Nova again from the start casts again");s.Trigger(false);Check(s.drops==0,"a hold that cast is not dropped");}
    {Sim s(Motion::FlingOut);s.Trigger(true);s.Move({.62f,-.35f,-.15f},{-.62f,-.35f,-.15f},.3f);
     Check(s.casts==0,"Nova: arms flung out from the sides, never together first, casts nothing");}
    {Sim s(Motion::PushDown);s.Trigger(true);s.Move({.15f,-.35f,-.30f},{-.15f,-.35f,-.30f},.35f);s.MoveR({.15f,-.78f,-.30f},.3f);
     Check(s.casts==0,"Frost Nova: one hand pushed down casts nothing");}
    {Sim s(Motion::PushDown);s.Trigger(true);s.Move({.15f,-.35f,-.30f},{-.15f,-.35f,-.30f},.35f);s.Move({.15f,-.78f,-.30f},{-.15f,-.78f,-.30f},2.5f);
     Check(s.casts==0,"Frost Nova: hands lowered slowly cast nothing");}
    // The owner's first session (2026-11-03): what was missed then casts now.
    {Sim s(Motion::FlingOut);s.Trigger(true);s.Move({.08f,-.40f,-.30f},{-.08f,-.40f,-.30f},.35f);s.Move({.40f,-.35f,-.20f},{-.40f,-.35f,-.20f},.25f);
     Check(s.casts==1,"Nova: a fling 80 cm wide casts (78 and 89 cm were missed)");}
    {Sim s(Motion::PushDown);s.Trigger(true);s.Move({.15f,-.36f,-.25f},{-.15f,-.36f,-.25f},.35f);s.Move({.30f,-.62f,.12f},{-.30f,-.62f,.12f},.3f);
     Check(s.casts==1,"Frost Nova: pushed down from the chest to the sides casts");}
    {Sim s(Motion::CrossGuard);s.Trigger(true);s.Move({-.07f,-.08f,-.08f},{.07f,-.08f,-.08f},.4f);s.Hold(.5f);
     Check(s.casts==1,"Energy Shield: the guard held 14 cm apart close to the face casts");}
    {Sim s(Motion::RubSnap);s.Trigger(true);s.Move({.045f,-.45f,-.30f},{-.045f,-.45f,-.30f},.35f);s.Hold(.4f);s.Move({.38f,-.45f,-.25f},{-.38f,-.45f,-.25f},.15f);
     Check(s.casts==0,"Static Field: palms together, no rubbing, then apart, casts nothing");}
    {Sim s(Motion::CrossGuard);s.Trigger(true);s.Move({.10f,-.12f,-.30f},{-.10f,-.12f,-.30f},.4f);s.Hold(.6f);
     Check(s.casts==1,"Energy Shield: hands up before the face, not crossed, casts too");}
    {Sim s(Motion::CrossGuard);s.Trigger(true);s.Move({.30f,-.12f,-.30f},{-.30f,-.12f,-.30f},.4f);s.Hold(.6f);
     Check(s.casts==0,"Energy Shield: hands 60 cm apart at the face cast nothing");}
    {Sim s(Motion::CrossGuard);s.Trigger(true);s.Move({-.10f,-.12f,-.30f},{.10f,-.12f,-.30f},.4f);s.Hold(.2f);s.Move(RestR,RestL,.4f);
     Check(s.casts==0,"Energy Shield: crossed too briefly casts nothing");}
    {Sim s(Motion::ArmUp);s.Trigger(true);s.MoveR({.15f,.08f,-.25f},.4f);s.Hold(.6f);
     Check(s.casts==0,"Thunder Storm: a hand only at forehead height casts nothing");}
    {Sim s(Motion::MatchStrike);s.Trigger(true);s.MoveR({.24f,-.55f,0},.3f);s.MoveR({.24f,-.95f,0},.9f);
     Check(s.casts==0,"Blaze: the hand lowered slowly casts nothing");}
    {Sim s(Motion::ForearmSweep);s.Trigger(true);s.MoveR({.45f,-.35f,-.35f},.4f);s.MoveL({.17f,-.24f,.05f},.6f);
     Check(s.casts==0,"Frozen Armor: the left hand to the shoulder without passing the right hand casts nothing");}
    {Sim s(Motion::Shake);s.Trigger(true);s.Move({.15f,-.40f,-.30f},{-.15f,-.40f,-.30f},.35f);
     s.Drive(1.f,[](float t,math::Vec& r,math::Vec&){r.y=-.40f+.06f*std::sin(t*6.2832f*4);});
     Check(s.casts==0,"Shiver Armor: one fist shaken casts nothing");}
    {Sim s(Motion::FlingOut);s.Trigger(true);s.in.motion=Motion::FlingOut;s.Trigger(true);s.in.active=false;s.Step();s.in.active=true;Nova(s);
     Check(s.casts==0,"made inactive while held: the trigger must be pressed again");s.Trigger(false);s.Trigger(true);Nova(s);Check(s.casts==1,"pressed again, it casts");}
    {Sim s(Motion::FlingOut);s.Trigger(true);s.Move({.08f,-.40f,-.30f},{-.08f,-.40f,-.30f},.35f);s.in.session=4;s.Move({.62f,-.35f,-.15f},{-.62f,-.35f,-.15f},.25f);
     Check(s.casts==0,"a new session forgets the hold");}
    // Free: a spell on a hand slot, its motion alone, no trigger.
    for(const auto& m:all) {
        std::string label=std::string(m.name)+" (on a slot): ";
        Sim s(m.motion);s.in.free=true;m.perform(s);
        Check(s.casts==1&&s.began==0&&s.drops==0,(label+"the motion alone casts, nothing to let go").c_str());
        s.Move(RestR,RestL,.5f);s.Hold(.8f);m.perform(s);Check(s.casts==2,(label+"and again").c_str());
    }
    {Sim s(Motion::FlingOut);s.in.free=true;s.in.active=false;Nova(s);Check(s.casts==0,"free: inactive (a trigger squeezed, a bottle) casts nothing");
     s.in.active=true;s.Hold(.2f);Nova(s);Check(s.casts==1,"free: active again, no press needed");}
    {Sim s(Motion::PushDown);s.in.free=true;s.Move({.15f,-.35f,-.30f},{-.15f,-.35f,-.30f},.4f);s.Move(RestR,RestL,1.5f);s.Hold(1.f);
     Check(s.casts==0,"free: arms lowered to rest at an easy pace cast no Frost Nova");}
    {   // both grips cast spells: a grip grabs only alone
        spellmotion::SoloGrips g;double t=0;
        auto step=[&](float r,float l,double dt){t+=dt;const float v[2]={r,l};g.Step(t,v);};
        step(1,0,.01);Check(!g.solo[0],"a grip alone grabs only after a moment...");
        step(1,0,.11);Check(g.solo[0],"...held alone 0.1 s, it grabs");
        step(1,1,.01);Check(g.solo[0]&&!g.solo[1],"the other pressed after: the first keeps its grab, the other never");
        step(.5f,1,.2);Check(g.solo[0]&&!g.solo[1],"a squeeze dipping to .5 is still held");
        step(0,1,.01);step(0,1,.2);Check(!g.solo[0]&&!g.solo[1],"let go: nothing");
        step(1,1,.01);step(.2f,1,.01);step(1,1,.2);Check(!g.solo[0]&&!g.solo[1],"both held, a grip let go and pressed again: still no grab");
        step(0,0,.01);step(1,0,.03);step(1,1,.01);step(1,1,.3);Check(!g.solo[0]&&!g.solo[1],"pressed together (the other 30 ms later): no grab");
        step(0,0,.01);step(0,1,.01);step(0,1,.11);Check(g.solo[1],"the left alone grabs too");
    }
    std::puts("spell-motion-test: ok");
    return 0;
}
