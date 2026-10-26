#include "aim_reach.h"
#include "arm_pointing.h"
#include <cstdio>
#include <cstdlib>
using namespace framevr;
static void Check(bool b,const char* why){if(!b){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
static bool Near(float a,float b){return std::fabs(a-b)<1e-3f;}
int main() {
    using namespace aimreach;
    // The game refuses more than 50 cells on either axis; the reach keeps a margin inside it.
    Check(Within(GameCells,0)&&!Within(GameCells+.1f,0),"straight along an axis: the game's reach");
    Check(Within(40,40)&&Near(std::hypot(40.f,40.f),56.5685f),"a corner reaches farther than a circle would");
    Check(!Within(30,GameCells+1)&&!Within(-GameCells-1,0),"either axis past the reach is out, either way");
    Check(!Within(NAN,0)&&!Within(0,INFINITY),"nonsense is out");
    float x=90,y=0;Check(Clamp(x,y)&&Near(x,GameCells)&&Near(y,0),"clamped along its own heading");
    x=60;y=30;Check(Clamp(x,y)&&Near(x,GameCells)&&Near(y,GameCells/2),"the heading is kept");
    x=-60;y=-30;Check(Clamp(x,y)&&Near(x,-GameCells)&&Near(y,-GameCells/2),"backwards too");
    x=3;y=4;Check(Clamp(x,y)&&Near(x,3)&&Near(y,4),"inside the reach nothing changes");
    x=NAN;Check(!Clamp(x,y),"nonsense is refused");
    Check(Near(Along(1,0),GameCells)&&Near(Along(1,1),CornerCells)&&Along(0,0)==0,"reach along a heading");
    // Arm out to point: each feature its own state and setting.
    using armpointing::State;using armpointing::Update;
    State a,b;
    Check(Update(a,.25f,0,20)&&!Update(b,.25f,0,40),"one hand, two settings, two answers");
    Check(Update(a,.16f,0,20),"once pointing it lets go 6 cm nearer");
    Check(!Update(a,.13f,0,20),"and then lets go");
    State down;Check(!Update(down,.4f,-.8f,20),"a hand pointing within 45 degrees of straight down is resting");
    Check(Update(down,.4f,-.6f,20)&&Update(down,.4f,-.74f,20)&&!Update(down,.4f,-.8f,20),"raised it points, and lets go past 50 degrees down");
    State side;Check(!Update(side,0,0,20),"a hand at the side has no reach along its aim");
    State always;Check(Update(always,0,0,0),"0 cm: the hand always points");
    Check(Near(armpointing::ArmOutCm(1.5f),15)&&Near(armpointing::ArmOutCm(1.6f),16),"the arm-out is a tenth of the span: 15 cm on 1.5 m, 16 cm on the default 1.6 m");
    std::puts("aim reach tests passed");
}
