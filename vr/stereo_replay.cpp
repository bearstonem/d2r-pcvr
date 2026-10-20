// See stereo_replay.h. Adapted from natpoh/diablo2-vr-port 8bd1d3b vr/vrcam.cpp
// namespaces camfix and replay (MIT, Copyright (c) 2026 BodyWalkVR): upload
// buffers read through ReShade's init_resource, the native command-list hooks
// on the root signature and root CBV setters, camera slots with left and right
// staging, the game's present held at the end of the left pass, the lists
// submitted again in the game's own calls, one present a pair. Left out here:
// the queue signal hooks and held signals (off upstream), DLSS predication
// (hangs upstream), the stall guard and the stack dump.
#include "stereo_replay.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <reshade.hpp>

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <mutex>
#include <cstdio>
#include <cstring>
#include <vector>

#include "MinHook.h"
#include "ecl_hook.h"
#include "game_device_only.h"
#include "replay_camera.h"
#include "replay_groups.h"
#include "replay_pause.h"
#include "sigscan.h"
#include "transport.h"

namespace stereoreplay {
namespace {
using namespace reshade::api;

void (*g_log)(const char*) = nullptr;
void Log(const char* s) { if (g_log) g_log(s); }
void LogF(const char* fmt, ...) {
    char b[512];
    va_list a;
    va_start(a, fmt);
    vsnprintf(b, sizeof b, fmt, a);
    va_end(a);
    Log(b);
}
double UsNow() {
    static LARGE_INTEGER f = [] { LARGE_INTEGER q; QueryPerformanceFrequency(&q); return q; }();
    LARGE_INTEGER c; QueryPerformanceCounter(&c);
    return (double)c.QuadPart * 1e6 / (double)f.QuadPart;
}

std::atomic<int> g_marks{3}, g_ahead{2}, g_positions{2};
std::atomic<bool> g_prev{true};
std::atomic<bool> g_registered{false};
// Armed the first time Replay is wanted in a settled area: only then are the
// native command lists hooked and the game's upload buffers mapped - a player
// who never picks Replay keeps the game's lists as they were.
std::atomic<bool> g_armed{false};

// ---- this pair's eyes, from the camera -----------------------------------------------------
SRWLOCK g_eyesLock = SRWLOCK_INIT;
replaycam::Eyes g_eyes;
bool g_viewOk = false, g_projOk = false;
bool EyesNow(replaycam::Eyes* out) {
    AcquireSRWLockShared(&g_eyesLock);
    *out = g_eyes;
    const bool ok = g_viewOk && g_projOk;
    ReleaseSRWLockShared(&g_eyesLock);
    out->positions = g_positions.load(std::memory_order_relaxed);
    out->prev = g_prev.load(std::memory_order_relaxed);
    return ok;
}

// ---- the game's upload buffers: a GPU address read as CPU memory ---------------------------
// Each one noted as it is made (the add-on joins before the game makes its
// device), mapped the first time a camera binding reads it.
struct Upload { uint64_t va, size; uint8_t* cpu; uint64_t res; device* dev; bool tried; };
std::vector<Upload> g_uploads;   // sorted by va
SRWLOCK g_upLock = SRWLOCK_INIT;
void OnInitResource(device* dev, const resource_desc& d, const subresource_data*, resource_usage, resource res) {
    if (d.type != resource_type::buffer || d.heap != memory_heap::upload) return;
    const Upload u{((ID3D12Resource*)res.handle)->GetGPUVirtualAddress(), d.buffer.size, nullptr, res.handle, dev, false};
    AcquireSRWLockExclusive(&g_upLock);
    g_uploads.insert(std::upper_bound(g_uploads.begin(), g_uploads.end(), u, [](const Upload& a, const Upload& b) { return a.va < b.va; }), u);
    ReleaseSRWLockExclusive(&g_upLock);
}
void OnDestroyResource(device*, resource res) {
    AcquireSRWLockExclusive(&g_upLock);
    for (size_t i = 0; i < g_uploads.size(); ++i)
        if (g_uploads[i].res == res.handle) { g_uploads.erase(g_uploads.begin() + i); break; }
    ReleaseSRWLockExclusive(&g_upLock);
}
uint8_t* Cpu(uint64_t va, uint64_t* room) {
    const auto find = [&]() -> Upload* {
        auto it = std::upper_bound(g_uploads.begin(), g_uploads.end(), va, [](uint64_t v, const Upload& u) { return v < u.va; });
        if (it == g_uploads.begin()) return nullptr;
        --it;
        return va < it->va + it->size ? &*it : nullptr;
    };
    AcquireSRWLockShared(&g_upLock);
    Upload* u = find();
    uint8_t* out = u && u->cpu ? u->cpu + (va - u->va) : nullptr;
    const bool map = u && !u->cpu && !u->tried;
    if (out) *room = u->va + u->size - va;
    ReleaseSRWLockShared(&g_upLock);
    if (!map) return out;
    AcquireSRWLockExclusive(&g_upLock);   // the first read of this buffer: mapped once (D3D12's Map is thread-safe)
    if ((u = find()) && !u->cpu && !u->tried) {
        u->tried = true;
        void* cpu = nullptr;
        if (u->dev->map_buffer_region({u->res}, 0, UINT64_MAX, map_access::write_only, &cpu) && cpu) u->cpu = (uint8_t*)cpu;
    }
    if (u && u->cpu) { out = u->cpu + (va - u->va); *room = u->va + u->size - va; }
    ReleaseSRWLockExclusive(&g_upLock);
    return out;
}

// ---- camera keys: each (root signature, parameter) judged once by its content ---------------
struct Key { ID3D12RootSignature* rs; UINT param; int verdict; replaycam::Layout lay; };
std::vector<Key> g_keys;
SRWLOCK g_keyLock = SRWLOCK_INIT;
std::atomic<uint32_t> g_keyGen{0};
// Each recording thread's cache of verdicts (tens of thousands of root CBVs a pass: no lock on the way).
struct Cached { ID3D12RootSignature* rs; UINT param; int verdict; };
thread_local Cached t_cache[64] = {};
thread_local uint32_t t_cacheGen = 0;
// -1: no verdict yet (not in an upload buffer this time, or the camera not built yet)
int VerdictSlow(ID3D12RootSignature* rs, UINT param, uint64_t va) {
    AcquireSRWLockShared(&g_keyLock);
    int v = -1;
    for (size_t i = 0; i < g_keys.size(); ++i) if (g_keys[i].rs == rs && g_keys[i].param == param) { v = g_keys[i].verdict ? 1 + (int)i : 0; break; }
    ReleaseSRWLockShared(&g_keyLock);
    if (v >= 0) return v;
    replaycam::Eyes eyes;
    if (!EyesNow(&eyes)) return -1;
    uint64_t room = 0;
    uint8_t* cpu = Cpu(va, &room);
    if (!cpu) return -1;
    // the whole slot's worth (4 KB): a buffer's view may be further on than the first 1 KB
    float f[1024];
    const int n = (int)std::min<uint64_t>(room, sizeof f) / 4;
    memcpy(f, cpu, n * 4);
    const replaycam::Layout lay = replaycam::Learn(f, n, eyes.left);
    const bool cam = lay.nv || lay.nc;
    AcquireSRWLockExclusive(&g_keyLock);
    g_keys.push_back({rs, param, cam ? 1 : 0, lay});
    v = cam ? (int)g_keys.size() : 0;   // 1 + its index
    ReleaseSRWLockExclusive(&g_keyLock);
    if (cam) {
        char vs[160] = "";
        for (int k = 0, at = 0; k < lay.nv && at < 150; ++k) at += snprintf(vs + at, sizeof vs - at, " %d(%u)", lay.vo[k], lay.parts[k]);
        LogF("vrcam: replay - camera constants: root signature %p parameter %u - %d view(s) at%s, %d position(s) at %d %d %d %d",
             (void*)rs, param, lay.nv, vs, lay.nc, lay.co[0], lay.co[1], lay.co[2], lay.co[3]);
    }
    return v;
}
int Verdict(ID3D12RootSignature* rs, UINT param, uint64_t va) {
    if (t_cacheGen != g_keyGen.load(std::memory_order_relaxed)) { t_cacheGen = g_keyGen.load(); for (Cached& x : t_cache) x = {}; }
    Cached& c = t_cache[(((uintptr_t)rs >> 4) ^ (param * 7u)) & 63];
    if (c.rs == rs && c.param == param) return c.verdict;
    const int v = VerdictSlow(rs, param, va);
    if (v >= 0) c = {rs, param, v};
    return v > 0 ? v : 0;
}
// A position-only key whose buffers fill the slots is an object's, not the camera's.
void Demote(int key) {
    AcquireSRWLockExclusive(&g_keyLock);
    if (g_keys[key - 1].verdict) {
        g_keys[key - 1].verdict = 0;
        g_keyGen.fetch_add(1);
        LogF("vrcam: replay - root signature %p parameter %u: too many buffers a pass - an object's, not the camera's",
             (void*)g_keys[key - 1].rs, g_keys[key - 1].param);
    }
    ReleaseSRWLockExclusive(&g_keyLock);
}

// ---- camera slots: the left pass binds these instead of the game's buffers ------------------
// At the binding the game's values are copied into an upload staging slot for
// the left eye and, rewritten, into one for the right; the GPU copies the left
// ones into the slots before the left pass's lists and the right ones before
// the replay - the queue keeps the order, the CPU never waits, and the game's
// own buffers are never touched.
constexpr int kSlots = 256, kSlotBytes = 4096, kRing = 4;   // slots a pass; staging for kRing pairs
ID3D12Resource* g_slotsBuf = nullptr;   // DEFAULT: what the shaders read
ID3D12Resource* g_stageBuf = nullptr;   // UPLOAD: [ring][eye][slot]
uint8_t* g_stageCpu = nullptr;
uint64_t g_slotsVa = 0;
int g_ring = 0;
struct SlotUse { uint64_t gameVa; int bytes; int key; };
SlotUse g_slotUse[kSlots];
std::atomic<int> g_nSlots{0};
SRWLOCK g_slotLock = SRWLOCK_INIT;
std::atomic<bool> g_redirect{false}, g_capture{false};
std::atomic<uint32_t> g_overflow{0}, g_folded{0}, g_positionsMoved{0};
bool MakeSlots(ID3D12Device* dev) {
    if (g_slotsBuf) return true;
    D3D12_HEAP_PROPERTIES hp{};
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Height = 1; rd.DepthOrArraySize = 1; rd.MipLevels = 1; rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    rd.Width = (UINT64)kSlots * kSlotBytes;
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&g_slotsBuf)))) return false;
    rd.Width = (UINT64)kRing * 2 * kSlots * kSlotBytes;
    hp.Type = D3D12_HEAP_TYPE_UPLOAD;
    if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&g_stageBuf)))) return false;
    D3D12_RANGE none{0, 0};
    if (FAILED(g_stageBuf->Map(0, &none, (void**)&g_stageCpu))) return false;
    g_slotsBuf->SetName(L"d2rvr replay camera slots");
    g_slotsVa = g_slotsBuf->GetGPUVirtualAddress();
    LogF("vrcam: replay - camera slots made: %d x %d bytes on the GPU, staging for %d pairs", kSlots, kSlotBytes, kRing);
    return true;
}
uint8_t* Stage(int ring, int eye, int slot) { return g_stageCpu + ((size_t)(ring * 2 + eye) * kSlots + slot) * kSlotBytes; }
uint64_t StageOffset(int ring, int eye, int slot) { return ((uint64_t)(ring * 2 + eye) * kSlots + slot) * kSlotBytes; }
int KeyCount(int key) {
    AcquireSRWLockShared(&g_slotLock);
    int c = 0;
    const int n = g_nSlots.load();
    for (int i = 0; i < n; ++i) c += g_slotUse[i].key == key;
    ReleaseSRWLockShared(&g_slotLock);
    return c;
}
// A buffer's slot this pass, found without a lock: the binding hook runs on the
// game's recording threads hundreds of times a pass. Open addressing, va 0 =
// empty, cleared each left pass; written under g_slotLock (slot, then va).
constexpr int kHash = 1024;   // a power of two, 4 x kSlots
std::atomic<uint64_t> g_hashVa[kHash];
std::atomic<int> g_hashSlot[kHash];
uint32_t HashOf(uint64_t va) { return (uint32_t)((va >> 8) * 0x9E3779B97F4A7C15ull >> 54) & (kHash - 1); }
int FindSlot(uint64_t va) {
    for (uint32_t h = HashOf(va), i = 0; i < kHash; ++i, h = (h + 1) & (kHash - 1)) {
        const uint64_t v = g_hashVa[h].load(std::memory_order_acquire);
        if (v == va) return g_hashSlot[h].load(std::memory_order_relaxed);
        if (!v) return -1;
    }
    return -1;
}
void PutSlot(uint64_t va, int slot) {   // under g_slotLock
    for (uint32_t h = HashOf(va), i = 0; i < kHash; ++i, h = (h + 1) & (kHash - 1))
        if (!g_hashVa[h].load(std::memory_order_relaxed)) {
            g_hashSlot[h].store(slot, std::memory_order_relaxed);
            g_hashVa[h].store(va, std::memory_order_release);
            return;
        }
}
void ClearSlots() { for (int i = 0; i < kHash; ++i) g_hashVa[i].store(0, std::memory_order_relaxed); }
// Both eyes' values made in ordinary memory first, then written straight into
// the write-combined staging under the lock (reading staging back is slow).
uint64_t Redirect(uint64_t va, const replaycam::Layout& lay, int key, bool* fresh) {
    if (const int i = FindSlot(va); i >= 0) return g_slotsVa + (uint64_t)i * kSlotBytes;
    if (g_nSlots.load(std::memory_order_relaxed) >= kSlots) { *fresh = true; g_overflow.fetch_add(1, std::memory_order_relaxed); return 0; }
    replaycam::Eyes eyes;
    if (!EyesNow(&eyes)) return 0;
    uint64_t room = 0;
    uint8_t* cpu = Cpu(va, &room);
    if (!cpu) { *fresh = true; return 0; }
    const int bytes = (int)std::min<uint64_t>(room, kSlotBytes) & ~3;
    alignas(16) uint8_t left[kSlotBytes], right[kSlotBytes];
    memcpy(left, cpu, bytes);
    replaycam::PrevToRightEye((float*)left, bytes / 4, lay, eyes);
    memcpy(right, left, bytes);
    const auto done = replaycam::PatchRight((float*)right, bytes / 4, lay, eyes);
    g_folded.fetch_add((uint32_t)done.folded, std::memory_order_relaxed);
    g_positionsMoved.fetch_add((uint32_t)done.positions, std::memory_order_relaxed);
    AcquireSRWLockExclusive(&g_slotLock);
    const int n = g_nSlots.load();
    if (const int i = FindSlot(va); i >= 0) { ReleaseSRWLockExclusive(&g_slotLock); return g_slotsVa + (uint64_t)i * kSlotBytes; }
    *fresh = true;
    if (n >= kSlots) { ReleaseSRWLockExclusive(&g_slotLock); g_overflow.fetch_add(1); return 0; }
    memcpy(Stage(g_ring, 0, n), left, bytes);
    memcpy(Stage(g_ring, 1, n), right, bytes);
    g_slotUse[n] = {va, bytes, key};
    g_nSlots.store(n + 1);
    PutSlot(va, n);
    ReleaseSRWLockExclusive(&g_slotLock);
    return g_slotsVa + (uint64_t)n * kSlotBytes;
}
void RecordSlots(ID3D12GraphicsCommandList* l, int eye, int from, int to) {
    for (int i = from; i < to; ++i)
        l->CopyBufferRegion(g_slotsBuf, (UINT64)i * kSlotBytes, g_stageBuf, StageOffset(g_ring, eye, i), (UINT64)g_slotUse[i].bytes);
}

