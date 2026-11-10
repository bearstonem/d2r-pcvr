#pragma once
#include "frame_math.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace framevr::allyhealth {
inline constexpr unsigned Capacity=8;
inline constexpr int Width=512,RowHeight=80,Height=Capacity*RowHeight;
struct Entry {
    uint32_t id=0;
    bool mercenary=false;
    std::array<char,64> name{};
    float health=0,maxHealth=0;
    bool positionKnown=false,gridPosition=false;
    math::Vec world{}; // renderer world coordinates, or grid x/z if gridPosition
    bool SameImage(const Entry& b)const {return id==b.id&&mercenary==b.mercenary&&name==b.name&&health==b.health&&maxHealth==b.maxHealth;}
    bool Valid()const {
        return id&&name[0]&&name.back()==0&&std::isfinite(health)&&std::isfinite(maxHealth)&&
            health>=0&&maxHealth>0&&maxHealth<1000000&&health<=maxHealth*2;
    }
    float Fraction()const {return Valid()?std::clamp(health/maxHealth,0.f,1.f):0.f;}
};
struct List {
    uint64_t session=0,at=0;
    unsigned count=0;
    std::array<Entry,Capacity> entries{};
    bool SameImage(const List& b)const {
        if(session!=b.session||count!=b.count||count>Capacity)return false;
        for(unsigned i=0;i<count;++i)if(!entries[i].SameImage(b.entries[i]))return false;
        return true;
    }
    bool Current(uint64_t owner,uint64_t now)const {
        return owner&&session==owner&&at<=now&&now-at<=1000&&count<=Capacity;
    }
};
}
