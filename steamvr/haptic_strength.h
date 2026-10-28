#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include "../shared/d2r_vr_gore.h"

namespace framevr::haptics {
constexpr float DefaultStrength=100,MaxStrength=200,StrengthStep=10;
enum class Category { Melee, BowDraw, BowRelease, Spell, Interaction, Alert, Count };
constexpr int CategoryCount=int(Category::Count);
struct Strengths {
    float master=DefaultStrength;
    std::array<float,CategoryCount> category{100,100,100,100,100,100};
};
// gameplay::Output feedback IDs: selection/nock, release, potion,
// string proximity, full draw, melee readiness. A potion stays Interaction
// even when the player has a bow equipped.
inline Category MotionCategory(unsigned event,bool bowReady,bool javelinReady) {
    if(event==6)return Category::Melee;
    if(event==3)return Category::Interaction;
    if(event==4||event==5||(bowReady&&(event==1||event==2)))return event==2?Category::BowRelease:Category::BowDraw;
    if(javelinReady&&(event==1||event==2))return Category::BowRelease;
    return Category::Interaction;
}
struct Sample {float amplitude;int milliseconds;float frequency=0;};
inline constexpr std::array<Sample,CategoryCount> Samples{{{.95f,90},{.35f,25},{.65f,45},{.4f,25},{.45f,45},{.25f,100}}};
// Share the selected melee pulse between real impacts and the settings test.
// Default to a longer tap at the runtime's preferred frequency; the original
// 10 ms / 180 Hz pulse is an opt-in for controllers that reproduce it well.
inline constexpr Sample SampleFor(Category category,bool shortMelee=false,int hand=0) {
    if(int(category)<0||int(category)>=CategoryCount)return {};
    if(category==Category::Melee&&shortMelee)return {.95f,10,180};
    if(category==Category::Melee&&hand==1)return {.95f,45};
    return Samples[int(category)];
}
// Start from the player's own melee sample (respecting Short melee pulses and hand variant)
// and double its duration at the same frequency so a lethal kill buzz always feels heavier.
inline constexpr Sample GoreSampleFor(gore::GoreType type, Sample base = SampleFor(Category::Melee)) {
    if(type==gore::GoreType::None)return base;
    return {1.0f, base.milliseconds * 2, base.frequency};
}
inline constexpr Sample GoreSampleFor(gore::GoreType type, bool shortMelee, int hand = 0) {
    return GoreSampleFor(type, SampleFor(Category::Melee, shortMelee, hand));
}
using Pattern=std::array<Sample,2>; // right, left
inline Pattern BowPull(float draw) {
    draw=std::isfinite(draw)?std::clamp(draw,0.f,1.f):0;
    return {{{.22f+.28f*draw,25},{.1f+.15f*draw,20}}};
}
inline constexpr Pattern Release(bool bow) {return {{{.65f,45},bow?Sample{.65f,35}:Sample{}}};}
inline constexpr int PreviewCount(Category category) {
    return int(category)<0||int(category)>=CategoryCount?0:category==Category::BowDraw||category==Category::Alert?1:2;
}
inline constexpr const char* PreviewLabel(Category category,int choice) {
    if(choice<0||choice>=PreviewCount(category))return "";
    if(category==Category::Melee)return choice==0?"Weapon":"Shield";
    if(category==Category::BowRelease)return choice==0?"Bow":"Throw";
    if(category==Category::BowDraw||category==Category::Alert)return "Test";
    return choice==0?"Test L":"Test R";
}
inline Pattern Preview(Category category,int choice,bool shortMelee=false) {
    Pattern pulses{};
    if(choice<0||choice>=PreviewCount(category))return pulses;
    if(category==Category::BowDraw)return BowPull(.5f);
    if(category==Category::BowRelease)return Release(choice==0);
    const int hand=category==Category::Melee?choice:category==Category::Alert?1:1-choice;
    pulses[hand]=SampleFor(category,shortMelee,hand);return pulses;
}
inline float NormalizeStrength(float percent) {
    if(!std::isfinite(percent))return DefaultStrength;
    return std::round(std::clamp(percent,0.f,MaxStrength)/StrengthStep)*StrengthStep;
}
inline float ScaleAmplitude(float amplitude,float master,float category=DefaultStrength) {
    if(!std::isfinite(amplitude))return 0;
    return std::clamp(std::clamp(amplitude,0.f,1.f)*(NormalizeStrength(master)/100.f)*(NormalizeStrength(category)/100.f),0.f,1.f);
}
}
