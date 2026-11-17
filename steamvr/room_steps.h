#pragma once
// Room follows you (VR Settings > Comfort > Body, beside Seated play; [gameplay] room_steps, on by default since 2026-11-08).
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
// is drawn, a skill is cast, the game carries the hero (Leap's take-off and flight), a hand
// swings, a menu, wheel or VR Settings is open, or the real left stick walks.
//
// The origin follows only what pays the step off (Follow): the game moving the hero itself -
// Leap's flight, Whirlwind, Charge, a knock-back - is no step of yours. Followed whole, the
// origin flew with him, the camera stayed behind, and only a reload put it back (a player,
// 2026-11-12: "it disconnects the player from the in game character ... I ended up outside the
// walls while my actual character was inside"). And a watchdog: your neck FarMetres from the
// hero's spot for FarSeconds, with no walk of his closing on it, is a view come off him, not a
// step owed - the room is put back under you, as F11 does (Output::recentre).
// Tracking space is OpenXR's: +x right, +y up, -z ahead, metres.
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
constexpr double BlockedSeconds=.6;   // pushed this long while the hero walked under BlockedMetres: a wall - stop pushing
constexpr float BlockedMetres=.03f;
constexpr float UnblockMetres=.15f;   // then a step this much elsewhere tries again
// The hero covers 0.1-0.2 m a game frame (about 4 m/s, 2026-11-08 log) and stops on the game's grid after
// the stick lets go: the push ends early by the distance he will coast, so he glides in under you.
constexpr double CoastSeconds=.05;
constexpr float MaxCoastMetres=.15f;
constexpr double RateSeconds=.08;     // the closing speed measured over this window
constexpr double TailSeconds=.30;     // after the push ends, the origin follows the hero this long (he stops on a grid)
constexpr float Push=.55f;            // stick length: past the game's dead zone (.316), short of a run (.95)
constexpr float SwingSpeed=1.2f;      // a hand faster than this, m/s, is a swing: pause
constexpr double SwingSeconds=.4;     // and stay paused this long after it
constexpr float LungeSpeed=1.5f;      // the head faster than this, m/s, starts nothing
// The hero farther than this in one head view, m, is a jump, a leap or a teleport: not followed. By distance,
// not speed: the head view is built twice a pair, back to back, and walking moved him 0.1-0.2 m between
// them in under a millisecond (2026-11-08 log: every walking frame taken for a jump at the old 4 m/s).
constexpr float MaxHeroStep=.75f;
// But by distance a Leap is no jump: 25 cells (~10 m) in ~0.75 s is 0.1-0.25 m a head view (2026-11-12 log: the
// hero 9941.6,10111.5 -> 9912.8,10141.3 world units, 4.27 a metre), so it is told by Follow and Pause::Carried.
// The watchdog: Room follows you keeps the hero within a step (StartMetres) of you; this far, this long, with no
// walk closing on it faster than CatchUpSpeed, he is not coming - the view is put back on him.
constexpr float FarMetres=1.5f;
constexpr double FarSeconds=1.5;
constexpr float CatchUpSpeed=.3f;     // m/s
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

// Carried: the game carries the hero itself - a Leap or Leap Attack from its cast through the take-off crouch,
// then while his model is in the air (vrcam -> transport HeroCarried). Cast alone held only while the cast's
// button or motion pressed, and he flew on for most of a second with the follow back on.
enum class Pause : uint8_t {None,Off,Untracked,Menu,Stick,Grip,Trigger,Bow,Cast,Swing,Carried};
inline const char* PauseName(Pause p) {
    switch(p){
        case Pause::None:return "none";case Pause::Off:return "off";case Pause::Untracked:return "untracked";
        case Pause::Menu:return "menu";case Pause::Stick:return "stick";case Pause::Grip:return "grip";
        case Pause::Trigger:return "trigger";case Pause::Bow:return "bow";case Pause::Cast:return "cast";
        case Pause::Swing:return "swing";case Pause::Carried:return "the game carries the hero";
    }
    return "?";
}
struct Input {
    double time=0;           // seconds
    bool on=false,tracked=false,menu=false,stick=false,grip=false,trigger=false,bow=false,cast=false,carried=false;
    Vec eye{};Quat head{};   // the head's tracked pose
    Vec origin{};            // the tracking origin now
    Vec hand[2]{};bool handTracked[2]{};
};
struct Output {
    bool push=false;float x=0,y=0;   // the stick to write, when push
    bool follow=false;               // the origin follows the hero this frame (Shift, as far as Follow takes it)
    float offset=0;                  // the neck's distance from the hero, m
    Pause pause=Pause::None;
    bool blocked=false;              // the hero could not come closer (a wall): not pushed until you step elsewhere
    // The watchdog: the room is put back under you - transport moves the origin by shift (tracking space, on the
    // ground; the chase has moved its own already), so the camera is on the hero again. apart: how far you
    // stood from his spot, for how long (heldFor), m and s.
    bool recentre=false;Vec shift{};float apart=0;double heldFor=0;
};

