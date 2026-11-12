#include "spell_motion.h"
#include "motion_left.h"
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
    bool landing=false;math::Vec point{};   // the last cast's: Teleport on its shown landing, and the arm that pointed it
    spellmotion::Land land=spellmotion::Land::None;spellmotion::Spot spot{};   // the last cast's spot of its own
    float frame=1/90.f;
    Sim(Motion m,float heading=0){yaw=math::Yaw(heading);in.time=1;in.session=3;in.epoch=1;in.active=true;in.motion=m;
        in.tracked[0]=in.tracked[1]=true;in.head=head;in.headRotation=yaw;Place(0,RestR);Place(1,RestL);Step();}
    float body=1;   // the player's size: every point the reference body's, times this (and in.bodyScale, when told)
    void Place(int h,math::Vec p){in.hand[h]=head+math::Rotate(yaw,p*body);}
    static float Ease(float t){return t*t*t*(10+t*(-15+6*t));}
    // Both hands to their points together (a hand given as itself stays).
    void Move(math::Vec r,math::Vec l,float seconds) {
        const math::Vec a[2]={in.hand[0],in.hand[1]},b[2]={head+math::Rotate(yaw,r*body),head+math::Rotate(yaw,l*body)};
        const int steps=std::max(1,int(seconds/frame));
        for(int i=1;i<=steps;++i){for(int h=0;h<2;++h)in.hand[h]=a[h]+(b[h]-a[h])*Ease(float(i)/steps);Step();}
    }
    void MoveR(math::Vec r,float seconds){Move(r,Local(1),seconds);}
    void MoveL(math::Vec l,float seconds){Move(Local(0),l,seconds);}
    math::Vec Local(int h)const{return math::Rotate(math::Conjugate(yaw),in.hand[h]-head)*(1/body);}
    // Each hand where f puts it at t seconds in.
    void Drive(float seconds,const std::function<void(float,math::Vec&,math::Vec&)>& f) {
        for(int i=1;i<=int(seconds/frame);++i){math::Vec r=Local(0),l=Local(1);f(i*frame,r,l);Place(0,r);Place(1,l);Step();}
    }
    void Hold(float s){for(int i=0;i<int(s/frame);++i)Step();}
    void Trigger(bool down){in.held=down;Step();}
    void Step(){in.time+=frame;out=motionleft::Seen(g.Step(motionleft::Played(in)));if(out.cast){++casts;aim=out.aim;landing=out.landing;point=out.point;land=out.land;spot=out.spot;}if(out.began)++began;if(out.dropped){++drops;lastDrop=out.dropped;}}
};
// The motions, done well, from the resting pose; the trigger is the caller's.
static void Nova(Sim& s){s.Move({.08f,-.40f,-.30f},{-.08f,-.40f,-.30f},.35f);s.Move({.62f,-.35f,-.15f},{-.62f,-.35f,-.15f},.25f);}
static void FrostNova(Sim& s){s.Move({.15f,-.35f,-.30f},{-.15f,-.35f,-.30f},.35f);s.Move({.15f,-.78f,-.30f},{-.15f,-.78f,-.30f},.30f);}
static void StaticField(Sim& s) {
    s.Move({.045f,-.45f,-.30f},{-.045f,-.45f,-.30f},.35f);
    s.Drive(.6f,[](float t,math::Vec& r,math::Vec&){r.z=-.30f+.05f*std::sin(t*6.2832f*4);});   // four rubs
    s.Move({.38f,-.45f,-.25f},{-.38f,-.45f,-.25f},.15f);
}
static void EnergyShield(Sim& s){s.Move({-.10f,-.12f,-.30f},{.10f,-.12f,-.30f},.4f);s.Hold(.7f);}
static void ThunderStorm(Sim& s){s.MoveR({.15f,.40f,-.10f},.45f);s.Hold(.5f);}
static void Blaze(Sim& s){s.MoveR({.24f,-.55f,0},.3f);s.MoveR({.24f,-.95f,0},.2f);}
static void FrozenArmor(Sim& s){s.Move({.45f,-.35f,-.35f},{.32f,-.35f,-.32f},.45f);s.MoveL({.17f,-.24f,.05f},.45f);}
static void ShiverArmor(Sim& s) {
    s.Move({.18f,-.40f,-.30f},{-.18f,-.40f,-.30f},.35f);
    s.Drive(1.f,[](float t,math::Vec& r,math::Vec& l){const float y=-.40f+.06f*std::sin(t*6.2832f*4);r.y=l.y=y;});
}
static void Hug(Sim& s){s.Move({-.18f,-.32f,-.05f},{.18f,-.32f,-.05f},.45f);s.Hold(.5f);}
static void Teleport(Sim& s){s.MoveR({.20f,-.32f,-.45f},.35f);s.Hold(.25f);s.MoveR({.19f,-.26f,-.20f},.15f);}
static void FireWall(Sim& s){s.MoveR({-.20f,-.30f,-.40f},.35f);s.Hold(.1f);s.MoveR({.24f,-.30f,-.40f},.2f);s.Hold(.2f);}
static void Meteor(Sim& s){s.MoveR({.15f,.35f,-.05f},.3f);s.MoveR({.18f,-.40f,-.40f},.2f);s.Hold(.1f);}
static void Blizzard(Sim& s){s.Move({.15f,.30f,-.10f},{-.15f,.30f,-.10f},.3f);s.Move({.20f,-.30f,-.35f},{-.20f,-.30f,-.35f},.25f);s.Hold(.2f);}
static void Hydra(Sim& s){s.MoveL({-.165f,.12f,-.45f},.3f);s.Hold(.2f);}

