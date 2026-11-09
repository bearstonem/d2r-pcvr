#pragma once
// Head skip: in full-body first person (F4) the hero's helm is left out of the eye views' draws and kept in the shadow
// pass, so a long helm (a Druid's Wolf Head: "the teeth hover in front of your face", #429) is not seen from inside while
// the hero's shadow keeps it. The head itself stays drawn and is cut by the eye's near clip as before (head_shadow.h).
//
// Where (D2R 3.3.93787, read from the game's code; vr/head_skip.cpp hooks it):
//   The renderer collects each view's draw calls on the CPU before any are recorded. "RenderTask: Collect draw calls"
//   (0xE0FD60) walks the models (entt views over ModelComponent, TransformComponent and four more) and calls one
//   per-model lambda, Gather (0xE0AA50), from six places (GatherCallSites) and from nowhere else. Gather picks the
//   primary, secondary or tertiary list (and the outline list) and hands the model to Append (0xEDB450,
//   "GatherModelDrawCalls"), which pushes one 256-byte draw call per mesh. The depth and forward passes draw those lists.
//   The shadow passes collect their own: ShadowRenderPasses::CollectDirectionalDrawCalls (0xF49760) and the omni and
//   spot collectors call Append from their own lambdas (AppendFromShadow), never through Gather; so do the capture views
//   (CaptureRenderer::Render) and the terrain mask. The two eyes are two whole passes of the game (vrcam
//   OrigDrawGameScreen), each with its own collection, its own shadow maps and its own lists. A model left out of
//   Gather is therefore missing from both eye views' depth, colour and outline draws and from nothing else.
//   Gather(capture, entity, ModelComponent* (0x88), TransformComponent* (0xD0), 4 more): the entity id in edx, the
//   ModelComponent in r8 (its model instance at +0), the TransformComponent in r9 (world matrix at +0x40, its
//   translation at +0x70, the parent entity at +0xC0); the same at all six call sites.
//
// Who is the hero's helm: a helm (helmet, circlet, Druid pelt) is its own unit, placed and attached to the hero: its
// root entity (a dropped_items_helmet / _pelt skeleton) is a child of the hero's root entity - the one whose
// SkeletonComponent holds the hero's SkeletonInstance (skel::HeroTransform), as a held crossbow's is (vr/skeletons.cpp
// CaptureCrossbow checks exactly that) - and its model entity a child of that root. So a draw is the local hero's helm
// when its model entity's parent, or its parent's parent, is the local hero's root, and its model file is a head-worn
// item's (KindOfPath). A mercenary's helm, another player's, the same helm dropped on the ground or shown in a capture
// view hang off other roots or are not gathered here: they are drawn.
//
// The head: the hero's head model (data/hd/character/player/<class>/head) is NOT left out. For seven classes of eight it
// carries the body's skin as well: one mesh with the face, the neck, the bare chest and arms, and for the Paladin and
// the Sorceress the right hand's fingers (Paladin body2Shape binds R_hand_bind_jnt and the fingers; Sorceress
// bodyCropped1 R_Wrist and R_finger_*; Barbarian, Necromancer, Druid, Assassin and Warlock their arms or chest; only the
// Amazon's head model is the head alone). No draw holds the face without the arms, so the eye's cut stays.
//
// Cost: Gather runs once per model per eye pass (a few thousand). Off, the hook is one load. Armed, a load of the parent
// id, a compare, three float loads and a distance (NearEye) decide for almost every model; only the hero's own parts
// and the few models whose place is within reach of the eyes (CandidateRadiusM) go on to a small cache, under a shared
// lock; a model not yet in it is read once (its parent's parent, its model file) and kept for SeenMs. The shadow
// tally hook on Append compares the entity with the tracked helms (four loads). No GPU work, no ReShade callback.
//
// This header is pure (no game, no Windows): steamvr/head_skip_test.cpp. The game side is vr/head_skip.cpp.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace framevr::headskip {

constexpr uint32_t NoEntity = 0xFFFFFFFFu;
// entt: the low 20 bits are the entity's index, the high 12 its version; an index of all ones is no entity.
inline bool IsNone(uint32_t e) { return (e & 0xFFFFFu) == 0xFFFFFu; }

