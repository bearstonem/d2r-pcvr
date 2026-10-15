#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace framevr::vitals {
enum class Location { Wrist, Palm, Either, Off };
inline constexpr const char* LocationNames[]={"Wrist","Palm","Either","Off"};
inline bool Wrist(Location where){return where==Location::Wrist||where==Location::Either;}
inline bool Palm(Location where){return where==Location::Palm||where==Location::Either;}
constexpr float MinTilt=-60,MaxTilt=60,TiltStep=5;
inline float NormalizeTilt(float degrees){return std::isfinite(degrees)?std::clamp(degrees,MinTilt,MaxTilt):0.f;}

// Independent of combat ownership: an occluded right hand must not reset a
// deliberate left-palm presentation. Tracking/menu/world changes do reset it.
class PalmReveal {
    uint64_t session=0,epoch=0;
    double last=0,since=-1;
    bool shown=false;
public:
    bool Step(uint64_t world,uint64_t turn,double now,bool allowed,bool enter,bool keep) {
        if(!std::isfinite(now)){shown=false;since=-1;last=0;return false;}
        if(world!=session||turn!=epoch||now<=last||now-last>.15){shown=false;since=-1;}
        session=world;epoch=turn;last=now;
        if(!world||!allowed){shown=false;since=-1;return false;}
        if(shown){if(!keep){shown=false;since=-1;}return shown;}
        if(!enter){since=-1;return false;}
        if(since<0)since=now;
        shown=now-since>=.25;
        return shown;
    }
};
}
