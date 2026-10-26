#pragma once
#include "frame_math.h"
#include "stick_gestures.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

// Minimap zoom. The wrist / palm map and the wheel's MAP tab are one 512x512
// image of the game's corner map, centered on the hero, so they share one zoom
// (it is changed at the wrist or palm; the wheel's MAP tab only shows it).
// While you look at the wrist or palm map, a mostly vertical push of the right
// stick is the map's: up zooms in, down zooms out. Left / right still turns,
// but not during a zoom push, until the stick comes back to the center.
// Jeff, 2026-10-21: "the map zoom in and out should not be buttons. When
// looking at the map, the right stick up and down zooms in and out."
// The zoom is not saved, and each new game starts at 1x: "Restarting should
// reset the map to default levels in case people forget how to zoom in and out."
namespace framevr::mapzoom {
// 1 is the whole native map in the frame, as before: the source is the game's
// own corner map, so there is nothing further out to zoom to. Pulling down
// until it stops is the way home.
constexpr float Min=1,Max=4;
// Map view = Full map / Full top-down (wrist_full_map.h): 1 is the game's full map at
// its widest, and the game redraws it closer as you zoom in, so it stays sharp to 16x.
constexpr float FullMax=16;
static_assert(FullMax>=Max);
constexpr float Doublings=1.5f;   // per second at full stick: 1x to 4x in 1.3 s, to 16x in 2.7 s
constexpr float Dead=.3f;         // stick travel ignored, either way
constexpr float Notch=.25f;       // a light tick each quarter step (faster as it zooms in); a firmer one at either stop
// Looking at the map: its center within LookIn degrees of the head's forward
// starts it, and it lasts until past LookOut, so a glance at the map's edge
// does not flicker the stick between map and game.
constexpr float LookIn=22,LookOut=30;
// The right stick's up / down has two owners: this map while it is looked at,
// otherwise the world gestures (stick_gestures.h: up runs / walks, down shows
// loot labels). Looking at the map drops a world push from the next frame, so
// zooming never toggles running; a stick still held when the look ends must come
// back to the center before it counts in the world again. A push already held
// when the look starts zooms straight away.
inline bool WorldGestures(bool world,bool mapLooked){return world&&!mapLooked;}
// A new game (a new local player) starts back at 1x. session: the local player
// handle, 0 between games; seen: the last nonzero one.
inline bool NewGame(uint64_t& seen,uint64_t session) {
    const bool fresh=session&&session!=seen;
    if(session)seen=session;
    return fresh;
}
inline float Clamp(float z,float max=Max){return std::isfinite(z)?std::clamp(z,Min,max):Min;}
// A quarter step up to 4x; past it (the full map) a whole step, so the ticks do not buzz.
inline int NotchOf(float z){
    z=Clamp(z,FullMax);
    return z<=Max?int(std::floor((z-Min)/Notch+1e-4f)):int((Max-Min)/Notch)+int(std::floor(z-Max+1e-4f));
}
// head: the headset pose; panel: the shown map's center, in the same space.
inline bool Looking(math::Vec headP,math::Quat headQ,math::Vec panel,bool was) {
    const auto toward=panel-headP;
    const float distance2=math::Dot(toward,toward);
    if(!std::isfinite(distance2)||distance2<1e-4f)return false;
    const float cosine=math::Dot(math::Rotate(headQ,{0,0,-1}),toward*(1/std::sqrt(distance2)));
    return std::isfinite(cosine)&&cosine>std::cos((was?LookOut:LookIn)*.0174532925f);
}
struct Input {
    double time=0;
    bool looking=false;   // the wrist / palm map is shown and looked at (Looking)
    float stick=0;        // right stick: up +1, down -1
    float x=0;            // right stick: right +1, left -1
    float max=Max;        // the closest zoom: Max, or FullMax on the full map
};
struct Output {
    bool owned=false;     // the right stick's up / down is the map's this frame
    bool turnOwned=false; // a zoom push holds the stick: left / right turn nothing until the center
    bool zooming=false;   // the stick is pushed past the dead zone
    bool started=false,stopped=false;   // a zoom gesture's edges, for the log
    float zoom=Min;
    int tick=0;           // 1 a quarter crossed, 2 a stop reached
};
class Gesture {
    bool zooming=false;
    bool held=false;      // a mostly vertical push started, and the stick has not come back to the center
    double last=0;
    float zoom=Min;
public:
    // Tracking or focus lost: no gesture continues. The zoom stays for this game.
    void Reset(){zooming=held=false;last=0;}
    // A new game: back to the whole map.
    void NewGame(){Reset();zoom=Min;}
    float Zoom()const{return zoom;}
    Output Step(const Input& in) {
        Output out;
        const double dt=last>0&&in.time>last?std::min(in.time-last,.1):0;
        last=in.time;
        const bool valid=in.looking&&std::isfinite(in.stick)&&std::isfinite(in.x);
        const float y=valid?std::clamp(in.stick,-1.f,1.f):0,x=valid?std::clamp(in.x,-1.f,1.f):0;
        // Like the world gestures: only a mostly vertical push zooms, so a turn
        // that drifts up or down stays a turn; once zooming, the push holds the
        // stick until it comes back near the center.
        if(!held)held=std::fabs(y)>=Dead&&std::fabs(y)>=stickgestures::Vertical*std::fabs(x);
        else if(std::hypot(x,y)<stickgestures::Centre)held=false;
        const bool now=held&&std::fabs(y)>=Dead;
        out.started=now&&!zooming;out.stopped=!now&&zooming;zooming=now;
        // a lower ceiling (the full map turned off) brings the zoom back under it at once
        const float top=std::isfinite(in.max)?std::clamp(in.max,Min,FullMax):Max;
        zoom=Clamp(zoom,top);
        if(now&&dt>0) {
            const float before=zoom;
            zoom=Clamp(zoom*std::exp2(std::copysign((std::fabs(y)-Dead)/(1-Dead),y)*Doublings*float(dt)),top);
            if(zoom!=before)out.tick=zoom==Min||zoom==top?2:NotchOf(zoom)!=NotchOf(before)?1:0;
        }
        out.owned=in.looking;out.turnOwned=held;out.zooming=zooming;out.zoom=zoom;
        return out;
    }
};
}