// The game's code this works on, as 3.3.93787 (and 93847, 94995 with D2RLoader 1.3.1) has it.
namespace rva {
constexpr uint64_t Gather = 0xE0AA50;   // the main view's per-model draw-call gather
constexpr uint8_t GatherSig[] = {0x40, 0x53, 0x55, 0x56, 0x57, 0x41, 0x56, 0x48, 0x81, 0xEC, 0x40, 0x02, 0x00, 0x00};
constexpr uint64_t Append = 0xEDB450;   // GatherModelDrawCalls: a model's draw calls into a list, for every gather
constexpr uint8_t AppendSig[] = {0x44, 0x89, 0x4C, 0x24, 0x20, 0x48, 0x89, 0x54, 0x24, 0x10, 0x55, 0x53, 0x56, 0x41, 0x54, 0x41, 0x56};
// Every call to Gather: the six entt view iterations of "Collect draw calls" (0xDEFEA0, 0xDF0290, 0xDF0680, 0xDF0CF0,
// 0xDF10E0, 0xDF1750). Each must still call Gather before it is hooked.
constexpr uint64_t GatherCallSites[] = {0xDF0235, 0xDF0625, 0xDF0A15, 0xDF1085, 0xDF1475, 0xDF1AEA};
// The calls to Append, by who makes them (the call instruction; it returns 5 bytes on).
constexpr uint64_t AppendFromGather[] = {0xE0AB52, 0xE0ABB2};   // the main list, the outline list
constexpr uint64_t AppendFromShadow[] = {0xF4159D, 0xF4171F, 0xF41794, 0xF418ED, 0xF41A09, 0xF41B54};   // shadow_pass.cpp
constexpr uint64_t AppendFromTerrainMask = 0xDB0421;   // "Collect terrain mask draw calls"
constexpr uint64_t AppendFromCapture = 0xF86A2C;       // CaptureRenderer::Render
}  // namespace rva

// The 5 bytes of `call target` at `site` (E8, rel32): what each call site must hold before the hook goes in.
inline void CallBytes(uint64_t site, uint64_t target, uint8_t out[5]) {
    const int64_t rel = int64_t(target) - int64_t(site + 5);
    const uint32_t r = uint32_t(int32_t(rel));
    out[0] = 0xE8;
    for (int i = 0; i < 4; ++i) out[1 + i] = uint8_t(r >> (8 * i));
}

// Who called Append, from the return address's RVA.
enum class Caller : uint8_t { Main, Shadow, TerrainMask, Capture, Unknown };
inline Caller CallerOf(uint64_t returnRva) {
    for (uint64_t s : rva::AppendFromGather) if (returnRva == s + 5) return Caller::Main;
    for (uint64_t s : rva::AppendFromShadow) if (returnRva == s + 5) return Caller::Shadow;
    if (returnRva == rva::AppendFromTerrainMask + 5) return Caller::TerrainMask;
    if (returnRva == rva::AppendFromCapture + 5) return Caller::Capture;
    return Caller::Unknown;
}

// What a model is, by its file. HeadWorn: a helmet, circlet or Druid pelt, as the files hold them
// (data/hd/items/armor/helmet|circlet|pelt/<name>/<name>_lodN.model), or as the item definitions name the six helmets
// and five pelts whose model lives with the dropped items (data/hd/items/dropped_items/<name>.model or _lodN).
enum class Kind : uint8_t { Other, HeadWorn };
constexpr const char* DroppedHeadWorn[] = {"cap_hat",     "crown_of_thieves", "fanged_helm", "jawbone_cap",
                                           "rockstopper", "war_bonnet",       "antlers",     "falcon_mask",
                                           "hawk_helm",   "spirit_mask",      "wolf_head"};
