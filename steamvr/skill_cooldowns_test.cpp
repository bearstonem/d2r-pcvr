// Wrist skill cooldowns (steamvr/skill_cooldowns.h): the stat search and timers as the game keeps them, the board a
// sample makes, the icons' places and sweep, the ticks owed to refused casts, and the setting.
#include <cmath>
#include <cstdio>
#include <vector>
#include "skill_cooldowns.h"
#include "interaction_paint.h"
#include "vr_settings.h"

namespace cd = framevr::cooldowns;
static int g_fail = 0, g_checks = 0;
static void Check(bool ok, const char* what) { ++g_checks; if (!ok) { std::printf("FAIL: %s\n", what); ++g_fail; } }

int main() {
    // ---- the game's numbers: 25 server frames a second (Skills.txt localdelay in frames) ----
    Check(cd::FrameMs * 25 == 1000, "25 frames a second");
    Check(25 * cd::FrameMs == 1000 && 45 * cd::FrameMs == 1800 && 30 * cd::FrameMs == 1200 && 10 * cd::FrameMs == 400,
          "Frozen Orb 1.0 s, Blizzard 1.8 s, Meteor 1.2 s, Fist of the Heavens 0.4 s");
    Check(cd::Packed(cd::OwnDelayStat, 64) == ((359 << 16) | 64) && cd::Packed(359, 401) == ((359 << 16) | 401), "stat 359, the skill as layer");

    // ---- the stat search: sorted 8-byte entries, as 2F99D0 ----
    {
        std::vector<cd::StatEntry> list = {{cd::Packed(0, 0), 5}, {cd::Packed(359, 56), 1300}, {cd::Packed(359, 59), 1345},
                                           {cd::Packed(359, 64), 1225}, {cd::Packed(360, 64), 7}};
        unsigned reads = 0;
        const auto read = [&](uint64_t i, cd::StatEntry& e) { ++reads; if (i >= list.size()) return false; e = list[size_t(i)]; return true; };
        int32_t v = 0;
        Check(cd::FindStat(list.size(), 359, 64, read, v) && v == 1225, "Frozen Orb's layer: its end frame");
        Check(cd::FindStat(list.size(), 359, 56, read, v) && v == 1300, "Meteor's layer");
        Check(cd::FindStat(list.size(), 359, 59, read, v) && v == 1345, "Blizzard's layer");
        Check(!cd::FindStat(list.size(), 359, 121, read, v), "no layer for a skill not recharging");
        Check(!cd::FindStat(list.size(), 361, 64, read, v), "another stat with that layer is not it");
        reads = 0;
        Check(!cd::FindStat(0, 359, 64, read, v) && !cd::FindStat(cd::MaxStats + 1, 359, 64, read, v) && reads == 0, "an empty or unbounded list is not read");
        const auto broken = [&](uint64_t i, cd::StatEntry& e) { if (i == 2) return false; e = list[size_t(i)]; return true; };
        Check(!cd::FindStat(list.size(), 359, 64, broken, v), "an unreadable entry fails the search");
        std::vector<cd::StatEntry> many;
        for (int s = 0; s < 400; ++s) many.push_back({cd::Packed(359, s), 1000 + s});
        const auto readMany = [&](uint64_t i, cd::StatEntry& e) { ++reads; e = many[size_t(i)]; return true; };
        reads = 0;
        Check(cd::FindStat(many.size(), 359, 277, readMany, v) && v == 1277 && reads <= 11, "a binary search, as native");
    }
    // ---- the two timers ----
    Check(cd::SharedEnd(2, 1530.f) == 1530 && cd::SharedEnd(0, 1530.f) == 0 && cd::SharedEnd(2, NAN) == 0 && cd::SharedEnd(2, -1.f) == 0,
          "the shared delay's end: flag 2 and a finite frame");
    Check(cd::ReadyFrame(1000, 1025, false, 0) == 1025, "its own delay");
    Check(cd::ReadyFrame(1000, 1010, true, 1025) == 1025 && cd::ReadyFrame(1000, 1030, true, 1025) == 1030, "the later of its own and the shared delay");
    Check(cd::ReadyFrame(1000, 0, true, 1025) == 1025, "the shared delay alone (Werewolf: every delay skill waits)");
    Check(cd::ReadyFrame(1000, 1025, false, 1040) == 1025, "the shared delay counts only while state 121 is on");
    Check(cd::ReadyFrame(1000, 990, false, 0) == 1000 + cd::UntimedFrames && cd::ReadyFrame(1000, 0, true, 0) == 1005, "blocked with no timer: held one sample");

    // ---- the board ----
    cd::Board b;
    b.known = true; b.at = 50000; b.frame = 1000;
    Check(b.Add(64, 1023) && b.Add(59, 1045) && b.Add(64, 1010) && b.count == 2 && b.list[0].endFrame == 1023, "one skill once, the later end");
    Check(!b.Add(-1, 1100) && b.count == 2, "no skill, no entry");
    Check(b.ReadyAt(b.list[0]) == 50000 + 23 * 40 && b.Left(64, 50000) == 920 && b.Left(64, 50920) == 0 && b.Left(56, 50000) == 0,
          "ready on the server's frame, in ms");
    Check(cd::Cooling(b, 64, 50100) && !cd::Cooling(b, 64, 50920) && !cd::Cooling(b, 56, 50100) && !cd::Cooling(b, -1, 50100), "cooling until ready");
    Check(!cd::Cooling(b, 59, 51001) && b.Fresh(51000) && !b.Fresh(51001), "a stale board never refuses");
    {
        cd::Board unknown = b;unknown.known = false;
        Check(!cd::Cooling(unknown, 64, 50100), "an unknown board never refuses");
        cd::Board later = b;later.at = 50200; later.frame = 1005;
        Check(later.SameSkills(b), "a new sample of the same cooldowns is the same skills (no new revision)");
        later.list[0].endFrame = 1030;
        Check(!later.SameSkills(b), "a new end frame is a change");
        cd::Board full;full.known = true;
        for (int s = 0; s < cd::MaxSampled; ++s) full.Add(300 + s, 2000);
        Check(!full.Add(400, 2000) && full.count == cd::MaxSampled, "bounded");
    }
    // ---- the sweep ----
    Check(cd::LeftSteps(0, 1000) == 0 && cd::LeftSteps(1000, 1000) == cd::Steps && cd::LeftSteps(500, 1000) == cd::Steps / 2 &&
          cd::LeftSteps(1, 1000) == 1 && cd::LeftSteps(5000, 0) == cd::Steps, "the share left, in steps");

    // ---- the icons' places, in the wrist art (512 x 256) ----
    for (int i = 0; i < cd::MaxShown; ++i) {
        const auto p = cd::WristIcon[i];
        const float r = cd::IconRadius;
        Check(p.x - r >= 0 && p.x + r <= 512 && p.y - r >= 0 && p.y + r <= 256, "inside the wrist art");
        for (const float ox : {147.8f, 364.f}) Check(std::hypot(p.x - ox, p.y - 125.f) >= 70 + r, "clear of the orbs and their carved rims");
        Check(p.y + r <= 106, "above the clasp");
        for (int j = i + 1; j < cd::MaxShown; ++j)
            Check(std::hypot(p.x - cd::WristIcon[j].x, p.y - cd::WristIcon[j].y) >= 2 * r + 4, "apart from each other");
    }
    Check(cd::PlaceOrder[0] == 1 && std::fabs(cd::WristIcon[1].x - 256) < .5f, "one cooldown sits in the middle, over the clasp");

    // ---- the tracker: places, sweep, the server running late, the shared delay ----
    {
        const auto all = [](int) { return true; };
        cd::Tracker t;
        cd::Board s;s.known = true;s.at = 10000;s.frame = 500;s.Add(64, 523);   // Frozen Orb, seen 2 frames after the cast
        t.Update(1, s, 10000, all);
        auto icons = t.Shown(10000);
        Check(icons[1].skill == 64 && icons[1].left == cd::Steps && icons[0].skill < 0 && icons[2].skill < 0, "first seen: the middle, full");
        icons = t.Shown(10460);
        Check(icons[1].skill == 64 && icons[1].left == cd::Steps / 2, "half way: half the sweep");
        s.at = 10200;s.frame = 505;s.Add(59, 550);   // Blizzard
        t.Update(1, s, 10200, all);
        icons = t.Shown(10200);
        Check(icons[1].skill == 64 && icons[0].skill == 59, "the next takes the left; the first keeps its place");
        s.Add(56, 535);s.Add(121, 515);s.Add(244, 600);   // Meteor, Fist of the Heavens, Volcano
        t.Update(1, s, 10200, all);
        icons = t.Shown(10200);
        Check(icons[2].skill >= 0 && icons[2].skill != 64 && icons[2].skill != 59, "the third the right");
        int shown = 0;for (const auto& i : icons) shown += i.skill >= 0;
        Check(shown == 3, "three at most");
        // Frozen Orb ready on frame 523: 10200 + 18 * 40 = 10920. Gone at once, before any sample says so.
        icons = t.Shown(10920);
        Check(icons[1].skill != 64, "gone when ready");
        t.Update(1, s, 10920, all);   // the same (old) sample: its place is free for one waiting
        icons = t.Shown(10920);
        int waiting = 0;for (const auto& i : icons) waiting += i.skill >= 0;
        Check(icons[0].skill == 59 && waiting == 3 && icons[1].skill >= 0 && icons[1].skill != 64, "a place freed goes to the next waiting");
        // Cast again: a new end frame after the last one ended is a new cooldown, full again.
        cd::Board again;again.known = true;again.at = 12000;again.frame = 550;again.Add(64, 574);
        t.Update(1, again, 12000, all);
        icons = t.Shown(12000);
        Check(icons[1].skill == 64 && icons[1].left == cd::Steps, "cast again: a new full sweep, the others gone");
        // The shared delay begins while it runs (Werewolf): its end moves later, the sweep keeps its share.
        again.at = 12200;again.frame = 555;again.list[0].endFrame = 594;
        t.Update(1, again, 12200, all);
        icons = t.Shown(12200);
        Check(icons[1].skill == 64 && icons[1].left >= cd::Steps * 3 / 4 && icons[1].left < cd::Steps, "an end moved later lengthens the same cooldown");
        // Stale, unknown or another hero: nothing.
        t.Update(1, again, 13300, all);
        Check(t.Shown(13300)[1].skill < 0, "a stale sample shows nothing");
        t.Update(1, again, 12300, all);
        t.Update(2, again, 12300, all);
        Check(t.Shown(12300)[1].skill == 64 && t.Shown(12300)[1].left == cd::Steps, "another session starts over");
    }
    {   // a server a frame late lists Frozen Orb again with the same end frame after its time: the same cooldown
        const auto all = [](int) { return true; };
        cd::Tracker t;
        cd::Board s;s.known = true;s.at = 20000;s.frame = 700;s.Add(64, 725);   // ready at 21000
        t.Update(1, s, 20000, all);
        Check(t.Shown(20999)[1].skill == 64 && t.Shown(21000)[1].skill < 0, "gone at its time");
        cd::Board late = s;late.at = 21000;late.frame = 724;
        t.Update(1, late, 21000, all);
        Check(t.Shown(21000)[1].skill == 64 && t.Shown(21000)[1].left == 1, "listed again: its last step, not a new sweep");
        Check(t.Shown(21040)[1].skill < 0, "then gone");
        cd::Board done = late;done.at = 21100;done.frame = 726;done.count = 0;
        t.Update(1, done, 21100, all);
        Check(t.Shown(21100)[1].skill < 0, "no longer listed: forgotten");
    }
    {   // only the skills the player casts in VR (a slot, a glyph, a motion)
        cd::Tracker t;
        cd::Board s;s.known = true;s.at = 1000;s.frame = 100;s.Add(64, 125);s.Add(59, 145);
        t.Update(1, s, 1000, [](int skill) { return skill == 59; });
        const auto icons = t.Shown(1000);
        Check(icons[1].skill == 59 && icons[0].skill < 0 && icons[2].skill < 0, "a skill on no button, glyph or motion is left out");
    }
    // ---- ticks owed to refused casts ----
    {
        cd::ReadyTicks ticks;
        cd::Board s;s.known = true;s.at = 1000;s.frame = 100;s.Add(64, 125);   // ready at 2000
        ticks.Refused(64, 1, 1200);
        Check(ticks.Waiting(64) && ticks.Step(s, 1500)[0].skill < 0, "no tick while it recharges");
        auto due = ticks.Step(s, 2000);
        Check(due[0].skill == 64 && due[0].hand == 1 && due[0].since == 1200 && due[1].skill < 0 && !ticks.Waiting(64), "one tick on the drawing hand, once ready");
        Check(ticks.Step(s, 2100)[0].skill < 0, "only once");
        ticks.Refused(64, 1, 1300);ticks.Refused(64, 0, 1400);
        due = ticks.Step(s, 2000);
        Check(due[0].skill == 64 && due[0].hand == 0 && due[1].skill < 0, "refused twice: one tick, the latest hand");
        ticks.Refused(64, 0, 1400);
        cd::Board stale = s;stale.at = 500;
        Check(ticks.Step(stale, 2000)[0].skill < 0 && ticks.Waiting(64), "a stale board holds the tick");
        Check(ticks.Step(stale, 1400 + cd::GiveUpMs)[0].skill < 0 && !ticks.Waiting(64), "given up after 10 s, no tick");
        ticks.Refused(-1, 0, 1);ticks.Refused(64, 2, 1);
        Check(!ticks.Waiting(64) && !ticks.Waiting(-1), "no skill or no hand: nothing owed");
        for (int k = 0; k < 6; ++k) ticks.Refused(300 + k, 0, 100 + uint64_t(k));
        int owed = 0;for (int k = 0; k < 6; ++k) owed += ticks.Waiting(300 + k);
        Check(owed == 4 && !ticks.Waiting(300) && ticks.Waiting(305), "four owed at most, the oldest dropped");
        ticks.Clear();
        Check(!ticks.Waiting(305), "cleared");
    }
    // ---- the wrist repaints as the icons change (interaction_paint.h) ----
    {
        framevr::interaction::Snapshot s;s.session = 5;
        framevr::interaction::Output o;
        const auto before = framevr::interaction::Context(s, o, true);
        o.cooldowns[1] = {64, 12};
        Check(!(framevr::interaction::Context(s, o, true) == before), "a new step of the sweep repaints");
    }
    // ---- the setting: VR Settings > Interface > Wrist > Life and mana > Skill cooldowns ----
    {
        namespace st = framevr::settings;
        const auto& d = st::Definitions[st::SkillCooldowns];
        Check(st::SkillCooldowns > st::RoomSteps && st::SkillCooldowns < st::Count, "appended after the keys before it");
        Check(d.defaultValue == 1 && d.min == 0 && d.max == 1 && d.step == 1 && std::wstring(d.section) == L"interactions" &&
              std::wstring(d.key) == L"skill_cooldowns", "[interactions] skill_cooldowns, On by default");
        Check(st::Normalize(st::SkillCooldowns, .4f) == 0 && st::Normalize(st::SkillCooldowns, 7.f) == 1, "a toggle");
    }
    if (g_fail) { std::printf("Skill cooldowns: %d of %d checks failed\n", g_fail, g_checks); return 1; }
    std::printf("Skill cooldowns: %d checks passed (timers, board, places, sweep, ticks, setting)\n", g_checks);
    return 0;
}
