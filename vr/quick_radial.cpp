// Quick cast radial. Glue around the pure engine steamvr/quick_radial.h, called from
// transport (vr/quick_radial.h lists the frames):
//   - Input (before the right stick's gestures): both pad sticks read (the ring's clock hand), the held hand's zeroed while the
//     ring owns it (no walk, turn, run toggle or loot names), and a finished picture uploaded into its two regions of upstream's
//     shared dynamic image (UploadShared; steamvr/quick_radial_atlas.h).
//   - Press (upstream's press this frame): the held button opens the ring (Open by: a tap, or a hold), the stick picks, its
//     return to the middle (or the release) queues the cast; the ring menu's place edits (link::edits) applied, its view
//     (link::view) published.
//   - Cast (before glyph casting's press step): the queued cast through upstream's own glyph press, rw::StartCast, the ring
//     wheel's tap cast's steps (its hand slot, else the right trigger's record): ONE call site.
//   - Layer (transport EndPair): the ring's 3 quads (ring, the pick's rim, the middle's name).
//   - Request (after interactions.Step): the ring menu's hold to assign on its Quick cast entry made upstream's clear of that
//     button; an item's use (a belt potion, an inventory item) sent through the same request.
// The pictures are painted on the radial's own worker (vr/quick_radial_paint.cpp), which also reads the character's save name
// (charsave) and keeps each character's ring, and the radial's button, in d2r_vr_quick_radial.ini beside the plugin.
// Closed, with nothing pending: one branch a frame in the press (a 0.5 s tick looks for a new character or a changed ring and
// paints it ahead of the next open), no allocation, no lock. No swapchain.
#include <windows.h>
#include <openxr/openxr.h>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "quick_radial.h"
#include "character_save.h"
#include "../steamvr/quick_radial.h"
#include "../steamvr/quick_radial_atlas.h"
#include "../steamvr/quick_radial_link.h"
#include "../steamvr/quick_radial_paint.h"
#include "../steamvr/ring_wheel.h"        // rw::StartCast: upstream's glyph press, as the ring wheel's tap cast
#include "../steamvr/ring_wheel_text.h"   // the game's color codes cleaned from a name
#include "../steamvr/glyph_cast.h"
#include "../steamvr/hand_controls.h"
#include "../steamvr/interaction_icons.h"
#include "../steamvr/interactions.h"
#include "../steamvr/skill_slots.h"
#include "../steamvr/logging.h"

namespace {
namespace rt = framevr::quickradial::rt;
namespace qr = framevr::quickradial;
namespace ia = framevr::interaction;
namespace rw = framevr::ringwheel;
namespace tx = framevr::ringwheel::text;
namespace paint = framevr::quickradial::paint;

// The radial's feel: the tuned values (Quick cast has no menu rows). Open by is the one choice kept, ini-only: [quick_radial]
// open_by in d2r_vr_quick_radial.ini, 0 Toggle (the default), 1 Hold (LoadSettings, once).
std::atomic<bool> openByToggle{true};
qr::Tuning ReadTuning() {
    qr::Tuning t;   // the struct's defaults are the tuned values: 1 m ahead, 8 deg below the eyes, ring 10 deg, items 5 deg, dead zone 35%,
                    // pick hold 8 deg, center hold 120 ms, pick tick 25 ms at 30%, cast buzz 70 ms at 70%, strongest potion first,
                    // Fixed places, the ring shuts after a cast
    t.toggle = openByToggle.load(std::memory_order_relaxed);
    return t;
}// A skill or item onto the ring from outside the page (a menu moved it in; a put made before the save was read): Fixed, the
// first empty place; Growing, the end of the packed ring.
bool AddToRing(qr::Ring& r, qr::Item it, bool fixed) {
    if (fixed) return r.AddFree(it);
    if (!r.Packed()) r.Pack();
    return r.Add(it);
}
int PlaceOf(const qr::Ring& r, int id) { for (int i = 0; i < r.n; ++i) if (r.items[size_t(i)].skill == id) return i; return -1; }
using qr::PackedIndex; using qr::RealPlace;   // Growing's packed page numbers (steamvr/quick_radial.h)
// The log's words for a ring item: Growing "item 2 of 3" (its place among the filled ones, never the real
// place of a ring Fixed left with gaps); Fixed "place 9, 2 filled" (the place's clock name). index: the item's real place.
std::string ItemWords(int index, int n, bool fixed) {
    char w[48];
    if (fixed) std::snprintf(w, sizeof w, "place %d, %d filled", qr::PlaceNumber(index, true), n);
    else std::snprintf(w, sizeof w, "item %d of %d", index + 1, n);
    return w;
}
std::string ItemWords(const qr::Ring& r, int index, bool fixed) { return ItemWords(fixed ? index : PackedIndex(r, index), r.Count(), fixed); }
// The plugin's folder (the ini beside it, the font in its resources).
HMODULE Self() {
    HMODULE m = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(&Self), &m);
    return m;
}
std::wstring IniFile() {
    static const std::wstring path = [] {
        wchar_t buf[MAX_PATH]{};
        DWORD n = GetModuleFileNameW(Self(), buf, MAX_PATH);
        if (!n || n >= MAX_PATH) return std::wstring();
        while (n && buf[n - 1] != L'\\') --n;
        buf[n] = 0;
        return std::wstring(buf) + L"d2r_vr_quick_radial.ini";
    }();
    return path;
}
std::wstring Wide(const std::string& s) { return std::wstring(s.begin(), s.end()); }

