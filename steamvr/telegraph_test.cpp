#include "telegraph.h"
#include <cstdio>
#include <cstdlib>
using namespace framevr::telegraph;
static void Check(bool b,const char* why){if(!b){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
static int Showing(const Marks& marks,uint64_t now){int n=0;for(const auto& m:marks)n+=Sample(m,now).on;return n;}
int main() {
    Check(FlightMs(28)==2000,"Fire Blast's time follows the measured bomb speed");
    Check(FlightMs(0)==MinFlightMs,"a throw at your feet still shows briefly");
    Check(ForSkill(251,28).showMs==ThrowMs+2000&&ForSkill(251,28).radius==RadiusCells,"Fire Blast is a ground skill like the others: an area and a time");
    Check(!Sample({},1000).on,"nothing thrown, nothing drawn");
    const Mark m{100,200,1000,2000,0,1};
    Check(!Sample(m,999).on,"not before the throw");
    auto d=Sample(m,1000);Check(d.on&&d.fill==0&&d.alpha==1,"appears the moment the bomb is thrown, unfilled");
    d=Sample(m,2000);Check(d.on&&d.fill>.49f&&d.fill<.51f,"half filled halfway through the flight");
    d=Sample(m,3000);Check(d.on&&d.fill==1&&d.alpha==1,"full when the bomb should land");
    d=Sample(m,3000+LateMs+FadeMs/2);Check(d.on&&!d.burst&&d.alpha>.4f&&d.alpha<.6f,"a missed burst event still fades out");
    Check(!Sample(m,3000+LateMs+FadeMs).on,"and then disappears");
    Mark landed=m;landed.landed=1800;
    d=Sample(landed,1800);Check(d.on&&d.burst&&d.fill==1&&d.alpha==1,"burst completes the fill and flashes");
    Check(!Sample(landed,1800+FadeMs).on,"gone after the burst fade");

    // Several bombs in the air keep their own circles, as any ground skill's do.
    Board board;
    board.Place(28,0,1000,2000,0,RadiusCells);board.Place(0,28,1200,2000,0,RadiusCells);board.Place(-28,0,1400,2000,0,RadiusCells);
    Check(Showing(board.Get(),1500)==3,"three throws show three circles");
    Check(board.Nearest(1,27,1600).cast==1200&&!board.Nearest(500,500,1600).cast,"a burst is matched to the circle it landed in, for the log");
    for(int i=0;i<MaxMarks+2;++i)board.Place(float(i*3),0,10000+i,2000,0,RadiusCells);
    const auto full=board.Get();
    Check(Showing(full,10010)==MaxMarks,"more casts than slots reuse the oldest circles");
    bool newest=false;for(const auto& k:full)newest|=k.x==float((MaxMarks+1)*3);
    Check(newest,"the newest cast always gets a circle");
    // Timed by the game (attack speed changes the throw): the launch
    // missile re-times the circle, the landing ends it and teaches the next guess.
    {
        Board timed;bool landing=true;
        const int fb=FlightOf(385,landing);
        Check(fb>=0&&!landing&&FlightOf(386,landing)==fb&&landing&&FlightOf(9999,landing)<0,"Fire Blast's launch and landing missiles are known");
        Check(timed.Place(28,0,1000,ForSkill(251,28).showMs,0,RadiusCells,0,0,0,251),"a Fire Blast cast places its circle at once");
        Check(timed.Launched(fb,0,0,28,0,1100)==CellsPerSecond,"a quick throw (faster attack) launches after 0.1 s");
        auto m=timed.Get();int at=-1;for(int i=0;i<MaxMarks;++i)if(m[i].cast==1000)at=i;
        Check(at>=0&&m[at].flight==100+2000&&m[at].landed==0,"re-timed from the launch: full 2 s later, waiting for the landing");
        Check(!timed.Launched(fb,0,0,28,0,1150),"one launch per circle");
        const auto ended=timed.Landed(fb,28.5f,0,2600);   // 28 cells in 1.5 s: a faster bomb
        Check(ended.cast==1000,"the landing ends its circle");
        Check(timed.Speed(fb)>CellsPerSecond+2&&timed.Speed(fb)<28/1.5f,"the next guess moves toward the speed it flew");
        d=Sample(timed.Get()[at],2600);Check(d.on&&d.burst,"it flashes at the real landing");
        Check(!timed.Landed(fb,28,0,2700).cast,"each circle lands once");
        Check(CellsPerSecondOf(2304)>14.f&&CellsPerSecondOf(2304)<14.1f&&CellsPerSecondOf(0)==0,"the game's velocity in cells/s (Fire Blast: 14.06)");
        Check(timed.Place(0,40,6000,3000,0,RadiusCells,0,0,0,251)&&timed.Launched(fb,0,0,0,40,6100,20.f)==20.f,"a faster build's bomb is timed at its own live speed");
        m=timed.Get();at=-1;for(int i=0;i<MaxMarks;++i)if(m[i].cast==6000)at=i;
        Check(at>=0&&m[at].flight==100+2000,"40 cells at 20 cells/s: full 2 s after the launch");
        Check(timed.Place(0,-40,7000,3000,0,RadiusCells,0,0,0,251)&&timed.Launched(fb,0,0,0,-40,7100,500.f)==timed.Speed(fb),"a nonsense velocity falls back to the speed last flown");
        Check(timed.Place(10,10,5000,700,0,2.f,0,0,0,262)&&!timed.Launched(fb,0,0,10,10,5100),"another skill's circle is never re-timed by Fire Blast's missiles");
    }
    // Placed skills (traps, Blizzard): fill over their time, then fade; repeats merge.
    Board placed;
    Check(placed.Place(50,50,20000,1000,1,4.f),"a Blizzard cast places a circle");
    Check(!placed.Place(50.5f,50,20100,1000,1,4.f),"the same cast repeating at once is not a second circle");
    auto marks=placed.Get();int found=-1;for(int i=0;i<MaxMarks;++i)if(marks[i].cast==20000)found=i;
    Check(found>=0&&marks[found].element==1&&marks[found].radius==4.f,"the circle keeps its element and radius");
    d=Sample(marks[found],20500);Check(d.on&&d.fill>.49f&&d.fill<.51f,"half filled halfway");
    Check(!Sample(marks[found],21000+FadeMs).on,"gone after its time and the fade");
    Check(placed.Place(60,50,20100,600,2,2.5f),"another point at once is its own circle");
    // Measured areas: Blizzard wide and long, Meteor filling until it lands, Fire Wall a strip.
    Check(ForSkill(59).radius==7.f&&ForSkill(59).showMs==4100&&ForSkill(59).halfLength==0,"Blizzard: 7 cells for the 4.1 s storm");
    { bool landing=true;Check(ForSkill(256,28).showMs==ThrowMs+2000&&ForSkill(256).radius==ShockWebCells&&FlightOf(388,landing)>=0&&!landing&&FlightOf(389,landing)>=0&&landing,"Shock Web: thrown and timed like Fire Blast, a 6-cell spread"); }
    Check(ForSkill(56).showMs==2650&&ForSkill(56).radius==5.f,"Meteor: 5 cells, full when it lands at 2.65 s");
    Check(ForSkill(51).halfLength==5.f&&ForSkill(51).radius<1.f,"Fire Wall: a narrow 10-cell strip");
    Board wall;
    Check(wall.Place(10,10,30000,700,0,.75f,0,4,5),"a strip places");
    marks=wall.Get();found=-1;for(int i=0;i<MaxMarks;++i)if(marks[i].cast==30000)found=i;
    Check(found>=0&&marks[found].dirX==0&&marks[found].dirY==1&&marks[found].halfLength==5.f,"its direction is kept as a unit vector");
    Check(wall.Place(40,10,30000,700,0,2,0,0,5)&&[&]{for(const auto& k:wall.Get())if(k.x==40)return k.halfLength==0;return false;}(),"no direction: a circle");
    std::puts("Landing circle: timing, several casts, burst matching and reuse passed");
}
