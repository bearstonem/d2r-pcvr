// Render distance for the perspective camera (see drawdist.h, RECON.md section 5).
//
// How the game decides what to draw: every simulation tick the 3D layer starts
// a list with the hero's room, calls FindRoomsVisibleToRoomSet once (one ring of
// neighbours), activates every room of that list, then walks one or two rings
// further only to prefetch and to activate rooms inside the old camera's
// (0.9-scaled) frustum. Behind and beside a free camera the map therefore ends
// one room away.
//
// What we change, only inside that tick:
//   radius 2: after the game's own first pass we run the same pass again, so
//             the second ring - rooms the game already keeps loaded - is
//             activated in every direction. Nothing is loaded that the game
//             would not have loaded anyway.
//   radius 3+: D2Common keeps ROOM objects (tiles, collision) only two rings
//             out; the third ring has preset data, further rings nothing. We
//             walk the level's room graph (DRLG_ROOM near lists) out to the
//             radius, ask the game to tile missing rooms with its own
//             on-demand function (one room per tick, so loading never
//             stalls a frame), and add them to the list. Only rooms of levels
//             the game already put in the list are considered.

#include "drawdist.h"

#include "../camera/game_layout.h"
#include "../sigscan/sigscan.h"
#include "../../steamvr/far_objects.h"

#include <D2RLPlugin/api.h>

#include <Windows.h>
#include <atomic>
#include <cstring>
#include <cstdio>
#ifdef D2RVR_NATIVE_OPENXR
#include "../../steamvr/logging.h"
#endif

