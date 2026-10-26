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
// A trap or bomb is about this big in flight, for finding what it hits first.
inline constexpr float Radius=.06f;
}