inline Kind KindOfPath(const char* path) {
    if (!path) return Kind::Other;
    char p[260];
    size_t n = 0;
    for (; path[n] && n + 1 < sizeof p; ++n) {
        const char c = path[n];
        p[n] = c == '\\' ? '/' : (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c;
    }
    if (path[n]) return Kind::Other;   // longer than any model path
    p[n] = 0;
    if (std::strstr(p, "/items/armor/helmet/") || std::strstr(p, "/items/armor/circlet/") || std::strstr(p, "/items/armor/pelt/"))
        return Kind::HeadWorn;
    const char* d = std::strstr(p, "/items/dropped_items/");
    if (!d) return Kind::Other;
    const char* name = d + std::strlen("/items/dropped_items/");
    if (std::strchr(name, '/')) return Kind::Other;
    const char* dot = std::strstr(name, ".model");
    if (!dot || dot[6]) return Kind::Other;
    size_t len = size_t(dot - name);
    // "_lod<digits>" off the end
    if (const char* lod = std::strstr(name, "_lod"); lod && lod < dot) {
        const char* q = lod + 4;
        while (q < dot && *q >= '0' && *q <= '9') ++q;
        if (q == dot && q > lod + 4) len = size_t(lod - name);
    }
    for (const char* known : DroppedHeadWorn)
        if (std::strlen(known) == len && std::strncmp(known, name, len) == 0) return Kind::HeadWorn;
    return Kind::Other;
}

// The hot path's test: may this model be the hero's (his own part, parent == root) or something hung on him near the
// eyes (a helm: within r of the eye, its parent's parent checked after)? r2: the radius squared, world units.
constexpr float CandidateRadiusM = 0.6f;   // a helm's root sits at the head bone, 10-25 cm from the eyes
inline bool NearEye(const float p[3], const float eye[3], float r2) {
    const float dx = p[0] - eye[0], dy = p[1] - eye[1], dz = p[2] - eye[2];
    const float d2 = dx * dx + dy * dy + dz * dz;
    return d2 <= r2;   // false for NaN
}
inline bool Candidate(uint32_t parent, uint32_t root, const float pos[3], const float eye[3], float r2) {
    if (IsNone(root) || IsNone(parent)) return false;   // no hero, or a unit's own root (the hero's too: no model)
    return parent == root || NearEye(pos, eye, r2);
}
// Does a model hang on the local hero: chain is its parent, that one's parent and so on (n of them, read until a unit's
// root); one of the first ChainMax is his root. A helm's model: its parent the helm's root, whose parent is the hero's
// root; one level more is allowed should the game hang it on his head part instead.
constexpr int ChainMax = 3;
inline bool HangsOnHero(const uint32_t* chain, int n, uint32_t root) {
    if (!chain || IsNone(root)) return false;
    for (int i = 0; i < n && i < ChainMax; ++i) {
        if (IsNone(chain[i])) return false;
        if (chain[i] == root) return true;
    }
    return false;
}
// The decision for a candidate: left out when it hangs on the local hero and its file is a head-worn item's.
inline bool Skip(Kind kind, const uint32_t* chain, int n, uint32_t root) { return kind == Kind::HeadWorn && HangsOnHero(chain, n, root); }

// The armed state: an eye pass in F4 with the head kept for its shadow stamps it; the hook acts for ArmMs after.
constexpr uint64_t ArmMs = 250;
inline bool Armed(uint64_t at, uint64_t now) { return at != 0 && (now <= at || now - at <= ArmMs); }

// The candidates' decisions, by entity: a small cache, the oldest replaced. An entry holds while the hero's root and
// the model instance are the same and for SeenMs (a helm taken off and another put on gets new entities anyway).
constexpr uint64_t SeenMs = 2000;
struct Seen {
    uint32_t entity = NoEntity, root = NoEntity;
    uint64_t inst = 0, at = 0;
    Kind kind = Kind::Other;
    bool hero = false;   // hangs on the local hero (HangsOnHero)
    bool skip = false;
};
template <size_t N>
struct SeenCache {
    Seen e[N];
    const Seen* Find(uint32_t entity, uint32_t root, uint64_t inst, uint64_t now) const {
        for (const Seen& s : e)
            if (s.entity == entity && s.root == root && s.inst == inst && s.at && now >= s.at && now - s.at <= SeenMs) return &s;
        return nullptr;
    }
    void Put(const Seen& s) {
        Seen* slot = &e[0];
        for (Seen& x : e) {
            if (x.entity == s.entity) { slot = &x; break; }
            if (x.at < slot->at) slot = &x;
        }
        *slot = s;
    }
    void Clear() { for (Seen& x : e) x = Seen{}; }
};

// The helms left out, for the shadow tally and the log: up to Slots entities, each until it has gone unseen for
// TrackMs. The game side keeps the ids in atomics the Append hook reads; this is their bookkeeping.
constexpr size_t Slots = 4;
constexpr uint64_t TrackMs = 1000;
struct Tracked {
    uint32_t entity[Slots] = {NoEntity, NoEntity, NoEntity, NoEntity};
    uint64_t at[Slots] = {};
    // Returns the slot it is in (-1: all full of fresher ones).
    int Note(uint32_t e, uint64_t now) {
        int freeSlot = -1;
        for (size_t i = 0; i < Slots; ++i) {
            if (entity[i] == e) { at[i] = now; return int(i); }
            if (freeSlot < 0 && (entity[i] == NoEntity || now < at[i] || now - at[i] > TrackMs)) freeSlot = int(i);
        }
        if (freeSlot >= 0) { entity[freeSlot] = e; at[freeSlot] = now; }
        return freeSlot;
    }
    // Lets go of what has gone unseen; true when any went.
    bool Expire(uint64_t now) {
        bool any = false;
        for (size_t i = 0; i < Slots; ++i)
            if (entity[i] != NoEntity && (now < at[i] || now - at[i] > TrackMs)) { entity[i] = NoEntity; at[i] = 0; any = true; }
        return any;
    }
    bool Has(uint32_t e) const {
        if (e == NoEntity) return false;
        for (uint32_t x : entity) if (x == e) return true;
        return false;
    }
    int Count() const { int n = 0; for (uint32_t x : entity) n += x != NoEntity; return n; }
};

// The once-a-second line's counts. skipped: helm gathers left out of each eye's main lists (each a model: one draw per
// mesh). shadow / terrain / capture / unknown: Append took a tracked helm for that pass (the shadow kept it).
// leaked: a tracked helm reached Append from Gather (not left out: the hook was off for that pass).
struct Window {
    uint32_t skipped[2] = {}, shadow = 0, terrain = 0, capture = 0, unknown = 0, leaked = 0;
    uint32_t candidates = 0, classified = 0, heroParts = 0, notHero = 0;
};
// The line. armed: an eye pass armed the hook within the window; root: the hero's root entity (NoEntity: not known);
// helms: the tracked entities and their files (count of them).
inline int Line(char* out, size_t size, const Window& w, bool armed, uint32_t root, const uint32_t* helms, const char* const* files,
                int count) {
    char tracked[200] = "none";
    size_t at = 0;
    for (int i = 0; i < count && at + 1 < sizeof tracked; ++i) {
        const int k = std::snprintf(tracked + at, sizeof tracked - at, "%s%X %s", i ? ", " : "", helms[i], files && files[i] ? files[i] : "?");
        if (k < 0) break;
        at += size_t(k);
    }
    char rootText[16];
    if (IsNone(root)) std::snprintf(rootText, sizeof rootText, "not known");
    else std::snprintf(rootText, sizeof rootText, "%X", root);
    return std::snprintf(out, size,
                         "Frame head skip: 1 s: %s; hero root %s; helm left out of the eye views: left %u, right %u model gathers; "
                         "kept for the shadow: %u shadow gathers (terrain mask %u, capture %u, other %u); not left out %u; "
                         "helms tracked %d: %s; candidates %u (read %u: hero parts %u, not the hero's %u)",
                         armed ? "armed (F4, head kept for its shadow)" : "not armed", rootText, w.skipped[0], w.skipped[1], w.shadow,
                         w.terrain, w.capture, w.unknown, w.leaked, count, tracked, w.candidates, w.classified, w.heroParts, w.notHero);
}

}  // namespace framevr::headskip

// The game side (vr/head_skip.cpp). Not used by the pure test.
namespace headskip {
// [arms] head_skip_draws (default 1): the hooks go in and the helm is left out; 0: nothing changes. [debug] head_skip
// (default 1): the window line every second while armed; 0: every 10 s.
void Configure(bool on, bool everySecond);
// The periodic thread, twice a second: the hooks (in a world, retried for a minute), then the log.
void Install(bool inWorld);
void Report();
// VrProjInner, each first-person eye projection. keep: the head is kept for its shadow this pass (F4 full body,
// head_shadow, hide_head, no shapeshift form - as head_shadow's EyeNear `on`). eye: 0 left, 1 right. eyeWorld: the
// head centre in the world (vrcam g_eyeWorld); unitsPerMetre: world units per metre of the player.
void EyePass(bool keep, int eye, const float eyeWorld[3], float unitsPerMetre);
}  // namespace headskip
