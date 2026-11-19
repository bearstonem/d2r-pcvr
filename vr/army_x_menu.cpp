// Army X menu: glue around the pure engine steamvr/army_x_menu.h, called from the mod's own files (vr/army_x_orders.h):
//   - Input / Request (transport Input, around interactions.Step): while a hand wheel is open and an army is out, "Army X
//     menu" is one more entry in the engine's catalog, so Edit Slots on a hand button lists it (as it lists Draw glyph); it
//     is taken out again right after the Step. Picking it becomes the game's own clear of that button's skill (as Draw
//     glyph's pick does); on the reply's Applied the slot is kept in d2r_pcvr.ini, [wheel] army_x_menu_button.
//   - Press (transport Input, before the commander button block): the held button opens the cross, the hand's movement
//     points, the let go gives the order through the commander path (companionai::ExecuteCommanderSkill): one call site,
//     Give() below. A game skill or a Draw glyph put on that button takes it back: the menu is on no button again.
//   - Layer (transport EndPair): the frame's quads, from the menu's regions of the static image (painted once a session
//     by Paint), all or none. No swapchain, texture or per-frame allocation of its own.
//   - Picks (vrcam's aim tick): the monster each hand aims at by the mod's aim rules, and the eyes', kept for the press.
//   - Attack my target and Move and hold are summons only: the let go queues them (xm::FocusQueue) and ArmyTick gives them
//     on the game thread under the coordinator's lock (SetPriorityTarget / AnchorAt), never the mercenary's order. With no
//     monster aimed the hand picks one with its ray (trigger locks, grip cancels); Move and hold's spot is picked on the
//     ground the same way, with the tactical chord's preview ring and its 40 sub-tile reach.
//   - GameTick (the game thread, after the army step, no lock held): the priority target now, for the mark's life; and the
//     scan round a held spot (Defend), for the next army tick.
// While the menu is on no button every handler returns at its first test.
#include <windows.h>
#include <wincodec.h>
#include <openxr/openxr.h>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>
#include "companion_ai.h"
#include "army_x_orders.h"
#include "target_lock.h"
#include "../steamvr/army_x_menu.h"
#include "../steamvr/panel_atlas.h"
#include <algorithm>
#include "../steamvr/interactions.h"
#include "../steamvr/gameplay.h"
#include "../steamvr/hand_controls.h"
#include "../steamvr/skill_slots.h"
#include "../steamvr/interaction_icons.h"
#include "../steamvr/logging.h"
#include "../steamvr/telegraph.h"
#include "../steamvr/army_x_hunt.h"
#include "melee.h"
#include <memory>

namespace {
namespace seam = framevr::armyseam;
namespace cfg = framevr::armyseam::cfg;
namespace xm = framevr::xmenu;
namespace ia = framevr::interaction;

// The hand button the menu is on: its slot, -1 none. Kept in d2r_pcvr.ini beside the plugin, [wheel] army_x_menu_button
// (the slot + 1; 0 none), as the quick item is. Read once; written when Edit Slots puts the menu on a button and when a
// game skill or a Draw glyph takes that button.
std::atomic<int> g_button{-2};   // -2: not read yet
std::wstring IniPath() {
    HMODULE module = nullptr; wchar_t path[32768]{};
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(&IniPath), &module) ||
        !GetModuleFileNameW(module, path, DWORD(std::size(path)))) return {};
    std::wstring ini = path;
    const auto slash = ini.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return {};
    ini.resize(slash + 1); ini += L"d2r_pcvr.ini";
    return ini;
}
int ReadButton() {
    int b = g_button.load(std::memory_order_relaxed);
    if (b != -2) return b;
    const std::wstring ini = IniPath();
    b = ini.empty() ? -1 : xm::SlotOfButton(float(GetPrivateProfileIntW(L"wheel", L"army_x_menu_button", 0, ini.c_str())), framevr::skills::Count);
    int unread = -2;
    g_button.compare_exchange_strong(unread, b, std::memory_order_relaxed);
    return g_button.load(std::memory_order_relaxed);
}
void SaveButton(int slot) {
    g_button.store(slot, std::memory_order_relaxed);
    const std::wstring ini = IniPath();
    if (ini.empty()) return;
    wchar_t value[16]; swprintf_s(value, L"%d", slot + 1);
    WritePrivateProfileStringW(L"wheel", L"army_x_menu_button", value, ini.c_str());
}

// The cross's quads, handed from the Input thread's Press to EndPair's Layer without a lock (a sequence counter: odd while
// written; a reader that sees it change keeps the last list it read).
// n and open are published beside it, so Layer returns before copying when there is nothing to draw; open counts
// the opens, so the no-room line is said once an open.
struct Shared {
    std::atomic<uint32_t> seq{0};
    std::atomic<int> n{0};
    std::atomic<uint32_t> open{0};
    xm::DrawList list;
};
Shared shared;
void Publish(const xm::DrawList& d) {
    const uint32_t s = shared.seq.load(std::memory_order_relaxed);
    shared.seq.store(s + 1, std::memory_order_release);
    std::atomic_thread_fence(std::memory_order_release);
    shared.list = d;
    shared.n.store(d.n, std::memory_order_relaxed);
    shared.seq.store(s + 2, std::memory_order_release);
}

struct State {
    xm::Controller ctl;
    float aim[2]{};               // the hand's aimed floor cell at the press (Hold ground's spot)
    xm::Aimed target;             // the monster the hand (else the eyes) aimed at the press (Attack my target's), and when (GetTickCount64)
    double targetAtMs = 0;
    double openedMs = 0;
    // Attack my target out on the game thread (g_focus): its log line's start and the hand to buzz.
    char pendingHead[400] = "";
    int pendingHand = 0;
    bool drawn = false;           // the last published list was not empty
    // The bind (the hand wheel's Edit Slots pick or the ring wheel's hold to assign): its request while the reply is awaited.
    xm::BindWatch bind;
    bool injected = false;        // the catalog entry is in the engine's snapshot right now
    uint64_t boundAtMs = 0;       // GetTickCount64 of the last bind (a snapshot from before it may still show the old skill)
    // Attack my target's targeting mode and the mark after it.
    xm::Targeting targeting;
    xm::Mark mark;
    double aimStartMs = 0, missedToldMs = -1e9;
    char aimHead[160] = "";       // the let go's line start, for the mode's end line
    // Move and hold's spot pick: the same targeting mode on the ground (candidate 1: a floor cell within reach).
    xm::Targeting ground;
    double groundStartMs = 0;
    bool ringShown = false;       // our preview ring is on telegraph's CompanionMark (cleared when the pick ends)
    int wasInReach = -1;          // the last frame's reach (-1: no floor), for the chord's boundary click
    float groundAt[2]{};          // the spot given (for the outcome's line)
    double groundHeroDist = 0;
};
State st;
// The hands' aimed monsters (Picks, vrcam's UpdateThread, each update while the switch is On), read at a press (transport's
// Input thread). A lock of its own, uncontended: the press takes it once an open. [0] right hand, [1] left, [2] the eyes (only
// while the outline would show the eyes' monster).
std::mutex g_picksLock;
xm::Aimed g_picks[3];
// The hostile scan those picks came from (the outline's monsters and cells, targetlock), copied under the same lock: the
// targeting ray's candidates and where the mark stands. at: GetTickCount64 of the scan (0: none).
targetlock::Snapshot g_scan;
constexpr uint64_t ScanFreshMs = 500;   // as vrcam takes a scan for the picks (mobaim::Tick)
// The commander's priority target now, from the game thread's tick (GameTick); the mark lives while it is the mark's.
std::atomic<uint32_t> g_priority{0};
std::atomic<uint64_t> g_priorityAt{0};   // GetTickCount64 of that tick (0: none yet); older than Mark::StaleMs: stale
// This session's static picture carries the mark's frames (Paint; read by Layer).
std::atomic<bool> g_markPainted{false};
// Attack my target and Move and hold, posted by the let go (transport) and given by ArmyTick (the game thread, the army step's
// Moves) only.
xm::FocusQueue g_focus;
// Defend (game thread only: ArmyTick): the X menu's own anchor and the monster it set.
xhunt::Defend g_defend;
// Defend's scan, game thread only: ArmyTick (under g_tacticalLock, no game call) asks for it with the
// spot, the radius and the summon to see from (want); GameTick (GameTick, the same thread, no lock, right after the
// army step) scans and leaves the result (scan) for the next ArmyTick. n -1: none.
struct DefendWant { uint32_t viewer = 0; xhunt::Vec2 spot{}; float radius = 0; uint64_t atMs = 0; };
struct DefendScan { xhunt::Foe foes[xhunt::DefendScanMax]; int n = -1; xhunt::Vec2 spot{}; uint64_t atMs = 0; };
DefendWant g_defendWant;
DefendScan g_defendScan;
constexpr uint64_t DefendWantMs = 1000;        // an ask older than this (no army tick since: Fall out, no army) is not scanned
constexpr uint64_t DefendScanFreshMs = 1000;   // a scan older than this is not used (the ticks are about 250 ms apart)
// Hold here goes through the commander path on transport's thread (ExecuteCommanderSkill: its spot clamped to 14 sub-tiles
// past 16); its anchor is claimed as the X menu's on the next army tick (GetTickCount64 of the Give; 0 none).
std::atomic<uint64_t> g_holdClaimMs{0};
constexpr uint64_t ClaimMs = 1500;
// The army step's last tick through the seam (GetTickCount64; 0 none): an order's time-out says why it was not taken.
std::atomic<uint64_t> g_armyTickMs{0};
// This session's static picture carries the sheet (set in Paint, on transport's thread before the upload; read by Layer).
std::atomic<bool> g_sheetPainted{false};
static_assert(xm::atlas::Width == framevr::panelatlas::ArmyXMenu.w && xm::atlas::Height == framevr::panelatlas::ArmyXMenu.h, "the sheet fills its region");
static_assert(xm::atlas::MarkFrame == framevr::panelatlas::ArmyXMarkFrame && xm::atlas::MarkFrames == framevr::panelatlas::ArmyXMarkFrames &&
              xm::atlas::MarkFrame == framevr::panelatlas::ArmyXMark.w && xm::atlas::MarkStripH == framevr::panelatlas::ArmyXMark.h,
              "the mark's strip fills its region, frame for frame");
