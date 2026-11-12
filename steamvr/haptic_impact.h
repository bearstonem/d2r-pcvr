#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include "haptic_strength.h"

// What a strike feels like by what it struck, and the bow's release snap: short
// sequences of pulses (amplitude, duration, frequency, each at its own time after
// the contact) played through the one haptic path (haptic_pulse.h), so the
// player's Vibration strengths and mutes apply to every step. Value-only.
//
// The rhythm carries the difference, since several controllers ignore the
// requested frequency: flesh is one long thud, dry undead three short clicks, a
// spirit a soft swell with no attack, a container a crack and then a long low
// crumble, a parry two sharp ringing taps, a block one tap.
namespace framevr::haptics {
enum class Surface { Flesh, Bone, Spirit, Container, Block, Parry, Count };
constexpr int SurfaceCount=int(Surface::Count);
inline constexpr const char* SurfaceName(Surface s) {
    switch(s) {
        case Surface::Flesh:return "flesh";case Surface::Bone:return "bone";case Surface::Spirit:return "spirit";
        case Surface::Container:return "container";case Surface::Block:return "block";case Surface::Parry:return "parry";
        default:return "?";
    }
}
// MonStats flags as the game's record holds them (vr/melee.cpp MonsterFlags; holy_bolt.h reads the
// same undead and demon bits): 0x800 low undead (skeletons, zombies, mummies), 0x1000 high undead,
// 0x4000 flying. A flying high undead is a ghost or wraith. Everything else, demons and unread
// flags (0) included, is flesh: the record names no stone or metal, so none is invented here.
constexpr uint32_t MonLowUndead=0x800,MonHighUndead=0x1000,MonFlying=0x4000;
inline constexpr Surface MonsterSurface(uint32_t flags) {
    if((flags&MonHighUndead)&&(flags&MonFlying))return Surface::Spirit;
    if(flags&MonLowUndead)return Surface::Bone;
    return Surface::Flesh;
}
struct Step {int at=0;Sample sample{};};   // at: milliseconds after the contact
struct Sequence {
    static constexpr int Max=4;
    std::array<Step,Max> steps{};int count=0;Category category=Category::Melee;
    constexpr int Length()const{int end=0;for(int i=0;i<count;++i)end=std::max(end,steps[i].at+steps[i].sample.milliseconds);return end;}
};
constexpr float ShortTapHz=180;constexpr int ShortTapMs=10;   // Short melee taps (SampleFor)
// The pattern for a strike. Flesh and Block are the taps they always were (SampleFor: 90 ms on the
// weapon hand, 45 ms on the shield hand). Short melee taps keeps each pattern's rhythm and strengths
// with every step cut to the 10 ms / 180 Hz tap that setting asks for.
inline constexpr Sequence ImpactPattern(Surface surface,int hand=0,bool shortMelee=false) {
    Sequence s;
    switch(surface) {
        case Surface::Bone:      s.count=3;s.steps={{{0,{.95f,25,240}},{45,{.55f,20,240}},{85,{.35f,20,240}}}};break;
        case Surface::Spirit:    s.count=3;s.steps={{{0,{.22f,40,60}},{40,{.40f,60,60}},{100,{.18f,50,60}}}};break;
        case Surface::Container: s.count=2;s.steps={{{0,{1.f,35,170}},{45,{.40f,110,70}}}};break;
        case Surface::Parry:     s.count=2;s.steps={{{0,{1.f,20,250}},{50,{.60f,35,250}}}};break;
        case Surface::Block:     s.count=1;s.steps[0]={0,SampleFor(Category::Melee,false,1)};break;
        default:                 s.count=1;s.steps[0]={0,SampleFor(Category::Melee,false,hand)};break;
    }
    if(shortMelee)for(int i=0;i<s.count;++i){s.steps[i].sample.milliseconds=ShortTapMs;s.steps[i].sample.frequency=ShortTapHz;}
    return s;
}
// A bow's release, by how far it was drawn: a sharp crack in both hands, then the
// string settling in the bow hand (left). Bow / throw release strength scales it.
inline Sequence BowSnap(float draw,int hand) {
    draw=std::isfinite(draw)?std::clamp(draw,0.f,1.f):0;
    Sequence s;s.category=Category::BowRelease;
    if(hand==0){s.count=1;s.steps[0]={0,{.45f+.50f*draw,20,220}};}
    else if(hand==1){s.count=2;s.steps={{{0,{.50f+.50f*draw,25,220}},{40,{.20f+.30f*draw,70,80}}}};}
    return s;
}
// One sequence per hand; a new one replaces what was playing there. Play, then
// ask Due each frame: it hands out each step once, when its time has come. A
// step more than LateMs overdue (a stall) is dropped rather than played out of
// rhythm. Single-threaded: the XR input update owns it.
class Sequencer {
    struct Voice {Sequence sequence{};int next=0;uint64_t start=0;float scale=1;bool active=false,preview=false;Strengths levels{};};
    std::array<Voice,2> voices{};
public:
    static constexpr uint64_t LateMs=60;
    struct Out {Sample sample{};Category category=Category::Melee;bool preview=false;Strengths levels{};int step=0;};
    // scale multiplies every step's amplitude (a harder swing; clamped 0..2). preview: unsaved strengths (Feel it).
    void Play(int hand,const Sequence& sequence,uint64_t now,float scale=1,const Strengths* preview=nullptr) {
        if(hand<0||hand>=2)return;
        auto& v=voices[hand];v={};
        if(sequence.count<=0||sequence.count>Sequence::Max)return;
        v.sequence=sequence;v.start=now;v.scale=std::isfinite(scale)?std::clamp(scale,0.f,2.f):1.f;v.active=true;
        if(preview){v.preview=true;v.levels=*preview;}
    }
    bool Due(int hand,uint64_t now,Out& out) {
        if(hand<0||hand>=2)return false;
        auto& v=voices[hand];
        while(v.active) {
            if(v.next>=v.sequence.count){v.active=false;break;}
            const auto& step=v.sequence.steps[v.next];
            const uint64_t at=v.start+uint64_t(std::max(step.at,0));
            if(now<at)return false;
            const int index=v.next++;
            if(v.next>=v.sequence.count)v.active=false;
            if(now-at>LateMs||step.sample.milliseconds<=0)continue;
            out.sample=step.sample;out.sample.amplitude=std::clamp(step.sample.amplitude*v.scale,0.f,1.f);
            out.category=v.sequence.category;out.preview=v.preview;out.levels=v.levels;out.step=index;
            return true;
        }
        return false;
    }
    bool Active(int hand)const{return hand>=0&&hand<2&&voices[hand].active;}
    void Clear(){voices={};}
};
}
