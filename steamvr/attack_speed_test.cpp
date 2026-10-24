#include "attack_speed.h"
#include <cstdio>
#include <cstdlib>
using namespace framevr::attackspeed;
static void Check(bool ok,const char* why){if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
int main(){
    // Attack cycle lengths at 0 IAS / 0 WSM, as listed by the Amazon Basin
    // wiki's A1 tables (ACL column), for every class and weapon class.
    constexpr int acl[ClassCount][int(Weapon::Count)]={
        {11,13,12,17,15,17,13,19,0},   // Amazon
        {14,17,16,21,20,15,16,19,0},   // Sorceress
        {14,18,18,22,23,19,17,19,0},   // Necromancer
        {13,14,16,17,19,17,15,19,0},   // Paladin
        {11,15,15,17,18,18,14,19,0},   // Barbarian
        {15,18,18,20,22,16,15,19,0},   // Druid
        {10,14,14,22,22,18,15,20,13},  // Assassin (claws: animation speed 208)
    };
    for(int c=0;c<ClassCount;++c)for(int w=0;w<int(Weapon::Count);++w)
        Check(Frames(c,Weapon(w),0,0)==acl[c][w],"base cycle matches the published table");
    // IAS breakpoints published by the Amazon Basin wiki ("Barbarian attack
    // speed", WSM 0): the minimum IAS for each frame count. The cycle must
    // reach each count exactly there, and not one IAS sooner.
    struct Breakpoint {Weapon weapon;int frames,ias;};
    constexpr Breakpoint barbarian[]={
        {Weapon::OneHandSwing,14,9},{Weapon::OneHandSwing,13,18},{Weapon::OneHandSwing,12,30},
        {Weapon::OneHandSwing,11,48},{Weapon::OneHandSwing,10,75},{Weapon::OneHandSwing,9,125},
        {Weapon::TwoHandSwing,16,8},{Weapon::TwoHandSwing,15,15},{Weapon::TwoHandSwing,14,26},{Weapon::TwoHandSwing,13,39},
        {Weapon::TwoHandSwing,12,58},{Weapon::TwoHandSwing,11,86},{Weapon::TwoHandSwing,10,138}};
    for(const auto& b:barbarian)
        Check(Frames(4,b.weapon,b.ias,0)==b.frames&&Frames(4,b.weapon,b.ias-1,0)==b.frames+1,"Barbarian IAS breakpoints match the published tables");
    Check(Eias(0)==0&&Eias(20)==17&&Eias(60)==40&&Eias(120)==60&&Eias(-15)==0,"IAS has diminishing returns, never negative");
    {   // Druid shapeshift forms (D2R 2.4), checked against ChthonVII's wereform calculator
        // (normal attack, total frames) over every weapon class and EIAS -85..150.
        struct Point {int form;Weapon weapon;int eias,frames;};
        constexpr Point points[]={
            {Wolf,Weapon::OneHandSwing,0,19},{Wolf,Weapon::OneHandSwing,20,15},{Wolf,Weapon::OneHandSwing,30,14},
            {Wolf,Weapon::OneHandSwing,50,12},{Wolf,Weapon::OneHandSwing,80,10},
            {Bear,Weapon::OneHandSwing,0,19},{Bear,Weapon::OneHandSwing,20,15},{Bear,Weapon::OneHandSwing,50,12},
            {Wolf,Weapon::HandToHand,-85,107},{Wolf,Weapon::HandToHand,0,15},{Wolf,Weapon::HandToHand,150,6},
            {Wolf,Weapon::TwoHandThrust,0,23},{Wolf,Weapon::TwoHandThrust,75,13},{Wolf,Weapon::TwoHandThrust,150,9},
            {Bear,Weapon::Staff,0,17},{Bear,Weapon::Staff,75,9},{Bear,Weapon::Staff,150,6},
            {Bear,Weapon::Crossbow,-30,28},{Bear,Weapon::Crossbow,40,14}};
        for(const auto& p:points)Check(WereformFrames(5,p.form,p.weapon,p.eias)==p.frames,"wereform frames match the 2.4 calculator");
        Check(WereformFrames(5,Wolf,Weapon::OneHandSwing,151)==WereformFrames(5,Wolf,Weapon::OneHandSwing,150)&&
              WereformFrames(5,Wolf,Weapon::OneHandSwing,-200)==WereformFrames(5,Wolf,Weapon::OneHandSwing,-85),"wereform EIAS is capped at -85..150");
        Check(WereformFrames(5,Wolf,Weapon::OneHandSwing,150)<WereformFrames(5,Wolf,Weapon::OneHandSwing,75),"the form's cap is above the human 75");
        Check(WereformFrames(5,Human,Weapon::OneHandSwing,0)==0&&WereformFrames(5,Wolf,Weapon::Claw,0)==0&&WereformFrames(9,Wolf,Weapon::OneHandSwing,0)==0,
              "unknown form, weapon class or class: no cycle");
        // Werewolf level 1 (+20 skill speed), a 1-handed sword of WSM 0, no IAS: attackrate 120.
        Check(FormEias(0,120)==20&&WereformFrames(5,Wolf,Weapon::OneHandSwing,FormEias(0,120))==15,"skill speed comes through attackrate");
        Check(FormEias(60,100)==40&&FormEias(0,110)==10&&FormEias(0,80)==-20,"item IAS diminishes; WSM and slows lower attackrate");
    }
    // Amazon, no weapon, 60 IAS: EIAS 40 -> AnimRate 358 -> ceil(3072/358)-1 = 8.
    Check(Frames(0,Weapon::HandToHand,60,0)==8,"IAS shortens the cycle");
    Check(Frames(4,Weapon::OneHandSwing,0,-20)<Frames(4,Weapon::OneHandSwing,0,0)&&
          Frames(4,Weapon::OneHandSwing,0,20)>Frames(4,Weapon::OneHandSwing,0,0),"negative WSM is faster, positive slower");
    for(int ias=0;ias<400;++ias)
        Check(Frames(3,Weapon::OneHandSwing,ias+1,0)<=Frames(3,Weapon::OneHandSwing,ias,0),"more IAS is never slower");
    Check(Frames(3,Weapon::OneHandSwing,100000,-60,1000)==Frames(3,Weapon::OneHandSwing,100000,-60,75),"speed sum is bounded above");
    Check(Frames(3,Weapon::OneHandSwing,0,1000)>0,"heavily slowed attacks still have a finite cycle");
    // Human form reads skill speed and slows from attackrate (68), which already subtracts the
    // main hand's WSM: the logged unbuffed rows (WSM 0/+10/-10 -> 100/90/110) are no skill speed.
    Check(SkillSpeed(100,0)==0&&SkillSpeed(90,10)==0&&SkillSpeed(110,-10)==0,"unbuffed attackrate is no skill speed");
    for(int c=0;c<ClassCount;++c)for(int w=0;w<int(Weapon::Count);++w)for(int wsm=-30;wsm<=30;wsm+=10)for(int ias:{0,20,60,120})
        Check(Frames(c,Weapon(w),ias,wsm,SkillSpeed(100-wsm,wsm))==Frames(c,Weapon(w),ias,wsm),"unbuffed cycles are unchanged");
    // Paladin, one-handed sword (WSM 0), no IAS: 14 frames (560 ms); Fanaticism's +30% -> 11 (440 ms).
    Check(Frames(3,Weapon::OneHandSwing,0,0,SkillSpeed(130,0))==11&&Milliseconds(11)==440,"skill speed shortens the human cycle");
    Check(Frames(3,Weapon::OneHandSwing,0,0,SkillSpeed(50,0))>14,"a slow lengthens it");
    // An off-hand blade keeps the same skill speed with its own WSM.
    Check(Frames(4,Weapon::OneHandSwing,0,20,SkillSpeed(130-0,0))==Frames(4,Weapon::OneHandSwing,0,20,30),"off-hand: its own WSM, the main hand's skill speed");
    Check(Frames(3,Weapon::OneHandSwing,200,0,SkillSpeed(200,0))==Frames(3,Weapon::OneHandSwing,200,0,1000),"the human 75 EIAS cap still holds");
    Check(Frames(-1,Weapon::OneHandSwing,0,0)==0&&Frames(7,Weapon::OneHandSwing,0,0)==0&&Frames(0,Weapon::Claw,0,0)==0,
          "unknown class or unusable weapon class has no cycle");
    Check(Milliseconds(15)==600&&Milliseconds(8)==320&&Milliseconds(0)==0,"25 frames per second");
    Weapon w{};
    Check(FromCode("1hs ",w)&&w==Weapon::OneHandSwing&&FromCode("ht1",w)&&w==Weapon::Claw&&FromCode("xbw\0",w)&&w==Weapon::Crossbow,
          "wclass codes map regardless of padding");
    Check(!FromCode("abc",w),"unknown wclass is refused");
    Cadence cadence;
    Check(cadence.Ready(7,1000)&&cadence.Since(7,1000)==UINT64_MAX,"first hit is always available, with no previous hit");
    cadence.Landed(7,1000,600);
    Check(!cadence.Ready(7,1200)&&cadence.Remaining(7,1200)==400&&cadence.Since(7,1200)==200,"a landed hit starts the cycle");
    Check(cadence.Since(8,1200)==UINT64_MAX,"another session has no previous hit");
    Check(cadence.Ready(7,1600),"the next hit is available after one cycle");
    Check(cadence.Ready(8,1100),"a new game session is never locked out by the previous one");
    cadence.Landed(7,2000,0);
    Check(cadence.Ready(7,2000),"an unknown cycle never gates");
    {   // one batch, two claws on two creatures, applied out of time order
        Cadence c;c.Landed(7,3005,600);
        Check(c.Since(7,3000)==0,"a contact timed before the latest landed hit is not recovered");
        c.Landed(7,3000,600);
        Check(c.Since(7,3010)==5&&!c.Ready(7,3604)&&c.Ready(7,3605),"the clock never runs back");
    }
    std::puts("attack-speed-test passed");
}
