// Head skip, the game side: the hero's helm left out of the eye views' draw-call collection and kept in the shadow
// passes' (steamvr/head_skip.h has where in the game's code, why the head itself stays, and the cost).
//   HookGather: the render jobs' "Collect draw calls" (0xE0AA50, per model, per eye pass): returns without gathering a
//     model that is the local hero's helm while an eye pass in F4 armed it.
//   HookAppend: every pass's gathers (0xEDB450): counts which passes still took a helm that is left out of the eyes'.
//   EyePass: the render thread, each first-person eye projection (vrcam VrProjInner): arms the skip, the hero's root.
//   Install / Report: the periodic thread, twice a second.
// Reads in the render jobs: the TransformComponent the game hands Gather (its parent at +0xC0, its place at +0x70),
// directly; for a candidate only, once per SeenMs: its parent's TransformComponent (skel::EntityTransform) and its
// model's file (model instance +0x20 resource, +0x08 path; vr/engine_units.cpp ModelState), through guarded reads.
// Log "Frame head skip:": the hooks going in (or not), the INI keys when they change, a helm found or let go, and while
// armed a line a second ([debug] head_skip=1, the default; 0: every 10 s) with the window's counts.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <intrin.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include "../steamvr/head_skip.h"
#include "guarded_text.h"
#include "logging.h"
#include "memory.h"
#include "sigscan.h"
#include "skeletons.h"

