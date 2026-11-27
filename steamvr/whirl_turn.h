#pragma once
// Whirlwind on, steered while a whirl is under way (owner, 2026-11-25, after the first runs: "Let's make each
// whirl three times as long ... We should have full movement, just like as we're walking, running around the
// map, as we do normally, when we have whirlwind on, until we turn it off, or until it runs out."; letting go of
// the stick stops the hero, as a walk does).
//
// A whirl's straight path is laid at its start (SrvSt38) and the hero refuses commands until he reaches its end,
// but the game steps him toward his path's current point every tick and re-aims him after each step (PathStep
// 0x3812d0 -> 0x380fd0, 3.3.93787). A path laid anew while he whirls turns him on the next step. The game's own
// sequence for laying it is the start's (the whirl's collision mask, the target, PathCompute, the moving mask),
// with the footprint it reserves at the end moved along; vr/whirl_turn.cpp does that on the server's hero and
// then on the client's, on each one's own thread. Here: when and where to.
//
// Where: the ground left of the whirl, along the walk stick's way. The whirl keeps its length (one whirl's mana
// buys no more ground than the game gave it); only its heading changes. Let go of the stick after a push and the
// whirl ends StopCells on, as a walk stops. With less than MinLeftCells left the whirl ends as it is, and the
// next takes the new way. The client's hero follows the server's spot (Follow), so the two end together.
//
// Value-only: steamvr/whirl_turn_test.cpp.
#include "aim_reach.h"
#include <algorithm>
#include <cmath>

namespace framevr::whirlturn {
constexpr float TurnDegrees=5.f;      // a smaller turn is left alone
constexpr float RoundCells=.75f;      // a spot is a whole cell: below this much across, a turn is rounding
constexpr float MinLeftCells=2.f;     // less left: the whirl ends as it is
constexpr float StopCells=1.5f;       // let go: it ends this far on
constexpr double IntervalSeconds=.08; // at most one re-aim in this long on each side (the server steps every 40 ms)
constexpr float FollowCells=2.5f;     // the client's whirl is the server's when its end is this near

struct Whirl {float heroX=0,heroY=0,endX=0,endY=0;};   // grid cells: the hero, the end of his whirl
struct Steer {bool pushed=false;float gx=0,gy=0;bool stop=false;};   // the stick's grid way (unit) while pushed; let go
struct Aim {bool turn=false;bool stop=false;int x=0,y=0;};

inline bool Finite(const Whirl& w){return std::isfinite(w.heroX)&&std::isfinite(w.heroY)&&std::isfinite(w.endX)&&std::isfinite(w.endY);}

// The re-aim for the whirl now: a new end (whole cells), or none.
inline Aim Decide(const Whirl& w,const Steer& s) {
    Aim a;
    if(!Finite(w))return a;
    const float dx=w.endX-w.heroX,dy=w.endY-w.heroY,left=std::hypot(dx,dy);
    if(!(left>=MinLeftCells))return a;
    const float hx=dx/left,hy=dy/left;
    float tx=0,ty=0;
    if(s.pushed) {
        const float n=std::hypot(s.gx,s.gy);
        if(!std::isfinite(n)||n<.5f)return a;
        const float gx=s.gx/n,gy=s.gy/n;
        // Turned enough to matter, and more than a whole cell's rounding at this distance.
        const float least=std::max(TurnDegrees*.0174533f,std::atan2(RoundCells,left));
        if(hx*gx+hy*gy>=std::cos(least))return a;
        float ox=gx*left,oy=gy*left;
        if(!aimreach::Clamp(ox,oy))return a;
        tx=w.heroX+ox;ty=w.heroY+oy;
    } else if(s.stop) {
        if(left<=StopCells+RoundCells)return a;
        tx=w.heroX+hx*StopCells;ty=w.heroY+hy*StopCells;a.stop=true;
    } else return a;
    a.x=int(std::lround(tx));a.y=int(std::lround(ty));
    if(a.x<1||a.y<1||a.x>65534||a.y>65534)return Aim{};
    if(a.x==int(std::lround(w.endX))&&a.y==int(std::lround(w.endY)))return Aim{};
    a.turn=true;
    return a;
}

// The client's hero takes the server's new end when his whirl is the one the server turned: his end where that
// whirl's began (origin), or where the server's was before this turn (from), or he has followed this whirl before
// (followed); not when he is already at the new one (to). True: lay it.
inline bool Follow(float endX,float endY,int originX,int originY,int fromX,int fromY,int toX,int toY,bool followed) {
    if(!std::isfinite(endX)||!std::isfinite(endY))return false;
    const auto by=[&](int x,int y,float within){return std::hypot(endX-float(x),endY-float(y))<=within;};
    if(by(toX,toY,.5f))return false;
    return followed||by(originX,originY,FollowCells)||by(fromX,fromY,FollowCells);
}
}
