#pragma once
#include <cmath>
#include <cstdint>
#include <string_view>

// An NPC conversation finds the hero standing (owner 2026-10-29: "When walking towards an
// npc and interacting with them while walking the character drifts ... I feel like the
// character should stop"; after rev1, 2026-10-30: "It needs to instantly stop player
// movement when the NPC dialogue starts").
// The left stick walks by handing the game a point cells ahead (vrcam HookStickGet). A
// pressed on an NPC opens the conversation in the same frame, and from then on the game
// takes no walk command: neither its own stop for the stick nor any click (rev1 clicked his
// own cell at once, 23 tests on 2026-10-29: he still went 1-6 cells to the stick's point).
// So the stop comes first: at the press, with the stick pushed, the walk is let go (the
// game's own stop, as when the stick is released) and A reaches the game HoldMs later.
namespace framevr::talkstop {
// The panels a conversation opens (names from the 2026-10 logs); story speech can open alone.
inline bool TalkPanel(std::string_view name) {return name=="NpcDialogPanel"||name=="ScrollSpeechPanel";}

constexpr uint64_t HoldMs=50;       // A kept from the game after the press, while the walk stops
constexpr uint64_t MinDownMs=80;    // then A down at least this long, even after a quick tap
constexpr uint64_t WalkMaxMs=1000;  // the walk let go until the conversation opens, at most this

// The interact press onto an NPC, on the pad's poll (vrcam ApplyActions).
struct Hold {
    bool on=false,wasDown=false;
    uint64_t pressAt=0;
    uint32_t talks=0;
    // down: the interact press as the game would get it; npc: its target is an NPC;
    // walking: the left stick pushed; talksNow: conversations opened so far (gamestate).
    // Returns whether A goes to the game now; began: this poll started a hold.
    bool Poll(bool down,bool npc,bool walking,uint32_t talksNow,uint64_t now,bool* began=nullptr) {
        const bool start=down&&!wasDown;wasDown=down;
        if(began)*began=false;
        if(start&&npc&&walking&&!on) {on=true;pressAt=now;talks=talksNow;if(began)*began=true;}
        if(!on)return down;
        if(now-pressAt<HoldMs)return false;   // the game stops the walk first
        const bool a=down||now-pressAt<HoldMs+MinDownMs;
        if(talksNow!=talks||now-pressAt>=WalkMaxMs)on=false;   // the conversation is up, or none came
        return a;
    }
    // Whether the walk is let go now (the stick reaches the game as released).
    bool WalkHeld(uint64_t now) const {return on&&now-pressAt<WalkMaxMs;}
};

constexpr uint64_t WatchMs=1500;   // the drift after an open is measured this long (developer log)
constexpr uint64_t LostMs=500;     // no hero position this long after the open: not measured

// How far the hero goes after a conversation opens (developer builds), on vrcam's 1 ms timer.
// opened: conversations opened so far; x, y the hero's grid position when known.
struct Watch {
    enum class Action {None,Report,Lost};
    bool synced=false,armed=false,have=false;
    uint32_t talks=0;
    uint64_t openedAt=0;
    float x0=0,y0=0,drift=0;
    // Whether a position read is wanted now: at an open, and at the watch's end.
    bool Wants(uint32_t opened,uint64_t now) const {
        if(!synced)return false;
        if(opened!=talks)return true;
        return armed&&(!have||now-openedAt>=WatchMs);
    }
    // active: VR, in a game. Off, or the first call: only counts.
    Action Step(bool active,uint32_t opened,uint64_t now,bool known,float x,float y) {
        if(!synced||!active) {synced=true;talks=opened;armed=false;return Action::None;}
        if(opened!=talks) {talks=opened;armed=true;have=false;openedAt=now;}
        if(!armed)return Action::None;
        if(!have) {
            if(known) {have=true;x0=x;y0=y;return Action::None;}
            if(now-openedAt>=LostMs) {armed=false;return Action::Lost;}
            return Action::None;
        }
        if(now-openedAt<WatchMs)return Action::None;
        armed=false;
        if(!known)return Action::None;
        const float dx=x-x0,dy=y-y0;drift=std::sqrt(dx*dx+dy*dy);
        return Action::Report;
    }
};
}