namespace {
namespace hk = framevr::headskip;

using GatherFn = void(__fastcall*)(void* capture, uint32_t entity, void* model, void* transform, void* a5, void* a6, void* a7, void* a8);
using AppendFn = uint64_t(__fastcall*)(void* a1, void* a2, void* a3, uint32_t entity, void* a5, void* a6, void* a7, void* a8, void* a9,
                                       void* a10, uint64_t a11);

constexpr uint64_t RootMs = 100;        // the hero's root looked up again at most this often (and when his model is rebuilt)
constexpr int InstallTries = 60;        // once a second in a world, then given up

std::atomic<bool> g_on{true}, g_everySecond{true};
std::atomic<int> g_hook{0}, g_tally{0};   // 0 not in yet, 1 in, -1 given up
GatherFn g_origGather = nullptr;
AppendFn g_origAppend = nullptr;
std::atomic<uintptr_t> g_base{0};

// The hot path's inputs (EyePass writes them).
std::atomic<uint64_t> g_armedAt{0};
std::atomic<uint32_t> g_root{hk::NoEntity};
std::atomic<float> g_eyePos[3] = {};
std::atomic<float> g_r2{0};
std::atomic<int> g_eye{0};
// The helms left out, for HookAppend (copies of g_tracked's ids, NoEntity in a free slot).
std::atomic<uint32_t> g_helm[hk::Slots] = {hk::NoEntity, hk::NoEntity, hk::NoEntity, hk::NoEntity};
std::atomic<int> g_helmCount{0};

// The window's counts (relaxed; exchanged by Report).
std::atomic<uint32_t> w_skipped[2] = {}, w_shadow{0}, w_terrain{0}, w_capture{0}, w_unknown{0}, w_leaked{0};
std::atomic<uint32_t> w_candidates{0}, w_classified{0}, w_heroParts{0}, w_notHero{0};
std::atomic<uint64_t> w_armedSeen{0};   // the last ms an armed pass reached Gather

// The decisions and the tracked helms, under g_lock (shared in the render jobs' lookups, short holds).
SRWLOCK g_lock = SRWLOCK_INIT;
hk::SeenCache<64> g_seen;                 // the hero's ~20 parts and what comes near the eyes
hk::Tracked g_tracked;
char g_file[hk::Slots][96] = {};         // each tracked helm's model file, for the log
struct Named { uint32_t entity = hk::NoEntity; char file[96] = {}; };
Named g_named[8];                         // the files of the last helms found (Classify)
int g_namedNext = 0;
bool g_changed = false;                   // the tracked set changed since the last Report

void Say(const char* text) { framevr::logging::Write(text); }

template <class T> bool Read(uintptr_t at, T* v) { return at > 0x10000 && framevr::ReadMemory(v, reinterpret_cast<const void*>(at), sizeof(T)); }

// A candidate's decision: does it hang on the local hero (its parents up to ChainMax, one of them his root), and is its
// model a head-worn item's. Guarded reads only; outside g_lock (skel::EntityTransform takes the ECS lock).
hk::Seen Classify(uint32_t entity, uint64_t inst, uint32_t parent, uint32_t root, uint64_t now, char* file, size_t fileSize) {
    hk::Seen s;
    s.entity = entity; s.root = root; s.inst = inst; s.at = now;
    file[0] = 0;
    w_classified.fetch_add(1, std::memory_order_relaxed);
    uint32_t chain[hk::ChainMax] = {parent, hk::NoEntity, hk::NoEntity};
    int n = 1;
    while (n < hk::ChainMax && chain[n - 1] != root && !hk::IsNone(chain[n - 1])) {
        uintptr_t comp = 0;
        uint32_t up = hk::NoEntity;
        if (!skel::EntityTransform(chain[n - 1], &comp) || !Read(comp + 0xC0, &up)) break;
        chain[n++] = up;
    }
    s.hero = hk::HangsOnHero(chain, n, root);
    if (!s.hero) { w_notHero.fetch_add(1, std::memory_order_relaxed); return s; }
    w_heroParts.fetch_add(1, std::memory_order_relaxed);
    uintptr_t res = 0, path = 0;
    if (inst > 0x10000 && Read(uintptr_t(inst) + 0x20, &res) && Read(res + 0x08, &path))
        framevr::ReadText(path, file, int(fileSize), [](void* d, const void* src, size_t k) { return framevr::ReadMemory(d, src, k); });
    s.kind = hk::KindOfPath(file);
    s.skip = hk::Skip(s.kind, chain, n, root);
    return s;
}

// A helm left out this pass: tracked for HookAppend and the log.
void Track(uint32_t entity, uint64_t now) {
    AcquireSRWLockExclusive(&g_lock);
    const bool had = g_tracked.Has(entity);
    const int slot = g_tracked.Note(entity, now);
    if (slot >= 0 && !had) {
        const char* file = "?";
        for (const Named& n : g_named) if (n.entity == entity) { file = n.file; break; }
        strncpy_s(g_file[slot], file, _TRUNCATE);
        g_helm[slot].store(entity, std::memory_order_relaxed);
        g_helmCount.store(g_tracked.Count(), std::memory_order_relaxed);
        g_changed = true;
    }
    ReleaseSRWLockExclusive(&g_lock);
}

// True: leave this model out of the eye pass's lists.
bool Leave(uint32_t entity, const uint8_t* model, const uint8_t* transform, uint64_t now) {
    const uint32_t root = g_root.load(std::memory_order_relaxed);
    uint32_t parent = hk::NoEntity;
    float pos[3];
    std::memcpy(&parent, transform + 0xC0, sizeof parent);   // the game's own component, as it handed it over
    std::memcpy(pos, transform + 0x70, sizeof pos);          // the world matrix's translation
    const float eye[3] = {g_eyePos[0].load(std::memory_order_relaxed), g_eyePos[1].load(std::memory_order_relaxed),
                          g_eyePos[2].load(std::memory_order_relaxed)};
    if (!hk::Candidate(parent, root, pos, eye, g_r2.load(std::memory_order_relaxed))) return false;
    w_candidates.fetch_add(1, std::memory_order_relaxed);
    uint64_t inst = 0;
    std::memcpy(&inst, model, sizeof inst);   // ModelComponent +0: its model instance
    hk::Seen s;
    bool found = false;
    AcquireSRWLockShared(&g_lock);
    if (const hk::Seen* f = g_seen.Find(entity, root, inst, now)) { s = *f; found = true; }
    ReleaseSRWLockShared(&g_lock);
    if (!found) {
        char file[96];
        s = Classify(entity, inst, parent, root, now, file, sizeof file);
        AcquireSRWLockExclusive(&g_lock);
        g_seen.Put(s);
        if (s.skip) {
            Named& n = g_named[g_namedNext];
            g_namedNext = (g_namedNext + 1) % int(sizeof g_named / sizeof g_named[0]);
            n.entity = entity;
            strncpy_s(n.file, file, _TRUNCATE);
        }
        ReleaseSRWLockExclusive(&g_lock);
    }
    if (!s.skip) return false;
    Track(entity, now);
    w_skipped[g_eye.load(std::memory_order_relaxed) & 1].fetch_add(1, std::memory_order_relaxed);
    return true;
}

void __fastcall HookGather(void* capture, uint32_t entity, void* model, void* transform, void* a5, void* a6, void* a7, void* a8) {
    const uint64_t armedAt = g_armedAt.load(std::memory_order_relaxed);
    if (armedAt && model && transform) {
        const uint64_t now = GetTickCount64();
        if (hk::Armed(armedAt, now)) {
            // (a store a model would bounce the line between the render jobs: once in 100 ms)
            if (now - w_armedSeen.load(std::memory_order_relaxed) >= 100) w_armedSeen.store(now, std::memory_order_relaxed);
            if (Leave(entity, static_cast<const uint8_t*>(model), static_cast<const uint8_t*>(transform), now)) return;
        }
    }
    g_origGather(capture, entity, model, transform, a5, a6, a7, a8);
}

uint64_t __fastcall HookAppend(void* a1, void* a2, void* a3, uint32_t entity, void* a5, void* a6, void* a7, void* a8, void* a9, void* a10,
                               uint64_t a11) {
    if (g_helmCount.load(std::memory_order_relaxed) > 0) {
        for (const auto& h : g_helm) {
            if (h.load(std::memory_order_relaxed) != entity) continue;
            const uintptr_t base = g_base.load(std::memory_order_relaxed), at = reinterpret_cast<uintptr_t>(_ReturnAddress());
            switch (hk::CallerOf(base && at > base ? at - base : 0)) {
            case hk::Caller::Main: w_leaked.fetch_add(1, std::memory_order_relaxed); break;
            case hk::Caller::Shadow: w_shadow.fetch_add(1, std::memory_order_relaxed); break;
            case hk::Caller::TerrainMask: w_terrain.fetch_add(1, std::memory_order_relaxed); break;
            case hk::Caller::Capture: w_capture.fetch_add(1, std::memory_order_relaxed); break;
            default: w_unknown.fetch_add(1, std::memory_order_relaxed); break;
            }
            break;
        }
    }
    return g_origAppend(a1, a2, a3, entity, a5, a6, a7, a8, a9, a10, a11);
}

// The six calls to Gather are where 3.3.93787 has them: it is the lambda "Collect draw calls" runs per model.
bool CallSitesHold() {
    for (const uint64_t site : hk::rva::GatherCallSites) {
        uint8_t b[5];
        hk::CallBytes(site, hk::rva::Gather, b);
        if (!d2rsig::Check(site, b, sizeof b)) return false;
    }
    return true;
}
bool ShadowSitesHold() {
    for (const uint64_t site : hk::rva::AppendFromShadow) {
        uint8_t b[5];
        hk::CallBytes(site, hk::rva::Append, b);
        if (!d2rsig::Check(site, b, sizeof b)) return false;
    }
    return true;
}
}  // namespace

