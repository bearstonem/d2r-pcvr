#pragma once
#include "aim_reach.h"
#include <cmath>
#include <cstdint>

namespace framevr::targetcone {
// Target lock: the hostile monster being aimed at. Inside a
// cone around the aim heading (on the grid), each monster scores its angle off
// the aim in degrees plus DistanceWeight per cell: the one being aimed at wins,
// and of two in the same line the nearer one (a mob close in front, never the
// one behind it). The current target keeps a small margin only, so turning the
// head or hand to another monster moves the lock at once.
constexpr float HalfAngleDeg=30.f;
constexpr float DistanceWeight=1.f;   // degrees per cell
constexpr float KeepMargin=3.f;       // score the current target is let off
constexpr float NearCells=12.f,MidCells=24.f; // color bands: near / mid / far
constexpr int MaxTargets=64;
// VR Settings > Aim > Aim with your view: the eyes catch a monster only
// within TunnelDeg of the head's heading and keep it while within KeepDeg; the
// hands pick only monsters within KeepDeg of it (Jeff's headset tests, 2026-10-22).
constexpr float TunnelDeg=17.f,KeepDeg=30.f;
struct Target {uint32_t id;float x,y;bool clear;};

// Keeping the nearest max of a stream: where a candidate at distance d goes in
// a list of n (distances), or -1. Below max it is appended (n); full, it
// replaces the farthest when nearer than it.
inline int NearestSlot(const float* distance,int n,int max,float d) {
    if(n<max)return n;
    int farthest=-1;
    for(int i=0;i<n;++i)if(farthest<0||distance[i]>distance[farthest])farthest=i;
    return farthest>=0&&d<distance[farthest]?farthest:-1;
}

// Index of the chosen target, or -1. dir is the aim heading on the grid
// (need not be normalized); current is the previous target's id, 0 = none.
// Only monsters within the game's reach count (aimreach): never one a skill
// could not be cast at.
inline int Pick(const Target* targets,int count,float heroX,float heroY,float dirX,float dirY,uint32_t current) {
    const float length=std::hypot(dirX,dirY);
    if(!targets||count<=0||!std::isfinite(length)||length<1e-4f)return -1;
    dirX/=length;dirY/=length;
    int best=-1,kept=-1;float bestScore=1e9f,keptScore=1e9f;
    for(int i=0;i<count;++i) {
        const auto& t=targets[i];
        if(!t.clear)continue; // a wall between: a shot would not reach it
        const float dx=t.x-heroX,dy=t.y-heroY,d=std::hypot(dx,dy);
        if(!aimreach::Within(dx,dy))continue;
        const float c=d<.5f?1.f:std::fmax(-1.f,std::fmin(1.f,(dx*dirX+dy*dirY)/d)); // on the hero: in any cone
        const float angle=std::acos(c)*180.f/3.14159265f;
        if(angle>HalfAngleDeg)continue;
        const float score=angle+DistanceWeight*d;
        if(t.id==current){kept=i;keptScore=score;}
        if(score<bestScore){bestScore=score;best=i;}
    }
    return kept>=0&&keptScore<=bestScore+KeepMargin?kept:best;
}
// Degrees between a heading and the direction to a monster (dx, dy from the
// hero); 0 for one on the hero, 180 with no heading.
inline float AngleOff(float dx,float dy,float dirX,float dirY) {
    const float d=std::hypot(dx,dy),l=std::hypot(dirX,dirY);
    if(!std::isfinite(l)||l<1e-4f)return 180.f;
    if(d<.5f)return 0.f;
    return std::acos(std::fmax(-1.f,std::fmin(1.f,(dx*dirX+dy*dirY)/(d*l))))*180.f/3.14159265f;
}
// The eyes with Aim follows your view: Pick, but a monster is caught only
// within catchDeg of the heading (the tunnel), and the one caught is kept while
// within keepDeg (the wider wedge), unless another is caught in the tunnel while
// the kept one is outside it. Both in the tunnel: as Pick, with its margin.
inline int PickTunnel(const Target* targets,int count,float heroX,float heroY,float dirX,float dirY,uint32_t current,
                      float catchDeg=TunnelDeg,float keepDeg=KeepDeg) {
    const float length=std::hypot(dirX,dirY);
    if(!targets||count<=0||!std::isfinite(length)||length<1e-4f)return -1;
    dirX/=length;dirY/=length;
    keepDeg=std::fmax(keepDeg,catchDeg);
    int best=-1,kept=-1;float bestScore=1e9f,keptScore=1e9f,keptAngle=1e9f;
    for(int i=0;i<count;++i) {
        const auto& t=targets[i];
        if(!t.clear)continue;
        const float dx=t.x-heroX,dy=t.y-heroY,d=std::hypot(dx,dy);
        if(!aimreach::Within(dx,dy))continue;
        const float c=d<.5f?1.f:std::fmax(-1.f,std::fmin(1.f,(dx*dirX+dy*dirY)/d));
        const float angle=std::acos(c)*180.f/3.14159265f;
        const float score=angle+DistanceWeight*d;
        if(t.id==current&&angle<=keepDeg){kept=i;keptScore=score;keptAngle=angle;}
        if(angle<=catchDeg&&score<bestScore){bestScore=score;best=i;}
    }
    if(kept<0)return best;
    if(keptAngle<=catchDeg)return keptScore<=bestScore+KeepMargin?kept:best;
    return best>=0?best:kept;
}
// 0 near, 1 mid, 2 far.
inline int Band(float distanceCells){return distanceCells<=NearCells?0:distanceCells<=MidCells?1:2;}
}
