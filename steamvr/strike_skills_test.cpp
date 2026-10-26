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
    Check(Known(Zeal)&&MultiHit(Zeal)&&Matches(Zeal,37,13)&&!Matches(Zeal,37,2),"Zeal strikes as a flurry, on Fury's functions");
    for(int s:{0,10,97,107,133,147,151,254})Check(!Known(s),"Attack, Jab, Smite, Charge, Double Swing, Frenzy, Whirlwind and Tiger Strike do not (yet)");
    Check(Matches(Maul,56,120)&&Matches(FeralRage,56,120)&&Matches(Rabies,57,121)&&Matches(FireClaws,58,2)&&Matches(Hunger,0,122)&&Matches(Fury,37,13),
          "each skill's compiled start/do functions");
    Check(!Matches(Fury,9,13)&&!Matches(Fury,37,2),"Fury with another start or do is refused");
    Check(Matches(Bash,32,2)&&Matches(Stun,32,2)&&Matches(Concentrate,32,2)&&Matches(Berserk,39,2)&&Matches(Sacrifice,29,64)&&Matches(Vengeance,35,2)&&
          Matches(PowerStrike,6,2)&&Matches(Impale,7,2)&&Matches(ChargedStrike,6,11)&&Matches(LightningStrike,10,14)&&Matches(PoisonDagger,16,32)&&
          Matches(Conversion,32,79),
          "each weapon skill's compiled start/do functions (Skills.txt)");
    Check(!Matches(Bash,1,2)&&!Matches(ChargedStrike,6,2)&&!Matches(PowerStrike,6,11),"a weapon skill's row that differs is refused");
    Check(MultiHit(Fury)&&MultiHit(Zeal)&&!MultiHit(Maul)&&!MultiHit(Hunger)&&!MultiHit(-1),"only Fury and Zeal are multi-hit");
    {   // Zeal: a weapon's flurry (form 0); Classic paces it at its Param2, a whole cycle.
        using F=Flurry::Feed;Flurry z;z.Begin(3,Zeal,0,5,100,1000);
        Check(z.Open(3,Zeal,0)&&z.Left()==4&&z.Floor(480,true)==480&&z.Floor(480,false)==FlurryStrokeGapMs,"Zeal's flurry and its pace");
        Check(z.Feeds(3,Zeal,0,1479,480,true)==F::TooSoon&&z.Feeds(3,Zeal,0,1480,480,true)==F::Hit&&z.Feeds(3,Zeal,0,1100,480,false)==F::Hit&&
              z.Feeds(3,Fury,0,1480,480,true)==F::None,"Classic: a full cycle; Unleashed: any later swing; another skill is not its flurry");
    }
    Check(!Matches(Hunger,56,122)&&!Matches(FireClaws,58,120),"a start or do that differs is refused");
    Check(!Matches(Maul,56,2)&&!Matches(Maul,0,120)&&!Matches(97,0,150),"a changed row or another skill is refused");
    Check(Armed(true,true,true,true,true,Maul)==Maul,"a confirmed trigger skill arms the hand");
    Check(Armed(false,true,true,true,true,Maul)==-1,"skill gestures off: basic strikes");
    Check(Armed(true,false,true,true,true,Maul)==-1&&Armed(true,true,false,true,true,Maul)==-1&&
          Armed(true,true,true,false,true,Maul)==-1&&Armed(true,true,true,true,false,Maul)==-1,"stale, unknown, unavailable or unconfirmed: basic strikes");
    Check(Armed(true,true,true,true,true,Bash)==Bash,"a weapon skill arms the hand too");
    Check(Armed(true,true,true,true,true,147)==-1,"a skill not in the list: basic strikes");
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
    // Fury flurry: 5 hits, a 480 ms cycle, a 960 ms window. Classic paces each
    // follow-up at 70 % of the cycle (336 ms); Unleashed only keeps one swipe's
    // follow-through (100 ms) from taking a second hit.
    {
        using F=Flurry::Feed;Flurry f;const uint64_t s=7;const int wolf=1;const bool classic=true,unleashed=false;
        Check(f.Feeds(s,Fury,wolf,1000,480,unleashed)==F::None&&f.Lapse(s,1000,480,wolf)==0,"no flurry before a cast");
        f.Begin(s,Fury,wolf,5,70,1000);
        Check(f.Open(s,Fury,wolf)&&f.Left()==4&&f.Total()==5,"the cast lands hit 1 of 5");
        Check(f.Floor(480,classic)==336&&f.Floor(480,unleashed)==FlurryStrokeGapMs&&f.Floor(0,unleashed)==FlurryStrokeGapMs&&
              Flurry::Window(480)==960&&Flurry::Window(0)==FlurryUnknownCycleMs,"pace floors and window");
        // Unleashed: every landed stroke takes the next hit, except one swipe's follow-through.
        Check(f.Feeds(s,Fury,wolf,1015,480,unleashed)==F::TooSoon&&f.Feeds(s,Fury,wolf,1099,480,unleashed)==F::TooSoon&&
              f.Feeds(s,Fury,wolf,1100,480,unleashed)==F::Hit,"Unleashed: a stroke 100 ms after the last hit takes the next");
        Check(f.Feeds(s,Fury,wolf,1250,480,unleashed)==F::Hit,"Unleashed: a 250 ms alternating swipe is never lost");
        // Classic: Fury's own pace.
        Check(f.Feeds(s,Fury,wolf,1335,480,classic)==F::TooSoon&&f.Feeds(s,Fury,wolf,1336,480,classic)==F::Hit,"Classic: a follow-up after 70 % of the cycle");
        Check(f.Feeds(s,Fury,wolf,999,480,unleashed)==F::TooSoon&&f.Feeds(s,Fury,wolf,999,480,classic)==F::TooSoon,"a contact timed before the last hit is too soon");
        Check(f.Feeds(s,Maul,wolf,1400,480,unleashed)==F::None&&f.Feeds(s,Fury,2,1400,480,unleashed)==F::None&&f.Feeds(8,Fury,wolf,1400,480,unleashed)==F::None,
              "another skill, form or game is not this flurry");
        // An early swipe (basic) does not move the floor: it runs from the last Fury hit.
        Check(f.Feeds(s,Fury,wolf,1200,480,classic)==F::TooSoon&&f.Feeds(s,Fury,wolf,1340,480,classic)==F::Hit,"Classic: early swipes never delay the next hit");
        Check(f.SinceHit(1340)==340&&f.SinceHit(900)==0,"time since the last hit");
        Check(f.Hit(1340)==2&&f.Left()==3,"hit 2");
        Check(f.Feeds(s,Fury,wolf,1600,480,classic)==F::TooSoon&&f.Feeds(s,Fury,wolf,1676,480,classic)==F::Hit,"Classic: the floor runs from hit 2");
        Check(f.Feeds(s,Fury,wolf,1439,480,unleashed)==F::TooSoon&&f.Feeds(s,Fury,wolf,1440,480,unleashed)==F::Hit,"Unleashed: the guard runs from hit 2");
        // Unleashed at ~250 ms alternation: hits 3-5 on the next three swipes.
        Check(f.Hit(1590)==3&&f.Feeds(s,Fury,wolf,1840,480,unleashed)==F::Hit&&f.Hit(1840)==4&&
              f.Feeds(s,Fury,wolf,2090,480,unleashed)==F::Hit&&f.Hit(2090)==5&&!f.Open(s,Fury,wolf),"Unleashed: five swipes, five hits, then spent");
        Check(f.Feeds(s,Fury,wolf,3000,480,unleashed)==F::None&&f.Lapse(s,9000,480,wolf)==0,"a spent flurry forfeits nothing");
        f.Begin(s,Fury,wolf,5,70,10000);f.Hit(10400);
        Check(f.Lapse(s,11360,480,wolf)==0&&f.Open(s,Fury,wolf),"still open two cycles after the last hit");
        Check(f.Lapse(s,11361,480,wolf)==3&&!f.Open(s,Fury,wolf),"lapses past the window, forfeiting 3");
        f.Begin(s,Fury,wolf,5,70,20000);
        Check(f.Lapse(9,20001,480,wolf)==4&&!f.Open(s,Fury,wolf),"a new game closes it");
        f.Begin(s,Fury,wolf,5,70,25000);
        Check(f.Lapse(s,25100,480,2)==4&&!f.Open(s,Fury,wolf),"a stroke in another form ends it");
        f.Begin(s,Fury,wolf,2,70,30000);
        Check(f.Left()==1&&f.Hit(30400)==2&&!f.Open(s,Fury,wolf),"a 2-hit Fury (level 1)");
        f.Begin(s,Fury,wolf,1,70,40000);
        Check(!f.Open(s,Fury,wolf),"a single-hit count opens nothing");
        f.Begin(s,Fury,wolf,5,0,50000);
        Check(f.Floor(480,classic)==480&&FlurryPace(70)==70&&FlurryPace(9)==100&&FlurryPace(101)==100,"Classic: an unread pace is one full cycle");
        f.Begin(s,Fury,wolf,5,70,60000);
        Check(f.Feeds(s,Fury,wolf,60001,0,classic)==F::Hit,"Classic: an unknown cycle never holds a follow-up back");
        f.End();
        Check(!f.Open(s,Fury,wolf)&&f.Lapse(s,99999,480,wolf)==0,"ended by another skill's cast");
    }
    // Spread: a later stroke lands Fury on its first two creatures; the casting stroke on one.
    {
        using F=Flurry::Feed;Flurry f;const uint64_t s=7;const int wolf=1;const bool unleashed=false;
        const uint64_t cast=Flurry::StrokeKey(0,10),next=Flurry::StrokeKey(1,4),again=Flurry::StrokeKey(0,11);
        Check(cast!=next&&Flurry::StrokeKey(0,4)!=next&&cast!=0,"each hand's strokes have their own keys");
        f.Begin(s,Fury,wolf,5,70,1000,cast);
        Check(f.SameStroke(cast)&&f.Feeds(s,Fury,wolf,1000,480,unleashed,cast)==F::TooSoon,"the casting stroke's other creatures stay basic");
        Check(f.Feeds(s,Fury,wolf,1250,480,unleashed,next)==F::Hit&&f.Hit(1250,next)==2,"the next stroke's first creature: hit 2");
        Check(f.SameStroke(next)&&f.Feeds(s,Fury,wolf,1250,480,unleashed,next)==F::Hit&&f.Hit(1250,next)==3,"its second creature: hit 3, at once");
        Check(f.Feeds(s,Fury,wolf,1251,480,unleashed,next)==F::TooSoon,"its third creature stays a cleave basic");
        Check(f.Feeds(s,Fury,wolf,1300,480,unleashed,again)==F::TooSoon&&f.Feeds(s,Fury,wolf,1350,480,unleashed,again)==F::Hit,
              "a new stroke keeps the follow-through guard from the last hit");
        Check(f.Hit(1350,again)==4&&f.Feeds(s,Fury,wolf,1350,480,unleashed,again)==F::Hit&&f.Hit(1350,again)==5&&!f.Open(s,Fury,wolf),
              "two more creatures: hits 4 and 5, then spent");
        f.Begin(s,Fury,wolf,5,70,2000,cast);
        Check(f.Feeds(s,Fury,wolf,2200,480,unleashed)==F::Hit,"without a stroke key: the pace rule only");
    }
    if(failures)return EXIT_FAILURE;
    std::printf("strike skills ok\n");return EXIT_SUCCESS;
}
