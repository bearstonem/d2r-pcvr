#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <mutex>
#include "frame_math.h"

// Teleport aimed with the controller along an arc (owner, 2026-11-09, after a session: "we need to be able to pick
// how far we want to teleport easily, as well as pointing the direction we want to teleport"; then, of the arc:
// "combine it with the awesome ring ... where the arc would land"). While the Sorceress's Teleport aim is up
// (spell_motion.h Output::aiming: the right arm out and steady), the right controller's own pointing - not the
// shoulder-to-fist line - throws an arc: its heading is the way, its tilt the distance, and where the arc meets
// the floor is the landing, where Teleport's sigil and pillar stand (telegraph::TeleportMark). The yank casts there.
//
// The arc is a throw: the speed that carries FarM at 45 degrees from the floor, so a controller held level at
// chest height lands about sqrt(2 * height * FarM) ahead, tilted up 45 degrees (or more) at FarM, and pointed down
// nearer, to NearM. Its far end gives the finest control (the throw's range changes slowest near 45 degrees).
// Room metres throughout; the floor is the room's (interactions Input::floor).
namespace framevr::teleportarc {
using math::Vec;
inline constexpr int Points=16;                       // drawn as Points-1 segments (D2R_TeleportArc.fxh)
inline constexpr float MaxUpDeg=45.f,MaxDownDeg=70.f; // tilt counted, degrees above / below level
struct Arc {
    bool ok=false;
    float distance=0;          // m along the floor from under the controller to the landing
    Vec land{};                // the landing, on the floor
    Vec way{};                 // the heading, level, unit
    std::array<Vec,Points> points{};   // from the controller to the landing
};
// origin and direction: the controller's aim pose (direction need not be unit); floor: the floor's height.
// No arc for a controller at or under the floor, pointing straight up or down, or nonsense.
inline Arc Compute(Vec origin,Vec direction,float floor,float nearM,float farM) {
    Arc a;
    const float h=origin.y-floor,level=std::hypot(direction.x,direction.z);
    if(!std::isfinite(h)||!std::isfinite(level)||!std::isfinite(direction.y)||!(h>.05f)||!(level>1e-3f)||
       !(nearM>0)||!(farM>nearM)||!std::isfinite(farM))return a;
    const float ux=direction.x/level,uz=direction.z/level;
    constexpr float rad=.0174532925f;
    const float tilt=std::clamp(std::atan2(direction.y,level),-MaxDownDeg*rad,MaxUpDeg*rad);
    // The throw (gravity 1): v^2 = farM carries farM at 45 degrees from the floor.
    const float v=std::sqrt(farM),up=v*std::sin(tilt),ahead=v*std::cos(tilt);
    const float t=up+std::sqrt(up*up+2*h);
    const float d=std::clamp(ahead*t,nearM,farM);
    // The drawn arc leaves the controller along its tilt and meets the floor at d: y(x) = h + slope x - k x^2.
    // Pointed down so steeply that it would bow upward (the clamp to nearM), a straight line instead.
    const float slope=std::tan(tilt),k=(h+slope*d)/(d*d);
    for(int i=0;i<Points;++i) {
        const float x=d*float(i)/float(Points-1);
        const float y=k<0?h*(1-x/d):std::max(0.f,h+slope*x-k*x*x);
        a.points[size_t(i)]={origin.x+ux*x,floor+y,origin.z+uz*x};
    }
    a.points[Points-1]={origin.x+ux*d,floor,origin.z+uz*d};
    a.ok=true;a.distance=d;a.land=a.points[Points-1];a.way={ux,0,uz};
    return a;
}
// The arc handed to the shader: each point relative to the hero in render units (vrcam: the eye positions are
// in the same frame), stamped when it was placed. The OpenXR thread places it every frame the aim is up; the
// render thread draws it while it is newer than ShowMs.
inline constexpr uint64_t ShowMs=150;
struct Drawn {
    std::array<Vec,Points> points{};
    uint64_t at=0;
};
class Board {
    std::mutex mutex;
    Drawn drawn{};
public:
    void Place(const std::array<Vec,Points>& points,uint64_t now){std::lock_guard lock(mutex);drawn={points,now};}
    void Clear(){std::lock_guard lock(mutex);drawn={};}
    Drawn Get(){std::lock_guard lock(mutex);return drawn;}
};
inline bool Shown(const Drawn& d,uint64_t now){return d.at&&now>=d.at&&now-d.at<ShowMs;}
inline Board& Current(){static Board board;return board;}
}
