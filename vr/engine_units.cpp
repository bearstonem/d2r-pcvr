// Adapted from natpoh/diablo2-vr-port (MIT, Copyright (c) 2026 natpoh) at 91cb1e8; see THIRD_PARTY.md.
// vr/engine/engitem.cpp there: the engine units' calls, letting go and hooks, and the ceiling's frame (its
// slab and rim wall; the belt, the held items, the domes, the beams and the vaults left out).
//
// Units the game draws as its own (2026-10-13 there): a unit's look is a UnitDefinition (a .json naming its
// model and textures). The game puts one in the renderer's ECS as UnitTranslationLayer::ProcessUnitCreation
// does for every unit: EntityContainerDefinition::GetOrLoadDefinition<UnitDefinition> (the FileRequest's path
// from blz::string(const char*), +0xB8 = 100, +0xC0 = 1, +0x108 = 1 as the game sets them; RequestMode 0 =
// there on return; UNIT_TYPE 4 = item), then UnitDefinition::PlaceInstance into the renderer's registry, with
// no UNIT behind it. The registry may be changed only on the thread that owns it (+0xA0, a thread id) while
// +0x110 is set - render jobs read it the rest of the time. So ours runs in the setter that shuts it
// (0x912C90(registry, false)): the game's own units are made by then, the frame's world matrices not yet
// worked out. Every RVA and its first bytes: vr/engine_units_signatures.h.
#include "engine_units.h"
#include "engine_units_signatures.h"
#include "engine_held_units.h"
#include "prop_scan.h"
#include "skeletons.h"
#include "../shared/game_ceiling.h"
#include "../cleanroom/sigscan/sigscan.h"

#include <windows.h>