// ---- The radial's worker: identify the character and load its ring, save, paint ----
struct Ident { bool ok = false, found = false, twins = false; uint64_t session = 0; int cls = -1, level = 0; std::string key, name; qr::Ring ring; };
struct Worker {
    std::mutex m;
    std::condition_variable cv;
    bool paintWanted = false, identWanted = false;
    paint::Job job; std::vector<std::string> jobIcons;
    int identClass = -1, identLevel = 0; uint64_t identSession = 0;
    std::vector<std::pair<std::string, std::string>> saves;
    std::shared_ptr<paint::Picture> done;
    Ident ident;
    std::atomic<bool> hasDone{false}, hasIdent{false}, hasNote{false};
    std::string note;                     // a save's outcome for the log (the frame thread says it: Tick)
    std::chrono::steady_clock::time_point retryAt{};   // S-C: a save that could not start waits until then
    bool failing = false;                 // said once a streak of failed saves, and once when one gets through
    void Loop() {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
        bool fontTried = false;
        for (;;) {
            std::unique_lock lock(m);
            const auto saveDue = [&] { return !saves.empty() && std::chrono::steady_clock::now() >= retryAt; };
            cv.wait_for(lock, std::chrono::milliseconds(500), [&] { return paintWanted || identWanted || saveDue(); });
            if (saveDue()) {
                auto s = std::move(saves); saves.clear();
                lock.unlock();
                const auto file = IniFile();
                const char* failed = nullptr; DWORD err = 0;
                if (!file.empty()) {   // written to a copy, flushed, then moved over the ini (an exit mid-write never cuts it short)
                    const std::wstring tmp = file + L".tmp";
                    const bool copied = CopyFileW(file.c_str(), tmp.c_str(), FALSE) != 0;
                    err = copied ? 0 : GetLastError();
                    const qr::SaveStart start = qr::SaveStartOf(copied, err == ERROR_FILE_NOT_FOUND);
                    if (start == qr::SaveStart::Retry) failed = "the ini could not be copied";
                    else {
                        if (start == qr::SaveStart::Fresh) DeleteFileW(tmp.c_str());   // no ini yet: a fresh copy
                        for (const auto& [key, text] : s) WritePrivateProfileStringW(L"quick_radial", Wide(key).c_str(), Wide(text).c_str(), tmp.c_str());
                        WritePrivateProfileStringW(nullptr, nullptr, nullptr, tmp.c_str());
                        if (!MoveFileExW(tmp.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) { err = GetLastError(); failed = "the new ini could not replace the old one"; }
                    }
                }
                std::lock_guard back(m);
                char line[200]{};
                if (failed) {   // kept and tried again in 2 s; said once a streak
                    qr::Requeue(saves, std::move(s));
                    retryAt = std::chrono::steady_clock::now() + std::chrono::seconds(2);
                    if (!failing) std::snprintf(line, sizeof line, "Frame quick radial: ring not saved yet: %s (error %lu); trying again every 2 s", failed, err);
                    failing = true;
                } else if (failing) { failing = false; std::snprintf(line, sizeof line, "Frame quick radial: ring saved (after a retry)"); }
                if (line[0]) { note = line; hasNote.store(true, std::memory_order_release); }
                continue;
            }
            if (!paintWanted && !identWanted) continue;   // woken for a save still waiting its retry
            if (identWanted) {
                identWanted = false;
                const int cls = identClass, level = identLevel; const uint64_t session = identSession;
                lock.unlock();
                Ident r; r.session = session; r.cls = cls; r.level = level;
                const auto save = charsave::Read(cls);
                // the newest save of the class at the live level, and the only save of that class at that level (two at one
                // level: the newest is no proof of who is played; the ring stays this game's, never saved under the other's name)
                const bool match = save.valid && save.problem == framevr::charsave::Problem::None && save.name[0] && save.heroClass == cls && level > 0 && save.level == level;
                r.twins = match && charsave::SameLevel(cls, level) > 1;
                if (match && !r.twins) {
                    r.ok = true; r.name = save.name; r.key = qr::Key(cls, save.name);
                    wchar_t text[400]{};
                    const auto file = IniFile();
                    if (!file.empty() && GetPrivateProfileStringW(L"quick_radial", Wide(r.key).c_str(), L"\x1", text, 400, file.c_str()) && !(text[0] == 1 && !text[1])) {
                        std::string narrow; for (const wchar_t* c = text; *c; ++c) narrow += char(*c < 128 ? *c : '?');
                        r.found = qr::Decode(narrow.c_str(), r.ring);
                    }
                } else r.name = save.name;
                std::lock_guard back(m);
                ident = std::move(r);
                hasIdent.store(true, std::memory_order_release);
                continue;
            }
            paintWanted = false;
            paint::Job j = std::move(job); std::vector<std::string> skills = std::move(jobIcons);
            lock.unlock();
            if (!fontTried) {   // the menu's Cinzel from the plugin's resources (vr_settings_assets.rc), once
                fontTried = true;
                const HMODULE mod = Self();
                if (const HRSRC res = FindResourceW(mod, L"D2RVR_FONT_CINZEL", MAKEINTRESOURCEW(10)))
                    if (const void* data = LockResource(LoadResource(mod, res))) paint::LoadFont(data, SizeofResource(mod, res));
            }
            // Each entry's picture from upstream's icon cache (a skill's "s<id>", an item's by its code; decoded on its own worker);
            // waited for at most 2 s in all.
            std::vector<std::shared_ptr<const ia::icons::Image>> held(skills.size());
            const auto until = GetTickCount64() + 2000;
            for (size_t i = 0; i < skills.size(); ++i) {
                if (skills[i].empty()) continue;
                const std::string& key = skills[i];
                held[i] = ia::icons::Get(key);
                while (!held[i] && GetTickCount64() < until) { ia::icons::Wait(100); held[i] = ia::icons::Get(key); }
                if (held[i] && i < j.entries.size()) j.entries[i].icon = {held[i]->rgba.data(), int(held[i]->width), int(held[i]->height)};
            }
            auto pic = std::make_shared<paint::Picture>();
            paint::Paint(j, *pic);
            std::lock_guard back(m);
            done = std::move(pic);
            hasDone.store(true, std::memory_order_release);
        }
    }
};
Worker* worker = nullptr;   // on the heap from the first need, never freed (its thread may outlive a DLL unload's statics)
Worker& W() {
    if (!worker) { worker = new Worker; std::thread([] { worker->Loop(); }).detach(); }
    return *worker;
}

// ---- The quads, handed from the press (Input) to EndPair without a lock (a sequence counter) ----
struct Shared {
    std::atomic<uint32_t> seq{0};
    std::atomic<int> n{0};
    std::atomic<uint32_t> open{0};
    std::atomic<uint64_t> drawKey{0};   // the picture the list was built for
    std::atomic<uint64_t> liveKey{0};   // the picture in the shared image now (two frames after its upload)
    std::atomic<bool> rebuilt{false};   // the shared image was made again (a session rebuild): paint and upload again
    qr::DrawList list;
};
Shared ringShared;
void Publish(const qr::DrawList& d, uint64_t key) {
    const uint32_t s = ringShared.seq.load(std::memory_order_relaxed);
    ringShared.seq.store(s + 1, std::memory_order_release);
    std::atomic_thread_fence(std::memory_order_release);
    ringShared.list = d;
    ringShared.n.store(d.n, std::memory_order_relaxed);
    ringShared.drawKey.store(key, std::memory_order_relaxed);
    ringShared.seq.store(s + 2, std::memory_order_release);
}

struct Pending { bool on = false, fixed = false; int skill = -1, owner = -1, hand = 0, slot = -1, index = -1, n = 0, picks = 0; ULONGLONG since = 0; double heldS = 0; char name[64] = ""; };
struct State {
    qr::Controller ctl;
    qr::Ring ring;
    // An item entry's use: asked of upstream through the request seam (Potion: the belt key; UseItem: the inventory use), once.
    struct Use { bool want = false, sent = false, fixed = false; ia::Request r; uint64_t seq = 0; double sentMs = 0; int hand = 0, slot = -1, index = -1, n = 0, picks = 0;
                 double heldS = 0; char name[80] = ""; char from[48] = ""; } use;
    uint64_t useNumber = 0;
    int buzzHand = -1, buzzCount = 0; float buzzAmp = 0; double buzzMs = 0;   // a buzz the press gives next (the request seam has no pulse)
    bool buzzShort = true;        // short pulses (every one 60 ms), or the one Cast buzz
    int bound = -1;
    bool busy = false;            // open, or holding the stick or trigger after a close: the press runs every frame
    double nextTickS = 0;
    // The character: the snapshot's session and class; its save name once read (key), the ring loaded from it.
    uint64_t session = 0; int cls = -2; bool identified = false, answered = false; double identRetryS = 0; std::string key, name;
    // skills a menu moved in before the save was read (on the live ring too), and the ring
    // menu's puts made then: added to the stored ring once read (those still on the live ring), never saved over it.
    struct Moved { qr::Item it; int from = -1; std::string where; bool page = false; };
    std::vector<Moved> movedBefore;
    int toldClass = -3;           // the last "class keeps upstream" line
    int noSaveTold = 0;           // the last "no save" / "two saves" line (once, not every 10 s)
    // The pictures: what the ring shows now (want), what was asked of the worker, what was uploaded and when (Input frames).
    uint64_t wantKey = 0, askedKey = 0, uploadedKey = 0, frame = 0, uploadFrame = 0;
    bool srgb = false;
    // The sticks (per physical hand, 0 right, 1 left, -1..1), from the Input seam; the one the ring owns.
    float stick[2][2]{};
    bool ownStick = false; int stickHand = 0;
    // One line an open.
    int picks = 0; double openedMs = 0; bool drawn = false;
    Pending pending;
    qr::BindWatch bind;
    int ownerTold = -2;
    bool offHandled = false;      // the Off cleanup ran (Press returns at once while the radial is off)
    uint32_t buttonSeen = 0;      // qr::state::buttonSet as last seen: a change (the ring menu's SetButton, or our own bind) is saved
    uint32_t noRoomTold = 0;
};
State st;

const ia::Skill* Find(const ia::Snapshot& s, int id) {   // the live catalog's entry for a skill id: the class's own first
    const ia::Skill* any = nullptr;
    for (unsigned i = 0; i < s.skillCount && i < s.catalog.size(); ++i) {
        const auto& k = s.catalog[i];
        if (k.id != id || !k.known) continue;
        if (k.owner == -1) return &k;
        if (!any) any = &k;
    }
    return any;
}
bool Usable(const ia::Skill* k) { return k && k->known && k->available; }
std::string NameOf(const ia::Snapshot& s, int id) {
    if (qr::IsExactId(id)) return qr::CodeName(qr::CodeOfExact(id));
    // the plain kind for one with tiers (a ring menu put or a log line is the kind, not Best or weakest: that is the Potion order's, ItemInfo's name)
    if (qr::IsFamilyId(id)) return qr::HasTiers(id & 0xffff) ? std::string(qr::FamilyName(id & 0xffff)) : qr::FamilyEntryName(id & 0xffff, true);
    const auto* k = Find(s, id);
    if (k && k->name[0]) return tx::Clean(k->name);
    return "Skill " + std::to_string(id);
}
// ---- Items (belt and inventory, as upstream's own belt cells and quick use read them) ----
using qr::BeltOf; using qr::InvOf;   // steamvr/quick_radial.h (the ring menu's kind page reads them too)
struct ItemView { std::string name; qr::ItemPick pick; };
ItemView ItemInfo(const ia::Snapshot& s, int id, const qr::Tuning& t) {
    ItemView v;
    qr::InvItem inv[128]; const int invN = InvOf(s, inv, 128);
    v.pick = qr::PickFor(id, BeltOf(s), inv, invN, t.strongest);
    if (qr::IsExactId(id)) {
        const uint32_t code = qr::CodeOfExact(id);
        const auto* row = s.quick.known ? s.quick.Find(code) : nullptr;
        if (!row) for (unsigned i = 0; i < s.quick.count && i < s.quick.items.size() && !row; ++i) if (qr::Code3(s.quick.items[i].item.code) == qr::Code3(code)) row = &s.quick.items[i];
        v.name = row && row->name[0] ? tx::Clean(row->name) : qr::CodeName(code);
    } else {
        const int f = id & 0xffff;
        v.name = qr::FamilyEntryName(f, t.strongest);   // "(best)" only where there are tiers (Healing, Mana, Rejuvenation)
    }
    return v;
}
std::string IconKeyOf(uint32_t code) { if (!code) return {}; ia::Item it; it.handle = 1; it.code = code; return ia::icons::ItemKey(it); }   // a base item's picture (any handle: the key is the code)
int IdOfEntry(const qr::Entry& e) { return e.index >= 0 && e.index < qr::MaxItems ? st.ring.items[size_t(e.index)].skill : -1; }   // -1: an empty place (Fixed)
// The picture's key: each item (skill, level, usable; an item's pick), the sizes, Open by's hint, the image's encoding, which
// places are empty (Fixed).
uint64_t KeyOf(const qr::Entries& es, const ia::Snapshot& s, const qr::Tuning& t) {
    uint64_t h = 1469598103934665603ull;
    const auto mix = [&](uint64_t v) { h ^= v; h *= 1099511628211ull; };
    mix(t.strongest); mix(t.toggle); mix(uint64_t(es.n)); mix(uint64_t(std::lround(t.radiusDeg * 10))); mix(uint64_t(std::lround(t.iconDeg * 10))); mix(st.srgb);
    if (es.fixed) { mix(0xF1ED); mix(es.filled); }   // Growing's key has no mask
    // the belt and the inventory read once a call, and only the pick (no name strings) on these open frames
    std::array<qr::BeltCol, 4> belt{}; qr::InvItem inv[128]; int invN = -1;
    for (int i = 0; i < es.n; ++i) {
        const auto& e = es.e[size_t(i)];
        const int id = IdOfEntry(e);
        if (id < 0) continue;   // an empty place: the mask above
        if (qr::IsItemId(id)) {
            if (invN < 0) { belt = BeltOf(s); invN = InvOf(s, inv, 128); }
            const qr::ItemPick p = qr::PickFor(id, belt, inv, invN, t.strongest);
            mix(uint64_t(id)); mix(p.code); mix(uint64_t(p.count)); mix(p.ok);
            continue;
        }
        const auto* k = Find(s, id);
        mix(uint64_t(id + 1)); mix(k ? uint64_t(k->level) : 0); mix(Usable(k));
    }
    return h | 1;
}
void AskPaint(const qr::Entries& es, const ia::Snapshot& s, const qr::Tuning& t, uint64_t key) {
    paint::Job j; j.t = t; j.srgb = st.srgb; j.key = key;
    std::vector<std::string> skills;
    for (int i = 0; i < es.n; ++i) {
        const auto& e = es.e[size_t(i)];
        paint::PEntry p;
        std::string icon;
        {
            const int id = IdOfEntry(e);
            if (id < 0) p.empty = true;   // Fixed: an empty place, a faint socket
            else if (qr::IsItemId(id)) {
                const auto v = ItemInfo(s, id, t);
                p.name = v.name; p.count = v.pick.count; p.dim = !v.pick.ok; p.why = v.pick.ok ? "" : v.pick.why;
                icon = IconKeyOf(v.pick.code);
            } else {
                const auto* k = Find(s, id);
                p.name = NameOf(s, id); p.level = k ? k->level : 0; p.dim = !Usable(k);
                icon = "s" + std::to_string(id);
            }
        }
        j.entries.push_back(std::move(p)); skills.push_back(std::move(icon));
    }
    auto& w = W();
    {
        std::lock_guard lock(w.m);
        w.job = std::move(j); w.jobIcons = std::move(skills); w.paintWanted = true;
    }
    w.cv.notify_one();
    st.askedKey = key;
}
void Pulses(int hand, int count) { st.buzzHand = hand; st.buzzAmp = .25f; st.buzzCount = count; st.buzzShort = true; st.buzzMs = 0; }
// An edit of the live ring: saved when the character's save is read (identified); before that kept for this game,
// its puts added to the stored ring once read (st.movedBefore, page), never over it.
void AskSave(void (*)(const char*), const char*, const qr::Item* put = nullptr) {
    if (st.identified && !st.key.empty()) {
        auto& w = W();
        { std::lock_guard lock(w.m); w.saves.emplace_back(st.key, qr::Encode(st.ring)); }
        w.cv.notify_one();
    } else if (put) st.movedBefore.push_back({*put, -1, "the ring menu", true});
}
// a moved skill the ring could not take: said taken off its button, three short pulses on that button's hand.
void TakenOff(const ia::Snapshot& s, int id, int from, const char* where, const char* why, void (*say)(const char*)) {
    Pulses(from >= 0 ? framevr::skills::Hand(from) : 0, 3);
    if (!say) return;
    char line[300];
    std::snprintf(line, sizeof line, "Frame quick radial: %s taken off %s (a menu took it; not added to the ring: %s): hold a button on it to put it back", NameOf(s, id).c_str(), where, why);
    say(line);
}
// The 0.5 s tick (closed or open): the bound slot, a new character, its save read, the hold ring painted ahead of the next open.
void Tick(const rt::PressFrame& f, const ia::Snapshot& s, bool on, const qr::Tuning& t, double nowS) {
    st.nextTickS = nowS + .5;
    st.bound = on ? qr::ButtonSlot() : -1;
    if (!on || !f.fresh) return;
    if (s.session != st.session || s.skillClass != st.cls) {   // a new character (or the first): its ring is read again
        const bool had = st.cls != -2;
        st.session = s.session; st.cls = s.skillClass; st.identified = false; st.answered = false; st.identRetryS = 0; st.movedBefore.clear();
        st.key.clear(); st.name.clear(); st.ring = {};
        (void)had;
    }
    if (!qr::ClassKnown(s.skillClass)) {
        if (st.toldClass != s.skillClass && f.say) {
            char line[200]; std::snprintf(line, sizeof line, "Frame quick radial: class %d keeps upstream (the ring stays shut on its button)", s.skillClass);
            f.say(line);
        }
        st.toldClass = s.skillClass;
        return;
    }
    st.toldClass = s.skillClass;
    auto& w = W();
    if (w.hasNote.load(std::memory_order_acquire)) {   // a save's outcome (S-C), said here on the frame thread
        std::string note; { std::lock_guard lock(w.m); note = std::move(w.note); w.hasNote.store(false, std::memory_order_relaxed); }
        if (f.say && !note.empty()) f.say(note.c_str());
    }
    if (w.hasIdent.load(std::memory_order_acquire)) {
        Ident r;
        { std::lock_guard lock(w.m); r = std::move(w.ident); w.hasIdent.store(false, std::memory_order_relaxed); }
        if (r.session == st.session && r.cls == st.cls) {
            char line[260];
            st.answered = true;   // the page: Saved or This game, no longer Reading
            if (!r.ok) {   // said once per reason and level, not every 10 s
                if (r.twins) std::snprintf(line, sizeof line, "Frame quick radial: two or more class %d saves at level %d: not sure which is played; the ring is kept for this game only", r.cls, r.level);
                else std::snprintf(line, sizeof line, "Frame quick radial: no save of class %d at level %d yet; the ring is kept for this game, read again every 10 s", r.cls, r.level);
                const int said = (r.twins ? 1 : 2) + r.level * 4;
                if (said == st.noSaveTold) line[0] = 0;
                st.noSaveTold = said;
            }
            else {
                st.identified = true; st.key = r.key; st.name = r.name;
                if (!r.found) {   // no ring stored yet: this game's ring (moved skills, the ring menu's edits) is saved as it is
                    const bool keep = st.ring.n > 0;
                    std::snprintf(line, sizeof line, "Frame quick radial: character (class %d, level %d): %s", r.cls, r.level,
                                  keep ? "no ring stored yet: this game's ring saved" : "no ring stored yet (empty: fill it from the ring menu's Quick cast)");
                    if (keep) { std::lock_guard lock(w.m); w.saves.emplace_back(st.key, qr::Encode(st.ring)); }
                } else {   // the stored ring wins; what was put on the live ring before (still there) is added to it, never over it
                    const qr::Ring live = st.ring;
                    st.ring = r.ring;
                    int added = 0;
                    for (const auto& m : st.movedBefore) {
                        if (!live.Has(m.it.skill) || st.ring.Has(m.it.skill)) continue;   // taken out again on the page, or stored already
                        if (AddToRing(st.ring, m.it, t.fixed)) { ++added; continue; }   // Fixed: the first empty place
                        if (!m.page) TakenOff(s, m.it.skill, m.from, m.where.c_str(), "the stored ring is full (12)", f.say);
                        else if (f.say) { char full[240]; std::snprintf(full, sizeof full, "Frame quick radial: %s (put in the ring menu before the save was read) not added: the stored ring is full (12)", NameOf(s, m.it.skill).c_str()); f.say(full); }
                    }
                    const int count = st.ring.Count();
                    std::snprintf(line, sizeof line, "Frame quick radial: character (class %d, level %d): %d item%s loaded%s", r.cls, r.level, count, count == 1 ? "" : "s",
                                  added ? " (with what this game put on it before, saved)" : "");
                    if (added) { std::lock_guard lock(w.m); w.saves.emplace_back(st.key, qr::Encode(st.ring)); }
                }
                st.movedBefore.clear();
                w.cv.notify_one();
            }
            if (f.say && line[0]) f.say(line);
        }
    }
    if (!st.identified && nowS >= st.identRetryS && s.experience.level > 0) {
        st.identRetryS = nowS + 10;
        { std::lock_guard lock(w.m); w.identWanted = true; w.identClass = s.skillClass; w.identLevel = int(s.experience.level); w.identSession = s.session; }
        w.cv.notify_one();
    }
    if (!st.ctl.Open() && st.bound >= 0) {   // the hold ring painted ahead of the next open, only while a button is bound (nothing is painted or kept by a player who never binds one)
        const auto es = qr::EntriesFor(qr::Mode::Cast, st.ring, t.fixed);
        const uint64_t key = KeyOf(es, s, t);
        st.wantKey = key;
        if (key != st.askedKey) AskPaint(es, s, t, key);
    }
}
// A menu (Glyphs, Gestures, Quick cast) put on a button and no free button could take that button's game skill back: it goes
// on this character's ring (appended last, saved as an edit), or it is said taken off that button with three short pulses on its
// hand (Off, another class, the ring full); on the ring already, it is kept there (no pulses). Before the character's
// save is read it goes on the live ring and is merged into the stored one once read, never saved over it.
void MoveIn(const ia::Snapshot& s, bool on, bool fixed, void (*say)(const char*)) {
    namespace lk = framevr::quickradial::link;
    const int code = lk::moveAsk.exchange(-1, std::memory_order_acq_rel);
    if (code < 0) return;
    const int id = lk::MoveSkill(code), from = lk::MoveSlot(code);
    const char* where = lk::moveFrom[0] ? lk::moveFrom : "its button";
    const auto* k = Find(s, id);
    if (on && k && st.ring.Has(id)) {
        (void)where;   // on the ring already: kept there, nothing to say
        return;
    }
    const char* no = !on ? "Quick cast is Off" : !qr::ClassKnown(st.cls) ? "this class keeps upstream" : !k ? "not this character's skill" :
                     st.ring.Count() >= qr::MaxItems ? "the ring is full (12)" : nullptr;
    if (no || !AddToRing(st.ring, {id, k->owner}, fixed)) { TakenOff(s, id, from, where, no ? no : "refused", say); return; }
    char what[200];
    const int at = PlaceOf(st.ring, id);
    if (fixed) std::snprintf(what, sizeof what, "added %s (a menu took %s; place %d, the first empty one)", NameOf(s, id).c_str(), where, qr::PlaceNumber(at, true));
    else std::snprintf(what, sizeof what, "added %s (a menu took %s; item %d)", NameOf(s, id).c_str(), where, st.ring.n);
    if (st.identified) { AskSave(say, what); return; }
    st.movedBefore.push_back({{id, k->owner}, from, where});   // not an edit over the stored ring; merged into it once read
    (void)what;   // kept for this game until the save is read, then merged (st.movedBefore)
}
// The ring menu's Quick cast page reads this character's ring (link::view), published
// here on a change (a copy of 12 ids under the link's lock; the 0.5 s tick, an edit, a moved skill).
void PublishView(bool on) {
    namespace lk = framevr::quickradial::link;
    static_assert(std::tuple_size_v<decltype(lk::View::ids)> == size_t(qr::MaxItems), "the view holds every place");
    const bool ready = on && qr::ClassKnown(st.cls);
    const lk::Save save = st.identified ? lk::Save::Saved : st.answered ? lk::Save::ThisGame : lk::Save::Reading;
    // Fixed places shows each place as it is (an empty one -1); Growing the packed ring (a ring Fixed left with gaps
    // closes up, as Growing's edits do)
    const bool fixed = true;   // Fixed places
    qr::Ring shown = st.ring;
    if (!fixed && !shown.Packed()) shown.Pack();
    std::lock_guard lock(lk::m);
    auto& v = lk::view;
    bool same = v.ready == ready && v.save == save && v.session == st.session && v.cls == st.cls && v.n == shown.n && v.fixed == fixed;
    for (int i = 0; i < shown.n && same; ++i) same = v.ids[size_t(i)] == shown.items[size_t(i)].skill;
    if (same) return;
    v.ready = ready; v.save = save; v.session = st.session; v.cls = st.cls; v.n = shown.n; v.fixed = fixed;
    for (int i = 0; i < qr::MaxItems; ++i) v.ids[size_t(i)] = i < shown.n ? shown.items[size_t(i)].skill : -1;
    ++v.rev;
}
// The ring menu's place edits (link::Ask), applied on the press frame after them: put an item in a place (qr::PutAt), clear one
// (qr::ClearAt) or move a place's item to another (qr::MoveTo), each by Quick cast layout (Fixed places or Growing);
// saved as edits (AskSave) and published. Another game's, Off or another class: dropped, said. Places in the log by the page's
// names (Fixed: the clock's hours).
void ApplyEdits(const ia::Snapshot& s, bool on, bool fixed, void (*say)(const char*)) {
    namespace lk = framevr::quickradial::link;
    // while this character's save is being read (the page asks nothing then; a race only) the edits wait for it
    if (on && qr::ClassKnown(st.cls) && st.session == s.session && !st.identified && !st.answered) return;
    std::array<lk::Edit, lk::MaxEdits> got{}; int n = 0;
    { std::lock_guard lock(lk::m); n = std::min(lk::editN, lk::MaxEdits); got = lk::edits; lk::editN = 0; lk::editWaiting.store(false, std::memory_order_relaxed); }
    char line[260], what[200];
    const char* layout = fixed ? "Fixed places" : "Growing";
    const auto place = [&](int p) { return qr::PlaceNumber(p, fixed); };                   // a place as the page named it
    const auto shown = [&](int real) { return fixed ? qr::PlaceNumber(real, true) : PackedIndex(st.ring, real) + 1; };   // where an item is now, as the page shows it
    for (int i = 0; i < n; ++i) {
        const auto& e = got[size_t(i)];
        // Growing over a ring Fixed left with gaps: the page shows it packed (its places 1..n). An edit that keeps the
        // order (a put in a filled place or past the last, a clear, a swap) is made on the real places, so Fixed's places survive;
        // only a move to past the last item reorders, and that packs the ring (said).
        int at = e.place, from = e.from;
        bool opFixed = fixed;
        if (!fixed && !st.ring.Packed() && e.place >= 0 && e.place < qr::MaxItems) {
            const bool pastEnd = e.place >= st.ring.Count();
            if ((pastEnd && (e.from >= 0 || (e.id >= 0 && st.ring.Has(e.id)))) || (pastEnd && st.ring.n >= qr::MaxItems)) {
                st.ring.Pack();   // a move to the end closes the gaps (Growing)
            } else { at = pastEnd ? st.ring.n : RealPlace(st.ring, e.place); from = e.from >= 0 ? RealPlace(st.ring, e.from) : -1; opFixed = true; }
        }
        const char* no = !on ? "switched Off" : !qr::ClassKnown(st.cls) ? "this class keeps upstream" : e.session != st.session ? "another game" :
                         e.place < 0 || e.place >= qr::MaxItems || e.from >= qr::MaxItems ? "no such place" : nullptr;
        if (no) {   // the ring menu said "asked"; the drop is said here, three short pulses on the pointing hand
            if (e.hand >= 0) Pulses(e.hand, 3);
            if (say) { std::snprintf(line, sizeof line, "Frame quick radial: a ring menu edit of place %d dropped: %s", place(e.place), no); say(line); }
            continue;
        }
        if (e.from >= 0) {   // Move: place from's item to place; a filled place: the two swap
            const bool swap = st.ring.Filled(at);
            const int movedId = st.ring.Filled(from) ? st.ring.items[size_t(from)].skill : -1;
            const std::string was = movedId >= 0 ? NameOf(s, movedId) : std::string();
            const std::string other = swap ? NameOf(s, st.ring.items[size_t(at)].skill) : std::string();
            if (movedId >= 0 && qr::MoveTo(st.ring, from, at, opFixed)) {
                if (swap) std::snprintf(what, sizeof what, "moved %s from place %d to place %d, %s to place %d (swapped; the ring menu, %s)", was.c_str(), place(e.from), place(e.place), other.c_str(), place(e.from), layout);
                else std::snprintf(what, sizeof what, "moved %s from place %d to place %d (the ring menu, %s)", was.c_str(), place(e.from), shown(PlaceOf(st.ring, movedId)), layout);   // Growing: last
                AskSave(say, what);
            }
            continue;
        }
        if (e.id < 0) {
            const std::string was = st.ring.Filled(at) ? NameOf(s, st.ring.items[size_t(at)].skill) : std::string();
            if (qr::ClearAt(st.ring, at, opFixed)) { std::snprintf(what, sizeof what, "took %s out of place %d (the ring menu%s)", was.c_str(), place(e.place), fixed ? ", Fixed places: nothing else moved" : ""); AskSave(say, what); }
            continue;
        }
        const bool item = qr::IsItemId(e.id);
        const ia::Skill* k = nullptr;
        if (!item) {
            for (unsigned c = 0; c < s.skillCount && c < s.catalog.size() && !k; ++c) if (s.catalog[c].id == e.id && s.catalog[c].owner == e.owner && s.catalog[c].known) k = &s.catalog[c];
            if (!k) k = Find(s, e.id);
        }
        if (!item && (!k || !qr::IsSkillId(e.id))) { if (say) { std::snprintf(line, sizeof line, "Frame quick radial: ring menu: %s not put in place %d: not this character's skill now", NameOf(s, e.id).c_str(), place(e.place)); say(line); } continue; }
        if (const qr::Item it{e.id, item ? -1 : k->owner}; qr::PutAt(st.ring, at, it, opFixed)) {
            std::snprintf(what, sizeof what, "put %s in place %d (the ring menu%s)", NameOf(s, e.id).c_str(), shown(PlaceOf(st.ring, e.id)), fixed ? ", Fixed places" : "");
            AskSave(say, what, &it);
        }
    }
    PublishView(on);
}
// The button and Open by from the plugin's ini, once, on the first press frame (a few microseconds; the worker is not needed for
// a read this small). The button is the ring menu's to change afterwards (SetButton), saved by the glue when it changes.
bool settingsLoaded = false;
void LoadSettings() {
    settingsLoaded = true;
    const auto file = IniFile();
    if (file.empty()) return;
    if (qr::state::buttonSet.load(std::memory_order_acquire) == 0) {
        const int b = int(GetPrivateProfileIntW(L"quick_radial", L"button", 0, file.c_str()));
        qr::state::button.store(b >= 1 && b <= 10 ? b : 0, std::memory_order_relaxed);
    }
    openByToggle.store(GetPrivateProfileIntW(L"quick_radial", L"open_by", 0, file.c_str()) == 0, std::memory_order_relaxed);
}
// The button changed (SetButton: a bind landed, or another menu took the button): kept in the ini by the worker.
void SaveButton() {
    auto& w = W();
    { std::lock_guard lock(w.m); w.saves.emplace_back("button", std::to_string(qr::state::button.load(std::memory_order_relaxed))); }
    w.cv.notify_one();
}}  // namespace

