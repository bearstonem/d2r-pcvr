#pragma once
#include <cstdint>
#include "bow_pose.h"

namespace skel::shield {
// A tracked shield must not stop at the authored arm's reach. Extend the arm
// only after it runs out of length; a small IK margin keeps the wrist on the
// requested point. Recompute from this frame's unextended pose, never accumulate.
// The cap contains extreme tracking/calibration errors rather than amplifying them.
inline float ReachScale(bow::Vec shoulder,bow::Vec wrist,float upper,float lower) {
    const float distance=bow::Length(wrist-shoulder),length=upper+lower;
    if(!std::isfinite(distance)||!std::isfinite(length)||upper<=.01f||lower<=.01f)return 1;
    return std::clamp(distance/(length*.999f),1.f,2.f);
}
// An authored shield-to-wrist relationship, independent of attack/walk
// animation and the main weapon's adjustment. Keep its handedness and size.
inline bool Follow(const bow::Mat& bindWrist,const bow::Mat& bindShield,
                   const bow::Mat& trackedWrist,bow::Mat& result) {
    bow::Mat inverse;
    if(!bow::Inverse(bindWrist,inverse)||!bow::Affine(bindShield)||!bow::Affine(trackedWrist))return false;
    const auto local=bow::Multiply(bindShield,inverse);
    const float scale=bow::Length({local.m[0],local.m[1],local.m[2]});
    if(scale<.3f||scale>3.f)return false;
    result=bow::Multiply(local,trackedWrist);return bow::Affine(result);
}
// VR Settings > Shield. 2026-10-21: the shield is placed in the ARM's own
// frame, built from joint positions only - never from a bone's axes, which each
// class's bind pose lays differently (on the Paladin "Up" moved the shield
// sideways off the arm; on the Assassin it moved it up):
//   along - elbow to wrist (Grip offset slides the shield back along it);
//   up    - toward the thumb: the pinky knuckle to the index knuckle, square to along;
//   out   - the shield's side of the arm (the back of the hand, or the palm when
//           the bind pose hangs it there), square to both.
// A wrist bend turns the knuckle line about itself, so it leaves "up" alone;
// only the forearm's twist turns the frame.
constexpr float kGripMinCm=-10,kGripMaxCm=40,kSideMaxCm=30,kFollowMaxPct=100,kRollMaxDeg=180;
inline bool Finite(bow::Vec v) {return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
inline float Clamp(float cm,float low,float high) {return std::isfinite(cm)?std::clamp(cm,low,high):0.f;}
struct Frame {bow::Vec along{},out{},up{};bool ok=false;};
// Which way out turns from Cross(up, along): learned once from the bind pose.
// The back of the hand from its knuckles (Fist's left palm, reversed); side -1
// puts the shield on the palm's side. 0 when the hand is degenerate.
inline float Handed(bow::Vec elbow,bow::Vec wrist,bow::Vec index,bow::Vec pinky,bow::Vec middle,int side) {
    using namespace bow;
    const Vec along=Normalized(wrist-elbow),knuckles=index-pinky;
    const Vec up=Normalized(knuckles-along*Dot(knuckles,along));
    const Vec back=Normalized(Cross(knuckles,middle-wrist));
    const float d=Dot(Cross(up,along),back);
    if(Length(along)<.5f||Length(up)<.5f||!std::isfinite(d)||std::fabs(d)<.1f)return 0;
    return (d>0?1.f:-1.f)*(side<0?-1.f:1.f);
}
// This frame's arm, as above; handed from Handed (the same for the bind pose and
// every frame after it, so the frame can never mirror or flip over).
inline Frame ArmFrame(bow::Vec elbow,bow::Vec wrist,bow::Vec index,bow::Vec pinky,float handed) {
    using namespace bow;
    Frame f;
    if(handed==0||!Finite(elbow)||!Finite(wrist)||!Finite(index)||!Finite(pinky))return f;
    const Vec arm=wrist-elbow,knuckles=index-pinky;
    if(!(Length(arm)>1e-4f))return f;
    f.along=Normalized(arm);
    const Vec up=knuckles-f.along*Dot(knuckles,f.along);
    if(!(Length(up)>.2f*Length(knuckles)))return f;   // knuckles along the forearm: no roll to read
    f.up=Normalized(up);f.out=Cross(f.up,f.along)*handed;
    f.ok=true;return f;
}
// A frame carried rigidly by a bone from its bind pose to now, squared up again.
inline Frame Carry(const Frame& bind,const bow::Mat& bindBone,const bow::Mat& boneNow) {
    using namespace bow;
    Frame f;Mat inverse;
    if(!bind.ok||!Inverse(bindBone,inverse)||!Affine(boneNow))return f;
    auto carry=[&](Vec v){return Vector(boneNow,Vector(inverse,v));};
    const Vec up=carry(bind.up);
    f.along=Normalized(carry(bind.along));
    f.out=carry(bind.out);f.out=Normalized(f.out-f.along*Dot(f.out,f.along));
    f.up=Cross(f.along,f.out);if(Dot(f.up,up)<0)f.up=f.up*-1;
    f.ok=Length(f.along)>.5f&&Length(f.out)>.5f;
    return f;
}
// v turned about the unit axis by angle radians (right hand rule).
inline bow::Vec TurnAbout(bow::Vec v,bow::Vec axis,float angle) {
    using namespace bow;
    const float c=std::cos(angle),s=std::sin(angle);
    return v*c+Cross(axis,v)*s+axis*(Dot(axis,v)*(1-c));
}
// The turn taking frame a to frame b (both the same handedness), as an axis and angle.
inline bool TurnBetween(const Frame& a,const Frame& b,bow::Vec& axis,float& angle) {
    using namespace bow;
    if(!a.ok||!b.ok)return false;
    float R[3][3];
    const Vec A[3]={a.along,a.out,a.up},B[3]={b.along,b.out,b.up};
    auto at=[](Vec v,int i){return i==0?v.x:i==1?v.y:v.z;};
    for(int r=0;r<3;++r)for(int c=0;c<3;++c)R[r][c]=at(B[0],r)*at(A[0],c)+at(B[1],r)*at(A[1],c)+at(B[2],r)*at(A[2],c);
    float w=std::sqrt(std::max(0.f,1+R[0][0]+R[1][1]+R[2][2]))*.5f;
    Vec q{std::sqrt(std::max(0.f,1+R[0][0]-R[1][1]-R[2][2]))*.5f,
          std::sqrt(std::max(0.f,1-R[0][0]+R[1][1]-R[2][2]))*.5f,
          std::sqrt(std::max(0.f,1-R[0][0]-R[1][1]+R[2][2]))*.5f};
    q.x=std::copysign(q.x,R[2][1]-R[1][2]);q.y=std::copysign(q.y,R[0][2]-R[2][0]);q.z=std::copysign(q.z,R[1][0]-R[0][1]);
    const float s=Length(q);
    if(!std::isfinite(s)||!std::isfinite(w))return false;
    if(s<1e-6f){axis={1,0,0};angle=0;return true;}
    axis=q*(1/s);angle=2*std::atan2(s,w);return true;
}
// VR Settings > Shield > Shield wrist turn ([gameplay] shield_follow_pct): t=0 strapped to the forearm (the
// wrist's bend ignored), 1 the hand's own turn - a slerp, so every share in
// between turns smoothly and the same way for every class and shield.
inline Frame Blend(const Frame& strapped,const Frame& wrist,float t) {
    using namespace bow;
    if(!strapped.ok)return wrist;
    if(!wrist.ok)return strapped;
    t=std::isfinite(t)?std::clamp(t,0.f,1.f):1.f;
    Vec axis;float angle;
    if(!TurnBetween(strapped,wrist,axis,angle))return wrist;
    Frame f;f.ok=true;
    f.along=Normalized(TurnAbout(strapped.along,axis,angle*t));
    f.out=Normalized(TurnAbout(strapped.out,axis,angle*t));
    f.up=Normalized(TurnAbout(strapped.up,axis,angle*t));
    return f;
}
// The resting roll about the forearm: positive turns the shield's face from out
// toward the little finger (-up). Held palm down, the back of the left hand faces
// the sky and its little finger points away from the body, so +90 turns a shield
// lying on the forearm like a tray to face outward.
inline Frame Roll(Frame f,float deg) {
    if(!f.ok||!std::isfinite(deg))return f;
    const float t=std::clamp(deg,-kRollMaxDeg,kRollMaxDeg)*.0174532925f,c=std::cos(t),s=std::sin(t);
    const bow::Vec out=f.out*c-f.up*s,up=f.up*c+f.out*s;
    f.out=out;f.up=up;return f;
}
// VR Settings > Shield > Shield roll ([gameplay] shield_roll_deg): -180..+180 in 5-degree steps,
// 0 (the attach's own facing) when it is not a number.
constexpr float kRollStepDeg=5;
inline float RollSetting(float deg) {
    if(!std::isfinite(deg))return 0;
    return std::round(std::clamp(deg,-kRollMaxDeg,kRollMaxDeg)/kRollStepDeg)*kRollStepDeg+0.f;
}
// VR Settings > Shield > Shield wrist turn ([gameplay] shield_follow_pct):
// 0 strapped to the forearm .. 100 turns with the wrist, in 10% steps; 50 when it
// is not a number.
constexpr float kFollowDefaultPct=50,kFollowStepPct=10;
inline float FollowSetting(float pct) {
    if(!std::isfinite(pct))return kFollowDefaultPct;
    return std::round(std::clamp(pct,0.f,kFollowMaxPct)/kFollowStepPct)*kFollowStepPct+0.f;
}
// The frame as a bone matrix (row vectors): its axes scaled, at origin.
inline bow::Mat Matrix(const Frame& f,bow::Vec origin,float scale) {
    bow::Mat m=bow::Identity();
    const bow::Vec r[3]={f.along*scale,f.out*scale,f.up*scale};
    for(int i=0;i<3;++i){m.m[i*4]=r[i].x;m.m[i*4+1]=r[i].y;m.m[i*4+2]=r[i].z;}
    bow::Position(m,origin);return m;
}
// The shield's attach bone, kept where the bind pose holds it in the arm's frame,
// now in this frame's: the bind frame at the bind wrist (and the hand bone's
// scale), the frame now at this wrist. With the wrist frame and no roll this is
// exactly Follow.
inline bool Place(const bow::Mat& bindAttach,const Frame& bind,bow::Vec bindWrist,float bindScale,
                  const Frame& now,bow::Vec wrist,float scale,bow::Mat& result) {
    bow::Mat inverse;
    if(!bind.ok||!now.ok||!(bindScale>1e-6f)||!(scale>1e-6f)||!std::isfinite(bindScale)||!std::isfinite(scale))return false;
    if(!bow::Inverse(Matrix(bind,bindWrist,bindScale),inverse)||!bow::Affine(bindAttach)||!Finite(wrist))return false;
    const auto local=bow::Multiply(bindAttach,inverse);
    const float size=bow::Length({local.m[0],local.m[1],local.m[2]});
    if(size<.3f||size>3.f)return false;
    result=bow::Multiply(local,Matrix(now,wrist,scale));return bow::Affine(result);
}
// Grip toward the elbow (+) or the hand (-), Out and Up, cm, clamped to the settings' ranges;
// anything not finite is 0.
inline bow::Vec Offset(bow::Vec along,bow::Vec out,bow::Vec up,float gripCm,float outCm,float upCm,float unitsPerMetre) {
    if(!std::isfinite(unitsPerMetre)||unitsPerMetre<=0)return {};
    return (along*-Clamp(gripCm,kGripMinCm,kGripMaxCm)+out*Clamp(outCm,-kSideMaxCm,kSideMaxCm)+
            up*Clamp(upCm,-kSideMaxCm,kSideMaxCm))*(.01f*unitsPerMetre);
}
// For the shield trace: the turn between two poses' axes, degrees (scale and
// mirroring ignored), and a pose's heading, pitch and roll, degrees.
inline float TurnDeg(const bow::Mat& a,const bow::Mat& b) {
    float trace=0;
    for(int r=0;r<3;++r)trace+=bow::Dot(bow::Normalized({a.m[r*4],a.m[r*4+1],a.m[r*4+2]}),bow::Normalized({b.m[r*4],b.m[r*4+1],b.m[r*4+2]}));
    return std::acos(std::clamp((trace-1)*.5f,-1.f,1.f))*57.29578f;
}
// The trace's jump check starts over (no JUMP, turn 0) on its first frame and
// after a gap: no trace frame for over kTraceGapMs (no shield or arm drawn, a
// level loading), or the shield's bone or skeleton changed since the last frame.
constexpr unsigned long long kTraceGapMs=500;
inline bool TraceFresh(bool haveFrame,unsigned long long lastMs,unsigned long long nowMs,bool sameShield) {
    return !haveFrame||!sameShield||nowMs<lastMs||nowMs-lastMs>kTraceGapMs;
}
inline bow::Vec Angles(const bow::Mat& m) {
    using namespace bow;
    const Vec x=Normalized({m.m[0],m.m[1],m.m[2]}),y=Normalized({m.m[4],m.m[5],m.m[6]}),z=Normalized({m.m[8],m.m[9],m.m[10]});
    return Vec{std::atan2(z.x,z.z),std::asin(std::clamp(-z.y,-1.f,1.f)),std::atan2(x.y,y.y)}*57.29578f;
}
}
