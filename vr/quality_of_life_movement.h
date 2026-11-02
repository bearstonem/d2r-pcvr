#pragma once
#include <windows.h>
#include <array>
#include <atomic>
#include <cmath>
#include <climits>
#include <cstdint>
#include <cstdio>

namespace framevr::qol::movement {
// Native ABI/signatures: vr/projectiles.cpp, vr/melee.cpp and
// vr/bow_signatures.h. The same velocity setter serves client and server.
inline constexpr uint64_t RelationRva=0x48e460,VelocityLayoutRva=0x341d30;
inline constexpr uint8_t RelationSig[]={0x41,0xb1,0x01,0xe9,0x28,0x43,0x00,0x00};
inline constexpr uint8_t VelocityLayoutSig[]={0x8b,0x81,0xa0,0x00,0x00,0x00,0xc3};
using RelationFn=int(*)(uintptr_t,uintptr_t,uintptr_t);
inline RelationFn relation=nullptr;
inline std::atomic<bool> ready{false},enabled{false};
inline std::atomic<double> multiplier{1};
inline std::atomic<uint32_t> localPlayer{~0u},generation{1};
inline std::atomic<uint64_t> adjustedCalls{0};
struct Server {uintptr_t game=0,player=0;uint32_t generation=0;};
inline thread_local Server server;

// Only an ID and a session tag cross threads, never a native unit pointer.
// A collision evicts an entry, making an unknown client ID pass through.
// Every server velocity assignment refreshes allegiance, including friendly
// results. Keep that result until its next assignment or session change so a
// long path cannot make client movement revert while server speed stays set.
class HostilityCache {
    struct Entry {std::atomic<uint64_t> key{0};};
    std::array<Entry,4096> entries{};
public:
    void Put(uint32_t id,uint32_t gen,int kind) {
        auto& e=entries[id%entries.size()];
        e.key.store(uint64_t(id)|(uint64_t(gen&0x3fffffff)<<32)|(uint64_t(kind&3)<<62),std::memory_order_release);
    }
    int Get(uint32_t id,uint32_t gen) const {
        const auto& e=entries[id%entries.size()];
        const uint64_t key=e.key.load(std::memory_order_acquire);
        if(!key||uint32_t(key)!=id||uint32_t((key>>32)&0x3fffffff)!=(gen&0x3fffffff)) return -1;
        return int(key>>62); // 0 friendly, 1 hostile
    }
};
inline HostilityCache hostility;

template<class T> bool Read(uintptr_t address,T& value) {
    SIZE_T copied=0;
    return address>=0x10000&&address<=UINTPTR_MAX-sizeof value&&ReadProcessMemory(GetCurrentProcess(),
        reinterpret_cast<const void*>(address),&value,sizeof value,&copied)&&copied==sizeof value;
}
inline int Scale(int native,double factor) {
    if(native<=0||!std::isfinite(factor)||factor<0||factor>3||factor==1) return native;
    if(factor==0) return 0;
    const double scaled=std::round(double(native)*factor);
    return scaled>=INT_MAX?INT_MAX:scaled<1?1:int(scaled);
}
inline void ResetPlayer(uint32_t id=~0u) {
    localPlayer.store(~0u,std::memory_order_release);
    generation.fetch_add(1,std::memory_order_acq_rel);
    localPlayer.store(id,std::memory_order_release);
}

inline void ObserveLookup(uintptr_t game,int type,uint32_t id,uintptr_t result) {
    const auto gen=generation.load(std::memory_order_acquire),local=localPlayer.load(std::memory_order_acquire);
    if(enabled.load(std::memory_order_acquire)&&type==0&&id==local&&local!=~0u) {
        uint32_t header[3]{};uintptr_t back=0;
        if(Read(result,header)&&header[0]==0&&header[2]==id&&Read(result+0xd8,back)&&back==game&&
           gen==generation.load(std::memory_order_acquire)) server={game,result,gen};
    }
}

inline int Adjust(uintptr_t path,int native) {
    const double factor=multiplier.load(std::memory_order_relaxed);
    if(native<=0||factor==1||!enabled.load(std::memory_order_acquire)||!ready.load(std::memory_order_acquire)||
       localPlayer.load(std::memory_order_acquire)==~0u) return native;
    uintptr_t unit=0,back=0,data=0,row=0,game=0;
    uint32_t h[4]{},flags=0;
    // Back-pointer validation and explicit WALK/RUN modes exclude missiles,
    // players, attacks, cast/skill movement, knockback and corpses.
    if(!Read(path+0x40,unit)||!Read(unit,h)||h[0]!=1||h[2]==~0u||
       (h[3]!=2&&h[3]!=15)||!Read(unit+0x38,back)||back!=path||
       !Read(unit+0x10,data)||!Read(data,row)||!Read(row+0x3c,flags)||
       (flags&(0x100u|0x400u))) return native; // NPC / inTown
    const uint32_t gen=generation.load(std::memory_order_acquire);
    int kind=-1;
    if(server.generation==gen&&server.game&&Read(unit+0xd8,game)&&game==server.game) {
        // This is the server unit on the thread that observed its local-player
        // server lookup. Never invoke server relationship code on client units.
        uint32_t player[3]{};uintptr_t playerGame=0;
        if(!Read(server.player,player)||player[0]!=0||player[2]!=localPlayer.load(std::memory_order_acquire)||
           !Read(server.player+0xd8,playerGame)||playerGame!=game) return native;
        kind=relation(game,server.player,unit)!=0?1:0;
        if(gen!=generation.load(std::memory_order_acquire)) return native;
        hostility.Put(h[2],gen,kind);
    } else kind=hostility.Get(h[2],gen);
    if(kind!=1||gen!=generation.load(std::memory_order_acquire)) return native;
    const int result=Scale(native,factor);
    if(result!=native) adjustedCalls.fetch_add(1,std::memory_order_relaxed);
    return result;
}
}