namespace framevr::quickradial::rt {
// transport Input, before the right stick's gestures: the sticks read (and the ring's zeroed), a finished picture uploaded.
void Input(InputFrame f) {
    const int walk = f.walkHand == 1 ? 1 : 0;
    st.stick[walk][0] = f.lx / 32767.f; st.stick[walk][1] = f.ly / 32767.f;
    st.stick[1 - walk][0] = f.rx / 32767.f; st.stick[1 - walk][1] = f.ry / 32767.f;
    st.srgb = f.srgb;
    if (st.ownStick) { if (st.stickHand == walk) f.lx = f.ly = 0; else f.rx = f.ry = 0; }
    ++st.frame;
    if (ringShared.rebuilt.exchange(false, std::memory_order_acq_rel)) {   // everything painted again (the next tick or frame asks)
        st.uploadedKey = st.askedKey = 0;
    }
    if (worker && worker->hasDone.load(std::memory_order_acquire)) {
        std::shared_ptr<paint::Picture> pic;
        { std::lock_guard lock(worker->m); pic = std::move(worker->done); worker->hasDone.store(false, std::memory_order_relaxed); }
        if (pic && f.upload) {
            const auto A = qr::shared::A, B = qr::shared::B;
            const bool ok = f.upload(A.x, A.y, A.w, A.h, &pic->a) && f.upload(B.x, B.y, B.w, B.h, &pic->b);
            if (ok) { st.uploadedKey = pic->key; st.uploadFrame = st.frame; }
            if (!ok && f.say) f.say("Frame quick radial: a picture was NOT uploaded (the shared image refused it)");
        }
    }
    if (st.uploadedKey && st.frame >= st.uploadFrame + 2 && ringShared.liveKey.load(std::memory_order_relaxed) != st.uploadedKey)
        ringShared.liveKey.store(st.uploadedKey, std::memory_order_release);
}
// transport Input: upstream's press this frame (before the commander button block).
void Press(PressFrame f) {
    if (!settingsLoaded) { LoadSettings(); st.buttonSeen = qr::state::buttonSet.load(std::memory_order_acquire); }
    const uint32_t buttonNow = qr::state::buttonSet.load(std::memory_order_acquire);
    const bool buttonMoved = buttonNow != st.buttonSeen;
    const double nowS = f.time;
    if (!st.busy && !buttonMoved && (st.bound < 0 || f.slot != st.bound) && nowS < st.nextTickS && !st.drawn && st.buzzCount == 0 &&
        framevr::quickradial::link::moveAsk.load(std::memory_order_relaxed) < 0 && !framevr::quickradial::link::editWaiting.load(std::memory_order_relaxed)) return;   // closed, not our press: one branch
    const bool on = qr::Enabled();
    if (!on) { if (st.offHandled) return; st.offHandled = true; } else st.offHandled = false;   // switched off: the Off cleanup below runs once, then nothing (no tick, no lock)
    if (buttonMoved) { st.buttonSeen = buttonNow; st.nextTickS = 0; st.ownerTold = -2; SaveButton(); }
    const auto& snap = *static_cast<const ia::Snapshot*>(f.snapshot);
    const qr::Tuning t = ReadTuning();
    if (nowS >= st.nextTickS || !on) { Tick(f, snap, on, t, nowS); PublishView(on); }
    if (framevr::quickradial::link::moveAsk.load(std::memory_order_relaxed) >= 0) { MoveIn(snap, on, t.fixed, f.say); PublishView(on); }   // a skill a menu took a button from
    if (framevr::quickradial::link::editWaiting.load(std::memory_order_acquire)) ApplyEdits(snap, on, t.fixed, f.say);   // the ring menu's Quick cast page
    const auto& hi = *static_cast<const framevr::skills::HandInput*>(f.hands);
    qr::FrameIn in;
    in.nowMs = nowS * 1000.0;
    in.on = on;
    in.bound = st.bound;
    in.held = f.slot;
    int button = -1;
    const int bh = st.bound >= 0 ? framevr::skills::Hand(st.bound) : 0;
    for (int b = 0; b < 5 && st.bound >= 0; ++b) if (framevr::skills::HandSlot(bh, b) == st.bound) button = b;
    in.hand = bh;
    in.down = button >= 0 && (button == 0 ? hi.trigger[bh] > .2f : hi.face[bh][(button - 1) % 2]);
    const bool skillThere = st.bound >= 0 && snap.skills[size_t(st.bound)].known && snap.skills[size_t(st.bound)].id >= 0;
    const bool glyphThere = st.bound >= 0 && (f.glyphDraw[0] == st.bound || f.glyphDraw[1] == st.bound);
    in.slotFree = st.bound >= 0 && (f.fresh ? !skillThere && !glyphThere : st.ctl.Open());
    in.classOk = qr::ClassKnown(st.cls);
    in.wheelOpen = f.wheel;
    in.occupied = f.occupied;
    in.tracked = f.headTracked;
    const int ownHand = st.ctl.Open() ? st.ctl.hand : bh;   // the open ring's hand, else the bound button's
    in.stick[0] = st.stick[ownHand][0]; in.stick[1] = st.stick[ownHand][1];
    in.eye = {f.head[0], f.head[1], f.head[2]};
    in.head = {f.head[3], f.head[4], f.head[5], f.head[6]};
    if (st.buzzCount > 0 && f.pulse && in.nowMs >= st.buzzMs) {   // an item's outcome (from the request seam): its buzz, or three short ones
        f.pulse(st.buzzHand, st.buzzAmp, st.buzzShort ? 60000000 : uint64_t(std::max(1.f, t.castMs) * 1e6));
        if (--st.buzzCount > 0) st.buzzMs = in.nowMs + 140;
    }
    if (st.bound >= 0 && f.fresh) {   // which of a game skill, a Draw glyph or nothing holds the bound button (ownerTold)
        const int owner = skillThere ? 1 : glyphThere ? 2 : 0;
        st.ownerTold = owner;
    }
    const bool wasOpen = st.ctl.Open();
    const qr::FrameOut out = st.ctl.Step(in, st.ring, t);
    if (out.mute) { f.requested = -1; f.interact = false; }
    st.ownStick = out.ownStick; st.stickHand = st.ctl.Open() ? st.ctl.hand : bh;
    st.busy = st.ctl.Open() || st.ctl.stickHold || st.ctl.pressHold;
    if (out.tick) {
        ++st.picks;
        if (t.tickMs > 0 && t.tickAmp > 0 && f.pulse) f.pulse(bh, t.tickAmp, uint64_t(t.tickMs * 1e6));
    }
    char line[320];
    const auto head = [&](char* buf, size_t n) {
        std::snprintf(buf, n, "Frame quick radial: %s hand, slot %d, %.2f s, %d pick%s", bh ? "left" : "right", st.bound, (in.nowMs - st.openedMs) / 1000.0, st.picks, st.picks == 1 ? "" : "s");
    };
    switch (out.event) {
        case qr::Event::Opened:
            st.openedMs = in.nowMs; st.picks = 0;
            ringShared.open.fetch_add(1, std::memory_order_relaxed);
            break;
        case qr::Event::Cast: {
            const int id = st.ring.items[size_t(out.index)].skill;
            if (qr::IsItemId(id)) {   // an item: upstream's belt key or its inventory use, through the request seam
                const auto v = ItemInfo(snap, id, t);
                head(line, sizeof line);
                if (!v.pick.ok || st.use.want) {
                    std::snprintf(line + std::strlen(line), sizeof line - std::strlen(line), ": %s (%s) not used: %s", v.name.c_str(), ItemWords(st.ring, out.index, t.fixed).c_str(),
                                  st.use.want ? "the last item is still on its way" : v.pick.why);
                    if (f.say) f.say(line);

                    Pulses(bh, 3); st.buzzMs = in.nowMs;
                    break;
                }
                auto& u = st.use;
                u = {}; u.want = true; u.hand = bh; u.slot = st.bound; u.index = t.fixed ? out.index : PackedIndex(st.ring, out.index); u.n = st.ring.Count(); u.fixed = t.fixed; u.picks = st.picks; u.heldS = (in.nowMs - st.openedMs) / 1000.0;
                std::snprintf(u.name, sizeof u.name, "%s", v.name.c_str());
                const std::string what = qr::CodeName(v.pick.code);
                if (v.pick.column >= 0) {
                    u.r.action = ia::Action::Potion; u.r.index = v.pick.column;
                    u.r.item = snap.belt[size_t(v.pick.column)].Next(); u.r.revision = snap.belt[size_t(v.pick.column)].revision;
                    std::snprintf(u.from, sizeof u.from, "belt column %d", v.pick.column + 1);
                } else {
                    const ia::QuickItem* row = nullptr;
                    for (unsigned i = 0; i < snap.quick.count && i < snap.quick.items.size() && !row; ++i)
                        if (qr::Code3(snap.quick.items[i].item.code) == qr::Code3(v.pick.code) && snap.quick.items[i].quantity > 0) row = &snap.quick.items[i];
                    if (!row) { u = {}; break; }
                    u.r.action = ia::Action::UseItem; u.r.index = int(row->item.code); u.r.item = row->item;
                    std::snprintf(u.from, sizeof u.from, "the inventory");
                }
                std::snprintf(u.name, sizeof u.name, "%s: %s", v.name.c_str(), what.c_str());
                break;
            }
            const auto* k = Find(snap, id);
            if (!Usable(k)) {   // shown dim: never cast
                head(line, sizeof line);
                std::snprintf(line + std::strlen(line), sizeof line - std::strlen(line), ": %s (%s) not cast: %s", NameOf(snap, id).c_str(), ItemWords(st.ring, out.index, t.fixed).c_str(),
                              !k ? "not this character's skill now" : "the game refuses it now (shown dim)");
                if (f.say) f.say(line);

                if (f.pulse) f.pulse(bh, .25f, 60000000);
            } else {   // the Cast row starts it through upstream's glyph press this frame, then logs and buzzes
                auto& p = st.pending;
                p = {}; p.on = true; p.skill = id; p.owner = k->owner; p.hand = bh; p.slot = st.bound; p.index = t.fixed ? out.index : PackedIndex(st.ring, out.index); p.n = st.ring.Count(); p.fixed = t.fixed;
                p.picks = st.picks; p.since = GetTickCount64(); p.heldS = (in.nowMs - st.openedMs) / 1000.0;
                std::snprintf(p.name, sizeof p.name, "%s", NameOf(snap, id).c_str());
            }
            break;
        }
        case qr::Event::None:
            if (out.emptyBack) {   // Toggle: the stick back from a place with no item: nothing cast, two pulses, still open
                Pulses(bh, 2); st.buzzMs = in.nowMs;
            }
            break;
        default: break;
    }
    // What the ring shows: painted again when it changed (the worker; it shows once uploaded), the quads every open frame.
    if (st.ctl.Open()) {
        const auto& es = st.ctl.shown;
        const uint64_t key = KeyOf(es, snap, t);
        st.wantKey = key;
        if (key != st.askedKey) AskPaint(es, snap, t, key);
        Publish(st.ctl.DrawNow(t), key);
        st.drawn = true;
    } else if (st.drawn || wasOpen) {
        Publish(qr::DrawList{}, 0);
        st.drawn = false;
    }
    if (!on) {   // the Off call: nothing fires after Off; the ring menu's asks are dropped (none waits for the next On)
        st.pending = {}; st.ownStick = false; st.bind.Forget();
        namespace lk = framevr::quickradial::link;
        lk::moveAsk.store(-1, std::memory_order_relaxed);
        { std::lock_guard lock(lk::m); lk::editN = 0; lk::editWaiting.store(false, std::memory_order_relaxed); }
        PublishView(false);
    }
}
// transport Input, before glyph casting's press step: a queued cast through upstream's glyph press. One branch when none waits.
void Cast(CastFrame f) {
    if (!st.pending.on) return;
    if (!qr::Enabled()) { st.pending = {}; return; }   // switched off: nothing fires after
    if (!f.snapshot || !f.cast) return;
    const auto p = st.pending;
    const auto& s = *static_cast<const ia::Snapshot*>(f.snapshot);
    rw::CastWait w{true, p.skill, p.owner, p.hand, p.slot, 0};
    const auto r = rw::StartCast(w, *static_cast<framevr::glyphcast::Cast*>(f.cast), s, true, f.fresh, f.active, !f.wheel && !f.block, f.now,
                                 double(GetTickCount64() - p.since), [&](int skill, int owner) { return f.park && f.park(f.session, skill, owner); });
    if (w.on) return;   // a wheel still shut ahead of it (at most 1 s)
    st.pending = {};
    char line[360];
    std::snprintf(line, sizeof line, "Frame quick radial: %s hand, slot %d, %.2f s, %d pick%s: %s (%s) ", p.hand ? "left" : "right", p.slot, p.heldS, p.picks,
                  p.picks == 1 ? "" : "s", p.name, ItemWords(p.index, p.n, p.fixed).c_str());
    if (r.refused || !r.started) {
        std::snprintf(line + std::strlen(line), sizeof line - std::strlen(line), "not cast: %s", r.refused ? r.refused : "upstream's press did not start");
        if (f.pulse) f.pulse(p.hand, .25f, 60000000);
        if (f.say) f.say(line);
        return;
    }
    if (const auto* k = Find(s, p.skill); f.press && k) f.press(k);   // the aim queries see the cast (glyphPress.skill; the aim stays the player's)
    const qr::Tuning t = ReadTuning();
    if (t.castMs > 0 && t.castAmp > 0 && f.pulse) f.pulse(p.hand, t.castAmp, uint64_t(t.castMs * 1e6));
}
// transport EndPair: the ring's quads from the shared image, all of them or none.
void Layer(const LayerFrame& f) {
    static XrCompositionLayerQuad quads[qr::MaxQuads];   // lives until xrEndFrame (the submit keeps a pointer)
    static uint64_t chainWas = 0;
    if (f.chain && f.chain != chainWas) {   // a new shared image (session rebuilt): its regions are empty until painted again
        if (chainWas) { ringShared.liveKey.store(0, std::memory_order_release); ringShared.rebuilt.store(true, std::memory_order_release); }
        chainWas = f.chain;
    }
    if (!f.layer || !f.chain || ringShared.n.load(std::memory_order_relaxed) == 0) return;
    const uint32_t s1 = ringShared.seq.load(std::memory_order_acquire);
    if (s1 & 1) return;
    const qr::DrawList d = ringShared.list;
    const uint64_t key = ringShared.drawKey.load(std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_acquire);
    if (ringShared.seq.load(std::memory_order_relaxed) != s1 || d.n == 0) return;
    if (key != ringShared.liveKey.load(std::memory_order_acquire)) return;   // its picture not in the shared image yet: nothing (a frame or two)
    if (f.count + uint32_t(d.n) > f.limit) {   // no room for all three: none, every upstream layer kept (said once an open)
        const uint32_t opens = ringShared.open.load(std::memory_order_relaxed);
        if (st.noRoomTold != opens && f.say) {
            char line[140]; std::snprintf(line, sizeof line, "Frame quick radial: no room for its %d quads (%u of %u layers used), not drawn (said once an open)", d.n, f.count, f.limit);
            f.say(line);
        }
        st.noRoomTold = opens;
        return;
    }
    for (int i = 0; i < d.n; ++i) {
        const auto& q = d.q[size_t(i)];
        const auto region = qr::shared::Of(q.region);
        quads[i] = {XR_TYPE_COMPOSITION_LAYER_QUAD};
        const int rect[4] = {region.x + q.rect.x, region.y + q.rect.y, q.rect.w, q.rect.h};
        f.quad(f.quadCtx, &quads[i], q.pose, q.w, q.h, rect, f.chain, true);
    }
}
// After interactions.Step: a BindSkill for the radial (the ring wheel's hold to assign on its shelf entry) made upstream's clear of
// the button's game skill; the reply kept.
std::atomic<uint64_t> useSeq{0};   // the inventory use in flight: QuickUse lets exactly this request through
void Request(RequestFrame f) {
    if (!qr::Enabled()) { st.bind.Forget(); st.use = {}; useSeq.store(0); return; }   // switched off: a bind or an item still waiting is forgotten
    if (!f.request || !f.reply) return;
    auto& r = *static_cast<ia::Request*>(f.request);
    {   // an item entry's use: sent once when the request is free, its answer logged and buzzed
        auto& u = st.use;
        const double nowMs = double(GetTickCount64());
        if (u.want && u.sent) {
            const auto& rep = *static_cast<const ia::Reply*>(f.reply);
            const bool mine = rep.sequence == u.seq && rep.result != ia::Result::None && rep.result != ia::Result::Pending;
            if (mine || nowMs - u.sentMs > 2500) {
                const bool ok = mine && (rep.result == ia::Result::Applied || rep.result == ia::Result::Dispatched);
                char line[360];
                std::snprintf(line, sizeof line, "Frame quick radial: %s hand, slot %d, %.2f s, %d pick%s: %s (%s) %s from %s%s", u.hand ? "left" : "right", u.slot, u.heldS,
                              u.picks, u.picks == 1 ? "" : "s", u.name, ItemWords(u.index, u.n, u.fixed).c_str(), ok ? (u.r.action == ia::Action::Potion ? "drunk (upstream confirmed it gone)" : "used (upstream's inventory use dispatched)") : "not used",
                              u.from, ok ? "" : mine ? (rep.result == ia::Result::Cancelled ? ": cancelled (a new game)" : ": refused by upstream") : ": no answer in 2.5 s");
                if (!ok && f.say) f.say(line);   // only a failed use is logged

                const qr::Tuning t = ReadTuning();
                if (ok) { if (t.castMs > 0 && t.castAmp > 0) { st.buzzHand = u.hand; st.buzzMs = 0; st.buzzAmp = t.castAmp; st.buzzCount = 1; st.buzzShort = false; } }
                else Pulses(u.hand, 3);
                useSeq.store(0, std::memory_order_release);
                u = {};
            }
        } else if (u.want && r.action == ia::Action::None && f.snapshot) {
            ia::Request q = u.r;
            q.sequence = (1ull << 62) | ++st.useNumber; q.session = f.session; q.at = f.now;
            if (q.action == ia::Action::UseItem) useSeq.store(q.sequence, std::memory_order_release);
            r = q; u.seq = q.sequence; u.sent = true; u.sentMs = nowMs;
        }
    }
    st.bind.Take(r);
    int slot = -1;
    const auto done = st.bind.Reply(*static_cast<const ia::Reply*>(f.reply), slot);
    if (done == qr::BindWatch::Done::None) return;
    const bool ok = done == qr::BindWatch::Done::Applied;
    if (ok) { qr::SetButton(float(slot + 1)); qr::link::bindApplied.store(slot, std::memory_order_release); st.bound = slot; st.ownerTold = -2; }   // the ring menu lets the other menus on that button go (link::bindApplied)
    if (f.say) {
        char line[160];
        std::snprintf(line, sizeof line, "Frame quick radial: slot %d %s", slot, ok ? "is the radial's button (saved)" : "refused by the game: the button keeps what it had");
        f.say(line);
    }
}
// vr/quick_items.cpp Use (UI thread): upstream's inventory use takes only its one assigned item; the radial's own request (its
// sequence, in flight) is let through for its code, for that call only (the caller puts the assignment back).
uint32_t QuickUse(const void* request, uint32_t assigned) {
    const auto* r = static_cast<const ia::Request*>(request);
    const uint64_t seq = useSeq.load(std::memory_order_acquire);
    if (r && seq && r->sequence == seq && r->action == ia::Action::UseItem && r->index != 0) return uint32_t(r->index);
    return assigned;
}
}  // namespace framevr::quickradial::rt