namespace headskip {
void Configure(bool on, bool everySecond) {
    static int told = -1;
    g_on.store(on, std::memory_order_relaxed);
    g_everySecond.store(everySecond, std::memory_order_relaxed);
    if (!on) g_armedAt.store(0, std::memory_order_relaxed);
    if (const int now = int(on) | int(everySecond) << 1; now != told) {
        told = now;
        char line[256];
        std::snprintf(line, sizeof line, "Frame head skip: [arms] head_skip_draws=%d, [debug] head_skip=%d (%s)", int(on), int(everySecond),
                      on ? "in F4 the hero's helm is left out of the eye views' draws and kept in the shadow; the head keeps the eye's cut"
                         : "the helm is drawn in the eye views; the eye's cut only");
        Say(line);
    }
}

void Install(bool inWorld) {
    if (!g_on.load(std::memory_order_relaxed) || g_hook.load() != 0 || !inWorld) return;
    static ULONGLONG askedAt = 0;
    static int tries = 0;
    const ULONGLONG now = GetTickCount64();
    if (now - askedAt < 1000) return;
    askedAt = now;
    ++tries;
    char line[320];
    if (CallSitesHold() && d2rsig::Hook(hk::rva::Gather, hk::rva::GatherSig, sizeof hk::rva::GatherSig, (void*)&HookGather, (void**)&g_origGather)) {
        if (const uintptr_t at = d2rsig::Addr(hk::rva::Gather)) g_base.store(at - hk::rva::Gather);
        g_hook.store(1);
        Say("Frame head skip: in - the eye passes' draw-call collection (0xE0AA50, called from its six view iterations) leaves the hero's "
            "helm out while F4 keeps the head for its shadow");
        // The tally: which passes still gather the helm (the shadow's). Diagnostic only.
        if (ShadowSitesHold() &&
            d2rsig::Hook(hk::rva::Append, hk::rva::AppendSig, sizeof hk::rva::AppendSig, (void*)&HookAppend, (void**)&g_origAppend)) {
            g_tally.store(1);
            Say("Frame head skip: the passes that still gather a left-out helm are counted (0xEDB450; its six shadow callers as in 3.3.93787)");
        } else {
            g_tally.store(-1);
            Say("Frame head skip: 0xEDB450 or its shadow callers not as in 3.3.93787 - the helm is still left out, the shadow's gathers are not counted");
        }
        return;
    }
    if (tries >= InstallTries) {
        g_hook.store(-1);
        std::snprintf(line, sizeof line,
                      "Frame head skip: NOT in - 0xE0AA50 or its six callers are not as in 3.3.93787 (after %d tries): the helm is drawn, the "
                      "eye's cut as before",
                      tries);
        Say(line);
    }
}

void EyePass(bool keep, int eye, const float eyeWorld[3], float unitsPerMetre) {
    if (!keep || !eyeWorld || !g_on.load(std::memory_order_relaxed) || g_hook.load(std::memory_order_relaxed) != 1) {
        g_armedAt.store(0, std::memory_order_relaxed);
        return;
    }
    const uint64_t now = GetTickCount64();
    static std::atomic<uint64_t> rootAt{0};
    static std::atomic<uint32_t> rootGen{0xFFFFFFFFu};
    const uint32_t gen = skel::HeroGen();
    if (gen != rootGen.load(std::memory_order_relaxed) || now - rootAt.load(std::memory_order_relaxed) >= RootMs) {
        rootAt.store(now, std::memory_order_relaxed);
        rootGen.store(gen, std::memory_order_relaxed);
        skel::HeroXform hx;
        uint32_t root = hk::NoEntity;
        if (skel::HeroTransform(&hx) && hk::IsNone(hx.parent) && !hk::IsNone(hx.entity)) root = hx.entity;
        if (g_root.exchange(root, std::memory_order_relaxed) != root) {
            AcquireSRWLockExclusive(&g_lock);
            g_seen.Clear();
            ReleaseSRWLockExclusive(&g_lock);
        }
    }
    for (int i = 0; i < 3; ++i) g_eyePos[i].store(eyeWorld[i], std::memory_order_relaxed);
    const float r = hk::CandidateRadiusM * unitsPerMetre;
    g_r2.store(std::isfinite(r) && r > 0 ? r * r : 0.f, std::memory_order_relaxed);
    g_eye.store(eye & 1, std::memory_order_relaxed);
    g_armedAt.store(now, std::memory_order_relaxed);
}

void Report() {
    if (g_hook.load(std::memory_order_relaxed) != 1) return;
    const uint64_t now = GetTickCount64();
    // Helms unseen for TrackMs let go; a change is told at once.
    uint32_t helms[hk::Slots];
    const char* files[hk::Slots];
    char fileCopy[hk::Slots][96];
    int count = 0;
    bool changed = false;
    AcquireSRWLockExclusive(&g_lock);
    if (g_tracked.Expire(now)) changed = true;
    for (size_t i = 0; i < hk::Slots; ++i) {
        g_helm[i].store(g_tracked.entity[i], std::memory_order_relaxed);
        if (g_tracked.entity[i] == hk::NoEntity) continue;
        helms[count] = g_tracked.entity[i];
        strncpy_s(fileCopy[count], g_file[i], _TRUNCATE);
        files[count] = fileCopy[count];
        ++count;
    }
    g_helmCount.store(count, std::memory_order_relaxed);
    changed = changed || g_changed;
    g_changed = false;
    ReleaseSRWLockExclusive(&g_lock);
    static uint64_t nextLine = 0;
    const uint64_t armedSeen = w_armedSeen.load(std::memory_order_relaxed);
    const bool armed = armedSeen && now >= armedSeen && now - armedSeen <= 1000;
    static bool wasArmed = false;
    const bool tell = changed || (armed && now >= nextLine) || (wasArmed && !armed);
    wasArmed = armed;
    if (!tell) return;
    nextLine = now + (g_everySecond.load(std::memory_order_relaxed) ? 1000 : 10000);
    hk::Window w;
    w.skipped[0] = w_skipped[0].exchange(0); w.skipped[1] = w_skipped[1].exchange(0);
    w.shadow = w_shadow.exchange(0); w.terrain = w_terrain.exchange(0); w.capture = w_capture.exchange(0);
    w.unknown = w_unknown.exchange(0); w.leaked = w_leaked.exchange(0);
    w.candidates = w_candidates.exchange(0); w.classified = w_classified.exchange(0);
    w.heroParts = w_heroParts.exchange(0); w.notHero = w_notHero.exchange(0);
    char line[768];
    hk::Line(line, sizeof line, w, armed, g_root.load(std::memory_order_relaxed), helms, files, count);
    if (g_tally.load(std::memory_order_relaxed) != 1)
        strncat_s(line, " (the shadow's gathers are not counted: 0xEDB450 not hooked)", _TRUNCATE);
    if (changed) strncat_s(line, " [the helms tracked changed]", _TRUNCATE);
    Say(line);
}
}  // namespace headskip
