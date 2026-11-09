#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include "frame_math.h"
#include "native_panels.h"

// Floating map: pull a copy of the wrist map into the world, put it back at the wrist.
// Jeff, 2026-10-21: "detach the minimap from the wrist into a floating panel ... resize it with both lasers";
// 2026-10-29: "I should be able to rotate, move, pitch the map in any 3D axis"; 2026-11-01: "the wrist map stays ...
// we can grab one copy of the map to put in our field of view ... if we drag the floating map back close to the
// wrist, the floating map goes away"; 2026-11-03: "it should grab just from pointing at the direction".
//  - Right Grip with the right controller pointing at the raised wrist map (or roughly at it, within PointDeg past
//    its edge; or the right hand on it) pulls out a copy there and holds it. The wrist keeps its own map: both quads
//    draw the one map picture (transport's mapEye), so the copy is always what VR Settings > Interface > Wrist > Map view shows.
//  - Held, the copy follows the controller 1:1 in position, yaw, pitch and roll (its pose is kept in the controller's
//    frame); the right stick forward / back pushes it along the laser or pulls it in (0.25 to 3 m). Let go: it stays
//    there in the room, turned to face you (FaceMe). Point at it again to grab it: anywhere on it, or within PointMargin
//    of its side past an edge (never more than PointDeg past it), so its hit areas grow and shrink with its size (Jeff
//    2026-11-07) and a large copy never takes presses aimed only near it.
//  - Both grips within BothGripsS, in either order, with either hand pointing at it: spread the hands to grow it,
//    squeeze to shrink; it stays centered between the hands. Left Grip while Right Grip holds it does the same.
//  - Let go with its center within AwayM of the wrist map: put away (gone). A light buzz says so while held there.
//  - Shown while it is within LookDeg of where you look (so it never covers the fight); held, always.
//  - A light buzz when the right laser comes onto a map Right Grip would take, only while a copy is out.
//  - The right stick's up / down zooms the floating map while the right laser is anywhere on it (out.pointed; Jeff
//    2026-11-06: "zooming on the map if pointing at the map and using the up and down, thats a deliberate movement").
//    The wrist map zooms as upstream, by looking at it.
//  - Save and Exit or a new game puts it back on the wrist (Jeff 2026-11-07: "have it not save the map location on save
//    and exit, should default back to the wrist"): nothing is remembered, and the next pull is the shipped size. A load
//    screen inside the same game (a waypoint, an Act change) keeps it where it is. Nothing is written to disk.
// With no copy pulled nothing is drawn, captured or buzzed, and the stick is never taken.
namespace framevr::floatmap {
namespace math=framevr::math;
constexpr float Deg=.0174532925f;

// ---- The shipped values, tuned in the headset 2026-11-06. One line each.
constexpr float SizeM=.20f;          // a pulled copy's side, m (the wrist map is 24 cm)
constexpr float AwayM=.25f;          // put away: let go this close to the wrist map puts it away, m
constexpr float LookDeg=30;          // look to show: shown within this of your view, degrees (0: always)
constexpr bool FaceMe=true;          // Face me: turned upright to face your eyes on release
constexpr float PointDeg=15;         // point to grab, the wrist map: a press this far past its edge still takes it, degrees
constexpr float PointMargin=.65f;    // point to grab, the floating map: a press this many of its sides past its edge takes it
                                     // (scales with the map: 13 cm at the 20 cm size), never more than PointDeg past it
constexpr float BothGripsS=.25f;     // two-hand size: both grips within this, either order, s (250 ms)
constexpr bool AimBuzz=true;         // Aim buzz: a light buzz when the right laser comes onto a map (a copy out)
// The copy stays in the room where you let it go.

constexpr float Press=.6f;            // a grip counts as held past this (interactions.h's own grips[] threshold)
constexpr float MinSize=.10f,MaxSize=1.50f;   // the square's side after a two-hand resize, m
constexpr float LookOutExtra=5;       // look to show: it hides again only past the angle plus this (no flicker)
constexpr float WarmExtra=15;         // look to show: the capture starts this much sooner, so the picture is there
constexpr double GapS=.5;             // no step for this long (a load, a hitch): nothing held carries over
constexpr float MinReach=.25f,MaxReach=3.f;   // push / pull: the grabbed point's distance along the laser, m
constexpr float StickDead=.2f;        // the right stick's push / pull starts past this
constexpr float StickRest=.25f;       // a stick the map held is the game's again once both axes are within this
constexpr float PushSpeed=1.5f;       // m/s along the laser at a full push
constexpr double AimRearmS=.3;        // Aim buzz: the laser off the map this long before it buzzes again (no buzz on jitter)
constexpr float TouchM=.12f;          // the right hand this close to the wrist map's center pulls a copy too
constexpr float AwayOutExtra=.03f;    // put away: the reach's buzz re-arms only past awayM plus this (no buzz on jitter)
constexpr float TieDeg=1;             // point to grab: both maps this close in angle: the one nearer the hand takes it

// The values above, as one struct, so the tests can try each one's other settings (0 is each one's Off).
struct Tuning {
    float size=SizeM;
    float awayM=AwayM;            // 0: never put away
    float lookDeg=LookDeg;        // 0: always shown
    bool faceOnRelease=FaceMe;
    bool aimBuzz=AimBuzz;
    float aimDeg=PointDeg;        // the wrist map's band; 0: point to grab Off, exactly on a map (or the hand on the wrist map)
    float aimMargin=PointMargin;  // the floating map's band, in its sides past each edge (with point to grab On)
    float bothGripsS=BothGripsS;  // 0: only Left Grip while Right Grip holds it
};
struct Input {
    double time=0;            // s
    uint64_t session=0;       // the game's local player (0: none)
    bool gameplay=false;      // it may show: the motion layer on, in the world, no wheel or VR Settings, the snapshot fresh
    bool handFree=false;      // the right hand holds or touches nothing (no bottle, holster or bow draw)
    bool leftFree=false;      // and the left
    bool nearGrab=false;      // a hand is at a belt bottle or a holster: a press there is theirs unless it is exactly on the map
    math::Vec head{}; math::Quat headQ{0,0,0,1}; bool headValid=false;
    math::Vec hand[2]{}; bool handTracked[2]{};   // grip poses, 0 right, 1 left
    math::Vec aim{}; math::Quat aimQ{0,0,0,1}; bool aimTracked=false;   // the right controller's pointing
    math::Vec aimLeft{}; math::Quat aimLeftQ{0,0,0,1}; bool aimLeftTracked=false;   // the left's (two-hand size only)
    float grip[2]{};
    float stickX=0,stickY=0;  // the right stick, -1..1 (+y forward)
    // the wrist map quad as last drawn (wristShown: drawn within the last few frames)
    bool wristShown=false; math::Vec wristP{}; math::Quat wristQ{0,0,0,1}; float wristSize=.24f;
};
// PutAwayNewGame: Save and Exit / a new game put the copy away. Pulled: a copy pulled off the wrist (and held); PutAway: let go at the wrist, gone.
enum class Event { None, Grabbed, Released, ResizeStart, ResizeEnd, PutAwayNewGame, Pulled, PutAway };
inline const char* EventName(Event e){
    switch(e){case Event::Grabbed:return "grabbed";case Event::Released:return "let go";
        case Event::Pulled:return "pulled from the wrist";case Event::PutAway:return "put away";
        case Event::ResizeStart:return "two-hand resize";case Event::ResizeEnd:return "resize done";
        case Event::PutAwayNewGame:return "put away (new game)";default:return "none";}
}
// Where a ray passes a map: point to grab's measure, and the log's.
struct MapAim {
    bool known=false;         // there is a map to aim at
    bool plane=false;         // the ray crosses its plane ahead of the hand
    float x=0,y=0;            // there, in map sizes from its center, + right and up as you see its face (within +-.5: on it)
    float miss=180;           // degrees from the ray to the map's nearest point (0: on it)
    float center=180;         // degrees from the ray to its center
    float right=0,up=0;       // its center from the ray in the controller's own frame, degrees (+: it is right of / above the ray)
    float distance=0;         // m, the hand to the map
    math::Vec point{};        // on it: the point; else its center
};
enum class Refused { None, Away, Busy, AtBottle, Hidden, Untracked, KeptBack };
inline const char* RefusedName(Refused r){
    switch(r){case Refused::Busy:return "the hand holds something";case Refused::AtBottle:return "at a bottle or holster: only exactly on the map";
        case Refused::Hidden:return "the map is hidden while you look away";case Refused::Untracked:return "the controller is not tracked";
        case Refused::KeptBack:return "kept back for two hands";default:return "aimed away";}
}
struct Output {
    bool want=false;          // capture the map for it this frame
    bool shown=false;         // draw it this frame (the picture permitting)
    bool held=false;          // the right grip holds it
    bool resizing=false;      // and the left grip resizes it
    bool own[2]{};            // grips it took: zero them, so no skill, potion or holster sees that press
    bool ownStick=false;      // the right stick is the map's: zero it (push / pull while held, until at rest)
    bool pointed=false;       // a copy out, not held: the right laser on the shown floating map: the stick zooms it
    int pointedMap=-1;        // 0 the floating copy (the wrist map is never pointed: it zooms by looking, as upstream)
    math::Vec p{}; math::Quat q{0,0,0,1}; float size=0;
    float lookDeg=180;        // its center from your view's center
    float reach=0,reachStart=0;   // held: the grabbed point along the laser now, and at the grab, m
    int pushes=0;             // held: stick pushes or pulls this hold
    Event event=Event::None;  // the first thing that happened this frame, for the log
    int pulse=-1;             // a hand to buzz (0 right, 1 left; -1 none)
    bool aimed=false;         // the right laser is on a map Right Grip would take (shown, the hand free, not held)
    bool aimEdge=false;       // Aim buzz: aimed now with a copy out, off it for AimRearmS before (a light buzz)
    bool atWrist=false;       // held within awayM of the wrist's map: a let go now puts it away
    bool awayEdge=false;      // came into that reach this frame (a light buzz)
    // The log: a fresh Right Grip in the world, where its ray passed each map, and why it was not taken.
    bool pressed=false; Refused refused=Refused::None; MapAim pressMap,pressWrist;
    int gaveBack=0;           // two-hand size: a grip held back went to the game this frame (1 right, 2 left), no second grip came
    int bothFirst=0;          // two-hand size started this frame: which came first (1 right, 2 left, 3 together)
    float bothGapMs=0,spread=0;   // how far apart they came; the hands' spread at the start, m
};

struct Pose {math::Vec p{};math::Quat q{0,0,0,1};};
inline math::Quat Unit(math::Quat q){
    const float n=std::sqrt(q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w);
    return n>1e-6f?math::Quat{q.x/n,q.y/n,q.z/n,q.w/n}:math::Quat{0,0,0,1};
}
// b, given in a's frame, in a's parent frame.
inline Pose Compose(Pose a,Pose b){return {a.p+math::Rotate(a.q,b.p),Unit(math::Multiply(a.q,b.q))};}
// world, in frame's own frame (Compose(frame, Relative(frame, world)) == world).
inline Pose Relative(Pose frame,Pose world){
    const auto inv=math::Conjugate(Unit(frame.q));
    return {math::Rotate(inv,world.p-frame.p),Unit(math::Multiply(inv,world.q))};
}
// Upright, its face (+Z) toward the eyes from p.
inline math::Quat FaceEyes(math::Vec p,math::Vec head){
    const auto d=math::Normalized(p-head);
    if(!(math::Dot(d,d)>.5f))return {0,0,0,1};
    return Unit(math::Multiply(math::Yaw(std::atan2(-d.x,-d.z)),panels::Pitch(std::asin(std::clamp(d.y,-1.f,1.f)))));
}
// Its turn for the log, degrees, as q = Yaw * Pitch * Roll: yaw + turned left; pitch of its back (-Z): -90 flat like a
// table (face up), +90 overhead facing down; roll + counterclockwise as you see its face. Flat: yaw from its top edge, roll 0.
struct Angles {float yaw=0,pitch=0,roll=0;};
inline Angles AnglesOf(math::Quat q){
    q=Unit(q);
    const auto f=math::Rotate(q,{0,0,-1}),u=math::Rotate(q,{0,1,0});
    Angles a;
    a.pitch=std::asin(std::clamp(f.y,-1.f,1.f));
    if(std::fabs(f.y)>.9995f){a.yaw=f.y<0?std::atan2(-u.x,-u.z):std::atan2(u.x,u.z);a.roll=0;}
    else{
        a.yaw=std::atan2(-f.x,-f.z);
        const auto r=math::Rotate(math::Conjugate(math::Multiply(math::Yaw(a.yaw),panels::Pitch(a.pitch))),u);
        a.roll=std::atan2(-r.x,r.y);
    }
    a.yaw/=Deg;a.pitch/=Deg;a.roll/=Deg;
    return a;
}
inline float AngleTo(math::Vec dir,math::Vec unit){return std::acos(std::clamp(math::Dot(dir,unit),-1.f,1.f))/Deg;}
// Degrees from unit `dir` to the short arc between units a and b, as seen from the ray's origin.
inline float ArcMiss(math::Vec dir,math::Vec a,math::Vec b){
    float best=std::min(AngleTo(dir,a),AngleTo(dir,b));
    const auto n=math::Cross(a,b);const float nn=math::Dot(n,n);
    if(nn>1e-10f){
        const auto c=math::Normalized(dir-n*(math::Dot(dir,n)/nn));
        if(math::Dot(c,c)>.5f&&math::Dot(math::Cross(a,c),n)>=0&&math::Dot(math::Cross(c,b),n)>=0)best=std::min(best,AngleTo(dir,c));
    }
    return best;
}
// The ray from `ray` along the controller's -Z against a square map, either face (a quad shows both; rolled or placed
// overhead, its back may face you). miss 0 is exactly the laser on it; else the angle to the nearest point of its edge,
// so it scales with the map: any size, its whole area and the same band around it.
inline MapAim AimAt(math::Vec ray,math::Quat aimQ,Pose at,float size){
    MapAim m;m.known=true;
    const auto dir=math::Rotate(aimQ,{0,0,-1});
    const auto inverse=math::Conjugate(at.q);
    const auto o=math::Rotate(inverse,ray-at.p),d=math::Rotate(inverse,dir);
    const float half=size*.5f;
    const auto toCenter=math::Normalized(at.p-ray);
    m.center=math::Dot(toCenter,toCenter)>.5f?AngleTo(dir,toCenter):0;
    const auto c=math::Rotate(math::Conjugate(aimQ),toCenter);
    m.right=std::atan2(c.x,-c.z)/Deg;m.up=std::atan2(c.y,-c.z)/Deg;
    const float nx=std::clamp(o.x,-half,half),ny=std::clamp(o.y,-half,half);
    m.distance=std::sqrt((o.x-nx)*(o.x-nx)+(o.y-ny)*(o.y-ny)+o.z*o.z);
    m.point=at.p;
    if(std::fabs(d.z)>1e-5f){
        const float t=-o.z/d.z;
        if(t>0){
            const float px=o.x+d.x*t,py=o.y+d.y*t;
            m.plane=true;m.x=size>0?px/size:0;m.y=size>0?py/size:0;
            if(size>0&&std::fabs(px)<=half&&std::fabs(py)<=half){m.miss=0;m.point=ray+dir*t;return m;}
        }
    }
    const auto corner=[&](float x,float y){return math::Normalized(at.p+math::Rotate(at.q,{x,y,0})-ray);};
    const math::Vec k[4]={corner(-half,-half),corner(half,-half),corner(half,half),corner(-half,half)};
    m.miss=180;
    for(int i=0;i<4;++i)m.miss=std::min(m.miss,ArcMiss(dir,k[i],k[(i+1)%4]));
    m.miss=std::max(m.miss,.01f);   // off it is never 0, however close (acos rounds a few hundredths of a degree to 0)
    return m;
}
// Which map a press takes, given which ones it is within the band of (a: the floating one, b: the wrist's): -1 none, 0 the
// floating one, 1 the wrist's. Both: the smaller miss; about the same (exactly on both, one behind the other), the one
// nearer the hand.
inline int Choose(bool a,bool b,const MapAim& map,const MapAim& wrist){
    if(!a&&!b)return -1;
    if(a!=b)return a?0:1;
    if(std::fabs(map.miss-wrist.miss)>TieDeg)return map.miss<wrist.miss?0:1;
    return map.distance<=wrist.distance?0:1;
}
inline int Closest(const MapAim& map,const MapAim& wrist,float tolerance){
    return Choose(map.known&&map.miss<=tolerance,wrist.known&&wrist.miss<=tolerance,map,wrist);
}
// The floating map's point-to-grab band: the ray on its square grown by `margin` of its side past each edge, so the band
// is the same part of the map at any size and from any distance (an angle band would be a fixed width in the world). A ray
// that never crosses its plane ahead (the hand at its side, pointing along it): the hand within that margin of the map,
// pointing toward it.
inline bool InBand(math::Vec ray,math::Quat aimQ,Pose at,float size,float margin){
    if(!(margin>0))return false;
    const auto grown=AimAt(ray,aimQ,at,size*(1+2*margin));
    if(grown.miss<=0)return true;
    if(grown.plane)return false;
    const auto m=AimAt(ray,aimQ,at,size);
    return m.distance<=margin*size&&m.center<90;
}
// Push / pull: the reach after `delta` m, never past MaxReach outward nor MinReach inward; a reach already
// outside that range is never jumped back (only moved toward it).
inline float Pushed(float reach,float delta){
    if(delta>0)return std::max(reach,std::min(reach+delta,MaxReach));
    if(delta<0)return std::min(reach,std::max(reach+delta,MinReach));
    return reach;
}
// The stick's push this frame, m: past StickDead, scaled to a full push at PushSpeed m/s.
inline float StickPush(float stickY,float dt){
    const float a=std::fabs(stickY);
    if(!(a>StickDead)||!(dt>0))return 0;
    return std::copysign((std::min(a,1.f)-StickDead)/(1-StickDead)*PushSpeed*std::min(dt,.1f),stickY);
}
// The angle between the view's center and a point, degrees (180: unknown).
inline float LookAngle(math::Vec head,math::Quat headQ,math::Vec at){
    const auto v=at-head;const float n=std::sqrt(math::Dot(v,v));
    if(!(n>1e-4f))return 180;
    const float c=std::clamp(math::Dot(math::Rotate(headQ,{0,0,-1}),v*(1/n)),-1.f,1.f);
    return std::acos(c)/Deg;
}
// The size after a two-hand pull: the start size times now / start spread, within MinSize..MaxSize.
inline float Resized(float startSize,float startSpread,float spread){
    if(!(startSpread>.02f)||!std::isfinite(spread))return std::clamp(startSize,MinSize,MaxSize);
    return std::clamp(startSize*spread/startSpread,MinSize,MaxSize);
}

// What the copy shows, for the log: the picture's kind (the full map's redirect active: fullmap::Active) against Map
// view's. Step says when a line is due: the shown picture's kind or the setting changed, and has held KindHoldS.
constexpr double KindHoldS=.25;
struct KindWatch {
    bool was=false;double since=-1;int told=-1;
    bool Full()const{return told>=0&&(told&1);}
    bool ViewFull()const{return told>=0&&(told&2);}
    bool Matches()const{return Full()==ViewFull();}
    bool Step(bool shown,bool pictureFull,bool viewFull,double now){
        if(!shown){since=-1;return false;}
        if(since<0||pictureFull!=was){was=pictureFull;since=now;return false;}
        if(now-since<KindHoldS)return false;
        const int state=(pictureFull?1:0)|(viewFull?2:0);
        if(state==told)return false;
        told=state;return true;
    }
};

class Engine {
    Pose at{};                // the copy's pose in the room (placed)
    Pose grip{};              // held: its pose in the controller's (aim) frame
    math::Vec hitDir{};       // held: the grabbed point's direction in the aim frame (unit)
    math::Vec centerFromHit{};// held: the center from the grabbed point, in the aim frame
    float reach=0,reachStart=0;int pushes=0;bool pushing=false;
    bool placed=false,owned=false,resizing=false,gripWas[2]{},looked=false;
    bool swallow[2]{};        // a grip the map held stays ours until it is let go (never a fresh press for the game)
    bool stickSwallow=false;  // the stick the map held stays ours until it is back at rest
    float scale=0;            // the two-hand size (0: the tuning's)
    float startSize=0,startSpread=0;
    bool aiming=false;double aimOffAt=-1e9;   // Aim buzz: the laser on it last frame; when it last left (or a let go)
    bool atWrist=false;       // held within awayM of the wrist's map last frame (its buzz once per coming in)
    double wait[2]={-1,-1};   // two-hand size: a grip held back from the game since this time, for the other (0 right, 1 left; -1 none)
    bool twoHand=false;       // two-hand size: both hands have it: centered on their middle (the offset it had), its turn kept
    math::Vec twoOffset{};math::Quat twoQ{0,0,0,1};
    uint64_t session=0;
    double last=-1;
    float Dist(const Input& in)const{const auto d=in.hand[0]-in.hand[1];return std::sqrt(math::Dot(d,d));}
    math::Vec Middle(const Input& in)const{return (in.hand[0]+in.hand[1])*.5f;}
    void LetGoAll(){owned=resizing=looked=pushing=twoHand=false;wait[0]=wait[1]=-1;}
public:
    float Size(const Tuning& t)const{return scale>0?scale:std::clamp(t.size,MinSize,MaxSize);}
    bool Placed()const{return placed;}
    Output Step(const Input& in,const Tuning& t){
        Output out;
        const bool backwards=last>=0&&in.time<last;
        // A gap in the steps (a load screen, a hitch): nothing held carries over; the place stays.
        if(last>=0&&(backwards||in.time-last>GapS)){LetGoAll();swallow[0]=swallow[1]=false;stickSwallow=false;gripWas[0]=in.grip[0]>Press;gripWas[1]=in.grip[1]>Press;}
        const double dt=last>=0&&!backwards?in.time-last:0;
        last=in.time;
        // A new game (Save and Exit, then a game): the copy is put away, back on the wrist, and nothing is kept: no place, no
        // two-hand size (the next pull is the shipped size). A hold lets go; a grip still squeezed stays swallowed until it
        // is released (never a fresh press for the game). A load screen inside the same game keeps the copy (the gap above).
        if(in.session&&in.session!=session){
            const bool had=session!=0&&placed;
            session=in.session;LetGoAll();
            placed=false;scale=0;atWrist=aiming=false;
            if(had)out.event=Event::PutAwayNewGame;
        }
        // A swallowed grip is let go only by its own release (the map letting go of a grip still squeezed, a wheel
        // opening or tracking lost, must not hand the game a fresh Right or Left Grip press).
        for(int h=0;h<2;++h)if(!(in.grip[h]>Press))swallow[h]=false;
        if(std::fabs(in.stickX)<=StickRest&&std::fabs(in.stickY)<=StickRest)stickSwallow=false;
        out.own[0]=swallow[0];out.own[1]=swallow[1];out.ownStick=stickSwallow;
        if(!in.headValid){wait[0]=wait[1]=-1;gripWas[0]=in.grip[0]>Press;gripWas[1]=in.grip[1]>Press;return out;}
        const bool wasOut=placed;   // a copy out at the frame's start: Aim buzz and the stick's point-to-zoom only then
        const float size=Size(t);
        // The right grip: a press that starts with the controller pointing at a map (either face) takes it; a press
        // that started anywhere else never does, even when it sweeps across it.
        const bool right=in.grip[0]>Press,left=in.grip[1]>Press;
        const bool rightEdge=right&&!gripWas[0],leftEdge=left&&!gripWas[1];
        const Pose aim{in.aim,Unit(in.aimQ)};
        const bool both=t.bothGripsS>0;
        // A grip held back for the other one: let go, or out of the world, it is over; past the window the game gets it.
        for(int h=0;h<2;++h)if(wait[h]>=0){
            if(!(in.grip[h]>Press)||!both||!in.gameplay)wait[h]=-1;
            else if(in.time-wait[h]>t.bothGripsS){wait[h]=-1;out.gaveBack=h+1;}
        }
        const bool look=looked||!(t.lookDeg>0);
        const bool mapFree=placed&&!owned&&in.gameplay&&in.handFree&&in.aimTracked&&look;
        const bool wristFree=!owned&&in.gameplay&&in.handFree&&in.aimTracked&&in.wristShown;
        const bool pointing=t.aimDeg>0;
        const float tolerance=pointing&&!in.nearGrab?t.aimDeg:0;   // the wrist map's band, degrees
        const float margin=pointing&&!in.nearGrab?t.aimMargin:0;    // the floating map's, in its sides (its current size)
        const Pose wristAt{in.wristP,Unit(in.wristQ)};
        // Where the laser is on the maps: tested for a fresh press, a Left Grip that may start the two-hand size, and each
        // frame with Right Grip up while a copy is out (Aim buzz, the stick's zoom). Nothing out: only on a press.
        const bool bothAsk=both&&leftEdge&&!right;
        const bool watch=wasOut&&!right;
        MapAim onMap,onWrist;
        if(mapFree&&(rightEdge||watch||bothAsk))onMap=AimAt(in.aim,aim.q,at,size);
        if(wristFree&&(rightEdge||watch||bothAsk))onWrist=AimAt(in.aim,aim.q,wristAt,in.wristSize);
        // Point to grab Off: exactly on the floating one, else exactly on the wrist's. On: the closer of the two, each within
        // its own band (the floating map's in its sides, so it follows a two-hand resize).
        // The band is capped at the wrist map's angle (tolerance), so a large copy never takes presses aimed only near it.
        const bool mapIn=onMap.known&&(onMap.miss<=0||(pointing&&onMap.miss<=tolerance&&InBand(in.aim,aim.q,at,size,margin)));
        const int pick=pointing?Choose(mapIn,onWrist.known&&onWrist.miss<=tolerance,onMap,onWrist):onMap.known&&onMap.miss<=0?0:onWrist.known&&onWrist.miss<=0?1:-1;
        bool aimedNow=pick==0,force=false;
        const bool wristNow=pick==1;
        math::Vec hitAt=aimedNow?onMap.point:onWrist.point;
        // Two-hand size: both grips within the window, in either order, with either hand pointing at it. A grip that may be
        // the first of the pair is held back from the game (only then: that press points at a map), so a resize never leaks
        // a fresh press; if no second grip comes the game gets it, the window late.
        bool leftJoined=false;
        if(both&&!owned&&in.gameplay&&in.handFree&&in.leftFree&&in.aimTracked&&in.handTracked[0]&&in.handTracked[1]&&(leftEdge||rightEdge)){
            const auto leftAim=AimAt(in.aimLeft,Unit(in.aimLeftQ),at,size);
            const bool leftOn=placed&&look&&in.aimLeftTracked&&(leftAim.miss<=0||(leftAim.miss<=tolerance&&InBand(in.aimLeft,Unit(in.aimLeftQ),at,size,margin)));
            const bool leftWaits=wait[1]>=0,rightWaits=wait[0]>=0;
            bool join=false;
            if(rightEdge&&pick>=0)join=leftWaits||leftEdge;   // the right takes it as any press; the left joins
            else if(mapFree&&((rightEdge&&leftWaits)||(leftEdge&&rightWaits)||(rightEdge&&leftEdge&&leftOn))){   // taken for the other hand's aim
                aimedNow=force=join=true;hitAt=at.p;
            }else if(rightEdge&&leftWaits){wait[1]=-1;out.gaveBack=2;}
            else if(!in.nearGrab&&leftEdge&&!right&&(leftOn||pick>=0))wait[1]=in.time;
            else if(!in.nearGrab&&rightEdge&&!left&&leftOn)wait[0]=in.time;
            if(join){
                out.bothFirst=leftWaits?2:rightWaits?1:3;
                out.bothGapMs=leftWaits?float((in.time-wait[1])*1000):rightWaits?float((in.time-wait[0])*1000):0;
                out.spread=Dist(in);
                leftJoined=leftWaits;wait[0]=wait[1]=-1;
            }
        }
        // The log: where the press went, measured even when a gate refused it (a press, so a few a minute).
        if(rightEdge&&in.gameplay){
            out.pressed=true;
            out.pressMap=onMap.known?onMap:placed?AimAt(in.aim,aim.q,at,size):MapAim{};
            out.pressWrist=onWrist.known?onWrist:in.wristShown?AimAt(in.aim,aim.q,wristAt,in.wristSize):MapAim{};
            out.refused=wait[0]>=0?Refused::KeptBack:!in.aimTracked?Refused::Untracked:!in.handFree?Refused::Busy:
                        in.nearGrab&&(out.pressMap.known||out.pressWrist.known)?Refused::AtBottle:placed&&!look&&!out.pressWrist.known?Refused::Hidden:Refused::Away;
        }
        // Pull from the wrist: a fresh Right Grip with the laser on the wrist's map, or the right hand on it (not on the
        // floating copy: that one is grabbed as it is) pulls out a copy there, its center and turn the wrist map's, and
        // holds it. A copy already out jumps to the wrist (one copy at a time). Point to grab On: the hand on it counts only
        // with no copy out, so a press beside the wrist aimed elsewhere never moves a copy here.
        if(rightEdge&&!aimedNow&&wristFree){
            const auto d=in.hand[0]-in.wristP;
            const bool touch=in.handTracked[0]&&math::Dot(d,d)<TouchM*TouchM&&(!pointing||(!placed&&!in.nearGrab));
            if(wristNow||touch){
                placed=true;at=wristAt;
                hitAt=wristNow?onWrist.point:in.wristP;aimedNow=true;out.event=Event::Pulled;
            }
        }
        const bool aimedAny=aimedNow||(pointing&&wristNow);
        if(aimedAny){
            if(!aiming&&in.time-aimOffAt>=AimRearmS&&!rightEdge)out.aimEdge=t.aimBuzz&&wasOut;
            aiming=true;
        }else{if(aiming)aimOffAt=in.time;aiming=false;}
        out.aimed=aimedAny;
        if((rightEdge||force)&&!owned){
            if(aimedNow){
                owned=true;pushing=false;pushes=0;
                grip=Relative(aim,at);
                const auto hitLocal=math::Rotate(math::Conjugate(aim.q),hitAt-in.aim);
                reach=reachStart=std::sqrt(math::Dot(hitLocal,hitLocal));
                hitDir=reach>1e-4f?hitLocal*(1/reach):math::Vec{0,0,-1};
                centerFromHit=grip.p-hitLocal;
                if(out.event==Event::None)out.event=Event::Grabbed;
                out.pulse=0;
            }
        }
        if(owned&&(!right||!in.gameplay||!in.aimTracked)){
            owned=resizing=pushing=twoHand=false;aimOffAt=in.time;   // no Aim buzz for the laser still on it just after a let go
            if(t.faceOnRelease)at.q=FaceEyes(at.p,in.head);
            if(out.event==Event::None)out.event=Event::Released;
            out.reach=reach;out.reachStart=reachStart;out.pushes=pushes;
            // Let go at the wrist (a real let go, not a wheel opening): put away.
            if(atWrist&&t.awayM>0&&!right&&in.gameplay){
                placed=false;atWrist=false;out.event=Event::PutAway;out.pulse=0;
                out.own[0]=swallow[0];out.own[1]=swallow[1];out.ownStick=stickSwallow;
                gripWas[0]=right;gripWas[1]=left;
                return out;
            }
        }
        if(owned){
            // Push / pull along the laser: the grabbed point keeps its direction in the hand's frame, its distance moves.
            stickSwallow=true;
            if(twoHand){pushing=false;}   // both hands size it: the stick does nothing (still the map's)
            else if(const float push=StickPush(in.stickY,float(dt));push!=0){
                if(!pushing){pushing=true;++pushes;}
                reach=Pushed(reach,push);
                grip.p=hitDir*reach+centerFromHit;
            }else if(std::fabs(in.stickY)<=StickDead)pushing=false;
            // Two hands: Left Grip squeezed while the right holds it.
            if((leftEdge||leftJoined)&&in.handTracked[0]&&in.handTracked[1]){
                resizing=true;startSize=Size(t);startSpread=Dist(in);out.pulse=1;out.spread=startSpread;
                if(out.event==Event::None||out.event==Event::Grabbed)out.event=Event::ResizeStart;
                if(both){const Pose hold=Compose(aim,grip);twoHand=true;twoOffset=hold.p-Middle(in);twoQ=hold.q;}
            }
            if(resizing&&(!left||!in.handTracked[0]||!in.handTracked[1])){
                resizing=false;if(out.event==Event::None)out.event=Event::ResizeEnd;
            }
            if(resizing)scale=Resized(startSize,startSpread,Dist(in));
            if(twoHand&&!resizing){   // the right hand alone again, from where it is: no jump
                twoHand=false;
                grip=Relative(aim,at);
                reach=std::sqrt(math::Dot(grip.p,grip.p));
                hitDir=reach>1e-4f?grip.p*(1/reach):math::Vec{0,0,-1};
                centerFromHit={};
            }
            if(twoHand){at.p=Middle(in)+twoOffset;at.q=twoQ;}
            else at=Compose(aim,grip);
            out.reach=reach;out.reachStart=reachStart;out.pushes=pushes;
        }else resizing=false;
        // Held within awayM of the wrist's map: a let go puts it away (a light buzz on coming in; out again only past
        // awayM plus AwayOutExtra). Both hands on it, the left hand at the wrist is no put away.
        if(owned&&!twoHand&&t.awayM>0&&in.wristShown){
            const auto d=at.p-in.wristP;const float r=atWrist?t.awayM+AwayOutExtra:t.awayM;
            const bool now=math::Dot(d,d)<r*r;
            out.awayEdge=now&&!atWrist&&out.event!=Event::Pulled;
            atWrist=now;
        }else atWrist=false;
        out.atWrist=atWrist;
        out.held=owned;out.resizing=resizing;
        swallow[0]=swallow[0]||owned;swallow[1]=swallow[1]||resizing;
        out.own[0]=swallow[0]||wait[0]>=0;out.own[1]=swallow[1]||wait[1]>=0;out.ownStick=stickSwallow;
        // The stick's zoom: a copy out, not held, Right Grip up, the right laser anywhere on the shown floating map at its
        // current size (Jeff 2026-11-07: "the floating map does it when pointing at any of the map, so if the map scales in
        // size, so does the selection area"). Exactly on it, not within point to grab's band: aimed near a copy in a fight,
        // the stick stays the run toggle and Show Items. Never the wrist map: it zooms as upstream, by looking at it.
        out.pointed=watch&&placed&&!owned&&onMap.known&&onMap.miss<=0;
        out.pointedMap=out.pointed?0:-1;
        if(!placed){gripWas[0]=right;gripWas[1]=left;return out;}   // nothing out
        out.p=at.p;out.q=at.q;out.size=Size(t);
        // Look to show: within lookDeg it shows, it hides past lookDeg + LookOutExtra; held, always.
        out.lookDeg=LookAngle(in.head,in.headQ,out.p);
        if(t.lookDeg>0)looked=out.lookDeg<(looked?t.lookDeg+LookOutExtra:t.lookDeg);else looked=true;
        out.shown=in.gameplay&&(looked||owned);
        out.want=in.gameplay&&(owned||!(t.lookDeg>0)||out.lookDeg<t.lookDeg+WarmExtra);
        gripWas[0]=right;gripWas[1]=left;
        return out;
    }
};

// ---- transport's two calls (steamvr/floating_map_frame.cpp). Poses in transport's interaction space.
// StepFrame: transport Input, once a frame after the aim poses, before the right stick's gestures, the map zoom and the hands'
// skills read the grips and the stick. gripRight/gripLeft (motionInput.grip) and stickRx/stickRy (the menu controls' right
// stick) are set to 0 in place while the map holds them. want: capture the map for the copy (ORed with MapWanted).
// pointed: the right stick's up / down zooms the map (ORed into the map zoom's look). wrist*: the wrist map quad as last
// drawn (its pose; when, GetTickCount64 ms, 0 never; its side, m).
struct InputFrame {
    double time; uint64_t nowMs, session;
    bool gameplay, handFree, leftFree, nearGrab;
    math::Vec head; math::Quat headQ; bool headValid;
    math::Vec hand[2]; bool handTracked[2]; math::Quat handQ;
    math::Vec aim; math::Quat aimQ; bool aimTracked;
    math::Vec aimLeft; math::Quat aimLeftQ; bool aimLeftTracked;
    math::Vec wristP; math::Quat wristQ; uint64_t wristMs; float wristSize;
    float& gripRight; float& gripLeft;
    int16_t& stickRx; int16_t& stickRy;
    bool& want; bool& pointed;
    void (*pulse)(int hand, float strength);
    void (*say)(const char*);
};
// DrawFrame: transport EndPair, once a headset frame, after the wrist panels (a full layer budget drops the copy, never a
// wrist panel). visible: the wrist panels' own gate and no wheel; picture: this frame's map image is copied (mapEye.copied).
// quad: one quad of the whole map picture at pose (p, q), side m, through transport's motionQuad.
struct LayerFrame {
    bool visible, picture;
    uint64_t nowMs;
    void* quadCtx;
    void (*quad)(void* ctx, math::Vec p, math::Quat q, float side);
    void (*say)(const char*);
};
void StepFrame(InputFrame& f);
void DrawFrame(const LayerFrame& f);
}
