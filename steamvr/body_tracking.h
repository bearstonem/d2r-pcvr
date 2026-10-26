#pragma once
#include "frame_math.h"

namespace framevr::body {
using math::Vec;
inline bool Finite(Vec v) { return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z); }
// Model coordinates: +X left, +Y up, +Z forward. Pitch is positive up;
// roll is positive right ear down, as published with the tracked hand sample.
inline Vec NeckTarget(Vec eye, Vec neckToEye, float yaw, float pitch, float roll) {
    const float s=std::sin(yaw),c=std::cos(yaw),sp=std::sin(pitch),cp=std::cos(pitch);
    const Vec forward{s*cp,sp,c*cp},right{-c,0,s},levelUp{-s*sp,cp,-c*sp};
    const Vec up=levelUp*std::cos(roll)+right*std::sin(roll);
    const Vec left=math::Cross(up,forward);
    return eye-(left*neckToEye.x+up*neckToEye.y+forward*neckToEye.z);
}
inline float UnitsPerMetre(float standingEye, float userHeight, float reach) {
    return std::clamp(standingEye/std::max(.8f,userHeight),2.f,8.f)*reach;
}
// Fit shoulder-to-wrist length to measured grip span at the height's world
// scale. Hands and equipment keep their authored size; targets stay 1:1.
// low: the shortest fit; a shapeshift form's long arms (the werebear's are 1.7x
// the druid's) pass 0.5.
// unitScale: the world's units per metre over the rig's own (a shapeshift form drawn
// scaled in a world kept at the person's size).
inline float ArmScale(float modelEye,float userEye,float span,float shoulderWidth,float armLength,float palm,float low=.65f,float unitScale=1.f) {
    if(!std::isfinite(modelEye+userEye+span+shoulderWidth+armLength+palm+unitScale)||
       modelEye<=0||userEye<.8f||span<.85f||span>2.6f||shoulderWidth<0||armLength<.01f||palm<0||unitScale<=0)return 1;
    return std::clamp(((span*UnitsPerMetre(modelEye,userEye,unitScale)-shoulderWidth)*.5f-palm)/armLength,std::clamp(low,.3f,.65f),1.6f);
}
// Row-vector affine matrix: resize an arm around its shoulder. The caller
// restores the hand subtree from its original local transforms afterwards.
inline void ScaleArmBone(float* m,Vec shoulder,float scale) {
    for(int row=0;row<3;++row)for(int col=0;col<3;++col)m[row*4+col]*=scale;
    m[12]=shoulder.x+(m[12]-shoulder.x)*scale;
    m[13]=shoulder.y+(m[13]-shoulder.y)*scale;
    m[14]=shoulder.z+(m[14]-shoulder.z)*scale;
}
// A two-segment leg, using the animated knee direction as its pole. Preserve
// segment lengths; clamp unreachable targets rather than stretching the skin.
inline bool Knee(Vec hip, float thigh, float shin, Vec& foot, Vec pole, Vec& knee) {
    if(!Finite(hip)||!Finite(foot)||!Finite(pole)||!std::isfinite(thigh+shin)||thigh<=.01f||shin<=.01f)return false;
    Vec delta=foot-hip;float distance=std::sqrt(math::Dot(delta,delta));
    const float low=std::fabs(thigh-shin)+.0001f,high=(thigh+shin)*.9999f;
    if(high<=low)return false;
    Vec axis=distance>1e-5f?delta*(1.f/distance):Vec{0,-1,0};
    distance=std::clamp(distance,low,high);foot=hip+axis*distance;
    pole=pole-axis*math::Dot(pole,axis);
    if(math::Dot(pole,pole)<1e-6f) {
        const Vec fallback=std::fabs(axis.z)<.9f?Vec{0,0,1}:Vec{1,0,0};
        pole=fallback-axis*math::Dot(fallback,axis);
    }
    const float along=(thigh*thigh-shin*shin+distance*distance)/(2*distance);
    knee=hip+axis*along+math::Normalized(pole)*std::sqrt(std::max(0.f,thigh*thigh-along*along));
    return Finite(knee);
}
}
