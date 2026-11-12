#pragma once
#include <array>
#include <cmath>
#include <string_view>
#include <span>
#include "../steamvr/frame_math.h"

// Procedural skin pose, derived from this installed bow's bind pose. No
// captured game animation or model data is bundled. Only the verified rig is
// accepted; unknown bows retain their native animation and the draw guides.
namespace skel::bow {
using framevr::math::Vec;
using namespace framevr::math;
struct Mat { float m[16]{}; };
constexpr int Count=13, Cog=3, String=8, Arrow=11;
using Pose=std::array<Mat,Count>;
inline constexpr size_t MaxBones=64;
using ArrowMask=std::array<bool,MaxBones>;
// Installed bows have several reduced/LOD skeleton layouts. Arrow-only hiding
// uses verified names/ancestry, independently of the optional limb solver.
inline ArrowMask ArrowBones(std::span<const std::string_view> names,std::span<const int> parents) {
    ArrowMask mask{};
    if(names.size()!=parents.size()||names.empty()||names.size()>MaxBones)return mask;
    for(size_t i=0;i<names.size();++i) {
        if(parents[i]<-1||parents[i]>=int(i))return {};
        mask[i]=names[i].starts_with("arrow")||names[i]=="alt_arrow"||(parents[i]>=0&&mask[parents[i]]);
    }
    return mask;
}
inline void HideArrow(Mat& bone) {
    for(int row=0;row<3;++row)for(int col=0;col<3;++col)bone.m[row*4+col]*=.001f;
}
inline constexpr std::array<std::string_view,Count> Names={
    "root","translate","anim","cog","upperBend","lowerBend",
    "lowerString","upperString","bowString","joint2","joint1","arrow","arrow_helper"};
inline constexpr std::array<int,Count> Parents={-1,0,1,2,3,3,5,4,3,6,7,3,11};
inline float Length(Vec v) {return std::sqrt(Dot(v,v));}
inline Vec Position(const Mat& a) {return {a.m[12],a.m[13],a.m[14]};}
inline void Position(Mat& a,Vec p) {a.m[12]=p.x;a.m[13]=p.y;a.m[14]=p.z;}
inline Mat Identity() {Mat m;m.m[0]=m.m[5]=m.m[10]=m.m[15]=1;return m;}
inline Mat Multiply(const Mat& a,const Mat& b) {
    Mat r;
    for(int i=0;i<4;++i)for(int k=0;k<4;++k)for(int j=0;j<4;++j)r.m[i*4+j]+=a.m[i*4+k]*b.m[k*4+j];
    return r;
}
inline Vec Vector(const Mat& m,Vec v) {
    return {v.x*m.m[0]+v.y*m.m[4]+v.z*m.m[8],v.x*m.m[1]+v.y*m.m[5]+v.z*m.m[9],v.x*m.m[2]+v.y*m.m[6]+v.z*m.m[10]};
}
inline bool Affine(const Mat& a) {
    for(float x:a.m)if(!std::isfinite(x)||std::fabs(x)>10000)return false;
    return std::fabs(a.m[3])+std::fabs(a.m[7])+std::fabs(a.m[11])<.001f&&std::fabs(a.m[15]-1)<.001f;
}
inline bool Inverse(const Mat& a,Mat& r) {
    if(!Affine(a))return false;
    const float* m=a.m;
    const float c0=m[5]*m[10]-m[6]*m[9],c1=m[6]*m[8]-m[4]*m[10],c2=m[4]*m[9]-m[5]*m[8];
    const float det=m[0]*c0+m[1]*c1+m[2]*c2;
    if(std::fabs(det)<1e-6f)return false;
    r=Identity();
    r.m[0]=c0/det;r.m[1]=(m[2]*m[9]-m[1]*m[10])/det;r.m[2]=(m[1]*m[6]-m[2]*m[5])/det;
    r.m[4]=c1/det;r.m[5]=(m[0]*m[10]-m[2]*m[8])/det;r.m[6]=(m[2]*m[4]-m[0]*m[6])/det;
    r.m[8]=c2/det;r.m[9]=(m[1]*m[8]-m[0]*m[9])/det;r.m[10]=(m[0]*m[5]-m[1]*m[4])/det;
    Position(r,Vector(r,Position(a))*-1);return Affine(r);
}
inline Mat Rotation(Vec from,Vec to) {
    from=Normalized(from);to=Normalized(to);
    const Vec cross=Cross(from,to);const float s=Length(cross),c=std::clamp(Dot(from,to),-1.f,1.f);
    Mat r=Identity();
    if(s<1e-6f) {
        if(c>0)return r;
        const Vec k=Normalized(Cross(from,std::fabs(from.x)<.9f?Vec{1,0,0}:Vec{0,1,0}));
        const float v[]={k.x,k.y,k.z};
        for(int i=0;i<3;++i)for(int j=0;j<3;++j)r.m[4*i+j]=2*v[i]*v[j]-(i==j?1.f:0.f);
        return r;
    }
    const Vec k=cross*(1/s);const float t=1-c;
    const float v[]={k.x,k.y,k.z};
    for(int i=0;i<3;++i)for(int j=0;j<3;++j)r.m[4*i+j]=t*v[i]*v[j]+(i==j?c:0.f);
    r.m[1]+=k.z*s;r.m[2]-=k.y*s;r.m[4]-=k.z*s;r.m[6]+=k.x*s;r.m[8]+=k.y*s;r.m[9]-=k.x*s;
    return r;
}
inline Mat Turn(Mat a,const Mat& rotation,Vec position) {
    Position(a,{});a=Multiply(a,rotation);Position(a,position);return a;
}
// Find the limb tip at the intersection of a fixed limb radius and string
// radius. Choose the branch nearest the resting tip, so each half folds inward
// smoothly without stretching its string or crossing the other limb.
inline bool Tip(Vec hinge,Vec restTip,Vec restNock,Vec nock,Vec& result) {
    const float limb=Length(restTip-hinge),string=Length(restNock-restTip),d=Length(nock-hinge);
    if(limb<.01f||string<.01f||d<.01f||d>=limb+string||d<=std::fabs(limb-string))return false;
    const Vec along=(nock-hinge)*(1/d);
    const float a=(limb*limb-string*string+d*d)/(2*d);
    const Vec side=Normalized(restTip-hinge-along*Dot(restTip-hinge,along));
    if(Length(side)<.9f)return false;
    result=hinge+along*a+side*std::sqrt(std::max(0.f,limb*limb-a*a));return true;
}
struct Rig {
    Pose rest{}; // normalized into the cog frame
    Vec restNock{},back{};
    float span=0;
    bool valid=false;
    bool Learn(const Pose& bind) {
        *this={};Mat inverseCog;
        if(!Inverse(bind[Cog],inverseCog))return false;
        for(int i=0;i<Count;++i) {
            Mat inverse;
            if(!Inverse(bind[i],inverse))return false;
            rest[i]=Multiply(bind[i],inverseCog);
        }
        restNock=Position(rest[String]);
        const Vec upper=Position(rest[7]),lower=Position(rest[6]),up=Normalized(upper-lower);
        span=Length(upper-lower);
        // String lies behind the grip, perpendicular to the upper/lower axis.
        back=Normalized(restNock-up*Dot(restNock,up));
        if(span<.1f||span>50||Length(back)<.9f)return false;
        // Reject incompatible geometry before any live pose can be changed.
        for(int side=0;side<2;++side) {
            const int bend=side?5:4,tip=side?6:7;
            Vec p;
            if(!Tip(Position(rest[bend]),Position(rest[tip]),restNock,restNock+back*(span*.46f),p))return false;
        }
        valid=true;return true;
    }
    // The direction across the bow's plane (its limbs and its string lie in it), in the cog's frame:
    // across that plane a bow is its own mirror image. For left-handed play (hand_swap_pose.h).
    Vec AcrossPlane() const {return valid?Normalized(Cross(Normalized(Position(rest[7])-Position(rest[6])),back)):Vec{};}
    bool DrawAt(const Mat& cog,Vec nock,Vec direction,bool arrowVisible,Pose& out) const {
        if(!valid||!std::isfinite(Length(nock))||!std::isfinite(Length(direction))||Length(direction)<.01f||!Affine(cog))return false;
        Mat inverse;if(!Inverse(cog,inverse))return false;
        // Constrain an unreachable hand position toward rest instead of
        // stretching/exploding the rig. Ordinary draws solve at the exact hand.
        Vec tips[2];bool reachable=false;
        for(int attempt=0;attempt<16&&!reachable;++attempt) {
            reachable=Tip(Position(rest[4]),Position(rest[7]),restNock,nock,tips[0])&&
                      Tip(Position(rest[5]),Position(rest[6]),restNock,nock,tips[1]);
            if(!reachable)nock=restNock+(nock-restNock)*.75f;
        }
        if(!reachable)return false;
        Pose pose=rest;
        for(int side=0;side<2;++side) {
            const int bend=side?5:4,tip=side?6:7,joint=side?9:10;
            const Vec hinge=Position(rest[bend]),oldTip=Position(rest[tip]),newTip=tips[side];
            const Mat bendTurn=Rotation(oldTip-hinge,newTip-hinge);
            pose[bend]=Turn(rest[bend],bendTurn,hinge);
            const Mat stringTurn=Rotation(restNock-oldTip,nock-newTip);
            pose[tip]=Turn(rest[tip],stringTurn,newTip);
            pose[joint]=Turn(rest[joint],stringTurn,newTip+Vector(stringTurn,Position(rest[joint])-oldTip));
        }
        const Mat arrowTurn=Rotation(back*-1,direction);
        pose[String]=Turn(rest[String],arrowTurn,nock);
        for(int i=Arrow;i<Count;++i) {
            pose[i]=Turn(rest[i],arrowTurn,nock+Vector(arrowTurn,Position(rest[i])-restNock));
            if(!arrowVisible)for(int row=0;row<3;++row)for(int col=0;col<3;++col)pose[i].m[row*4+col]*=.001f;
        }
        // Publish only a complete finite pose; a bad rig leaves native data intact.
        for(int i=0;i<Count;++i) {pose[i]=Multiply(pose[i],cog);if(!Affine(pose[i]))return false;}
        out=pose;return true;
    }
    bool Draw(const Mat& cog,float amount,bool arrowVisible,Pose& out) const {
        if(!std::isfinite(amount))return false;
        return DrawAt(cog,restNock+back*(span*.46f*std::clamp(amount,0.f,1.f)),back*-1,arrowVisible,out);
    }
};
}
