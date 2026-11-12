#pragma once
#include <cmath>

// What a hand holds, from its controller to the item's attach bone, as pure maths: the hand turned like
// the controller (skeletons.cpp PlanWrist / ApplyWrist) and Weapon Adjust ([weapon_<kind>], PoseBody).
// skeletons.cpp calls these for both hands and both handednesses; hand_swap_mirror_test.cpp poses a rig
// with them to check that left-handed play is the mirror image of right-handed play. The arithmetic is
// skeletons.cpp's own, operation for operation (its V3, Q4 and Mat helpers), so moving it here changed no
// result. Model space: y up, the model faces +z, its left is +x; row-vector matrices, a bone's axes in
// rows 0..2 and its origin in 12..14.
namespace skel::held {
struct V {float x,y,z;};
struct Q {float x,y,z,w;};   // a quaternion, x y z w
inline V Add(V a,V b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
inline V Sub(V a,V b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
inline V Scale(V a,float k){return {a.x*k,a.y*k,a.z*k};}
inline float Dot(V a,V b){return a.x*b.x+a.y*b.y+a.z*b.z;}
inline V Cross(V a,V b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
inline float Len(V a){return sqrtf(Dot(a,a));}
inline V Norm(V a){const float l=Len(a);return l>1e-6f?V{a.x/l,a.y/l,a.z/l}:V{0,0,1};}
inline Q Mul(Q a,Q b) {
    return {a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,
            a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,
            a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w,
            a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z};
}
inline Q Axis(float ax,float ay,float az,float deg) {
    const float h=deg*0.00872664626f,s=sinf(h);
    return {ax*s,ay*s,az*s,cosf(h)};
}
inline V Rotate(Q q,V v) {   // q v q*
    const V u={q.x,q.y,q.z};
    const V t=Scale(Cross(u,v),2.0f);
    return Add(Add(v,Scale(t,q.w)),Cross(u,t));
}
// A direction in the head's turn-only frame (x right, y up, z back) -> model space (its right is -x, ahead
// +z), turned so "ahead of me" is where the camera looks: by the camera's yaw in model space (c, s).
inline V ToModel(const float* v,float c,float s) {
    const V m={-v[0],v[1],-v[2]};
    return {m.x*c+m.z*s,m.y,-m.x*s+m.z*c};
}
// a * b, row-vector 4x4: each row of a taken through b.
inline void Mul(const float* a,const float* b,float* r) {
    for(int i=0;i<16;++i)r[i]=0;
    for(int row=0;row<4;++row)
        for(int k=0;k<4;++k) {
            const float x=a[row*4+k];
            for(int col=0;col<4;++col)r[row*4+col]+=x*b[k*4+col];
        }
}

// Where the controller turns its hand. The bone's own axes are unknown (and mirrored on one side), so the
// hand is read off its knuckles: wrist -> middle knuckle is where it points, pinky -> index knuckle runs
// along the fist. OpenXR's grip pose: -Z through the fist from the little finger to the thumb, +X out of
// the right palm and into the left one (to the user's right in a natural hold, for both hands), so a
// vertical fist punching forward has -Z up, +X right, +Y back: the knuckles point along -Y.
//   rot      the controller's turn in the head's turn-only frame (x y z w; normalised here)
//   wristDeg [hands] pitch / yaw / roll, turned about the controller's own x, y, z; yaw and roll are
//            mirrored on the left (side 1), so one setting means the same bend on either hand
// point / along: model space, unit. False for a turn of no length.
inline bool Aim(const float rot[4],const float wristDeg[3],int side,float c,float s,V& point,V& along) {
    Q qc={rot[0],rot[1],rot[2],rot[3]};
    const float ql=sqrtf(qc.x*qc.x+qc.y*qc.y+qc.z*qc.z+qc.w*qc.w);
    if(!(ql>0.5f))return false;
    qc={qc.x/ql,qc.y/ql,qc.z/ql,qc.w/ql};
    const float mir=side==0?1.0f:-1.0f;
    qc=Mul(qc,Mul(Axis(1,0,0,wristDeg[0]),Mul(Axis(0,1,0,mir*wristDeg[1]),Axis(0,0,1,mir*wristDeg[2]))));
    const V py=Rotate(qc,{0,-1,0}),pz=Rotate(qc,{0,0,-1});
    const float a[3]={py.x,py.y,py.z},b[3]={pz.x,pz.y,pz.z};
    point=Norm(ToModel(a,c,s));
    along=Norm(ToModel(b,c,s));
    return true;
}
// The hand bone's rotation part from Aim: pLocal (wrist -> middle knuckle in the bone's frame) onto point,
// kLocal (pinky -> index) onto along made square to it; the third axis keeps the bone's handedness (det,
// +-1). Each row keeps its length (the bone's scale); the origin is left alone.
inline void Turn(float* hand,V pLocal,V kLocal,float det,V point,V along) {
    const V a1=pLocal,a2=Norm(Sub(kLocal,Scale(a1,Dot(kLocal,a1)))),a3=Cross(a1,a2);
    const V b1=point,b2=Norm(Sub(along,Scale(b1,Dot(along,b1)))),b3=Scale(Cross(b1,b2),det);
    const V A[3]={a1,a2,a3},B[3]={b1,b2,b3};
    float R[9]={};   // R = A^T B
    auto at=[](V v,int i){return i==0?v.x:i==1?v.y:v.z;};
    for(int i=0;i<3;++i)for(int j=0;j<3;++j)
        R[i*3+j]=at(A[0],i)*at(B[0],j)+at(A[1],i)*at(B[1],j)+at(A[2],i)*at(B[2],j);
    for(int i=0;i<3;++i) {
        const float len=sqrtf(hand[i*4]*hand[i*4]+hand[i*4+1]*hand[i*4+1]+hand[i*4+2]*hand[i*4+2]);
        for(int j=0;j<3;++j)hand[i*4+j]=R[i*3+j]*len;
    }
}

// [weapon_<kind>] (VR Settings > Weapon Adjust): the attach bone moved and turned in its own axes, a[0..2]
// x y z in cm and a[3..5] pitch yaw roll in degrees about its own x, y, z; upm: model units per metre.
// Applied to a left-handed bone while it is still the mirror image (hand_swap_pose.h MirrorGrip), it moves
// and turns it as the mirror image of the right-handed adjustment, with no signs to keep.
inline bool Adjusts(const float a[6]) {return a[0]!=0.0f||a[1]!=0.0f||a[2]!=0.0f||a[3]!=0.0f||a[4]!=0.0f||a[5]!=0.0f;}
inline void TurnMatrix(int axis,float deg,float* r) {   // row-vector rotation about one of the bone's own axes
    for(int i=0;i<16;++i)r[i]=0;
    r[0]=r[5]=r[10]=r[15]=1.0f;
    const float t=deg*0.0174532925f,c=cosf(t),s=sinf(t);
    const int i=(axis+1)%3,j=(axis+2)%3;
    r[i*4+i]=c;r[i*4+j]=s;r[j*4+i]=-s;r[j*4+j]=c;
}
// The turn part of an adjustment: pitch, then yaw, then roll, in the bone's own frame.
inline void AdjustTurn(const float a[6],float* local) {
    float x[16],y[16],z[16],xy[16];
    TurnMatrix(0,a[3],x);TurnMatrix(1,a[4],y);TurnMatrix(2,a[5],z);
    Mul(x,y,xy);Mul(xy,z,local);
}
// The adjustment on one bone, given its turn (AdjustTurn): moved along the bone's own axes as it was, then
// turned about its own origin.
inline void Adjust(float* bone,const float a[6],const float* local,float upm) {
    V shift{0,0,0};
    for(int r=0;r<3;++r) {
        const V axis=Norm(V{bone[r*4],bone[r*4+1],bone[r*4+2]});
        shift=Add(shift,Scale(axis,a[r]*0.01f*upm));
    }
    float turned[16];Mul(local,bone,turned);
    for(int i=0;i<16;++i)bone[i]=turned[i];
    bone[12]+=shift.x;bone[13]+=shift.y;bone[14]+=shift.z;
}
}