// ---- the native command lists' root signature and root CBV setters --------------------------
using SetRSFn = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, ID3D12RootSignature*);
using SetCBVFn = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, UINT, D3D12_GPU_VIRTUAL_ADDRESS);
// Each command list type has its own vtable: the direct lists' and the compute
// lists' are hooked, each with its own trampolines.
SetRSFn OrigSetGfxRS[2] = {}, OrigSetCompRS[2] = {};
SetCBVFn OrigSetGfxCBV[2] = {}, OrigSetCompCBV[2] = {};
thread_local ID3D12GraphicsCommandList* t_list = nullptr;
thread_local ID3D12RootSignature* t_rs[2] = {};
uint64_t Note(int which, ID3D12GraphicsCommandList* cl, UINT param, uint64_t va) {
    if (!g_capture.load(std::memory_order_relaxed) || !va) return 0;
    ID3D12RootSignature* rs = t_list == cl ? t_rs[which] : nullptr;
    if (!rs) return 0;
    const int key = Verdict(rs, param, va);
    if (!key || !g_redirect.load(std::memory_order_relaxed)) return 0;
    AcquireSRWLockShared(&g_keyLock);
    const replaycam::Layout lay = g_keys[key - 1].lay;
    ReleaseSRWLockShared(&g_keyLock);
    bool fresh = false;
    const uint64_t to = Redirect(va, lay, key, &fresh);
    if (fresh && !lay.nv && KeyCount(key) > 24) Demote(key);
    return to;
}
template <int S> void STDMETHODCALLTYPE HookSetGfxRS(ID3D12GraphicsCommandList* cl, ID3D12RootSignature* rs) {
    if (t_list != cl) { t_list = cl; t_rs[1] = nullptr; }
    t_rs[0] = rs; OrigSetGfxRS[S](cl, rs);
}
template <int S> void STDMETHODCALLTYPE HookSetCompRS(ID3D12GraphicsCommandList* cl, ID3D12RootSignature* rs) {
    if (t_list != cl) { t_list = cl; t_rs[0] = nullptr; }
    t_rs[1] = rs; OrigSetCompRS[S](cl, rs);
}
template <int S> void STDMETHODCALLTYPE HookSetGfxCBV(ID3D12GraphicsCommandList* cl, UINT p, D3D12_GPU_VIRTUAL_ADDRESS va) {
    const uint64_t to = Note(0, cl, p, va);
    OrigSetGfxCBV[S](cl, p, to ? to : va);
}
template <int S> void STDMETHODCALLTYPE HookSetCompCBV(ID3D12GraphicsCommandList* cl, UINT p, D3D12_GPU_VIRTUAL_ADDRESS va) {
    const uint64_t to = Note(1, cl, p, va);
    OrigSetCompCBV[S](cl, p, to ? to : va);
}
std::vector<void*> g_hookedFns;   // functions hooked already (vtables may share them)
SRWLOCK g_hookLock = SRWLOCK_INIT;
bool HookOne(void* fn, void* detour, void** orig) {
    if (std::find(g_hookedFns.begin(), g_hookedFns.end(), fn) != g_hookedFns.end()) return true;
    const bool ok = MH_CreateHook(fn, detour, orig) == MH_OK && MH_EnableHook(fn) == MH_OK;
    if (ok) g_hookedFns.push_back(fn);
    return ok;
}
// ID3D12GraphicsCommandList: SetComputeRootSignature 29, SetGraphicsRootSignature 30,
// SetComputeRootConstantBufferView 37, SetGraphicsRootConstantBufferView 38.
template <int S> bool HookTable(void** vt) {
    bool ok = true;
    ok &= HookOne(vt[29], (void*)&HookSetCompRS<S>, (void**)&OrigSetCompRS[S]);
    ok &= HookOne(vt[30], (void*)&HookSetGfxRS<S>, (void**)&OrigSetGfxRS[S]);
    ok &= HookOne(vt[37], (void*)&HookSetCompCBV<S>, (void**)&OrigSetCompCBV[S]);
    ok &= HookOne(vt[38], (void*)&HookSetGfxCBV<S>, (void**)&OrigSetGfxCBV[S]);
    return ok;
}
bool g_listHooked[2] = {};
void OnReset(command_list* cl) {
    if (!g_armed.load(std::memory_order_relaxed) || (g_listHooked[0] && g_listHooked[1])) return;
    ID3D12GraphicsCommandList* n = (ID3D12GraphicsCommandList*)cl->get_native();
    const D3D12_COMMAND_LIST_TYPE t = n->GetType();
    const int slot = t == D3D12_COMMAND_LIST_TYPE_DIRECT ? 0 : t == D3D12_COMMAND_LIST_TYPE_COMPUTE ? 1 : -1;
    if (slot < 0) return;
    AcquireSRWLockExclusive(&g_hookLock);
    if (!g_listHooked[slot]) {
        g_listHooked[slot] = true;
        void** vt = *(void***)n;
        const bool ok = slot == 0 ? HookTable<0>(vt) : HookTable<1>(vt);
        LogF("vrcam: replay - %s command list hooks (root signature, root CBV) %s", slot == 0 ? "direct" : "compute", ok ? "in" : "FAILED");
    }
    ReleaseSRWLockExclusive(&g_hookLock);
}

