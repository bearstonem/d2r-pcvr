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
static void CorpseExplosion(Sim& s){s.Move({.15f,-.30f,-.08f},{-.15f,-.30f,-.08f},.35f);s.Hold(.1f);s.Move({.15f,-.28f,-.55f},{-.15f,-.28f,-.55f},.15f);s.Hold(.2f);}
static void Golem(Sim& s){s.MoveR({.165f,.12f,-.45f},.3f);s.Hold(.2f);}
static void GolemLeft(Sim& s){s.MoveL({-.165f,.12f,-.45f},.3f);s.Hold(.2f);}
static void AmplifyDamage(Sim& s){s.MoveR({.15f,-.30f,-.08f},.35f);s.Hold(.1f);s.MoveR({.15f,-.28f,-.55f},.15f);s.Hold(.2f);}
static void LowerResist(Sim& s){s.MoveL({-.17f,-.30f,-.08f},.35f);s.Hold(.1f);s.MoveL({-.17f,-.28f,-.55f},.15f);s.Hold(.2f);}
static void PoisonNova(Sim& s){s.Move({.08f,-.38f,-.25f},{-.08f,-.38f,-.25f},.35f);s.Move({.55f,-.60f,-.20f},{-.55f,-.60f,-.20f},.25f);s.Hold(.2f);}
static void BoneWall(Sim& s){s.MoveR({-.20f,-.30f,-.40f},.35f);s.Hold(.1f);s.MoveR({.24f,-.30f,-.40f},.2f);s.Hold(.2f);}
static void PoisonExplosion(Sim& s){s.Move({.15f,-.25f,-.35f},{-.15f,-.25f,-.35f},.35f);s.Hold(.1f);s.Move({.15f,-.55f,-.35f},{-.15f,-.55f,-.35f},.15f);s.Hold(.2f);}
static void RaiseMage(Sim& s){s.Move({.25f,-.62f,-.15f},{-.25f,-.62f,-.15f},.3f);s.Move({.55f,.20f,-.20f},{-.55f,.20f,-.20f},.3f);s.Hold(.2f);}
static void BonePrison(Sim& s){s.Move({.35f,-.30f,-.40f},{-.35f,-.30f,-.40f},.4f);s.Hold(.1f);s.Move({.06f,-.30f,-.42f},{-.06f,-.30f,-.42f},.3f);s.Hold(.2f);}
// Things a Necromancer does with both grips held that are none of them.
static void Rest(Sim& s){s.Hold(2.f);}
static void Walk(Sim& s){s.Drive(3.f,[](float t,math::Vec& r,math::Vec& l){const float w=std::sin(t*6.2832f);
    r={.25f,-.6f+.03f*w*w,-.15f-.2f*w};l={-.25f,-.6f+.03f*w*w,-.15f+.2f*w};});}
