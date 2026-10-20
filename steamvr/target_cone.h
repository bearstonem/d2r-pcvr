#pragma once
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
constexpr float RangeCells=40.f;   // as far as a directed spell aims (spellaim::AimDistance)
constexpr float DistanceWeight=1.f;   // degrees per cell
constexpr float KeepMargin=3.f;       // score the current target is let off
constexpr float NearCells=12.f,MidCells=24.f; // colour bands: near / mid / far
constexpr int MaxTargets=64;
struct Target {uint32_t id;float x,y;bool clear;};

// Index of the chosen target, or -1. dir is the aim heading on the grid
// (need not be normalized); current is the previous target's id, 0 = none.
inline int Pick(const Target* targets,int count,float heroX,float heroY,float dirX,float dirY,uint32_t current) {
    const float length=std::hypot(dirX,dirY);
    if(!targets||count<=0||!std::isfinite(length)||length<1e-4f)return -1;
    dirX/=length;dirY/=length;
    int best=-1,kept=-1;float bestScore=1e9f,keptScore=1e9f;
    for(int i=0;i<count;++i) {
        const auto& t=targets[i];
        if(!t.clear)continue; // a wall between: a shot would not reach it
        const float dx=t.x-heroX,dy=t.y-heroY,d=std::hypot(dx,dy);
        if(d>RangeCells)continue;
        const float c=d<.5f?1.f:std::fmax(-1.f,std::fmin(1.f,(dx*dirX+dy*dirY)/d)); // on the hero: in any cone
        const float angle=std::acos(c)*180.f/3.14159265f;
        if(angle>HalfAngleDeg)continue;
        const float score=angle+DistanceWeight*d;
        if(t.id==current){kept=i;keptScore=score;}
        if(score<bestScore){bestScore=score;best=i;}
    }
    return kept>=0&&keptScore<=bestScore+KeepMargin?kept:best;
}
// 0 near, 1 mid, 2 far.
inline int Band(float distanceCells){return distanceCells<=NearCells?0:distanceCells<=MidCells?1:2;}
}
