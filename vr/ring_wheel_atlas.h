#pragma once
// Lab Ring wheel (T025, build step R5): the atlases (plan E4, E2 text; frame-budget section 2). Everything here runs on the
// Lab's one worker thread, never on the frame thread:
//  - the compose pass's Setup (shader compile, GPU objects; perf R4 item 1),
//  - the glyph atlas: upstream's menu fonts (Cinzel 700, Alegreya Sans 500 / 700; SIL OFL) at the render scale through
//    DirectWrite's grayscale glyph analysis, the 2.5 px dark outline baked in (fill red, outline green), and the metrics
//    the text layout measures with (steamvr/ring_wheel_text.h),
//  - the icon atlas: the player's own skill icons from upstream's local icon cache (interaction::icons, decoded on its own
//    worker), each pre-scaled to its cell's art, premultiplied, 4 mips (2 x 2 box in linear light). A missing icon is
//    listed, and its seat draws L1's initials disc.
// Each image goes to the GPU through Compose::Stage; the frame thread's Compose::Pump copies it in 1 MB slices.
// A build runs once per change of class, seats or render scale (Want with the same request: nothing).
#include "ring_wheel_compose.h"
#include "../steamvr/ring_wheel_draw.h"
#include <filesystem>
#include "../steamvr/interaction_icons.h"
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace framevr::ringwheel {
struct Fonts {
    std::array<std::vector<uint8_t>, text::Faces> face;
    bool Ok() const { for (const auto& f : face) if (f.empty()) return false; return true; }
};
// steamvr/assets/menu/fonts (the preview), or the DLL's RCDATA D2RVR_FONT_* (vr_settings_assets.rc; the game).
bool FontsFromFolder(const std::wstring& folder, Fonts& out);
bool FontsFromModule(void* module, Fonts& out);
// The glyph atlas at the render scale: printable ASCII, Latin-1 and the ellipsis for every text style.
bool BuildGlyphs(const Fonts& fonts, int scalePct, AtlasImage& out, text::Metrics& m, std::string& error);
// The icon atlas: `ids[i]` the skill id for seat i (-1 none). `source` returns a decoded icon or null (not cached yet, or
// none); `settle` waits for the decodes `source` queued (interaction::icons::Wait).
using IconSource = std::function<std::shared_ptr<const interaction::icons::Image>(int skill)>;
// keyed, despeckled (T036): the icons cut clean (steamvr/ring_wheel_unbake.h) and the pixels the cleanup pass changed.
struct IconPack { std::vector<int16_t> cellOf; std::vector<int> missing; int wanted = 0, found = 0, scalePct = 100, keyed = 0, despeckled = 0; };
// variant: the icon Labs' remaps On (Request::iconVariant): a remapped icon keeps its own art (unbake::Keyed).
void BuildIcons(const int* ids, int n, int scalePct, const IconSource& source, const std::function<void()>& settle, AtlasImage& out, IconPack& pack, uint32_t variant = 0);
// R8 match stage 3a: the game effect texture atlas (AtlasKind::FxTex) from upstream's spell sprite cache file (sprites.ssp,
// steamvr/spell_sprites.h): the fire plates 03 and 04, the smoke atlas, the arc flipbook and the glow card, BC4 decoded on
// the CPU, packed and mipped. `served`: how many of the kit's 8 fx-tex.js keys it stands for (7: no curved shockwave in the
// cache; 0 on any failure, `why` says why). Read-only; nothing written.
bool BuildFxTex(const std::filesystem::path& cacheFile, AtlasImage& out, int& served, std::string& why);
// One BC4 (unorm) 4 x 4 block into 16 values (tests).
void DecodeBc4Block(const uint8_t* block, uint8_t out[16]);
// The spell sprite cache next to the plugin (d2r_vr_spell_sprites\sprites.ssp), as elemental_hands_renderer finds it.
std::filesystem::path SpellSpriteCache();

class AtlasWorker {
public:
    struct Request {
        int classId = -1, scalePct = 150, n = 0;
        std::array<int, MaxSeats + ExtraIcons> ids{};   // the seats, then the Gestures page's other cards (round 2026-11-08 item 6)
        uint32_t iconVariant = 0;   // the icon Labs' remaps (IconVariant, headset fixes 2026-11-15): a change is a new build
        bool operator==(const Request&) const = default;
    };
    struct Built {
        Request request;
        IconPack icons;
        text::Metrics metrics;
        unsigned iconFlip = 0, glyphFlip = 0;   // usable once Compose::Flips reaches these
        double glyphMs = 0, iconMs = 0;
    };
    AtlasWorker() = default;
    // Review R5 K3: never a blocking join here (it may run at DLL unload, under the loader lock). A worker that exited is
    // joined; one still running is detached (the Lab holds its worker on the heap and never destroys it while running).
    ~AtlasWorker() { if (thread.joinable()) { if (exited.load()) thread.join(); else thread.detach(); } }
    AtlasWorker(const AtlasWorker&) = delete;
    AtlasWorker& operator=(const AtlasWorker&) = delete;
    // Frame thread, at the session or the Lab's On edge: starts the worker, which runs compose->Setup first.
    void Start(Compose* compose, ID3D12Device* device, ID3D12CommandQueue* queue, int format, Fonts fonts, IconSource source,
               std::function<void()> settle, void (*say)(const char*));
    // Before Start: where the game effect texture cache is read from (default SpellSpriteCache(); the preview: a local copy;
    // an empty path: none, level 4).
    void SetFxTexCache(std::filesystem::path file) { fxTexFile = std::move(file); fxTexSet = true; }
    // Frame thread, on a change only (class, seats, render scale): a lock and a notify.
    void Want(const Request& r);
    // The newest finished build whose atlases the frame thread's Pump has flipped in, or null (a lock: call it when a
    // flip count changed).
    std::shared_ptr<const Built> Current(const Compose& compose);
    unsigned Builds() const { return builds.load(); }   // icon builds finished (staged)
    bool Failed() const { return failed.load(); }
    // Frame thread, the Lab's Off edge (perf R5 must-fix): never waits. The worker leaves its loop (a build in progress
    // stops at its next stage), then runs `onExit` on its own thread (the Lab frees the compose there, after the GPU).
    void Retire(std::function<void()> onExit);
    bool Exited() const { return exited.load(); }
    // Joins the worker (instant once Exited); OnUiReset and a new Start call it.
    void Stop();
private:
    void Run();
    void Loop();
    std::function<void()> onExit;
    std::atomic<bool> exited{false};
    Compose* compose = nullptr;
    ID3D12Device* device = nullptr;
    ID3D12CommandQueue* queue = nullptr;
    int format = 0;
    Fonts fonts;
    IconSource source;
    std::function<void()> settle;
    void (*say)(const char*) = nullptr;
    std::thread thread;
    std::mutex mutex;
    std::condition_variable wake;
    bool stop = false, wanted = false;
    Request want;
    std::shared_ptr<const Built> latest;
    std::atomic<unsigned> builds{0};
    std::atomic<bool> failed{false};
    std::filesystem::path fxTexFile; bool fxTexSet = false;
};
}  // namespace framevr::ringwheel
