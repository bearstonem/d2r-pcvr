#include "loot_beams.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <vector>

using namespace framevr::lootbeams;

constexpr uint32_t Code(const char* s) {
    return uint32_t(uint8_t(s[0]))|uint32_t(uint8_t(s[1]))<<8|uint32_t(uint8_t(s[2]))<<16|uint32_t(uint8_t(s[3]))<<24;
}

int main() {
    // The game's label colours.
    assert(Colour(Beam::Unique).r==199&&Colour(Beam::Unique).g==179&&Colour(Beam::Unique).b==119);
    assert(Colour(Beam::Set).r==0&&Colour(Beam::Set).g==252&&Colour(Beam::Set).b==0);
    assert(Colour(Beam::Rare).r==255&&Colour(Beam::Rare).g==255&&Colour(Beam::Rare).b==100);
    assert(Colour(Beam::Rune).r==255&&Colour(Beam::Rune).g==168&&Colour(Beam::Rune).b==0);
    assert(Colour(Beam::None).r==0&&Colour(Beam::None).g==0&&Colour(Beam::None).b==0);
    assert(!std::strcmp(Name(Beam::Set),"set")&&!std::strcmp(Name(Beam::None),"none"));

    // Floor modes only.
    assert(OnFloor(3)&&OnFloor(5));
    for(uint32_t m:{0u,1u,2u,4u,6u})assert(!OnFloor(m));

    // Rune codes, space or NUL padded.
    assert(RuneNumber(Code("r01 "))==1);
    assert(RuneNumber(Code("r26 "))==26);
    assert(RuneNumber(Code("r33 "))==33);
    assert(RuneNumber(Code("r20\0"))==20);
    assert(RuneNumber(Code("r00 "))==0&&RuneNumber(Code("r34 "))==0);
    assert(RuneNumber(Code("rin "))==0&&RuneNumber(Code("hp1 "))==0&&RuneNumber(Code("r1x "))==0);
    assert(RuneNumber(Code("r01x"))==0);

    // Rarity -> beam.
    const Wanted all{};
    assert(Classify(QualityUnique,0,all)==Beam::Unique);
    assert(Classify(QualitySet,0,all)==Beam::Set);
    assert(Classify(QualityRare,0,all)==Beam::Rare);
    for(uint32_t q:{0u,1u,2u,3u,QualityMagic,QualityCrafted,9u})assert(Classify(q,0,all)==Beam::None);
    // Rare can be switched off; the others stay.
    Wanted noRare{};noRare.rare=false;
    assert(Classify(QualityRare,0,noRare)==Beam::None&&Classify(QualityUnique,0,noRare)==Beam::Unique);
    // High runes: from the threshold up, whatever their (normal) quality.
    assert(Classify(2,25,all)==Beam::None&&Classify(2,26,all)==Beam::Rune&&Classify(2,33,all)==Beam::Rune);
    Wanted lem{};lem.highRune=20;
    assert(Classify(2,20,lem)==Beam::Rune&&Classify(2,19,lem)==Beam::None);
    Wanted noRunes{};noRunes.runes=false;
    assert(Classify(2,33,noRunes)==Beam::None);

    // The beams to draw: nearest first, none past the range, at most MaxBeams.
    {
        std::vector<Lying> items;
        items.push_back({1,100,100,Beam::Unique});      // on the hero
        items.push_back({2,130,100,Beam::Rare});        // 30 away
        items.push_back({3,110,100,Beam::None});        // magic: never
        items.push_back({4,100+RangeCells+1,100,Beam::Set});   // just out of range
        items.push_back({5,100,90,Beam::Rune});         // 10 away
        items.push_back({6,100+RangeCells,100,Beam::Set});     // exactly at the range
        items.push_back({7,143,143,Beam::Set});         // 43,43: inside the square, outside the circle
        size_t count=9;
        auto picked=Select(items.data(),items.size(),100,100,count);
        assert(count==4);
        assert(picked[0].id==1&&picked[1].id==5&&picked[2].id==2&&picked[3].id==6);
        // More than MaxBeams: the nearest MaxBeams, in order.
        items.clear();
        for(uint32_t i=0;i<20;++i)items.push_back({100+i,100+int(19-i),100,Beam::Rare});   // ids 100.. farthest first
        picked=Select(items.data(),items.size(),100,100,count);
        assert(count==MaxBeams);
        for(size_t k=0;k<MaxBeams;++k)assert(picked[k].id==119-k);
        // Equal distances keep the scan's order; nothing at all gives none.
        items={{1,105,100,Beam::Rare},{2,95,100,Beam::Set},{3,100,105,Beam::Unique}};
        picked=Select(items.data(),items.size(),100,100,count);
        assert(count==3&&picked[0].id==1&&picked[1].id==2&&picked[2].id==3);
        picked=Select(nullptr,0,100,100,count);
        assert(count==0);
        static_assert(ServerItemTable==0x5c6*8);
        static_assert(InRange(100+RangeCells,100,100,100)&&!InRange(100+RangeCells+1,100,100,100));
        static_assert(InRange(142,142,100,100)&&!InRange(143,143,100,100));   // a circle, not a square
        // The brightness slider: 100% the beam as tuned, 10% the faintest; never 0.
        static_assert(Strength(100)==1.0f&&Strength(70)==0.7f&&Strength(10)==0.1f);
        static_assert(Strength(0)==0.1f&&Strength(-5)==0.1f&&Strength(250)==1.0f);
        const float nan=std::numeric_limits<float>::quiet_NaN();
        assert(Strength(nan)==1.0f);
    }

    std::puts("loot beams: ok");
    return 0;
}
