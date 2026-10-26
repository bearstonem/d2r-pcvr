#pragma once
#include <algorithm>

namespace framevr::armpointing {
// Is the aiming hand held out to point? The hand's reach out in front of the
// head along where it points (metres; a hand hanging at the side or the bow
// string drawn back to the face has none) against the feature's "arm out"
// distance (cm). A hand pointing within 45 degrees of straight down is resting,
// not aiming (Jeff, 2026-10-20: a dangling hand nudged forward took the
// target). Each lets go a little past its edge (6 cm, 50 degrees), so the
// answer does not flicker. 0 cm: any reach. A feature keeps its own State.
inline constexpr float ReleaseM=.06f,DownOn=-.707f,DownOff=-.766f;
// The arm counts as pointing once held out a tenth of the player's span (Jeff:
// 15 cm on a 1.5 m span): VR Settings > Controller span, metres, to the arm-out cm.
inline constexpr float SpanShare=.1f;
inline float ArmOutCm(float spanM){return spanM*SpanShare*100.f;}
struct State {bool on=false;};
// forwardM: the hand's horizontal offset from the head along its horizontal
// aim; rayY: the aim's vertical part (-1 straight down).
inline bool Update(State& state,float forwardM,float rayY,float armOutCm) {
    const float on=armOutCm*.01f,need=state.on?std::max(0.f,on-ReleaseM):on;
    state.on=rayY>(state.on?DownOff:DownOn)&&(on<=0||forwardM>=need);
    return state.on;
}
}
