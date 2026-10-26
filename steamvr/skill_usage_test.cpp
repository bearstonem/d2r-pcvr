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
    Check(Gesture(strikeskills::Bash,rightTrigger)&&Gesture(strikeskills::Bash,leftTrigger)&&!Gesture(strikeskills::Bash,rightFace),
          "a strike skill is a gesture on either trigger, a press on a face button");
    Check(Has(Line(strikeskills::Bash,rightTrigger),"full-cycle")&&Has(Details(strikeskills::Bash,rightTrigger),"no on-hit effects")&&
          Has(Details(strikeskills::Bash,rightTrigger),"basic attack"),"a strike: the full-cycle rule, what quicker swings lose, when a swing stays basic");
    Check(Has(Line(strikeskills::Zeal,rightTrigger),"full-strength")&&Has(Details(strikeskills::Zeal,rightTrigger),"on-hit effects")&&
          Has(Details(strikeskills::Fury,rightTrigger),"however fast")&&Has(Details(strikeskills::Zeal,rightTrigger),"no more mana"),
          "a flurry's later swings are full-strength hits with on-hit effects and no mana, whatever their speed");
    Check(Has(Line(strikeskills::Bash,rightFace),"On a trigger it is a gesture")&&Has(Details(strikeskills::Zeal,rightFace),"On a trigger it becomes a gesture"),
          "a face button points to the trigger gesture");
    Check(Gesture(weaponskills::Smite,leftTrigger)&&!Gesture(weaponskills::Smite,rightTrigger)&&Has(Details(weaponskills::Smite,leftTrigger),"shield"),
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
    if(failures)return 1;
    std::puts("skill usage: every skill and slot fits; gestures and flurries described");
    return 0;
}
