#pragma once
#include <algorithm>
#include <cmath>

namespace framevr::aimreach {
// The game's own limit on how far away a skill's target may be: native
// skill-command validation rejects abs(target - player) > 50 cells on EITHER
// grid axis (3.3.93787: 0x4fe076 -> 0x4fe1f0). Everything that picks or places
// something far off for a skill - the aimed monster, a ground skill's point -
// stays inside that square, less a margin for the hero moving between the
// client's aim and the server's check, so no monster is chosen that a skill
// could not reach.
inline constexpr float CommandLimitCells=50.f,MarginCells=5.f;
inline constexpr float GameCells=CommandLimitCells-MarginCells;
// The farthest any in-reach point can be in a straight line (a corner of the square).
inline constexpr float CornerCells=GameCells*1.41422f;

// Is (dx, dy) from the hero inside the game's reach?
inline bool Within(float dx,float dy) {
    if(!std::isfinite(dx)||!std::isfinite(dy))return false;
    return std::max(std::fabs(dx),std::fabs(dy))<=GameCells;
}
// Shortens (dx, dy) along its heading until it is Within; false for nonsense.
inline bool Clamp(float& dx,float& dy) {
    if(!std::isfinite(dx)||!std::isfinite(dy))return false;
    const float axis=std::max(std::fabs(dx),std::fabs(dy));
    if(axis>GameCells){const float scale=GameCells/axis;dx*=scale;dy*=scale;}
    return true;
}
// How far the reach goes along a heading (need not be normalized), in cells.
inline float Along(float dirX,float dirY) {
    const float length=std::hypot(dirX,dirY);
    if(!std::isfinite(length)||length<1e-6f)return 0;
    float x=dirX/length*CornerCells*2,y=dirY/length*CornerCells*2;
    Clamp(x,y);
    return std::hypot(x,y);
}
}
