#pragma once
#include <cmath>

namespace framevr::stickgestures {
// The right stick's up and down in the world (its sideways pushes turn): up
// toggles running and walking, once per push; down, while held, shows every
// nearby loot label (the game's Show items, Alt). A push counts only when
// mostly vertical. From then until the stick returns near the centre it turns
// nothing, so a slightly diagonal push never snap-turns, and a sideways push
// that slides up or down is still a turn.
inline constexpr float Push=.65f,Vertical=1.6f,Centre=.3f;
struct Result {bool toggleRun=false,showLoot=false,owned=false;};
class Gestures {
    enum class Direction {None,Up,Down};
    Direction direction=Direction::None;
    bool armed=false;   // back near the centre since the last push or interruption
public:
    // Menus, wheels, settings or focus loss drop the push; a stick still held
    // when they end must come back to the centre before it counts again.
    void Reset(){direction=Direction::None;armed=false;}
    // x, y: the stick, -1 to 1 (up positive).
    Result Step(bool allowed,float x,float y) {
        if(!allowed||!std::isfinite(x)||!std::isfinite(y)){Reset();return {};}
        const float length=std::hypot(x,y);
        Result r;
        if(direction==Direction::None) {
            if(length<Centre)armed=true;
            else if(armed&&std::fabs(y)>=Push&&std::fabs(y)>=Vertical*std::fabs(x)) {
                direction=y>0?Direction::Up:Direction::Down;armed=false;
                r.toggleRun=direction==Direction::Up;
            } else if(length>=Push)armed=false;   // a turn
        } else if(length<Centre) {
            direction=Direction::None;armed=true;
        }
        r.showLoot=direction==Direction::Down;
        r.owned=direction!=Direction::None;
        return r;
    }
};
}