// ---- the replay ------------------------------------------------------------------------------
struct Sub { ID3D12CommandQueue* q; ID3D12CommandList* cl; uint32_t call = 0; };
std::vector<Sub> g_subs;   // the left pass's submissions, in order
SRWLOCK g_lock = SRWLOCK_INIT;
IDXGISwapChain* g_proxy = nullptr;    // what the game presents through (ReShade's proxy)
UINT g_presentFlags = 0;
IDXGISwapChain3* g_native = nullptr;  // the real swap chain, for its back buffers
bool g_leftHeld = false;
uint8_t* g_wrap = nullptr;            // the game's swap chain wrapper (its present's r8)
ID3D12Device* g_dev = nullptr;
ID3D12CommandQueue* g_direct = nullptr;
struct Pt { ID3D12Fence* f = nullptr; UINT64 v = 0; };
// A fence per queue: one fence signalled from two queues went back down when
// the second finished first, and a GPU wait on it never ended (upstream).
struct QFence { ID3D12CommandQueue* q; ID3D12Fence* f; UINT64 v; };
std::vector<QFence> g_qf;
Pt g_lastReplay, g_prevReplay;
HANDLE g_event = nullptr;
constexpr int kAllocs = 16;
ID3D12CommandAllocator* g_alloc[kAllocs] = {};
Pt g_allocDone[kAllocs] = {};
int g_allocAt = 0, g_openAlloc = 0;
ID3D12GraphicsCommandList* g_list = nullptr;
thread_local bool t_ours = false;   // our own submissions: not the game's calls
// Our copy list, its allocators and fences: the game may submit from more than
// one thread while the left pass is captured.
std::mutex g_gpuMx;
std::atomic<int> g_capturing{0};
std::atomic<DWORD> g_captureThread{0};
std::atomic<bool> g_restorePending{false};
int g_leftCopied = 0;
std::atomic<uint32_t> g_lateSlots{0};
effect_runtime* g_rt = nullptr;
effect_technique g_rightTech{};
std::atomic<bool> g_rightEffects{false};
uint64_t g_rtvRes[8] = {};
resource_view g_rtv[8] = {};
std::atomic<uint32_t> g_replays{0}, g_presents{0};   // (the watchdog's)
// For the 10 s line, since the last TakeStats (the pairs' thread alone writes them).
uint32_t g_statReplayed = 0, g_statFails = 0, g_statLists = 0, g_statCalls = 0, g_statHeld[4] = {}, g_statAreaSeen = 0;
double g_statSubmitUs = 0.0;
replaypause::Settle g_settle;
std::atomic<uint32_t> g_areaChanges{0};

