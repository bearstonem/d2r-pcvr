#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

// Walking with the tabbed menu held in the left hand (Leather or Menu only). Under
// its menu the game runs no walk of its own, so the left stick becomes what a held
// left click on bare ground does: a walk toward a point a few cells ahead (vrcam's
// leatherwalk sends the game's map click on the UI thread, never with a unit under
// the game's pointer). Letting go must stop the hero there and then (a player's
// report, 2026-11-08: "the movement does not end immediately but after delay", the
// hero drifting on). The stop used to wait for the next 100 ms tick, went to the
// hero's own cell, which the game may take as nothing to do (leaving him on his
// way to the last point, up to 6 cells ahead), and was never sent at all when the
// menu closed as the stick was let go. Now:
//  - a let-go is seen on the next pass of vrcam's 1 ms timer, the menu open or not;
//  - the stop goes to the next cell on his way, StopAhead along the walk, rounded:
//    never his own cell, never one behind him (StopAhead > half a diagonal cell);
//  - it is watched: still walking CheckMs after a stop, it goes again (at most
//    MaxRepeats times), toward where he is actually going;
//  - walk clicks lead by LeadMin-LeadMax cells (fuller push, further), one every
//    ClickMs, so a stop the game ignored altogether leaves him at most that far on.
// Grid cells and the game's grid x / y; the stick as the game's screen sees it
// (x right, y up; vrcam's ViewStickToGame), any length past the dead zone.
namespace framevr::sheetwalk {
constexpr uint64_t ClickMs=50,SampleMs=100,CheckMs=300,SettleMs=600;
// Moving: cells in one SampleMs (4.5 cells/s; the hero walks 6 and runs 9 unhurried),
// so a hero still slowly reaching the stop's cell is not taken for one walking on.
constexpr float LeadMin=2.5f,LeadMax=4.5f,StopAhead=.75f,Moving=.45f;
constexpr int MaxRepeats=2;

struct Input {
    uint64_t now=0;
    bool world=false;     // in a game, the map click hook in: a stop can be sent
    bool held=false;      // ...and the menu held with the world round it: walk clicks too
    bool pushed=false;    // the left stick past the game's dead zone
    float sx=0,sy=0;      // that stick on the game's screen
    bool hero=false;float px=0,py=0;   // the hero's position, cells
};
enum class Kind:uint8_t{None,Walk,Stop};
// A walk click is the held left button (type 1) after the first (type 0); a stop a fresh press.
struct Click{Kind kind=Kind::None;int x=0,y=0;bool held=false;};
// A stop watched to its end: how far he went on after the let-go and when he stood
// (moving: still walking when the watch gave up).
struct Settled{bool due=false;float cells=0;uint64_t ms=0;int repeats=0;bool moving=false;};
struct Output{Click click{};Settled settled{};bool repeated=false;};

// (sx, sy) on the isometric screen as a grid direction: right = +x -y, up = -x -y.
inline bool GridDirection(float sx,float sy,float& gx,float& gy) {
    const float l=std::sqrt(sx*sx+sy*sy);
    if(!std::isfinite(l)||l<1e-4f)return false;
    gx=(sx-sy)/l*.70710678f;gy=(-sx-sy)/l*.70710678f;return true;
}
inline bool Cell(float x,float y,int& cx,int& cy) {
    if(!std::isfinite(x)||!std::isfinite(y)||x<.5f||y<.5f||x>=65534.5f||y>=65534.5f)return false;
    cx=int(std::lround(x));cy=int(std::lround(y));return true;
}
inline bool StopCell(float px,float py,float dx,float dy,int& cx,int& cy) {
    return Cell(px+dx*StopAhead,py+dy*StopAhead,cx,cy);
}

class Walker {
    enum class State{Idle,Walking,Stopping} state=State::Idle;
    uint64_t lastClick=0,stopAt=0,releaseAt=0,sampleAt=0,stoodAt=0;
    float dx=0,dy=0,fromX=0,fromY=0,sampleX=0,sampleY=0;
    bool moving=false;int repeats=0;
public:
    bool Busy() const{return state!=State::Idle;}
    bool Walking() const{return state==State::Walking;}
    bool Stopping() const{return state==State::Stopping;}
    void Reset(){*this=Walker{};}
    Output Step(const Input& in) {
        Output out;
        const bool hero=in.hero&&std::isfinite(in.px)&&std::isfinite(in.py);
        float gx=0,gy=0;
        if(in.world&&in.held&&in.pushed&&hero&&GridDirection(in.sx,in.sy,gx,gy)) {
            if(state==State::Walking&&in.now-lastClick<ClickMs)return out;
            const float push=std::min(std::sqrt(in.sx*in.sx+in.sy*in.sy),1.f);
            const float lead=LeadMin+(LeadMax-LeadMin)*push;
            int x=0,y=0;
            if(!Cell(in.px+gx*lead,in.py+gy*lead,x,y))return out;
            out.click={Kind::Walk,x,y,state==State::Walking};
            state=State::Walking;lastClick=in.now;dx=gx;dy=gy;
            return out;
        }
        if(state==State::Walking) {   // let go, the menu gone or the hero lost: stop him now
            int x=0,y=0;
            if(!in.world||!hero||!StopCell(in.px,in.py,dx,dy,x,y)){Reset();return out;}
            out.click={Kind::Stop,x,y,false};
            // The stick still pushed (the menu closed under it): the game's own walk takes
            // him on from here, so the stop is not watched and never sent again.
            if(in.pushed){Reset();return out;}
            state=State::Stopping;stopAt=releaseAt=sampleAt=in.now;
            fromX=sampleX=in.px;fromY=sampleY=in.py;repeats=0;moving=true;
            return out;
        }
        if(state!=State::Stopping)return out;
        if(!in.world||!hero||in.pushed){Reset();return out;}   // gone, or walking some other way
        if(in.now-sampleAt<SampleMs)return out;
        // Every SampleMs: still on his way? Decided only here, on a whole sample.
        const float mx=in.px-sampleX,my=in.py-sampleY,m=std::sqrt(mx*mx+my*my);
        moving=m>Moving;
        if(moving){dx=mx/m;dy=my/m;stoodAt=0;}
        else if(!stoodAt)stoodAt=sampleAt;   // standing since the last sample, at the latest
        sampleX=in.px;sampleY=in.py;sampleAt=in.now;
        if(moving&&repeats<MaxRepeats&&in.now-stopAt>=CheckMs) {
            int x=0,y=0;
            if(StopCell(in.px,in.py,dx,dy,x,y)){out.click={Kind::Stop,x,y,false};out.repeated=true;}
            ++repeats;stopAt=in.now;
            return out;
        }
        if(in.now-stopAt>=SettleMs&&(!moving||repeats>=MaxRepeats)) {
            const float ox=in.px-fromX,oy=in.py-fromY;
            out.settled={true,std::sqrt(ox*ox+oy*oy),(moving||!stoodAt?in.now:stoodAt)-releaseAt,repeats,moving};
            Reset();
        }
        return out;
    }
};
}