namespace pa = framevr::panelatlas;
static_assert(pa::Inside(pa::ArmyXMenu, pa::StaticWidth, pa::StaticHeight) && pa::Inside(pa::ArmyXMark, pa::StaticWidth, pa::StaticHeight) &&
              pa::Apart(pa::ArmyXMenu, pa::KeyboardCaps) && pa::Apart(pa::ArmyXMenu, pa::KeyboardCapsLit) && pa::Apart(pa::ArmyXMenu, pa::Stash(3)) &&
              pa::Apart(pa::ArmyXMark, pa::Pointer) && pa::Apart(pa::ArmyXMark, pa::Compass) && pa::Apart(pa::ArmyXMark, pa::Gameplay) && pa::Apart(pa::ArmyXMark, pa::GrabHint) &&
              pa::Apart(pa::ArmyXMark, pa::ArmyXMenu), "the menu's two regions sit in free space of the static image");

const char* WhyName(xm::Release r) {
    switch (r) {
        case xm::Release::Center: return "let go in the center";
        case xm::Release::Early: return "let go inside the press settle";
        case xm::Release::NoArmy: return "not a summoner class now";
        case xm::Release::Past: return "let go past the circle's reach (X menu feel > Parent depth)";
        default: return "";
    }
}
void PathText(char* out, size_t n, const xm::Hover& h) {
    if (h.parent < 0) { std::snprintf(out, n, "the center"); return; }
    const auto& b = xm::Layout[size_t(h.parent)];
    if (h.sub == xm::SubPast) std::snprintf(out, n, "%s > past its reach", b.name);
    else if (h.sub < 0 || h.sub >= b.count) std::snprintf(out, n, "%s", b.name);
    else std::snprintf(out, n, "%s > %s", b.name, xm::OrderLabel(b.orders[h.sub]));
}

// The one call site of an order: the commander path, with the spot aimed at the press.
bool Give(seam::ArmyXPressFrame& f, int order) { return f.order(order, st.aim[0], st.aim[1]); }
// The one call site of Attack my target and Move and hold, summons only: the army half of the commander's
// IssueTacticalOrder, on the coordinator it already holds under g_tacticalLock (the army step's Moves seam, the game
// thread), never the mercenary's order. Focus: ClearAnchor + SetPriorityTarget (companion_ai.cpp validates it each tick and lets it
// go when it dies); Anchor: AnchorAt (the formation re-centered on the spot, held there as the commander's Hold Ground / MoveAndHold
// hold it), the X menu's own anchor for Defend.
bool Apply(framevr::commander::SummonArmyCoordinator& c, const xm::Aimed& a, xm::FocusQueue::Kind k) {
    if (k == xm::FocusQueue::Kind::Anchor) {
        c.AnchorAt({a.x, a.y});
        g_defend.Own({a.x, a.y});
        return true;
    }
    if (!a.id) return false;
    c.ClearAnchor();
    c.SetPriorityTarget(a.id, {a.x, a.y});
    g_defend.Forget();   // the anchor is gone: nothing of ours to defend, and the player's focus is never touched
    return true;
}

