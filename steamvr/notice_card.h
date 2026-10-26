#pragma once
#include "native_panels.h"
#include <array>
#include <cstdint>
#include <cstring>

namespace framevr::notice {
// D2R's NotificationPanel ("Quest has been updated", "You've leveled up") is
// part of the flat interface, so in XR it hung in the middle of the view, at
// the eyes, for 10.8 s a message. The panel stays open but hidden (its timers
// and its Start button action are untouched); the message is shown on a short
// card standing in the room instead, like the native dialogue surfaces.
enum Kind {Quest,Skills,Stats,Kinds};
// Controller wording: XR runs the native interface in controller mode.
inline constexpr const char* Keys[Kinds]={"d2r:QuestLogNotificationController",
    "d2r:LevelUpNotificationController","d2r:LevelUpStatsNotificationController"};
// Those strings name the Start button with a glyph of the game's own font
// (U+E01D) that system fonts lack. U+2261 is the same three bars, in 3 bytes too.
inline void StartGlyph(char* text) {
    for(char* p=text;(p=std::strstr(p,"\xEE\x80\x9D"));p+=3)std::memcpy(p,"\xE2\x89\xA1",3);
}

constexpr int Width=512,Height=112;
constexpr float PanelWidth=.46f,PanelHeight=PanelWidth*Height/Width;
// At the eyes' heading, a metre out and below eye level, leaning back to face them.
constexpr float Distance=1.f,Drop=.28f;
inline panels::Placement Place(math::Vec head,math::Quat look) {
    const auto yaw=math::Yaw(math::Heading(look));
    return {head+math::Rotate(yaw,{0,-Drop,-Distance}),
        math::Multiply(yaw,panels::Pitch(-std::atan2(Drop,Distance)))};
}

// One card at a time, each for ShowMs from when it is first on screen. One
// that cannot be shown for WaitMs (an opaque menu, VR Settings) is dropped
// rather than surfacing out of context. A card that directly follows another
// keeps its place; otherwise it is placed where the player looks.
constexpr uint64_t ShowMs=3000,WaitMs=10000,FollowMs=500;
struct Card {int kind=-1;uint64_t sequence=0;bool place=false;};
class Cards {
    std::array<int,4> queue{};int count=0;
    int current=-1;uint64_t sequence=0,since=0,shownAt=0,endedAt=0;
    bool shown=false,ended=false,place=false;
    void Next(uint64_t now) {
        if(current>=0&&shown){ended=true;endedAt=now;}
        current=-1;
        if(!count)return;
        current=queue[0];for(int i=1;i<count;++i)queue[i-1]=queue[i];--count;
        ++sequence;since=now;shown=false;place=!ended||now-endedAt>FollowMs;
    }
public:
    void Push(int kind,uint64_t now) {
        if(kind<0||kind>=Kinds||count==int(queue.size()))return;
        queue[count++]=kind;
        if(current<0)Next(now);
    }
    Card Step(uint64_t now) {
        if(current>=0&&(shown?now-shownAt>=ShowMs:now-since>=WaitMs))Next(now);
        return {current,sequence,place};
    }
    // The current card reached the screen: its time starts, its place is set.
    void Shown(uint64_t now) {if(current>=0&&!shown){shown=true;shownAt=now;}place=false;}
    void Clear() {count=0;current=-1;shown=ended=place=false;}
};

// What adds cards: quest updates counted on the UI thread, and a level gained
// within one character's session once the native panel shows it (the game
// queues both level-up messages). A new session adopts both without a card.
constexpr uint64_t LevelWaitMs=2000;
struct Watch {
    uint64_t session=0,levelAt=0;uint32_t level=0,quests=0;bool leveled=false;
    template<class Push>void Step(uint64_t s,bool levelKnown,uint32_t newLevel,uint32_t questCount,bool panelOpen,uint64_t now,Push push) {
        if(!s){session=0;level=0;leveled=false;return;}
        if(s!=session){session=s;level=levelKnown?newLevel:0;quests=questCount;leveled=false;return;}
        if(questCount!=quests){if(int32_t(questCount-quests)>0)push(Quest);quests=questCount;}
        if(levelKnown) {
            if(level&&newLevel>level&&!leveled){leveled=true;levelAt=now;}
            level=newLevel;
        }
        if(!leveled)return;
        if(panelOpen){push(Skills);push(Stats);leveled=false;}
        else if(now-levelAt>=LevelWaitMs)leveled=false;
    }
};
}
