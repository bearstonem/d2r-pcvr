#include "strike_skills.h"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
using namespace framevr::strikeskills;
static int failures=0;
static void Check(bool ok,const char* what){if(!ok){std::printf("FAIL: %s\n",what);++failures;}}
int main() {
    for(int s:{Maul,FeralRage,Rabies,FireClaws,Hunger,Fury})Check(Known(s),"the Druid's form skills strike");
    for(int s:{PowerStrike,Impale,ChargedStrike,LightningStrike,PoisonDagger,Sacrifice,Vengeance,Conversion,Bash,Stun,Concentrate,Berserk})
        Check(Known(s)&&!MultiHit(s),"the single-hit weapon skills strike");
    Check(Known(Zeal)&&MultiHit(Zeal)&&Matches(Zeal,37,13)&&!Matches(Zeal,37,2),"Zeal strikes as a multi-hit skill, on Fury's functions");
    for(int s:{0,10,97,107,151,254})Check(!Known(s),"Attack, Jab, Smite, Charge, Whirlwind and Tiger Strike do not (yet)");
    Check(Known(DoubleSwing)&&Known(Frenzy)&&DualStrike(DoubleSwing)&&DualStrike(Frenzy)&&!MultiHit(Frenzy)&&!DualStrike(Bash)&&!DualStrike(Fury),
          "Double Swing and Frenzy strike as dual strike skills");
    Check(Matches(DoubleSwing,68,70)&&Matches(Frenzy,67,9)&&!Matches(Frenzy,67,2),"their own start and do rows");
    Check(Matches(Maul,56,120)&&Matches(FeralRage,56,120)&&Matches(Rabies,57,121)&&Matches(FireClaws,58,2)&&Matches(Hunger,0,122)&&Matches(Fury,37,13),
          "each skill's compiled start/do functions");
    Check(!Matches(Fury,9,13)&&!Matches(Fury,37,2),"Fury with another start or do is refused");
    Check(Matches(Bash,32,2)&&Matches(Stun,32,2)&&Matches(Concentrate,32,2)&&Matches(Berserk,39,2)&&Matches(Sacrifice,29,64)&&Matches(Vengeance,35,2)&&
          Matches(PowerStrike,6,2)&&Matches(Impale,7,2)&&Matches(ChargedStrike,6,11)&&Matches(LightningStrike,10,14)&&Matches(PoisonDagger,16,32)&&
          Matches(Conversion,32,79),
          "each weapon skill's compiled start/do functions (Skills.txt)");
    Check(!Matches(Bash,1,2)&&!Matches(ChargedStrike,6,2)&&!Matches(PowerStrike,6,11),"a weapon skill's row that differs is refused");
    Check(MultiHit(Fury)&&MultiHit(Zeal)&&!MultiHit(Maul)&&!MultiHit(Hunger)&&!MultiHit(-1),"only Fury and Zeal are multi-hit");
    Check(!Matches(Hunger,56,122)&&!Matches(FireClaws,58,120),"a start or do that differs is refused");
    Check(!Matches(Maul,56,2)&&!Matches(Maul,0,120)&&!Matches(97,0,150),"a changed row or another skill is refused");
    Check(Armed(true,true,true,true,true,Maul)==Maul,"a confirmed trigger skill arms the hand");
    Check(Armed(false,true,true,true,true,Maul)==-1,"skill gestures off: basic strikes");
    Check(Armed(true,false,true,true,true,Maul)==-1&&Armed(true,true,false,true,true,Maul)==-1&&
          Armed(true,true,true,false,true,Maul)==-1&&Armed(true,true,true,true,false,Maul)==-1,"stale, unknown, unavailable or unconfirmed: basic strikes");
    Check(Armed(true,true,true,true,true,Bash)==Bash,"a weapon skill arms the hand too");
    Check(Armed(true,true,true,true,true,151)==-1&&Armed(true,true,true,true,true,Frenzy)==Frenzy,"a skill not in the list (Whirlwind): basic strikes; Frenzy arms");
    Check(FromUseState(0)==Refusal::None&&FromUseState(1)==Refusal::Mana&&FromUseState(2)==Refusal::Weapon&&
          FromUseState(4)==Refusal::Form&&FromUseState(8)==Refusal::Delay,"native use states");
    for(int s:{3,5,6,7,9,-1})Check(FromUseState(s)==Refusal::Other,"other use states");
    Check(Voice(Refusal::Mana)&&!Voice(Refusal::Form)&&!Voice(Refusal::Weapon)&&!Voice(Refusal::Delay)&&!Voice(Refusal::None),"only mana speaks");
    Check(Casts(Maul,false,true)&&!Casts(Maul,false,false),"Unleashed: full-cycle first contact only");
    Check(Casts(Maul,true,false)&&Casts(Maul,true,true),"Classic: every paced contact");
    Check(!Casts(-1,true,true)&&!Casts(-1,false,true),"unarmed hand never casts");
    Check(LeftChoice(0)==LeftStrikes::Own&&LeftChoice(1)==LeftStrikes::SameAsRight&&LeftChoice(2)==LeftStrikes::Own&&LeftChoice(7)==LeftStrikes::Own,
          "left-hand strikes choices; the retired Basic (2) or an unknown value is its own trigger");
    Check(StrikeTrigger(0,LeftStrikes::Own)==0&&StrikeTrigger(0,LeftStrikes::SameAsRight)==0,
          "the right hand always follows the right trigger");
    Check(StrikeTrigger(1,LeftStrikes::Own)==1&&StrikeTrigger(1,LeftStrikes::SameAsRight)==0,
          "the left hand: its own trigger or the right's");
    Check(FormVoice(1,false,true,-1,0)==WolfGrowl&&FormVoice(2,false,true,Maul,0)==BearGrowl,"a full-cycle stroke growls in either form, basic or cast");
    Check(FormVoice(1,false,true,-1,84)==WolfGrowl&&FormVoice(1,false,true,-1,85)==0&&FormVoice(2,false,true,-1,79)==BearGrowl&&FormVoice(2,false,true,-1,80)==0,
          "the native growl chances: wolf 85%, bear 80%");
    Check(FormVoice(1,false,true,Rabies,99)==WolfBite&&FormVoice(2,false,true,Hunger,99)==BearBite&&FormVoice(1,false,true,Hunger,0)==WolfBite,
          "Rabies and Hunger bite, always");
    Check(FormVoice(1,false,false,-1,0)==0&&FormVoice(2,false,false,Rabies,0)==0,"flailing and cleave follow-ups stay silent (Unleashed)");
    Check(FormVoice(1,true,false,-1,0)==WolfGrowl,"Classic: every paced strike");
    Check(FormVoice(0,true,true,-1,0)==0&&FormVoice(3,true,true,-1,0)==0,"no voice outside the forms");
    // Fury and Zeal: the native rhythm (rev4, measured from rev1's native casts).
    Check(SequenceGap(360,Fury)==160&&SequenceGap(560,Zeal)==280,"Fury 4/9 and Zeal 1/2 of the cycle: 160 ms and 280 ms as measured");
    Check(SequenceGap(0,Fury)==222&&SequenceGap(0,Zeal)==250&&SequenceGap(90,Fury)==SequenceMinGapMs,"an unknown cycle, and a floor");
    {
        Sequence q;const uint64_t s=7;const int wolf=1;
        Check(!q.Open(s)&&q.DueAt()==0,"nothing before a cast");
        q.Begin(s,Fury,0,wolf,5,360,1000,42);
        Check(q.Open(s)&&q.Left()==4&&q.Total()==5&&q.Last()==42&&q.DueAt()==1160,"the cast lands hit 1 of 5; hit 2 one gap later");
        Check(!q.Due(s,1139)&&q.Due(s,1140)&&!q.Due(8,1160),"due up to 20 ms early (the next game update), in its game");
        Check(q.Hit(1190,43)==2&&q.Last()==43&&q.DueAt()==1320,"hit 2 run a tick late: hit 3 still on the rhythm");
        Check(q.Hit(1500,44)==3&&q.DueAt()==1580,"a long stall: never sooner than half a gap");
        Check(!q.Lapse(s,wolf)&&q.Lapse(s,2)&&!q.Open(s),"leaving the form ends it");
        q.Begin(s,Zeal,0,0,3,560,5000,9);
        Check(q.DueAt()==5280&&q.Hit(5280,9)==2&&q.Hit(5560,9)==3&&!q.Open(s)&&q.DueAt()==0,"a 3-hit Zeal every 280 ms, then spent");
        q.Begin(s,Fury,0,wolf,1,360,9000,9);
        Check(!q.Open(s),"a single-hit count opens nothing");
        q.Begin(s,Fury,0,wolf,5,360,9000,9);q.End();
        Check(!q.Open(s)&&!q.Lapse(s,2),"ended: nothing left to lapse");
        q.Begin(s,Fury,0,wolf,5,360,9000,9);
        Check(q.Lapse(9,wolf)&&!q.Open(s),"a new game ends it");
    }
    {   // Dual strike skills: one cast = one swing of each blade (Cal, 2026-11-03, decision A)
        DualCast c;const uint64_t s=5;
        Check(!c.Open(s,1000)&&!c.Second(s,1,1000),"nothing open before a cast");
        c.Begin(s,Frenzy,0,1000,540);
        Check(c.Open(s,1000)&&c.Skill()==Frenzy&&c.Blade()==0,"the right blade opened it");
        Check(!c.Second(s,0,1100)&&c.Open(s,1100),"the same blade never lands hit 2");
        Check(c.Second(s,1,1150)&&!c.Open(s,1150),"the other blade's contact lands hit 2 and closes it");
        Check(!c.Second(s,1,1200)&&!c.Forfeit(s,2000),"only one hit 2; nothing to forfeit after it");
        c.Begin(s,DoubleSwing,1,3000,540);
        Check(!c.Second(s,0,3540)&&!c.Open(s,3540),"the beat over: too late");
        Check(c.Forfeit(s,3540)&&!c.Forfeit(s,3600),"forfeited once");
        c.Begin(s,DoubleSwing,1,4000,540);
        Check(!c.Second(9,0,4100)&&!c.Forfeit(s,4100),"another game never claims it; still open");
        c.End();Check(!c.Open(s,4100),"ended");
        c.Begin(s,Frenzy,0,5000,0);Check(c.Open(s,5499)&&!c.Open(s,5500),"an unknown cast length: half a second");
    }
    if(failures)return EXIT_FAILURE;
    std::printf("strike skills ok\n");return EXIT_SUCCESS;
}