namespace d2rcam::drawdist {
namespace {

using namespace d2rcam::layout;

using SimTickFn     = void(__fastcall*)(void* layer);
using FindRoomsFn   = void(__fastcall*)(void* layer, void* room, void* list);
using EnsureTiledFn = void*(__fastcall*)(uint32_t gameVersion, void* drlgRoom);
using PushBackFn    = void(__fastcall*)(void* list, void* const* element);
using SetRadiusFn   = void(__fastcall*)(float units);
using GetRadiusFn   = float(__fastcall*)();
using InRangeFn     = bool(__fastcall*)(const float* box, const float* hero, float radiusSq, int flags);

const D2RL::PluginContext* g_ctx  = nullptr;
uintptr_t                  g_base = 0;

SimTickFn     o_simTick     = nullptr;
FindRoomsFn   o_findRooms   = nullptr;
std::atomic<EnsureTiledFn> g_ensureTiled{nullptr};
std::atomic<PushBackFn> g_pushBack{nullptr};
std::atomic<uint64_t> g_nextBindingsTry{0};
std::atomic<int> g_expansion{0},g_furthestRoom{0},g_tileAttempts{0};
std::atomic<uint64_t> g_lastExpansionMs{0};

std::atomic<bool> g_installed { false };
std::atomic<int>  g_radius { 1 };
std::atomic<int>  g_added { 0 };
std::atomic<int>  g_loaded { 0 };

// Model visibility: the game's setter and getter, and how long the forced
// re-sort after a change lasts (the next ticks pick the new radius up even if
// the hero stands still).
std::atomic<SetRadiusFn> g_setModelRadius{nullptr};
std::atomic<GetRadiusFn> g_getModelRadius{nullptr};
std::atomic<uint64_t>  g_recalcUntil { 0 };
std::atomic<bool> g_recalcOk{false};

// Big objects drawn farther: the game's test, hooked once the setting is first on.
InRangeFn              o_inRange = nullptr;
std::atomic<float>     g_bigRadius { 0.0f };
std::atomic<uint32_t>  g_bigKept { 0 };
uint64_t               g_nextInRangeTry = 0;

// Both hooks run on the thread that runs SimulationTick; the flags only pair a
// FindRooms call with the tick that made it.
bool g_inTick   = false;
bool g_expanded = false;
#ifdef D2RVR_NATIVE_OPENXR
thread_local double t_roomFindMs=0,t_roomExpandMs=0;
double TimeMs(){static const double scale=[](){LARGE_INTEGER f{};QueryPerformanceFrequency(&f);return 1000./f.QuadPart;}();LARGE_INTEGER t{};QueryPerformanceCounter(&t);return t.QuadPart*scale;}
#endif

// Rooms tiled per tick beyond the game's own two rings. Tiling builds a room's
// tiles and collision; one per tick spreads a new ring over a few frames.
constexpr int kTilesPerTick = 1;
// Upper bound on the graph walk, so a huge outdoor level cannot cost a frame.
constexpr int kMaxVisited = 512;

// See d2rcam.cpp: a detour can run before the loader has stored the trampoline.
template <typename Fn>
Fn Orig(Fn const volatile& p) {
	for (int i = 0; i < (1 << 20); ++i) {
		const Fn f = p;
		if (f != nullptr) {
			return f;
		}
		YieldProcessor();
	}
	return p;
}

template <typename T>
T& At(void* base, size_t offset) {
	return *reinterpret_cast<T*>(static_cast<uint8_t*>(base) + offset);
}

size_t ListSize(void* list) { return At<size_t>(list, roomlist::Size); }
void** ListData(void* list) { return At<void**>(list, roomlist::Data); }

bool ListContains(void* list, void* room) {
	void** data = ListData(list);
	const size_t n = ListSize(list);
	for (size_t i = 0; i < n; ++i) {
		if (data[i] == room) {
			return true;
		}
	}
	return false;
}

// Walks the DRLG room graph from the hero's room out to `radius` rings, tiles a
// bounded number of missing rooms and appends every tiled room to the list.
void LoadAndAdd(void* centreRoom, void* list, int radius) {
    g_lastExpansionMs.store(GetTickCount64());g_furthestRoom.store(0);
    const auto ensure=g_ensureTiled.load();const auto append=g_pushBack.load();
	if (ensure == nullptr || append == nullptr) {
        g_expansion.store(1);
		return;
	}
	const uintptr_t versionAt = d2rsig::Addr(GameVersionByte);
	if (versionAt == 0) {
        g_expansion.store(2);
		return;
	}
	const uint8_t version = *reinterpret_cast<const uint8_t*>(versionAt);
	if (version > 3) {   // classic / LoD / RotW; anything else means the address is wrong
        g_expansion.store(3);
		return;
	}
	void* centre = At<void*>(centreRoom, room::Drlg);
	if (centre == nullptr) {
        g_expansion.store(4);
		return;
	}
    g_expansion.store(5);

	// Levels the game itself chose to show (its own filter already ran).
	void*  levels[16];
	int    levelCount = 0;
	void** data       = ListData(list);
	const size_t n    = ListSize(list);
	for (size_t i = 0; i < n && levelCount < 16; ++i) {
		void* drlg = data[i] != nullptr ? At<void*>(data[i], room::Drlg) : nullptr;
		void* lvl  = drlg != nullptr ? At<void*>(drlg, drlgroom::Level) : nullptr;
		bool  seen = lvl == nullptr;
		for (int k = 0; k < levelCount && !seen; ++k) {
			seen = levels[k] == lvl;
		}
		if (!seen) {
			levels[levelCount++] = lvl;
		}
	}

	struct Node {
		void* room;
		int   depth;
	};
	static Node queue[kMaxVisited];   // only this thread uses it
	int head = 0, tail = 0;
	queue[tail++] = { centre, 0 };
	int budget = kTilesPerTick;

	while (head < tail) {
		const Node node = queue[head++];
		void* level = At<void*>(node.room, drlgroom::Level);
		bool  ok    = false;
		for (int k = 0; k < levelCount && !ok; ++k) {
			ok = levels[k] == level;
		}
		if (ok && node.depth >= 1) {
			void* room = At<void*>(node.room, drlgroom::Room);
			// Rings 1-2 are the game's own (tiled by it, lazily for ring 2).
			if (room == nullptr && node.depth >= 3 && budget > 0) {
                g_tileAttempts.fetch_add(1);
				room = ensure(version, node.room);
				--budget;
				if(room)g_loaded.fetch_add(1, std::memory_order_relaxed);
			}
            if(room)g_furthestRoom.store(std::max(g_furthestRoom.load(),node.depth));
			if (room != nullptr && !ListContains(list, room)) {
				append(list, &room);
			}
		}
		if (node.depth >= radius) {
			continue;
		}
		void** nearRooms = At<void**>(node.room, drlgroom::NearData);
		const size_t nearCount = At<size_t>(node.room, drlgroom::NearCount);
		if (nearRooms == nullptr || nearCount > 64) {   // a D2 room has a handful of neighbours
			continue;
		}
		for (size_t i = 0; i < nearCount && tail < kMaxVisited; ++i) {
			void* next = nearRooms[i];
			if (next == nullptr) {
				continue;
			}
			bool queued = false;
			for (int k = 0; k < tail && !queued; ++k) {
				queued = queue[k].room == next;
			}
			if (!queued) {
				queue[tail++] = { next, node.depth + 1 };
			}
		}
	}
}

void __fastcall HookSimTick(void* layer) {
	const SimTickFn original = Orig(o_simTick);
	if (original == nullptr) {
		return;
	}
	g_inTick   = true;
	g_expanded = false;
#ifdef D2RVR_NATIVE_OPENXR
    t_roomFindMs=t_roomExpandMs=0;const double start=TimeMs();
#endif
	original(layer);
	g_inTick = false;
#ifdef D2RVR_NATIVE_OPENXR
    const double ms=TimeMs()-start;static ULONGLONG lastLog=0;
    if(ms>50&&GetTickCount64()-lastLog>1000&&g_ctx){
        lastLog=GetTickCount64();char line[200];
        std::snprintf(line,sizeof line,"Frame game tick: %.1f ms, room discovery %.1f, VR room expansion %.1f; rings %d, added %d, tiled total %d",ms,t_roomFindMs,t_roomExpandMs,g_radius.load(),g_added.load(),g_loaded.load());framevr::logging::Write(line);
    }
#endif
	const uint64_t until = g_recalcUntil.load(std::memory_order_relaxed);
	if (until != 0 && GetTickCount64() > until) {
		g_recalcUntil.store(0, std::memory_order_relaxed);
		*reinterpret_cast<volatile uint8_t*>(d2rsig::Addr(ModelRecalcEveryFrame)) = 0;
	}
}

void __fastcall HookFindRooms(void* layer, void* room, void* list) {
	const FindRoomsFn original = Orig(o_findRooms);
	if (original == nullptr) {
		return;
	}
#ifdef D2RVR_NATIVE_OPENXR
    const double start=TimeMs();
#endif
	original(layer, room, list);
#ifdef D2RVR_NATIVE_OPENXR
    if(g_inTick)t_roomFindMs+=TimeMs()-start;
#endif
	// Only the tick's first pass builds the drawn set; later passes are prefetch
	// and the debug drawing also calls this function.
	if (!g_inTick || g_expanded) {
		return;
	}
	g_expanded = true;
	const int radius = g_radius.load(std::memory_order_relaxed);
	if (radius <= 1 || room == nullptr || list == nullptr) {
		g_added.store(0, std::memory_order_relaxed);
		return;
	}
	const size_t before = ListSize(list);
#ifdef D2RVR_NATIVE_OPENXR
    const double expandedAt=TimeMs();
#endif
	// Ring 2 through the game's own pass and filter. A third pass would find
	// nothing: ROOMs do not exist further out (RECON.md section 5).
	original(layer, room, list);
	if (radius >= 3) {
		LoadAndAdd(room, list, radius);
	}
	g_added.store(static_cast<int>(ListSize(list) - before), std::memory_order_relaxed);
#ifdef D2RVR_NATIVE_OPENXR
    t_roomExpandMs+=TimeMs()-expandedAt;
#endif
}

bool Check(const Site& s) {
	return d2rsig::Check(s.rva, s.bytes, s.size);
}

// The AssetStandbySystem's test for each model: kept drawn while true. A big model's reach grows
// with its size (steamvr/far_objects.h); with the setting off it is the game's own test.
bool __fastcall HookInRange(const float* box, const float* hero, float radiusSq, int flags) {
	const float big = g_bigRadius.load(std::memory_order_relaxed);
	if (big <= 0.0f) {
		return Orig(o_inRange)(box, hero, radiusSq, flags);
	}
	const bool in = farobjects::InRange(box, hero, radiusSq, big);
	if (in && !farobjects::InRange(box, hero, radiusSq, 0.0f)) {
		g_bigKept.fetch_add(1, std::memory_order_relaxed);
	}
	return in;
}

// Its page is decrypted once the game has run it (the hero walked); tried at most twice a second.
bool HookInRangeIn() {
	if (o_inRange != nullptr) {
		return true;
	}
	const uint64_t now = GetTickCount64();
	if (g_ctx == nullptr || now < g_nextInRangeTry) {
		return false;
	}
	g_nextInRangeTry = now + 500;
	if (!Check(ModelInRange) || !d2rsig::Hook(ModelInRange.rva, ModelInRange.bytes, ModelInRange.size, reinterpret_cast<void*>(&HookInRange), reinterpret_cast<void**>(&o_inRange))) {
		o_inRange = nullptr;
		return false;
	}
	return true;
}

}   // namespace

bool Install(const D2RL::PluginContext* ctx) {
	if (ctx == nullptr || ctx->exeBase == 0) {
		return false;
	}
	g_ctx  = ctx;
	g_base = ctx->exeBase;
	d2rsig::Resolve(ctx);
	Refresh();
    if(g_installed.load())return true;
    // FindRooms first: it does nothing until SimulationTick raises the flag.
    if (o_findRooms == nullptr) {
        if (!Check(FindRooms) || !d2rsig::Hook(FindRooms.rva, FindRooms.bytes, FindRooms.size, reinterpret_cast<void*>(&HookFindRooms), reinterpret_cast<void**>(&o_findRooms))) {
            o_findRooms = nullptr;return false;
        }
    }
    if (o_simTick == nullptr) {
        if (!Check(LevelSimTick) || !d2rsig::Hook(LevelSimTick.rva, LevelSimTick.bytes, LevelSimTick.size, reinterpret_cast<void*>(&HookSimTick), reinterpret_cast<void**>(&o_simTick))) {
            o_simTick = nullptr;return false;
        }
    }
    g_installed.store(true);
    return true;
}

void Refresh(){
    if(!g_ctx)return;
    const uint64_t now=GetTickCount64();auto next=g_nextBindingsTry.load();
    if(now<next||!g_nextBindingsTry.compare_exchange_strong(next,now+500))return;
	if (g_ensureTiled == nullptr && Check(EnsureRoomTiled)) {
		g_ensureTiled = reinterpret_cast<EnsureTiledFn>(d2rsig::Addr(EnsureRoomTiled.rva));
	}
	if (g_pushBack == nullptr && Check(RoomListPushBack)) {
		g_pushBack = reinterpret_cast<PushBackFn>(d2rsig::Addr(RoomListPushBack.rva));
	}
	if (g_setModelRadius == nullptr && Check(layout::SetModelRadius) && Check(layout::GetModelRadius)) {
		g_setModelRadius = reinterpret_cast<SetRadiusFn>(d2rsig::Addr(layout::SetModelRadius.rva));
		g_getModelRadius = reinterpret_cast<GetRadiusFn>(d2rsig::Addr(layout::GetModelRadius.rva));
	}
	if (!g_recalcOk) {
		g_recalcOk = Check(ModelRecalcCheck) && d2rsig::Addr(ModelRecalcEveryFrame) != 0;
	}
}

bool Installed() { return g_installed.load(); }

void SetRadius(int rooms) {
	if (rooms < 1) {
		rooms = 1;
	}
	if (rooms > 8) {
		rooms = 8;
	}
	g_radius.store(rooms);
}

int Radius() { return g_radius.load(); }

bool SetModelRadius(float units) {
    const auto setter=g_setModelRadius.load();
	if (setter == nullptr) {
		return false;
	}
	setter(units < 10.0f ? 10.0f : (units > 10000.0f ? 10000.0f : units));
	if (g_recalcOk) {   // re-sort now, not when the hero next walks out of the elastic range
		*reinterpret_cast<volatile uint8_t*>(d2rsig::Addr(ModelRecalcEveryFrame)) = 1;
		g_recalcUntil.store(GetTickCount64() + 500, std::memory_order_relaxed);
	}
	return true;
}

float ModelRadius() { const auto getter=g_getModelRadius.load();return getter ? getter() : -1.0f; }

RuntimeStatus ReadStatus(){
    RuntimeStatus s;s.hooks=Installed();s.tiler=g_ensureTiled.load()!=nullptr;
    s.append=g_pushBack.load()!=nullptr;s.modelRadius=g_setModelRadius.load()!=nullptr&&g_getModelRadius.load()!=nullptr;
    s.recalculate=g_recalcOk.load();s.rings=Radius();s.expansion=g_expansion.load();
    s.furthestRoom=g_furthestRoom.load();s.roomsAdded=g_added.load();s.roomsLoaded=g_loaded.load();
    s.tileAttempts=g_tileAttempts.load();const float actual=ModelRadius();
    s.activeModelRadius=std::isfinite(actual)?actual:-1;s.lastExpansionMs=g_lastExpansionMs.load();return s;
}

bool SetBigRadius(float units) {
	const float big = farobjects::Normalize(units);
	g_bigRadius.store(big, std::memory_order_relaxed);
	if (big <= 0.0f && o_inRange == nullptr) {
		return true;   // off: the game's test untouched
	}
	if (!HookInRangeIn()) {
		return false;
	}
	if (g_recalcOk) {   // re-sort now, as for the model radius
		*reinterpret_cast<volatile uint8_t*>(d2rsig::Addr(ModelRecalcEveryFrame)) = 1;
		g_recalcUntil.store(GetTickCount64() + 500, std::memory_order_relaxed);
	}
	return true;
}

float BigRadius() { return g_bigRadius.load(std::memory_order_relaxed); }
uint32_t TakeBigKept() { return g_bigKept.exchange(0, std::memory_order_relaxed); }
int RoomsAddedLastTick() { return g_added.load(); }
int RoomsLoadedTotal() { return g_loaded.load(); }

}   // namespace d2rcam::drawdist
