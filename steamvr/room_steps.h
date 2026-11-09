#pragma once
// Room follows you (VR Settings > Body, beside Seated play; [gameplay] room_steps, on by default since 2026-11-08).
//
// The hero never moves with the headset: the camera is the hero plus the tracked head's offset
// from the tracking origin (transport.cpp View), 1:1 and unbounded, so a real step leaves the
// hero's body behind until F11 or Calibrate body re-anchors it (Discord, 2026-11-04 and 11-08).
//
// When the player's neck is more than StartMetres from where the hero stands, this pushes the
// game's own walk toward it, through the left stick's path (a partly pushed stick, so the
// hero walks), and the game's collision stops him at walls. While he walks, transport moves
// the tracking origin by exactly the distance he covered (Shift), so the camera never moves on
// its own: the hero catches up underneath. The hero's position is never written.
//
// Measured at the neck, not the eyes, so looking down or turning the head is not a step.
// Paused (no push, and the origin no longer follows) while a grip or trigger is held, the bow
// is drawn, a skill is cast, a hand swings, a menu, wheel or VR Settings is open, or the real
// left stick walks. Tracking space is OpenXR's: +x right, +y up, -z ahead, metres.
#include "frame_math.h"
#include <algorithm>
#include <cmath>

namespace framevr::roomsteps {
using math::Vec;using math::Quat;

// Owner, 2026-11-08: "the room center becomes where we physically move" - so monsters attack where you stand,
// and the game's hero (with his legs) stays under you: a small dead zone, a short hold.
constexpr float StartMetres=.20f;     // the neck this far from the hero starts a walk
constexpr float StopMetres=.07f;      // and this near ends it
constexpr double HoldSeconds=.10;     // the offset held past StartMetres this long first
constexpr double BlockedSeconds=.6;   // walking this long without closing BlockedMetres: a wall - stop pushing
constexpr float BlockedMetres=.03f;
constexpr float UnblockMetres=.15f;   // then a step this much elsewhere tries again
constexpr double TailSeconds=.30;     // after the push ends, the origin follows the hero this long (he stops on a grid)
constexpr float Push=.55f;            // stick length: past the game's dead zone (.316), short of a run (.95)
constexpr float SwingSpeed=1.2f;      // a hand faster than this, m/s, is a swing: pause
constexpr double SwingSeconds=.4;     // and stay paused this long after it
constexpr float LungeSpeed=1.5f;      // the head faster than this, m/s, starts nothing
constexpr float MaxHeroSpeed=4.f;     // the hero faster than this, m/s, is a jump, a leap or a teleport: not followed
// The neck from the eyes, in the head's own frame: below and behind them.
inline constexpr Vec EyeToNeck{0,-.10f,.08f};

inline float Len(Vec v){return std::sqrt(math::Dot(v,v));}
inline float Horizontal(Vec v){return std::sqrt(v.x*v.x+v.z*v.z);}
inline Quat YawOf(Quat q){return math::Yaw(math::Heading(q));}
// The neck's point in tracking space: the eyes turn about it, so pitch and roll leave it in place.
inline Vec Neck(Vec eye,Quat head){return eye+math::Rotate(head,EyeToNeck);}
// The stick that walks toward a tracking-space offset, in the view's convention (x right, y ahead
// on the ground, length Push): the left stick's frame turns with the camera's yaw (vrcam
// ViewStickToGame), which is the head's yaw, and the stage-to-world turn cancels out.
inline void StickToward(Vec offset,Quat head,float& x,float& y,float push=Push) {
    const Vec local=math::Rotate(math::Conjugate(YawOf(head)),Vec{offset.x,0,offset.z});
    const float l=Horizontal(local);
    if(!(l>1e-4f)){x=y=0;return;}
    x=local.x/l*push;y=-local.z/l*push;
}
// The tracking-origin move that keeps the camera still while the hero moves heroDelta world units:
// the camera is hero + Rotate(base, head - origin) * upm, so origin += Rotate(base^-1, delta) / upm.
inline Vec Shift(Vec heroDelta,Quat base,float upm) {
    if(!(upm>0))return {};
    const Vec d=math::Rotate(math::Conjugate(base),Vec{heroDelta.x,0,heroDelta.z});
    return {d.x/upm,0,d.z/upm};
}

enum class Pause : uint8_t {None,Off,Untracked,Menu,Stick,Grip,Trigger,Bow,Cast,Swing};
inline const char* PauseName(Pause p) {
    switch(p){
        case Pause::None:return "none";case Pause::Off:return "off";case Pause::Untracked:return "untracked";
        case Pause::Menu:return "menu";case Pause::Stick:return "stick";case Pause::Grip:return "grip";
        case Pause::Trigger:return "trigger";case Pause::Bow:return "bow";case Pause::Cast:return "cast";
        case Pause::Swing:return "swing";
    }
    return "?";
}
struct Input {
    double time=0;           // seconds
    bool on=false,tracked=false,menu=false,stick=false,grip=false,trigger=false,bow=false,cast=false;
    Vec eye{};Quat head{};   // the head's tracked pose
    Vec origin{};            // the tracking origin now
    Vec hand[2]{};bool handTracked[2]{};
};
struct Output {
    bool push=false;float x=0,y=0;   // the stick to write, when push
    bool follow=false;               // the origin follows the hero this frame (Shift)
    float offset=0;                  // the neck's distance from the hero, m
    Pause pause=Pause::None;
    bool blocked=false;              // the hero could not come closer (a wall): not pushed until you step elsewhere
};

class Chase {
public:
    // Every input frame. The reference neck is re-anchored whenever the origin moved without us
    // (F11, Calibrate body, a world load, a recentered yaw): the head is then at the origin.
    Output Step(const Input& in) {
        Output out;
        // Lost tracking keeps the anchor: the step taken is still owed when the head comes back.
        if(in.tracked&&!(known&&Same(in.origin,origin)))Anchor(in);
        const Vec offset=known?Neck(in.eye,in.head)-reference:Vec{};
        out.offset=in.tracked?Horizontal(offset):0;
        const double dt=lastTime>0?in.time-lastTime:0;
        // Hands and head speeds, for swings and lunges.
        bool swing=false;float headSpeed=0;
        if(dt>1e-3&&dt<.25) {
            for(int h=0;h<2;++h)if(in.handTracked[h]&&handKnown[h]&&Len(in.hand[h]-hand[h])/float(dt)>SwingSpeed)swing=true;
            if(eyeKnown)headSpeed=Len(in.eye-eye)/float(dt);
        }
        for(int h=0;h<2;++h){hand[h]=in.hand[h];handKnown[h]=in.handTracked[h];}
        eye=in.eye;eyeKnown=in.tracked;lastTime=in.time;
        if(swing)swingUntil=in.time+SwingSeconds;
        out.pause=!in.on?Pause::Off:!in.tracked||!known?Pause::Untracked:in.menu?Pause::Menu:in.stick?Pause::Stick:
                  in.grip?Pause::Grip:in.trigger?Pause::Trigger:in.bow?Pause::Bow:in.cast?Pause::Cast:
                  in.time<swingUntil?Pause::Swing:Pause::None;
        if(out.pause!=Pause::None) {
            walking=false;farSince=-1;tailUntil=0;   // a pause also ends the follow: Leap and Teleport are not steps
            return out;
        }
        if(blocked) {   // the hero met a wall: wait for a step elsewhere
            if(Horizontal(offset-blockedAt)<UnblockMetres&&out.offset>StopMetres){out.blocked=true;return out;}
            blocked=false;
        }
        if(walking) {
            if(out.offset<=StopMetres){walking=false;tailUntil=in.time+TailSeconds;}
            else if(out.offset<checkOffset-BlockedMetres){checkOffset=out.offset;checkAt=in.time;}
            else if(in.time-checkAt>=BlockedSeconds){walking=false;blocked=true;blockedAt=offset;tailUntil=in.time+TailSeconds;out.blocked=true;}
        } else if(out.offset>=StartMetres&&headSpeed<=LungeSpeed) {
            if(farSince<0)farSince=in.time;
            if(in.time-farSince>=HoldSeconds){walking=true;farSince=-1;checkOffset=out.offset;checkAt=in.time;}
        } else farSince=-1;
        if(walking){out.push=true;StickToward(offset,in.head,out.x,out.y);}
        out.follow=walking||in.time<tailUntil;
        return out;
    }
    // The origin moved by d with the hero (transport View): the reference neck moves with it.
    void Shifted(Vec d){origin=origin+d;reference=reference+d;}
    // The room turned about the head (room_scale.h TurnAboutHead): the origin is now `to`, and the anchored
    // neck turns with the room by `rel` (the stage's turn, as tracking space sees it), so a turn is no step.
    void Turned(Vec to,Quat rel){if(!known)return;reference=to+math::Rotate(rel,reference-origin);origin=to;}
    void Reset(){*this=Chase{};}
    bool Walking()const{return walking;}
private:
    static bool Same(Vec a,Vec b){return Len(a-b)<1e-4f;}
    void Anchor(const Input& in){origin=in.origin;reference=Neck(in.eye,in.head);known=true;walking=false;blocked=false;farSince=-1;tailUntil=0;}
    bool known=false,walking=false,blocked=false,eyeKnown=false,handKnown[2]{};
    Vec origin{},reference{},eye{},hand[2]{},blockedAt{};
    double lastTime=0,farSince=-1,tailUntil=0,swingUntil=0,checkAt=0;float checkOffset=0;
};
}