class Chase {
public:
    // Every input frame. The reference neck is re-anchored whenever the origin moved without us
    // (F11, Calibrate body, a world load, a recentered yaw): the head is then at the origin.
    Output Step(const Input& in) {
        Output out;
        // Lost tracking keeps the anchor: the step taken is still owed when the head comes back.
        if(in.tracked&&!(known&&Same(in.origin,origin)))Anchor(in);
        if(in.tracked){neck=Neck(in.eye,in.head);neckKnown=true;}
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
                  in.carried?Pause::Carried:in.time<swingUntil?Pause::Swing:Pause::None;
        // The watchdog, through every pause but the ones that know nothing of you (off, untracked) or keep the
        // world still for you (a menu): FarMetres for FarSeconds, unless a walk is closing on it.
        const bool watched=out.pause!=Pause::Off&&out.pause!=Pause::Untracked&&out.pause!=Pause::Menu;
        const bool catchingUp=out.pause==Pause::None&&walking&&!blocked&&closing>CatchUpSpeed;
        if(watched&&out.offset>=FarMetres&&!catchingUp) {
            if(farFrom<0)farFrom=in.time;
            if(in.time-farFrom>=FarSeconds) {
                out.recentre=true;out.shift={offset.x,0,offset.z};out.apart=out.offset;out.heldFor=in.time-farFrom;
                out.blocked=blocked;
                origin=origin+out.shift;reference=reference+out.shift;
                walking=blocked=false;farSince=farFrom=-1;tailUntil=0;rateAt=0;closing=0;heroMoved=0;
                out.offset=Horizontal(Neck(in.eye,in.head)-reference);
                return out;
            }
        } else farFrom=-1;
        if(out.pause!=Pause::None) {
            walking=false;farSince=-1;tailUntil=0;   // a pause also ends the follow: Leap and Teleport are not steps
            return out;
        }
        if(blocked) {   // the hero met a wall: wait for a step elsewhere
            if(Horizontal(offset-blockedAt)<UnblockMetres&&out.offset>StopMetres){out.blocked=true;return out;}
            blocked=false;
        }
        if(in.time-rateAt>=RateSeconds||rateAt<=0) {   // how fast the gap closes, m/s (positive closing)
            closing=rateAt>0?float((rateOffset-out.offset)/(in.time-rateAt)):0.f;rateOffset=out.offset;rateAt=in.time;
        }
        if(walking) {
            const float coast=std::min(MaxCoastMetres,std::max(0.f,closing)*float(CoastSeconds));
            if(out.offset<=StopMetres+coast){walking=false;tailUntil=in.time+TailSeconds;}
            else if(heroMoved>=BlockedMetres){heroMoved=0;checkAt=in.time;}   // the hero walks (the origin followed him)
            else if(in.time-checkAt>=BlockedSeconds){walking=false;blocked=true;blockedAt=offset;tailUntil=in.time+TailSeconds;out.blocked=true;}
        } else if(out.offset>=StartMetres&&headSpeed<=LungeSpeed) {
            if(farSince<0)farSince=in.time;
            if(in.time-farSince>=HoldSeconds){walking=true;farSince=-1;heroMoved=0;checkAt=in.time;}
        } else farSince=-1;
        if(walking){out.push=true;StickToward(offset,in.head,out.x,out.y);}
        out.follow=walking||in.time<tailUntil;
        return out;
    }
    // How much of an origin move (Shift, for the hero's move since the last head view) the follow takes: what pays
    // your step off. A move that leaves you no farther from the hero - a walk toward you, give or take its angle -
    // is taken whole; any other only by its part toward you, and never past you. So the follow can never put the
    // camera farther from the hero than it was: whatever the game does with him, it moves the camera with him.
    Vec Follow(Vec shift) const {
        if(!known||!neckKnown)return {};
        const Vec owed{neck.x-reference.x,0,neck.z-reference.z};   // the hero's spot to your neck: Step's offset
        const Vec d{shift.x,0,shift.z};
        const float before=Horizontal(owed);
        if(Horizontal(owed-d)<=before)return d;
        if(!(before>1e-4f))return {};
        const Vec toward=owed*(1.f/before);
        return toward*std::clamp(math::Dot(d,toward),0.f,before);
    }
    // The origin moved by d with the hero (transport View, d from Follow): the reference neck moves with it. moved:
    // the hero's whole move, m (d's length unless given) - a hero the game carries elsewhere (Whirlwind) is no wall.
    void Shifted(Vec d,float moved=-1){origin=origin+d;reference=reference+d;heroMoved+=moved>=0?moved:Horizontal(d);}
    // The hero jumped (a teleport, a waypoint, another area: transport View, over MaxHeroStep in a view): the walk
    // after your step and the wall it met are left where they were; a step still owed starts over from the hold.
    void Jumped(){walking=blocked=false;farSince=-1;tailUntil=0;}
    // The room turned about the head (room_scale.h TurnAboutHead): the origin is now `to`, and the anchored
    // neck turns with the room by `rel` (the stage's turn, as tracking space sees it), so a turn is no step.
    void Turned(Vec to,Quat rel){if(!known)return;reference=to+math::Rotate(rel,reference-origin);origin=to;}
    void Reset(){*this=Chase{};}
    bool Walking()const{return walking;}
private:
    static bool Same(Vec a,Vec b){return Len(a-b)<1e-4f;}
    void Anchor(const Input& in){origin=in.origin;reference=Neck(in.eye,in.head);known=true;walking=false;blocked=false;farSince=farFrom=-1;tailUntil=0;}
    bool known=false,walking=false,blocked=false,eyeKnown=false,handKnown[2]{},neckKnown=false;
    Vec origin{},reference{},eye{},hand[2]{},blockedAt{},neck{};
    // farSince: the step held past StartMetres since; farFrom: the watchdog's FarMetres since.
    double lastTime=0,farSince=-1,farFrom=-1,tailUntil=0,swingUntil=0,checkAt=0,rateAt=0;float heroMoved=0,rateOffset=0,closing=0;
};
}
