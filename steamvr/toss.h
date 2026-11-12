#pragma once
#include "interactions.h"
#include "world_coordinates.h"

// Traps and bombs thrown to place (#224): the Assassin's Fire Blast, Shock
// Web, Blade Sentinel, the three sentries and the two wakes. A quick press
// casts where you point and a steady hold casts natively as before; a throw
// places the cast where its arc lands. The throw picks the spot only: the
// game's own cast, reach, damage and placement rules stay.
namespace framevr::toss {
inline bool Skill(int id) {
    switch(id) {
        case 251:case 256:case 257:case 261:case 262:case 271:case 272:case 276:return true;
        default:return false;
    }
}
// The model held while winding up (the potion cache's, from the game's own
// data): Fire Blast's bomb, Shock Web's caltrops, Blade Sentinel's blade; the
// sentries share one model, and the wakes another, as the game shares them.
inline uint32_t Model(int id) {
    switch(id) {
        case 251:return interaction::Code('t','f','b');
        case 256:return interaction::Code('t','s','w');
        case 257:return interaction::Code('t','b','s');
        case 261:case 271:case 276:return interaction::Code('t','s','t');
        case 262:case 272:return interaction::Code('t','w','k');
        default:return 0;
    }
}
// The grid cell under a world point (metres) on the floor under the player:
// cells are the unsheared grid at half a cell per rendered unit.
inline bool Cell(math::Vec world,float floor,float unitsPerMetre,float& x,float& y) {
    if(!std::isfinite(unitsPerMetre)||unitsPerMetre<1||unitsPerMetre>12)return false;
    const auto grid=coordinates::GridPlane({world.x,floor,world.z});
    x=grid.x*unitsPerMetre*.5f;y=grid.z*unitsPerMetre*.5f;
    return std::isfinite(x)&&std::isfinite(y)&&x>=1&&y>=1&&x<=65534&&y<=65534;
}
// The toss released at room point p with room velocity v (metres, y up), in
// world metres: the stage maps room to world, and the player's own world
// velocity (running, interaction::Carry) is carried along.
inline math::Vec WorldVelocity(const interaction::Stage& stage,math::Vec v,math::Vec carry) {return math::Rotate(stage.base,v)+carry;}
// Where it lands in grid cells, with the floor under the player at release
// (world metres) as the landing plane.
inline bool Landing(const interaction::Stage& stage,float floor,float unitsPerMetre,math::Vec p,math::Vec v,float& x,float& y,math::Vec carry={}) {
    if(!stage.known||!std::isfinite(unitsPerMetre)||unitsPerMetre<1||unitsPerMetre>12)return false;
    const auto worldP=stage.World(p),worldV=WorldVelocity(stage,v,carry);
    const double t=interaction::LandingTime(worldP,worldV,floor);
    if(t<=0)return false;
    return Cell(interaction::Ballistic(worldP,worldV,float(t)),floor,unitsPerMetre,x,y);
}
// The cell's floor point in world metres: Cell's inverse.
inline math::Vec World(float x,float y,float floor,float unitsPerMetre) {
    return coordinates::RenderGround({x*2.f/unitsPerMetre,0,y*2.f/unitsPerMetre},floor);
}
// The aimed throw (owner, 2026-11-10: "like Sorceress's teleport mechanic, it draws a sigil on the ground,
// where we can target, and when you toss it ... potions land on that target sigil, if the target sigil goes on
// top of a monster, then that's used"). While the trap or potion is held (the trigger down), the throwing
// hand's pointer throws Teleport's arc and a sigil shows where it lands. The hand aims only while it is held
// out in front of the head and slow: the wind-up and the throw itself leave the sigil where it was. The throw
// then lands on the sigil, on the monster under it when one stands within SnapCells of it, and the thrown
// bottle is sent on the arc that ends there (Launch).
inline constexpr float AimAhead=.20f;    // m ahead of the head, along its heading
inline constexpr float AimSpeed=.8f;     // m/s: the hand at most this fast
inline constexpr float AimFarCells=30.f; // the arc's reach (the game's own reach clamps it)
inline constexpr float SnapCells=2.5f;   // the sigil takes a monster this near its centre (a landing circle's radius)
inline bool Aims(float ahead,float speed){return std::isfinite(ahead)&&std::isfinite(speed)&&ahead>=AimAhead&&speed<=AimSpeed;}
// How far the throwing hand is ahead of the head along its heading, in metres (behind: negative). Either
// hand's: a hand out to the left reads as the same hand out to the right.
inline float Ahead(math::Vec head,math::Quat headQ,math::Vec hand) {
    return -math::Rotate(math::Conjugate(math::Yaw(math::Heading(headQ))),hand-head).z;
}
// The velocity that carries a throw from p to target (world metres) under the throw's gravity: as fast along
// the floor as it was thrown (speed, m/s; at least LaunchMinSpeed), in the air LaunchMinTime to LaunchMaxTime.
inline constexpr float LaunchMinSpeed=4.f,LaunchMinTime=.35f,LaunchMaxTime=1.5f;
inline bool Launch(math::Vec p,math::Vec target,float speed,math::Vec& v) {
    const float dx=target.x-p.x,dz=target.z-p.z,along=std::hypot(dx,dz);
    if(!std::isfinite(along+target.y+p.y+speed))return false;
    const float t=std::clamp(along/std::max(speed,LaunchMinSpeed),LaunchMinTime,LaunchMaxTime);
    v={dx/t,(target.y-p.y+.5f*interaction::ThrowGravity*t*t)/t,dz/t};
    return true;
}
// A trap or bomb is about this big in flight, for finding what it hits first.
inline constexpr float Radius=.06f;
}
