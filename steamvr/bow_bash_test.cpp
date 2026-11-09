// Bow bash (#443): the bow's capsule, the waits between bashes, the act-boss refusal, and the bow's
// swing through the melee engine's own stroke rules. The stagger itself is the game's hit recovery
// (vr/melee.cpp BowBash); no damage path exists to test.
#include "bow_bash.h"
#include <cstdio>
#include <cstdlib>
using namespace framevr;using namespace framevr::melee;
static void Check(bool ok,const char* why){if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
static bool Near(float a,float b){return std::fabs(a-b)<.0001f;}
int main(){
    using bowbash::Verdict;
    {   // The capsule: centred on the grip, limb tip to limb tip, whatever length the axis came with.
        const auto b=bowbash::Blade({1,1.2f,-.5f},{0,2,0});
        Check(Near(b.base.y,1.2f-bowbash::HalfLengthM)&&Near(b.tip.y,1.2f+bowbash::HalfLengthM)&&Near(b.base.x,1)&&Near(b.tip.z,-.5f),
              "the bow runs both ways from its grip along the limbs");
        Check(Near(Length(b.tip-b.base),1.1f)&&Near(b.radius,bowbash::RadiusM),"about 1.1 m limb tip to limb tip");
        // A horizontal swing past a monster (body 0.3 m to 1.5 m, 0.3 m thick): the riser, the upper limb or the lower one hits.
        const Body body{{0,.3f,-1},{0,1.5f,-1},.3f};
        const auto swing=[&](float gripY){return Touches(bowbash::Blade({-1,gripY,-1},{0,1,0}),bowbash::Blade({1,gripY,-1},{0,1,0}),body);};
        Check(swing(1.f),"the riser at chest height hits");
        Check(swing(2.2f),"held high, the lower limb still reaches the body's top");
        Check(swing(-.1f),"held low, the upper limb reaches it");
        Check(!swing(2.8f),"well above the body, the bow misses");
    }
    {   // The waits: 1.5 s between bashes, 3 s per monster; only a landed stagger starts them.
        bowbash::Board board;
        Check(board.Judge(1,7,1000,false,false)==Verdict::Stagger,"a first bash staggers");
        Check(board.Judge(1,7,1000,true,false)==Verdict::ActBoss,"an act boss never staggers");
        Check(board.Judge(1,7,1000,false,true)==Verdict::Recovering,"a monster already in its hit recovery is left to it");
        board.Landed(1,7,1000);
        Check(board.Judge(1,8,1500,false,false)==Verdict::TooSoon&&board.Wait(1,1500)==1000,"another monster waits out the 1.5 s, 1000 ms left");
        Check(board.Judge(1,8,2499,false,false)==Verdict::TooSoon,"still too soon 1 ms before");
        Check(board.Judge(1,8,2500,false,false)==Verdict::Stagger,"another monster 1.5 s later staggers");
        Check(board.Judge(1,7,2500,false,false)==Verdict::SameMonster&&board.WaitFor(1,7,2500)==1500,"the same monster waits out its 3 s");
        Check(board.Judge(1,7,4000,false,false)==Verdict::Stagger,"the same monster 3 s later staggers again");
        Check(board.Judge(1,7,1200,true,false)==Verdict::ActBoss,"an act boss is refused as one, whatever the wait");
        // Refusals start nothing: judging is read-only.
        for(int i=0;i<5;++i)board.Judge(1,9,1100+i,false,false);
        Check(board.Wait(1,2500)==0&&board.WaitFor(1,9,2500)==0,"refused bashes start no wait");
        board.Landed(1,8,2500);
        Check(board.Judge(1,8,3000,false,false)==Verdict::TooSoon&&board.Judge(1,8,4000,false,false)==Verdict::SameMonster,
              "the second monster's own 3 s run from its bash");
        Check(board.Judge(2,8,3000,false,false)==Verdict::Stagger,"a new game forgets the old one's bashes");
        board.Landed(2,8,3000);
        Check(board.WaitFor(2,7,3100)==0,"the old game's monsters are gone with it");
        // Many monsters: the oldest entries give way, the latest are kept.
        bowbash::Board many;uint64_t t=10000;
        for(uint32_t id=100;id<140;++id,t+=bowbash::CooldownMs)many.Landed(1,id,t);
        Check(many.WaitFor(1,139,t)>0&&many.Judge(1,139,t,false,false)==Verdict::SameMonster,"the latest monster keeps its wait");
    }
    {   // The bow's swing through the melee engine: a bash sample is its own lane state.
        Sample s;s.active=true;s.epoch=1;s.equipment=5;s.unitsPerMetre=4;s.minWeaponSpeed=12;s.handIndex=1;s.bowBash=true;
        s.hand.p={-.3f,1.1f,-.4f};s.blade=bowbash::Blade(s.hand.p,{0,1,0});s.thrustAxis={0,1,0};
        Engine engine;Sweep sweep;
        const auto step=[&](Vec d){s.time+=.02;s.hand.p=s.hand.p+d;s.blade=bowbash::Blade(s.hand.p,{0,1,0});return engine.Step(s,sweep);};
        for(int i=0;i<8;++i)step({});
        bool swung=false;for(int i=0;i<8;++i)swung|=step({.06f,0,-.01f});   // 3 m/s: below 12 m/s, no bash
        Check(!swung,"a slow bow wave is no bash; the swing speed setting applies");
        for(int i=0;i<8;++i)step({});
        for(int i=0;i<8&&!swung;++i)swung=step({.26f,0,0});   // 13 m/s
        Check(swung&&!sweep.thrust&&Near(Length(sweep.after.tip-sweep.after.base),1.1f),"a hard swing of the bow makes a stroke with the whole bow");
        Check(step({.26f,0,0}),"the hard swing goes on");
        auto drawn=s;drawn.bowBash=false;drawn.time+=.02;drawn.hand.p=drawn.hand.p+Vec{.26f,0,0};drawn.blade=bowbash::Blade(drawn.hand.p,{0,1,0});
        Check(!engine.Step(drawn,sweep),"drawing the string ends the bash stroke, even mid-swing");
        PoseHistory history;history.Push(1,s);auto now=s;now.bowBash=false;now.time+=.01;history.Push(2,now);Sample found;
        Check(!history.Find(1,now.time,found),"a pose sampled before the string was drawn is not a bash any more");
        Check(history.Find(2,now.time,found)&&!found.bowBash,"the current pose is found as it is");
    }
    std::puts("bow-bash-test: ok");
    return 0;
}
