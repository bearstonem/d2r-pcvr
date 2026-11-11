#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

// Frame generation's wait, taken late ([render] frame_generation_late_wait, on
// unless set to 0; steamvr/PERFORMANCE.md "Frame generation's wait").
// While OFXR Bridge 0.2.1 makes frames on SteamVR its presenter thread owns the
// runtime's frame calls, and the game's xrWaitFrame is answered by the bridge:
// no sleep to a display slot, then a block until the GPU has finished the
// bridge's last synthesis, which is queued on the game's own queue behind the
// pair before. Called at the start of a pair, that block is the GPU wait
// [render] defer_gpu_wait took out, 3-6 ms on nearly every pair.
// Late: the pair's head and hands are found first, for the time this pair's
// xrWaitFrame is expected to give (the last one's time and one period on), the
// first eye is drawn, and only then, before the first image of the pair is
// taken, come xrWaitFrame and xrBeginFrame - by when that synthesis is done.
// The calls stay in their order on the one thread, one wait to a begin to an
// end, so a wrong guess about the bridge costs timing and nothing else. (The
// next frame's wait from a second thread, while this one is drawn, does not
// work on 0.2.1: with its presenter up no generated frame is made any more,
// without it the game's thread hangs in xrEndFrame on SteamVR.)
// The bridge's presenter cannot be asked for. What shows it: the period the
// wait gives is twice the shortest this session gave (the bridge doubles it
// only then). SteamVR alone can give a longer period to a late caller, so the
// sign can be wrong; the order of the calls does not depend on it.
// Value-only: nothing here calls OpenXR or reads a clock.
namespace framevr::latewait {
enum class Why {On,KeyOff,NotGenerating,NotPair,NotSteamVr,NoFrameBefore,NotDrawn,PeriodNotDoubled,Gap};
inline const char* Reason(Why w){
    constexpr const char* names[]={
        "xrWaitFrame comes after the first eye's draw",
        "frame_generation_late_wait is 0",
        "frame generation is not making frames",
        "no pair of both eyes is drawn",
        "the runtime is not SteamVR",
        "the frame before had no wait of its own to go by",
        "the runtime did not want the frame before drawn",
        "the wait's period is not twice the session's shortest (OFXR Bridge's presenter is not up)",
        "a gap in frames"};
    return names[int(w)];
}
// What this frame is asked with: the key, the readout Active, a pair of both
// eyes (not by turns, not a menu's frame), SteamVR, and the clock (ms).
struct Ask {bool wanted=false,generating=false,pair=false,steamVr=false;double nowMs=0;};
struct Plan {
    Why why=Why::KeyOff;
    int64_t time=0,period=0;   // late: the time the head and hands are found for, and the period given on
    bool Late()const{return why==Why::On;}
};
// A frame further apart from the last than this is not gone by: menus, loading, a hitch.
inline constexpr double GapMs=250;
// The period counts as doubled from this many times the session's shortest.
inline constexpr double DoubledFrom=1.8;
struct Tracker {
    int64_t shortest=0;                  // the shortest period a wait of this session gave: the headset's own
    int64_t time=0,period=0;             // the last wait's answer
    bool drawn=false,known=false;        // it wanted the frame drawn; there is such an answer, from the frame before
    double posedAtMs=0;                  // when the frame before found its head
    // A new session: nothing is known of it.
    void Reset(){*this={};}
    // The frame's xrWaitFrame answered (in either order).
    void Waited(int64_t displayTime,int64_t displayPeriod,bool shouldRender){
        time=displayTime;period=displayPeriod;drawn=shouldRender;known=displayPeriod>0;
        if(displayPeriod>0&&(!shortest||displayPeriod<shortest))shortest=displayPeriod;
    }
    // The frame's wait or begin failed, or it had none: the next frame waits first.
    void Failed(){known=false;}
    bool Doubled()const{return shortest>0&&double(period)>=DoubledFrom*double(shortest);}
    // The time this frame's own wait should give: the last one's and a period on,
    // and for each whole headset frame the game ran over that, one more of those.
    int64_t Expected(double sincePosedMs)const{
        const double over=sincePosedMs-double(period)*1e-6,own=double(shortest)*1e-6;
        const int64_t slipped=own>0&&over>0?int64_t(std::llround(over/own)):0;
        return time+period+std::min<int64_t>(slipped,8)*shortest;
    }
    // Decided once a frame, before it is opened; Posed() then notes when it found its head.
    Plan Decide(const Ask& a)const{
        Plan p;
        p.why=!a.wanted?Why::KeyOff:!a.generating?Why::NotGenerating:!a.pair?Why::NotPair:!a.steamVr?Why::NotSteamVr:
              !known?Why::NoFrameBefore:!drawn?Why::NotDrawn:!Doubled()?Why::PeriodNotDoubled:
              !(posedAtMs>0&&a.nowMs-posedAtMs<GapMs)?Why::Gap:Why::On;
        if(p.Late()){p.time=Expected(a.nowMs-posedAtMs);p.period=period;}
        return p;
    }
    void Posed(double nowMs){posedAtMs=nowMs;}
};
}
