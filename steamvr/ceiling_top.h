#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

// The dungeon ceiling at the level's highest floor from the start (owner, 2026-11-07: "force ceiling height upon entering
// a dungeon or dungeon level to the absolute highest it's going to be while you're in there, because currently I walk up
// the stairs and the ceiling stays the height it was before until I walk forward then it raises up").
// The ceiling (the game-drawn slab and the shader's vault alike) stood on the lowest floor the hero had stood on. Now its
// top is the highest floor of the hero's level: every room the game builds there reports its border floors (its height
// plus each edge, room_stitch.h), and the hero's own floor counts too. At a new level it is taken whole; a higher room
// built later (rooms are built up to six rings out) raises it gently, never lowers it.
namespace framevr::ceilingtop {
// The highest and lowest border floor of the rooms built in a few recent levels (the game's DRLG level).
struct LevelFloors {
    static constexpr int Slots=8;
    struct Slot {uintptr_t level=0;float hi=0,lo=0;uint64_t used=0;};
    Slot slots[Slots]{};
    uint64_t clock=0;
    void Add(uintptr_t level,float height,const float edges[4]) {
        if(!level||!std::isfinite(height))return;
        float hi=-INFINITY,lo=INFINITY;
        for(int i=0;i<4;++i)if(std::isfinite(edges[i])){hi=std::max(hi,height+edges[i]);lo=std::min(lo,height+edges[i]);}
        if(!std::isfinite(hi)){hi=lo=height;}
        Slot* s=nullptr;
        for(auto& x:slots)if(x.level==level){s=&x;break;}
        if(!s) {
            s=&slots[0];
            for(auto& x:slots)if(x.used<s->used)s=&x;
            *s={level,hi,lo,0};
        } else {s->hi=std::max(s->hi,hi);s->lo=std::min(s->lo,lo);}
        s->used=++clock;
    }
    bool Highest(uintptr_t level,float& hi) const {
        if(!level)return false;
        for(const auto& x:slots)if(x.level==level){hi=x.hi;return true;}
        return false;
    }
};

// The ceiling's top floor as shown: the highest known, taken whole for Settle s after a new level, eased up after that.
struct Top {
    static constexpr double Settle=2.0;   // s after entering a level: the rooms built then are the level as he arrives
    static constexpr double Rise=0.35;    // s, the time constant of a later rise
    bool have=false;
    uint32_t gen=0;uintptr_t level=0;
    double since=0,at=0;
    float target=0,shown=0;
    // gen: the area's look (bumped on each new area); level: the hero's DRLG level (0 unknown); heroY his floor;
    // built/haveBuilt: the level's highest built floor. Returns the floor the ceiling's top stands on.
    float Step(double now,uint32_t areaGen,uintptr_t heroLevel,float heroY,bool haveBuilt,float built) {
        if(!std::isfinite(heroY))return shown;
        if(!have||areaGen!=gen||(heroLevel&&heroLevel!=level)) {
            have=true;gen=areaGen;level=heroLevel;since=now;target=shown=heroY;
        }
        if(!level&&heroLevel)level=heroLevel;
        target=std::max(target,heroY);
        if(haveBuilt&&std::isfinite(built))target=std::max(target,built);
        const double dt=std::clamp(now-at,0.0,0.1);at=now;   // a gap between calls is no reason to jump
        if(now-since<Settle||shown>target)shown=target;
        else if(target>shown){shown+=float((target-shown)*(1.0-std::exp(-dt/Rise)));if(target-shown<0.01f)shown=target;}
        return shown;
    }
};
}
