#pragma once
// Whirlwind steered by the walk stick (players on the mod's Discord, 2026-11-19: "It just goes in a straight
// line for me which makes it mostly a waste of mana to use as you cant stay in the pack of enemies. On desktop
// you go the direction of your mouse cursor so maybe we could make it so it goes the direction you are pressing
// your joystick?"; another: "I thought this should work that way from the start").
//
// A whirl cannot be turned once it has begun. The game's own server code (D2MOO, the open rewrite of
// 1.10f / 1.14d, whose Whirlwind row D2R's skills.txt matches: start 38, do 76, anim SQ): Whirlwind's start
// makes the hero uninterruptable until the whirl reaches its spot, and every new command while it lasts, a
// press or a held button's repeat, is refused. As it ends, a held button's next repeat starts the next whirl at
// once (the same skill may re-enter its own mode). Desktop "follows the cursor" that way: each whirl of the
// chain goes to where the cursor is as it starts. So the stick steers between whirls, and each whirl costs its
// mana; nothing is pressed or held here, only where the game's own commands aim is chosen:
//  - a button or trigger: while the walk stick is pushed past the game's own dead zone, each Whirlwind cast and
//    each repeat of the held button aims Ahead cells along the stick (vrcam NativeSpellAim). Holding the button
//    chains the whirls, each where the stick points as it starts;
//  - the T: the stick as the T casts gives its whirl's way, at the T's own length (the transport's motion
//    cast; MotionSpot). Still one whirl a T.
// The stick is the walk's (transport walkLx / walkLy, after Walk direction's turn): where you look, or where the
// walk stick's controller points, the sticks swapped left-handed. At rest, everything aims as before: the aimed
// monster, the free aim or the hand, and the T straight ahead. While pushed, the stick wins over the aimed
// monster.
//
// Value-only: steamvr/whirl_steer_test.cpp.
#include "aim_reach.h"
#include "walk_direction.h"
#include <cmath>

namespace framevr::whirlsteer {
using math::Vec;
constexpr int Whirlwind=151;
// The game's own dead zone for its walk stick (0x1210DA0: 10349 of 32767, not rescaled): steering begins where
// walking does. x right, y ahead, each -1..1.
constexpr float DeadZone=10349.f/32767.f;
inline bool Pushed(float x,float y){return std::isfinite(x)&&std::isfinite(y)&&x*x+y*y>DeadZone*DeadZone;}
// Cells along the stick for a button's whirl: the T's own length (motions_barbarian.h whirlAhead), so the two
// whirl alike. A whirl is one cast's mana however far it goes, and the stick can turn the chain only as each one
// ends: 10 cells, about 0.8 s at the 12 cells a second the owner's whirls went (2026-11-12), turns it often
// enough without spending a cast on every few steps; shorter would turn sooner and cost more mana for the ground.
constexpr float Ahead=10;
// The stick (the view's frame) as a unit way on the game's grid. right and ahead are the view's level right and
// ahead in the world (vrcam's StickFrame), as walking takes them (walkdir::OnGround, also in ViewStickToGame).
// The grid's axes are the world's x and z (vrcam HandGridDir: measured, neither turned nor mirrored), and a
// level way needs no floor shear (world_coordinates.h). False with the stick at rest.
inline bool GridWay(float x,float y,Vec right,Vec ahead,float& gx,float& gy) {
    if(!Pushed(x,y))return false;
    const Vec w=walkdir::OnGround(x,y,right,ahead);
    const float n=std::hypot(w.x,w.z);
    if(!std::isfinite(n)||n<1e-4f)return false;
    gx=w.x/n;gy=w.z/n;return true;
}
// The spot cells along a unit grid way from the hero (grid cells), kept inside the game's reach (aimreach).
inline bool Spot(float heroX,float heroY,float gx,float gy,float cells,float& x,float& y) {
    if(!std::isfinite(heroX)||!std::isfinite(heroY)||!std::isfinite(gx)||!std::isfinite(gy)||!std::isfinite(cells)||cells<=0)return false;
    float dx=gx*cells,dy=gy*cells;
    if(!aimreach::Clamp(dx,dy))return false;
    x=heroX+dx;y=heroY+dy;return true;
}
// The T's spot from the stick: cells ahead of where you face and to the right (spellmotion::Spot's terms; the
// transport's SpotCell turns it by the head's heading, the same frame the walk stick is read in). The length is
// the T's, whatever the stick's push. False with the stick at rest: the T's own spot stands.
inline bool MotionSpot(float x,float y,float cells,float& ahead,float& right) {
    if(!Pushed(x,y)||!std::isfinite(cells)||cells<=0)return false;
    const float n=std::hypot(x,y);
    ahead=cells*y/n;right=cells*x/n;return true;
}
}
