#pragma once
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
}
