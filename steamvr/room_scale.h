#pragma once
// Room-scale: you move freely in your room, and the stick moves the whole room (the hero's spot is its
// centre). The camera is the hero plus the head's offset from the tracking origin, turned by the stage's
// turn (transport.cpp View): camera = hero + Rotate(base, head - origin) * upm.
//
// A snap or smooth turn changes base. Kept as it was, the room turned about its centre, so standing a
// step from it a 45-degree snap threw the view sideways by most of that step. As in other VR games the
// room now turns about your head: the origin moves so the head's place in the world is unchanged.
// Tracking space is OpenXR's: +x right, +y up, -z ahead, metres.
#include "frame_math.h"
#include <cmath>

namespace framevr::roomscale {
using math::Vec;using math::Quat;
// The origin after the stage turned from `was` to `now`, so Rotate(now, head - origin) keeps
// Rotate(was, head - origin) on the ground; the origin's height is left as it was.
inline Vec TurnAboutHead(Vec origin,Vec head,Quat was,Quat now) {
    const Vec d=head-origin;
    const Vec turned=math::Rotate(math::Multiply(math::Conjugate(now),was),Vec{d.x,0,d.z});
    return {head.x-turned.x,origin.y,head.z-turned.z};
}
// How far the head stands from the room's centre on the ground, metres.
inline float Apart(Vec head,Vec origin){const float x=head.x-origin.x,z=head.z-origin.z;return std::sqrt(x*x+z*z);}
}
