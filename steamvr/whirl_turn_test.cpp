#include "whirl_turn.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
using namespace framevr::whirlturn;
static void Check(bool ok,const char* why){if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
static Steer Push(float degrees){const float r=degrees*.0174533f;return Steer{true,std::cos(r),std::sin(r),false};}
int main(){
    const Whirl w{5000,4000,5024,4000};   // 24 cells left, heading +x
    {   // The stick the way he goes: nothing to do.
        Check(!Decide(w,Push(0)).turn,"straight on: no re-aim");
        Check(!Decide(w,Push(4)).turn&&!Decide(w,Push(-4)).turn,"a few degrees off: left alone");
        Check(!Decide(w,Steer{}).turn,"the stick at rest, never pushed: left alone");
    }
    {   // Pushed another way: the same ground left, the new way.
        const Aim a=Decide(w,Push(90));
        Check(a.turn&&!a.stop&&a.x==5000&&a.y==4024,"a quarter turn: 24 cells along the new way");
        const Aim b=Decide(w,Push(180));
        Check(b.turn&&b.x==4976&&b.y==4000,"turned right round: 24 cells back");
        const Aim c=Decide(w,Push(45));
        Check(c.turn&&std::abs(std::hypot(float(c.x-5000),float(c.y-4000))-24)<.8f,"diagonal: still 24 cells (whole cells)");
        const Aim d=Decide(w,Push(10));
        Check(d.turn,"ten degrees: turned");
        Steer big=Push(90);big.gx*=.7f;big.gy*=.7f;
        Check(Decide(w,big).turn&&Decide(w,big).y==4024,"the way's length does not matter");
    }
    {   // Little left: rounding to whole cells is no turn; under two cells the whirl ends as it is.
        const Whirl near{5000,4000,5003,4000};
        Check(!Decide(near,Push(10)).turn,"3 cells left: 10 degrees is within a cell's rounding");
        Check(Decide(near,Push(90)).turn,"3 cells left: a quarter turn still turns");
        const Whirl last{5000,4000,5001.5f,4000};
        Check(!Decide(last,Push(90)).turn&&!Decide(last,Steer{false,0,0,true}).turn,"under 2 cells left: it ends as it is");
    }
    {   // Let go after a push: it ends a cell and a half on, the way he goes.
        const Aim s=Decide(w,Steer{false,0,0,true});
        Check(s.turn&&s.stop&&s.x==5002&&s.y==4000,"let go: ends 1.5 cells on (whole cells)");
        const Whirl two{5000,4000,5002.2f,4000};
        Check(!Decide(two,Steer{false,0,0,true}).turn,"already ending: nothing to cut");
        Check(Decide(w,Steer{true,0,1,true}).turn&&!Decide(w,Steer{true,0,1,true}).stop,"pushed wins over let go");
    }
    {   // Never out of the game's reach, never nonsense.
        const Whirl wide{5000,4000,5000,4040};   // 40 cells left (no whirl is this long; still kept in reach)
        const Aim a=Decide(wide,Push(45));
        Check(a.turn&&std::abs(a.x-5000)<=45&&std::abs(a.y-4000)<=45,"inside the game's reach square");
        Check(!Decide(Whirl{NAN,4000,5024,4000},Push(90)).turn,"a position unread: nothing");
        Check(!Decide(w,Steer{true,NAN,1,false}).turn&&!Decide(w,Steer{true,0,0,false}).turn,"a way unread: nothing");
        Check(!Decide(Whirl{2,2,2,30},Push(180)).turn,"off the map's edge: nothing");
    }
    {   // The client follows the server's spot on the same whirl: from where it began, where it was, or again.
        Check(Follow(5024,4000,5024,4000,5024,4000,5000,4024,false),"his end where the server's was: lay the new one");
        Check(Follow(5023,4001,5024,4000,5024,4000,5000,4024,false),"a cell apart: the same whirl");
        Check(Follow(5024,4000,5024,4000,5010,4010,5000,4024,false),"two turns came at once: his end at the whirl's start still follows");
        Check(Follow(5010,4010,5024,4000,5016,4016,5000,4024,true),"followed this whirl before: follows again");
        Check(!Follow(5000,4024,5024,4000,5024,4000,5000,4024,true),"already there: nothing");
        Check(!Follow(5010,4030,5024,4000,5016,4016,5000,4024,false),"another whirl (his own end elsewhere): left alone");
        Check(!Follow(NAN,4000,5024,4000,5024,4000,5000,4024,true),"unread: nothing");
    }
    std::puts("whirl-turn-test: ok");
}
