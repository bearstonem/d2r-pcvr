#pragma once
#include "frame_math.h"
#include <algorithm>
namespace framevr::armocclusion {
// Conservative forearm capsule: does the eye-to-particle segment cross it?
inline bool Hidden(math::Vec eye,math::Vec p,math::Vec wrist,math::Vec elbow,float radius=.032f) {
    const auto u=p-eye,v=elbow-wrist,r=eye-wrist;
    const float a=math::Dot(u,u),e=math::Dot(v,v),b=math::Dot(u,v),c=math::Dot(u,r),f=math::Dot(v,r);
    if(a<1e-8f||e<.0025f||e>=1.f)return false;
    const float den=a*e-b*b;
    float s=den>1e-10f?std::clamp((b*f-c*e)/den,0.f,1.f):0.f;
    float t=(b*s+f)/e;
    if(t<0){t=0;s=std::clamp(-c/a,0.f,1.f);}
    else if(t>1){t=1;s=std::clamp((b-c)/a,0.f,1.f);}
    const auto delta=r+u*s-v*t;
    return s>0&&math::Dot(delta,delta)<radius*radius;
}
}