void Buzz(seam::ArmyXPressFrame& f, int hand, const xm::Tuning& t) {
    if (t.issueMs > 0 && t.issueAmp > 0 && f.pulse) f.pulse(hand, t.issueAmp, uint64_t(t.issueMs * 1e6));
}
// Attack my target could not focus a monster: Spearhead wedge (the Aggressive stance) through the commander path instead, its
// toast "No target" over the wedge's name when there was no monster. head: the line's start (hand, slot, time, path, order);
// id: the monster it was for (0: none).
enum class FallWhy { NoTarget, TacticalOff, Refused, NotTaken };
// Why the game thread did not take an order within FocusQueue::TimeoutMs (the army step's seam runs only with an army out and not
// in Fall out; coordinator: a clear toast and line, never a silent drop). noArmy: no summon the army step tracks.
void NotTakenWhy(char* out, size_t n, bool& noArmy) {
    noArmy = ::companionai::ActiveSummonArmyCount() == 0;
    const uint64_t at = g_armyTickMs.load(std::memory_order_relaxed), now = GetTickCount64();
    if (noArmy) std::snprintf(out, n, "no army out (no summon the army step tracks)");
    else if (::companionai::IsSummonFreeRoam()) std::snprintf(out, n, "Fall out is on (the army step idles in Fall out: give Regroup or a formation first)");
    else std::snprintf(out, n, "the army step did not run for %.1f s (a load, a menu, or VR Settings' summon formations off)",
                       at && now >= at ? double(now - at) / 1000.0 : -1.0);
}
void Fallback(seam::ArmyXPressFrame& f, const char* head, FallWhy why, uint32_t id, int hand, double nowMs, const xm::Tuning& t) {
    const int given = xm::AttackFallback;
    const bool ok = Give(f, given);
    if (ok) Buzz(f, hand, t);
    st.ctl.Given(given, ok, nowMs, t, why == FallWhy::NoTarget);
    char reason[200];
    if (why == FallWhy::NoTarget) std::snprintf(reason, sizeof reason, "no target");
    else if (why == FallWhy::TacticalOff) std::snprintf(reason, sizeof reason, "VR Settings' Tactical commands are off");
    else if (why == FallWhy::Refused) std::snprintf(reason, sizeof reason, "monster %u refused", id);
    else {
        char whyNot[160]; bool noArmy = false;
        NotTakenWhy(whyNot, sizeof whyNot, noArmy);
        std::snprintf(reason, sizeof reason, "monster %u not taken by the game thread within %.0f s: %.120s", id, xm::FocusQueue::TimeoutMs / 1000, whyNot);
    }
    char line[480];
    std::snprintf(line, sizeof line, "%s (%s, wedge: %s (commander %d)) %s", head, reason, xm::OrderLabel(given), given,
                  ok ? "given (the commander took it)" : "refused by the commander (formations off)");
    if (f.say) f.say(line);
}
// Each frame while Attack my target is out on the game thread: its outcome, once. Took: the buzz, the toast and the line;
// refused or not taken in time: the wedge (Fallback). off: the switch's Off call: a queued one is dropped; one the game thread
// already took is only logged.
// Move and hold's outcome: Took: the buzz, the toast and the line; not taken in time: "Move and hold" over "No army" or
// "Canceled" and the line with why (no fallback order).
void SettleSpot(seam::ArmyXPressFrame& f, xm::FocusQueue::Outcome o, bool quiet, double nowMs, const xm::Tuning& t) {
    const xm::Aimed a = g_focus.target;
    char line[480];
    if (o == xm::FocusQueue::Outcome::Took) {
        if (!quiet) { Buzz(f, st.pendingHand, t); st.ctl.Given(xm::MoveHold, true, nowMs, t); }
        const float r = cfg::Gated(cfg::rules.defendRadius);
        char defend[96];
        if (r > 0) std::snprintf(defend, sizeof defend, "defended within %.1f sub-tiles (Defend radius, ini)", r);
        else std::snprintf(defend, sizeof defend, "Defend radius Off: the commander's hold");
        std::snprintf(line, sizeof line, "%s (spot %.1f %.1f, %.1f sub-tiles from you; summons only: the coordinator's AnchorAt on the game thread, the mercenary not ordered; %s) given%s",
                      st.pendingHead, a.x, a.y, st.groundHeroDist, defend, quiet ? "; taken off its button while it was being given: no buzz or toast" : "");
    } else {
        char whyNot[160]; bool noArmy = false;
        NotTakenWhy(whyNot, sizeof whyNot, noArmy);
        if (!quiet) st.ctl.Toast(noArmy ? xm::atlas::LabelNoArmy : xm::atlas::LabelCanceled, nowMs, t, xm::atlas::LabelMoveHold);
        std::snprintf(line, sizeof line, "%s (spot %.1f %.1f), nothing given: not taken by the game thread within %.0f s: %.160s%s", st.pendingHead, a.x, a.y,
                      xm::FocusQueue::TimeoutMs / 1000, whyNot, quiet ? " (taken off its button)" : "");
    }
    if (f.say) f.say(line);
}
void Settle(seam::ArmyXPressFrame& f, double nowMs, const xm::Tuning& t, bool off) {
    const xm::Aimed a = g_focus.target;   // written by Post before it was queued; Drain never writes it
    const bool spot = g_focus.kind == xm::FocusQueue::Kind::Anchor;   // Move and hold's (the same: written before it was queued)
    char line[320];
    if (off) {
        st.mark.Clear();
        if (g_focus.Off() == xm::FocusQueue::OffResult::Dropped) {
            if (spot) std::snprintf(line, sizeof line, "%s (spot %.1f %.1f), nothing given (taken off its button before the game thread took it)", st.pendingHead, a.x, a.y);
            else std::snprintf(line, sizeof line, "%s (monster %u), nothing given (taken off its button before the game thread took it)", st.pendingHead, a.id);
            if (f.say) f.say(line);
            return;
        }
    }
    const auto o = g_focus.Poll(nowMs);
    if (o == xm::FocusQueue::Outcome::None) return;
    // Switched Off while the game thread was giving it: its outcome, after the next On, is only logged.
    const bool quiet = g_focus.TakeQuiet() || off;
    if (spot) { SettleSpot(f, o, quiet, nowMs, t); return; }
    if (o != xm::FocusQueue::Outcome::Took && st.mark.id == a.id) st.mark.Clear();   // not focused: no arrow over it
    if (o == xm::FocusQueue::Outcome::Took) {
        if (!quiet) { Buzz(f, st.pendingHand, t); st.ctl.Given(xm::AttackTarget, true, nowMs, t); }
        else if (st.mark.id == a.id) st.mark.Clear();
        std::snprintf(line, sizeof line, "%s (monster %u%s at %.1f %.1f, the summon army's priority target, summons only: the mercenary not ordered; given on the game thread) given%s",
                      st.pendingHead, a.id, a.eyes ? " (the eyes' pick)" : "", a.x, a.y,
                      quiet ? "; taken off its button while it was being given: no buzz, toast or mark" : st.mark.id != a.id ? "" : "; the mark is over it");
    } else if (quiet) {
        std::snprintf(line, sizeof line, "%s (monster %u %s; taken off its button, no wedge)", st.pendingHead, a.id,
                      o == xm::FocusQueue::Outcome::Refused ? "refused" : "not taken by the game thread in time");
    } else {
        Fallback(f, st.pendingHead, o == xm::FocusQueue::Outcome::Refused ? FallWhy::Refused : FallWhy::NotTaken, a.id, st.pendingHand, nowMs, t);
        return;
    }
    if (f.say) f.say(line);
}

// A picture of steamvr/army_x_menu.rc (RCDATA `name`) as straight RGBA through `wic`; empty when it is missing or not
// wantW x wantH. The sheet (D2RVR_ARMY_X_SHEET): the session's static paint and the bind entry's icon (the icon worker's own
// WIC); the mark's strip (D2RVR_ARMY_X_MARK): the static paint.
void DecodePicture(IWICImagingFactory* wic, const wchar_t* name, UINT wantW, UINT wantH, std::vector<uint8_t>& sheet, UINT& w, UINT& h) {
    sheet.clear(); w = h = 0;
    HMODULE module = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(&DecodePicture), &module);
    const HRSRC res = FindResourceW(module, name, MAKEINTRESOURCEW(10));   // RT_RCDATA
    const DWORD bytes = res ? SizeofResource(module, res) : 0;
    auto* data = res ? static_cast<BYTE*>(LockResource(LoadResource(module, res))) : nullptr;
    IWICStream* stream = nullptr; IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr; IWICFormatConverter* conv = nullptr;
    if (wic && bytes && data &&
        SUCCEEDED(wic->CreateStream(&stream)) && SUCCEEDED(stream->InitializeFromMemory(data, bytes)) &&
        SUCCEEDED(wic->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnLoad, &decoder)) &&
        SUCCEEDED(decoder->GetFrame(0, &frame)) && SUCCEEDED(frame->GetSize(&w, &h)) && w == wantW && h == wantH &&
        SUCCEEDED(wic->CreateFormatConverter(&conv)) &&
        SUCCEEDED(conv->Initialize(frame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) {
        sheet.resize(size_t(w) * h * 4);
        if (FAILED(conv->CopyPixels(nullptr, w * 4, UINT(sheet.size()), sheet.data()))) sheet.clear();
    }
    for (IUnknown* p : {static_cast<IUnknown*>(conv), static_cast<IUnknown*>(frame), static_cast<IUnknown*>(decoder),
                        static_cast<IUnknown*>(stream)}) if (p) p->Release();
}
void DecodeSheet(IWICImagingFactory* wic, std::vector<uint8_t>& sheet, UINT& w, UINT& h) {
    DecodePicture(wic, L"D2RVR_ARMY_X_SHEET", UINT(xm::atlas::Width), UINT(xm::atlas::Height), sheet, w, h);
}
// Straight RGBA (rw x rh) premultiplied into region r of the static image (only inside it).
void PaintRegion(const seam::StaticAtlasFrame& f, framevr::panelatlas::Region r, const std::vector<uint8_t>& px) {
    static const xm::atlas::Premul premul;
    for (int y = 0; y < r.h; ++y) {
        uint8_t* out = &f.pixels[(size_t(r.y + y) * size_t(f.width) + size_t(r.x)) * 4];
        const uint8_t* in = &px[size_t(y) * size_t(r.w) * 4];
        for (int x = 0; x < r.w; ++x, out += 4, in += 4) premul.Texel(in, out, f.srgb);
    }
}