static void Run() {
    using Do=void(*)(Sim&);
    const struct {Motion motion;Do perform;int skill;const char* name;} all[]={
        {Motion::FlingOut,Nova,48,"Nova"},{Motion::PushDown,FrostNova,44,"Frost Nova"},{Motion::RubSnap,StaticField,42,"Static Field"},
        {Motion::CrossGuard,EnergyShield,58,"Energy Shield"},{Motion::ArmUp,ThunderStorm,57,"Thunder Storm"},{Motion::MatchStrike,Blaze,46,"Blaze"},
        {Motion::ForearmSweep,FrozenArmor,40,"Frozen Armor"},{Motion::Shake,ShiverArmor,50,"Shiver Armor"},{Motion::Hug,Hug,60,"Chilling Armor"},
        {Motion::Punch,Teleport,54,"Teleport"},{Motion::FireWall,FireWall,51,"Fire Wall"},{Motion::Meteor,Meteor,56,"Meteor"},
        {Motion::Blizzard,Blizzard,59,"Blizzard"},{Motion::Hydra,Hydra,62,"Hydra"}};
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
        Check(spellmotion::PageOf(1).count==spellmotion::SorceressCount&&spellmotion::SorceressCount==14&&spellmotion::PageOf(3).count==4&&spellmotion::PageOf(5).count==2,
              "the Sorceress's 14, the Paladin's 4, the Druid's 2");
        Check(spellmotion::PageOf(0).count==4&&spellmotion::PageOf(4).count==13&&spellmotion::PageOf(2).count==12&&spellmotion::PageOf(6).count==7,
              "the Amazon's 4, the Barbarian's 13 (Leap Attack and Whirlwind after the table's, then Find Potion, Grim Ward and Taunt), the Necromancer's 12 (the table's, the left arm's golem, Bone Wall, Poison Explosion, Raise Skeletal Mage), the Assassin's 7");
        Check(spellmotion::PageOf(7).count==16&&spellmotion::MaxCards==16,"the Warlock's 16 (three demons, three sigils, Abyss, Flame Wave, three hexes, Psychic Ward, Eldritch Blast, Ring of Fire, Apocalypse, Blood Boil)");
        for(int c:{8,-1})Check(!spellmotion::HasMotions(c)&&spellmotion::PageOf(c).count==0,"no motions for an unknown class");
        for(int c:{0,2,3,4,5,6,7}) {   // their cards' motions are the engine's (the Paladin's and the Druid's since 2026-11-08), each watched in the class's order exactly once
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
                  "the right arm's golem: the strongest learned, Fire, then Blood, then Clay");
            Check(spellmotion::CastSkillOf(85,known({75,85,94}))==85&&spellmotion::CastSkillOf(85,known({75,94}))==75&&spellmotion::CastSkillOf(85,known({75,85}))==75&&
                  spellmotion::CastSkillOf(85,known({85}))==85,"the left arm's golem: the next one down, else the only one");
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
                Check(spellmotion::Family(id)==(id==75||id==85||id==268),"the families' cards: the golems' two and the shadows'");
                Check(!spellmotion::On(c,id,1<<spellmotion::Bit(c,id))&&spellmotion::On(c,id,~(1<<spellmotion::Bit(c,id))&1023)==true,"its bit alone turns it off");
            }
        }
        {   // the golems' cards say which golem each arm raises (owner, 2026-11-06: "One is a blood golem, the other one is a fire golem")
            const auto all=[](int){return true;};
            const auto clay=[](int id){return id==75;};
            const auto title=[](int card,int casts){return std::string(spellmotion::SpellName(casts))+spellmotion::CardArm(card);};
            Check(title(75,spellmotion::CastSkillOf(75,all))=="Fire Golem (right arm)"&&title(85,spellmotion::CastSkillOf(85,all))=="Blood Golem (left arm)",
                  "all three golems learned: Fire Golem (right arm), Blood Golem (left arm)");
            Check(title(75,spellmotion::CastSkillOf(75,clay))=="Clay Golem (right arm)"&&title(85,spellmotion::CastSkillOf(85,clay))=="Clay Golem (left arm)",
                  "Clay Golem alone: both arms raise it, and both cards say so");
            Check(std::string(spellmotion::SpellName(spellmotion::CastSkillOf(268,all)))=="Shadow Master"&&*spellmotion::CardArm(268)==0,"the shadows' card: Shadow Master once learned, no arm");
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
    Check(spellmotion::For(36)==Motion::None&&spellmotion::For(101)==Motion::None&&spellmotion::For(117)==Motion::HolyShield,
        "Fire Bolt and Holy Bolt have no motion: their glyph casts as before (Holy Shield's sign is a motion since 2026-11-08)");
    {   // a body of another size (owner, 2026-11-05: calibration to make the motions "more accurate"): its hands
        // in the reference body's units by the calibrated span, capped at 0.8-1.2
        Check(spellmotion::BodyScale(spellmotion::ReferenceSpan)==1&&spellmotion::BodyScale(0)==1&&spellmotion::BodyScale(std::nanf(""))==1&&
              spellmotion::BodyScale(1.75f)>1.19f&&spellmotion::BodyScale(1.75f)<1.2f+1e-6f&&spellmotion::BodyScale(3.f)==spellmotion::MaxBodyScale&&
              spellmotion::BodyScale(.5f)==spellmotion::MinBodyScale,"the body scale: the reference span 1, none calibrated 1, capped at 0.8-1.2");
        for(const float body:{.8f,1.2f}) {
            const auto told=[&](Sim& s){s.body=body;s.in.bodyScale=body;s.Place(0,RestR);s.Place(1,RestL);s.Step();};
            {Sim s(Motion::Punch);told(s);s.Trigger(true);Teleport(s);Check(s.casts==1,"Teleport: a smaller or larger body, its scale told, casts");}
            {Sim s(Motion::FlingOut);told(s);s.Trigger(true);Nova(s);Check(s.casts==1,"Nova: a smaller or larger body, its scale told, casts");}
            {Sim s(Motion::Meteor);told(s);s.Trigger(true);Meteor(s);
             Check(s.casts==1&&s.spot.ahead>4&&s.spot.ahead<16,"Meteor: a smaller or larger body lands as the reference body's throw");}
        }
        {   // a small body's arm held out: untold, short of the reference reach; told, it points
            Sim untold(Motion::Punch);untold.body=.8f;untold.Place(0,RestR);untold.Place(1,RestL);untold.Step();untold.Trigger(true);
            untold.MoveR({.17f,-.22f,-.40f},.35f);untold.Hold(.25f);
            Check(!untold.out.aiming,"Teleport: a small body's arm out, its scale not told, is short of the reach");
            Sim told(Motion::Punch);told.body=.8f;told.in.bodyScale=.8f;told.Place(0,RestR);told.Place(1,RestL);told.Step();told.Trigger(true);
            told.MoveR({.17f,-.22f,-.40f},.35f);told.Hold(.25f);
            Check(told.out.aiming,"Teleport: the same arm, its scale told, points");
        }
    }
    {   // the owner's hugs (2026-11-05): held 1.9 s, the left hand 17 cm under the eyes, behind them; none cast
        const auto hug=[](Sim& s){s.Move({-.13f,-.22f,-.10f},{.13f,-.17f,.08f},.45f);s.Hold(.6f);};
        {Sim s(Motion::Hug);s.Trigger(true);hug(s);Check(s.casts==1,"Chilling Armor: the owner's hug casts");}
        {Sim s(Motion::CrossGuard);s.Trigger(true);hug(s);Check(s.casts==0,"Energy Shield: the hug is not its guard");}
        {Sim s(Motion::CrossGuard);s.Trigger(true);s.Move({-.10f,-.15f,-.05f},{.10f,-.15f,-.05f},.45f);s.Hold(.6f);
         Check(s.casts==0,"Energy Shield: crossed at the chest close to the body is the hug's");}
        {Sim s(Motion::Hug);s.Trigger(true);s.Move({-.10f,-.15f,-.05f},{.10f,-.15f,-.05f},.45f);s.Hold(.6f);
         Check(s.casts==1,"Chilling Armor: crossed at the chest close to the body casts");}
    }
    {   // the owner's picks (2026-11-05): ground spells and a summon, each to its spot
        {Sim s(Motion::FireWall);s.Trigger(true);FireWall(s);
         Check(s.casts==1&&s.land==spellmotion::Land::Spot&&std::fabs(s.spot.ahead-spellmotion::sorceress::tuning.fireWallAhead)<.2f,"Fire Wall: the wall ahead, where the sweep's middle heads");}
        {Sim s(Motion::Meteor);s.Trigger(true);Meteor(s);
         Check(s.casts==1&&s.land==spellmotion::Land::Spot&&s.spot.ahead>4&&s.spot.ahead<16&&std::fabs(s.spot.right)<1.5f,"Meteor: thrown down ahead, it lands along the arm, nearer than level");}
        {Sim s(Motion::Meteor);s.Trigger(true);s.MoveR({.15f,.35f,-.05f},.3f);s.MoveR({.45f,-.40f,-.30f},.2f);s.Hold(.1f);
         Check(s.casts==1&&s.spot.right>2,"Meteor: thrown to the front right, it lands to the right");}
        {Sim s(Motion::Meteor);s.Trigger(true);s.MoveR({.15f,.35f,-.05f},.3f);s.MoveR({.18f,-.40f,-.40f},.8f);s.Hold(.1f);
         Check(s.casts==0,"Meteor: the arm lowered slowly from overhead casts nothing");}
        {Sim s(Motion::Meteor);s.Trigger(true);s.MoveR({.15f,.35f,-.05f},.3f);s.MoveR({.18f,-.62f,-.30f},.2f);s.Hold(.1f);
         Check(s.casts==1&&s.spot.ahead<10,"Meteor: thrown down steeply, it lands near");}
        {Sim s(Motion::Meteor);s.Trigger(true);s.MoveR({.15f,.35f,-.05f},.3f);s.MoveR({.18f,-.22f,-.50f},.2f);s.Hold(.1f);
         Check(s.casts==1&&s.spot.ahead>17,"Meteor: thrown out level, it lands far");}
        {Sim s(Motion::Meteor);s.Trigger(true);s.MoveR({.15f,.35f,-.05f},.3f);s.MoveR({.18f,-.40f,-.40f},1.6f);s.Hold(.1f);
         Check(s.casts==0,"Meteor: the arm lowered slowly from overhead casts nothing");}
        {Sim s(Motion::Meteor);s.Trigger(true);Blizzard(s);Check(s.casts==0,"Meteor: both arms pulled down is Blizzard's");}
        {Sim s(Motion::ArmUp);s.Trigger(true);Meteor(s);Check(s.casts==0,"Thunder Storm: the arm thrown down without its hold is Meteor's");}
        {Sim s(Motion::Blizzard);s.Trigger(true);Blizzard(s);
         Check(s.casts==1&&s.land==spellmotion::Land::Spot&&s.spot.ahead==spellmotion::sorceress::tuning.blizzardAhead&&s.spot.right==0,"Blizzard: straight ahead");}
        {Sim s(Motion::Blizzard);s.Trigger(true);s.Move({.15f,.30f,-.10f},{-.15f,.30f,-.10f},.3f);s.Move({.20f,-.30f,-.35f},{-.20f,-.30f,-.35f},1.2f);
         Check(s.casts==0,"Blizzard: the arms lowered slowly cast nothing");}
        {Sim s(Motion::PushDown);s.Trigger(true);s.Move({.15f,.30f,-.10f},{-.15f,.30f,-.10f},.3f);s.Move({.20f,-.50f,-.35f},{-.20f,-.50f,-.35f},.3f);
         Check(s.casts==1,"Frost Nova, watched alone, still takes a push to the waist (Blizzard is watched before it)");}
        {Sim s(Motion::Hydra);s.Trigger(true);s.MoveL({-.45f,.10f,-.40f},.3f);s.Hold(.2f);
         Check(s.casts==1&&s.spot.right<-1&&s.spot.ahead>1,"Hydra: swept up toward the front left, it stands there");}
        {Sim s(Motion::Hydra);s.Trigger(true);s.MoveL({-.15f,.40f,-.05f},.4f);s.Hold(.5f);
         Check(s.casts==0,"Hydra: the left arm raised straight up (Thunder Storm) casts nothing");}
        {Sim s(Motion::Hydra);s.Trigger(true);s.Move({.165f,.12f,-.45f},{-.165f,.12f,-.45f},.3f);
         Check(s.casts==0,"Hydra: both arms swept up casts nothing");}
    }
    // Point and pull (owner, 2026-11-05, in the headset: "I want to be able to use that targeting better ... With our
    // arm kind of tucked into us. It's hard to aim where you want to go").
    {
        const auto point=[](Sim& s,math::Vec at){s.MoveR(at,.35f);s.Hold(.25f);};
        const auto near=[](float a,float b,float within){return std::fabs(a-b)<=within;};
        const spellmotion::Tuning t{};
        {Sim s(Motion::Punch);s.Trigger(true);point(s,{.17f,-.22f,-.50f});
         Check(s.out.aiming&&s.casts==0,"Teleport: the arm held out shows its landing, casting nothing");
         const auto spot=spellmotion::PointSpot(s.out.point,s.g.tuning);
         Check(near(spot.ahead,t.pointFar,1.5f)&&near(spot.right,0,1.5f),"Teleport: the arm level points to the far landing, straight ahead");
         s.MoveR({.17f,-.20f,-.22f},.15f);
         Check(s.casts==1&&s.landing&&!s.out.aiming,"Teleport: the yank back casts on the landing shown");}
        for(float heading:{-2.f,1.f}){Sim s(Motion::Punch,heading);s.Trigger(true);point(s,{.17f,-.55f,-.40f});
         const auto spot=spellmotion::PointSpot(s.out.point,s.g.tuning);
         Check(s.out.aiming&&spot.ahead<t.pointFar*.6f&&spot.ahead>t.pointNear&&near(spot.right,0,1.5f),"Teleport: pointed down, nearer, head-local at any heading");}
        {Sim s(Motion::Punch);s.Trigger(true);point(s,{.50f,-.30f,-.40f});
         const auto spot=spellmotion::PointSpot(s.out.point,s.g.tuning);
         Check(s.out.aiming&&spot.right>8&&spot.ahead>8,"Teleport: pointed ahead and to the right, it lands ahead and to the right");}
        {Sim s(Motion::Punch);s.Trigger(true);point(s,{.17f,-.22f,-.50f});s.MoveR({.40f,-.22f,-.40f},.8f);s.Hold(.25f);
         Check(s.out.aiming&&spellmotion::PointSpot(s.out.point,s.g.tuning).right>5,"Teleport: the circle follows the arm moved steadily");}
        {Sim s(Motion::Punch);s.Trigger(true);s.MoveR({.17f,-.22f,-.50f},.35f);s.MoveR({.17f,-.20f,-.22f},.15f);s.Hold(.3f);
         Check(s.casts==0,"Teleport: swung out and straight back, never held out: nothing");}
        {Sim s(Motion::Punch);s.Trigger(true);point(s,{.17f,-.22f,-.50f});s.MoveR(RestR,1.f);s.Hold(1.f);
         Check(s.casts==0&&!s.out.aiming,"Teleport: the arm let down slowly: no cast, and the circle goes");}
        {Sim s(Motion::Punch);s.Trigger(true);s.MoveR({.15f,.40f,-.15f},.4f);s.Hold(.4f);
         Check(!s.out.aiming,"Teleport: an arm raised overhead (Thunder Storm) points nowhere");}
        // the right arm alone (owner, 2026-11-05: "When we raise both arms, the circle shouldn't be drawn")
        {Sim s(Motion::Punch);s.Trigger(true);s.Move({.17f,-.22f,-.50f},{-.12f,-.25f,-.45f},.35f);s.Hold(.4f);
         Check(!s.out.aiming,"Teleport: both arms out in front show no circle");}
        {Sim s(Motion::Punch);s.Trigger(true);s.MoveL({-.20f,.10f,-.10f},.3f);point(s,{.17f,-.22f,-.50f});
         Check(!s.out.aiming,"Teleport: the right arm out with the left raised high shows no circle");}
        {Sim s(Motion::Punch);s.Trigger(true);point(s,{.17f,-.22f,-.50f});s.MoveL({-.15f,-.28f,-.40f},.2f);
         Check(!s.out.aiming,"Teleport: the left arm raised in front takes the circle away at once");
         s.MoveR({.17f,-.20f,-.22f},.15f);s.Hold(.2f);Check(s.casts==0,"Teleport: and the yank then casts nothing");
         s.MoveL(RestL,.3f);s.MoveR({.17f,-.22f,-.50f},.3f);s.Hold(.25f);
         Check(s.out.aiming,"Teleport: the left let down again and the right held out, it shows again");}
        {Sim s(Motion::Punch);s.Trigger(true);s.MoveL({-.15f,-.38f,-.10f},.3f);point(s,{.17f,-.22f,-.50f});
         Check(s.out.aiming,"Teleport: the left hand resting at the chest (not out in front) leaves the circle");}
        {Sim s(Motion::Punch);s.Trigger(true);point(s,{.17f,-.22f,-.50f});s.in.active=false;s.Step();s.in.active=true;s.Hold(.1f);
         Check(!s.out.aiming,"Teleport: the grips let go, the landing goes");}
        {Sim s(Motion::Punch);s.Trigger(true);point(s,{.17f,-.22f,-.50f});s.Hold(t.aimSeconds+.5f);
         Check(!s.out.aiming,"Teleport: shown aimSeconds with no yank, it goes");}
        {Sim s(Motion::Punch);s.Trigger(true);point(s,{.17f,-.22f,-.50f});s.MoveR({.17f,-.20f,-.22f},.15f);s.Hold(1.f);point(s,{.17f,-.22f,-.50f});
         Check(s.casts==1&&s.out.aiming,"Teleport: after a cast, the arm out again shows a new landing");s.MoveR({.17f,-.20f,-.22f},.15f);
         Check(s.casts==2,"Teleport: and the yank lands again");}
        Check(near(spellmotion::PointSpot({0,0,-1},t).ahead,t.pointFar,.01f)&&near(spellmotion::PointSpot({0,-1,-.001f},t).ahead,t.pointNear,.1f)&&
              near(spellmotion::PointSpot({0,.5f,-1},t).ahead,t.pointFar,.01f)&&near(spellmotion::PointSpot({1,0,0},t).right,t.pointFar,.01f)&&
              near(spellmotion::PointSpot({0,0,0},t).ahead,t.pointNear,.01f),
              "Teleport: level or up the far landing, straight down the near one, pointed right it lands right, nowhere the near one");
    }
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
    {Sim s(Motion::CrossGuard);s.Trigger(true);s.Move({-.07f,-.08f,-.08f},{.07f,-.08f,-.08f},.4f);s.Hold(.7f);
     Check(s.casts==1,"Energy Shield: the guard held 14 cm apart close to the face casts");}
    {Sim s(Motion::RubSnap);s.Trigger(true);s.Move({.045f,-.45f,-.30f},{-.045f,-.45f,-.30f},.35f);s.Hold(.4f);s.Move({.38f,-.45f,-.25f},{-.38f,-.45f,-.25f},.15f);
     Check(s.casts==0,"Static Field: palms together, no rubbing, then apart, casts nothing");}
    {Sim s(Motion::CrossGuard);s.Trigger(true);s.Move({.10f,-.12f,-.30f},{-.10f,-.12f,-.30f},.4f);s.Hold(.7f);
     Check(s.casts==1,"Energy Shield: hands up before the face, not crossed, casts too");}
    // A two-handed staff handled with both grips held (owner, 2026-11-06: "I'm accidentally triggering everything")
    {Sim s(Motion::PushDown);s.Trigger(true);s.Move({.15f,-.35f,-.30f},{-.15f,-.35f,-.30f},.35f);s.Move({.15f,-.78f,-.30f},{-.15f,-.78f,-.30f},.6f);s.Hold(.3f);
     Check(s.casts==0,"Frost Nova: a staff lowered from the chest to the waist in 0.6 s casts nothing");}
    {Sim s(Motion::PushDown);s.Trigger(true);s.Move({.15f,-.35f,-.30f},{-.15f,-.35f,-.30f},.35f);s.Move({.15f,-.57f,-.30f},{-.15f,-.57f,-.30f},.2f);s.Hold(.3f);
     Check(s.casts==0,"Frost Nova: a push of 22 cm casts nothing");}
    {Sim s(Motion::CrossGuard);s.Trigger(true);s.Move({.10f,-.12f,-.30f},{-.10f,-.12f,-.30f},.4f);s.Hold(.25f);s.Move(RestR,RestL,.4f);s.Hold(.3f);
     Check(s.casts==0,"Energy Shield: the guard held under half a second casts nothing");}
    {Sim s(Motion::CrossGuard);s.Trigger(true);s.Move({.16f,-.12f,-.30f},{-.17f,-.12f,-.30f},.4f);s.Hold(.8f);
     Check(s.casts==0,"Energy Shield: hands 33 cm apart at the face (a staff's grip) cast nothing");}
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
    {   // Shiver Armor apart from Static Field (a tester, 2026-11-05: "Rubbing reads as shaking"): both palms rubbed
        // together shake nothing, and still charge Static Field; fists shaken close together are a rub, not a shake.
        const auto rub=[](Sim& s){s.Move({.045f,-.45f,-.30f},{-.045f,-.45f,-.30f},.35f);
            s.Drive(1.f,[](float t,math::Vec& r,math::Vec& l){const float w=.05f*std::sin(t*6.2832f*4);r.z=-.30f+w;l.z=-.30f-w;r.y=l.y=-.45f+.03f*w;});};
        {Sim s(Motion::Shake);s.in.free=true;rub(s);Check(s.casts==0,"Shiver Armor: both palms rubbed together shake nothing");}
        {Sim s(Motion::RubSnap);s.in.free=true;rub(s);s.Move({.38f,-.45f,-.25f},{-.38f,-.45f,-.25f},.15f);
         Check(s.casts==1,"Static Field: both palms rubbed, then snapped apart, casts");}
        {Sim s(Motion::Shake);s.in.free=true;s.Move({.08f,-.40f,-.30f},{-.08f,-.40f,-.30f},.35f);
         s.Drive(1.f,[](float t,math::Vec& r,math::Vec& l){const float y=-.40f+.06f*std::sin(t*6.2832f*4);r.y=l.y=y;});
         Check(s.casts==0,"Shiver Armor: fists shaken 16 cm apart cast nothing (held well apart)");}
    }
    {   // The warmer/colder buzz (Output::progress): nothing at rest, Begun once the first part is done, rising to the cast
        {Sim s(Motion::FlingOut);s.in.free=true;s.Hold(.5f);Check(s.out.progress==0,"progress: resting, none");
         s.Move({.08f,-.40f,-.30f},{-.08f,-.40f,-.30f},.35f);
         Check(std::fabs(s.out.progress-spellmotion::Begun)<.02f,"progress: Nova's hands together, Begun");
         s.Move({.25f,-.37f,-.24f},{-.25f,-.37f,-.24f},.12f);
         const float mid=s.out.progress;
         Check(mid>spellmotion::Begun+.1f&&mid<1,"progress: halfway through the fling, warmer");
         s.Move({.62f,-.35f,-.15f},{-.62f,-.35f,-.15f},.12f);
         Check(s.casts==1&&s.out.progress==0,"progress: cast, and none on the cast's frame");}
        {Sim s(Motion::CrossGuard);s.in.free=true;s.Move({-.10f,-.12f,-.30f},{.10f,-.12f,-.30f},.4f);s.Hold(.15f);
         Check(s.out.progress>spellmotion::Begun&&s.out.progress<1&&s.casts==0,"progress: Energy Shield's guard held part of the time");}
        {Sim s(Motion::Shake);s.in.free=true;s.Move({.18f,-.40f,-.30f},{-.18f,-.40f,-.30f},.35f);
         s.Drive(.25f,[](float t,math::Vec& r,math::Vec& l){const float y=-.40f+.06f*std::sin(t*6.2832f*4);r.y=l.y=y;});
         Check(s.casts==0&&s.out.progress>spellmotion::Begun,"progress: Shiver Armor shaken a little, warmer");}
    }
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
}
// Left-handed play written out by hand, with no helper between: what the transport gives the engine for a
// left-handed player (hand_role.h) - hand 0 is the LEFT hand, hand 1 the right, Input::mirror set - and the
// one-sided motions done with the other arm, on the other side.
static void LeftHandedByHand() {
    const auto leftHanded=[](Sim& s){s.in.mirror=true;s.Place(0,RestL);s.Place(1,RestR);s.Step();s.Trigger(true);};   // hand 0: the left hand, at rest on the left
    {   // Teleport: the LEFT arm out, pointing ahead and to the left; the fist yanked back casts there
        Sim s(Motion::Punch);leftHanded(s);
        s.MoveR({-.34f,-.32f,-.45f},.35f);s.Hold(.25f);   // MoveR moves hand 0: the left hand
        Check(s.out.aiming&&s.out.point.x<-.1f,"left-handed Teleport: the left arm held out shows its landing, to the LEFT of ahead");
        Check(spellmotion::PointSpot(s.out.point,s.g.tuning).right<-1&&spellmotion::PointSpot(s.out.point,s.g.tuning).ahead>10,"its spot: ahead and to the left, where the left arm points");
        s.MoveR({-.33f,-.26f,-.20f},.15f);
        Check(s.casts==1&&s.landing&&s.point.x<-.1f&&s.aim.x<-.1f&&s.aim.z<0,"the left fist yanked back casts it on that landing, its way to the left");
    }
    {   // the right arm doing the right-handed Teleport is nothing to a left-handed player (the off arm up hides it)
        Sim s(Motion::Punch);leftHanded(s);
        s.MoveL({.20f,-.32f,-.45f},.35f);s.Hold(.25f);   // MoveL moves hand 1: the right hand
        Check(!s.out.aiming,"left-handed: the RIGHT arm held out shows no landing");
        s.MoveL({.19f,-.26f,-.20f},.15f);Check(s.casts==0,"and its yank casts nothing");
    }
    {   // and right-handed the left arm still shows and casts nothing: OFF is what it was
        Sim s(Motion::Punch);s.Trigger(true);
        s.MoveL({-.34f,-.32f,-.45f},.35f);s.Hold(.25f);Check(!s.out.aiming,"right-handed: the left arm held out shows no landing, as before");
        s.MoveL({-.33f,-.26f,-.20f},.15f);Check(s.casts==0,"nor casts");
    }
    {   // Frozen Armor: "sweep the left hand from the right hand up to the right shoulder" is, left-handed,
        // the RIGHT hand from the left hand up to the LEFT shoulder
        Sim s(Motion::ForearmSweep);leftHanded(s);
        s.Move({-.45f,-.35f,-.35f},{-.32f,-.35f,-.32f},.45f);   // the left arm out on its side, the right hand at the left hand
        s.MoveL({-.17f,-.24f,.05f},.45f);                      // the right hand up to the left shoulder
        Check(s.casts==1,"left-handed Frozen Armor: the right hand swept from the left hand up to the left shoulder");
        Sim wrong(Motion::ForearmSweep);leftHanded(wrong);
        wrong.Move({.32f,-.35f,-.32f},{.45f,-.35f,-.35f},.45f);wrong.MoveR({.17f,-.24f,.05f},.45f);   // the right-handed sweep, by the same two real hands
        Check(wrong.casts==0,"the right-handed sweep (left hand up to the right shoulder) is not Frozen Armor left-handed");
    }
    {   // Nova: a two-hand motion that is its own mirror image casts either way, its hands simply exchanged
        Sim s(Motion::FlingOut);leftHanded(s);
        s.Move({-.08f,-.40f,-.30f},{.08f,-.40f,-.30f},.35f);s.Move({-.62f,-.35f,-.15f},{.62f,-.35f,-.15f},.25f);
        Check(s.casts==1,"left-handed Nova: the same fling, each hand out on its own side");
    }
    {   // Mirrored: the reflection in the upright plane through the head along its heading
        spellmotion::Input in;in.time=1;in.mirror=true;
        const float heading=.7f;const auto yaw=math::Yaw(heading);
        in.head={.3f,1.6f,-.2f};
        in.headRotation=math::Multiply(yaw,math::Multiply(math::Quat{std::sin(.2f),0,0,std::cos(.2f)},math::Quat{0,0,std::sin(.15f),std::cos(.15f)}));   // pitched and rolled
        in.hand[0]=in.head+math::Rotate(yaw,{.25f,-.4f,-.3f});in.hand[1]=in.head+math::Rotate(yaw,{-.2f,-.5f,-.1f});
        in.handVelocity[0]=math::Rotate(yaw,{1,2,3});in.grip[0]=.25f;in.grip[1]=.75f;in.tracked[0]=true;
        const auto m=spellmotion::Mirrored(in);
        const auto local=[&](math::Vec v){return math::Rotate(math::Conjugate(yaw),v);};
        const auto near=[](math::Vec a,math::Vec b){return std::fabs(a.x-b.x)<1e-4f&&std::fabs(a.y-b.y)<1e-4f&&std::fabs(a.z-b.z)<1e-4f;};
        Check(!m.mirror&&near(m.head,in.head),"Mirrored: the head stays where it is");
        Check(near(local(m.hand[0]-m.head),{-.25f,-.4f,-.3f})&&near(local(m.hand[1]-m.head),{.2f,-.5f,-.1f}),"each hand across to the other side, the same height and reach; the order kept");
        Check(near(local(m.handVelocity[0]),{-1,2,3}),"the velocities with them");
        Check(m.grip[0]==.25f&&m.grip[1]==.75f&&m.tracked[0]&&!m.tracked[1],"grips and tracking are the caller's order");
        Check(std::fabs(math::Heading(m.headRotation)-math::Heading(in.headRotation))<1e-4f,"the heading is kept, so the head-local frame is the same one");
        const auto up=math::Rotate(in.headRotation,{0,1,0}),mirroredUp=math::Rotate(m.headRotation,{0,1,0});
        const auto headingYaw=math::Yaw(math::Heading(in.headRotation));
        const auto a=math::Rotate(math::Conjugate(headingYaw),up),b=math::Rotate(math::Conjugate(headingYaw),mirroredUp);
        Check(near(b,{-a.x,a.y,a.z}),"the head's lean reverses: its up reflected");
        const auto twice=spellmotion::Mirrored(m);
        Check(near(twice.hand[0],in.hand[0])&&near(twice.hand[1],in.hand[1])&&near(twice.handVelocity[0],in.handVelocity[0])&&
              std::fabs(math::Dot(math::Rotate(twice.headRotation,{1,0,0}),math::Rotate(in.headRotation,{1,0,0}))-1)<1e-4f,"mirrored twice is the input again");
        spellmotion::Output out;out.aim={.3f,.1f,-.9f};out.point={-.2f,0,-1};out.spot={7,3};out.end[0]={.4f,0,0};out.end[1]={-.5f,0,0};out.behind[0]=.1f;out.behind[1]=.2f;
        spellmotion::MirrorSides(out);
        Check(out.aim.x==-.3f&&out.aim.y==.1f&&out.aim.z==-.9f&&out.point.x==.2f&&out.spot.right==-3&&out.spot.ahead==7&&out.end[0].x==-.4f&&out.end[1].x==.5f&&
              out.behind[0]==.1f&&out.behind[1]==.2f,"an answer's sides turned back: its way, its landing, its spot's right, where the hands ended; per-hand figures keep the engine's order");
    }
    {   // a reset, a bad pose and the hold's own rules are the same mirrored
        Sim s(Motion::Punch);s.in.mirror=true;s.in.head.x=NAN;s.Step();Check(!s.out.cast&&!s.g.Watching(),"left-handed: a non-finite head is ignored, as right-handed");
    }
}
// Everything above, right-handed as written, then played by a left-handed player (motion_left.h): the mirror
// image of every fixture, the left hand as the engine's hand 0, with the same expectations.
int main() {
    motionleft::Both(Run);
    LeftHandedByHand();
    std::puts("spell-motion-test: ok (right-handed, and every fixture again left-handed as its mirror image)");
    return 0;
}