Pt SignalOn(ID3D12CommandQueue* q) {
    for (QFence& x : g_qf)
        if (x.q == q) { t_ours = true; q->Signal(x.f, ++x.v); t_ours = false; return {x.f, x.v}; }
    QFence x{q, nullptr, 0};
    if (!g_dev || FAILED(g_dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&x.f)))) return {};
    g_qf.push_back(x);
    return SignalOn(q);
}
void GpuWait(ID3D12CommandQueue* q, const Pt& p) { if (p.f) { t_ours = true; q->Wait(p.f, p.v); t_ours = false; } }
void WaitFor(const Pt& p, DWORD ms) {
    if (!p.f || p.f->GetCompletedValue() >= p.v) return;
    p.f->SetEventOnCompletion(p.v, g_event);
    WaitForSingleObject(g_event, ms);
}
bool Setup(ID3D12CommandQueue* q) {
    if (g_list) return true;
    if (FAILED(q->GetDevice(IID_PPV_ARGS(&g_dev)))) return false;
    g_qf.reserve(8);   // (the watchdog reads it from its own thread)
    g_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    for (int i = 0; i < kAllocs; ++i)
        if (FAILED(g_dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&g_alloc[i])))) return false;
    if (FAILED(g_dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_alloc[0], nullptr, IID_PPV_ARGS(&g_list)))) return false;
    g_list->Close();
    g_list->SetName(L"d2rvr replay copy list");
    if (!MakeSlots(g_dev)) Log("vrcam: replay - camera slots could NOT be made: the right eye would be the left one");
    Log("vrcam: replay - fences, allocators and the copy list made");
    return true;
}
// Our own list on the direct queue: the next allocator of the ring, reset and
// opened. A reset under a list the GPU still runs removes the device: its last
// list waited for, up to 2 s (longer and the GPU is hung anyway).
ID3D12GraphicsCommandList* Open() {
    const int a = g_allocAt;
    g_allocAt = (g_allocAt + 1) % kAllocs;
    WaitFor(g_allocDone[a], 2000);
    g_alloc[a]->Reset();
    g_list->Reset(g_alloc[a], nullptr);
    g_openAlloc = a;
    return g_list;
}
void Submit(ID3D12CommandQueue* q) {
    g_list->Close();
    ID3D12CommandList* l = g_list;
    t_ours = true; q->ExecuteCommandLists(1, &l); t_ours = false;
    g_allocDone[g_openAlloc] = SignalOn(q);
}
// One eye's camera values into the slots, on the direct queue.
void CopySlots(int eye) {
    if (eye == 0) g_leftCopied = 0;
    if (!g_direct || !g_slotsBuf) return;
    const int n = g_nSlots.load();
    if (!n) return;
    RecordSlots(Open(), eye, 0, n);
    if (eye == 0) g_leftCopied = n;
    Submit(g_direct);
}
// Slots made since the left values went in (bound by lists recorded after the
// pass's first submission): theirs now, before the next submission; a compute
// submission waits for that copy.
void CopyLateSlots(ID3D12CommandQueue* nq) {
    if (!g_redirect.load() || !g_direct || !g_slotsBuf || g_nSlots.load() <= g_leftCopied) return;
    const int n = g_nSlots.load();
    g_lateSlots.fetch_add((uint32_t)(n - g_leftCopied), std::memory_order_relaxed);
    RecordSlots(Open(), 0, g_leftCopied, n);
    g_leftCopied = n;
    Submit(g_direct);
    if (nq != g_direct) GpuWait(nq, g_allocDone[g_openAlloc]);
}

