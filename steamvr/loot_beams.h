#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

// Loot beams (Cal, 2026-10-31): a beam of light stands over each Rare, Set and
// Unique item and each high rune lying on the floor, in the colour the game
// writes its name in, until it is picked up. Not through walls; Rare can be
// switched off. The game's own Horadric Light beam (the one D2R Reimagined puts
// on its runes) could not be recoloured: four data-only tests on Cal's build left
// every copy white. So the mod draws the beam itself, in D2R_DepthFog.fx's
// outline pass, where each pixel's world point is known: the scene in front
// hides it, walls included (loot_beam_scan.cpp finds the items, vrcam sets the
// shader's BeamPos/BeamCol). Still to come: none on items the loot filter
// hides, and the VR Settings toggles. This header holds the rules.
namespace framevr::lootbeams {
enum class Beam : uint8_t {None,Rare,Set,Unique,Rune};
struct Rgb {uint8_t r=0,g=0,b=0;};
// The game's own label colours (data/global/ui/layouts/_profilehd.json):
// Unique FontColorGoldYellow, Set FontColorGreen, Rare FontColorYellow, and
// FontColorOrange for runes (the colour the game writes rune names in).
inline constexpr Rgb Colour(Beam beam) {
    switch(beam) {
        case Beam::Unique:return {199,179,119};
        case Beam::Set:return {0,252,0};
        case Beam::Rare:return {255,255,100};
        case Beam::Rune:return {255,168,0};
        default:return {};
    }
}
inline constexpr const char* Name(Beam beam) {
    switch(beam) {
        case Beam::Unique:return "unique";
        case Beam::Set:return "set";
        case Beam::Rare:return "rare";
        case Beam::Rune:return "rune";
        default:return "none";
    }
}
// Item quality as the game stores it (D2RLoader SDK Items::Quality).
inline constexpr uint32_t QualityMagic=4,QualitySet=5,QualityRare=6,QualityUnique=7,QualityCrafted=8;
// Item unit modes: 3 lying on the floor, 5 falling to it.
inline constexpr uint32_t ModeOnFloor=3,ModeDropping=5;
inline constexpr bool OnFloor(uint32_t mode) {return mode==ModeOnFloor||mode==ModeDropping;}
// Runes are r01 (El) .. r33 (Zod). Which count as high is Cal's call; until he
// sets it, Vex (r26) and up. Reimagined beams Lem (r20) and up.
inline constexpr int FirstRune=1,LastRune=33,DefaultHighRune=26;
// Rune number of a four-character item code ('r','0','1',' ' as the game packs
// it, low byte first; a trailing space or NUL), 0 for anything else.
inline constexpr int RuneNumber(uint32_t code) {
    const char c0=char(code&0xff),c1=char((code>>8)&0xff),c2=char((code>>16)&0xff),c3=char((code>>24)&0xff);
    if(c0!='r'||c1<'0'||c1>'9'||c2<'0'||c2>'9'||(c3!=' '&&c3!=0))return 0;
    const int n=(c1-'0')*10+(c2-'0');
    return n>=FirstRune&&n<=LastRune?n:0;
}
// The server's item units (measured on Cal's build, 2026-10-31): unit table 3,
// game + 0x5c6*8, 128 buckets chained through +0x158 (the 0x80-qword stride from
// players at 0x446 put them at 0x646, which is empty). Quality at item data +0x00,
// flags at +0x18, the floor cell at the static path's +0x10/+0x14.
inline constexpr uintptr_t ServerItemTable=0x5c6*8;
inline constexpr size_t MaxBeams=8;           // D2R_DepthFog.fx draws BeamPos0..7
inline constexpr int RangeCells=60;           // farther than this from the hero, no beam
struct Lying {uint32_t id=0;int x=0,y=0;Beam beam=Beam::None;};
// VR Settings > Interface > Labels > Loot beams' slider ([loot] beams_brightness, 10% to 100%) as the
// beams' strength (D2R_DepthFog.fx BeamShape.w: the pillar and its floor glow alike). An
// out-of-range ini value is held to the slider's ends; never 0, which the filter is for.
inline constexpr float Strength(float percent) {
    return percent!=percent?1.0f:percent<10.0f?0.1f:percent>100.0f?1.0f:percent/100.0f;
}
// Within RangeCells of the hero (hx, hy), across the ground.
inline constexpr bool InRange(int x,int y,int hx,int hy) {
    const long long dx=(long long)x-hx,dy=(long long)y-hy;
    return dx*dx+dy*dy<=(long long)RangeCells*RangeCells;
}
// The beams to draw: beam-worthy items within RangeCells of the hero (hx, hy),
// nearest first, at most MaxBeams. Ties keep the scan's order.
inline std::array<Lying,MaxBeams> Select(const Lying* items,size_t n,int hx,int hy,size_t& count) {
    std::array<Lying,MaxBeams> out{};std::array<long long,MaxBeams> squared{};
    count=0;
    for(size_t i=0;i<n;++i) {
        const auto& item=items[i];
        if(item.beam==Beam::None||!InRange(item.x,item.y,hx,hy))continue;
        const long long dx=(long long)item.x-hx,dy=(long long)item.y-hy,d=dx*dx+dy*dy;
        size_t at=count;
        while(at>0&&squared[at-1]>d)--at;
        if(at>=MaxBeams)continue;
        for(size_t k=count<MaxBeams?count:MaxBeams-1;k>at;--k){out[k]=out[k-1];squared[k]=squared[k-1];}
        out[at]=item;squared[at]=d;
        if(count<MaxBeams)++count;
    }
    return out;
}
struct Wanted {bool rare=true,set=true,unique=true,runes=true;int highRune=DefaultHighRune;};
// rune: RuneNumber of the item's code (0 when it is not a rune).
inline constexpr Beam Classify(uint32_t quality,int rune,const Wanted& wanted) {
    if(rune) return wanted.runes&&rune>=wanted.highRune&&rune<=LastRune?Beam::Rune:Beam::None;
    switch(quality) {
        case QualityUnique:return wanted.unique?Beam::Unique:Beam::None;
        case QualitySet:return wanted.set?Beam::Set:Beam::None;
        case QualityRare:return wanted.rare?Beam::Rare:Beam::None;
        default:return Beam::None;
    }
}
}
