// Head skip (steamvr/head_skip.h): which models are a head-worn item, which candidates are the local hero's, the
// call-site bytes and callers, the armed window, the decision cache, the tracked helms and the log line.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include "head_skip.h"

namespace hk = framevr::headskip;
static int g_fail = 0;
static void Check(bool ok, const char* what) { if (!ok) { std::printf("FAIL: %s\n", what); ++g_fail; } }

int main() {
    using hk::Kind;
    {   // head-worn items by file, in both forms the game names them
        const char* worn[] = {"data/hd/items/armor/pelt/wolf_head/wolf_head_lod0.model", "data/hd/items/dropped_items/wolf_head.model",
                              "data/hd/items/dropped_items/wolf_head_lod3.model", "DATA\\HD\\ITEMS\\ARMOR\\HELMET\\HELM\\HELM_LOD0.MODEL",
                              "data/hd/items/armor/circlet/diadem/diadem.model", "data/hd/items/dropped_items/cap_hat_lod0.model",
                              "data/hd/items/armor/helmet/unique_warlock_helm/unique_warlock_helm_lod1.model",
                              "data:data/hd/items/dropped_items/spirit_mask.model"};
        for (const char* p : worn) Check(hk::KindOfPath(p) == Kind::HeadWorn, p);
        const char* other[] = {nullptr, "", "data/hd/character/player/druid/head_lod0.model", "data/hd/character/player/druid/head.model",
                               "data/hd/items/weapon/bow/short_bow/short_bow_lod0.model", "data/hd/items/dropped_items/ancient_armor_lod0.model",
                               "data/hd/items/dropped_items/wolf_head_lodx.model", "data/hd/items/dropped_items/wolf_heads.model",
                               "data/hd/items/dropped_items/sub/wolf_head.model", "data/hd/items/dropped_items/wolf_head.model.bak",
                               "data/hd/items/armor/voodoo_head/zombie_head/zombie_head_lod0.model",
                               "data/hd/items/armor/shield/buckler/buckler_lod0.model",
                               "data/hd/env/model/global/prop/act3/jungle/act3_jungle_props/wolf_head01_lod0.model",
                               "data/hd/items/misc/quiver/arrows/arrows_lod0.model"};
        for (const char* p : other) Check(hk::KindOfPath(p) == Kind::Other, p ? p : "(null)");
        char longPath[400];
        std::memset(longPath, 'a', sizeof longPath - 1);
        longPath[sizeof longPath - 1] = 0;
        std::memcpy(longPath + 300, "/items/armor/helmet/", 20);
        Check(hk::KindOfPath(longPath) == Kind::Other, "a path longer than any model's is not read as one");
        for (const char* name : hk::DroppedHeadWorn) {
            char p[128];
            std::snprintf(p, sizeof p, "data/hd/items/dropped_items/%s_lod0.model", name);
            Check(hk::KindOfPath(p) == Kind::HeadWorn, "every dropped-items helm and pelt by its own name");
        }
    }
    {   // the call bytes and who called Append
        uint8_t b[5];
        hk::CallBytes(0xDF0A15, hk::rva::Gather, b);
        // 0xE0AA50 - (0xDF0A15 + 5) = 0x1A036
        const uint8_t want[5] = {0xE8, 0x36, 0xA0, 0x01, 0x00};
        Check(std::memcmp(b, want, 5) == 0, "call rel32 forward");
        hk::CallBytes(0xE0AB52, 0xEDB450, b);
        const uint8_t app[5] = {0xE8, 0xF9, 0x08, 0x0D, 0x00};   // 0xEDB450 - 0xE0AB57 = 0xD08F9
        Check(std::memcmp(b, app, 5) == 0, "call rel32 to Append");
        hk::CallBytes(0x2000, 0x1000, b);
        const uint8_t back[5] = {0xE8, 0xFB, 0xEF, 0xFF, 0xFF};   // -0x1005
        Check(std::memcmp(b, back, 5) == 0, "call rel32 backward");
        Check(sizeof hk::rva::GatherCallSites / sizeof hk::rva::GatherCallSites[0] == 6, "six calls to Gather");
        for (uint64_t s : hk::rva::AppendFromGather) Check(hk::CallerOf(s + 5) == hk::Caller::Main, "Gather's calls: main");
        for (uint64_t s : hk::rva::AppendFromShadow) Check(hk::CallerOf(s + 5) == hk::Caller::Shadow, "the shadow gathers");
        Check(hk::CallerOf(hk::rva::AppendFromTerrainMask + 5) == hk::Caller::TerrainMask, "terrain mask");
        Check(hk::CallerOf(hk::rva::AppendFromCapture + 5) == hk::Caller::Capture, "capture views");
        Check(hk::CallerOf(hk::rva::AppendFromShadow[0]) == hk::Caller::Unknown, "the call itself is not its return");
        Check(hk::CallerOf(0) == hk::Caller::Unknown, "anything else: unknown");
    }
    {   // the hot path's first test, and the decision
        const uint32_t root = 0x00100023, helmRoot = 0x00200045, other = 0x00300067, none = 0xFFFFFu;
        const float eye[3] = {10, 5, 10}, near[3] = {10.2f, 5.3f, 10.1f}, far[3] = {10, 0, 10};
        const float upm = 2.4f, r2 = hk::CandidateRadiusM * upm * hk::CandidateRadiusM * upm;
        Check(hk::Candidate(root, root, far, eye, r2), "the hero's own part (his root's child) wherever it stands");
        Check(hk::Candidate(helmRoot, root, near, eye, r2), "something near the eyes under another parent: a candidate");
        Check(!hk::Candidate(other, root, far, eye, r2), "far from the eyes and not the hero's part: drawn at once");
        Check(!hk::Candidate(none, root, near, eye, r2), "a unit's root (no parent): drawn");
        Check(!hk::Candidate(0xFFFFFFFFu, root, near, eye, r2), "no parent read");
        Check(!hk::Candidate(root, hk::NoEntity, far, eye, r2), "no hero known: nothing is a candidate");
        const float nan = std::numeric_limits<float>::quiet_NaN(), bad[3] = {nan, 5, 10};
        Check(!hk::Candidate(helmRoot, root, bad, eye, r2), "a place that is not a number is not near");
        const uint32_t helm[] = {helmRoot, root}, onRoot[] = {root}, merc[] = {helmRoot, other, none}, ground[] = {other, none},
                       onHead[] = {helmRoot, 0x00400089, root}, deep[] = {helmRoot, 0x00400089, 0x005000AB, root}, noParent[] = {none, root};
        Check(hk::Skip(Kind::HeadWorn, helm, 2, root), "the hero's helm: its parent's parent is his root");
        Check(hk::Skip(Kind::HeadWorn, onRoot, 1, root), "a helm hung on the hero's root itself");
        Check(hk::Skip(Kind::HeadWorn, onHead, 3, root), "a helm hung on one of his parts (one level more)");
        Check(!hk::Skip(Kind::HeadWorn, deep, 4, root), "not deeper than ChainMax");
        Check(!hk::Skip(Kind::HeadWorn, helm, 1, root), "only the levels read count");
        Check(!hk::Skip(Kind::HeadWorn, merc, 3, root), "a mercenary's or another player's helm: drawn");
        Check(!hk::Skip(Kind::HeadWorn, ground, 2, root), "a helm on the ground: drawn");
        Check(!hk::Skip(Kind::Other, helm, 2, root), "the hero's bow at the cheek: drawn");
        Check(!hk::Skip(Kind::Other, onRoot, 1, root), "the hero's head and body parts: drawn");
        Check(!hk::Skip(Kind::HeadWorn, noParent, 2, root), "a unit's root ends the chain");
        Check(!hk::Skip(Kind::HeadWorn, helm, 2, hk::NoEntity), "no hero: nothing left out");
        Check(!hk::Skip(Kind::HeadWorn, nullptr, 2, root), "no chain");
    }
    {   // armed for ArmMs after an eye pass
        Check(!hk::Armed(0, 1000), "never armed");
        Check(hk::Armed(1000, 1000) && hk::Armed(1000, 1000 + hk::ArmMs), "armed within ArmMs");
        Check(!hk::Armed(1000, 1001 + hk::ArmMs), "not after");
        Check(hk::Armed(1010, 1000), "a stamp a little ahead (another thread read the clock later): armed");
    }
    {   // the cache
        hk::SeenCache<4> c;
        Check(!c.Find(7, 1, 0x100, 1000), "empty");
        hk::Seen s;
        s.entity = 7; s.root = 1; s.inst = 0x100; s.at = 1000; s.kind = Kind::HeadWorn; s.hero = true; s.skip = true;
        c.Put(s);
        const hk::Seen* f = c.Find(7, 1, 0x100, 1500);
        Check(f && f->skip && f->hero, "found with its decision");
        Check(!c.Find(7, 2, 0x100, 1500), "another hero root: read again");
        Check(!c.Find(7, 1, 0x200, 1500), "another model instance: read again");
        Check(!c.Find(7, 1, 0x100, 1001 + hk::SeenMs), "stale after SeenMs");
        Check(!c.Find(7, 1, 0x100, 999), "a stamp from the future does not count");
        for (uint32_t e = 10; e < 14; ++e) { s.entity = e; s.at = 1000 + e; s.skip = false; c.Put(s); }
        Check(!c.Find(7, 1, 0x100, 1500), "the oldest replaced when full");
        Check(c.Find(13, 1, 0x100, 1500) && c.Find(10, 1, 0x100, 1500), "the newer kept");
        s.entity = 10; s.at = 1200; s.skip = true; c.Put(s);
        const hk::Seen* again = c.Find(10, 1, 0x100, 1500);
        Check(again && again->skip && c.Find(11, 1, 0x100, 1500), "the same entity updated in place, nothing else lost");
        c.Clear();
        Check(!c.Find(10, 1, 0x100, 1500), "cleared");
    }
    {   // tracked helms
        hk::Tracked t;
        Check(t.Count() == 0 && !t.Has(5) && !t.Has(hk::NoEntity), "none at first");
        Check(t.Note(5, 1000) == 0 && t.Note(6, 1000) == 1 && t.Note(5, 1100) == 0, "noted, again in its own slot");
        Check(t.Has(5) && t.Has(6) && t.Count() == 2, "both tracked");
        Check(t.Note(7, 1100) == 2 && t.Note(8, 1100) == 3 && t.Note(9, 1100) == -1, "four slots, the fifth refused while all are fresh");
        Check(!t.Expire(1100 + hk::TrackMs - 100) && t.Count() == 4, "nothing stale yet");
        Check(t.Note(9, 1000 + hk::TrackMs + 1) == 1, "a stale slot (6 unseen since 1000) taken by a new helm");
        Check(t.Expire(1100 + hk::TrackMs + 1) && t.Count() == 1 && t.Has(9), "the unseen let go");
    }
    {   // the log line
        hk::Window w;
        w.skipped[0] = 90; w.skipped[1] = 89; w.shadow = 180; w.candidates = 400; w.classified = 3; w.heroParts = 2; w.notHero = 1;
        const uint32_t helms[] = {0x00200046};
        const char* files[] = {"data/hd/items/dropped_items/wolf_head.model"};
        char line[640];
        const int n = hk::Line(line, sizeof line, w, true, 0x00100023, helms, files, 1);
        Check(n > 0 && size_t(n) < sizeof line, "the line fits");
        Check(std::strstr(line, "left 90, right 89") && std::strstr(line, "180 shadow gathers") && std::strstr(line, "200046 data/hd/items"),
              "counts per eye, the shadow's, the tracked helm");
        Check(std::strstr(line, "hero root 100023") != nullptr, "the hero's root");
        hk::Line(line, sizeof line, hk::Window{}, false, hk::NoEntity, nullptr, nullptr, 0);
        Check(std::strstr(line, "not armed") && std::strstr(line, "not known") && std::strstr(line, "helms tracked 0: none"), "nothing yet");
    }
    if (g_fail) { std::printf("%d head skip checks failed\n", g_fail); return 1; }
    std::printf("head skip: all checks passed\n");
    return 0;
}
