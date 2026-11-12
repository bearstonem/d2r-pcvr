// World sound (D2R 3.3.93787, FMOD Ex linked into D2R.exe): the game's own room echo,
// raised for the headset, and a log of which sounds the game starts.
//
// What the game does by itself (found in its code and data, none of it the mod's):
//  - Reverb. D2Client's sound environment switch (1FFCE0) reads the level's
//    soundenviron.txt row and hands two TEAXINFO (effects, voices) to
//    D2SoundEAXSetParams (83D440), which loads the EAX preset and eight numbers into the
//    reverb nodes soundchannels.json puts on the channels "sfx/eax" and "vo/eax". Every
//    world channel is a child of those two: monsters, combat, footsteps, objects, items,
//    skills, speech.
//  - Footsteps. The hero's own play on "sfx/ambient/footsteps-player", everyone else's on
//    "sfx/ambient/footsteps_hd", by the floor's material (soundenviron.txt Material 1 / 2,
//    FootstepMaterial on the HD tiles), with their own slider in the game's sound options.
//
// What this file adds, both through the loader (byte-checked, at 3.3.93787's addresses):
//  - D2SoundEAXSetParams, hooked: the two TEAXINFO go on with their Room level raised
//    (and at Strong a longer decay) - steamvr/world_sound.h. The game's numbers are kept,
//    so a change of the Echo setting is sent again from the game's UI thread at once.
//  - FMOD4SoundEngine::PlayAudioInstance (157CB90), hooked to count: the channel of every
//    sound the game starts, for one "Frame audio" line every 10 s. Nothing is changed.
//    The channel names come from D2Sound's channel table (246CA30, 0x98 a row), whose
//    place the table's own code vouches for (83EDBB).
//
// [audio] echo (1; VR Settings > Graphics > Environment > Echo), sound_log (1).
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <D2RLPlugin/api.h>

#include "sigscan.h"
#include "settings_menu.h"
#include "../steamvr/memory.h"
#include "../steamvr/world_sound.h"

