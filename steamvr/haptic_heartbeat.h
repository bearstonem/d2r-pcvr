#pragma once
#include <algorithm>
#include <cmath>
#include "haptic_strength.h"

// The low-life heartbeat, felt: a "lub-dub" in both controllers that quickens
// and strengthens as life drops. Value-only. The transport gives it the same
// level (heartbeat.h LifeLevel / Hush / Level: 0 above Interface > Warnings >
// Heartbeat starts at, one step per 10 % of life below it, 1 under 10 %) and
// tempo (heartbeat::Bpm) as the audible beat, so the two agree on when the heart
// is working and how hard; the audio keeps its own clock, so the two are in
// tempo, not locked in phase.
//
// Not felt: Off, in a menu, dead or with life unknown. Any of those stops it at
// once (no dub left hanging) and the next beat starts fresh on a lub.
namespace framevr::haptics {
struct HeartPulse {Sample sample{};int beat=0;};   // beat: 0 none, 1 lub, 2 dub
constexpr float HeartDefaultStart=60;   // % life, when the audible heartbeat is Off (its own default start)
constexpr float HeartHz=55;             // a low thump where the controller honours frequency
// The lub at this level: light when the heart first stirs, firm in the worst band.
inline Sample HeartLub(float level) {
    level=std::isfinite(level)?std::clamp(level,0.f,1.f):0;
    return {.30f+.50f*level,int(std::lround(45+15*level)),HeartHz};
}
// The dub: softer and shorter, as a heart's second sound is.
inline Sample HeartDub(float level) {
    level=std::isfinite(level)?std::clamp(level,0.f,1.f):0;
    return {.20f+.35f*level,int(std::lround(35+10*level)),HeartHz};
}
// Seconds from lub to dub: about a third of the beat, 0.21 s at rest (the
// built-in recording's own spacing), never under 0.14 s.
inline double HeartDubDelay(float bpm) {
    const double period=60./std::clamp(std::isfinite(bpm)?double(bpm):70.,30.,200.);
    return std::clamp(period*.3,.14,.21);
}
class Heartbeat {
    double lubAt=-1,dubAt=-1;float dubLevel=0;
public:
    void Reset(){*this=Heartbeat{};}
    bool Beating()const{return lubAt>=0;}
    // now: seconds, monotonic. level: 0..1 (0 = calm). felt: enabled, in the world, alive, no menu.
    // At most one pulse per call; the caller plays it on both hands (Category::Alert).
    HeartPulse Step(double now,float level,float bpm,bool felt) {
        if(!felt||!std::isfinite(now)||!std::isfinite(level)||level<=0){Reset();return {};}
        level=std::min(level,1.f);
        if(lubAt>=0&&(now<lubAt-5||now>lubAt+5)){lubAt=dubAt=-1;}   // a clock jump or a long stall: start over
        if(dubAt>=0&&now>=dubAt) {
            const bool late=now-dubAt>.08;const float at=dubLevel;dubAt=-1;
            if(!late)return {HeartDub(at),2};
        }
        if(lubAt<0||now>=lubAt) {
            const double period=60./std::clamp(std::isfinite(bpm)?double(bpm):70.,30.,200.);
            // Keep the rhythm when on time; after a hitch restart from now rather than hurrying to catch up.
            lubAt=(lubAt>=0&&now-lubAt<.08?lubAt:now)+period;
            dubAt=lubAt-period+HeartDubDelay(bpm);dubLevel=level;
            return {HeartLub(level),1};
        }
        return {};
    }
};
}
