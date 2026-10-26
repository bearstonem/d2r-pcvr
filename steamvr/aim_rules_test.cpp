#include "aim_rules.h"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
using namespace framevr::aimrules;
static void Check(bool b,const char* why){if(!b){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
int main() {
    Check(Of(36,true)==Use::Heading&&Of(12,true)==Use::Heading,"bolts and fans follow the target's heading");
    Check(Of(59,false)==Use::Ground&&Of(251,true)==Use::Ground&&Of(78,false)==Use::Ground&&Of(88,false)==Use::Ground,"Blizzard, Fire Blast and the bone walls are ground skills");
    Check(Of(66,false)==Use::Curse&&Of(87,false)==Use::Curse&&Of(91,false)==Use::Curse,"Necromancer curses centre on the target");
    Check(Of(107,false)==Use::Attack&&Of(143,false)==Use::Attack&&Of(275,false)==Use::Attack&&Of(151,false)==Use::Attack,"Charge, Leap Attack, Dragon Flight, Whirlwind attack the target");
    Check(Of(54,false)==Use::Spot&&Of(132,false)==Use::Spot&&Of(70,false)==Use::Spot,"Teleport, Leap and Raise Skeleton go to the pointed spot");
    Check(Of(74,false)==Use::CorpseAtTarget,"Corpse Explosion goes to the target");
    Check(Of(26,true)==Use::Native&&Of(149,false)==Use::Native,"Strafe and self skills are the game's");
    Check(Placed(Use::Curse)&&Placed(Use::Spot)&&!Placed(Use::Heading)&&!Placed(Use::Native),"curses and spots are placed, bolts are not");
    Check(TakesTarget(Use::Attack)&&!TakesTarget(Use::Spot)&&!TakesTarget(Use::Native),"travel spots never take the target");
    Check(Circle(Use::Ground)&&!Circle(Use::Curse),"curses are instant: no landing circle");
    Check(FrontOfTarget(78)==3&&FrontOfTarget(88)==0&&FrontOfTarget(59)==0,"Bone Wall in front of the target, Bone Prison on it");
    Check(WantsUnit(22)&&WantsUnit(93)&&WantsUnit(107)&&!WantsUnit(36)&&!WantsUnit(54)&&!WantsUnit(66),"seekers and Charge get the monster; bolts, Teleport and curses do not");
    Check(!WantsUnit(121)&&!WantsUnit(106),"Fist of the Heavens and Zeal stay with the game's own pick");
    Check(HandOffLive(22,1000,1500)&&!HandOffLive(22,1000,1501)&&!HandOffLive(-1,1000,1000),"a hand-off answers for 500 ms, and none is never live");
    Check(DropOnRelease(0,0)&&DropOnRelease(1,1)&&!DropOnRelease(0,1)&&!DropOnRelease(-1,0)&&!DropOnRelease(-1,1),
          "letting go drops only that button's own hand-off; one cast with the button up waits out its 500 ms");
    Check(KeepHeld(true,10,-44)&&KeepHeld(true,45,45)&&!KeepHeld(true,45.5f,0)&&!KeepHeld(true,0,-46)&&!KeepHeld(false,1,1),
          "a held target stays inside 45 cells on either axis, corners too, and lets go past it or once it is gone");
    const auto right=For(0,false),left=For(1,false),bow=For(0,true);
    Check(right.at[0]==Right&&right.at[1]==Left&&right.at[2]==Eyes,"a right button: right, left, eyes");
    Check(left.at[0]==Left&&left.at[1]==Right&&left.at[2]==Eyes,"a left button: left, right, eyes");
    Check(bow.at[0]==Left&&bow.at[1]==Right,"a bow skill on a right button: the bow hand first");
    for(const Use use:{Use::Heading,Use::Ground,Use::Curse,Use::Attack,Use::CorpseAtTarget})
        for(const bool other:{false,true})
            Check(Tries(false,false,use,other)==3&&Tries(false,true,use,other)==3,"Aim follows your view Off: every cast tries its hand, the other hand, the eyes");
    for(const Use use:{Use::Heading,Use::Ground,Use::Curse,Use::CorpseAtTarget})
        for(const bool other:{false,true})
            Check(Tries(true,false,use,other)==3&&Tries(true,true,use,other)==1,"On: a raised, pointing first hand tries its own monster only; a lowered one the whole order");
    Check(Tries(true,false,Use::Attack,false)==3&&Tries(true,false,Use::Attack,true)==3,"On: a melee attack from a lowered hand tries the whole order");
    Check(Tries(true,true,Use::Attack,true)==2,"On: a melee attack from a raised hand also tries the other hand's outlined monster, never the eyes'");
    Check(Tries(true,true,Use::Attack,false)==1,"On: a melee attack from a raised hand skips the other hand's monster when it is not outlined");
    Check(OtherOutlined(true,true,0,Left)&&OtherOutlined(true,true,1,Right),"Both hands: the other hand's monster is outlined");
    Check(OtherOutlined(true,false,1,Left)&&!OtherOutlined(true,false,0,Left)&&!OtherOutlined(true,false,1,Right),"Dominant hand: only the dominant hand's monster is outlined");
    Check(!OtherOutlined(false,true,0,Left)&&!OtherOutlined(false,false,1,Left),"Monster outline Off: nothing is outlined");
    Check(Of(107,false)==Use::Attack&&Of(143,false)==Use::Attack&&Of(275,false)==Use::Attack&&Of(151,false)==Use::Attack,"the melee attacks: Charge, Leap Attack, Dragon Flight, Whirlwind");
    Check(EyesOutlined(false,true)&&EyesOutlined(true,false)&&!EyesOutlined(true,true),"On, the eyes' monster is not outlined while a hand points");
    Check(Stable(373)&&Stable(402)&&!Stable(372)&&!Stable(403)&&Of(395,true)==Use::Native&&Of(388,false)==Use::Native,"the Warlock keeps upstream's aim: no rule, even for his missiles");
    {
        const Switches off{},spells{true,false,0},bows{false,true,0},point{false,false,1},monster{false,false,2};
        for(const Use use:{Use::Native,Use::Heading,Use::Ground,Use::Curse,Use::Attack,Use::Spot,Use::CorpseAtTarget})
            for(const bool bow:{false,true})Check(!RulesAim(use,bow,off)&&!MonsterSwitch(use,bow,off),"every option Off: no cast is aimed by the rules");
        Check(RulesAim(Use::Heading,false,spells)&&RulesAim(Use::Curse,false,spells)&&!RulesAim(Use::Heading,true,spells)&&!RulesAim(Use::Ground,false,spells),
              "Spells: spells only, not bow skills or ground skills");
        Check(RulesAim(Use::Heading,true,bows)&&!RulesAim(Use::Heading,false,bows)&&MonsterSwitch(Use::Heading,true,bows),"Bow skills: bow skills only");
        Check(RulesAim(Use::Ground,false,point)&&!MonsterSwitch(Use::Ground,false,point)&&!RulesAim(Use::Heading,false,point),
              "Ground skills Where you point: placed by the rules, never on a monster; other casts untouched");
        Check(RulesAim(Use::Ground,false,monster)&&MonsterSwitch(Use::Ground,false,monster),"Ground skills Aimed monster: on the monster");
        Check(!RulesAim(Use::Native,false,{true,true,2}),"a skill without a rule (the Warlock's, self skills) is never the rules'");
        Check(!Any(off)&&Any(spells)&&Any(bows)&&Any(point)&&Any(monster),"any option On wakes the rules; all Off, none of them runs");
    }
    std::puts("aim rules ok");
    return 0;
}