namespace worldsound {
namespace {
namespace ws = framevr::worldsound;
using ws::Reverb;

void (*g_log)(const char*) = nullptr;
void Log(const char* s) { if (g_log) g_log(s); }

const D2RL::PluginContext* g_ctx = nullptr;
const D2RL::ThreadService* g_threads = nullptr;

// D2SoundEAXSetParams(const TEAXINFO& effects, const TEAXINFO& voices). Its two callers
// (1FFE5F, 2010AF) use no return value; whatever is in RAX is handed back as it came.
inline constexpr uint64_t ReverbRva = 0x83d440;
inline constexpr uint8_t ReverbSig[] = {0x4c,0x8b,0xdc,0x49,0x89,0x5b,0x20,0x55,0x56,0x57,0x48,0x81,0xec,0xa0,0x00,0x00,0x00};
using ReverbFn = uintptr_t (*)(const Reverb*, const Reverb*);
ReverbFn g_reverbOrig = nullptr;
bool g_reverbIn = false, g_reverbTold = false;

// FMOD4SoundEngine::PlayAudioInstance(engine, AudioInstanceRef*) - the "Failed to play
// AudioInstance:" function. The ref points at {?, use counts, implementation}; the
// implementation keeps its channel group's id at +8 (SetChannelGroup, 157D390, writes it).
inline constexpr uint64_t PlayRva = 0x157cb90;
inline constexpr uint8_t PlaySig[] = {0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x18,0x55,0x57,0x41,0x56,0x48,0x8b,0xec,0x48,0x83,0xec,0x60};
using PlayFn = uintptr_t (*)(void*, void*);
PlayFn g_playOrig = nullptr;
bool g_playIn = false, g_playTold = false;

// D2Sound's channel table: "imul rax, [count], 0x98 / mov rcx, [rows]" in the function
// that builds the channel groups (83EDA0). The exact bytes carry both addresses.
inline constexpr uint64_t TableCodeRva = 0x83edbb;
inline constexpr uint8_t TableCodeSig[] = {0x48,0x69,0x05,0x72,0xdc,0xc2,0x01,0x98,0x00,0x00,0x00,0x48,0x8b,0x0d,0x63,0xdc,0xc2,0x01};
inline constexpr uint64_t TableRowsRva = 0x246ca30, TableCountRva = 0x246ca38;
inline constexpr size_t TableRow = 0x98, RowName = 0x00, RowGroup = 0x88;

inline constexpr uint32_t MaxGroups = 128;
std::atomic<uint32_t> g_started[MaxGroups];
std::atomic<uint32_t> g_startedOther{0};

std::atomic<int> g_echo{ws::EchoDefault};
std::atomic<bool> g_soundLog{true}, g_headset{true};
std::atomic<bool> g_resendQueued{false};

std::mutex g_lock;            // the game's last numbers, and what was sent for them
Reverb g_gameEffects{}, g_gameVoices{};
bool g_have = false;
int g_sentEcho = -1;
Reverb g_loggedEffects{};
int g_loggedEcho = -1;
ULONGLONG g_loggedAt = 0;
unsigned g_unlogged = 0;

bool Same(const Reverb& a, const Reverb& b) { return std::memcmp(&a, &b, sizeof a) == 0; }

// One line per change of place or setting, at most one a second.
void TellReverb(const Reverb& effects, const Reverb& sentEffects, const Reverb& voices, const Reverb& sentVoices, int echo, const char* why) {
    const ULONGLONG now = GetTickCount64();
    {
        std::lock_guard lock(g_lock);
        if (g_loggedEcho == echo && Same(g_loggedEffects, effects)) return;
        if (g_loggedAt && now - g_loggedAt < 1000) { ++g_unlogged; return; }
        g_loggedEffects = effects; g_loggedEcho = echo; g_loggedAt = now;
    }
    unsigned skipped = 0;
    { std::lock_guard lock(g_lock); skipped = g_unlogged; g_unlogged = 0; }
    char b[520];
    snprintf(b, sizeof b, "Frame audio echo: %s (%s) - effects: %s; voices: %s%s",
             ws::EchoName(echo), why, ws::Describe(effects, sentEffects).c_str(), ws::Describe(voices, sentVoices).c_str(),
             skipped ? " (earlier changes within a second not listed)" : "");
    Log(b);
}

uintptr_t ReverbHook(const Reverb* effects, const Reverb* voices) {
    if (!effects || !voices) return g_reverbOrig(effects, voices);
    const Reverb gameEffects = *effects, gameVoices = *voices;   // the game reads both right after
    const int echo = g_echo.load();
    {
        std::lock_guard lock(g_lock);
        g_gameEffects = gameEffects; g_gameVoices = gameVoices; g_have = true; g_sentEcho = echo;
    }
    const Reverb sentEffects = ws::Boost(gameEffects, echo), sentVoices = ws::Boost(gameVoices, echo);
    TellReverb(gameEffects, sentEffects, gameVoices, sentVoices, echo, "the game set this place's reverb");
    return g_reverbOrig(&sentEffects, &sentVoices);
}

// The game's UI thread: the place's numbers again, at the Echo now chosen.
void __cdecl Resend(const D2RL::PluginContext*, void*) noexcept {
    g_resendQueued.store(false);
    Reverb gameEffects, gameVoices;
    const int echo = g_echo.load();
    {
        std::lock_guard lock(g_lock);
        if (!g_have || !g_reverbOrig || g_sentEcho == echo) return;
        gameEffects = g_gameEffects; gameVoices = g_gameVoices; g_sentEcho = echo;
    }
    const Reverb sentEffects = ws::Boost(gameEffects, echo), sentVoices = ws::Boost(gameVoices, echo);
    TellReverb(gameEffects, sentEffects, gameVoices, sentVoices, echo, "Echo changed in VR Settings");
    g_reverbOrig(&sentEffects, &sentVoices);
}

uintptr_t PlayHook(void* engine, void* instance) {
    // Guarded reads: the ref, its implementation, the implementation's channel group.
    uintptr_t ref = 0, impl = 0; uint32_t group = ~0u;
    if (instance && framevr::ReadMemory(&ref, instance, sizeof ref) && ref &&
        framevr::ReadMemory(&impl, reinterpret_cast<const void*>(ref + 0x10), sizeof impl) && impl &&
        framevr::ReadMemory(&group, reinterpret_cast<const void*>(impl + 8), sizeof group) && group < MaxGroups)
        g_started[group].fetch_add(1, std::memory_order_relaxed);
    else g_startedOther.fetch_add(1, std::memory_order_relaxed);
    return g_playOrig(engine, instance);
}

// The names of the game's channel groups by id, read from D2Sound's table. Empty when the
// table is not where 3.3.93787 has it, not filled yet, or does not look like one.
std::vector<std::string> ChannelNames() {
    std::vector<std::string> names;
    if (!d2rsig::Check(TableCodeRva, TableCodeSig, sizeof TableCodeSig)) return names;
    const uintptr_t rowsAt = d2rsig::Addr(TableRowsRva), countAt = d2rsig::Addr(TableCountRva);
    uintptr_t rows = 0; uint64_t count = 0;
    if (!rowsAt || !countAt || !framevr::ReadMemory(&rows, reinterpret_cast<const void*>(rowsAt), sizeof rows) ||
        !framevr::ReadMemory(&count, reinterpret_cast<const void*>(countAt), sizeof count) || !rows || count == 0 || count > MaxGroups) return names;
    names.resize(MaxGroups);
    for (uint64_t i = 0; i < count; ++i) {
        const uintptr_t row = rows + i * TableRow;
        uintptr_t text = 0; uint32_t group = ~0u; char name[64] = {};
        if (!framevr::ReadMemory(&text, reinterpret_cast<const void*>(row + RowName), sizeof text) || !text ||
            !framevr::ReadMemory(&group, reinterpret_cast<const void*>(row + RowGroup), sizeof group) || group >= MaxGroups) continue;
        // The name may end near a page's edge: a byte at a time, up to its 0.
        size_t n = 0;
        while (n < sizeof name - 1 && framevr::ReadMemory(&name[n], reinterpret_cast<const void*>(text + n), 1) && name[n]) ++n;
        name[n] = 0;
        if (ws::ChannelName(name, n)) names[group] = name;
    }
    return names;
}

ULONGLONG g_countsAt = 0;
bool g_namesTold = false;
std::vector<std::string> g_names;

// Every 10 s in which the game started a sound: which channels, the busiest first.
void TellCounts() {
    const ULONGLONG now = GetTickCount64();
    if (!g_countsAt) { g_countsAt = now; return; }
    if (now - g_countsAt < 10000) return;
    const double seconds = double(now - g_countsAt) / 1000.0;
    g_countsAt = now;
    uint32_t started[MaxGroups]; uint32_t total = 0;
    for (uint32_t i = 0; i < MaxGroups; ++i) { started[i] = g_started[i].exchange(0, std::memory_order_relaxed); total += started[i]; }
    const uint32_t other = g_startedOther.exchange(0, std::memory_order_relaxed);
    if (!total && !other) return;
    bool named = false;
    for (const auto& n : g_names) if (!n.empty()) { named = true; break; }
    if (!named) g_names = ChannelNames();   // filled once the game has built its channels
    std::vector<std::pair<std::string, int>> counts;
    int heroSteps = 0, otherSteps = 0;
    for (uint32_t i = 0; i < MaxGroups; ++i) {
        if (!started[i]) continue;
        const std::string name = i < g_names.size() && !g_names[i].empty() ? g_names[i] : "channel " + std::to_string(i);
        if (ws::HeroSteps(name)) heroSteps += int(started[i]);
        if (ws::OtherSteps(name)) otherSteps += int(started[i]);
        counts.emplace_back(name, int(started[i]));
    }
    if (other) counts.emplace_back("no channel read", int(other));
    if (!g_namesTold && !g_names.empty()) {
        g_namesTold = true;
        int known = 0; for (const auto& n : g_names) if (!n.empty()) ++known;
        char b[160]; snprintf(b, sizeof b, "Frame audio: the game's channel table read, %d channels named", known); Log(b);
    }
    char b[700];
    snprintf(b, sizeof b, "Frame audio: %u sounds started in %.0f s - your footsteps %d, others' footsteps %d - %s",
             total + other, seconds, heroSteps, otherSteps, ws::CountsText(std::move(counts)).c_str());
    Log(b);
}
// The Echo row, once VR Settings has read the profile; on a monitor the game's own mix stays.
void ReadEcho() {
    const auto settings = vrsettings::Read();
    if (settings.ready) g_echo.store(g_headset.load() ? ws::NormalizeEcho(settings.values[framevr::settings::ReverbEcho]) : int(ws::EchoGame));
}
}   // namespace

void SetLogger(void (*log)(const char*)) { g_log = log; }
// From D2RLoaderLoadPlugin. soundLog: [audio] sound_log; echo: [audio] echo as saved (VR
// Settings takes over once it has read the profile); headset: the VR platform - on a
// monitor the echo stays the game's own.
void Init(const D2RL::PluginContext* ctx, bool soundLog, float echo, bool headset) {
    g_ctx = ctx;
    g_soundLog.store(soundLog);
    g_headset.store(headset);
    g_echo.store(headset ? ws::NormalizeEcho(echo) : int(ws::EchoGame));
    if (!ctx || ctx->QueryService(&g_threads) != D2RL::ServiceQueryResult::Success) g_threads = nullptr;
    Log("Frame audio: the game's sound is FMOD Ex built into D2R.exe (no audio DLL, no exported audio API); "
        "its reverb and its footsteps are the game's own, reached at byte-checked addresses");
}
// Safe to call repeatedly from vrcam's settings round (one thread): hooks go in once
// their code is there, the Echo setting is read, and the counts line is written.
void Poll() {
    if (!g_ctx) return;
    static ULONGLONG readAt = 0, triedAt = 0, since = 0;
    static bool toldMissing = false;
    const ULONGLONG now = GetTickCount64();
    if (!since) since = now;
    if (!readAt || now - readAt > 250) { readAt = now; ReadEcho(); }
    // The game's code is decrypted as it first runs: looked for every 2 s until it is there.
    const bool wantPlay = g_soundLog.load();
    if ((!g_reverbIn || (wantPlay && !g_playIn)) && (!triedAt || now - triedAt >= 2000)) {
        triedAt = now;
        if (!g_reverbIn && d2rsig::Check(ReverbRva, ReverbSig, sizeof ReverbSig))
            g_reverbIn = d2rsig::Hook(ReverbRva, ReverbSig, sizeof ReverbSig, reinterpret_cast<void*>(&ReverbHook), reinterpret_cast<void**>(&g_reverbOrig));
        if (g_reverbIn && !g_reverbTold) {
            g_reverbTold = true;
            char b[200];
            snprintf(b, sizeof b, "Frame audio echo: the game's reverb setter found (D2SoundEAXSetParams); Echo is %s - each place's numbers follow as the game sets them",
                     ws::EchoName(g_echo.load()));
            Log(b);
        }
        if (wantPlay && !g_playIn && d2rsig::Check(PlayRva, PlaySig, sizeof PlaySig))
            g_playIn = d2rsig::Hook(PlayRva, PlaySig, sizeof PlaySig, reinterpret_cast<void*>(&PlayHook), reinterpret_cast<void**>(&g_playOrig));
        if (g_playIn && !g_playTold) { g_playTold = true; Log("Frame audio: counting the sounds the game starts by channel (a line every 10 s with sound; [audio] sound_log=0 turns it off)"); }
    }
    // Not found a minute in: say so once, and keep looking.
    if (!toldMissing && now - since > 60000 && (!g_reverbIn || (wantPlay && !g_playIn))) {
        toldMissing = true;
        char b[260];
        snprintf(b, sizeof b, "Frame audio: not found in this game build after a minute -%s%s; that part stays the game's own",
                 g_reverbIn ? "" : " the reverb setter at 83D440 (Echo does nothing)",
                 !wantPlay || g_playIn ? "" : " the play function at 157CB90 (no sound counts)");
        Log(b);
    }
    // Echo changed since the place's reverb was sent: again, on the game's UI thread.
    if (g_reverbIn) {
        bool stale = false;
        { std::lock_guard lock(g_lock); stale = g_have && g_sentEcho != g_echo.load(); }
        if (stale && g_threads && g_threads->runOnUiThread && !g_resendQueued.exchange(true) &&
            g_threads->runOnUiThread(g_ctx, &Resend, nullptr) != D2RL::Threads::Result::Success) g_resendQueued.store(false);
    }
    if (g_playIn) TellCounts();
}
}   // namespace worldsound
