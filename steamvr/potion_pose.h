#pragma once
#include "frame_math.h"
namespace framevr::potions {
// Original models are normalized once by prepare_potions.py: 18 cm tall,
// centered on the neck axis, +Y points out of the bottle's opening.
constexpr float Height=.18f;
// Distance to the bottle's full vertical body, with padding supplied by the grab test.
inline float GrabDistance(math::Vec hand,math::Vec center,math::Quat rotation) {
    auto local=math::Rotate(math::Conjugate(rotation),hand-center);
    local.y-=std::clamp(local.y,-Height*.5f,Height*.5f);
    return std::sqrt(math::Dot(local,local));
}
inline math::Quat HeldRotation(math::Quat grip) {
    // Pitch the model -90 degrees around grip-local X: its opening follows
    // grip -Z instead of grip +Y, which points back at the player.
    return math::Multiply(grip,{-.70710678f,0,0,.70710678f});
}
inline math::Vec Center(math::Vec hand,math::Quat rotation) {
    return hand+math::Rotate(HeldRotation(rotation),{0,.025f,0});
}
inline math::Vec Opening(math::Vec hand,math::Quat rotation) {
    return Center(hand,rotation)+math::Rotate(HeldRotation(rotation),{0,Height*.5f,0});
}
inline bool AtMouth(math::Vec hand,math::Quat rotation,math::Vec head,math::Quat headRotation,bool sipping=false) {
    const auto mouth=head+math::Rotate(headRotation,{0,-.10f,-.08f});
    const auto difference=Opening(hand,rotation)-mouth;
    // Leave room for the headset shell and face-position differences. A
    // roughly 50-degree tilt is intentional drinking; requiring near-horizontal
    // alignment made ordinary sips fail. Wider exit bounds prevent a small
    // tracking wobble from repeatedly resetting the continuous dwell.
    const float radius=sipping?.19f:.16f;
    return math::Dot(difference,difference)<radius*radius&&
        math::Rotate(HeldRotation(rotation),{0,1,0}).y<(sipping?.75f:.65f);
}
}
