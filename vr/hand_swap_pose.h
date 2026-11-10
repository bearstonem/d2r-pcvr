#pragma once
#include "bow_pose.h"

// Left-handed play (steamvr/hand_role.h, #307): the main weapon drawn in the left hand, the off-hand
// weapon in the right. The game hangs each item on its own attach bone (R_weapon_attach,
// L_weapon_attach) and draws it from that bone's matrix as it is, so the bone is posed at the other
// wrist. Two facts shape how:
//   - the grip that is known good is the one the right-handed game shows, the attach bone relative
//     to its own wrist; the other hand's is its mirror image through the body's middle plane;
//   - a mirror image cannot be drawn: an item drawn from an attach matrix of the wrong handedness
//     (determinant sign) is inside out and culled (skeletons.cpp, bothAttach, 2026-10-06).
// So the bone is put where the mirror image has it and one of its own axes is turned over: the item
// is TURNED in the other hand, not mirrored (owner, 2026-11-09: "the asymmetric items are fine if
// they look rotated, not mirrored"). Row-vector matrices, a bone's axes in rows 0..2, as bow::Mat.
namespace skel::handswap {
// The handedness of a bone matrix: the determinant of its axes.
inline float Det(const bow::Mat& a) {
    const float* m=a.m;
    return m[0]*(m[5]*m[10]-m[6]*m[9])-m[1]*(m[4]*m[10]-m[6]*m[8])+m[2]*(m[4]*m[9]-m[5]*m[8]);
}
// One of the bone's own axes (0 x, 1 y, 2 z) turned over: the handedness changes, the origin stays.
inline void TurnOver(bow::Mat& a,int axis) {
    if(axis<0||axis>2)return;
    for(int c=0;c<3;++c)a.m[axis*4+c]=-a.m[axis*4+c];
}
// Gives m the handedness of `like` (the matrix the game itself animates that bone with) by turning
// `axis` over when they differ; true when it turned. Nothing to compare against (a zero determinant)
// leaves m alone.
inline bool MatchHandedness(bow::Mat& m,const bow::Mat& like,int axis) {
    const float a=Det(m),b=Det(like);
    if(!std::isfinite(a)||!std::isfinite(b)||a==0||b==0||(a<0)==(b<0))return false;
    TurnOver(m,axis);return true;
}
// The grip `attach` (an attach bone relative to its own wrist, bindWrist that wrist's bind matrix) as
// its mirror image has it relative to the other wrist (bindOther): the model is mirrored through its
// middle plane (x = 0, "its left is +x"), which carries one hand's bind pose onto the other's. The
// result has the opposite handedness: MatchHandedness after everything else has been applied in it
// (Weapon Adjust's offsets and turns are then mirrored with it, with no signs to keep).
inline bool MirrorGrip(const bow::Mat& attach,const bow::Mat& bindWrist,const bow::Mat& bindOther,bow::Mat& result) {
    bow::Mat inverse;
    if(!bow::Affine(attach)||!bow::Affine(bindWrist)||!bow::Inverse(bindOther,inverse))return false;
    bow::Mat mirrored=bow::Multiply(attach,bindWrist);
    for(int r=0;r<4;++r)mirrored.m[r*4]=-mirrored.m[r*4];   // x -> -x: each axis and the origin
    result=bow::Multiply(mirrored,inverse);return bow::Affine(result);
}
// Which of the attach bone's axes to turn over. A weapon runs along the bone's y (blade, shaft); held
// in a fist its flat faces the way the palm does and its edge the way the knuckles do. Turning the
// flat's axis over keeps the edge leading, so of x and z the one nearer the palm's normal is taken.
// attachBind: the attach bone in the bind pose's space (grip times the wrist's bind matrix).
inline int FlatAxis(const bow::Mat& attachBind,bow::Vec palmNormal) {
    const bow::Vec x=bow::Normalized({attachBind.m[0],attachBind.m[1],attachBind.m[2]}),z=bow::Normalized({attachBind.m[8],attachBind.m[9],attachBind.m[10]});
    const float n=bow::Length(palmNormal);
    if(!(n>1e-6f)||!std::isfinite(n))return 0;
    const float ax=std::fabs(bow::Dot(x,palmNormal)),az=std::fabs(bow::Dot(z,palmNormal));
    return std::isfinite(ax+az)&&az>ax?2:0;
}
// [hands] left_handed_turn_axis: 0 = FlatAxis (the default), 1..3 = the bone's x, y or z, for comparing.
inline int TurnAxis(int setting,int flatAxis) {return setting>=1&&setting<=3?setting-1:flatAxis==2?2:0;}
}
