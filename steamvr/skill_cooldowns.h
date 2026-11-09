#pragma once
// Wrist skill cooldowns (Vin's idea, Discord ideas channel, 2026-11-05): while a skill recharges, its icon sits dimmed
// between the life and mana orbs on the wrist (up to three), its color sweeping back clockwise until it is ready; then
// it goes. A glyph or motion cast of a skill still recharging is refused with the refusal buzz, not the cast's trail
// and buzz, and a small tick on that hand says when it is ready. VR Settings > Interface > Wrist > Life and mana >
// Skill cooldowns ([interactions] skill_cooldowns), On by default.
//
// The game's own timers (D2R 3.3.93787 code; 3.3.94995 keeps its addresses):
//  - A skill's own delay: Skills.txt localdelay, the compiled row's calc at +0x238 (Frozen Orb 25 frames, Blizzard
//    45, Meteor 30, Fist of the Heavens 10, Volcano 100 ...). After a cast the server's do dispatcher (43B192)
//    evaluates it, and 439500 keeps it as stat 359 with the skill id as its layer, in the stat list of state 185,
//    valued the server frame it ends on (339E60 -> 2F3030). A timer event (43AC00) drops each layer when its
//    frame comes. Each skill has its own, so another skill casts meanwhile.
//  - The shared delay: globaldelay, +0x234 (Werewolf and Werebear, 25 frames). 439470 puts the hero in state 121
//    (skilldelay), its stat list expiring on the frame it ends (339D70 -> 2F7AB0, a float at +0x24, flag 2 at +0x1C).
//  - The use check's delay test, 3404A0 (SKILLS_GetUseState 33F360 answers 8 when it fails): a skill is blocked while
//    its stat 359 layer is set, or while state 121 is on and either of its delays is above 0.
// The server runs 25 frames a second; game+0x170 is its frame. The game thread reads the hero every 200 ms
// (vr/skill_cooldowns.cpp): which of the hero's skills that test blocks, and the frame each is ready on. The client
// is not asked: the server's timers are the ones a cast meets.
//
// Pure (no game, no Windows): steamvr/skill_cooldowns_test.cpp.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace framevr::cooldowns {

constexpr int SharedDelayState = 121;   // states.txt skilldelay
constexpr int OwnDelayState = 185;      // the stat list holding each skill's own delay
constexpr int OwnDelayStat = 359;       // its stat, one layer per skill id
constexpr uint32_t FrameMs = 40;        // the server's 25 frames a second
constexpr int MaxSampled = 16;          // blocked skills kept from one sample
constexpr int MaxShown = 3;             // icons on the wrist
constexpr uint64_t MaxStats = 4096;     // a stat list's entries, bounded
constexpr uint64_t SampleMaxAgeMs = 1000;   // a board older than this is not trusted (sampled every 200 ms)
constexpr int UntimedFrames = 5;        // blocked with no timer read: held one sample (200 ms), then read again

// A stat list's stats (2F99D0): 8-byte entries {stat << 16 | layer, value}, sorted by the first.
inline int32_t Packed(int stat, int layer) { return int32_t((uint32_t(stat) << 16) | uint16_t(layer)); }
struct StatEntry { int32_t id = 0, value = 0; };
// The value of (stat, layer) in `count` entries, by the native's binary search. read(i, entry): entry i, false when
// unreadable. False when absent, unreadable or the count is out of bounds.
template <class Read> bool FindStat(uint64_t count, int stat, int layer, Read read, int32_t& value) {
    if (count == 0 || count > MaxStats) return false;
    const int32_t want = Packed(stat, layer);
    uint64_t lo = 0, hi = count;
    while (lo < hi) {
        const uint64_t mid = lo + (hi - lo) / 2;
        StatEntry e;
        if (!read(mid, e)) return false;
        if (e.id < want) lo = mid + 1; else hi = mid;
    }
    StatEntry e;
    if (lo >= count || !read(lo, e) || e.id != want) return false;
    value = e.value;
    return true;
}
// The shared delay's end frame from its stat list (2F7AB0 keeps it as a float and sets flag 2); 0: none.
inline int SharedEnd(uint32_t flags, float expire) {
    return (flags & 2) && std::isfinite(expire) && expire > 0 && expire < 2.0e9f ? int(expire) : 0;
}
// The frame a blocked skill is ready on: the later of its own delay's end (0: none) and, while state 121 is on, the
// shared delay's (the use check blocks on either). Neither ahead of `frame`: held UntimedFrames, then read again.
inline int ReadyFrame(int frame, int ownEnd, bool sharedOn, int sharedEnd) {
    int end = ownEnd > frame ? ownEnd : 0;
    if (sharedOn && sharedEnd > frame) end = std::max(end, sharedEnd);
    return end > frame ? end : frame + UntimedFrames;
}