#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace engineunits {
namespace {

LogFn g_log = nullptr;
CeilingFn g_ceilingFn = nullptr;
std::wstring g_modDir;   // the mod's .mpq folder ("" none)

void Log(const char* text) { if (g_log) g_log(text); }
void LogF(const char* fmt, ...) {
    char b[1024];
    va_list a;
    va_start(a, fmt);
    vsnprintf(b, sizeof b, fmt, a);
    va_end(a);
    Log(b);
}
#pragma optimize("", off)
bool SafeRead(void* d, const void* s, size_t n) noexcept { __try { memcpy(d, s, n); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; } }
bool SafeWrite(void* d, const void* s, size_t n) noexcept { __try { memcpy(d, s, n); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; } }
#pragma optimize("", on)

struct Shared { void* p; void* ctrl; };   // blz::shared_ptr
using StringCtorFn = void*(__fastcall*)(void* str, const char* s);
using LoadFn = Shared*(__fastcall*)(Shared* out, void* request, int mode, int unitType);
// a member function: this, then the result's place, then the arguments
using PlaceFn = Shared*(__fastcall*)(void* def, Shared* out, void* registry, void* part, void* filter, const float* pos, const float* rot,
                                     const float* v3, bool b);
using UpdateWorldFn = void*(__fastcall*)(void* out, uint32_t entity, void* comp);
using SetMutableFn = void(__fastcall*)(void* registry, bool allowed);
using TeardownFn = void(__fastcall*)(void*, void*, void*, void*);   // (it reads no argument; whatever is in the registers is passed on)

SetMutableFn g_origSetMutable = nullptr;
TeardownFn g_origTeardown = nullptr;
std::atomic<int> g_hook{0};       // the set-mutable hook: 0 not in yet, 1 in, -1 given up
std::atomic<int> g_teardown{0};   // the teardown hook, as g_hook
std::atomic<int> g_checked{0};    // the game's called functions: 0 not checked yet, 1 where 3.3.93787 has them, -1 not
std::atomic<bool> g_world{false};

bool ReadRegistry(uintptr_t reg, DWORD* owner, uint8_t* open) {
    __try { *owner = *(const DWORD*)(reg + 0xA0); *open = *(const uint8_t*)(reg + 0x110); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// ---- the game's calls; no C++ objects in these (SEH) ----
bool LoadDef(const char* path, Shared* def) {
    const auto ctor = (StringCtorFn)d2rsig::Addr(StringCtorRva);
    const auto load = (LoadFn)d2rsig::Addr(LoadUnitDefRva);
    *def = {};
    if (!ctor || !load) return false;
    __try {
        alignas(16) uint8_t req[0x200] = {};
        ctor(req, path);
        req[0xB8] = 100;
        *(uint64_t*)(req + 0xC0) = 1;
        *(uint64_t*)(req + 0x108) = 1;
        load(def, req, 0, 4);   // the request is the game's now (a by-value argument: it frees it)
        return def->p != nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *def = {};
        return false;
    }
}
bool PlaceAt(void* def, const float pos[3], Shared* inst) {
    const auto place = (PlaceFn)d2rsig::Addr(PlaceInstanceRva);
    void* reg = (void*)d2rsig::Addr(RegistryRva);
    *inst = {};
    if (!place || !reg) return false;
    __try {
        alignas(16) uint8_t part[0x80] = {}, filter[0x100] = {};
        *(uint32_t*)(filter + 4) = 0x811C9DC5u;   // as the game hands it over (an empty hash)
        alignas(16) const float rot[4] = {0.0f, 0.0f, 0.0f, 1.0f}, none[4] = {};
        alignas(16) float at[4] = {pos[0], pos[1], pos[2], 0.0f};
        place(def, inst, reg, part, filter, at, rot, none, true);
        return inst->p != nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *inst = {};
        return false;
    }
}
// A UnitInstance's entities: their ids at +0x10, how many at +0x18. The root is the one without a parent.
bool Entities(void* inst, uintptr_t* list, uint64_t* n) {
    return inst && SafeRead(list, (const uint8_t*)inst + 0x10, 8) && SafeRead(n, (const uint8_t*)inst + 0x18, 8) && *list && *n <= 64;
}
uint32_t RootOf(void* inst) {
    uintptr_t list = 0;
    uint64_t n = 0;
    if (!Entities(inst, &list, &n) || !n) return 0xFFFFFFFFu;
    for (uint64_t i = 0; i < n; ++i) {
        uint32_t e = 0, parent = 0;
        uintptr_t comp = 0;
        if (!SafeRead(&e, (const void*)(list + i * 4), 4) || !skel::EntityTransform(e, &comp)) continue;
        if (SafeRead(&parent, (const void*)(comp + 0xC0), 4) && (parent & 0xFFFFF) == 0xFFFFF) return e;
    }
    return 0xFFFFFFFFu;
}
// Its local matrix (+0x00): what the game sets for a unit before the frame's poses. The world (+0x40) does NOT
// follow by itself (a placed unit with nothing behind it is not in the renderer's update), so the game's own
// update follows each write: 0xE3A8C0(out, entity, TransformComponent*) - what PlaceInstance itself calls after
// placing - works out the world from the local and the parent's world, keeps the old world at +0x80 (DLSS's
// motion vectors) and tells the entity's attachments. Our bow hooks it: no props are written from these calls.
bool CallUpdateWorld(UpdateWorldFn update, uint32_t e, uintptr_t comp) {
    __try {
        alignas(16) float out[16];
        update(out, e, (void*)comp);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
bool UpdateWorld(uint32_t e) {
    uintptr_t comp = 0;
    const auto update = (UpdateWorldFn)d2rsig::Addr(UpdateWorldRva);
    if (e == 0xFFFFFFFFu || !update || !skel::EntityTransform(e, &comp)) return false;
    propscan::OwnTransforms own;
    return CallUpdateWorld(update, e, comp);
}
bool WriteLocal(uint32_t e, const float m[16]) {
    uintptr_t comp = 0;
    return e != 0xFFFFFFFFu && skel::EntityTransform(e, &comp) && SafeWrite((void*)comp, m, 64);
}
// The world laid over the previous one too (+0x40 to +0x80): the renderer takes no motion from this frame's
// move (a slab snapped to another tile, or put away and shown again).
void Still(uint32_t e) {
    uintptr_t comp = 0;
    float world[16];
    if (e != 0xFFFFFFFFu && skel::EntityTransform(e, &comp) && SafeRead(world, (const void*)(comp + 0x40), sizeof world))
        SafeWrite((void*)(comp + 0x80), world, sizeof world);
}
// The local laid, then every entity of the instance brought up to date - the root first, its children after it
// (their world hangs off the root's). `still`: no motion from this frame's move.
bool Lay(void* inst, uint32_t root, const float m[16], bool still) {
    if (!WriteLocal(root, m)) return false;
    UpdateWorld(root);
    if (still) Still(root);
    uintptr_t list = 0;
    uint64_t n = 0;
    if (!Entities(inst, &list, &n)) return true;
    for (uint64_t i = 0; i < n; ++i) {
        uint32_t e = 0;
        if (SafeRead(&e, (const void*)(list + i * 4), 4) && e != root) {
            UpdateWorld(e);
            if (still) Still(e);
        }
    }
    return true;
}

// ---- letting go, as the game does (vr/engine_held_units.h) ----
heldunits::Life g_life;          // from the world's teardown until a menu: nothing placed (Work)
heldunits::Tally g_areaTally;    // the instances let go during a game as an area change took their entities (told when the game is left)
bool ReleaseRef(Shared* s) {
    void* const c = s->ctrl;
    *s = {};
    if (!c) return false;
    __try { return heldunits::ReleaseCtrl(c); } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
// An instance's entity list down to the entities still its registry's (+8): the game's removal frees each one's
// slot, and one an area change took may hold another entity by now. False: not readable (left as it is).
bool TrimToLive(void* inst, bool* stale) {
    *stale = false;
    __try {
        const uintptr_t reg = *(const uintptr_t*)((const uint8_t*)inst + 8);
        uint32_t* const list = *(uint32_t**)((uint8_t*)inst + 0x10);
        uint64_t* const n = (uint64_t*)((uint8_t*)inst + 0x18);
        if (!reg || (!list && *n) || *n > 64) return false;
        const uint32_t* const ents = *(const uint32_t* const*)(reg + 0x30);
        const uint32_t* const end = *(const uint32_t* const*)(reg + 0x38);
        const size_t count = ents && end > ents ? (size_t)(end - ents) : 0;
        const size_t k = heldunits::KeepLive(list, (size_t)*n, [ents, count](uint32_t e) { return heldunits::Live(ents, count, e); });
        *stale = k != *n;
        *n = k;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
// An instance let go: only on the registry's owner thread (the game's removal asserts elsewhere) - held as
// before when not.
bool OnOwnerThread() {
    DWORD owner = 0;
    uint8_t open = 0;
    const uintptr_t reg = d2rsig::Addr(RegistryRva);
    return reg && ReadRegistry(reg, &owner, &open) && owner == GetCurrentThreadId();
}
void ReleaseInstance(Shared* inst, heldunits::Tally* t) {
    if (!inst->ctrl) { *inst = {}; return; }
    if (!OnOwnerThread()) { ++t->kept; return; }
    bool stale = false;
    if (inst->p && !TrimToLive(inst->p, &stale)) { ++t->kept; return; }   // (not readable: not handed to the game's removal)
    if (ReleaseRef(inst)) { ++t->instances; if (stale) ++t->stale; }
}
void ReleaseDefinition(Shared* def, heldunits::Tally* t) {
    if (ReleaseRef(def)) ++t->definitions;
}

// ---- the model the game made for a placed unit ----
// Its entities' ModelInstance (3.3.93787): the resource's file and whether it loaded (+0xC8 == 4), its meshes
// (+0x310 / +0x318; the first's triangles at Mesh +0x158), the visible list's count (+0x2A8), the layer mask
// (+0x00, bit 31 the whole model hidden), its place (+0x290). For the log.
std::string ModelState(void* placed) {
    uintptr_t list = 0;
    uint64_t n = 0;
    if (!Entities(placed, &list, &n)) return "the instance's entities not read";
    std::string s;
    for (uint64_t i = 0; i < n; ++i) {
        uint32_t e = 0;
        if (!SafeRead(&e, (const void*)(list + i * 4), 4)) continue;
        const uintptr_t inst = skel::EntityModelInstance(e);
        if (!inst) continue;
        uintptr_t res = 0, pathAt = 0, meshes = 0;
        uint8_t state = 0;
        uint32_t layers = 0, tri = 0;
        uint64_t nMesh = 0, nVis = 0;
        float at[3] = {};
        char path[97] = "?";
        if (SafeRead(&res, (const void*)(inst + 0x20), 8) && res > 0x10000) {
            SafeRead(&state, (const void*)(res + 0xC8), 1);
            if (SafeRead(&pathAt, (const void*)(res + 0x08), 8) && pathAt > 0x10000 && SafeRead(path, (const void*)pathAt, 96)) path[96] = 0;
            else strcpy_s(path, "?");
        }
        SafeRead(&layers, (const void*)inst, 4);
        SafeRead(&meshes, (const void*)(inst + 0x310), 8);
        SafeRead(&nMesh, (const void*)(inst + 0x318), 8);
        SafeRead(&nVis, (const void*)(inst + 0x2A8), 8);
        SafeRead(at, (const void*)(inst + 0x290), sizeof at);
        if (meshes > 0x10000 && nMesh && nMesh < 64) {
            uintptr_t mi = 0, mesh = 0;
            if (SafeRead(&mi, (const void*)meshes, 8) && mi > 0x10000 && SafeRead(&mesh, (const void*)mi, 8) && mesh > 0x10000)
                SafeRead(&tri, (const void*)(mesh + 0x158), 4);
        }
        char b[320];
        snprintf(b, sizeof b, "%sentity %X: model %s - %s (state %u), %llu mesh(es) (the first %u triangles), %llu visible, layers %08X%s, at %.1f %.1f %.1f",
                 s.empty() ? "" : "; ", e, path, state == 4 ? "loaded" : "NOT loaded", state, (unsigned long long)nMesh, tri,
                 (unsigned long long)nVis, layers, (layers & 0x80000000u) ? " (hidden)" : "", at[0], at[1], at[2]);
        s += b;
    }
    return s.empty() ? "no entity of it has a model instance (no ModelComponent made: the model not taken)" : s;
}
// Any entity of the placed instance has a model instance (its ModelComponent made)
bool HasModel(void* placed) {
    uintptr_t list = 0;
    uint64_t n = 0;
    if (!Entities(placed, &list, &n)) return false;
    for (uint64_t i = 0; i < n; ++i) {
        uint32_t e = 0;
        if (SafeRead(&e, (const void*)(list + i * 4), 4) && skel::EntityModelInstance(e)) return true;
    }
    return false;
}

// ---- the mod's files ----
std::wstring Under(const std::wstring& dir, const std::string& rel) {
    std::wstring p = dir + L"\\";
    for (const char c : rel) p += c == '/' ? L'\\' : (wchar_t)(unsigned char)c;
    return p;
}
// A file of the mod's data as text (at most 1 MiB): <mod>.mpq\<rel>, the game reads it from there
std::string ModText(const std::wstring& path) {
    std::string text;
    if (FILE* f = _wfopen(path.c_str(), L"rb")) {
        char buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof buf, f)) > 0 && text.size() < (1u << 20)) text.append(buf, n);
        fclose(f);
    }
    return text;
}
bool IsFile(const std::wstring& path) {
    const DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}
// The installer's receipt beside the .mpq folder (steamvr/prepare_game_ceiling.py), read once a game ("" none)
std::string g_receipt;
bool g_receiptRead = false;
const std::string& Receipt() {
    if (!g_receiptRead) {
        g_receiptRead = true;
        const size_t cut = g_modDir.find_last_of(L"\\/");
        if (cut != std::wstring::npos) g_receipt = ModText(g_modDir.substr(0, cut) + L"\\pcvr-game-ceiling.json");
    }
    return g_receipt;
}
// Why a piece's files are not there for the game ("" when they are): its unit (one root, a model) and its five
// LOD files in the mod, each named by the installer's receipt when there is one (a file the installer did not
// make is not ours to give the game). `unitText` gets the unit's file.
std::string FilesProblem(const std::string& unit, const char* model, std::string* unitText) {
    if (g_modDir.empty()) return "no mod folder (the game runs without -mod)";
    const std::wstring unitPath = Under(g_modDir, unit);
    *unitText = ModText(unitPath);
    if (const char* bad = gameceiling::UnitFileProblem(*unitText)) return unit + ": " + bad;
    const std::string& receipt = Receipt();
    if (!receipt.empty() && !gameceiling::ReceiptNames(receipt, unit)) return unit + " is not on the installer's receipt (pcvr-game-ceiling.json)";
    for (int lod = 0; lod < gameceiling::kLods; ++lod) {
        const std::string file = gameceiling::LodPath(model, lod);
        if (!IsFile(Under(g_modDir, file))) return file + " is not there";
        if (!receipt.empty() && !gameceiling::ReceiptNames(receipt, file)) return file + " is not on the installer's receipt";
    }
    return std::string();
}

// ---- [ceiling] game_ceiling: the dungeon ceiling as the game's own lit rock ----
// The unit the installer writes (the slab's model at one of the game's own test models' paths, the area's own
// tiling terrain as its maps, lightMask 19 as the caves' own pieces), loaded and placed, then laid every frame
// with its top over the area's floor and its middle on the whole tile nearest the hero - the rock stays put in
// the world as he walks. The game draws it lit by its torches and the hero's light, fogged by its fog, fire and
// spells in their own order. VR F4 where a ceiling is wanted; anywhere else laid under the ground at a
// thousandth of its size. While it shows the shader draws no ceiling (gameceiling::Drawer).
// [ceiling] game_ceiling_wall: the slab's rim bent down - a ring of the same rock laid with it, its middle at the
// hero, from the slab's top down to [ceiling] floor_depth under the area's floor (caves only; off by default).
struct Piece {
    std::string what, unit;
    const char* model = nullptr;     // gameceiling::Kind's slab or wall, as named there
    Shared def{}, inst{};
    uint32_t root = 0xFFFFFFFFu;
    bool loadTried = false, parked = true, laidOnce = false, wall = false;
    int tries = 0;
    ULONGLONG loadedAt = 0, placedAt = 0, modelAt = 0;
    float laid[16] = {};
    std::string told = "?";
};
Piece g_slabs[gameceiling::kKindCount], g_walls[gameceiling::kKindCount];
std::vector<Shared> g_slabOld;   // placings given up on (no model made): laid away, kept - the game's
std::atomic<bool> g_givenUp[gameceiling::kKindCount];
std::atomic<bool> g_shown{false};

void NamePieces() {
    static bool named = false;
    if (named) return;
    named = true;
    for (int k = 0; k < gameceiling::kKindCount; ++k) {
        const gameceiling::Kind& kind = gameceiling::kKinds[k];
        g_slabs[k].what = std::string(kind.name) + " ceiling";
        g_slabs[k].unit = gameceiling::UnitPath(k, false);
        g_slabs[k].model = kind.slab;
        g_walls[k].what = std::string(kind.name) + " wall";
        g_walls[k].unit = kind.wall ? gameceiling::UnitPath(k, true) : std::string();
        g_walls[k].model = kind.wall;
        g_walls[k].wall = true;
    }
}

// A piece's frame: its definition loaded once a game, placed (again after an area change, or after a placing that
// made no model), laid at `m` when wanted, at `away` when not. True: shown this frame.
bool PieceFrame(Piece& p, bool want, const char* why, const float hero[3], const float m[16], const float away[16]) {
    if (want && !p.loadTried) {   // the definition, once a game - and only files the installer made that hold what the game needs
        p.loadTried = true;
        std::string text;
        const std::string bad = p.model ? FilesProblem(p.unit, p.model, &text) : std::string("no model for it");
        if (!bad.empty()) {
            LogF("vrcam: game ceiling - the %s NOT given to the game: %s (the installer makes its files); the shader's ceiling in this room",
                 p.what.c_str(), bad.c_str());
        } else if (LoadDef(p.unit.c_str(), &p.def) && p.def.p) {
            p.loadedAt = GetTickCount64();
            LogF("vrcam: game ceiling - the %s's definition loaded: %s (one unit root; the model %s - the game's lit rock model with this kind's own "
                 "terrain maps, lightMask 19, shadowMask 0)", p.what.c_str(), p.unit.c_str(), gameceiling::ModelPath(p.model).c_str());
        } else {
            p.def = {};
            LogF("vrcam: game ceiling - the %s's definition did NOT load (%s): the game refused it; the shader's ceiling in this room", p.what.c_str(),
                 p.unit.c_str());
        }
    }
    if (!p.def.p) return false;
    const ULONGLONG nowMs = GetTickCount64();
    // a placing whose entities got no model in 3 s (its file still loading): laid away and placed again, 4 times at most
    if (want && p.inst.p && !HasModel(p.inst.p) && nowMs - p.placedAt > 3000) {
        if (p.tries < 4) {
            Lay(p.inst.p, p.root, away, true);
            g_slabOld.push_back(p.inst);
            ++p.tries;
            LogF("vrcam: game ceiling - the %s's placing %d made no model in 3 s: laid away, placed again (its definition asked %.1f s ago)", p.what.c_str(),
                 p.tries, (nowMs - p.loadedAt) / 1000.0);
            p.inst = {};
            p.root = 0xFFFFFFFFu;
            p.laidOnce = false;
        } else if (p.tries == 4) {
            ++p.tries;
            LogF("vrcam: game ceiling - the %s made no model in 5 placings over 15 s: the game does not take its model file - placed no more; the shader's "
                 "ceiling in this room", p.what.c_str());
        }
    }
    if (want && !p.inst.p && p.tries <= 4 && nowMs - p.loadedAt < 2000) return false;   // (its files loading)
    if (want && !p.inst.p && p.tries <= 4) {   // placed (again after an area change took its entity, or after a placing with no model)
        if (PlaceAt(p.def.p, hero, &p.inst)) {
            p.root = RootOf(p.inst.p);
            p.parked = false;
            p.placedAt = nowMs;
            p.laidOnce = false;
            LogF("vrcam: game ceiling - the %s placed (instance %p, root entity %X) at the hero %.1f %.1f %.1f", p.what.c_str(), p.inst.p, p.root, hero[0],
                 hero[1], hero[2]);
        } else {
            LogF("vrcam: game ceiling - PlaceInstance gave nothing for the %s - not asked again this game; the shader's ceiling in this room", p.what.c_str());
            p.tries = 5;
            return false;
        }
    }
    if (!p.inst.p) return false;
    if (p.root == 0xFFFFFFFFu) p.root = RootOf(p.inst.p);
    const float* at = want ? m : away;
    const bool still = !p.laidOnce || gameceiling::Jumped(p.laid, at, p.wall);
    if (!Lay(p.inst.p, p.root, at, still)) {   // its entity is gone (an area change): let go, placed again next frame
        LogF("vrcam: game ceiling - the %s's entity is gone (an area change): let go, placed again", p.what.c_str());
        ReleaseInstance(&p.inst, &g_areaTally);
        p.inst = {};
        p.root = 0xFFFFFFFFu;
        p.laidOnce = false;
        return false;
    }
    std::memcpy(p.laid, at, sizeof p.laid);
    p.laidOnce = true;
    p.parked = !want;
    if (std::string(why) != p.told) {
        p.told = why;
        if (want) LogF("vrcam: game ceiling - the %s shown at %.1f %.1f %.1f, scale %.2f round / %.2f down; its model: %s", p.what.c_str(), m[12], m[13], m[14],
                       m[0], m[5], ModelState(p.inst.p).c_str());
        else LogF("vrcam: game ceiling - the %s put away (under the ground, small): %s", p.what.c_str(), why);
    }
    if (want && nowMs - p.modelAt >= 10000) {   // its model as the game has it, every 10 s while shown (it may load after the placing)
        p.modelAt = nowMs;
        LogF("vrcam: game ceiling - the %s's model, 10 s: %s", p.what.c_str(), ModelState(p.inst.p).c_str());
    }
    return want;
}

void CeilingFrame() {
    NamePieces();
    CeilingIn c;
    if (g_ceilingFn) g_ceilingFn(&c);
    if (!c.ticked) {   // never wanted (or every piece put away): nothing to do
        bool parked = true;
        for (int k = 0; k < gameceiling::kKindCount; ++k) parked = parked && g_slabs[k].parked && g_walls[k].parked;
        if (parked) { g_shown.store(false); return; }
    }
    const int kind = c.kind >= 0 && c.kind < gameceiling::kKindCount ? c.kind : -1;
    gameceiling::In in;
    in.ticked = c.ticked;
    in.f4 = c.f4;
    in.wanted = c.wanted && kind >= 0;
    in.heroOk = c.heroOk;
    const char* why = gameceiling::WhyNot(in);
    const bool want = !*why;
    const bool wall = want && c.wallTick && gameceiling::kKinds[kind].wall;   // (rooms have walls of their own)
    static float toldHeight = -1.0f, toldTile = -1.0f, toldReach = -1.0f;
    static int toldKind = -2;
    if (want && (c.height != toldHeight || c.tileWorld != toldTile || (wall ? c.reach : -1.0f) != toldReach || kind != toldKind)) {
        toldHeight = c.height;
        toldTile = c.tileWorld;
        toldReach = wall ? c.reach : -1.0f;
        toldKind = kind;
        LogF("vrcam: game ceiling - the %s ceiling (%s): its top %.1f units over the area's floor (%s), %s; %.0f units a tile (4 x [ceiling] scale), "
             "%.0f units across", gameceiling::kKinds[kind].name, gameceiling::kKinds[kind].maps, c.height, c.heightFrom,
             gameceiling::kKinds[kind].relief > 0.0f ? "its rock hanging lower" : "flat", c.tileWorld, 2.0f * gameceiling::kTilesHalf * c.tileWorld);
        if (wall)
            LogF("vrcam: game ceiling - its rim bent down: the wall %.0f units round the hero, %.1f units down from the ceiling's top",
                 gameceiling::kWallRadius * c.tileWorld / gameceiling::kTile, c.reach);
    }
    float m[16], w[16], away[16];
    gameceiling::Matrix(c.hero, c.floorY, c.height, c.tileWorld, want, m);
    gameceiling::WallMatrix(c.hero, c.floorY, c.height, c.tileWorld, c.reach, wall, w);
    gameceiling::Matrix(c.hero, 0.0f, 0.0f, 1.0f, false, away);
    bool shown = false;
    for (int k = 0; k < gameceiling::kKindCount; ++k) {
        const bool mine = want && k == kind;
        const char* whyK = mine || !want ? why : "another kind's area";
        shown = PieceFrame(g_slabs[k], mine, whyK, c.hero, m, away) || shown;
        g_givenUp[k].store(gameceiling::GivenUp(g_slabs[k].loadTried, g_slabs[k].def.p != nullptr, g_slabs[k].tries));
        if (gameceiling::kKinds[k].wall)
            PieceFrame(g_walls[k], mine && wall, mine && !c.wallTick ? "[ceiling] game_ceiling_wall=0" : whyK, c.hero, w, away);
    }
    g_shown.store(shown);
}

// ---- the game left: every unit of ours let go before the game's own teardown (vr/engine_held_units.h) ----
// From the world's teardown (the game's thread that owns the registry): every instance first - its entities
// still the registry's, the game's own removal takes them out - then every definition; everything placed is
// forgotten, and the next game's first world loads and places it all afresh (g_life: nothing placed between).
void ResetPiece(Piece& p) {
    p.def = {};
    p.inst = {};
    p.root = 0xFFFFFFFFu;
    p.loadTried = false;
    p.parked = true;
    p.laidOnce = false;
    p.tries = 0;
    p.loadedAt = p.placedAt = p.modelAt = 0;
    p.told = "?";
}
heldunits::Tally LetGoAll() {
    heldunits::Tally t;
    for (Shared& s : g_slabOld) ReleaseInstance(&s, &t);
    for (Piece& p : g_slabs) ReleaseInstance(&p.inst, &t);
    for (Piece& p : g_walls) ReleaseInstance(&p.inst, &t);
    if (t.kept) return t;   // (not the owner thread: nothing forgotten - the definitions stay with the instances that need them)
    for (Piece& p : g_slabs) ReleaseDefinition(&p.def, &t);
    for (Piece& p : g_walls) ReleaseDefinition(&p.def, &t);
    g_slabOld.clear();
    for (Piece& p : g_slabs) ResetPiece(p);
    for (Piece& p : g_walls) ResetPiece(p);
    for (std::atomic<bool>& g : g_givenUp) g.store(false);
    g_receiptRead = false;   // (the next game reads the installer's receipt again)
    g_shown.store(false);
    return t;
}
// The world's teardown (0xD72610: the game's systems shut down, then the registry cleared, then its assets
// unloaded - where a reference still held is "Assets have been leaked"): ours let go at its entry, while our
// entities are still the registry's. Every leave of a game runs it (and the quit from inside one).
void __fastcall HookTeardown(void* a, void* b, void* c, void* d) {
    if (!OnOwnerThread()) {   // (not the thread our units are placed on: nothing touched here - Work may be running; held as before)
        Log("vrcam: game left (the world's teardown) on another thread than the registry's owner: our units held");
        g_origTeardown(a, b, c, d);
        return;
    }
    g_life.Teardown();
    const heldunits::Tally area = g_areaTally;
    g_areaTally = {};
    const heldunits::Tally t = LetGoAll();
    char also[160] = "";
    if (area.instances || area.kept) snprintf(also, sizeof also, "; during the game %d more let go as area changes took their entities", area.instances);
    Log((heldunits::Line("game left (the world's teardown)", t) + also).c_str());
    g_origTeardown(a, b, c, d);
}

// The game's functions this calls are where 3.3.93787 has them (each asked, so the line says which). The bow's
// hook may be on 0xE3A8C0: d2rsig::Check vouches for a function we hooked with the bytes the hook proved.
bool CheckFunctions() {
    const bool ctor = d2rsig::Check(StringCtorRva, StringCtorSig, sizeof StringCtorSig);
    const bool load = d2rsig::Check(LoadUnitDefRva, LoadUnitDefSig, sizeof LoadUnitDefSig);
    const bool place = d2rsig::Check(PlaceInstanceRva, PlaceInstanceSig, sizeof PlaceInstanceSig);
    const bool update = d2rsig::Check(UpdateWorldRva, UpdateWorldSig, sizeof UpdateWorldSig);
    const bool registry = d2rsig::Addr(RegistryRva) != 0, model = d2rsig::Addr(TypeSeqModelRva) != 0;
    const bool ok = ctor && load && place && update && registry && model;
    static bool told = false;
    if (ok || !told) {
        told = true;
        LogF("vrcam: engine units - the game's functions %s: string ctor 0x%llX %s, load 0x%llX %s, place 0x%llX %s, world update 0x%llX %s, registry %s, "
             "type_seq<ModelComponent> %s", ok ? "where 3.3.93787 has them" : "NOT all where 3.3.93787 has them (asked again a while)",
             (unsigned long long)StringCtorRva, ctor ? "ok" : "NOT", (unsigned long long)LoadUnitDefRva, load ? "ok" : "NOT",
             (unsigned long long)PlaceInstanceRva, place ? "ok" : "NOT", (unsigned long long)UpdateWorldRva, update ? "ok" : "NOT",
             registry ? "found" : "NOT found", model ? "found" : "NOT found");
    }
    return ok;
}

void Work() {
    if (!g_life.MayPlace(true)) return;   // (between the world's teardown and the menu: nothing placed into a registry the game is clearing)
    if (g_checked.load() == 0) {
        static int tries = 0;
        static ULONGLONG askedAt = 0;
        const ULONGLONG now = GetTickCount64();
        if (now - askedAt < 1000) return;
        askedAt = now;
        if (CheckFunctions()) g_checked.store(1);
        else if (++tries >= 20) {
            g_checked.store(-1);
            Log("vrcam: engine units - the game's functions are not where 3.3.93787 has them: the game draws no ceiling this run (the shader's stays)");
        }
        if (g_checked.load() <= 0) return;
    }
    if (g_checked.load() < 0 || g_teardown.load() != 1) return;   // (no let-go when a game is left: nothing placed - the game would find it leaked)
    CeilingFrame();
}

void __fastcall HookSetMutable(void* registry, bool allowed) {
    if (!allowed && g_world.load() && (uintptr_t)registry == d2rsig::Addr(RegistryRva)) {
        DWORD owner = 0;
        uint8_t open = 0;
        // still open, and ours to change: the game's own thread
        if (ReadRegistry((uintptr_t)registry, &owner, &open) && open && owner == GetCurrentThreadId()) Work();
    }
    g_origSetMutable(registry, allowed);
}

}  // namespace

void Init(LogFn log, CeilingFn ceiling, const wchar_t* modDirectory) {
    g_log = log;
    g_ceilingFn = ceiling;
    g_modDir = modDirectory ? modDirectory : L"";
    while (!g_modDir.empty() && (g_modDir.back() == L'\\' || g_modDir.back() == L'/')) g_modDir.pop_back();
}

void Install(bool wanted) {
    if (!wanted || (g_hook.load() != 0 && g_teardown.load() != 0)) return;
    // tried once a second while in a world (the game's code there is running by then); given up after a minute
    static ULONGLONG askedAt = 0;
    static int tries = 0;
    const ULONGLONG now = GetTickCount64();
    if (!g_world.load() || now - askedAt < 1000) return;
    askedAt = now;
    ++tries;
    if (g_teardown.load() == 0) {
        if (d2rsig::Hook(TeardownRva, TeardownSig, sizeof TeardownSig, (void*)&HookTeardown, (void**)&g_origTeardown)) {
            g_teardown.store(1);
            Log("vrcam: engine units - the game's units of ours let go when a game is left (the world's teardown, 0xD72610)");
        } else if (tries >= 60) {
            g_teardown.store(-1);
            Log("vrcam: engine units - the world's teardown (0xD72610) not as in 3.3.93787: nothing placed (the game would find it leaked when a game "
                "is left) - the shader's ceiling stays");
        }
    }
    if (g_hook.load() == 0) {
        if (d2rsig::Hook(SetMutableRva, SetMutableSig, sizeof SetMutableSig, (void*)&HookSetMutable, (void**)&g_origSetMutable)) {
            g_hook.store(1);
            Log("vrcam: engine units in - the game's own models placed while its registry may be changed (0x912C90)");
        } else if (tries >= 60) {
            g_hook.store(-1);
            Log("vrcam: engine units hook FAILED (0x912C90 not as in 3.3.93787) - the shader's ceiling stays");
        }
    }
}

void WorldSeen(bool inWorld) {
    g_world.store(inWorld);
    g_life.World(inWorld);
}

bool Ready() { return g_hook.load() == 1 && g_teardown.load() == 1 && g_checked.load() == 1; }
bool GivenUp(int kind) { return kind < 0 || kind >= gameceiling::kKindCount || g_givenUp[kind].load(std::memory_order_relaxed); }
bool CeilingShown() { return g_shown.load(std::memory_order_relaxed); }

}  // namespace engineunits
