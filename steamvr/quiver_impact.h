#pragma once
#include "bow_flight.h"
#include <span>

namespace framevr::bowflight {
// Only client effects emitted by this physical arrow's native ClientHit call.
// Monster missiles and ordinary native casts must never acquire our placement.
inline bool QuiverImpactMissile(int source,int effect) {
    switch(source) {
    case 27:return effect==204; // Magic Arrow -> teeth explosion
    case 12:return effect==29;
    case 28:case 40:return effect==30;
    case 41:return effect==42||effect==265;
    case 87:return effect==88; // central freeze; native ejecta keep their path
    default:return false; // persistent ground fire keeps native placement
    }
}
// Authored emitter offsets in the owned HD definitions (no assets shipped).
inline Vec QuiverImpactOffset(int missile) {
    switch(missile) {
    case 204:return {0,4.75f,1};
    case 29:case 265:return {0,4.75f,0};
    case 30:return {0,5,1.5f};
    case 42:return {0,4,0};
    case 88:return {0,5.25f,0};
    default:return {};
    }
}
inline Vec ImpactOrigin(const Flight& flight,int x,int y) {
    const auto contact=flight.ImpactPoint()*flight.shot.unitsPerMetre;
    const auto grid=GridPoint(flight.shot,flight.ImpactPoint())*flight.shot.unitsPerMetre;
    // Preserve native splash placement relative to the central cell without
    // rounding away the contact's sub-cell coordinates or vertical height.
    return contact+Vec{2.f*(x-int(grid.x*.5f)),0,2.f*(y-int(grid.z*.5f))};
}
inline void ImpactMatrix(Vec point,int missile,float out[16]) {
    const auto offset=QuiverImpactOffset(missile);
    const float position[3]{point.x,point.y,point.z};
    for(int i=0;i<3;++i)out[12+i]=position[i]-out[i]*offset.x-out[4+i]*offset.y-out[8+i]*offset.z;
    out[15]=1;
}
class ImpactVisuals {
    struct Effect {
        uint64_t ticket=0,session=0,expires=0;
        uintptr_t unit=0;
        uint32_t id=0,missile=0;
        Vec origin{};
        Vec contact{};
        int cellX=0,cellY=0;
        VisualBinding binding{};
        void Locate(Vec grid) {
            origin=contact+Vec{2.f*(int(grid.x*.5f)-cellX),0,2.f*(int(grid.z*.5f)-cellY)};
        }
        bool Matches(uintptr_t p,const uint32_t h[3]) const {
            return ticket&&unit==p&&h[0]==3&&h[1]==missile&&h[2]==id;
        }
    };
    std::array<Effect,128> effects{};
    size_t used=0;
    uint64_t serial=0;
    auto Used(){return std::span<Effect>(effects.data(),used);}
public:
    void Reset(){effects={};used=0;} // keep serial: old completions stay stale
    void Expire(uint64_t session,uint64_t now) {
        for(auto& e:Used())if(e.session!=session||now>=e.expires)e={};
        while(used&&!effects[used-1].ticket)--used;
    }
    bool Pending()const{return used!=0;}
    uint64_t Reserve(const Flight& flight,int missile,uint64_t now) {
        if(!OwnMissile(flight.shot)||!flight.shot.session||!QuiverImpactMissile(flight.shot.missile,missile))return 0;
        for(size_t i=0;i<effects.size();++i)if(!effects[i].ticket) {
            auto& e=effects[i];e={};e.ticket=++serial;e.session=flight.shot.session;
            e.expires=now+5000;e.missile=uint32_t(missile);
            const auto grid=GridPoint(flight.shot,flight.ImpactPoint())*flight.shot.unitsPerMetre;
            e.cellX=int(grid.x*.5f);e.cellY=int(grid.z*.5f);
            e.contact=e.origin=ImpactOrigin(flight,e.cellX,e.cellY);
            used=std::max(used,i+1);return e.ticket;
        }
        return 0; // native effect still plays if tracking capacity is exhausted
    }
    void Complete(uint64_t ticket,uintptr_t unit,const uint32_t h[3],const Vec* grid=nullptr) {
        for(auto& e:Used())if(e.ticket==ticket&&ticket) {
            if(!unit||h[0]!=3||h[1]!=e.missile||(e.unit&&!e.Matches(unit,h)))e={};
            else {e.unit=unit;e.id=h[2];if(grid)e.Locate(*grid);}
            break;
        }
    }
    bool Owns(uint64_t creating,uintptr_t unit,const uint32_t h[3]) {
        if(!unit||h[0]!=3)return false;
        for(const auto& e:Used())if(e.Matches(unit,h)||(creating&&e.ticket==creating&&e.missile==h[1]&&!e.unit))return true;
        return false;
    }
    bool Bind(uint64_t creating,uintptr_t unit,const uint32_t h[3],uint32_t entity,bool root,const Vec* grid=nullptr) {
        if(!root||!unit||h[0]!=3)return false;
        for(auto& e:Used())if(e.Matches(unit,h)||(creating&&e.ticket==creating&&e.missile==h[1]&&!e.unit)) {
            if(!e.binding.BindRoot(entity,root))return false;
            e.unit=unit;e.id=h[2];if(grid)e.Locate(*grid);return true;
        }
        return false;
    }
    bool Matrix(uint32_t entity,uint64_t session,float out[16]) {
        for(const auto& e:Used())if(e.ticket&&e.session==session&&e.binding.Matches(entity)) {
            ImpactMatrix(e.origin,int(e.missile),out);return true;
        }
        return false;
    }
    void Forget(uintptr_t unit){for(auto& e:Used())if(e.unit==unit)e={};}
};
}
