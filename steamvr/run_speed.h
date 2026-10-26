#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

// Movement speed (VR Settings > VR, last row; [gameplay] run_speed_pct, Cal
// 2026-10-25; first a skill-wheel slider, which replaced RUN / WALK). Its lowest
// stop, Walk (-10), walks; every stop above runs - Base run (0), then 10% of the
// way to the gear's full run speed per stop, up to Full run (100). The plugin keeps the game's own run /
// walk toggle in line with it. Movement skills (Charge, Leap, Whirlwind...),
// walking itself and every other unit keep the game's own speed.
//
// D2R's hero velocity is linear in velocitypercent (stat 67), which is an
// absolute percentage: 100 walking with nothing, and running adds 50, so a
// hero with no speed bonus runs at stat 67 = 150 (logged 2026-10-25 on a
// Necromancer, Paladin and Barbarian: 150; Paladin with Vigor 163; level-90
// Barbarian with Increased Speed 193 - velocity ~14.8 x stat 67 in each case).
// So the class's no-gear run velocity is native x 150 / stat67, and the slider
// picks a point between it and native, in 10% steps:
//   0% = that base (a new character with no speed bonus),
//   100% = the full speed the gear and skills give (native, the default).
// Never faster than native, never slower than the base; with no bonus, or a
// slow that leaves none, native stays as it is.
//
// Item Faster Run/Walk is already inside stat 67 (Cal's FRW test, 2026-10-25: the
// dev starter Boots' +30% FRW took a running Druid's stat 67 from 150 to 169 and
// back to 150 with them off; stat 96 read 0 throughout), so BASE strips it like
// every other bonus and stat 96 is only logged.
namespace framevr::runspeed {
constexpr int WalkPercent=-10,MinPercent=WalkPercent,MaxPercent=100,StepPercent=10,DefaultPercent=100;
constexpr int RunBaselineVelocityPercent=150;   // stat 67 of a running hero with no speed bonus
inline int NormalizePercent(float value) {
    if(!std::isfinite(value))return DefaultPercent;
    return std::clamp(int(std::lround(value/StepPercent))*StepPercent,MinPercent,MaxPercent);
}
// The WALK stop: the hero walks (the game's walk, untouched); every other stop runs.
inline bool Walks(int percent){return percent<=WalkPercent;}
// Percentage points of a run above the no-bonus base.
inline int BonusPoints(int velocityPercent){return velocityPercent-RunBaselineVelocityPercent;}
// The no-bonus run velocity for a native run velocity at this stat 67.
inline double BaseOf(int native,int velocityPercent) {
    return velocityPercent>0?native*double(RunBaselineVelocityPercent)/velocityPercent:double(native);
}
// Whether a walk right now says anything about the game's run / walk toggle. Out of
// stamina the game walks, and a walk while stamina recovers can't be trusted either
// (stamina test, 2026-10-25 11:56:55-57:00: walks at 39-84 of 138 read as "on walk"
// sent five toggles in 4.5 s; the recheck at 12:04 resumed running on its own at 12
// and 55 of 138 with no extra toggle). So once exhausted (below 5%) the hero counts as
// tired until it is seen running again or its stamina is back near full (95%); also
// tired whenever below a quarter. A new hero starts rested.
struct Stamina {
    uint32_t hero=0;bool recovering=false;
    bool Tired(uint32_t heroId,uint32_t mode,int stamina,int maxStamina) {
        if(heroId!=hero){hero=heroId;recovering=false;}
        if(maxStamina<=0||stamina<0)return false;
        if(mode==3)recovering=false;                       // running again: the game let it run
        else if(stamina*20<maxStamina)recovering=true;     // exhausted
        if(stamina*20>=maxStamina*19)recovering=false;     // rested
        return recovering||stamina*4<maxStamina;
    }
};
// The game's own run / walk toggle as the mod knows it, and when to send it so the
// hero's gait follows the slider (WALK walks, every other stop runs).
//
// The hero's mode alone can't say which way the toggle is set: with it on run,
// the game still walks while the stick is only partly pushed - every start of a
// run begins that way (Cal's second test, 2026-10-25: rev2 read those starts as
// "the toggle is on walk", flipped it, and every start walked for a second). So:
//  - any run seen (mode 3) means the toggle is on run;
//  - only a walk with the stick pushed fully for FullWalkMs, not tired (Stamina),
//    means it is on walk (out of stamina the game walks whatever the toggle says:
//    the FRW test, 2026-10-25, toggled three times in 3 s after a long run);
//  - each toggle sent flips what we know, and what is seen within SettleMs after
//    it (the game may still report the old gait) is ignored;
//  - a new character (session) starts unknown: nothing is sent until one of the
//    above shows the toggle. Standing still, a known toggle follows the slider at once.
struct GaitSync {
    enum State {Unknown=0,Walk=1,Run=2};
    static constexpr float FullStick=.95f;
    static constexpr uint64_t SeenMs=700,FullWalkMs=600,SettleMs=500,CooldownMs=1000;
    State state=Unknown;
    uint64_t session=0,toggledAt=0,fullWalkSince=0;
    // gait: the last one seen (Walk/Run, 0 none) at seenAt; stick: its length 0..1; times in ms.
    // True: send the game's toggle now (then call Sent if it went out).
    // tired: the hero's stamina is low, so a walk shows nothing.
    bool Step(uint64_t sessionNow,int gait,uint64_t seenAt,float stick,bool wantWalk,bool shown,uint64_t now,bool tired=false) {
        if(sessionNow!=session){session=sessionNow;state=Unknown;toggledAt=fullWalkSince=0;}
        const bool fresh=gait&&seenAt>=toggledAt+SettleMs&&now>=seenAt&&now-seenAt<SeenMs;
        if(fresh&&gait==Run){state=Run;fullWalkSince=0;}
        else if(fresh&&gait==Walk&&stick>=FullStick&&!tired) {
            if(!fullWalkSince)fullWalkSince=now;
            else if(now-fullWalkSince>=FullWalkMs)state=Walk;
        } else fullWalkSince=0;
        return shown&&sessionNow&&state!=Unknown&&state!=(wantWalk?Walk:Run)&&now-toggledAt>=CooldownMs;
    }
    void Sent(bool wantWalk,uint64_t now){state=wantWalk?Walk:Run;toggledAt=now;fullWalkSince=0;}
};
// Right stick up with Movement speed's Stick toggle on ([gameplay] run_walk_toggle, off by
// default; owner 2026-10-25: "bring the toggle back and put an option to enable/disable",
// "with it being disabled by default"): each push switches between walking and Movement
// speed's run - from Walk, the full run, as the game's own toggle ran. A new character,
// turning the toggle off and a change of Movement speed start from Movement speed again.
// GaitSync then brings the game's own toggle in line.
struct StickToggle {
    uint64_t session=0;int percent=DefaultPercent;bool switched=false;
    // True: the hero should walk now. pushed: the stick went up this step.
    bool Step(uint64_t sessionNow,bool enabled,int setting,bool pushed) {
        if(sessionNow!=session||!enabled||setting!=percent){session=sessionNow;percent=setting;switched=false;}
        if(enabled&&pushed)switched=!switched;
        return Walks(setting)!=switched;
    }
    // Running though Movement speed is on Walk: at the full run (Scaled would give BASE RUN).
    bool FullRun()const{return switched&&Walks(percent);}
};
// The velocity the slider gives a native run velocity. Never above native, never below the base.
inline int Scaled(int native,int velocityPercent,int percent) {
    percent=std::clamp(percent,0,MaxPercent);   // WALK while still running (until the toggle lands): BASE RUN
    if(native<=0||BonusPoints(velocityPercent)<=0||percent>=MaxPercent)return native;
    const double base=BaseOf(native,velocityPercent);
    const int v=int(std::lround(base+(native-base)*percent/100.0));
    return std::clamp(v,int(std::ceil(base)),native);
}
}
