#include "skill_usage.h"
#include <cstdio>
#include <cstring>
using namespace framevr;
static int failures=0;
static void Check(bool ok,const char* what){if(!ok){std::printf("FAIL: %s\n",what);++failures;}}
static bool Has(const std::string& text,const char* part){return text.find(part)!=std::string::npos;}
int main() {
    using namespace skillusage;
    const int rightTrigger=skills::TriggerSlot(0),leftTrigger=skills::TriggerSlot(1),rightFace=skills::HandSlot(0,1),leftFace=skills::HandSlot(1,2);
    bool fits=true;
    for(int skill=0;skill<400;++skill)for(int slot=0;slot<skills::Count;++slot) {
        const char* line=Line(skill,slot);const auto details=Details(skill,slot);
        fits=fits&&line[0]&&std::strlen(line)<=LineLength&&!details.empty()&&details.size()<=DetailsLength;
    }
    Check(fits,"every skill and slot has a line and a card text that fit");
    Check(!Line(-1,rightTrigger)[0]&&!Line(126,-1)[0]&&!Line(126,skills::Count)[0]&&Details(-1,rightTrigger).empty(),"no skill or no slot: nothing");
    Check(CastOnStrike(strikeskills::Bash,rightTrigger)&&CastOnStrike(strikeskills::Bash,leftTrigger)&&!CastOnStrike(strikeskills::Bash,rightFace)&&
          !Gesture(strikeskills::Bash,rightTrigger)&&ByMotion(strikeskills::Bash,rightTrigger)&&
          CastOnStrike(strikeskills::Fury,rightTrigger)&&CastOnStrike(strikeskills::Zeal,leftTrigger)&&
          !CastOnStrike(weaponskills::HolyShield,rightTrigger)&&!CastOnStrike(weaponskills::Strafe,rightTrigger),
          "a strike skill is Cast On Strike on either trigger, a press on a face button");
    Check(Has(Line(strikeskills::Bash,rightTrigger),"full-cycle")&&Has(Details(strikeskills::Bash,rightTrigger),"no on-hit effects")&&
          Has(Details(strikeskills::Bash,rightTrigger),"basic attack"),"a strike: the full-cycle rule, what quicker swings lose, when a swing stays basic");
    Check(Has(Line(strikeskills::Zeal,rightTrigger),"land by themselves")&&Has(Line(strikeskills::Fury,rightTrigger),"basic attacks")&&
          Has(Details(strikeskills::Fury,rightTrigger),"moving on to monsters nearby")&&Has(Details(strikeskills::Zeal,rightTrigger),"keep moving")&&
          Has(Details(strikeskills::Zeal,rightTrigger),"Swings while it runs"),
          "Fury and Zeal: one swing casts, the other hits land by themselves while you move, swings meanwhile stay basic");
    Check(Has(Line(strikeskills::Bash,rightFace),"On a trigger it is Cast On Strike")&&Has(Details(strikeskills::Zeal,rightFace),"On a trigger it becomes Cast On Strike"),
          "a face button points to Cast On Strike on a trigger");
    Check(CastOnStrike(weaponskills::Smite,leftTrigger)&&!CastOnStrike(weaponskills::Smite,rightTrigger)&&Has(Details(weaponskills::Smite,leftTrigger),"shield"),
          "Smite: a shield gesture on the left trigger");
    Check(Gesture(weaponskills::Strafe,rightTrigger)&&Gesture(weaponskills::Strafe,leftTrigger)&&!Gesture(weaponskills::Strafe,leftFace),"arrow skills: draw and release on a trigger");
    Check(Has(Line(7,rightFace),"loads it into every following arrow")&&Has(Details(7,rightFace),"Press again"),"a quiver arrow skill on a face button loads the quiver");
    Check(Gesture(15,rightTrigger)&&!Gesture(15,leftTrigger)&&Has(Line(15,leftFace),"following throw"),"javelin skills: the right trigger's throw, or loaded");
    Check(Gesture(weaponskills::DoubleThrow,rightTrigger)&&!Gesture(weaponskills::DoubleThrow,leftTrigger),"Double Throw on the right trigger");
    Check(Gesture(251,rightTrigger)&&!Gesture(251,leftTrigger)&&!Gesture(251,rightFace),"traps and bombs are thrown from the right trigger only");
    Check(!Gesture(36,rightTrigger)&&Has(Line(36,rightTrigger),"trigger")&&Has(Line(36,rightFace),"button"),"any other skill casts natively from its trigger or button");
    Check(Has(Line(Interact,rightFace),"pick up")&&Has(Line(weaponskills::Attack,rightTrigger),"attack"),"Interact and the basic attack say what they do, not \"cast\"");
    Check(Gesture(weaponskills::Throw,rightTrigger)&&!Gesture(weaponskills::Throw,leftTrigger)&&Has(Details(weaponskills::Throw,rightTrigger),"gas or oil potion")&&
          Has(Details(weaponskills::Attack,rightTrigger),"thrown by hand"),"Throw and Attack on the right trigger throw a held javelin or potion by hand");
    for(int slot=0;slot<skills::Count;++slot)
        Check(Gesture(weaponskills::BlessedHammer,slot)&&Gesture(weaponskills::Charge,slot),"Blessed Hammer and Charge are gestures from every slot");
    for(int slot=0;slot<skills::Count;++slot)
        Check(Gesture(weaponskills::HolyShield,slot)&&Gesture(weaponskills::Werewolf,slot)&&Gesture(weaponskills::Werebear,slot),
              "Holy Shield and the Druid's shapeshifts are gestures from every slot");
    for(int slot=0;slot<skills::Count;++slot)Check(Gesture(weaponskills::FistOfHeavens,slot),"Fist of the Heavens is a gesture from every slot");
    Check(Has(Details(weaponskills::FistOfHeavens,leftTrigger),"points at"),"Fist of the Heavens is described");
    Check(Has(Details(weaponskills::HolyShield,rightFace),"sign of the cross")&&Has(Line(weaponskills::Werewolf,leftTrigger),"bow")&&
          Has(Line(weaponskills::Werebear,rightTrigger),"slam"),"Holy Shield and the shapeshifts are described");
    Check(Has(Line(weaponskills::BlessedHammer,rightFace),"punch")&&Has(Details(weaponskills::Charge,leftTrigger),"weapon hand"),"the Paladin punches are described");
    if(failures)return 1;
    std::puts("skill usage: every skill and slot fits; gestures and native sequences described");
    return 0;
}
