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
// The Warlock's motions take the Necromancer's shapes (motions_warlock.h): this is that class's test, shape for shape, on the
// Warlock's motions - the fixtures keep the shapes' names (BoneArmor: Psychic Ward's crossed fists, Golem: Summon Goatman's
// arm ...), and the labels below name the shape where they say a Necromancer's skill. Then what differs: where a cast lands.
// The shapes, done well, from the resting pose (head-local: +X right, +Y up, -Z forward).
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
static void Run() {
    using Do=void(*)(Sim&);
    // aimed: it goes straight ahead (the golems' spot, Lower Resist along the left arm), level
    // aimed: it has a way of its own, straight ahead and level (a demon's spot, Sigil: Death's). The sigils that land where
    // the main hand points, the hexes, the ward, the ring and the blast have none.
    const struct {Motion motion;Do perform;const char* name;bool aimed;} all[]={
        {Motion::PsychicWard,BoneArmor,"Psychic Ward",false},{Motion::SummonDefiler,RaiseSkeleton,"Summon Defiler",true},
        {Motion::SigilRancor,CorpseExplosion,"Sigil: Rancor",false},{Motion::SummonGoatman,Golem,"Summon Goatman",true},
        {Motion::SummonTainted,GolemLeft,"Summon Tainted",true},
        {Motion::SigilLethargy,AmplifyDamage,"Sigil: Lethargy",false},{Motion::HexBane,LowerResist,"Hex: Bane",false},
        {Motion::RingOfFire,PoisonNova,"Ring of Fire",false},{Motion::HexSiphon,BonePrison,"Hex: Siphon",false},
        {Motion::SigilDeath,BoneWall,"Sigil: Death",true},{Motion::HexPurge,PoisonExplosion,"Hex: Purge",false},
        {Motion::EldritchBlast,RaiseMage,"Eldritch Blast",false}};
    Check(std::size(all)==std::size(spellmotion::warlock::Order)&&std::size(all)==size_t(spellmotion::PageOf(7).count),"every motion of the Warlock's is tested");
    for(const auto& m:all)Check(spellmotion::warlock::Shape(m.motion)!=Motion::None,"every one takes a shape of the Necromancer's");
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
            if(m.motion==Motion::SigilLethargy&&none==WandFlick)continue;
            Sim s(m.motion);none(s);
            Check(s.casts==0,(label+"resting, walking, a belt reach, a wand flick or lowering the arm casts nothing").c_str());
        }
    }
    const struct {Motion motion;Do perform;const char* label;} lazy[]={
        {Motion::SummonGoatman,SlowGolem,"Golem: the arm raised slowly casts nothing"},
        {Motion::RingOfFire,SlowNova,"Poison Nova: the arms spread slowly cast nothing"},
        {Motion::SigilRancor,SlowPair,"Corpse Explosion: both fists pushed out slowly cast nothing"},
        {Motion::SigilLethargy,SlowPunch,"Amplify Damage: a slow push out casts nothing"},
        {Motion::HexBane,SlowLeftPunch,"Lower Resist: a slow push out casts nothing"},
        {Motion::HexSiphon,SlowPrison,"Bone Prison: the hands closed slowly cast nothing"},
        {Motion::SigilDeath,SlowWall,"Bone Wall: a slow sweep casts nothing"},
        {Motion::HexPurge,SlowPush,"Poison Explosion: the fists lowered slowly cast nothing"},
        {Motion::EldritchBlast,SlowMage,"Raise Skeletal Mage: a slow lift casts nothing"},
        {Motion::SummonDefiler,SlowRaise,"Raise Skeleton: a slow lift casts nothing"},
        {Motion::PsychicWard,ShortArmor,"Bone Armor: crossed for a moment casts nothing"}};
    for(const auto& l:lazy){Sim s(l.motion);l.perform(s);Check(s.casts==0,l.label);}
    {   // the owner's headset session (2026-11-05): what was missed then casts now, and their calls
        Sim nova(Motion::RingOfFire);nova.Move({.08f,-.38f,-.25f},{-.08f,-.38f,-.25f},.35f);nova.Move({.70f,-.15f,-.10f},{-.68f,-.12f,-.15f},.25f);nova.Hold(.2f);
        Check(nova.casts==1,"Poison Nova: flung 138 cm wide at shoulder height casts");
        for(const auto m:{Motion::SummonDefiler,Motion::EldritchBlast}) {   // the owner's 58 cm, and a V
            Sim narrow(m);narrow.Move({.30f,-.62f,-.10f},{-.30f,-.62f,-.10f},.3f);narrow.Move({.29f,.25f,-.15f},{-.29f,.25f,-.15f},.3f);
            Check(narrow.casts==(m==Motion::SummonDefiler?1:0),"both arms swept up 58 cm apart past the face: Raise Skeleton's, no stop");
            Sim wide(m);wide.Move({.30f,-.62f,-.10f},{-.30f,-.62f,-.10f},.3f);wide.Move({.50f,.25f,-.15f},{-.50f,.25f,-.15f},.3f);
            Check(wide.casts==(m==Motion::EldritchBlast?1:0),"both arms swept up a metre apart past the face: Raise Skeletal Mage's");
        }
        Sim on(Motion::SummonDefiler);on.Move({.18f,-.62f,-.15f},{-.18f,-.62f,-.15f},.3f);on.Move({.18f,.40f,-.10f},{-.18f,.40f,-.10f},.3f);on.Move({.18f,-.62f,-.15f},{-.18f,-.62f,-.15f},.3f);
        Check(on.casts==1,"Raise Skeleton: swept on overhead and back down, once");
        for(const auto m:{Motion::SummonGoatman,Motion::SummonTainted}){Sim both(m);RaiseSkeleton(both);Check(both.casts==0,"a golem: both arms up is Raise Skeleton's");}
        {Sim one(Motion::SummonDefiler);Golem(one);GolemLeft(one);Check(one.casts==0,"Raise Skeleton: one arm up, then the other, is no Raise Skeleton");}
        {Sim left(Motion::SummonGoatman);GolemLeft(left);Check(left.casts==0,"the right arm's golem: the left arm up is the other card's");}
        Sim punch(Motion::SigilLethargy);punch.MoveR({.20f,-.20f,-.05f},.35f);punch.MoveR({.18f,-.18f,-.50f},.15f);punch.Hold(.2f);
        Check(punch.casts==1,"Amplify Damage: a punch straight from the chest, no pause, casts");
    }
    {   // the owner's headset session (2026-11-05): the pointing starts were punches ("It actually accidentally
        // triggers amplified damage again"); now a punch of each fist and of both, kept apart, and the golem
        // raised in front ("not beside our head, that's silly")
        for(const float lag:{0.f,.04f,.08f}) {   // both fists, the left a moment behind
            for(const auto m:{Motion::SigilLethargy,Motion::HexBane,Motion::SigilRancor}) {
                Sim p(m);p.Move({.15f,-.30f,-.08f},{-.15f,-.30f,-.08f},.35f);p.Hold(.1f);
                if(lag>0)p.MoveR({.15f,-.29f,-.08f-.47f*lag/.15f},lag);
                p.Move({.15f,-.28f,-.55f},{-.15f,-.28f,-.55f},.15f);p.Hold(.2f);
                Check(p.casts==(m==Motion::SigilRancor?1:0),"both fists punched together, one a little ahead: Corpse Explosion's alone");
            }
        }
        {Sim held(Motion::SigilLethargy);held.MoveL({-.15f,-.30f,-.50f},.4f);held.Hold(.4f);AmplifyDamage(held);
         Check(held.casts==1,"Amplify Damage: the right punch with the left hand already held out casts");}
        {Sim from(Motion::SigilLethargy);from.MoveR({.18f,-.62f,-.10f},.3f);from.MoveR({.17f,.12f,-.45f},.3f);
         Check(from.casts==0,"Amplify Damage: the right arm swept up in front (a golem) is no punch");}
        {Sim from(Motion::SummonGoatman);AmplifyDamage(from);Check(from.casts==0,"Golem: the punch is no golem");}
        {Sim aside(Motion::SummonGoatman);aside.MoveR({.45f,.10f,-.40f},.3f);aside.Hold(.2f);
         Check(aside.casts==1&&aside.spot.right>1&&aside.spot.ahead>1,"Golem: swept up toward the front right, it stands there");}
        {Sim aside(Motion::SummonTainted);aside.MoveL({-.45f,.10f,-.40f},.3f);aside.Hold(.2f);
         Check(aside.casts==1&&aside.spot.right<-1&&aside.spot.ahead>1,"Golem (left arm): swept up toward the front left, it stands there");}
        {Sim back(Motion::SigilDeath);back.MoveR({.30f,-.30f,-.40f},.35f);back.Hold(.1f);back.MoveR({-.16f,-.30f,-.40f},.2f);back.Hold(.2f);
         Check(back.casts==1,"Bone Wall: swept right to left as well");}
        {Sim aside(Motion::SigilDeath);aside.MoveR({.10f,-.30f,-.40f},.35f);aside.Hold(.1f);aside.MoveR({.55f,-.30f,-.30f},.2f);aside.Hold(.2f);
         Check(aside.casts==1&&aside.spot.right>1,"Bone Wall: swept across the front right, it stands there");}
        {Sim both(Motion::SigilDeath);both.Move({-.20f,-.30f,-.40f},{-.40f,-.30f,-.40f},.35f);both.Hold(.1f);both.Move({.24f,-.30f,-.40f},{.04f,-.30f,-.40f},.2f);
         Check(both.casts==0,"Bone Wall: both hands swept across is none");}
        {Sim low(Motion::SigilDeath);low.MoveR({-.20f,-.70f,-.30f},.35f);low.Hold(.1f);low.MoveR({.24f,-.70f,-.30f},.2f);
         Check(low.casts==0,"Bone Wall: swept by the hips is none");}
        {Sim drop(Motion::HexPurge);RaiseSkeleton(drop);drop.Move({.18f,-.62f,-.30f},{-.18f,-.62f,-.30f},.25f);drop.Hold(.2f);
         Check(drop.casts==0,"Poison Explosion: the arms let down in front after Raise Skeleton are no push");}
        {Sim fling(Motion::HexPurge);PoisonNova(fling);Check(fling.casts==0,"Poison Explosion: Poison Nova's fling is no push");}
        {   // the owner's first pushes (2026-11-05): 42-44 cm from the chin, 30 from the eyes, 17-20 cm coming in to 9 cm ahead
            Sim chin(Motion::HexPurge);chin.Move({.17f,-.04f,-.30f},{-.14f,-.04f,-.30f},.35f);chin.Hold(.1f);
            chin.Move({.17f,-.47f,-.25f},{-.14f,-.48f,-.27f},.15f);chin.Hold(.2f);
            Check(chin.casts==1,"Poison Explosion: pushed 43 cm down from the chin casts");
            Sim eyes(Motion::HexPurge);eyes.Move({.18f,.02f,-.28f},{-.17f,.02f,-.27f},.35f);eyes.Hold(.1f);
            eyes.Move({.18f,-.30f,-.26f},{-.17f,-.30f,-.25f},.15f);eyes.Hold(.2f);
            Check(eyes.casts==1,"Poison Explosion: pushed 32 cm down from the eyes casts");
            Sim hips(Motion::HexPurge);hips.Move({.28f,.04f,-.25f},{-.28f,.04f,-.22f},.35f);hips.Hold(.1f);
            hips.Move({.30f,-.70f,.08f},{-.28f,-.68f,.0f},.2f);hips.Hold(.2f);
            Check(hips.casts==1,"Poison Explosion: pushed 72 cm from the eyes to the hips, against the body, casts");
            Sim short_(Motion::HexPurge);short_.Move({.21f,-.27f,-.20f},{-.15f,-.25f,-.24f},.35f);short_.Hold(.1f);
            short_.Move({.21f,-.45f,-.09f},{-.15f,-.45f,-.15f},.12f);short_.Hold(.2f);
            Check(short_.casts==1,"Poison Explosion: pushed 18-20 cm down, coming in to 9 cm ahead, casts");
            Sim lowered(Motion::HexPurge);lowered.Move({.17f,-.20f,-.30f},{-.14f,-.20f,-.30f},.35f);lowered.Hold(.1f);
            lowered.Move({.17f,-.40f,-.28f},{-.14f,-.40f,-.28f},.4f);lowered.Hold(.2f);
            Check(lowered.casts==0,"Poison Explosion: the fists lowered at an easy pace cast nothing");
        }
        {Sim high(Motion::SummonGoatman);high.MoveR({.22f,.28f,-.05f},.3f);high.Hold(.2f);
         Check(high.casts==1&&high.spot.ahead==spellmotion::necromancer::tuning.golemAhead&&high.spot.right==0,"Golem: swept up overhead, straight ahead");}
    }
    {   // nothing low (a tester, 2026-11-05: "Please no floor stuff"), and the golem's spot ahead
        for(const auto m:{Motion::SummonDefiler,Motion::SummonGoatman,Motion::SummonTainted}) {
            Sim s(m);(m==Motion::SummonGoatman?Golem:m==Motion::SummonTainted?GolemLeft:RaiseSkeleton)(s);
            Check(s.casts==1&&s.g.Stats().low>-.65f,"Raise Skeleton and the golems: cast with the hands never below the waist");
        }
        Sim g(Motion::SummonGoatman);bool land=false;
        for(int i=0;i<40&&!g.casts;++i){g.MoveR({.17f,-.62f+.74f*float(i+1)/40,-.15f-.30f*float(i+1)/40},.3f/40);
            if(g.out.cast)land=g.out.land==spellmotion::Land::Spot&&g.out.spot.ahead==spellmotion::necromancer::tuning.golemAhead&&g.out.spot.right==0;}
        Check(g.casts==1&&land,"Golem: it stands on its spot ahead");
    }
    {   // where a Warlock's cast goes, by how the game casts the skill
        for(const auto m:{Motion::SigilLethargy,Motion::SigilRancor}) {   // laid where the pointer is, as Blizzard: where the main hand points
            Sim s(m);(m==Motion::SigilLethargy?AmplifyDamage:CorpseExplosion)(s);
            Check(s.casts==1&&s.land==spellmotion::Land::Pointed,"Sigil: Lethargy and Sigil: Rancor: where the main hand points");
        }
        {Sim s(Motion::SigilDeath);BoneWall(s);Check(s.casts==1&&s.land==spellmotion::Land::Spot&&s.spot.ahead>1,"Sigil: Death: on the spot its sweep draws, ahead");}
        {Sim s(Motion::SummonDefiler);RaiseSkeleton(s);
         Check(s.casts==1&&s.land==spellmotion::Land::Spot&&s.spot.ahead==spellmotion::warlock::Ahead&&s.spot.right==0,"Summon Defiler: on a spot straight ahead");}
        for(const auto m:{Motion::SummonGoatman,Motion::SummonTainted}) {
            Sim s(m);(m==Motion::SummonGoatman?Golem:GolemLeft)(s);
            Check(s.casts==1&&s.land==spellmotion::Land::Spot,"Summon Goatman and Summon Tainted: on the spot the arm heads for");
        }
        {Sim s(Motion::HexBane);LowerResist(s);Check(s.casts==1&&s.land==spellmotion::Land::None&&s.aim.x==0&&s.aim.y==0&&s.aim.z==0,"Hex: Bane: no way and no spot (the left arm's heading is Lower Resist's)");}
        for(const auto m:{Motion::HexPurge,Motion::HexSiphon,Motion::PsychicWard,Motion::RingOfFire,Motion::EldritchBlast}) {
            Sim s(m);(m==Motion::HexPurge?PoisonExplosion:m==Motion::HexSiphon?BonePrison:m==Motion::PsychicWard?BoneArmor:m==Motion::RingOfFire?PoisonNova:RaiseMage)(s);
            Check(s.casts==1&&s.land==spellmotion::Land::None,"a hex, the ward, the ring and the blast: no spot - they are the Warlock's own");
        }
        // the page: the game's ids, each with its motion, name and cue
        const int ids[]={373,376,377,393,396,400,385,389,386,387,384,394};
        const auto page=spellmotion::PageOf(7);
        for(int i=0;i<page.count;++i)
            Check(page.skills[i]==ids[i]&&spellmotion::Warlock(spellmotion::For(ids[i]))&&spellmotion::SpellName(ids[i])[0]&&
                  std::string(spellmotion::CueOf(ids[i]))!="no motion"&&spellmotion::Bit(7,ids[i])==i,"the Warlock's page: twelve skills, each with a motion, a name, a cue and its bit");
        for(const int id:{375,379,380,381,382,388,390,391,392,395,398,399,401,402,374,378,383,397})
            Check(spellmotion::For(id)==Motion::None,"the Warlock's other skills have no motion");
    }
}
// Everything above, right-handed as written, then played by a left-handed player (motion_left.h): the mirror
// image of every fixture, the left hand as the engine's hand 0, with the same expectations.
int main() {
    motionleft::Both(Run);
    std::puts("motions-warlock-test: ok (right-handed, and every fixture again left-handed as its mirror image)");
    return 0;
}