struct Cooldown {
    int skill = -1, endFrame = 0;
    bool operator==(const Cooldown&) const = default;
};
// One sample of the hero: the server frame and the tick (ms) it was read at, and the skills the use check blocked.
struct Board {
    bool known = false;
    uint64_t at = 0;
    int frame = 0, count = 0;
    std::array<Cooldown, MaxSampled> list{};
    // The skills and their end frames, not when it was read: the snapshot's revision moves only when these do.
    bool SameSkills(const Board& o) const {
        if (known != o.known || count != o.count) return false;
        for (int i = 0; i < count && i < MaxSampled; ++i) if (!(list[size_t(i)] == o.list[size_t(i)])) return false;
        return true;
    }
    // One skill once (an item's copy of a skill the hero has too: the later end); false when full or invalid.
    bool Add(int skill, int endFrame) {
        if (skill < 0) return false;
        for (int i = 0; i < count; ++i) if (list[size_t(i)].skill == skill) {
            list[size_t(i)].endFrame = std::max(list[size_t(i)].endFrame, endFrame);
            return true;
        }
        if (count >= MaxSampled) return false;
        list[size_t(count++)] = {skill, endFrame};
        return true;
    }
    uint64_t ReadyAt(const Cooldown& c) const { return at + uint64_t(std::max(0, c.endFrame - frame)) * FrameMs; }
    bool Fresh(uint64_t now) const { return known && at && (now >= at ? now - at : at - now) <= SampleMaxAgeMs; }
    // ms until the skill is ready; 0: ready, or not blocked when sampled.
    uint64_t Left(int skill, uint64_t now) const {
        for (int i = 0; i < count && i < MaxSampled; ++i) if (list[size_t(i)].skill == skill) {
            const uint64_t ready = ReadyAt(list[size_t(i)]);
            return ready > now ? ready - now : 0;
        }
        return 0;
    }
};
// Whether a cast of this skill would meet its delay now. An unknown or stale board never refuses.
inline bool Cooling(const Board& b, int skill, uint64_t now) { return skill >= 0 && b.Fresh(now) && b.Left(skill, now) > 0; }

// The wrist's icons, by place: 0 left, 1 middle, 2 right (WristIcon). left: the share of the delay still to run, in
// Steps (1..Steps); 0 with no skill.
constexpr int Steps = 24;
struct Icon {
    int skill = -1;
    uint8_t left = 0;
    bool operator==(const Icon&) const = default;
};
using Icons = std::array<Icon, MaxShown>;
inline uint8_t LeftSteps(uint64_t leftMs, uint64_t lengthMs) {
    if (!leftMs) return 0;
    if (!lengthMs || leftMs >= lengthMs) return uint8_t(Steps);
    return uint8_t(std::clamp<uint64_t>((leftMs * Steps + lengthMs - 1) / lengthMs, 1, Steps));
}
// Where the icons sit, in the wrist art (512 x 256, wrist-vitals.png at its runtime size): above the clasp, in the
// notch between the orbs' rims, the middle lower than the sides as the rims curve. The orbs' glass is centred at
// (147.8, 125) and (364, 125), 57.5 across (interaction_ui.cpp DrainOrb); their carved rims reach about 70.
struct Place { float x, y; };
constexpr Place WristIcon[MaxShown] = {{217, 67}, {256, 87}, {295, 67}};
constexpr float IconRadius = 18;
constexpr int PlaceOrder[MaxShown] = {1, 0, 2};   // a new cooldown takes the middle first, then the left, then the right

