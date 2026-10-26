#pragma once
#include <algorithm>
#include <cmath>
#include "bow_pose.h"

// The held shield, steadied. Measured in the headset (2026-10-21): the shield
// shook about as much as the controller (~1-2 deg/s and ~1.6-1.9 cm/s of
// back-and-forth while held still, the same at every Shield follows). So the
// placed shield ignores changes smaller than a dead zone, in turn and in
// position. Soft: past the dead zone only the excess passes, so the shield
// never jumps at its edge, and any real move goes through in the same frame
// (a smoothing filter tried first trailed behind when walking side to side,
// then snapped back). Rotation and position separately; each row's scale kept.
namespace skel::steady {
struct Quat {float w=1,x=0,y=0,z=0;};
inline float Dot(Quat a,Quat b){return a.w*b.w+a.x*b.x+a.y*b.y+a.z*b.z;}
inline Quat Normalized(Quat q){const float l=std::sqrt(Dot(q,q));return l>1e-9f?Quat{q.w/l,q.x/l,q.y/l,q.z/l}:Quat{};}
// Rows of a pure rotation (row-vector convention, as bow::Mat keeps them).
inline Quat FromRows(const float r[3][3]) {
    const float t=r[0][0]+r[1][1]+r[2][2];Quat q;
    if(t>0){const float s=std::sqrt(t+1)*2;q={.25f*s,(r[1][2]-r[2][1])/s,(r[2][0]-r[0][2])/s,(r[0][1]-r[1][0])/s};}
    else if(r[0][0]>r[1][1]&&r[0][0]>r[2][2]){const float s=std::sqrt(1+r[0][0]-r[1][1]-r[2][2])*2;q={(r[1][2]-r[2][1])/s,.25f*s,(r[1][0]+r[0][1])/s,(r[2][0]+r[0][2])/s};}
    else if(r[1][1]>r[2][2]){const float s=std::sqrt(1+r[1][1]-r[0][0]-r[2][2])*2;q={(r[2][0]-r[0][2])/s,(r[1][0]+r[0][1])/s,.25f*s,(r[2][1]+r[1][2])/s};}
    else{const float s=std::sqrt(1+r[2][2]-r[0][0]-r[1][1])*2;q={(r[0][1]-r[1][0])/s,(r[2][0]+r[0][2])/s,(r[2][1]+r[1][2])/s,.25f*s};}
    return Normalized(q);
}
inline void ToRows(Quat q,float r[3][3]) {
    const float w=q.w,x=q.x,y=q.y,z=q.z;
    r[0][0]=1-2*(y*y+z*z);r[0][1]=2*(x*y+w*z);r[0][2]=2*(x*z-w*y);
    r[1][0]=2*(x*y-w*z);r[1][1]=1-2*(x*x+z*z);r[1][2]=2*(y*z+w*x);
    r[2][0]=2*(x*z+w*y);r[2][1]=2*(y*z-w*x);r[2][2]=1-2*(x*x+y*y);
}
inline float AngleDeg(Quat a,Quat b){return 2*std::acos(std::clamp(std::fabs(Dot(a,b)),0.f,1.f))*57.2958f;}
inline Quat Slerp(Quat a,Quat b,float t) {
    if(Dot(a,b)<0)b={-b.w,-b.x,-b.y,-b.z};
    const float d=std::clamp(Dot(a,b),-1.f,1.f);
    if(d>.9995f)return Normalized({a.w+(b.w-a.w)*t,a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t});
    const float th=std::acos(d),s=std::sin(th),wa=std::sin((1-t)*th)/s,wb=std::sin(t*th)/s;
    return {a.w*wa+b.w*wb,a.x*wa+b.x*wb,a.y*wa+b.y*wb,a.z*wa+b.z*wb};
}
struct Settings {
    float angleDeadDeg=0;   // turn dead zone, degrees
    float moveDeadCm=0;     // position dead zone, cm
};
// Tuned in the headset (2026-10-21/22, Paladin, then a Barbarian with a shield):
// held still, the turn shake fell by half or more and the position jitter went
// away, and the shield stays on the arm while walking side to side.
inline constexpr Settings Shield{1.f,.15f};
struct Filter {
    bool ok=false;Quat q;bow::Vec p{};
    void Reset(){ok=false;}
    // m: the shield's model-space matrix, steadied in place. unitsPerCm: model
    // units in a cm. dt: seconds since the last call; a gap (or a first call) restarts.
    void Apply(bow::Mat& m,float dt,float unitsPerCm,const Settings& s) {
        float scale[3],r[3][3];
        for(int i=0;i<3;++i) {
            scale[i]=std::sqrt(m.m[i*4]*m.m[i*4]+m.m[i*4+1]*m.m[i*4+1]+m.m[i*4+2]*m.m[i*4+2]);
            if(!(scale[i]>1e-6f)||!std::isfinite(scale[i])){Reset();return;}
            for(int c=0;c<3;++c)r[i][c]=m.m[i*4+c]/scale[i];
        }
        // A mirrored bone (det < 0): steady its rotation and keep the mirror in the scale.
        const float det=r[0][0]*(r[1][1]*r[2][2]-r[1][2]*r[2][1])-r[0][1]*(r[1][0]*r[2][2]-r[1][2]*r[2][0])+r[0][2]*(r[1][0]*r[2][1]-r[1][1]*r[2][0]);
        if(det<0){scale[2]=-scale[2];for(int c=0;c<3;++c)r[2][c]=-r[2][c];}
        const Quat qIn=FromRows(r);const bow::Vec pIn{m.m[12],m.m[13],m.m[14]};
        if(!ok||!(dt>0)||dt>.25f||!(unitsPerCm>0)){q=qIn;p=pIn;ok=true;return;}
        const float turn=AngleDeg(q,qIn);
        if(turn>s.angleDeadDeg)q=Normalized(Slerp(q,qIn,(turn-s.angleDeadDeg)/turn));
        const bow::Vec d=pIn-p;const float dist=bow::Length(d)/unitsPerCm;   // cm
        if(dist>s.moveDeadCm)p=p+d*((dist-s.moveDeadCm)/dist);
        ToRows(q,r);
        for(int i=0;i<3;++i)for(int c=0;c<3;++c)m.m[i*4+c]=r[i][c]*scale[i];
        m.m[12]=p.x;m.m[13]=p.y;m.m[14]=p.z;
    }
};
}
