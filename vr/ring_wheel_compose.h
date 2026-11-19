#pragma once
// Lab Ring wheel (T025, build steps R4-R5): the compose pass. It draws steamvr/ring_wheel_draw.h's instance list into a
// target (the Lab's own layer swapchain in the game from R6, a WARP texture in ring-wheel-preview). Plan E3, E4,
// frame-budget.md sections 2, 3 and 6:
//  - Setup runs on the Lab's worker (vr/ring_wheel_atlas.h), never on the frame thread: shader compile (about 20 ms),
//    root signature, pipeline, three slots, both atlases twice (front and back), one upload staging, the query heap and a
//    persistently mapped readback. Ready() is its release store; the frame thread reads it with acquire.
//  - Atlases: the worker writes a packed image into the mapped staging (Stage); the frame thread's Pump copies at most 1 MB
//    a frame into the back texture and flips it in after the last slice. Nothing is created after Setup.
//  - Draw never waits: the slot it would record into still in flight (GetCompletedValue only) = Busy; the caller
//    re-submits the last image and keeps the compose due (ComposePolicy::Done(false)). A closed wheel never calls it.
//  - Output is premultiplied (ONE, INV_SRC_ALPHA, color and alpha) for a layer with XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT.
//  - Window() hands the CPU and GPU cost of every compose since the last call (mean, max, p95) to the E8 10 s line.
//  - Release waits on the Lab's own fence once, at most `waitMs`; false = still in flight, keep it and free in OnUiReset.
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct ID3D12Device; struct ID3D12CommandQueue; struct ID3D12Resource; struct ID3D12Fence;
namespace framevr::ringwheel {
struct Instance; struct ContentRect;
namespace shot { struct Raw; }   // steamvr/ring_wheel_shot.h
// Review R6 X5: a millisecond clock with sub-millisecond steps (QueryPerformanceCounter) for the engine's timers (the 350 ms
// tap limit, the 80 ms latch, the 30 ms jolt allowance); GetTickCount64 moves in about 15.6 ms steps.
double NowMs();
enum class AtlasKind { Icons = 0, Glyphs = 1, FxTex = 2 };
// A packed atlas: AtlasTexPx square; icons RGBA8 premultiplied with IconMips mips, glyphs RG8 (fill, outline), one mip;
// FxTex (stage 3a) FxTexPx square R8 with FxTexMips mips, the game's effect textures (white art, value = alpha).
// Rows tight (px * bpp bytes), mips one after another.
struct AtlasImage {
    AtlasKind kind = AtlasKind::Icons;
    int px = 0, bpp = 4, mips = 1;
    std::vector<uint8_t> data;
    size_t MipOffset(int mip) const { size_t o = 0; for (int i = 0; i < mip; ++i) { const size_t s = size_t(px >> i); o += s * s * size_t(bpp); } return o; }
};
// A screenshot's pixels, in the readback buffer the GPU has filled (Compose::Shot waited for that copy). It holds that
// buffer and nothing else of the compose pass, which may be freed first (the Lab going Off with a save pending).
// Round 2026-11-12 Job B: reading 7 MB out of it cost the frame about 2 ms, so the save worker does it (Read).
class ShotRows {
public:
    ShotRows() = default;
    ~ShotRows() { Drop(); }
    ShotRows(const ShotRows&) = delete;
    ShotRows& operator=(const ShotRows&) = delete;
    bool Has() const { return buffer != nullptr; }
    // Any one thread, once: the rows copied out, the buffer let go. False (and `out` empty): nothing held, the buffer could
    // not be mapped, or no memory.
    bool Read(shot::Raw& out);
private:
    friend class Compose;
    void Drop();
    ID3D12Resource* buffer = nullptr;   // our own reference
    uint64_t offset = 0, bytes = 0;
    size_t rowPitch = 0;
    int w = 0, h = 0;
    bool bgra = false, srgb = false;
};
class Compose {
public:
    Compose();
    ~Compose();
    Compose(const Compose&) = delete;
    Compose& operator=(const Compose&) = delete;
    // Any one thread, once (the Lab's worker). `format` a DXGI_FORMAT: R8G8B8A8 / B8G8R8A8, UNORM or _SRGB (sRGB targets get
    // linear colors; the hardware encodes).
    bool Setup(ID3D12Device* device, ID3D12CommandQueue* queue, int format, void (*say)(const char*));
    bool Ready() const { return ready.load(std::memory_order_acquire); }
    // Worker: the staging is free (no atlas staged or uploading).
    bool StagingFree() const { return Ready() && stage.load(std::memory_order_acquire) == 0; }
    // Worker: copies `image` into the mapped staging and hands it to the frame thread. False when not free or malformed.
    bool Stage(const AtlasImage& image);
    // Frame thread, once a frame while the Lab is On: at most 1 MB of a staged atlas; the bytes copied (0: nothing to do or busy).
    size_t Pump();
    bool HasAtlas(AtlasKind k) const;
    unsigned Flips(AtlasKind k) const;   // atlases flipped in so far
    size_t MaxSliceBytes() const;        // the largest Pump so far
    // R8 match stage 1a perf check (frame-budget.md): Setup's shader compile time and the pixel shader's instruction count
    // (D3DReflect; 0 when unavailable). The preview holds both under a budget.
    // compileMs: the shaders' load or compile and both pipelines' creation; warmMs: the warm-up draws (-1: not run).
    struct ShaderStats { double compileMs = 0, warmMs = 0; unsigned psInstructions = 0, fxInstructions = 0; bool embedded = false; };   // fx: PSFx (the effects); embedded: the offline blob, no compile
    // Perf 1b finding 1: with no offline blob, Setup compiles the effects as a placeholder (they draw nothing) and the
    // worker compiles PSFx after its first atlas (FinishEffects), then swaps it in. Worker only.
    bool EffectsPending() const;
    bool FinishEffects(void (*say)(const char*));
    ShaderStats Stats() const;
    // Frame thread: a Draw now would get a slot (no wait). The Lab asks before it acquires a swapchain image, so a busy
    // ring never acquires one it cannot draw.
    bool CanDraw() const;
    enum class Result { Composed, Busy, NotReady };
    // Clears `content` (texels from the target's top left) and draws `n` instances (at most MaxDraw) into it.
    // `target` in D3D12_RESOURCE_STATE_RENDER_TARGET, and left in it.
    Result Draw(ID3D12Resource* target, const ContentRect& content, const Instance* list, int n);
    // Frame thread, a screenshot's frame only (round 2026-11-08 item 4). A swapchain image is no copy source, so the list is
    // drawn again into a texture made for this shot and copied to a readback buffer made with it: first every compose in
    // flight finishes (at most `waitMs`), then the copy (at most `waitMs` again). Null when done; else why not, and resources
    // the GPU may still hold are kept until its fence passes. Nothing here runs without a screenshot.
    // Round 2026-11-12 Job B: the frame thread stops there, `out` holding the filled buffer. Copying its rows out
    // (ShotRows::Read), the picture (shot::Picture: the sRGB and straight-alpha math) and the PNG are the save worker's.
    const char* Shot(const ContentRect& content, const Instance* list, int n, unsigned waitMs, ShotRows& out);
    uint64_t Submitted() const;     // the Lab fence's last value
    uint64_t Completed() const;     // the Lab fence's completed value
    unsigned SkippedBusy() const;
    struct Window { unsigned composes = 0, gpuSamples = 0; double cpuMean = 0, cpuMax = 0, cpuP95 = 0, gpuMean = 0, gpuMax = 0, gpuP95 = 0; };
    Window TakeWindow();            // every compose since the last call (GPU: those completed), then reset
    bool Release(unsigned waitMs = 16);
    // The frame thread, at the Off edge before the worker is told (perf's X1 bound): true once every compose is done on the
    // GPU, waiting on the fence's event at most `waitMs` (no polling, so no 15.6 ms Sleep step past the bound).
    bool WaitIdle(unsigned waitMs);
    unsigned PreciseWaits() const;   // WaitIdle waits bounded by a high-resolution timer (else the 15.6 ms tick rounds them)
    // The frame thread, at the Off edge before the worker is told (perf's X6 check): the fence and the value the last compose
    // signals, the fence AddRef'd for the caller (who Releases it), so the frame thread can see the GPU finish without
    // touching this Compose again while the worker frees it. Not Ready (nothing ever drawn): null.
    ID3D12Fence* MarkLast(uint64_t& value) const;
    struct Impl;   // public for the file's helpers (Warm)
private:
    std::unique_ptr<Impl> impl;
    std::atomic<bool> ready{false};
    std::atomic<int> stage{0};   // 0 free, 1 staged (worker done), 2 uploading (frame thread)
};
// The compose shader's exact source and its FNV-1a 64 hash (tools/gen_ring_wheel_shader.py embeds the blob with that hash).
std::string ComposeShaderSource();
unsigned long long ShaderSourceHash(const std::string& s);
// Tests only (ring-wheel-preview --fallback): Setup ignores the offline blob, as a build without fxc would.
extern bool ComposeForceCompile;
}  // namespace framevr::ringwheel