// Each cooldown from the first sample that showed it to its ready frame. Its length is what was left when first seen:
// sampled every 200 ms, the sweep may start full up to 5 frames late; it still ends on the game's frame. A cooldown
// keeps its place until it ends, so the icons never trade places. It is forgotten once a fresh sample no longer lists
// it: a server running a little late lists it again with the same end frame, and it is the same cooldown, not a new
// one. reach(skill): whether the player can cast it in VR (a hand slot, a glyph or a motion); others are not shown.
class Tracker {
    struct Seen {
        int skill = -1, endFrame = 0, place = -1;
        uint64_t firstAt = 0, readyAt = 0, length = 0;
    };
    std::array<Seen, MaxSampled> seen{};
    uint64_t session = 0;
public:
    void Reset() { seen = {}; session = 0; }
    template <class Reach> void Update(uint64_t s, const Board& b, uint64_t now, Reach reach) {
        if (s != session) { seen = {}; session = s; }
        if (!b.Fresh(now)) { seen = {}; return; }   // no sample to go by: nothing shown
        for (auto& e : seen) {   // no longer blocked: gone
            if (e.skill < 0) continue;
            bool listed = false;
            for (int i = 0; i < b.count && i < MaxSampled; ++i) listed = listed || b.list[size_t(i)].skill == e.skill;
            if (!listed) e = {};
        }
        for (int i = 0; i < b.count && i < MaxSampled; ++i) {
            const auto& c = b.list[size_t(i)];
            const uint64_t ready = b.ReadyAt(c);
            Seen* match = nullptr;
            for (auto& e : seen) if (e.skill == c.skill) match = &e;
            if (match && (c.endFrame == match->endFrame || match->readyAt > now)) {
                // the same cooldown, read again; its end may move later while it runs (the shared delay began meanwhile)
                if (c.endFrame > match->endFrame) match->length += uint64_t(c.endFrame - match->endFrame) * FrameMs;
                match->endFrame = c.endFrame;
                match->readyAt = ready;
                continue;
            }
            if (ready <= now) { if (match) *match = {}; continue; }
            if (!match) for (auto& e : seen) if (e.skill < 0) { match = &e; break; }
            if (match) *match = {c.skill, c.endFrame, -1, now, ready, ready - now};   // a new cooldown
        }
        for (auto& e : seen) if (e.skill >= 0 && e.readyAt <= now) e.place = -1;   // ready: its place is free
        for (const int place : PlaceOrder) {   // free places go to the reachable cooldowns first seen
            bool taken = false;
            for (const auto& e : seen) taken = taken || (e.skill >= 0 && e.place == place);
            if (taken) continue;
            Seen* next = nullptr;
            for (auto& e : seen)
                if (e.skill >= 0 && e.place < 0 && e.readyAt > now && reach(e.skill) &&
                    (!next || e.firstAt < next->firstAt || (e.firstAt == next->firstAt && e.skill < next->skill))) next = &e;
            if (next) next->place = place;
        }
    }
    Icons Shown(uint64_t now) const {
        Icons icons{};
        for (const auto& e : seen)
            if (e.skill >= 0 && e.place >= 0 && e.place < MaxShown && e.readyAt > now)
                icons[size_t(e.place)] = {e.skill, LeftSteps(e.readyAt - now, e.length)};
        return icons;
    }
};

// A glyph or motion cast refused while its skill recharged: a tick on that hand once it is ready (once per refusal,
// the latest hand for a skill). A board that is not fresh holds the wait; GiveUpMs after the refusal it is dropped.
constexpr uint64_t GiveUpMs = 10000;
class ReadyTicks {
    struct Wait { int skill = -1, hand = -1; uint64_t since = 0; };
    std::array<Wait, 4> waits{};
public:
    void Clear() { waits = {}; }
    void Refused(int skill, int hand, uint64_t now) {
        if (skill < 0 || hand < 0 || hand > 1) return;
        Wait* slot = nullptr;
        for (auto& w : waits) if (w.skill == skill) slot = &w;
        for (auto& w : waits) if (!slot && w.skill < 0) slot = &w;
        if (!slot) {   // full: the oldest goes
            slot = &waits[0];
            for (auto& w : waits) if (w.since < slot->since) slot = &w;
        }
        *slot = {skill, hand, now};
    }
    // The skills ready now, with the hand each ticks: up to four, skill -1 past the last.
    std::array<Wait, 4> Step(const Board& b, uint64_t now) {
        std::array<Wait, 4> due{};
        size_t n = 0;
        for (auto& w : waits) {
            if (w.skill < 0) continue;
            if (now >= w.since + GiveUpMs) { w = {}; continue; }
            if (!b.Fresh(now) || Cooling(b, w.skill, now)) continue;
            due[n++] = w;
            w = {};
        }
        return due;
    }
    bool Waiting(int skill) const {
        for (const auto& w : waits) if (w.skill == skill && skill >= 0) return true;
        return false;
    }
};

}   // namespace framevr::cooldowns
