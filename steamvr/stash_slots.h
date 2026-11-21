#pragma once
// The stash's own Gems / Materials / Runes slots (bankexpansionlayouthd.json's advancedstash_*
// widgets) in the 2244 x 2352 UI image: the slot's tab and its middle. The layout's panel maps
// to the image as x = 361.4 + 0.585 lx, y = 115.6 + 0.585 ly (measured on the owner's Gems shot,
// 2026-11-21). Written by build-frame\make_stash_slots.py.
#include <cstdint>
#include <cstring>
namespace framevr::stashslots {
struct Slot {char code[4];int tab,x,y;};
inline constexpr Slot Slots[]={
    {"gcw",2,486,280},
    {"gfw",2,486,337},
    {"gsw",2,486,396},
    {"glw",2,486,454},
    {"gpw",2,486,513},
    {"gcg",2,557,280},
    {"gfg",2,557,337},
    {"gsg",2,557,396},
    {"glg",2,557,454},
    {"gpg",2,557,513},
    {"gcr",2,629,280},
    {"gfr",2,629,337},
    {"gsr",2,629,396},
    {"glr",2,629,454},
    {"gpr",2,629,513},
    {"gcy",2,700,280},
    {"gfy",2,700,337},
    {"gsy",2,700,396},
    {"gly",2,700,454},
    {"gpy",2,700,513},
    {"gcv",2,771,280},
    {"gfv",2,771,337},
    {"gsv",2,771,396},
    {"gzv",2,771,454},
    {"gpv",2,771,513},
    {"gcb",2,843,280},
    {"gfb",2,843,337},
    {"gsb",2,843,396},
    {"glb",2,843,454},
    {"gpb",2,843,513},
    {"skc",2,914,280},
    {"skf",2,914,337},
    {"sku",2,914,396},
    {"skl",2,914,454},
    {"skz",2,914,513},
    {"pk1",3,450,309},
    {"pk2",3,511,309},
    {"pk3",3,571,309},
    {"ua1",3,709,309},
    {"ua2",3,770,309},
    {"ua3",3,831,309},
    {"ua4",3,892,309},
    {"ua5",3,953,309},
    {"dhn",3,450,413},
    {"bey",3,511,413},
    {"mbr",3,571,413},
    {"xa1",3,709,413},
    {"xa2",3,770,413},
    {"xa3",3,831,413},
    {"xa4",3,892,413},
    {"xa5",3,953,413},
    {"rvs",3,450,493},
    {"rvl",3,511,493},
    {"toa",3,706,493},
    {"tes",3,768,493},
    {"ceh",3,829,493},
    {"bet",3,890,493},
    {"fed",3,950,493},
    {"r01",4,457,301},
    {"r02",4,518,303},
    {"r03",4,578,302},
    {"r04",4,639,302},
    {"r05",4,700,302},
    {"r06",4,761,302},
    {"r07",4,822,302},
    {"r08",4,883,302},
    {"r09",4,943,302},
    {"r10",4,457,363},
    {"r11",4,518,363},
    {"r12",4,578,363},
    {"r13",4,639,363},
    {"r14",4,700,363},
    {"r15",4,761,363},
    {"r16",4,822,363},
    {"r17",4,883,363},
    {"r18",4,943,363},
    {"r19",4,457,424},
    {"r20",4,518,424},
    {"r21",4,578,424},
    {"r22",4,639,424},
    {"r23",4,700,424},
    {"r24",4,761,424},
    {"r25",4,822,424},
    {"r26",4,883,424},
    {"r27",4,943,424},
    {"r28",4,457,485},
    {"r29",4,518,485},
    {"r30",4,883,485},
    {"r31",4,943,485},
    {"r32",4,457,547},
    {"r33",4,943,544},
};
// The slot for an item code packed as the collection packs it (chars little-endian, space-padded).
inline const Slot* Find(uint32_t code) {
    const char text[3]={char(code&0xFF),char((code>>8)&0xFF),char((code>>16)&0xFF)};
    for(const auto& s:Slots)if(std::memcmp(s.code,text,3)==0)return &s;
    return nullptr;
}
// Where a pull sends the hand's item to its slot: the whole stash page under the tab bar
// (owner 2026-11-21: "anywhere"; the log showed pulls at y 600-700 missed the slots-only area),
// all but the Horadric Cube's box, which stays the game's (gems go into the cube there).
constexpr int AreaLeft=395,AreaRight=1005,AreaTop=232,AreaBottom=990;
constexpr int CubeLeft=600,CubeRight=802,CubeTop=556,CubeBottom=815;
inline bool InArea(float x,float y) {
    return x>=AreaLeft&&x<=AreaRight&&y>=AreaTop&&y<=AreaBottom&&!(x>=CubeLeft&&x<=CubeRight&&y>=CubeTop&&y<=CubeBottom);
}
}   // namespace framevr::stashslots
