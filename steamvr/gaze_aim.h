#pragma once
#include "gaze.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

// Eye-gaze aiming (owner, 2026-11-08: "for headsets that have eye tracking ... the monster we look at
// is the one we target ... for supported headsets, of course"). With an eye tracker the runtime shares
// (XR_EXT_eye_gaze_interaction, the binding foveated rendering already makes) and VR Settings >
// Gameplay > Targeting > Aim with your eyes On (beside Aim with your view; On by default, shown only
// with a tracker), the eyes' source (aimrules::Eyes) takes where the eyes look instead of where the
// head points: the monster outlined while no hand points, the last choice of every cast's order, and a
// cast's free aim with no monster and no hand held out. The hands, and Aim with your view's 30 degrees
// round the head for them, stay as they were.
namespace framevr::gazeaim {
struct Vec {float x=0,y=0,z=0;};
inline float Dot(Vec a,Vec b){return a.x*b.x+a.y*b.y+a.z*b.z;}

// A sample counts for aiming when it is located and tracked (tracked: the runtime's valid and tracked
// bits) and, when the runtime stamps it, fresh as foveation wants it (gaze::Fresh). An unstamped one (0)
// still counts here: the tracked bits say the eyes are seen, and a slightly old direction only holds a
// pick a moment longer, where stale foveation would blur the spot you look at.
inline bool Usable(bool tracked,int64_t display,int64_t sample){return tracked&&(sample==0||gaze::Fresh(display,sample));}

// A direction in the left eye's view (OpenXR's: x right, y up, -z ahead) into the world through that
// eye's camera axes (vrcam's SkyView: right, up, back, three floats each), unit. False if degenerate.
inline bool ToWorld(const float axes[9],Vec local,Vec& out){
    out={local.x*axes[0]+local.y*axes[3]+local.z*axes[6],local.x*axes[1]+local.y*axes[4]+local.z*axes[7],
         local.x*axes[2]+local.y*axes[5]+local.z*axes[8]};
    const float l=std::sqrt(Dot(out,out));
    if(!std::isfinite(l)||l<1e-4f)return false;
    out={out.x/l,out.y/l,out.z/l};
    return true;
}

// A monster's body for the gaze: upright from BodyLowM to BodyHighM above its floor spot, in the
// player's metres (the world is scaled to the player's eye height).
constexpr float BodyLowM=.2f,BodyHighM=1.5f;
// Of how far above or below a body's middle the gaze crosses it (as seen, in degrees), the share
// DegreesOff adds: two bodies on one line of sight (looking past a near monster's head at the middle of
// one behind it) go to the one looked at nearer its middle. Side to side counts in full.
constexpr float MidWeight=.25f;
// Degrees the gaze (from eye along dir) is off a monster's body, the upright segment low..high above
// its floor spot (eye, floor and the heights in one frame, y up): the angle to the body's nearest point
// - where the gaze's own climb or drop meets the monster's distance, kept on the body, so looking at
// its head, its feet or between leaves the side-to-side angle alone - plus MidWeight of that point's
// height off the body's middle. 180 with no direction.
inline float DegreesOff(Vec eye,Vec dir,Vec floor,float low,float high){
    const float dx=floor.x-eye.x,dz=floor.z-eye.z,flat=std::hypot(dx,dz);
    if(!std::isfinite(Dot(dir,dir))||Dot(dir,dir)<1e-8f)return 180.f;
    const float ahead=dx*dir.x+dz*dir.z;   // the monster's distance along the gaze's heading, times the gaze's level part
    float y=eye.y;
    if(ahead>1e-4f)y=eye.y+dir.y*flat*flat/ahead;   // tan(the gaze's slope) / cos(its heading off the monster), at its distance
    else if(flat<1e-4f)y=dir.y<0?floor.y+low:floor.y+high;   // right under the eyes: the end looked toward
    const auto between=[](Vec a,Vec b){
        const float l=std::sqrt(Dot(a,a)*Dot(b,b));
        if(!std::isfinite(l)||l<1e-8f)return 0.f;
        return std::acos(std::clamp(Dot(a,b)/l,-1.f,1.f))*57.2957795f;
    };
    const Vec on{dx,std::clamp(y,floor.y+low,floor.y+high)-eye.y,dz},middle{dx,floor.y+.5f*(low+high)-eye.y,dz};
    const float off=between(on,dir)+MidWeight*between(on,middle);
    return std::isfinite(off)?off:180.f;
}

// The pick: a monster within CatchDeg of the gaze can be taken (the nearest to it), and the one taken
// stays while it is within KeepDeg, unless another is SwitchDeg nearer the gaze. A change waits until
// it has been the choice for DwellMs: a glance's jump crosses the monsters between in 20-60 ms, and
// the outline does not flash on each. A pick that died, or left sight or reach, gives way at once.
constexpr float CatchDeg=6.f,KeepDeg=10.f,SwitchDeg=2.f;
constexpr uint64_t DwellMs=100;
struct Candidate {uint32_t id=0;float angle=180;};   // in sight and in the game's reach; degrees off the gaze (DegreesOff)
class Picker {
    bool waiting=false;uint32_t pending=0;uint64_t since=0;
public:
    void Reset(){waiting=false;pending=0;since=0;}
    // current: the eyes' pick now (0 none). The pick from now: current, or what it changes to.
    uint32_t Step(const Candidate* c,int n,uint32_t current,uint64_t now){
        int best=-1,held=-1;
        for(int i=0;c&&i<n;++i){
            if(!std::isfinite(c[i].angle))continue;
            if(current&&c[i].id==current)held=i;
            if(c[i].angle<=CatchDeg&&(best<0||c[i].angle<c[best].angle))best=i;
        }
        const uint32_t bestId=best>=0?c[best].id:0;
        if(current&&held<0){Reset();return bestId;}   // gone: the next at once
        uint32_t want=current;
        if(!current||c[held].angle>KeepDeg)want=bestId;   // nothing, or the eyes left it: the nearest caught, or none
        else if(best>=0&&bestId!=current&&c[best].angle+SwitchDeg<c[held].angle)want=bestId;   // a clearly nearer one
        if(want==current){Reset();return current;}
        if(!waiting||pending!=want){waiting=true;pending=want;since=now;}
        if(now-since<DwellMs)return current;
        Reset();return want;
    }
};

// Which line the eyes' pick takes (Fallback): the head's view, the gaze, or the gaze's last pick held
// through a blink.
enum class Ray : uint8_t {Head,Gaze,Held};
// A blink (100-300 ms) or a dropped sample holds the gaze's pick: no flicker to the head's and back.
// Gaze lost for BlinkMs, the head's view picks again until usable samples have come for RegainMs.
constexpr uint64_t BlinkMs=350,RegainMs=150;
class Fallback {
    bool on=false,validRun=false,lostRun=false;uint64_t validSince=0,lostSince=0;
public:
    void Reset(){on=validRun=lostRun=false;}
    bool On()const{return on;}   // the gaze picks (Gaze or Held)
    // wanted: the option On with a tracker and something using the eyes' monster; valid: this tick's
    // sample (gazeaim::Usable, and recent).
    Ray Step(bool wanted,bool valid,uint64_t now){
        if(!wanted){Reset();return Ray::Head;}
        if(valid){
            lostRun=false;
            if(on)return Ray::Gaze;
            if(!validRun){validRun=true;validSince=now;}
            if(now-validSince<RegainMs)return Ray::Head;
            on=true;return Ray::Gaze;
        }
        validRun=false;
        if(!on)return Ray::Head;
        if(!lostRun){lostRun=true;lostSince=now;}
        if(now-lostSince<BlinkMs)return Ray::Held;
        on=lostRun=false;return Ray::Head;
    }
};
}