static void BeltReach(Sim& s){s.MoveR({.22f,-.78f,.05f},.4f);s.Hold(.2f);s.MoveR(RestR,.4f);s.MoveL({-.22f,-.78f,.05f},.4f);s.Hold(.2f);s.MoveL(RestL,.4f);s.Hold(.3f);}
static void WandFlick(Sim& s){s.MoveR({.20f,-.40f,-.35f},.25f);s.MoveR({.20f,-.38f,-.55f},.12f);s.MoveR(RestR,.3f);s.Hold(.3f);}
// Lazy versions of the motions, every one of which must be quick (a tester, 2026-11-05: "All motions should be
// quick and repeatable sequences that don't need you to move slow").
static void SlowGolem(Sim& s){s.MoveR({.17f,.12f,-.45f},1.4f);s.Hold(.2f);}
static void SlowNova(Sim& s){s.Move({.08f,-.38f,-.25f},{-.08f,-.38f,-.25f},.35f);s.Move({.55f,-.60f,-.20f},{-.55f,-.60f,-.20f},2.f);s.Hold(.2f);}
static void SlowPair(Sim& s){s.Move({.15f,-.30f,-.08f},{-.15f,-.30f,-.08f},.35f);s.Hold(.1f);s.Move({.15f,-.28f,-.55f},{-.15f,-.28f,-.55f},.8f);s.Hold(.2f);}
static void SlowPunch(Sim& s){s.MoveR({.15f,-.30f,-.08f},.35f);s.Hold(.1f);s.MoveR({.15f,-.28f,-.55f},.8f);s.Hold(.2f);}
static void SlowLeftPunch(Sim& s){s.MoveL({-.15f,-.30f,-.08f},.35f);s.Hold(.1f);s.MoveL({-.15f,-.28f,-.55f},.8f);s.Hold(.2f);}
static void SlowWall(Sim& s){s.MoveR({-.20f,-.30f,-.40f},.35f);s.Hold(.1f);s.MoveR({.24f,-.30f,-.40f},.9f);s.Hold(.2f);}
static void SlowPush(Sim& s){s.Move({.15f,-.25f,-.35f},{-.15f,-.25f,-.35f},.35f);s.Hold(.1f);s.Move({.15f,-.55f,-.35f},{-.15f,-.55f,-.35f},.9f);s.Hold(.2f);}
static void SlowMage(Sim& s){s.Move({.25f,-.62f,-.15f},{-.25f,-.62f,-.15f},.3f);s.Move({.55f,.20f,-.20f},{-.55f,.20f,-.20f},1.4f);s.Hold(.2f);}
static void SlowPrison(Sim& s){s.Move({.35f,-.30f,-.40f},{-.35f,-.30f,-.40f},.4f);s.Hold(.1f);s.Move({.06f,-.30f,-.42f},{-.06f,-.30f,-.42f},1.5f);s.Hold(.2f);}
static void SlowRaise(Sim& s){s.Move({.18f,-.62f,-.15f},{-.18f,-.62f,-.15f},.3f);s.Move({.22f,.18f,-.22f},{-.22f,.18f,-.22f},1.4f);s.Hold(.2f);}
static void ShortArmor(Sim& s){s.Move({-.10f,-.35f,-.10f},{.10f,-.35f,-.10f},.4f);s.Hold(.2f);s.Move(RestR,RestL,.4f);}
static void PointBack(Sim& s){Point(s);s.MoveR(RestR,.5f);s.Hold(.3f);}   // held out, then lowered back to rest
int main() {
    using Do=void(*)(Sim&);
    // aimed: it goes straight ahead (the golems' spot, Lower Resist along the left arm), level
    const struct {Motion motion;Do perform;const char* name;bool aimed;} all[]={
        {Motion::BoneArmor,BoneArmor,"Bone Armor",false},{Motion::RaiseSkeleton,RaiseSkeleton,"Raise Skeleton",false},
        {Motion::CorpseExplosion,CorpseExplosion,"Corpse Explosion",false},{Motion::Golem,Golem,"Golem",true},
        {Motion::GolemLeft,GolemLeft,"Golem (left arm)",true},
        {Motion::AmplifyDamage,AmplifyDamage,"Amplify Damage",false},{Motion::LowerResist,LowerResist,"Lower Resist",true},
        {Motion::PoisonNova,PoisonNova,"Poison Nova",false},{Motion::BonePrison,BonePrison,"Bone Prison",false},
        {Motion::BoneWall,BoneWall,"Bone Wall",true},{Motion::PoisonExplosion,PoisonExplosion,"Poison Explosion",false},
        {Motion::RaiseMage,RaiseMage,"Raise Skeletal Mage",false}};
    Check(std::size(all)==std::size(spellmotion::necromancer::Order),"every motion of the Necromancer's is tested");
    for(const auto& m:all) {
        const std::string label=std::string(m.name)+": ";
        Check(std::string(spellmotion::Cue(m.motion))!="no motion",(label+"it has a cue").c_str());
        for(float heading:{-2.f,0.f,2.f}) {
            Sim s(m.motion,heading);m.perform(s);
            Check(s.casts==1,(label+"done well, it casts once, at any heading").c_str());
            if(m.aimed)Check(s.aim.z<-.99f&&std::fabs(s.aim.x)<.05f&&std::fabs(s.aim.y)<.01f,(label+"it goes straight ahead, level").c_str());
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
        {Motion::PoisonNova,SlowNova,"Poison Nova: the arms spread slowly cast nothing"},
        {Motion::CorpseExplosion,SlowPair,"Corpse Explosion: both fists pushed out slowly cast nothing"},
        {Motion::AmplifyDamage,SlowPunch,"Amplify Damage: a slow push out casts nothing"},
        {Motion::LowerResist,SlowLeftPunch,"Lower Resist: a slow push out casts nothing"},
        {Motion::BonePrison,SlowPrison,"Bone Prison: the hands closed slowly cast nothing"},
        {Motion::BoneWall,SlowWall,"Bone Wall: a slow sweep casts nothing"},
        {Motion::PoisonExplosion,SlowPush,"Poison Explosion: the fists lowered slowly cast nothing"},
        {Motion::RaiseMage,SlowMage,"Raise Skeletal Mage: a slow lift casts nothing"},
        {Motion::RaiseSkeleton,SlowRaise,"Raise Skeleton: a slow lift casts nothing"},
        {Motion::BoneArmor,ShortArmor,"Bone Armor: crossed for a moment casts nothing"}};
    for(const auto& l:lazy){Sim s(l.motion);l.perform(s);Check(s.casts==0,l.label);}
    {   // the owner's headset session (2026-11-05): what was missed then casts now, and their calls
        Sim nova(Motion::PoisonNova);nova.Move({.08f,-.38f,-.25f},{-.08f,-.38f,-.25f},.35f);nova.Move({.70f,-.15f,-.10f},{-.68f,-.12f,-.15f},.25f);nova.Hold(.2f);
        Check(nova.casts==1,"Poison Nova: flung 138 cm wide at shoulder height casts");
        for(const auto m:{Motion::RaiseSkeleton,Motion::RaiseMage}) {   // the owner's 58 cm, and a V
            Sim narrow(m);narrow.Move({.30f,-.62f,-.10f},{-.30f,-.62f,-.10f},.3f);narrow.Move({.29f,.25f,-.15f},{-.29f,.25f,-.15f},.3f);
            Check(narrow.casts==(m==Motion::RaiseSkeleton?1:0),"both arms swept up 58 cm apart past the face: Raise Skeleton's, no stop");
            Sim wide(m);wide.Move({.30f,-.62f,-.10f},{-.30f,-.62f,-.10f},.3f);wide.Move({.50f,.25f,-.15f},{-.50f,.25f,-.15f},.3f);
            Check(wide.casts==(m==Motion::RaiseMage?1:0),"both arms swept up a metre apart past the face: Raise Skeletal Mage's");
        }
        Sim on(Motion::RaiseSkeleton);on.Move({.18f,-.62f,-.15f},{-.18f,-.62f,-.15f},.3f);on.Move({.18f,.40f,-.10f},{-.18f,.40f,-.10f},.3f);on.Move({.18f,-.62f,-.15f},{-.18f,-.62f,-.15f},.3f);
        Check(on.casts==1,"Raise Skeleton: swept on overhead and back down, once");
        for(const auto m:{Motion::Golem,Motion::GolemLeft}){Sim both(m);RaiseSkeleton(both);Check(both.casts==0,"a golem: both arms up is Raise Skeleton's");}
        {Sim one(Motion::RaiseSkeleton);Golem(one);GolemLeft(one);Check(one.casts==0,"Raise Skeleton: one arm up, then the other, is no Raise Skeleton");}
        {Sim left(Motion::Golem);GolemLeft(left);Check(left.casts==0,"the right arm's golem: the left arm up is the other card's");}
        Sim punch(Motion::AmplifyDamage);punch.MoveR({.20f,-.20f,-.05f},.35f);punch.MoveR({.18f,-.18f,-.50f},.15f);punch.Hold(.2f);
        Check(punch.casts==1,"Amplify Damage: a punch straight from the chest, no pause, casts");
    }
    {   // the owner's headset session (2026-11-05): the pointing starts were punches ("It actually accidentally
        // triggers amplified damage again"); now a punch of each fist and of both, kept apart, and the golem
        // raised in front ("not beside our head, that's silly")
        for(const float lag:{0.f,.04f,.08f}) {   // both fists, the left a moment behind
            for(const auto m:{Motion::AmplifyDamage,Motion::LowerResist,Motion::CorpseExplosion}) {
                Sim p(m);p.Move({.15f,-.30f,-.08f},{-.15f,-.30f,-.08f},.35f);p.Hold(.1f);
                if(lag>0)p.MoveR({.15f,-.29f,-.08f-.47f*lag/.15f},lag);
                p.Move({.15f,-.28f,-.55f},{-.15f,-.28f,-.55f},.15f);p.Hold(.2f);
                Check(p.casts==(m==Motion::CorpseExplosion?1:0),"both fists punched together, one a little ahead: Corpse Explosion's alone");
            }
        }
        {Sim held(Motion::AmplifyDamage);held.MoveL({-.15f,-.30f,-.50f},.4f);held.Hold(.4f);AmplifyDamage(held);
         Check(held.casts==1,"Amplify Damage: the right punch with the left hand already held out casts");}
        {Sim from(Motion::AmplifyDamage);from.MoveR({.18f,-.62f,-.10f},.3f);from.MoveR({.17f,.12f,-.45f},.3f);
         Check(from.casts==0,"Amplify Damage: the right arm swept up in front (a golem) is no punch");}
        {Sim from(Motion::Golem);AmplifyDamage(from);Check(from.casts==0,"Golem: the punch is no golem");}
        {Sim aside(Motion::Golem);aside.MoveR({.45f,.10f,-.40f},.3f);aside.Hold(.2f);
         Check(aside.casts==1&&aside.spot.right>1&&aside.spot.ahead>1,"Golem: swept up toward the front right, it stands there");}
        {Sim aside(Motion::GolemLeft);aside.MoveL({-.45f,.10f,-.40f},.3f);aside.Hold(.2f);
         Check(aside.casts==1&&aside.spot.right<-1&&aside.spot.ahead>1,"Golem (left arm): swept up toward the front left, it stands there");}
        {Sim back(Motion::BoneWall);back.MoveR({.30f,-.30f,-.40f},.35f);back.Hold(.1f);back.MoveR({-.16f,-.30f,-.40f},.2f);back.Hold(.2f);
         Check(back.casts==1,"Bone Wall: swept right to left as well");}
        {Sim aside(Motion::BoneWall);aside.MoveR({.10f,-.30f,-.40f},.35f);aside.Hold(.1f);aside.MoveR({.55f,-.30f,-.30f},.2f);aside.Hold(.2f);
         Check(aside.casts==1&&aside.spot.right>1,"Bone Wall: swept across the front right, it stands there");}
        {Sim both(Motion::BoneWall);both.Move({-.20f,-.30f,-.40f},{-.40f,-.30f,-.40f},.35f);both.Hold(.1f);both.Move({.24f,-.30f,-.40f},{.04f,-.30f,-.40f},.2f);
         Check(both.casts==0,"Bone Wall: both hands swept across is none");}
        {Sim low(Motion::BoneWall);low.MoveR({-.20f,-.70f,-.30f},.35f);low.Hold(.1f);low.MoveR({.24f,-.70f,-.30f},.2f);
         Check(low.casts==0,"Bone Wall: swept by the hips is none");}
        {Sim drop(Motion::PoisonExplosion);RaiseSkeleton(drop);drop.Move({.18f,-.62f,-.30f},{-.18f,-.62f,-.30f},.25f);drop.Hold(.2f);
         Check(drop.casts==0,"Poison Explosion: the arms let down in front after Raise Skeleton are no push");}
        {Sim fling(Motion::PoisonExplosion);PoisonNova(fling);Check(fling.casts==0,"Poison Explosion: Poison Nova's fling is no push");}
        {   // the owner's first pushes (2026-11-05): 42-44 cm from the chin, 30 from the eyes, 17-20 cm coming in to 9 cm ahead
            Sim chin(Motion::PoisonExplosion);chin.Move({.17f,-.04f,-.30f},{-.14f,-.04f,-.30f},.35f);chin.Hold(.1f);
            chin.Move({.17f,-.47f,-.25f},{-.14f,-.48f,-.27f},.15f);chin.Hold(.2f);
            Check(chin.casts==1,"Poison Explosion: pushed 43 cm down from the chin casts");
            Sim eyes(Motion::PoisonExplosion);eyes.Move({.18f,.02f,-.28f},{-.17f,.02f,-.27f},.35f);eyes.Hold(.1f);
            eyes.Move({.18f,-.30f,-.26f},{-.17f,-.30f,-.25f},.15f);eyes.Hold(.2f);
            Check(eyes.casts==1,"Poison Explosion: pushed 32 cm down from the eyes casts");
            Sim short_(Motion::PoisonExplosion);short_.Move({.21f,-.27f,-.20f},{-.15f,-.25f,-.24f},.35f);short_.Hold(.1f);
            short_.Move({.21f,-.45f,-.09f},{-.15f,-.45f,-.15f},.12f);short_.Hold(.2f);
            Check(short_.casts==1,"Poison Explosion: pushed 18-20 cm down, coming in to 9 cm ahead, casts");
            Sim lowered(Motion::PoisonExplosion);lowered.Move({.17f,-.20f,-.30f},{-.14f,-.20f,-.30f},.35f);lowered.Hold(.1f);
            lowered.Move({.17f,-.40f,-.28f},{-.14f,-.40f,-.28f},.4f);lowered.Hold(.2f);
            Check(lowered.casts==0,"Poison Explosion: the fists lowered at an easy pace cast nothing");
        }
        {Sim high(Motion::Golem);high.MoveR({.22f,.28f,-.05f},.3f);high.Hold(.2f);
         Check(high.casts==1&&high.spot.ahead==spellmotion::necromancer::tuning.golemAhead&&high.spot.right==0,"Golem: swept up overhead, straight ahead");}
    }
    {   // nothing low (a tester, 2026-11-05: "Please no floor stuff"), and the golem's spot ahead
        for(const auto m:{Motion::RaiseSkeleton,Motion::Golem,Motion::GolemLeft}) {
            Sim s(m);(m==Motion::Golem?Golem:m==Motion::GolemLeft?GolemLeft:RaiseSkeleton)(s);
            Check(s.casts==1&&s.g.Stats().low>-.65f,"Raise Skeleton and the golems: cast with the hands never below the waist");
        }
        Sim g(Motion::Golem);bool land=false;
        for(int i=0;i<40&&!g.casts;++i){g.MoveR({.17f,-.62f+.74f*float(i+1)/40,-.15f-.30f*float(i+1)/40},.3f/40);
            if(g.out.cast)land=g.out.land==spellmotion::Land::Spot&&g.out.spot.ahead==spellmotion::necromancer::tuning.golemAhead&&g.out.spot.right==0;}
        Check(g.casts==1&&land,"Golem: it stands on its spot ahead");
    }
    std::puts("motions-necromancer-test: ok");
    return 0;
}
