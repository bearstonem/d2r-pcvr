#pragma once
#include <algorithm>
#include <cmath>

namespace framevr::math {
struct Vec {
    float x, y, z;
    bool operator==(const Vec&) const = default;
};
struct Quat {
    float x, y, z, w;
    bool operator==(const Quat&) const = default;
};
struct Fov { float left, right, down, up; };
// Keep every runtime-recommended ray while removing projection offsets that
// D2R's native light-bin shader does not account for. Eye poses stay unchanged.
inline Fov EnclosingSymmetricFov(Fov a,Fov b) {
    const float h=std::max({std::fabs(a.left),std::fabs(a.right),std::fabs(b.left),std::fabs(b.right)});
    const float v=std::max({std::fabs(a.down),std::fabs(a.up),std::fabs(b.down),std::fabs(b.up)});
    return {-h,h,-v,v};
}
inline Vec operator+(Vec a, Vec b) { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
inline Vec operator-(Vec a, Vec b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
inline Vec operator*(Vec a, float b) { return {a.x*b,a.y*b,a.z*b}; }
inline float Dot(Vec a,Vec b) {return a.x*b.x+a.y*b.y+a.z*b.z;}
inline Vec Cross(Vec a,Vec b) {return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
inline Vec Normalized(Vec a) {const float n=std::sqrt(Dot(a,a));return n>1e-6f?a*(1/n):Vec{};}
inline Vec ResetHeight(Vec origin, Vec head) {return {origin.x,head.y,origin.z};}
inline Quat Conjugate(Quat q) { return {-q.x,-q.y,-q.z,q.w}; }
inline Quat Multiply(Quat a, Quat b) {
    return {a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,
            a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,
            a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w,
            a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z};
}
inline Vec Rotate(Quat q, Vec v) {
    const Quat r=Multiply(Multiply(q,{v.x,v.y,v.z,0}),Conjugate(q));
    return {r.x,r.y,r.z};
}
inline Quat Yaw(float angle) { return {0,std::sin(angle*.5f),0,std::cos(angle*.5f)}; }
inline float Heading(Quat q) { const auto f=Rotate(q,{0,0,-1});return std::atan2(-f.x,-f.z); }
struct PanelHit {float u=0,v=0,distance=0;Vec point{};};
inline bool IntersectPanel(Vec ray,Vec direction,Vec position,Quat rotation,float width,float height,PanelHit& hit) {
    const auto inverse=Conjugate(rotation);
    const auto o=Rotate(inverse,ray-position),d=Rotate(inverse,direction);
    if(width<=0||height<=0||o.z<=0||d.z>=-1e-5f)return false;
    const float t=-o.z/d.z;
    const auto p=o+d*t;
    const float u=.5f+p.x/width,v=.5f-p.y/height;
    if(t<=0||u<0||u>1||v<0||v>1)return false;
    hit={u,v,t,ray+direction*t};return true;
}
inline Quat FromAxes(Vec r,Vec u,Vec b) {
    Quat q{};
    const float trace=r.x+u.y+b.z;
    if(trace>0) {const float s=std::sqrt(trace+1)*2;q={(u.z-b.y)/s,(b.x-r.z)/s,(r.y-u.x)/s,s/4};}
    else if(r.x>u.y&&r.x>b.z) {const float s=std::sqrt(1+r.x-u.y-b.z)*2;q={s/4,(u.x+r.y)/s,(b.x+r.z)/s,(u.z-b.y)/s};}
    else if(u.y>b.z) {const float s=std::sqrt(1+u.y-r.x-b.z)*2;q={(u.x+r.y)/s,s/4,(b.y+u.z)/s,(b.x-r.z)/s};}
    else {const float s=std::sqrt(1+b.z-r.x-u.y)*2;q={(b.x+r.z)/s,(b.y+u.z)/s,s/4,(r.y-u.x)/s};}
    return q;
}
inline Quat BeamOrientation(Vec direction,Vec towardHead) {
    const auto u=Normalized(direction);
    if(Dot(u,u)<.5f)return {0,0,0,1};
    auto b=Normalized(towardHead-u*Dot(towardHead,u));
    // A nearly collinear head/beam vector loses perpendicular precision in
    // the subtraction above. Rebuild both axes before making a quaternion.
    auto r=Normalized(Cross(u,b));
    if(Dot(r,r)<.5f)r=Normalized(Cross(u,std::fabs(u.x)<.8f?Vec{1,0,0}:Vec{0,0,1}));
    b=Normalized(Cross(r,u));
    const auto q=FromAxes(r,u,b);
    const float length=std::sqrt(q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w);
    return {q.x/length,q.y/length,q.z/length,q.w/length};
}
inline void View(Vec eye, Quat q, float* m) {
    const auto r=Rotate(q,{1,0,0}), u=Rotate(q,{0,1,0}), b=Rotate(q,{0,0,1});
    m[0]=r.x;m[1]=u.x;m[2]=b.x;m[3]=0;
    m[4]=r.y;m[5]=u.y;m[6]=b.y;m[7]=0;
    m[8]=r.z;m[9]=u.z;m[10]=b.z;m[11]=0;
    m[12]=-(eye.x*r.x+eye.y*r.y+eye.z*r.z);
    m[13]=-(eye.x*u.x+eye.y*u.y+eye.z*u.z);
    m[14]=-(eye.x*b.x+eye.y*b.y+eye.z*b.z);m[15]=1;
}
inline void Projection(float left, float right, float down, float up, float nearZ, float* m) {
    const float l=std::tan(left),r=std::tan(right),d=std::tan(down),u=std::tan(up);
    std::fill(m,m+16,0.f);
    m[0]=2/(r-l);m[5]=2/(u-d);
    m[8]=(r+l)/(r-l);m[9]=(u+d)/(u-d);
    m[11]=-1;m[14]=nearZ; // D2R infinite, reversed Z, row vectors.
}
}
