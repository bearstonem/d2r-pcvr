#pragma once
#include <array>
#include <cstdint>
#include <cstring>

namespace d2rcam::monsterbars {
// D2R 3.3.93787: UnitAny::pMonsterData at +0x10; MonsterData's
// MonStats pointer at +0, special-monster flags at +0x1a. The latter's
// low bits identify superuniques, champions, uniques and their minions.
// See docs/monster-bars.md for layout evidence and validation limits.
// Read is injected so failed/stale reads can be covered without a game.
// Natively each read is a system call, and this runs for every label anchor
// of every eye pass: the fields come in one read per record (the unit's
// first 0x18 bytes, the monster data's first 0x1b, the row's first 0x40),
// three for an ordinary monster where each field had its own (seven), one
// for a unit that is no monster. The tests and their order are as they were.
inline constexpr size_t UnitBytes=0x18,DataBytes=0x1b,RowBytes=0x40;
template<class T,size_t N> T Field(const std::array<uint8_t,N>& bytes,size_t at) {
    T value{};std::memcpy(&value,bytes.data()+at,sizeof value);return value;
}
template<class Read> bool Hide(bool stereoUi,bool showOrdinary,bool above,
                                uintptr_t unit,Read read) {
    if(!stereoUi||showOrdinary||!above||unit<0x10000)return false;
    std::array<uint8_t,UnitBytes> unitBytes{};
    if(!read(unit,unitBytes)||Field<uint32_t>(unitBytes,0)!=1)return false;
    const uint32_t classId=Field<uint32_t>(unitBytes,4);
    const uintptr_t data=Field<uintptr_t>(unitBytes,0x10);
    if(classId>0xffff||data<0x10000)return false;
    std::array<uint8_t,DataBytes> dataBytes{};
    if(!read(data,dataBytes)||Field<uint8_t>(dataBytes,0x1a))return false;
    const uintptr_t row=Field<uintptr_t>(dataBytes,0);
    if(row<0x10000)return false;
    std::array<uint8_t,RowBytes> rowBytes{};
    if(!read(row,rowBytes)||Field<uint16_t>(rowBytes,0)!=classId)return false;
    const uint32_t flags=Field<uint32_t>(rowBytes,0x3c);
    // Require a recognized, enabled, killable monster row. Preserve bosses
    // even when they lack unique flags, and preserve NPC/interaction labels.
    constexpr uint32_t required=(1u<<25)|(1u<<15);
    constexpr uint32_t keep=(1u<<6)|(1u<<7)|(1u<<8)|(1u<<9)|(1u<<10);
    return (flags&required)==required&&(flags&keep)==0;
}
}