// ReShade's execute_command_list: each list the game submits while the left pass runs.
void OnExecute(command_queue* q, command_list* cl) {
    if (!g_capturing.load(std::memory_order_relaxed) || t_ours) return;
    const command_queue_type t = q->get_type();
    // The copy queue: its lists from the draw thread inside the left pass are
    // part of the frame (left out, the right eye read the left eye's copy); the
    // streaming's uploads from other threads stay out.
    if ((t & command_queue_type::graphics) == 0 && (t & command_queue_type::compute) == 0 &&
        GetCurrentThreadId() != g_captureThread.load(std::memory_order_relaxed)) return;
    ID3D12CommandQueue* nq = (ID3D12CommandQueue*)q->get_native();
    {
        std::lock_guard lock(g_gpuMx);
        if (g_restorePending.exchange(false)) {   // the left pass's first list: the left camera into the slots first
            if (!g_direct && nq->GetDesc().Type == D3D12_COMMAND_LIST_TYPE_DIRECT) g_direct = nq;
            if (g_redirect.load() && g_direct && Setup(g_direct)) CopySlots(0);
        } else CopyLateSlots(nq);
    }
    AcquireSRWLockExclusive(&g_lock);
    g_subs.push_back({nq, (ID3D12CommandList*)cl->get_native()});
    ReleaseSRWLockExclusive(&g_lock);
}
// The game's ExecuteCommandLists itself (vr/ecl_hook.h): the lists captured
// just before it get its number, so the replay submits them as it did.
std::atomic<uint32_t> g_eclSeq{0};
void ObserveExec(ID3D12CommandQueue* q, unsigned n, ID3D12CommandList* const* lists) {
    if (t_ours || !n || !g_capturing.load(std::memory_order_relaxed)) return;
    AcquireSRWLockExclusive(&g_lock);
    replaygroups::Tag(g_subs, q, (const void* const*)lists, n, g_eclSeq.fetch_add(1, std::memory_order_relaxed) + 1);
    ReleaseSRWLockExclusive(&g_lock);
}
void OnPresent(command_queue*, swapchain* sc, const rect*, const rect*, uint32_t, const rect*) {
    g_presents.fetch_add(1, std::memory_order_relaxed);
    if (!g_native)
        if (IDXGISwapChain* n = (IDXGISwapChain*)sc->get_native()) n->QueryInterface(IID_PPV_ARGS(&g_native));
}
ID3D12Resource* BackBuffer(UINT i) {
    ID3D12Resource* r = nullptr;
    g_native->GetBuffer(i, IID_PPV_ARGS(&r));
    return r;
}
// An eye's ReShade effects on its back buffer now. ReShade renders its effects
// once between two presents: the left eye's here, at the held present; the
// right eye's technique alone after the replay (render_technique has no
// once-a-frame check), on the replayed picture and depth.
bool EffectsAtHold(ID3D12Resource* bb, UINT index, bool right) {
    if (!g_rt || index >= 8) return false;
    // The right eye's technique only once ReShade has it on (made, after the
    // left eye's pass switched it on): nothing wanted, no effects to draw.
    if (right && (!g_rightTech.handle || !g_rt->get_technique_state(g_rightTech))) return false;
    device* dev = g_rt->get_device();
    const resource res{(uint64_t)bb};
    if (g_rtvRes[index] != res.handle) {
        if (g_rtv[index].handle) dev->destroy_resource_view(g_rtv[index]);
        g_rtv[index] = {0};
        const D3D12_RESOURCE_DESC d = bb->GetDesc();
        if (!dev->create_resource_view(res, resource_usage::render_target, resource_view_desc((format)d.Format), &g_rtv[index])) return false;
        g_rtvRes[index] = res.handle;
    }
    command_queue* q = g_rt->get_command_queue();
    command_list* cl = q->get_immediate_command_list();
    t_ours = true;
    cl->barrier(res, resource_usage::present, resource_usage::render_target);
    g_rightEffects.store(right);
    if (right) g_rt->render_technique(g_rightTech, cl, g_rtv[index], g_rtv[index]);
    else g_rt->render_effects(cl, g_rtv[index], g_rtv[index]);
    g_rightEffects.store(false);
    cl->barrier(res, resource_usage::render_target, resource_usage::present);
    q->flush_immediate_command_list();
    t_ours = false;
    return true;
}
// The left pass's present: its effects run and it goes to the headset; the
// present itself is held back (the game is told it went).
bool HoldLeft() {
    if (!g_native || !g_rt || !g_rightTech.handle) return false;
    ID3D12CommandQueue* direct = nullptr;
    AcquireSRWLockExclusive(&g_lock);
    for (const Sub& s : g_subs)
        if (s.q->GetDesc().Type == D3D12_COMMAND_LIST_TYPE_DIRECT) { direct = s.q; break; }
    ReleaseSRWLockExclusive(&g_lock);
    if (!direct) return false;
    {
        std::lock_guard lock(g_gpuMx);
        g_direct = direct;
        if (!Setup(direct)) return false;
    }
    const UINT index = g_native->GetCurrentBackBufferIndex();
    ID3D12Resource* bb = BackBuffer(index);
    if (!bb) return false;
    // ReShade rewrites an effect's constants in place at each render: the last
    // pair's right eye must be done on the GPU before the left eye's go in.
    WaitFor(g_lastReplay, 100);
    const bool ok = EffectsAtHold(bb, index, false);
    bb->Release();
    if (ok) { t_ours = true; framevr::CaptureEffects(g_rt); t_ours = false; }   // the left eye to the headset, as at a present
    return ok;
}
// The game's present (D2R 3.3.93787 0x109F4C0: rcx its device, rdx a result,
// r8 its swap chain wrapper - [0] the swap chain it presents through, +0x50 the
// sync interval, +0x38 / +0x39 flags, +0x98 the back buffer it draws next).
constexpr uint64_t RVA_GAME_PRESENT = 0x109F4C0;
const uint8_t kSigGamePresent[16] = {0x48,0x89,0x5C,0x24,0x18,0x48,0x89,0x54,0x24,0x10,0x57,0x48,0x83,0xEC,0x40,0x4D};
using GamePresentFn = void* (*)(void*, void*, void*);
GamePresentFn OrigGamePresent = nullptr;
std::atomic<uint32_t> g_presentHookCalls{0};   // (why the replay is not ready, in the log)
void* HookGamePresent(void* dev, void* result, void* wrap) {
    g_presentHookCalls.fetch_add(1, std::memory_order_relaxed);
    if (wrap) {
        __try {   // and the flags it presents with: tearing allowed (0x200) as the game decides it
            const uint8_t* w = (const uint8_t*)wrap;
            g_proxy = *(IDXGISwapChain**)wrap;
            g_presentFlags = *((const uint8_t*)dev + 0x80) && *(const uint32_t*)(w + 0x50) == 0 && !w[0x38] && w[0x39] ? 0x200u : 0u;
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    if (g_capturing.load() && !g_leftHeld && HoldLeft()) {
        g_leftHeld = true;
        g_wrap = (uint8_t*)wrap;
        *(uint16_t*)result = 0;                    // the game's own success: code 0, HRESULT 0
        *(uint32_t*)((uint8_t*)result + 4) = 0;
        return result;
    }
    return OrigGamePresent(dev, result, wrap);
}
void SetGameBackBuffer(uint8_t* wrap, UINT index) {
    __try { *(uint32_t*)(wrap + 0x98) = index; } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// A watchdog: no present for 3 s while replaying - each queue's fences, to the
// log, once (its writer thread goes on if the game's is stuck).
DWORD WINAPI Watchdog(void*) {
    uint32_t seen = 0;
    double since = UsNow();
    bool told = false;
    for (;;) {
        Sleep(500);
        const uint32_t now = g_presents.load();
        if (now != seen || !g_replays.load()) { seen = now; since = UsNow(); told = false; continue; }
        if (told || UsNow() - since < 3e6) continue;
        told = true;
        Log("vrcam: replay watchdog - no present for 3 s while replaying:");
        for (const QFence& x : g_qf)
            LogF("vrcam: replay watchdog -   queue %p (type %d): our fence signalled %llu, done %llu", (void*)x.q, (int)x.q->GetDesc().Type,
                 (unsigned long long)x.v, (unsigned long long)x.f->GetCompletedValue());
        LogF("vrcam: replay watchdog -   last replay point %llu, two back %llu; slots %d; device removed reason %08lX", (unsigned long long)g_lastReplay.v,
             (unsigned long long)g_prevReplay.v, g_nSlots.load(), g_dev ? (unsigned long)g_dev->GetDeviceRemovedReason() : 0ul);
    }
}
}  // namespace

void Register(void (*log)(const char*), const Settings& settings) {
    if (g_registered.exchange(true)) return;
    g_log = log;
    Configure(settings);
    reshade::register_event<reshade::addon_event::init_resource>(&d2rvr::D3D12Only<&OnInitResource>::Call);
    reshade::register_event<reshade::addon_event::destroy_resource>(&d2rvr::D3D12Only<&OnDestroyResource>::Call);
    reshade::register_event<reshade::addon_event::reset_command_list>(&d2rvr::D3D12Only<&OnReset>::Call);
    reshade::register_event<reshade::addon_event::execute_command_list>(&d2rvr::D3D12Only<&OnExecute>::Call);
    reshade::register_event<reshade::addon_event::present>(&d2rvr::D3D12Only<&OnPresent>::Call);
    if (HANDLE h = CreateThread(nullptr, 0, Watchdog, nullptr, 0, nullptr)) CloseHandle(h);
    LogF("vrcam: replay ready - Stereo rendering: Replay draws the right eye from the left pass's lists (submitted %s)",
         settings.marks >= replaygroups::kGame ? "as the game's calls" : "one list a call");
}
void Configure(const Settings& s) {
    g_marks.store(std::clamp(s.marks, 0, 4));
    g_ahead.store(std::clamp(s.ahead, 1, 2));
    g_positions.store(std::clamp(s.positions, 0, 2));
    g_prev.store(s.prev);
}
void SetLeftView(const float view[16], float shiftWorld) {
    AcquireSRWLockExclusive(&g_eyesLock);
    for (int i = 0; i < 16; ++i) g_eyes.left[i] = view[i];
    g_eyes.shift = shiftWorld;
    g_viewOk = std::isfinite(shiftWorld) && shiftWorld > 0.0f;
    ReleaseSRWLockExclusive(&g_eyesLock);
}
void SetProjection(float projSx, float projShift) {
    AcquireSRWLockExclusive(&g_eyesLock);
    g_eyes.projSx = projSx;
    g_eyes.projShift = projShift;
    g_projOk = std::isfinite(projSx) && projSx != 0.0f && std::isfinite(projShift);
    ReleaseSRWLockExclusive(&g_eyesLock);
}
void SetEffects(effect_runtime* rt, effect_technique right) {
    if (g_rt != rt) { for (auto& v : g_rtv) v = {0}; for (auto& r : g_rtvRes) r = 0; }   // (views of the old runtime's device go with it)
    g_rt = rt;
    g_rightTech = right;
}
bool Wanted(bool inWorld) {
    if (!g_registered.load()) return false;
    const bool was = g_settle.Settled();
    replaypause::Why stopped;
    const bool settled = g_settle.Step(g_areaChanges.load(), UsNow(), inWorld, &stopped);
    ++g_statHeld[int(g_settle.held)];
    if (stopped == replaypause::Why::kNoWorld) Log("vrcam: replay - paused (no world: a menu or an area change)");
    else if (stopped == replaypause::Why::kArea) Log("vrcam: replay - paused (the game set a biome: an area loads) - two passes until it has settled");
    else if (stopped == replaypause::Why::kLongFrame) Log("vrcam: replay - paused (a game frame of over 150 ms: a load) - two passes until the area has settled");
    if (!settled) return false;
    if (!was) Log("vrcam: replay - on (the area settled)");
    if (!g_armed.exchange(true)) Log("vrcam: replay - armed: the game's command lists are hooked as they are next reset");
    // Our fences, allocators and camera slots, before the first pair binds
    // anything: ReShade's queue is the game's direct queue.
    if (!g_list && g_rt) {
        std::lock_guard lock(g_gpuMx);
        g_direct = (ID3D12CommandQueue*)g_rt->get_command_queue()->get_native();
        if (!Setup(g_direct)) return false;
    }
    static int hooked = -1;
    if (hooked < 0) {
        hooked = d2rsig::Hook(RVA_GAME_PRESENT, kSigGamePresent, sizeof kSigGamePresent, (void*)&HookGamePresent, (void**)&OrigGamePresent) ? 1 : 0;
        LogF("vrcam: replay - the game's present %s", hooked ? "hooked" : "NOT where expected - no replay");
    }
    replaycam::Eyes eyes;
    const bool eyesOk = EyesNow(&eyes);
    const bool ready = hooked == 1 && g_proxy && g_native && g_rt && g_rightTech.handle && eyesOk;
    static bool told = false;   // once a settled stretch: what is missing
    if (!was) told = false;
    if (!ready && !told) {
        told = true;
        LogF("vrcam: replay - settled but NOT ready: the game's present %s (%u calls), its swap chain %s, the real one %s, effects %s, "
             "right technique %s, eyes %s (gap %.3f world units, projection x %.3f)",
             hooked == 1 ? "hooked" : "not hooked", g_presentHookCalls.load(), g_proxy ? "seen" : "NOT seen", g_native ? "seen" : "NOT seen",
             g_rt ? "seen" : "NOT seen", g_rightTech.handle ? "found" : "NOT found", eyesOk ? "known" : "NOT known", eyes.shift, eyes.projSx);
    }
    return ready;
}
void AreaChanged() {
    g_areaChanges.fetch_add(1);
    // the last replay done on the GPU before the game frees what it drew with: 200 ms at most
    const Pt last = g_lastReplay;
    if (!last.f || last.f->GetCompletedValue() >= last.v) return;
    if (HANDLE e = CreateEventW(nullptr, FALSE, FALSE, nullptr)) {
        if (SUCCEEDED(last.f->SetEventOnCompletion(last.v, e))) WaitForSingleObject(e, 200);
        CloseHandle(e);
    }
}
void BeginLeft() {
    // The game resets the left pass's command allocators once its own frame
    // fence passes, which comes before our replay of them: the replay one or
    // two pairs back done first.
    WaitFor(g_ahead.load() >= 2 ? g_prevReplay : g_lastReplay, 100);
    g_prevReplay = g_lastReplay;
    AcquireSRWLockExclusive(&g_lock);
    g_subs.clear();
    ReleaseSRWLockExclusive(&g_lock);
    g_leftHeld = false;
    g_captureThread.store(GetCurrentThreadId());
    g_ring = (g_ring + 1) % kRing;
    g_nSlots.store(0);
    ClearSlots();
    g_redirect.store(g_slotsBuf && g_stageCpu);
    g_restorePending.store(true);
    g_capture.store(true);
    g_capturing.store(1);
}
bool RightFromLeft() {
    const int marks = g_marks.load(std::memory_order_relaxed);
    g_capturing.store(0);
    std::vector<Sub> subs;
    AcquireSRWLockExclusive(&g_lock);
    subs.swap(g_subs);
    ReleaseSRWLockExclusive(&g_lock);
    const bool held = g_leftHeld;
    g_leftHeld = false;
    // The right pass, if there is one, binds the game's buffers again.
    g_capture.store(false);
    const bool slots = g_redirect.exchange(false) && g_slotsBuf;
    if (!held || !slots || !g_direct) {
        ++g_statFails;
        static uint32_t told = 0;   // why, the first few times
        if (told++ < 5) LogF("vrcam: replay - this pair fell back to two passes: left present %s, camera slots %s, direct queue %s",
                             held ? "held" : "NOT held", slots ? "in use" : "NOT in use", g_direct ? "known" : "NOT known");
        return false;
    }
    std::unique_lock gpu(g_gpuMx);
    CopySlots(1);   // the right eye's camera into the slots, before the replay
    if (marks >= replaygroups::kGame)   // the game's calls seen from the next pair on (each queue once)
        for (const Sub& s : subs) {
            static std::vector<ID3D12CommandQueue*> hooked;
            if (std::find(hooked.begin(), hooked.end(), s.q) != hooked.end()) continue;
            hooked.push_back(s.q);
            LogF("vrcam: replay - the game's ExecuteCommandLists on queue %p (type %d) watched %s", (void*)s.q, (int)s.q->GetDesc().Type,
                 eclhook::Install(s.q, &ObserveExec) ? "- hook in" : "- hook FAILED");
        }
    const double t0 = UsNow();
    // The lists again, a batch per run of one queue, our fence at each queue
    // switch; the first batch on compute must see the right eye's slots too.
    if (!subs.empty() && subs[0].q != g_direct) GpuWait(subs[0].q, SignalOn(g_direct));
    ID3D12CommandQueue* prev = nullptr;
    unsigned calls = 0;
    for (size_t i = 0; i < subs.size();) {
        ID3D12CommandQueue* q = subs[i].q;
        const size_t runStart = i;
        while (i < subs.size() && subs[i].q == q) ++i;
        if (prev && prev != q) GpuWait(q, SignalOn(prev));
        for (const replaygroups::Batch& b : replaygroups::PlanRun(subs, runStart, i, marks)) {
            ID3D12CommandList* ls[128];
            for (size_t off = 0; off < b.count; off += 128) {
                const size_t c = std::min<size_t>(128, b.count - off);
                for (size_t k = 0; k < c; ++k) ls[k] = subs[b.first + off + k].cl;
                t_ours = true; q->ExecuteCommandLists((UINT)c, ls); t_ours = false;
                ++calls;
            }
            if (replaygroups::SignalAfterEach(marks)) SignalOn(q);
        }
        prev = q;
    }
    if (prev && prev != g_direct) GpuWait(g_direct, SignalOn(prev));
    gpu.unlock();   // (the present below runs ReShade's and the transport's callbacks)
    g_statSubmitUs += UsNow() - t0;
    g_statLists += (uint32_t)subs.size();
    g_statCalls += calls;
    // The right eye: its effects on the replayed picture and depth, then one
    // real present - the transport takes it to the headset at that present.
    const UINT index = g_native->GetCurrentBackBufferIndex();
    if (ID3D12Resource* bb = BackBuffer(index)) { EffectsAtHold(bb, index, true); bb->Release(); }
    t_ours = true;
    g_proxy->Present(0, g_presentFlags);
    t_ours = false;
    gpu.lock();
    g_lastReplay = SignalOn(g_direct);
    gpu.unlock();
    // The game asked for its next back buffer right after its (held) present.
    if (g_wrap) SetGameBackBuffer(g_wrap, g_native->GetCurrentBackBufferIndex());
    if (g_replays.fetch_add(1) == 0) LogF("vrcam: replay - the first pair from one pass: %zu lists in %u calls, %d camera slots", subs.size(), calls, g_nSlots.load());
    ++g_statReplayed;
    return true;
}
bool Capturing() { return g_capturing.load(std::memory_order_relaxed) != 0; }
bool RightEffects() { return g_rightEffects.load(std::memory_order_relaxed); }
Stats TakeStats() {
    Stats s;
    s.replayed = g_statReplayed; s.fallbacks = g_statFails; s.lists = g_statLists; s.calls = g_statCalls; s.submitMs = g_statSubmitUs / 1000.0;
    s.slots = (uint32_t)g_nSlots.load(); s.overflow = g_overflow.exchange(0); s.lateSlots = g_lateSlots.exchange(0);
    s.folded = g_folded.exchange(0); s.positions = g_positionsMoved.exchange(0);
    const uint32_t areas = g_areaChanges.load();
    s.biomeSets = areas - g_statAreaSeen; g_statAreaSeen = areas;
    s.restartArea = g_statHeld[int(replaypause::Why::kArea)]; s.restartLong = g_statHeld[int(replaypause::Why::kLongFrame)];
    s.outOfWorld = g_statHeld[int(replaypause::Why::kNoWorld)]; s.settling = g_settle.pairs; s.settleOf = replaypause::Settle::kPairs;
    g_statReplayed = g_statFails = g_statLists = g_statCalls = 0; g_statSubmitUs = 0.0;
    for (uint32_t& n : g_statHeld) n = 0;
    return s;
}

}  // namespace stereoreplay