// ---- Attack my target's targeting mode and its mark ----
const char* EndName(xm::TargetEnd e) {
    switch (e) {
        case xm::TargetEnd::Grip: return "canceled by the grip";
        case xm::TargetEnd::Timeout: return "canceled: no lock in time";
        case xm::TargetEnd::Off: return "taken off its button";
        case xm::TargetEnd::Replaced: return "the X menu opened again";
        default: return "";
    }
}
// The scanned monster with this id, if the scan is fresh (its cells: the outline's).
bool Scanned(const targetlock::Snapshot& s, uint32_t id, float& x, float& y) {
    if (!id || !s.at || GetTickCount64() - s.at > ScanFreshMs) return false;
    for (int i = 0; i < s.count && i < framevr::targetcone::MaxTargets; ++i)
        if (s.targets[i].id == id) { x = s.targets[i].x; y = s.targets[i].y; return true; }
    return false;
}
}  // namespace

namespace armyxmenu {
int Button() { return ReadButton(); }
bool On() { return ReadButton() >= 0; }
// Before interactions.Step: Edit Slots on a hand button lists "Army X menu" (General / Items, as Draw glyph), with an army out.
void Input(seam::WheelInputFrame f) {
    auto* in = static_cast<ia::Input*>(f.engineIn);
    if (!in) return;
    auto& s = in->snapshot;
    st.injected = false;
    if (!armyxmenu::On() || f.wheelHand < 0 || !ia::HasSummonArmy(s) || s.skillCount >= s.catalog.size()) return;
    for (unsigned i = 0; i < s.skillCount && i < s.catalog.size(); ++i) if (s.catalog[i].id == xm::ItemId) return;
    ia::Skill item{};
    item.id = xm::ItemId; item.owner = -1; item.level = 1; item.known = item.available = true;
    item.directed = true;   // never in the GLYPHS page's picker (its skills need no target)
    std::snprintf(item.name, sizeof item.name, "Army X menu");
    s.catalog[s.skillCount++] = item;
    st.injected = true;
}
// After interactions.Step (after the ring wheel's row): the entry out again; a BindSkill for it, from Edit Slots or the ring
// wheel's hold to assign, made the commander's clear of the button's game skill; the reply kept.
void Request(seam::WheelRequestFrame f) {
    if (auto* in = static_cast<ia::Input*>(f.engineIn); in && st.injected) {
        auto& s = in->snapshot;
        if (s.skillCount > 0 && s.catalog[s.skillCount - 1].id == xm::ItemId) s.catalog[--s.skillCount] = {};
        st.injected = false;
    }
    const bool on = armyxmenu::On();
    if (!on) { st.bind.Forget(); return; }   // the Off call: a bind still waiting is forgotten
    if (!f.request || !f.reply) return;
    auto& r = *static_cast<ia::Request*>(f.request);
        if (st.bind.Take(r)) {   // the commander's own clear (interactionstate's BindSkill), as Draw glyph's pick
        char line[180];
        std::snprintf(line, sizeof line, "Frame army x menu: %s put it on slot %d (its game skill %d cleared first)", "Edit Slots", r.index, r.previousSkill);
        if (f.say) f.say(line);
    }
    int slot = -1;
    const auto done = st.bind.Reply(*static_cast<const ia::Reply*>(f.reply), slot);
    if (done != xm::BindWatch::Done::None) {
        const bool ok = done == xm::BindWatch::Done::Applied;
        if (ok) { SaveButton(slot); st.boundAtMs = GetTickCount64(); }
        char line[160];
        std::snprintf(line, sizeof line, "Frame army x menu: slot %d %s", slot,
                      ok ? "is the Army X menu button ([wheel] army_x_menu_button saved)" : "refused by the game: the button keeps what it had");
        if (f.say) f.say(line);
    }
}
// vrcam mobaim::Tick (Picks): each hand's aimed monster and its cells, stamped now (a pick older
// than xm::AimedFreshMs at a press is no target, so the key gate's missing Off call needs no re-arm).
void Picks(const seam::HandPicksFrame& f) {
    const auto* s = static_cast<const targetlock::Snapshot*>(f.snapshot);
    const double now = double(GetTickCount64());
    const uint32_t ids[3] = {f.hand[0], f.hand[1], f.eyes};
    xm::Aimed a[3];
    for (int k = 0; k < 3; ++k) {
        a[k].atMs = now;
        if (!s || !ids[k]) continue;
        for (int i = 0; i < s->count && i < framevr::targetcone::MaxTargets; ++i)
            if (s->targets[i].id == ids[k]) { a[k].id = ids[k]; a[k].x = s->targets[i].x; a[k].y = s->targets[i].y; break; }
    }
    std::lock_guard lock(g_picksLock);
    for (int k = 0; k < 3; ++k) g_picks[k] = a[k];
    if (s) g_scan = *s; else { g_scan.count = 0; g_scan.at = 0; }   // no fresh scan: no candidates, no mark drawn
}
// companion_ai's game-thread tick (GameTick; right after the army step, the same thread, no
// companion_ai lock held): the commander's priority target, kept for the mark's life; and Defend's scan round the X menu's spot
// S1/S2: never under g_tacticalLock, and seen from a summon by the spot, as the mercenary's hold sentry scans from the
// mercenary at its spot, not from the hero tens of sub-tiles away), for the next army tick.
void GameTick(seam::CompanionTickFrame f) {
    if (!armyxmenu::On()) return;
    const uint64_t now = GetTickCount64();
    g_priority.store(f.priorityTarget, std::memory_order_relaxed);
    g_priorityAt.store(now, std::memory_order_relaxed);
    armyxhunt::Sample();   // the fight back's look at each attacker (vr/army_x_hunt.cpp): its game reads, no lock held
    auto& w = g_defendWant;
    if (!w.viewer || !w.atMs || now < w.atMs || now - w.atMs > DefendWantMs) return;   // no army tick asked lately (Fall out, no army)
    uintptr_t game = 0, self = 0;
    const uintptr_t viewer = melee::LocalServer(game, self) ? melee::ServerUnit(game, 1, w.viewer) : 0;
    auto& s = g_defendScan;
    s.n = -1; s.atMs = now; s.spot = w.spot;
    if (!viewer) return;   // the summon is gone: no scan this tick
    melee::Hostile h[xhunt::DefendScanMax]{};
    const int n = melee::HostilesAround(h, xhunt::DefendScanMax, w.radius, w.spot.x, w.spot.y, viewer);
    for (int i = 0; i < n && i < xhunt::DefendScanMax; ++i) s.foes[i] = {h[i].id, h[i].x, h[i].y};
    s.n = n < 0 ? -1 : std::min(n, xhunt::DefendScanMax);
}
// The army step (vr/army_x_hunt.cpp Moves calls this first, on Moves: the game thread, under g_tacticalLock
// exclusive, right after the coordinator's Step; about 4 a second with an army out, formations on, not Fall out): a queued Attack
// my target or Move and hold is given here, and only here (Apply: summons only); Hold here's anchor claimed; then Defend, from
// the last tick's scan (no game call here: the hook contract, army_x_orders.h).
void ArmyTick(seam::ArmyMovesFrame& f) {
    auto& c = f.coord;
    const bool on = armyxmenu::On();
    g_armyTickMs.store(f.nowMs, std::memory_order_relaxed);
    if (on) g_focus.Drain([&c](const xm::Aimed& a) { return Apply(c, a, g_focus.kind); });
    char line[260];
    // Hold here (the commander path, transport's thread) anchored the army: that anchor is the X menu's, to defend. Timed
    // on the clock now.
    if (const uint64_t claim = g_holdClaimMs.exchange(0, std::memory_order_relaxed), now = GetTickCount64();
        on && claim && now >= claim && now - claim <= ClaimMs && c.anchorActive) {
        g_defend.Own(c.anchorPos);
        if (f.say) {
            std::snprintf(line, sizeof line, "Frame army x menu: defend: Hold here's spot (%.1f %.1f) is the X menu's", c.anchorPos.x, c.anchorPos.y);
            f.say(line);
        }
    }
    // Defend the X menu's spot (Off, the radius 0 or Hunt the mark Off: a monster it set is let go, the commander's hold).
    const float radius = on ? cfg::Gated(cfg::rules.defendRadius) : 0.f;
    const bool hunting = on && xhunt::ModeOf(cfg::Gated(cfg::rules.hunt)) != xhunt::Mode::Off;
    const bool owned = g_defend.Owns();
    const auto& s = g_defendScan;
    const auto sp = g_defend.Spot();
    const bool fresh = s.n >= 0 && f.nowMs >= s.atMs && f.nowMs - s.atMs <= DefendScanFreshMs;   // the last tick's, after its step
    const bool here = std::hypot(s.spot.x - sp.x, s.spot.y - sp.y) < .05f;
    const auto d = g_defend.Step(c, radius, hunting, f.nowMs, s.foes, fresh && here ? s.n : -1);
    // The next scan (GameTick, after this step): wanted while the X menu holds a spot to defend and nothing is focused; seen from
    // the summon nearest the spot.
    auto& w = g_defendWant;
    w.viewer = 0;
    if (g_defend.Owns() && radius > 0 && hunting && !c.priorityTargetId) {
        float best = 1e9f;
        for (size_t i = 0; i < f.unitCount; ++i) {
            const float dd = std::hypot(f.units[i].pos.x - sp.x, f.units[i].pos.y - sp.y);
            if (dd < best) { best = dd; w.viewer = f.units[i].unitId; }
        }
        w.spot = g_defend.Spot(); w.radius = radius; w.atMs = f.nowMs;
    }
    if (!on && !owned) return;
    // Switched Off (N3, recommended yes): the X menu's own anchor (Move and hold, Hold here's claimed spot) goes
    // too, the coordinator's ClearAnchor (the army re-forms on the hero); another order's anchor is never touched.
    if (!on && g_defend.Release(c) && f.say) {
        std::snprintf(line, sizeof line, "Frame army x menu: taken off its button: the X menu's held spot (%.1f %.1f) let go (the coordinator's ClearAnchor: the army re-forms on you)", sp.x, sp.y);
        f.say(line);
    }
    if (!f.say) return;
    if (d.gaveUp) {   // alive after its focus was cleared: the hunt gave up on it
        std::snprintf(line, sizeof line, "Frame army x menu: defend: monster %u given up (the hunt could not reach it): skipped for %.0f s or until a new spot",
                      d.gaveUp, double(xhunt::GivenUpMs) / 1000.0);
        f.say(line);
    }
    if (d.act == xhunt::Defend::Act::None) return;
    switch (d.act) {
        case xhunt::Defend::Act::Picked:
            std::snprintf(line, sizeof line, "Frame army x menu: defend: monster %u, %.1f sub-tiles from the spot (%.1f %.1f; %d hostiles within %.1f): the summons' target, summons only",
                          d.id, d.dist, sp.x, sp.y, d.seen, radius);
            break;
        case xhunt::Defend::Act::Lost:
            std::snprintf(line, sizeof line, "Frame army x menu: defend: monster %u is no longer the target (killed, lost, or given up by the hunt: the next scan tells); back to the spot", d.id);
            break;
        case xhunt::Defend::Act::LetGo:
            if (d.dist > 0) std::snprintf(line, sizeof line, "Frame army x menu: defend: monster %u ran %.1f sub-tiles from the spot (past %.1f + %.0f): let go, back to the spot", d.id, d.dist, radius, xhunt::DefendSlack);
            else std::snprintf(line, sizeof line, "Frame army x menu: defend: monster %u let go (%s): the commander's hold", d.id, !on ? "taken off its button" : radius <= 0 ? "Defend radius Off" : "Hunt the mark Off");
            break;
        case xhunt::Defend::Act::Ended:
            std::snprintf(line, sizeof line, "Frame army x menu: defend: the spot (%.1f %.1f) is no longer held (another order; its target, if any, is that order's)", sp.x, sp.y);
            break;
        default: return;
    }
    f.say(line);
}// The held button, once a frame.
void Press(seam::ArmyXPressFrame f) {
    // A game skill or a Draw glyph put on the menu's button takes it back at once: the menu is on no button again (one
    // line), and with that every army rule of it is off. A snapshot from just before the bind may still show the skill
    // the bind cleared: wait it out.
    if (const int was = armyxmenu::Button(); was >= 0 && was < framevr::skills::Count && f.fresh && f.snapshot && GetTickCount64() - st.boundAtMs > 2000) {
        const auto& now = *static_cast<const ia::Snapshot*>(f.snapshot);
        const bool skill = now.skills[size_t(was)].known && now.skills[size_t(was)].id >= 0;
        if (skill || f.glyphDraw[0] == was || f.glyphDraw[1] == was) {
            char line[160];
            std::snprintf(line, sizeof line, "Frame army x menu: slot %d %s: the menu is on no button now", was, skill ? "has a game skill" : "is a Draw glyph button");
            if (f.say) f.say(line);
            SaveButton(-1);
        }
    }
    const bool on = armyxmenu::On();
    const int bound = armyxmenu::Button();
    // Closed and not our press (or nothing bound), no targeting and no mark: one branch, nothing else (the toast still fades out
    // below).
    if (!st.ctl.menu.open && !st.drawn && !st.ctl.waitUp && !g_focus.Waiting() && !st.targeting.Busy() && !st.ground.Busy() && !st.ringShown &&
        !st.mark.id && (bound < 0 || f.slot != bound)) return;
    if (!on) st.ctl.toastLabel = -1;   // the Off call: no stale toast after the next On
    const auto& snap = *static_cast<const ia::Snapshot*>(f.snapshot);
    const auto* aims = static_cast<const framevr::gameplay::Pose*>(f.aims);
    xm::FrameIn in;
    in.nowMs = f.time * 1000.0;
    in.on = on;
    in.bound = bound >= 0 && bound < framevr::skills::Count ? bound : -1;
    in.held = f.slot;
    in.occupied = f.occupied;
    // The bound button's raw state: the commander's slot also drops to -1 while it is down. Its hand is the slot's
    // (skills::Hand), its button the trigger or a face button, the grip layer's slots on the same face button.
    {
        const auto& hi = *static_cast<const framevr::skills::HandInput*>(f.hands);
        const int bh = in.bound >= 0 ? framevr::skills::Hand(in.bound) : 0;
        int button = -1;
        for (int b = 0; b < 5 && in.bound >= 0; ++b) if (framevr::skills::HandSlot(bh, b) == in.bound) button = b;
        in.down = button >= 0 && (button == 0 ? hi.trigger[bh] > .2f : hi.face[bh][(button - 1) % 2]);
        in.hand = st.ctl.menu.open ? st.ctl.hand : bh;
    }
    const bool skillThere = in.bound >= 0 && snap.skills[size_t(in.bound)].known && snap.skills[size_t(in.bound)].id >= 0;
    const bool glyphThere = in.bound >= 0 && (f.glyphDraw[0] == in.bound || f.glyphDraw[1] == in.bound);
    in.slotFree = in.bound >= 0 && (f.fresh ? !skillThere && !glyphThere : st.ctl.menu.open);   // a stale snapshot keeps an open cross
    in.wheelOpen = f.wheel;
    in.tracked = f.aimsTracked[in.hand] && f.headTracked;
    in.handPos = {aims[in.hand].p.x, aims[in.hand].p.y, aims[in.hand].p.z};
    in.eye = {f.head[0], f.head[1], f.head[2]};
    in.head = {f.head[3], f.head[4], f.head[5], f.head[6]};
    in.army = ia::HasSummonArmy(snap);
    const xm::Tuning t = xm::Shipped();
    if (g_focus.Waiting()) Settle(f, in.nowMs, t, !on);   // Attack my target's outcome from the game thread
    const auto& hi = *static_cast<const framevr::skills::HandInput*>(f.hands);
    const xm::WorldMap map = f.engineIn ? xm::MapOf(*static_cast<const ia::Input*>(f.engineIn), f.upm) : xm::WorldMap{};
    // The mark's life: the commander's priority target (the game thread's), the switch.
    if (st.mark.id) {
        const uint32_t was = st.mark.id;
        const uint64_t at = g_priorityAt.load(std::memory_order_relaxed), now = GetTickCount64();
        const bool fresh = at && now >= at && double(now - at) <= xm::Mark::StaleMs;
        const auto end = !on ? xm::Mark::End::Dropped : st.mark.Step(g_priority.load(std::memory_order_relaxed), in.nowMs, fresh);
        if (!on) st.mark.Clear();
        if (end != xm::Mark::End::None && f.say) {
            char line[220];
            std::snprintf(line, sizeof line, "Frame army x menu: the mark over monster %u is gone (%s)", was,
                          !on ? "taken off its button" : end == xm::Mark::End::Dropped ? "the commander's priority target let it go: it died, was lost, or an order cleared it" :
                          end == xm::Mark::End::Stale ? "no game tick for 2 s confirmed the commander's priority target (a load, or companion AI idle)" :
                          "the commander never made it the priority target");
            f.say(line);
        }
    }
    // Attack my target's targeting mode: the ray's candidate, the lock, the cancels. It owns its hand's trigger.
    xm::Aimed cand;   // this frame's candidate (id 0: none)
    if (st.targeting.Busy()) {
        const int h = st.targeting.hand;
        if (st.targeting.active && f.aimsTracked[h] && map.Valid()) {
            const xm::WorldRay ray = xm::RayOf(map, aims[h].p, aims[h].q);
            std::lock_guard lock(g_picksLock);
            if (g_scan.at && GetTickCount64() - g_scan.at <= ScanFreshMs) {
                const int i = xm::RayPick(g_scan.targets, std::min(g_scan.count, framevr::targetcone::MaxTargets), g_scan.heroX, g_scan.heroY,
                                          map, ray, t.reachCells, st.targeting.candidate);
                if (i >= 0) cand = {g_scan.targets[i].id, g_scan.targets[i].x, g_scan.targets[i].y, double(GetTickCount64()), false};
            }
        }
        xm::TargetIn ti;
        ti.nowMs = in.nowMs; ti.on = on; ti.trigger = hi.trigger[h]; ti.grip = hi.grip[h]; ti.candidate = cand.id; ti.timeoutS = t.targetS;
        const auto to = st.targeting.Step(ti);
        // The trigger's own action (attack, skill) never fires: its slot's request is cleared, the locking pull's too.
        xm::OwnTrigger(to, h, f.slot, f.requested, f.interact);
        if (to.tick && t.hoverMs > 0 && t.hoverAmp > 0 && f.pulse) f.pulse(h, t.hoverAmp, uint64_t(t.hoverMs * 1e6));
        if (to.missed && in.nowMs - st.missedToldMs > 1000 && f.say) {
            st.missedToldMs = in.nowMs;
            f.say("Frame army x menu: targeting: the trigger pulled with no monster on the ray: swallowed, still targeting (said at most once a second)");
        }
        const double s = (in.nowMs - st.aimStartMs) / 1000.0;
        if (to.end == xm::TargetEnd::Locked) {
            st.mark.Clear();
            char head[160];
            std::snprintf(head, sizeof head, "Frame army x menu: %s hand, targeting %.2f s: locked monster %u", h ? "left" : "right", s, cand.id);
            if (g_focus.Post(cand, in.nowMs)) {   // the game thread gives it (Settle: the buzz and the toast); the arrow stays on it now
                std::snprintf(st.pendingHead, sizeof st.pendingHead, "%s", head);
                st.pendingHand = h;
                st.mark.Set(cand.id, in.nowMs);
            } else if (f.say) {
                char line[240]; std::snprintf(line, sizeof line, "%s, nothing given (the last Attack my target is still on its way to the game thread)", head);
                f.say(line);
            }
        } else if (to.end != xm::TargetEnd::None) {
            if (to.end != xm::TargetEnd::Off) st.ctl.Toast(xm::atlas::LabelCanceled, in.nowMs, t);
            if (f.say) {
                char line[240];
                std::snprintf(line, sizeof line, "Frame army x menu: %s hand, targeting %.2f s: %s, nothing given", h ? "left" : "right", s, EndName(to.end));
                f.say(line);
            }
        }
        // The X menu on that very trigger: the cross never sees it while the mode owns it (the lock is not a new press).
        if (st.targeting.Busy() && in.bound == framevr::skills::TriggerSlot(h)) in.down = false;
    }
    // Move and hold's spot pick: the targeting mode on the ground. The hand's laser meets the floor (xm::FloorCell);
    // within the commander's 40 sub-tiles of the hero (companionai::ElasticCombatLeashDistance, read from the commander) the candidate is
    // that cell: the tactical chord's green preview ring there, its trigger sets it; past it the chord's red ring and its boundary
    // click, and a pull is refused with the chord's error buzz. The grip or the time row cancels.
    if (st.ground.Busy()) {
        const int h = st.ground.hand;
        float gx = 0, gy = 0, px = 0, py = 0;
        bool floor = false, reach = false;
        double heroDist = 0;
        if (st.ground.active && f.aimsTracked[h] && map.Valid()) {
            floor = xm::FloorCell(map, xm::RayOf(map, aims[h].p, aims[h].q), gx, gy);
            const bool hero = ::companionai::GetPlayerWorldSubtilePos(px, py);
            heroDist = hero ? std::hypot(gx - px, gy - py) : 0.0;
            reach = floor && (!hero || xm::InReach(gx, gy, px, py, float(framevr::companionai::ElasticCombatLeashDistance)));
        }
        xm::TargetIn ti;
        ti.nowMs = in.nowMs; ti.on = on; ti.trigger = hi.trigger[h]; ti.grip = hi.grip[h]; ti.candidate = reach ? 1u : 0u; ti.timeoutS = t.targetS;
        const auto to = st.ground.Step(ti);
        xm::OwnTrigger(to, h, f.slot, f.requested, f.interact);
        auto& board = framevr::telegraph::Current();
        if (st.ground.active && floor) {   // the chord's preview: green within reach, red past it (CompanionMark, its one mark)
            board.Place(gx, gy, GetTickCount64(), 150, reach ? 3 : 4, reach ? 1.8f : 2.2f, 0, 0, 0, framevr::telegraph::CompanionMark);
            st.ringShown = true;
            const int now = reach ? 1 : 0;
            if (st.wasInReach >= 0 && now != st.wasInReach && f.pulse)   // the chord's boundary click
                f.pulse(h, reach ? .20f : .35f, reach ? 15000000ull : 25000000ull);
            st.wasInReach = now;
        } else if (st.ringShown) {
            board.ClearSkill(framevr::telegraph::CompanionMark);
            st.ringShown = false; st.wasInReach = -1;
        }
        if (to.missed) {   // a pull with no spot: past the reach (the chord's error buzz) or no floor on the ray
            if (floor && !reach && f.pulse) f.pulse(h, .60f, 60000000ull);
            if (in.nowMs - st.missedToldMs > 1000 && f.say) {
                st.missedToldMs = in.nowMs;
                char line[200];
                if (floor) std::snprintf(line, sizeof line, "Frame army x menu: move and hold: too far (%.1f > %d sub-tiles from you): refused, still picking (said at most once a second)",
                                         heroDist, framevr::companionai::ElasticCombatLeashDistance);
                else std::snprintf(line, sizeof line, "Frame army x menu: move and hold: the trigger pulled with no ground on the ray: swallowed, still picking (said at most once a second)");
                f.say(line);
            }
        }
        const double s = (in.nowMs - st.groundStartMs) / 1000.0;
        if (to.end != xm::TargetEnd::None && st.ringShown) { board.ClearSkill(framevr::telegraph::CompanionMark); st.ringShown = false; st.wasInReach = -1; }
        if (to.end == xm::TargetEnd::Locked) {
            char head[200];
            std::snprintf(head, sizeof head, "Frame army x menu: %s hand, move and hold %.2f s: spot set", h ? "left" : "right", s);
            const xm::Aimed spot{0, gx, gy, double(GetTickCount64()), false};
            if (g_focus.Post(spot, in.nowMs, xm::FocusQueue::Kind::Anchor)) {   // the game thread gives it (Settle: the buzz and the toast)
                std::snprintf(st.pendingHead, sizeof st.pendingHead, "%s", head);
                st.pendingHand = h; st.groundAt[0] = gx; st.groundAt[1] = gy; st.groundHeroDist = heroDist;
            } else if (f.say) {
                char line[280]; std::snprintf(line, sizeof line, "%s, nothing given (the last order is still on its way to the game thread)", head);
                f.say(line);
            }
        } else if (to.end != xm::TargetEnd::None) {
            if (to.end != xm::TargetEnd::Off) st.ctl.Toast(xm::atlas::LabelCanceled, in.nowMs, t, xm::atlas::LabelMoveHold);
            if (f.say) {
                char line[240];
                std::snprintf(line, sizeof line, "Frame army x menu: %s hand, move and hold %.2f s: %s, nothing given", h ? "left" : "right", s, EndName(to.end));
                f.say(line);
            }
        }
        if (st.ground.Busy() && in.bound == framevr::skills::TriggerSlot(h)) in.down = false;
    }
    auto out = st.ctl.Step(in, t);
    if (out.mute) { f.requested = -1; f.interact = false; }
    if (out.event == xm::Event::Opened && st.targeting.active) {   // the X menu again (another button): the targeting ends, nothing given
        const int h = st.targeting.hand;
        st.targeting.End(hi.trigger[h]);
        cand = {};
        if (f.say) {
            char line[200];
            std::snprintf(line, sizeof line, "Frame army x menu: %s hand, targeting %.2f s: %s, nothing given", h ? "left" : "right",
                          (in.nowMs - st.aimStartMs) / 1000.0, EndName(xm::TargetEnd::Replaced));
            f.say(line);
        }
    }
    if (out.event == xm::Event::Opened && st.ground.active) {   // the same for Move and hold's spot pick
        const int h = st.ground.hand;
        st.ground.End(hi.trigger[h]);
        if (st.ringShown) { framevr::telegraph::Current().ClearSkill(framevr::telegraph::CompanionMark); st.ringShown = false; st.wasInReach = -1; }
        if (f.say) {
            char line[200];
            std::snprintf(line, sizeof line, "Frame army x menu: %s hand, move and hold %.2f s: %s, nothing given", h ? "left" : "right",
                          (in.nowMs - st.groundStartMs) / 1000.0, EndName(xm::TargetEnd::Replaced));
            f.say(line);
        }
    }
    if (out.event == xm::Event::Opened) {
        shared.open.fetch_add(1, std::memory_order_relaxed);
        st.openedMs = in.nowMs;
        st.aim[0] = st.aim[1] = 0;
        if (f.aimAt) f.aimAt(f.aimCtx, in.hand, st.aim);   // Hold ground's spot: where the hand aimed at the press
        // Attack my target's monster: the one this hand aimed at the press, else the eyes' (the outline's rule), by the commander's aim rules
        st.targetAtMs = double(GetTickCount64());
        {
            std::lock_guard lock(g_picksLock);
            st.target = xm::PickFor(g_picks[in.hand == 1 ? 1 : 0], g_picks[2], st.targetAtMs);
        }
    }
    if (out.tick && t.hoverMs > 0 && t.hoverAmp > 0 && f.pulse) f.pulse(out.hand, t.hoverAmp, uint64_t(t.hoverMs * 1e6));
    if (out.event == xm::Event::Issue || out.event == xm::Event::Canceled) {
        char path[64]; PathText(path, sizeof path, st.ctl.path[1]);
        // The layout it was given in and which arm was held
        char arms[176]; xm::ArmsText(arms, sizeof arms, st.ctl.arms, st.ctl.path[1].parent, t.viaCenter);
        char layout[240];
        if (t.parentDepthDeg > 0) std::snprintf(layout, sizeof layout, "%s layout, depth %.1f deg, %s", t.tee ? "T" : "straight", t.parentDepthDeg, arms);
        else std::snprintf(layout, sizeof layout, "%s layout, %s", t.tee ? "T" : "straight", arms);
        char line[720];
        if (out.event == xm::Event::Issue) {
            char head[400];
            std::snprintf(head, sizeof head, "Frame army x menu: %s hand, slot %d, %.2f s, %s: %s -> %s", out.hand ? "left" : "right",
                          in.bound, (in.nowMs - st.openedMs) / 1000.0, layout, path, xm::OrderLabel(out.order));
            line[0] = 0;
            if (st.mark.id) {   // a new order: the mark is gone (the new one's own comes when the commander takes it)
                char gone[120]; std::snprintf(gone, sizeof gone, "Frame army x menu: the mark over monster %u is gone (a new order)", st.mark.id);
                if (f.say) f.say(gone);
                st.mark.Clear();
            }
            if (out.order == xm::AttackTarget) {
                const auto how = xm::PlanAttack(st.target, st.targetAtMs, ::companionai::IsTacticalCommandsEnabled(), t.reachCells > 0);
                if (how == xm::AttackHow::Targeting) {   // no monster outlined: the hand picks one (the cross is closed now)
                    st.targeting.Start(out.hand, in.nowMs, hi.trigger[out.hand], hi.grip[out.hand]);
                    st.aimStartMs = in.nowMs;
                    char limit[32];
                    if (t.targetS > 0) std::snprintf(limit, sizeof limit, "%.1f s", t.targetS); else std::snprintf(limit, sizeof limit, "no time limit");
                    std::snprintf(line, sizeof line, "%s (no monster outlined: targeting, the %s hand's ray out to %.0f cells, %s; its trigger locks, the grip cancels%s)",
                                  head, out.hand ? "left" : "right", t.reachCells, limit, hi.trigger[out.hand] > xm::TriggerDown ? "; its trigger is held: let go first" : "");
                } else if (how != xm::AttackHow::Focus)   // no monster (reach Off), or Tactical commands off: the Aggressive stance instead, now
                    Fallback(f, head, how == xm::AttackHow::NoTarget ? FallWhy::NoTarget : FallWhy::TacticalOff, 0u, out.hand, in.nowMs, t);
                else if (g_focus.Post(st.target, in.nowMs)) {   // the game thread gives it; its line, buzz and toast when it reports (Settle)
                    std::snprintf(st.pendingHead, sizeof st.pendingHead, "%s", head);
                    st.pendingHand = out.hand;
                    st.mark.Set(st.target.id, in.nowMs);   // the arrow over it from the let go (gone again if the commander refuses)
                } else std::snprintf(line, sizeof line, "%s, nothing given (the last order is still on its way to the game thread)", head);
            } else if (out.order == xm::MoveHold) {   // the hand picks the spot (the cross is closed now); summons only
                st.ground.Start(out.hand, in.nowMs, hi.trigger[out.hand], hi.grip[out.hand]);
                st.groundStartMs = in.nowMs; st.wasInReach = -1;
                char limit[32];
                if (t.targetS > 0) std::snprintf(limit, sizeof limit, "%.1f s", t.targetS); else std::snprintf(limit, sizeof limit, "no time limit");
                std::snprintf(line, sizeof line, "%s (pick the spot: the %s hand's laser on the ground, within %d sub-tiles of you; its trigger sets it, the grip cancels, %s; summons only%s)",
                              head, out.hand ? "left" : "right", framevr::companionai::ElasticCombatLeashDistance, limit,
                              hi.trigger[out.hand] > xm::TriggerDown ? "; its trigger is held: let go first" : "");
            } else {
                const bool ok = Give(f, out.order);
                if (ok) Buzz(f, out.hand, t);
                st.ctl.Given(out.order, ok, in.nowMs, t);
                // Hold here's anchor is the X menu's: the next army tick claims it to defend (ArmyTick, Defend radius)
                if (ok && out.order == framevr::commander::SkillHoldGround) g_holdClaimMs.store(GetTickCount64(), std::memory_order_relaxed);
                std::snprintf(line, sizeof line, "%s (commander %d) %s%s", head, out.order, ok ? "given (the commander took it)" : "refused by the commander (formations off)",
                              ok && out.order == framevr::commander::SkillHoldGround ? "; summons only; the spot defended (Defend radius, ini)" : "");
            }
        } else {
            const char* why = !on ? "taken off its button" : in.wheelOpen ? "a wheel opened" : !in.slotFree ? "the button was taken" :
                              in.occupied ? "the hand took a potion or holster" : WhyName(out.why);
            std::snprintf(line, sizeof line, "Frame army x menu: %s hand, slot %d, %.2f s, %s: %s, nothing given (%s)", out.hand ? "left" : "right",
                          in.bound, (in.nowMs - st.openedMs) / 1000.0, layout, path, why);
        }
        if (f.say && line[0]) f.say(line);
    }
    // The targeting hint where the cross was, the candidate's arrow, and the mark's (each over its monster, in the room).
    if (st.targeting.active && !st.ctl.menu.open) xm::TargetingHint(out.draw, st.ctl.plane, t);
    if (st.ground.active && !st.ctl.menu.open) xm::TargetingHint(out.draw, st.ctl.plane, t, xm::atlas::LabelMoveHold);
    if (map.Valid()) {
        xm::Quad q;
        if (st.targeting.active && cand.id && cand.id != st.mark.id && xm::MarkQuad(map, cand.x, cand.y, in.eye, in.nowMs, false, q)) out.draw.Add(q);
        float mx = 0, my = 0;
        bool seen = false;
        if (st.mark.id) { std::lock_guard lock(g_picksLock); seen = Scanned(g_scan, st.mark.id, mx, my); }
        if (seen && xm::MarkQuad(map, mx, my, in.eye, in.nowMs, true, q)) out.draw.Add(q);
    }
    if (out.draw.n || st.drawn) { Publish(out.draw); st.drawn = out.draw.n > 0; }
}
// EndPair: the frame's quads from the static atlas, all of them or none.
void Layer(const seam::ArmyXLayerFrame& f) {
    static XrCompositionLayerQuad quads[xm::MaxQuads];   // lives until xrEndFrame (the submit keeps a pointer)
    static uint32_t toldOpen = 0;
    if (!f.layer || !f.chain || shared.n.load(std::memory_order_relaxed) == 0 || !armyxmenu::On() || !g_sheetPainted.load(std::memory_order_acquire)) return;
    const uint32_t s1 = shared.seq.load(std::memory_order_acquire);
    if (s1 & 1) return;
    const xm::DrawList d = shared.list;
    std::atomic_thread_fence(std::memory_order_acquire);
    if (shared.seq.load(std::memory_order_relaxed) != s1 || d.n == 0) return;
    if (f.count + uint32_t(d.n) > f.limit) {   // no room for the whole cross: none of it, every the commander layer kept (once an open)
        const uint32_t opens = shared.open.load(std::memory_order_relaxed);
        if (toldOpen != opens && f.say) {
            char line[140]; std::snprintf(line, sizeof line, "Frame army x menu: no room for its %d quads (%u of %u layers used), not drawn (said once an open)", d.n, f.count, f.limit);
            f.say(line);
        }
        toldOpen = opens;
        return;
    }
    const bool markPainted = g_markPainted.load(std::memory_order_acquire);
    uint32_t used = 0;
    for (int i = 0; i < d.n; ++i) {
        const auto& q = d.q[i];
        if (q.sheet == xm::SheetMark && !markPainted) continue;   // the mark's strip missing this session: no arrow, the rest drawn
        const auto region = q.sheet == xm::SheetMark ? framevr::panelatlas::ArmyXMark : framevr::panelatlas::ArmyXMenu;
        quads[used] = {XR_TYPE_COMPOSITION_LAYER_QUAD};
        const int rect[4] = {region.x + q.rect.x, region.y + q.rect.y, q.rect.w, q.rect.h};
        f.quad(f.quadCtx, &quads[used], q.pose, q.w, q.h, rect, f.chain, true);
        ++used;
    }
}
// The icon worker: the bind entry's icon for its own key only (xm::IconKey,
// which no manifest has): the cross from the sheet (xm::atlas::BindIcon), once a session, for every painter that asks.
std::shared_ptr<const framevr::interaction::icons::Image> Icon(const std::string& key, void* wic) {
    if (key != xm::IconKey) return nullptr;
    std::vector<uint8_t> sheet; UINT w = 0, h = 0;
    DecodeSheet(static_cast<IWICImagingFactory*>(wic), sheet, w, h);
    auto image = std::make_shared<framevr::interaction::icons::Image>();
    xm::atlas::BindIcon(sheet.empty() ? nullptr : sheet.data(), int(w), int(h), image->rgba);
    image->width = xm::atlas::BindW; image->height = xm::atlas::BindH;
    char line[160];
    std::snprintf(line, sizeof line, "Frame army x menu: its bind icon %s", sheet.empty() ? "missing (the sheet did not decode): the entry shows its letters" : "painted from the sheet (the cross, 132 x 130)");
    framevr::logging::Write(line);
    if (sheet.empty()) return nullptr;
    return image;
}
const bool g_iconMaker = (framevr::interaction::icons::SetBuiltIn(&Icon), true);
// SetupStatic (each session start, before the upload): the X menu's sheet into its region, premultiplied. Its tab On or Off
// (the static picture is uploaded once a session; the switch can turn On later).
void PaintMark(const seam::StaticAtlasFrame& f);
void Paint(const seam::StaticAtlasFrame& f) {
    PaintMark(f);   // its own region and checks, whatever the sheet's do
    g_sheetPainted.store(false, std::memory_order_release);
    const auto r = framevr::panelatlas::ArmyXMenu;
    if (!f.pixels || !framevr::panelatlas::Inside(r, f.width, f.height)) return;
    const auto t0 = std::chrono::steady_clock::now();
    // Never over someone else's picture: the region and its Gap must still be empty, else no pictures this session.
    if (!xm::atlas::RegionClear(f.pixels, f.width, f.height, r.x, r.y, r.w, r.h, framevr::panelatlas::Gap)) {
        if (f.say) f.say("Frame army x menu: its static-image region is not empty; no pictures this session");
        return;
    }
    // Decoded into a local each session (about 1 MB, freed at once).
    std::vector<uint8_t> sheet;
    UINT w = 0, h = 0;
    const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IWICImagingFactory* wic = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)))) DecodeSheet(wic, sheet, w, h);
    if (wic) wic->Release();
    if (SUCCEEDED(co)) CoUninitialize();
    if (!sheet.empty()) {
        PaintRegion(f, r, sheet);   // premultiplied, as its quads are sent (only inside its own region)
        g_sheetPainted.store(true, std::memory_order_release);
    }
    if (f.say) {
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        char line[160]; std::snprintf(line, sizeof line, "Frame army x menu: its sheet %s (%ux%u, %s, %.1f ms at session start)",
                                      sheet.empty() ? "is missing or the wrong size: nothing drawn" : "painted", w, h, f.srgb ? "sRGB" : "linear", ms);
        f.say(line);
    }
}
// The same session start: the target mark's frames (Attack my target's arrow) into their own region, under the same checks.
void PaintMark(const seam::StaticAtlasFrame& f) {
    const auto r = framevr::panelatlas::ArmyXMark;
    g_markPainted.store(false, std::memory_order_release);
    if (!f.pixels || !framevr::panelatlas::Inside(r, f.width, f.height)) return;
    if (!xm::atlas::RegionClear(f.pixels, f.width, f.height, r.x, r.y, r.w, r.h, framevr::panelatlas::Gap)) {
        if (f.say) f.say("Frame army x menu: the target mark's static-image region is not empty; no arrow this session");
        return;
    }
    std::vector<uint8_t> strip;
    UINT w = 0, h = 0;
    const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IWICImagingFactory* wic = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))))
        DecodePicture(wic, L"D2RVR_ARMY_X_MARK", UINT(r.w), UINT(r.h), strip, w, h);
    if (wic) wic->Release();
    if (SUCCEEDED(co)) CoUninitialize();
    if (!strip.empty()) { PaintRegion(f, r, strip); g_markPainted.store(true, std::memory_order_release); }
    if (f.say) {
        char line[160]; std::snprintf(line, sizeof line, "Frame army x menu: the target mark's %d frames %s (%ux%u)", xm::atlas::MarkFrames,
                                      strip.empty() ? "are missing or the wrong size: no arrow drawn" : "painted", w, h);
        f.say(line);
    }
}
}  // namespace armyxmenu
