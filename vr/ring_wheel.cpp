// Lab Ring wheel (Lab > Experiment: Ring wheel; exp.ring_wheel, Dev only; ticket T025, 2026-11-07). Jeff: "let's also add a
// new lab item for turn on the new skill menu ... the class skills are broken into 3 sections on the screen. When you mouse
// over one areas the other two tree skills fade away ... The center of the skill tree menu would give the helper text for the
// mouse over skill ... One side to open glyphs and the other gestures". Plan: vault build-plan.md in
// Tickets\025-ring-skill-wheel-lab (E1 seams, E9 build order); frame budget: frame-budget.md in the same folder.
//
// The ring is a skin over upstream's wheel engine (interaction::Engine), not a second engine: upstream keeps open and close,
// pages, the footer, editors and every request; this Lab moves the panel, turns laser hits into upstream cells or ring-only
// events, queues BindSkill requests upstream's dispatch already takes, and swaps the wheel quad's picture.
//
// T036 (ring tree cleanup, vault Tickets\036-ring-tree-cleanup, mockup Design\ring-wheel\t036): each skill circle in layers, each
// its own row on Ring wheel > Layers (Icon: the clean icon, keyed on the atlas worker; Color ring; Element frame; Float;
// Animation: the old rest glows, over-face passes and crawl); the center in thirds (Glyphs, Gestures, Quick cast; ring_wheel.h
// ThirdAt), each third a pseudo seat (a tap opens it, a hold puts that sub-menu on the button); Glyphs and Gestures on the General
// shelf (Center > Sub-menus on the shelf); a sub-menu's button opens its wheel there (FilterRingHands).
// T035 + T036 headset fixes (2026-11-16, Jeff: "ONE quick cast"): the Quick cast page (upstream's eight button slots) is gone. The
// Quick cast third is Lab Quick cast radial: a tap opens its page in the ring (12 places, a chooser each; 2026-11-16), a hold puts the radial on that
// button (its own bind, as its shelf entry's); its switch Off, the third says so and does nothing. A seat or third under the laser
// takes a sub-menu button's press like any other button's (a hold rebinds it); a Lab menu bound over a game skill moves that skill
// (to the menu's old button, else into Quick cast) and says where.
// Build step R8: the animation layers (float, element glow, hover effects, icon art motion, tree burst, lightning crawl) from
// Ring wheel > Animation (the 4-value Animation row sets the six under it; a row changed by hand shows Custom), and the fight
// dim / death and load fade (draw only: a health drop dims all but the pointed icon to 40%; death or a stale snapshot 15%, no
// hits until the wheel opens again).
// Build step R7: the Glyphs and Gestures pages in the ring's look (steamvr/ring_wheel_pages.h): upstream's own pages and
// actions underneath (a shape's other hand, a gesture card, Back: upstream's cell for one frame); a shape's skill chosen on
// the three trees filtered to upstream's glyph picker list, sent as upstream's BindGlyph (its paged picker never opens).
// The center halves' grow delay and the blurb's hold are rows (Placement P10, P11).
// Build step R6Q: Jeff's quick cast. On the ring, any of the ten buttons (either hand) pressed on a skill:
// a tap casts it and closes the wheel (upstream's center cell for one frame, then upstream's own glyph press through the
// OnRingCast seam, so mana, cast rate and aim stay the game's and the player's); held to Hold to assign, it goes on that
// button (BindSkill); released between, no change. The plain cancel button (right B, left X) with the laser off the wheel
// closes it. Build step R6: the ring on the main (Spells) page. Placement (R3) opens the panel ahead of the eyes and
// keeps it in the room. On the Spells page, once the Lab's GPU pass and atlases are ready for the hero's class and skills,
// the ring draws (steamvr/ring_wheel_draw.h through vr/ring_wheel_compose.cpp into the Lab's own 1536 x 1320 swapchain)
// and replaces upstream's quad; the other hand's ray hits the ring (steamvr/ring_wheel.h Engine); a release on a footer tab
// or a center half becomes upstream's own cell for one frame; a skill picked, then one of the open hand's five buttons,
// binds it through upstream's BindSkill. Every other page (Glyphs, Gestures, editors, Equipment, Developer) and the time
// before the ring is ready: upstream's picture and hit test at the Lab's pose, as R3. The Map page: exactly upstream's.
//
// Rows (vr/lab_hooks_table.h), all on TabRingWheel with no key gate, so its master is the gate and the Off edge drains:
//  - FilterWheelPointer  transport Input, after MotionPointer: the spawn pose, the ray, the hit (ring: canvas px; else upstream's cell).
//  - FilterWheelInput    transport Input, before interactions.Step: the ring's engine step; upstream's hover / trigger = -1 / 0,
//                        or the released upstream cell for one frame; a bind queued.
//  - FilterWheelRequest  transport Input, after interactions.Step: the queued BindSkill when upstream sent none; its reply.
//  - OnWheelLayer        transport EndPair, after the wheel quad's if: the On edge's setup (swapchain, the atlas worker), the
//                        atlas slices, the compose and the quad; the Off edge's retire (never a wait on the frame thread).
//  - OnRingCast          transport Input, before glyph casting's press step: a queued quick cast starts once the wheel is
//                        shut (upstream's glyphcast::Dispatch, interactionstate::Park, glyphCast.Begin); feedback pulses.
//  - FilterWheelTracking transport Input, after handInput.active (round 2026-11-08 #1, ring_wheel_close.h): while the wheel is
//                        open only a deliberate action or a named safety reason closes it. FilterWheelInput and FilterWheelRequest
//                        do the same for the interaction engine and put its input back. Lab Off: upstream's values.
//  - VetoWheelBlip       transport Input, before the unfocused ClearInput: a blip under 250 ms keeps the open wheel.
//  - OnWheelBeam         transport EndPair, after the laser's if (job B): the non-owner hand's thin beam, no dot.
//  - OnLayerShot         transport EndPair, a screenshot's frame only: the ring's layer as its own picture (round 2026-11-08 item 4).
//  - FilterRingHands     transport Input, before HandControls steps (T036): a sub-menu's button opens that hand's wheel (its stick
//                        click's toggle) and the wheel goes to that sub-menu (not while a seat or third is under the laser).
//  Job B (pointing with either hand, ring_wheel_pointing.h): FilterWheelPointer gets both aims and the stepped buttons; the
//  owner gets upstream's beam, dot, hover and hit, its trigger is the click; row Pointing hand.
//  - OnUiReset           Always: joins the worker and frees everything after the GPU (session end).
//  - OnClearInput        transport ClearInput (the headset unfocused, the session ending, a failed sync or copy): the open
//                        wheel's close line, written there (audit 2026-11-10 bug 2). No new seam: upstream's own hook.
// Cost with the master Off: each seam is one relaxed load, an AND and a not-taken branch, its arguments never built.
// Telemetry "Frame ring wheel:" (vault Findings\Telemetry tags.md, plan E8): On / Off each edge; setup; "upstream: <reason>"
// once a session; placed at most once a 10 s; a Placement change at most once a second; "10 s:" while the ring is open; each bind.
#include <windows.h>
#include <d3d12.h>
#define XR_USE_GRAPHICS_API_D3D12
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
#include "lab_hooks.h"
#include "lab_store.h"
#include "lab_buttons.h"
#include "settings_menu.h"   // job B: the Button names row (vrsettings::Read), for the badges' letters
#include "ring_wheel_compose.h"
#include "ring_wheel_atlas.h"
#include "../steamvr/lab_quick_radial_link.h"
#include "../steamvr/lab_quick_radial.h"   // T035: a tap on Quick cast (its third, its shelf entry) opens its page; the radial's ring (link)
#include "../steamvr/ring_wheel_draw.h"
#include "../steamvr/ring_wheel_pages.h"
#include "../steamvr/ring_wheel_iconbox.h"
#include "../steamvr/ring_wheel_menu_art.h"   // T036 (2026-11-16): the sub-menus' own pictures
#include "../steamvr/ring_wheel_glyph_art_decode.h"   // Glyph icon: the painted glyph and sigil squares (the sheet's decode)
#include "../steamvr/ring_wheel_tree_art_decode.h"   // T025: the tree marks' painted pictures (the sheet's decode; the pure side comes with ring_wheel_draw.h)
#include "../steamvr/ring_wheel_close.h"
#include "../steamvr/hand_controls.h"
#include "../steamvr/gameplay.h"
#include "../steamvr/ring_wheel_pointing.h"
#include "../steamvr/spell_motion.h"
#include "../steamvr/glyph_cast.h"
#include "../steamvr/ring_wheel_grab.h"
#include "../steamvr/ring_wheel_life.h"
#include "../steamvr/ring_wheel_reply.h"
#include "../steamvr/ring_wheel_log.h"
#include "../steamvr/png_file.h"
#include "../steamvr/ring_wheel_shot.h"

namespace labringwheel {
namespace lab = framevr::lab;
namespace rw = framevr::ringwheel;
namespace tx = framevr::ringwheel::text;
namespace math = framevr::math;
namespace ia = framevr::interaction;
namespace qr = framevr::quickradial;
namespace {
bool told = false;          // the On line said, so the Off edge's drain call says Off once
void (*sayKept)(const char*) = nullptr;   // transport's Say as the seams hand it over, for the two hooks that carry no arguments (OnClearInput, OnUiReset)
bool developerNow = false;  // the snapshot's developer mode, for the pointer's quad (review R6 X2)
rw::Placer placer;
bool placed = false;        // this frame the Lab placed the panel (the layer seam sizes the quad)
float placedWidth = .8f;
ia::Pose placedPose{};
// Feedback 1 item 2: grip to move (steamvr/ring_wheel_grab.h, upstream's panels::Grab). The pointing hand's grip, trigger and
// face buttons come from FilterWheelInput (one frame late: no new upstream line); its ray's hit from the last Pointer.
rw::RingGrab ringGrab;
bool gripIn[2] = {}, triggerIn[2] = {}, faceIn[2] = {};
bool grabHit = false, grabPlaceSet = false;
rw::Placement grabPlace{};
int grabPage = -1;
uint64_t grabSession = 0;
ULONGLONG grabSaidAt = 0;
ULONGLONG placedToldAt = 0, tuneToldAt = 0;
rw::Placement tuneTold{};

// ---- The GPU side: held on the heap and never freed at DLL unload (review R4 J2); the Off edge retires it to the worker ----
struct Gpu {
    rw::Compose compose;
    rw::AtlasWorker worker;
    rw::IconBox icons;       // review R6 X3: the worker's icons, fetched by the frame thread (the worker never calls upstream)
    XrSwapchain chain = XR_NULL_HANDLE;
    std::vector<XrSwapchainImageD3D12KHR> images;
    PFN_xrDestroySwapchain destroy = nullptr;
    bool hasImage = false;   // an image was released (the quad may show it)
    // Perf's X6 check: the last compose's fence (our own reference) and value, taken at the Off edge when the swapchain stayed;
    // the frame thread destroys the swapchain once it is reached, without waiting for the worker. Null: nothing to wait for.
    ID3D12Fence* doneFence = nullptr;
    uint64_t doneValue = 0;
    ~Gpu() { if (doneFence) doneFence->Release(); }
};
Gpu* gpu = nullptr;
rw::Retired<Gpu> retiring;   // retired at Off edges: the GPU side freed on its worker; the swapchain and the rest in Reap (perf R6 #4, X1)
unsigned saidUpstream = 0;   // "upstream: <reason>" bits said this session

// ---- The ring's state (frame thread) ----
uint64_t seatsKey = 0;
bool seatsValid = false;
uint32_t revision = 0;       // the scene's data revision (review R5 K6): seats rebuilt in place, a new atlas
rw::Layout layout;
std::vector<std::string> initials;
std::vector<const char*> initialPtr;
rw::AtlasWorker::Request request;
std::shared_ptr<const rw::AtlasWorker::Built> built;
int seatClass = -1;
rw::Engine engine;
bool engineOpen = false;
rw::StepOut step{};
rw::QuickOut quickOut{};
unsigned seenFlips = ~0u;
float holdFracWas = 0;   // the hold ring's fill last frame (ticks at 25 / 50 / 75 %)
std::string holdSlotText, holdStatusText;
bool holdBound = false;   // the held button has a skill: "Replaces" over its name
uint64_t blurbKey = ~0ull;
ULONGLONG acquireToldAt = 0;
double acquireMax = 0;
bool ringActive = false;     // this frame the ring drew and hit-tests (the Spells page, ready)
bool hit = false; float hitX = 0, hitY = 0;   // the pointer's hit, canvas px
bool held[2][3] = {};        // the wheel hand's trigger, primary, secondary last frame (press edges)
rw::ComposePolicy policy;
tx::BlurbCache blurbs;
int blurbSeat = -1;
// A bind for FilterWheelRequest, and the one in flight.
// draw: the #3 Draw glyph button; menu (T036): a sub-menu's button (the clear first). A Lab menu's bind (a sub-menu, Quick cast,
// Army X menu; 2026-11-16 bug 4): was / wasOwner the game skill that button held, oldSlot the menu's button before, labName its name.
struct Bind { bool queued = false; int index = -1, skill = -1, owner = -1, slot = -1, hand = 0, layout = 0; bool draw = false; int menu = -1;
              int was = -1, wasOwner = -1, oldSlot = -1; const char* labName = nullptr; };
uint64_t bindSequence = 0;
rw::ReplyWait bindWait, glyphWait;   // review-r6 X4: the reply each sent request waits for (1.5 s at most)
// R6Q: the quick cast state machine, a cast waiting for the wheel to shut, feedback pulses (hand, when).
rw::QuickCast quick;
bool quickHeld[2][3] = {};   // each hand's trigger, primary, secondary last frame
struct PendingCast { bool on = false; int skill = -1, owner = -1, hand = 0, slot = -1, layout = 0; double held = 0; ULONGLONG since = 0; };
PendingCast pendingCast;
struct Pulse { int hand = 0; ULONGLONG at = 0; float strength = 0; };
std::array<Pulse, 8> pulses{};
int pulseCount = 0;
constexpr int CenterCell = 8;   // upstream's wheel center: a trigger there closes the wheel (interaction::Engine, wheelLatch)
void QueuePulses(int hand, int n, float strength) {
    const ULONGLONG now = GetTickCount64();
    for (int i = 0; i < n && pulseCount < int(pulses.size()); ++i) pulses[size_t(pulseCount++)] = {hand, now + ULONGLONG(i) * 90, strength};
}
Bind bindSent;
ULONGLONG windowAt = 0;
// R8: per seat its element and its order among the lightning icons; the fight fade; the Animation row's last value.
std::vector<uint8_t> elements;
std::vector<rw::RestGlow> restGlows;   // R8 match stage 1: per seat its rest glow (the kit's el list per skill)
int restFromKit = 0, restByTree = 0; bool restSaid = true;   // the last layout's counts, logged once a layout (Frame ring wheel: rest glow)
std::vector<int8_t> crawlIdx;
int crawlCount = 0;
rw::FightFade fightFade;
rw::FightOut fightNow{};
bool deadSaid = false;
ULONGLONG hitSaidAt = 0, animSaidAt = 0;
int animSeen = -1;
rw::Anim animTold{};
int pointedWas = -1;
double pointedSince = 0;
rw::PointedFx pointedFx;            // R8 match stage 2: the pointed effect's onset, fades and resume (spec 4a)
std::vector<int> fxSaid;            // the skills whose effect's chain stage was logged this session (spec 7.3 S7)
// The Animation rows as one Anim (L4); still when nothing moves (the Off preset: a compose only on a change).
rw::Anim ReadAnim() {
    rw::Anim a;
    a.floats = labstore::On(lab::RingWheelFloat);
    a.glow = std::clamp(int(std::lround(labstore::Value(lab::RingWheelGlow))), 0, 2);
    a.hoverFx = labstore::On(lab::RingWheelHoverFx);
    a.faceFx = labstore::On(lab::RingWheelFaceFx);
    a.burst = labstore::On(lab::RingWheelBurst);
    a.crawlS = int(std::lround(labstore::Value(lab::RingWheelCrawlS)));
    a.still = !a.floats && a.glow == rw::GlowOff && !a.hoverFx && !a.faceFx && a.crawlS == 0;
    return a;
}
// The Animation row (frame thread): a new preset writes rows 2-7 (L4 table); rows changed by hand show the preset they
// match, else Custom. Choosing Custom changes nothing.
void SyncPreset(void (*say)(const char*)) {
    const int row = std::clamp(int(std::lround(labstore::Value(lab::RingWheelAnim))), 0, 4);
    if (animSeen >= 0 && row != animSeen && row < int(rw::Preset::Custom)) {
        const rw::Anim p = rw::PresetAnim(rw::Preset(row));
        labstore::Set(lab::RingWheelFloat, p.floats ? 1.f : 0.f); labstore::Set(lab::RingWheelGlow, float(p.glow));
        labstore::Set(lab::RingWheelHoverFx, p.hoverFx ? 1.f : 0.f); labstore::Set(lab::RingWheelFaceFx, p.faceFx ? 1.f : 0.f);
        labstore::Set(lab::RingWheelBurst, p.burst ? 1.f : 0.f); labstore::Set(lab::RingWheelCrawlS, float(p.crawlS));
    } else {
        const int match = int(rw::PresetOf(ReadAnim()));
        if (match != row) labstore::Set(lab::RingWheelAnim, float(match));
    }
    animSeen = std::clamp(int(std::lround(labstore::Value(lab::RingWheelAnim))), 0, 4);
    const rw::Anim now = ReadAnim();
    const ULONGLONG t = GetTickCount64();
    if (say && !(now == animTold) && t - animSaidAt > 1000) {
        animTold = now; animSaidAt = t;
        static constexpr const char* names[5] = {"Off", "Floating icons", "Effect on hover", "All effects", "Custom"};
        static constexpr const char* glows[3] = {"Off", "Pointed only", "Everywhere"};
        char line[220];
        std::snprintf(line, sizeof line, "Frame ring wheel: animation %s (float %s, glow %s, hover %s, face %s, burst %s, crawl %d s)%s", names[size_t(animSeen)],
                      now.floats ? "on" : "off", glows[size_t(now.glow)], now.hoverFx ? "on" : "off", now.faceFx ? "on" : "off", now.burst ? "on" : "off", now.crawlS,
                      now.still ? ", still: redraws only on a change" : "");
        say(line);
    }
}
// R7: the ring's page (upstream's Spells, Glyphs or Motions page; the glyph chooser is the Lab's own on upstream's Glyphs),
// the pages' engine and picture, upstream's glyph and gesture state as its last Output had it, a glyph bind.
rw::RingPage ringPage = rw::RingPage::Main;
bool ringPageSeen = false;
rw::PageEngine pages;
rw::PageOut pageOut{};
rw::PageView view;
uint64_t viewKey = ~0ull;
uint32_t viewRevision = 0;
int choose = -1;                 // the shape whose skill the chooser picks (-1: none)
// 2026-11-16 (Jeff: "ring menu > quick cast / ring menu > gestures / ring menu > glyph"; "the edit mode is when the user is in the
// ring menu"): Quick cast's page (its 12 places) over upstream's Spells page while quickPageOn; quickChoose: the place its chooser
// fills (-1: the page). chooseQuick: the chooser's seats are Quick cast's (BuildChooser), not the glyphs'.
bool quickPageOn = false, chooseQuick = false;
int quickChoose = -1;
// Quick cast's Move (2026-11-17), from its page's center, lower half (review F8: no seat on the chooser): picking (which place),
// then quickMoveFrom (the place whose item waits for where it goes; -1 none). The page's next trigger on a place asks the radial
// (link::Edit from); Back or Move again stops it.
int quickMoveFrom = -1;
bool quickMovePick = false;
// Quick cast's places as the page names them (lab_quick_radial.h PlaceNumber; Fixed places: the clock's hours), from the radial's view.
bool quickClock = false;
int PlaceNo(int place) { return qr::PlaceNumber(place, quickClock); }
bool QuickPlaceFilled(int place) { const auto qv = framevr::quickradial::link::Read(); return place >= 0 && place < qv.n && place < int(qv.ids.size()) && qv.ids[size_t(place)] >= 0; }
// Review 2026-11-16 (exact items): the kind whose page is open over the chooser (rw::RingPage::QuickKind; 0: none) and its items
// (the kind, then every tier the game has, qr::TiersOf; the ones not carried dim), made when the kind is picked, never per frame.
int quickKind = 0, quickKindN = 0, quickKindCarried = 0;
std::array<int, rw::QuickPlaces> quickKindIds{};
std::array<bool, rw::QuickPlaces> quickKindDim{};   // 2026-11-17: a tier not carried (still can be put in a place)
// Quick cast's potions and scrolls (its chooser's row, the atlas's pictures): every family, best-first as the radial's Items list.
constexpr int QuickFamilies[] = {qr::FamHealth, qr::FamMana, qr::FamRejuv, qr::FamStamina, qr::FamAntidote, qr::FamThawing, qr::FamPortal, qr::FamIdentify};
std::vector<uint8_t> chooseDim;  // per chooser seat: 1 = not on the ring's glyph list
// The chooser's own seats (2026-11-14, the Commander orders' row): the ring's glyph list (rw::GlyphChoices) and the layout
// built from it (rw::ChooserLayout), with each seat's look. Made when a shape is chosen, never per frame.
std::vector<ia::Skill> chooseList;
rw::Layout chooseLayout;
uint64_t chooseKey = 0;   // the seatsKey they were built for (0: none)
std::vector<uint8_t> chooseElements; std::vector<int8_t> chooseCrawl; std::vector<rw::RestGlow> chooseRest;
std::vector<std::string> chooseInitials; std::vector<const char*> chooseInitialPtr;
std::vector<int16_t> chooseCells; const void* chooseCellsOf = nullptr;   // per chooser seat its icon cell, read once per atlas
float chooseHoldFrac = 0;   // an order's hold ring: from the first frame of the press (rw::OrderHoldFrac)
rw::ArmyWatch armyWatch;    // review M1: the army answer, watched beside the seats' key, never in it
bool chooseStale = false;   // the army answer changed: the chooser's seats are rebuilt before they are used again
std::string chooseTitle, clearLine, topCount, bottomCount;
uint64_t countKey = ~0ull;
int wheelHandNow = 0, glyphHandNow = 0, motionsOffNow = 0;
// ---- Other Labs' parts of the ring (headset fixes 2026-11-15), asked through Lab-to-Lab seams (vr/lab_hooks.h), never their
// keys (Dev structure rule 6). Both seams are key-gated on the other Lab's switch: with it Off, one branch and the defaults. ----
// FilterRingEntry: Lab Army X menu's entry on the General shelf (Jeff assigns skills here, hold to assign, and never saw it
// in upstream's Edit Slots). Its id joins the seats' key (a change rebuilds the layout); dim and slot are draw-only.
// T035 (2026-11-15): a second Lab's entry, Lab Quick cast radial's, beside the Army X menu's (FilterRingEntry): slot 0 Army X
// menu through its key-gated seam, slot 1 Quick cast radial read from its pure header while its switch is On (no row free).
struct Entry { int id = -1; const char* name = nullptr; const char* help = nullptr; bool dim = false; int slot = -1; };
constexpr int EntryCount = 2;
std::array<Entry, EntryCount> entries;
std::array<ia::Skill, EntryCount> entrySkills{};   // each entry as a catalog skill would be: its name and help for the blurb, the hold and the log
std::array<int, EntryCount> entrySeats{-1, -1};    // its seat in the main layout (-1: none)
std::vector<uint8_t> mainDim;    // per main seat: 1 drawn faint (an entry its Lab calls dim: Army X menu with no army out); empty: none
std::array<bool, EntryCount> entryDimSaid{}; std::array<int, EntryCount> entrySaid{-1, -1};
// What a dim entry means (its Lab's words): Army X menu's cross opens dim with no army out; Quick cast radial keeps upstream for this class.
constexpr const char* EntryDimLog[EntryCount] = {"dim (no army out: its cross opens dim, No army)", "dim (this class keeps upstream: its ring stays shut)"};
constexpr const char* EntryLitLog[EntryCount] = {"lit (an army is out)", "lit"};
constexpr const char* EntryDimBlurb[EntryCount] = {"No army out: it opens dim", "Not for this class"};
// FilterIconVariant: the icon remaps that are On (Lab Kick icon's bit). A change: every icon fetched again, a new atlas.
rw::IconVariant iconVariant;
int EntryOf(int id, int owner) {
    if (owner != -1) return -1;
    for (int k = 0; k < EntryCount; ++k) if (entries[size_t(k)].id >= 0 && id == entries[size_t(k)].id) return k;
    return -1;
}
bool IsEntry(int id, int owner) { return EntryOf(id, owner) >= 0; }
// ---- T036 (vault Tickets\036-ring-tree-cleanup): the layers, the center's thirds, each sub-menu on a button ----
struct WantMenu { int third = -1, hand = -1; double since = 0; };
WantMenu wantMenu;                // a sub-menu asked for (its button, a tap on its shelf entry) until upstream's page is there
constexpr double WantMenuMs = 1000;   // a wheel that never got there (busy hands, an editor, clicks ignored): given up this long after the ask or the last page change, or after FireGate::MaxFires clicks on one page (S-B)
rw::FireGate fireGate;            // review F2: one frame at 1, then 0; none on the frame the wheel opens or changes hand
rw::MenuButtons buttonsNow;       // each sub-menu's button: its key, kept only while the button holds no game skill (rw::Kept)
bool menuHeld[2][3] = {};         // the Hands seam's last trigger, primary and secondary per hand (press edges)
bool menuEntriesNow = false;      // Sub-menus on the shelf as the seats were built
std::vector<uint8_t> mainMark;    // per main seat: 1 + the sub-menu of a shelf entry, 0 none
int thirdSaid = -2;               // the last pointed third logged
rw::SeatLayers layersTold{}; bool layersSaid = false; ULONGLONG layersSaidAt = 0;
framevr::lab::Key MenuKey(int t) { return t == rw::ThirdGlyphs ? framevr::lab::RingWheelGlyphsButton : t == rw::ThirdGestures ? framevr::lab::RingWheelGesturesButton : framevr::lab::RingWheelQuickButton; }
rw::MenuButtons ReadButtons() {   // review F1: a trigger stored before the refusal is ignored (the attack stays the attack)
    rw::MenuButtons b;
    for (int t = 0; t < rw::ThirdCount; ++t) {
        if (t == rw::ThirdQuick) continue;   // 2026-11-16: Quick cast's button is Lab Quick cast radial's own (its key moved there once, Input)
        const int s = rw::ButtonSlotOf(labstore::Value(MenuKey(t)));
        b.slot[size_t(t)] = s == framevr::skills::TriggerSlot(0) || s == framevr::skills::TriggerSlot(1) ? -1 : s;
    }
    return b;
}
rw::SeatLayers ReadLayers() {
    rw::SeatLayers l;
    l.icon = labstore::On(framevr::lab::RingWheelIcon); l.ring = labstore::On(framevr::lab::RingWheelColorRing);
    l.frame = labstore::On(framevr::lab::RingWheelElementFrame); l.fx = labstore::On(framevr::lab::RingWheelFxLayer);
    return l;
}
// A hand slot (0-9) as the player names it: "Right A", "Left grip + Y", "Right trigger".
std::string SlotWords(int slot, int layout) {
    for (int h = 0; h < 2; ++h) for (int k = 0; k < rw::Slots; ++k)
        if (rw::SlotOf(h, k) == slot) return k == 0 ? std::string(h ? "Left trigger" : "Right trigger") : std::string(h ? "Left " : "Right ") + rw::SlotName(framevr::controllers::Layout(layout), h, k);
    return "slot " + std::to_string(slot);
}
// A sub-menu's shelf entry as a catalog skill would be: its name and help for the blurb, the hold and the log.
const ia::Skill* MenuSkill(int third) {
    static ia::Skill made[rw::ThirdCount];
    static constexpr const char* help[rw::ThirdCount] = {
        "Opens the Glyphs page: your hands' glyph shapes and the skill each casts.",
        "Opens the Gestures page: which gestures cast, each one On or Off.",
        "Opens Quick cast's page: 12 places; trigger one to choose a skill, potion or scroll for it."};
    if (third < 0 || third >= rw::ThirdCount) return nullptr;
    auto& k = made[size_t(third)];
    if (k.id < 0) {
        k.id = rw::MenuEntryId(third); k.owner = -1; k.level = 1; k.known = k.available = true; k.directed = true;   // never on the glyph chooser's list
        std::snprintf(k.name, sizeof k.name, "%s", rw::MenuEntryName[third]);
        std::snprintf(k.description, sizeof k.description, "%s", help[third]);
    }
    return &k;
}
void AskOtherLabs(const ia::Snapshot& s, void (*say)(const char*)) {
    std::array<Entry, EntryCount> got;
    LAB_HOOK(FilterRingEntry, lab::RingEntryFrame{&s, got[0].id, got[0].name, got[0].help, got[0].dim, got[0].slot});
    if (labstore::On(lab::QuickRadialOn)) {   // T035's entry: its own switch and pure header (written when the hook table was full at 128 rows; 192 since 2026-11-16)
        const auto q = framevr::quickradial::ShelfEntryFor(true, s.skillClass, labstore::Value(lab::QuickRadialButton), framevr::skills::Count);
        got[1] = Entry{q.id, q.name, q.help, q.dim, q.slot};
    }
    for (int k = 0; k < EntryCount; ++k) {
        const Entry& e = got[size_t(k)];
        Entry& entry = entries[size_t(k)];
        ia::Skill& entrySkill = entrySkills[size_t(k)];
        if (e.id >= 0 && (e.id != entry.id || e.name != entry.name || e.help != entry.help)) {
            entrySkill = {}; entrySkill.id = e.id; entrySkill.owner = -1; entrySkill.level = 1; entrySkill.known = entrySkill.available = true;
            entrySkill.directed = true;   // never on the glyph chooser's list (it needs no target, and is no skill)
            std::snprintf(entrySkill.name, sizeof entrySkill.name, "%s", e.name ? e.name : "?");
            std::snprintf(entrySkill.description, sizeof entrySkill.description, "%s", e.help ? e.help : "");
        }
        if (e.id != entrySaid[size_t(k)] && say) {   // its arrival and its going, once each (the switch's edges)
            char line[220];
            if (e.id >= 0) std::snprintf(line, sizeof line, "Frame ring wheel: entry %s on the General shelf (bind id %d, %s; hold a button on it to put it there)", e.name ? e.name : "?", e.id, e.dim ? EntryDimLog[k] : "lit");
            else std::snprintf(line, sizeof line, "Frame ring wheel: entry %s gone from the shelf (its Lab switched Off)", entrySkill.name[0] ? entrySkill.name : "?");
            say(line);
            entrySaid[size_t(k)] = e.id; entryDimSaid[size_t(k)] = e.dim;
        } else if (e.id >= 0 && e.dim != entryDimSaid[size_t(k)] && say) {   // the army (or the class) comes and goes: one line a change
            char line[160];
            std::snprintf(line, sizeof line, "Frame ring wheel: entry %s %s", entrySkill.name, e.dim ? EntryDimLog[k] : EntryLitLog[k]);
            say(line);
            entryDimSaid[size_t(k)] = e.dim;
        }
        entry = e;
        const int seat = entrySeats[size_t(k)];
        if (seat >= 0 && size_t(seat) < mainDim.size() && mainDim[size_t(seat)] != uint8_t(e.dim)) { mainDim[size_t(seat)] = e.dim; ++revision; }   // draw-only
    }
    uint32_t v = 0;
    LAB_HOOK(FilterIconVariant, lab::IconVariantFrame{v});
    if (iconVariant.Step(v)) {   // both edges: Kick's s1 is our art now, or the game's plate again
        request.iconVariant = v;
        if (gpu) {
            gpu->icons.Forget();
            if (seatsValid) { gpu->icons.Want(request.ids.data(), request.n, rw::NowMs()); gpu->worker.Want(request); }
        }
        if (say) {
            char line[200];
            std::snprintf(line, sizeof line, "Frame ring wheel: icon remaps changed (Kick icon %s): icons fetched again, atlas rebuilt%s", (v & 1u) ? "On" : "Off", seatsValid && gpu ? "" : " when the ring is next built");
            say(line);
        }
    }
}
// Feedback 1 items 3 and 5: the bound-button badges (steamvr/ring_wheel_badges.h), re-placed only on a bind, row or seat change.
rw::BadgeBoard badges;
int ctrlLayoutNow = 0;   // the controller layout Input last saw (the badges' letters)
rw::LetterLayoutHold letterHold; int letterHeldSaid = -1;   // job B: the last real layout per hand; the held layout last logged
std::array<int, 2> glyphDrawNow{-1, -1};   // #3: each hand's Draw glyph slot as upstream's last Output had it
rw::BadgeLook BadgeLookNow() {
    rw::BadgeLook k;
    const float a = labstore::Value(lab::RingWheelBadgeAngle), z = labstore::Value(lab::RingWheelBadgeSize);
    k.angleDeg = a < 0 ? rw::BadgeAuto : int(std::lround(a)) % 360;
    k.depth = rw::BadgeDepth(std::clamp(int(std::lround(labstore::Value(lab::RingWheelBadgeDepth))), 0, 3));
    k.sizePx = z < 15 ? 0 : int(std::lround(z));
    return k;
}
// The slots' skills from the snapshot the ring already reads; said on a new placement only.
const rw::BadgeSet* Badges(const ia::Snapshot& s, double nowMs, const rw::Layout& l, uint64_t key, void (*say)(const char*)) {
    rw::BoundSlots b;
    for (int h = 0; h < 2; ++h) for (int k = 0; k < rw::Slots; ++k)
        if (const int slot = rw::SlotOf(h, k); slot >= 0) {
            b.id[size_t(h)][size_t(k)] = s.skills[size_t(slot)].id; b.owner[size_t(h)][size_t(k)] = s.skills[size_t(slot)].owner;
            for (const Entry& entry : entries)   // an entry's button (its slot's game skill cleared)
                if (slot == entry.slot && entry.id >= 0 && b.id[size_t(h)][size_t(k)] < 0) { b.id[size_t(h)][size_t(k)] = entry.id; b.owner[size_t(h)][size_t(k)] = -1; }
            if (const int t = buttonsNow.MenuOf(slot); t >= 0 && menuEntriesNow && b.id[size_t(h)][size_t(k)] < 0) { b.id[size_t(h)][size_t(k)] = rw::MenuEntryId(t); b.owner[size_t(h)][size_t(k)] = -1; }   // T036: a sub-menu's button on its shelf entry
        }
    const rw::BadgeLook look = BadgeLookNow();
    if (badges.Update(l, key, b, framevr::controllers::Layout(ctrlLayoutNow), look, nowMs) && say) {
        const auto& set = badges.Set();
        char angle[16]; if (look.angleDeg < 0) std::snprintf(angle, sizeof angle, "Auto"); else std::snprintf(angle, sizeof angle, "%d deg", look.angleDeg);
        char labels[96] = ""; size_t at = 0;   // job B: the letters shown, hand+slot=letter (R0=A is the right hand's slot 0 ...), and the layout they came from
        for (int i = 0; i < set.count && at + 8 < sizeof labels; ++i) at += size_t(std::snprintf(labels + at, sizeof labels - at, "%s%c%d=%c", i ? " " : "", set.b[size_t(i)].hand ? 'L' : 'R', set.b[size_t(i)].slot, set.b[size_t(i)].letter));
        char line[320];
        if (look.sizePx <= 0) std::snprintf(line, sizeof line, "Frame ring wheel: badges off (size Off)");
        else std::snprintf(line, sizeof line, "Frame ring wheel: badges %d placed (angle %s, depth %s, size %d px), %d moved by the search, min clearance %.1f px; letters [%s] from the %s layout",
                           set.count, angle, rw::DepthName(look.depth), look.sizePx, set.moved, set.count ? double(set.worst) : 0.0, labels, framevr::controllers::Name(framevr::controllers::Layout(ctrlLayoutNow)));
        say(line);
    }
    return &badges.Set();
}
ia::GlyphSets glyphSets = ia::NoGlyphs;
struct GlyphBind { bool queued = false; int hand = 0, shape = -1, skill = -1, owner = -1; };
GlyphBind glyphSent;
// Perf's X4 check: the binds and glyphs made but not sent yet, in order (one slot let a third quick assign drop the second).
struct Pending { bool glyph = false; Bind bind{}; GlyphBind g{}; };
rw::SendQueue<Pending> sendQueue;
// Upstream's page as a ring page: Spells, Glyphs (or the chooser), Motions for a class with motions; else false (upstream's).
bool RingPageOf(int page, int skillClass, rw::RingPage& out) {
    if (page == int(ia::MenuPage::Spells)) { out = rw::RingPage::Main; return true; }
    if (page == int(ia::MenuPage::Glyphs)) { out = choose >= 0 ? rw::RingPage::GlyphChoose : rw::RingPage::Glyphs; return true; }
    if (page == int(ia::MenuPage::Motions) && framevr::spellmotion::HasMotions(skillClass)) { out = rw::RingPage::Gestures; return true; }
    return false;
}
bool RingShown(int page) { return page == int(ia::MenuPage::Spells) || page == int(ia::MenuPage::Glyphs) || page == int(ia::MenuPage::Motions); }
const char* PageName(rw::RingPage p) {
    return p == rw::RingPage::Main ? "main" : p == rw::RingPage::Glyphs ? "Glyphs" : p == rw::RingPage::GlyphChoose ? "glyph chooser" :
           p == rw::RingPage::Quick ? "Quick cast" : p == rw::RingPage::QuickChoose ? "Quick cast chooser" : p == rw::RingPage::QuickKind ? "Quick cast kind" : "Gestures";
}

void SayOnce(lab::WheelLayerFrame& f, unsigned bit, const char* why) {
    if (saidUpstream & bit) return;
    saidUpstream |= bit;
    if (f.say) f.say((std::string("Frame ring wheel: upstream: ") + why).c_str());
}
rw::Placement Read() {
    rw::Placement p;
    p.distanceM = labstore::Value(lab::RingWheelDistance) / 100;
    p.widthM = labstore::Value(lab::RingWheelWidth) / 100;
    p.belowDeg = labstore::Value(lab::RingWheelBelow);
    p.worldLock = labstore::On(lab::RingWheelWorldLock);
    return p;
}
ia::Pose PoseOf(const float* f) { return {{f[0], f[1], f[2]}, {f[3], f[4], f[5], f[6]}}; }
void Write(void* xr, const ia::Pose& p) {
    auto& x = *static_cast<XrPosef*>(xr);
    x.orientation = {p.q.x, p.q.y, p.q.z, p.q.w}; x.position = {p.p.x, p.p.y, p.p.z};
}
int RenderPct() { return rw::ScalePct(int(std::lround(labstore::Value(lab::RingWheelRenderPct)))); }
// The content rectangle (canvas px and texels) and its quad in meters, centered where the content is.
struct Quad { rw::ContentRect c; float w = 0, h = 0; ia::Pose pose{}; };
Quad QuadOf(const ia::Pose& wheel, float widthM, bool developer) {
    Quad q;
    q.c = rw::Content(rw::FooterWidth(labstore::On(lab::RingWheelFooterFull), developer ? 6 : 5), RenderPct());
    const auto m = rw::QuadFor(q.c.x0, q.c.y0, q.c.canvasW, float(rw::ContentRows), widthM);
    q.w = m.w; q.h = m.h;
    q.pose = {wheel.p + math::Rotate(wheel.q, {m.dx, m.dy, 0}), wheel.q};
    return q;
}
bool Ready() {
    return gpu && gpu->compose.Ready() && built && built->request == request && seatsValid && gpu->chain != XR_NULL_HANDLE;
}
// The Tree layout row (plan E7 #10, L4 row 10): Trees shown (0, the default) or Tree orbs (1).
rw::TreeLayout LayoutNow() { return labstore::On(lab::RingWheelTreeLayout) ? rw::TreeLayout::Orbs : rw::TreeLayout::Shown; }
int layoutSaid = -1;   // the Tree layout last logged
// T025 Tree icon (Layers page): the rows' reading, one branch each. Off (the master, or Drawn): today's frame, no picture asked for.
rw::treeart::Mode TreeMode() { return rw::treeart::ModeOf(labstore::Value(lab::RingWheelTreeIcon)); }
int TreeSizePct() { return rw::treeart::SizePct(labstore::Value(lab::RingWheelTreeIconSize)); }
bool TreePictures() { return TreeMode() != rw::treeart::Mode::Drawn && TreeSizePct() > 0; }   // the atlas holds the class's painted squares only then
bool treeSaid = false; int treeSaidKey = -1;   // "Frame ring wheel: tree icons" once a page open, and on a change
// Glyph icon (Center page): the Glyphs page's shapes and the center split's sigils as painted squares. Off (Drawn): no picture asked for.
rw::glyphart::Mode GlyphMode() { return rw::glyphart::ModeOf(labstore::Value(lab::RingWheelGlyphIcon)); }
int GlyphSizePct() { return rw::glyphart::SizePct(labstore::Value(lab::RingWheelGlyphIconSize)); }
bool glyphSaid = false; int glyphSaidKey = -1;   // "Frame ring wheel: glyph icons" once a page open, and on a change
int pageIconsSay = -1;  // item 6: the page cards just added to the atlas request, to log (-1 nothing to say)
// The hero's skills changed (class, learned skills, item skills) or the Tree layout row: a new layout, and the atlas worker asked once.
void Seats(const ia::Snapshot& s) {
    uint64_t h = 1469598103934665603ull;
    auto mix = [&](int64_t v) { h ^= uint64_t(v); h *= 1099511628211ull; };
    mix(s.skillClass); mix(s.skillCount); mix(RenderPct()); mix(int(LayoutNow())); mix(TreePictures()); mix(int(GlyphMode()));   // T025: the class's painted squares join the atlas request
    for (unsigned i = 0; i < s.skillCount && i < s.catalog.size(); ++i) { mix(s.catalog[i].id); mix(s.catalog[i].owner); }
    for (const Entry& entry : entries) mix(entry.id);   // headset fixes 2026-11-15: the other Labs' shelf entries come and go with their switches (not with the army)
    const bool menus = labstore::On(lab::RingWheelMenuEntries);   // T036: the sub-menus' entries come and go with their row
    mix(menus);
    // Review M1: the summon army is watched beside this key, never mixed into it (a key change resets the main ring).
    const auto army = armyWatch.Step(s, h);
    if (army.changed) chooseStale = true;   // the chooser's row appears or goes; the main ring is not touched
    if (h == seatsKey) {
        if (army.iconsNew && seatsValid) {   // this hero's first army: the orders' icons join the request, once, and stay
            request.n = rw::AddOrderIcons(request.ids.data(), request.n, int(request.ids.size()));
            if (gpu) { gpu->icons.Want(request.ids.data(), request.n, rw::NowMs()); gpu->worker.Want(request); }
        }
        return;
    }
    seatsKey = h;
    std::array<rw::SeatIn, rw::MaxSeats> in{};
    int n = 0;
    seatsValid = rw::SeatsFrom(s, in, n) && n > 0;
    if (seatsValid) for (const Entry& entry : entries) n = rw::AddShelfEntry(in, n, entry.id);   // headset fixes 2026-11-15: the other Labs' entries, General shelf
    if (seatsValid && menus) for (int t = 0; t < rw::ThirdCount; ++t) if (t != rw::ThirdQuick) n = rw::AddShelfEntry(in, n, rw::MenuEntryId(t));   // T036: the sub-menus, General shelf (Quick cast: the radial's own entry, 2026-11-16)
    menuEntriesNow = menus;
    seatClass = s.skillClass;
    engineOpen = false; blurbs.Reset(); blurbKey = ~0ull; blurbSeat = -1; ++revision;
    entrySeats.fill(-1); mainDim.clear(); mainMark.clear();
    if (!seatsValid) return;
    layout = rw::Build(in.data(), n, LayoutNow());
    request = {}; request.classId = s.skillClass; request.scalePct = RenderPct(); request.n = layout.count;
    request.iconVariant = iconVariant.seen;   // the icon remaps On now (Kick icon): its own atlas
    initials.assign(size_t(layout.count), std::string());
    for (int i = 0; i < layout.count; ++i) {
        const auto& seat = layout.seats[size_t(i)];
        request.ids[size_t(i)] = seat.id;
        if (const int t = seat.owner == -1 ? rw::MenuOfEntry(seat.id) : -1; t >= 0) {   // T036: a sub-menu's entry draws its picture (its id is its art's, 2026-11-16); its mark if none
            if (mainMark.empty()) mainMark.assign(size_t(layout.count), 0);
            mainMark[size_t(i)] = uint8_t(1 + t);
            continue;
        }
        if (const int k = EntryOf(seat.id, seat.owner); k >= 0) {   // another Lab's entry: its letters from its own name, faint while it says so
            entrySeats[size_t(k)] = i;
            if (k == 1) {   // 2026-11-16: Lab Quick cast radial's entry shows the Quick cast picture (its third's), its mark if none
                if (mainMark.empty()) mainMark.assign(size_t(layout.count), 0);
                mainMark[size_t(i)] = uint8_t(1 + rw::ThirdQuick); request.ids[size_t(i)] = rw::menuart::ArtId(rw::ThirdQuick);
            }
            if (mainDim.size() != size_t(layout.count)) mainDim.assign(size_t(layout.count), 0);
            mainDim[size_t(i)] = entries[size_t(k)].dim;
            for (const auto& w : tx::Words(tx::Clean(entrySkills[size_t(k)].name)))
                if (!w.empty() && initials[size_t(i)].size() < 2 && std::isalpha(uint8_t(w[0]))) initials[size_t(i)] += w[0];
            continue;
        }
        for (unsigned k = 0; k < s.skillCount && k < s.catalog.size(); ++k)
            if (s.catalog[k].id == seat.id && s.catalog[k].owner == seat.owner) {
                for (const auto& w : tx::Words(tx::Clean(s.catalog[k].name)))
                    if (!w.empty() && initials[size_t(i)].size() < 2 && std::isalpha(uint8_t(w[0]))) initials[size_t(i)] += w[0];
                break;
            }
    }
    for (int t = 0; t < rw::ThirdCount; ++t) {   // T036 (2026-11-16): the center thirds' pictures, right after the seats (the main page shows them: first in a full atlas)
        const int id = rw::menuart::ArtId(t);
        if (request.n < int(request.ids.size()) && std::find(request.ids.data(), request.ids.data() + request.n, id) == request.ids.data() + request.n) request.ids[size_t(request.n++)] = id;
    }
    request.n = rw::treeart::AddIcons(TreeMode(), TreeSizePct(), s.skillClass, request.ids.data(), request.n, int(request.ids.size()));   // T025: the class's painted tree squares (up to three; none while Drawn or hidden)
    request.n = rw::glyphart::AddIcons(GlyphMode(), request.ids.data(), request.n, int(request.ids.size()));   // Glyph icon: the painted shapes and sigils (up to 12; none while Drawn)
    request.n = rw::AddGesturePageIcons(s.skillClass, request.ids.data(), request.n, int(request.ids.size()));   // item 6: only here, when the seats changed
    if (army.icons) request.n = rw::AddOrderIcons(request.ids.data(), request.n, int(request.ids.size()));   // the chooser's order row has its icons ready
    // 2026-11-16: Quick cast's potions and scrolls, each picture once (a kind shows one: qr::PictureId). Review N2: asked for every
    // class and with Quick cast radial Off too (19 cells, once a layout, nothing per frame): the seats' key carries no Lab switch, so
    // a switch turned On later finds them ready.
    for (const uint32_t code : qr::ExactCodes) {
        const int id = qr::ExactId(code);
        if (request.n < int(request.ids.size()) && std::find(request.ids.data(), request.ids.data() + request.n, id) == request.ids.data() + request.n) request.ids[size_t(request.n++)] = id;
    }
    pageIconsSay = request.n - layout.count;
    initialPtr.clear();
    for (const auto& x : initials) initialPtr.push_back(x.c_str());
    elements.assign(size_t(layout.count), uint8_t(rw::Element::Phys)); crawlIdx.assign(size_t(layout.count), int8_t(-1)); crawlCount = 0;
    restGlows.assign(size_t(layout.count), rw::RestGlow{});
    pointedFx.Reset();   // its seat index belongs to the old layout
    for (int i = 0; i < layout.count; ++i) {   // R8 match stage 1: the glow per skill (kit table; its tree's element when the kit lacks it)
        const auto& seat = layout.seats[size_t(i)];
        restGlows[size_t(i)] = rw::RestGlowOf(seat.id, s.skillClass, seat.tree);
        elements[size_t(i)] = uint8_t(rw::ElementOfGlow(restGlows[size_t(i)]));
        if (rw::CrawlsAtRest(restGlows[size_t(i)]) && crawlCount < 127) crawlIdx[size_t(i)] = int8_t(crawlCount++);
    }
    restFromKit = 0; restByTree = 0;
    for (int i = 0; i < layout.count; ++i) ++(rw::SkillFxOf(layout.seats[size_t(i)].id) ? restFromKit : restByTree);
    restSaid = false;
    if (gpu) { gpu->icons.Want(request.ids.data(), request.n, rw::NowMs()); gpu->worker.Want(request); }
}
const ia::Skill* SkillOf(const ia::Snapshot& s, int id, int owner) {
    for (unsigned k = 0; k < s.skillCount && k < s.catalog.size(); ++k) if (s.catalog[k].id == id && s.catalog[k].owner == owner) return &s.catalog[k];
    if (const int k = EntryOf(id, owner); k >= 0) return &entrySkills[size_t(k)];   // headset fixes 2026-11-15: another Lab's shelf entry (Army X menu, Quick cast radial)
    if (owner == -1 && rw::MenuOfEntry(id) >= 0) return MenuSkill(rw::MenuOfEntry(id));   // T036: a sub-menu's shelf entry
    if (owner == -1 && rw::orders::Is(id)) {   // a Commander order is no catalog skill: upstream's own entry for it (its name in the blurb, the hold and the log)
        static ia::Skill made[rw::orders::Count];
        auto& order = made[size_t(id - rw::orders::First)];
        order = ia::MakeCommanderSkill(id, true);
        return &order;
    }
    return nullptr;
}
// The chooser's seats and their look, from the ring's one glyph list (when a shape is chosen, or the seats changed under it).
void BuildChooser(const ia::Snapshot& s, bool forQuick = false) {
    chooseQuick = forQuick;
    if (forQuick) {   // 2026-11-16: the skills the radial casts (rw::QuickChoices), its potions and scrolls on the shelf's row
        chooseList = rw::QuickChoices(s);
        std::vector<int> row;
        for (const int fam : QuickFamilies) row.push_back(qr::FamilyId(fam));
        chooseLayout = rw::ChooserRow(layout, chooseList, row);
    } else {
        chooseList = rw::GlyphChoices(s);
        chooseLayout = rw::ChooserLayout(layout, chooseList);
    }
    const size_t n = size_t(chooseLayout.count);
    chooseDim.assign(n, 1); chooseElements.assign(n, uint8_t(rw::Element::Phys)); chooseCrawl.assign(n, int8_t(-1));
    chooseRest.assign(n, rw::RestGlow{}); chooseInitials.assign(n, std::string());
    const bool mainLook = elements.size() == size_t(layout.count) && restGlows.size() == size_t(layout.count) && crawlIdx.size() == size_t(layout.count) && initials.size() == size_t(layout.count);
    for (size_t i = 0; i < n; ++i) {
        const auto& seat = chooseLayout.seats[i];
        if (rw::ChoiceOf(chooseList, seat.id, seat.owner) || (forQuick && rw::ItemSeat(seat.id))) chooseDim[i] = 0;
        int m = -1;
        for (int j = 0; j < layout.count && m < 0; ++j) if (layout.seats[size_t(j)].id == seat.id && layout.seats[size_t(j)].owner == seat.owner) m = j;
        if (m >= 0 && mainLook) { chooseElements[i] = elements[size_t(m)]; chooseCrawl[i] = crawlIdx[size_t(m)]; chooseRest[i] = restGlows[size_t(m)]; chooseInitials[i] = initials[size_t(m)]; }
        else {   // an order: the shelf's look, its letters from the one header
            chooseRest[i] = rw::RestGlowOf(seat.id, s.skillClass, seat.tree);
            chooseElements[i] = uint8_t(rw::ElementOfGlow(chooseRest[i]));
            if (rw::OrderSeat(chooseLayout, int(i))) chooseInitials[i] = rw::orders::Initials(seat.id);
            if (rw::ItemSeat(seat.id)) chooseInitials[i] = qr::FamilyInitials(seat.id & 0xffff);   // Quick cast's potions and scrolls
        }
    }
    chooseInitialPtr.clear();
    for (const auto& x : chooseInitials) chooseInitialPtr.push_back(x.c_str());
    chooseCellsOf = nullptr; chooseKey = seatsKey; chooseStale = false; chooseHoldFrac = 0; ++revision;
}
// Reviews S3 / S4 (2026-11-16): the radial's ring as the page may show and edit it: its copy (link::View) for this character
// (session, class), and whether its save is read.
rw::QuickRingState QuickState(const ia::Snapshot& s, framevr::quickradial::link::View& qv) {
    namespace lk = framevr::quickradial::link;
    qv = lk::Read();
    quickClock = qv.fixed;   // 2026-11-17: the places' names follow Quick cast layout
    if (!qv.ready || qv.session != s.session || qv.cls != s.skillClass || qv.save == lk::Save::Reading) return rw::QuickRingState::Reading;
    return qv.save == lk::Save::Saved ? rw::QuickRingState::Saved : rw::QuickRingState::ThisGame;
}
// A kind's name on the ring menu: the plain kind for one with tiers (its page offers Best or a tier; the Potion order's "(best)" /
// "(weakest)" is the radial's), the potion's own name for the others.
std::string QuickKindName(int family) { return qr::HasTiers(family) ? std::string(qr::FamilyName(family)) : qr::FamilyEntryName(family, true); }
// Potion choice (2026-11-17): a kind with tiers (Healing, Mana, Rejuvenation: qr::HasTiers) picked on Quick cast's chooser always
// opens its page, carried or not: the kind first (Best), then every tier the game has, strongest first (qr::TiersOf, the game's
// own potion codes); the ones not carried draw dim and can still be put in a place. Returns the tiers listed (0: no page, the
// caller puts the kind: Antidote, Thawing, Stamina have one potion each). quickKindCarried: how many of them the hero carries.
int KindPage(const ia::Snapshot& s, int family) {
    if (!qr::HasTiers(family)) return 0;
    static qr::InvItem inv[128];   // the frame thread's only (as the radial's own list)
    const int invN = qr::InvOf(s, inv, 128);
    int have[16];
    const int carried = qr::CarriedOf(family, qr::BeltOf(s), inv, invN, have, 16);
    const int tiers = qr::TiersOf(family, quickKindIds.data() + 1, int(quickKindIds.size()) - 1);
    quickKindIds[0] = qr::FamilyId(family); quickKindDim[0] = false; quickKindCarried = 0;
    for (int k = 0; k < tiers; ++k) {
        const int id = quickKindIds[size_t(k) + 1];
        const bool carries = std::find(have, have + carried, id) != have + carried;
        quickKindDim[size_t(k) + 1] = !carries; quickKindCarried += carries;
    }
    quickKindN = 1 + tiers; quickKind = family;
    return tiers;
}
// T036: each sub-menu's button, from its key, kept only while the button holds no game skill and draws no glyph (a skill put
// there later, or Draw glyph, shuts it while it is there: said, the key kept for when the button is free again, review S5). Judged on a fresh snapshot only, and not in
// the 2 s after a sub-menu's own clear was applied (the snapshot catches up with it).
ULONGLONG keptHoldUntil = 0;
void KeepButtons(const ia::Snapshot& s, void (*say)(const char*)) {
    const rw::MenuButtons stored = ReadButtons();
    buttonsNow = stored;
    const ULONGLONG t = GetTickCount64();
    if (!stored.Any() || t < keptHoldUntil || !s.at || t < s.at || t - s.at > 650) return;
    std::array<int, framevr::skills::Count> game{};
    for (int i = 0; i < framevr::skills::Count; ++i) game[size_t(i)] = s.skills[size_t(i)].id;
    const rw::MenuButtons kept = rw::Kept(stored, game, glyphDrawNow);
    static rw::MenuButtons told;   // review S5: the stored key stays (another character may have a skill there); said on each change
    for (int k = 0; k < rw::ThirdCount; ++k) {
        const bool shut = kept.slot[size_t(k)] != stored.slot[size_t(k)];
        const bool wasShut = told.slot[size_t(k)] == stored.slot[size_t(k)] && stored.slot[size_t(k)] >= 0;
        if (shut == wasShut) continue;
        told.slot[size_t(k)] = shut ? stored.slot[size_t(k)] : -1;
        if (say) {
            char line[220];
            std::snprintf(line, sizeof line, shut ? "Frame ring wheel: sub-menu %s stays shut on %s (a skill or Draw glyph is on that button now; kept for when it is free)" : "Frame ring wheel: sub-menu %s on %s again (the button is free)", rw::MenuEntryName[k], SlotWords(stored.slot[size_t(k)], ctrlLayoutNow).c_str());
            say(line);
        }
    }
    buttonsNow = kept;
}
// A bind or glyph to send (Request sends them in order, one at a time). With SendDepth already waiting, the new one is
// refused out loud: the log line, Refused on the bind and three short pulses, as an answered refusal.
void Enqueue(const Pending& p, const ia::Snapshot& s, void (*say)(const char*)) {
    if (sendQueue.Push(p)) return;
    const int hand = p.glyph ? p.g.hand : p.bind.hand;
    if (!p.glyph) engine.Refused();
    QueuePulses(hand, 3, .35f);
    if (!say) return;
    char line[240];
    const int skill = p.glyph ? p.g.skill : p.bind.skill, owner = p.glyph ? p.g.owner : p.bind.owner;
    const auto* k = skill >= 0 ? SkillOf(s, skill, owner) : nullptr;
    const char* name = skill < 0 ? "(clear)" : k ? nullptr : "?";
    const std::string clean = name ? std::string(name) : tx::Clean(k->name);
    if (p.glyph) std::snprintf(line, sizeof line, "Frame ring wheel: glyph %s, the %s hand's -> %s (refused: %d already waiting to be sent)",
                               framevr::glyphs::Name(framevr::glyphs::Shape(p.g.shape)), hand ? "left" : "right", clean.c_str(), rw::SendDepth);
    else std::snprintf(line, sizeof line, "Frame ring wheel: bind %s -> %s (refused: %d already waiting to be sent)", clean.c_str(),
                       rw::SlotName(framevr::controllers::Layout(p.bind.layout), p.bind.hand, p.bind.slot).c_str(), rw::SendDepth);
    say(line);
}
// A hand slot (0-9) as the hand and its button (0 trigger .. 4 grip + secondary); false: none.
bool HandButtonOf(int slot, int& hand, int& button) {
    for (int h = 0; h < 2; ++h) for (int k = 0; k < rw::Slots; ++k) if (rw::SlotOf(h, k) == slot) { hand = h; button = k; return true; }
    return false;
}
std::string SkillWords(const ia::Snapshot& s, int id, int owner) {
    if (rw::orders::Is(id)) return rw::orders::Label(id);
    const auto* k = SkillOf(s, id, owner);
    return k ? tx::Clean(k->name) : "skill " + std::to_string(id);
}
// 2026-11-16 bug 4: a Lab menu's bind remembers what its button held and where the menu was (its Applied moves that skill).
void Displaced(Bind& b, const ia::Snapshot& s, int oldSlot, const char* name) {
    if (b.index >= 0 && b.index < framevr::skills::Count) { b.was = s.skills[size_t(b.index)].id; b.wasOwner = s.skills[size_t(b.index)].owner; }
    b.oldSlot = oldSlot == b.index ? -1 : oldSlot; b.labName = name;
}
// The menu's clear was applied: the skill it took off the button goes to the menu's old button (free now: a swap), else into
// Quick cast's ring (the radial adds it, link::moveAsk), else it is said taken off, with three short pulses. Never silent.
void MoveDisplaced(const Bind& b, const ia::Snapshot& s, void (*say)(const char*)) {
    if (b.was < 0 || !b.labName) return;
    const bool oldGlyph = b.oldSlot >= 0 && (glyphDrawNow[0] == b.oldSlot || glyphDrawNow[1] == b.oldSlot);
    const int oldSkill = b.oldSlot >= 0 && b.oldSlot < framevr::skills::Count ? s.skills[size_t(b.oldSlot)].id : -1;
    const auto* k = SkillOf(s, b.was, b.wasOwner);
    const bool castable = k && k->known && !rw::orders::Is(b.was) && framevr::quickradial::IsSkillId(b.was) && labstore::On(lab::QuickRadialOn) &&
                          framevr::quickradial::ClassKnown(s.skillClass);
    const std::string name = SkillWords(s, b.was, b.wasOwner), from = SlotWords(b.index, b.layout);
    char line[260] = "";
    switch (rw::WhereMoved(b.was, b.index, b.oldSlot, oldSkill, oldGlyph, castable)) {
    case rw::MovedTo::OldButton: {   // review S1: asked; the swap's own bind line says Applied or Refused (with three pulses)
        int h = 0, button = 0;
        if (!HandButtonOf(b.oldSlot, h, button)) break;   // review N3: never a bind to hand 0, button 0 (the attack)
        Pending p; p.bind = {true, b.oldSlot, b.was, b.wasOwner, button, h, b.layout}; Enqueue(p, s, say);
        std::snprintf(line, sizeof line, "Frame ring wheel: %s asked to move from %s to %s (%s took %s; its old button was free)", name.c_str(), from.c_str(),
                      SlotWords(b.oldSlot, b.layout).c_str(), b.labName, from.c_str());
        break;
    }
    case rw::MovedTo::QuickCast: {   // review S1: asked; the radial says added, on the ring already, or taken off (three pulses)
        namespace lk = framevr::quickradial::link;
        std::snprintf(lk::moveFrom, sizeof lk::moveFrom, "%s", from.c_str());
        lk::moveAsk.store(lk::MoveCode(b.was, b.index), std::memory_order_release);
        std::snprintf(line, sizeof line, "Frame ring wheel: %s left %s (%s took it): asked Quick cast's ring to take it", name.c_str(), from.c_str(), b.labName);
        break;
    }
    case rw::MovedTo::Dropped:
        QueuePulses(b.hand, 3, .35f);
        std::snprintf(line, sizeof line, "Frame ring wheel: %s taken off %s by %s (no free button for it, and Quick cast is Off or can't take it): hold a button on it to put it back",
                      name.c_str(), from.c_str(), b.labName);
        break;
    default: break;
    }
    if (say && line[0]) say(line);
}
// T036: a hold to assign on a sub-menu's third or shelf entry: upstream's own clear of that button's game skill (as Draw glyph's
// pick, and Army X menu's), and on its Applied the sub-menu keeps the button (Request). A button that draws glyphs is refused out
// loud: the clear would leave Draw glyph on it. Quick cast's third (2026-11-16): Lab Quick cast radial's own bind (QuickBind).
void QuickBind(const rw::QuickOut& e, int layout, const ia::Snapshot& s, void (*say)(const char*));
void MenuBind(int third, const rw::QuickOut& e, int layout, const ia::Snapshot& s, void (*say)(const char*)) {
    if (third == rw::ThirdQuick) { QuickBind(e, layout, s, say); return; }
    const int was = s.skills[size_t(std::max(0, rw::SlotOf(e.hand, e.slot)))].id;
    char line[240];
    if (!rw::MenuButtonAllowed(e.slot)) {   // review F1: never a trigger (the attack, and the ring's own click)
        QueuePulses(e.hand, 3, .35f);
        if (say) { std::snprintf(line, sizeof line, "Frame ring wheel: sub-menu %s not put on %s: a trigger stays the attack and the ring's click (use a face button)", rw::MenuEntryName[third], rw::SlotName(framevr::controllers::Layout(layout), e.hand, e.slot).c_str()); say(line); }
        return;
    }
    if (rw::HeldBy(was, glyphDrawNow, e.hand, e.slot) == rw::Held::DrawGlyph) {
        QueuePulses(e.hand, 3, .35f);
        if (say) { std::snprintf(line, sizeof line, "Frame ring wheel: sub-menu %s not put on %s: that button draws glyphs (put a skill on it first)", rw::MenuEntryName[third], rw::SlotName(framevr::controllers::Layout(layout), e.hand, e.slot).c_str()); say(line); }
        return;
    }
    {
        Pending p; p.bind = {true, rw::SlotOf(e.hand, e.slot), -1, -1, e.slot, e.hand, layout, false, third};
        Displaced(p.bind, s, ReadButtons().slot[size_t(third)], rw::MenuEntryName[third]);   // bug 4: its Applied moves the skill there
        Enqueue(p, s, say);
    }
    if (say) {
        const auto* k = was >= 0 ? SkillOf(s, was, s.skills[size_t(std::max(0, rw::SlotOf(e.hand, e.slot)))].owner) : nullptr;
        std::snprintf(line, sizeof line, "Frame ring wheel: sub-menu assign %s -> %s (was %s) held=%.0f ms", rw::MenuEntryName[third],
                      rw::SlotName(framevr::controllers::Layout(layout), e.hand, e.slot).c_str(), k ? tx::Clean(k->name).c_str() : was >= 0 ? "?" : "none", e.heldMs);
        say(line);
    }
    QueuePulses(e.hand, 1, .6f);
}
// Quick cast on a button (a hold on its third): the radial's own bind, the same request as a hold on its shelf entry (BindSkill with
// its id; its FilterWheelRequest row makes it upstream's clear and keeps the button on Applied). Refused out loud here and never
// sent (rw::QuickBindRefusal): a trigger first (review F1 of 2026-11-16: as every sub-menu, the attack and the ring's click), its
// switch Off (nothing would take the request), a class that keeps upstream, a Draw glyph button.
void QuickBind(const rw::QuickOut& e, int layout, const ia::Snapshot& s, void (*say)(const char*)) {
    const int index = rw::SlotOf(e.hand, e.slot);
    const int was = index >= 0 ? s.skills[size_t(index)].id : -1;
    char line[260];
    const char* no = rw::QuickBindRefusal(e.slot, labstore::On(lab::QuickRadialOn), framevr::quickradial::ClassKnown(s.skillClass),
                                          rw::HeldBy(was, glyphDrawNow, e.hand, e.slot) == rw::Held::DrawGlyph);
    if (no || index < 0) {
        QueuePulses(e.hand, 3, .35f);
        if (say) { std::snprintf(line, sizeof line, "Frame ring wheel: Quick cast not put on %s: %s", SlotWords(index, layout).c_str(), no ? no : "no such button"); say(line); }
        return;
    }
    {
        Pending p; p.bind = {true, index, framevr::quickradial::ItemId, -1, e.slot, e.hand, layout};
        Displaced(p.bind, s, labbuttons::SlotOfLab(lab::QuickRadialButton), "Quick cast");   // bug 4
        Enqueue(p, s, say);
    }
    if (say) {
        std::snprintf(line, sizeof line, "Frame ring wheel: assign Quick cast -> %s (was %s) held=%.0f ms, from its third", SlotWords(index, layout).c_str(),
                      was >= 0 ? SkillWords(s, was, s.skills[size_t(index)].owner).c_str() : "none", e.heldMs);
        say(line);
    }
    QueuePulses(e.hand, 1, .6f);
}
// The Off edge (and a reset), the frame thread: the swapchain destroyed here once the last compose is done on the GPU (at
// most 16 ms, normally 0; perf's X1 check: the drain is the hook's last call, so it must not wait for the next On), then the
// GPU side goes to its worker, which frees it after the GPU is done. Review-r6 X1: the worker never touches OpenXR.
bool DestroyChain(Gpu* g) {   // true: a swapchain was destroyed (its callers say so once, audit 2026-11-10 bug 8)
    const bool had = g->chain != XR_NULL_HANDLE && g->destroy;
    if (had) g->destroy(g->chain);
    g->chain = XR_NULL_HANDLE;
    if (g->doneFence) { g->doneFence->Release(); g->doneFence = nullptr; }
    return had;
}
void Retire(void (*say)(const char*), const char* where) {
    if (!gpu) return;
    Gpu* g = gpu;
    gpu = nullptr;
    g->icons.Close();   // a worker waiting for icons goes on at once
    const rw::OffResult r = retiring.Off(g, 16,
        [](Gpu* x, unsigned ms) {
            if (x->compose.WaitIdle(ms)) return true;
            x->doneFence = x->compose.MarkLast(x->doneValue);   // still busy: the frame thread watches the fence itself (X6)
            return !x->doneFence; },
        [] { return rw::NowMs(); },
        [](Gpu* x) { DestroyChain(x); }, [](Gpu* x) { x->worker.Retire([x] { x->compose.Release(INFINITE); }); });
    if (say) {
        char line[160];
        if (r.destroyed) { say(rw::ReleasedLine(where, r.ms).c_str()); return; }
        std::snprintf(line, sizeof line, "Frame ring wheel: swapchain kept: the GPU still busy after %.0f ms; destroyed once it is done (next On or reset)", r.ms);
        say(line);
    }
}
// The retired GPU sides whose worker exited: the swapchain destroyed here (the frame thread, OnWheelLayer under transport's
// mutex, as upstream destroys its own; X1), then the worker joined (instant) and the side deleted; a running one is skipped.
// `wait` (OnUiReset, teardown): joins them all first. A swapchain kept at its Off edge says its release here (bug 8).
void Reap(bool wait, void (*say)(const char*)) {
    auto gone = [say](Gpu* g, const char* where) {   // the line is built only for a swapchain that was there, with a log to take it (perf nit)
        rw::SayReleased(DestroyChain(g) && say, [where] { return rw::ReleasedLine(where, -1); }, [say](const char* line) { if (say) say(line); });
    };
    // Perf's X6 check: a kept swapchain goes once its GPU work is done, worker or not, so the next On waits a frame or two.
    retiring.DestroyDone([](Gpu* g) { return g->chain != XR_NULL_HANDLE; },
        [](Gpu* g) { return !g->doneFence || g->doneFence->GetCompletedValue() >= g->doneValue; },
        [&](Gpu* g) { gone(g, rw::ReapWhere(wait, rw::ReapStep::GpuDone)); });   // review N3: at the session's end it is the session's
    retiring.Reap(wait, [](Gpu* g) { return g->worker.Exited(); }, [](Gpu* g) { g->worker.Stop(); },
        [&](Gpu* g) { gone(g, rw::ReapWhere(wait, rw::ReapStep::WorkerGone)); }, [](Gpu* g) { delete g; });
}
HMODULE Module() {
    HMODULE m = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(&Module), &m);
    return m;
}
// T036 (2026-11-16): a sub-menu's picture for the atlas worker (its icon source; never the frame thread): decoded from the
// plugin once a process (ring_wheel_menu_art.h), said once each ("Frame ring wheel: sub-menu picture").
std::shared_ptr<const ia::icons::Image> MenuArt(int third, void (*say)(const char*)) {
    static std::atomic<bool> said[rw::ThirdCount] = {};
    const auto d = rw::menuart::Get(Module(), third);
    if (say && !said[third].exchange(true)) {   // the worker's own log (its atlas lines come from the same thread)
        char line[200];
        if (d.image) std::snprintf(line, sizeof line, "Frame ring wheel: sub-menu picture %s: ours, %ux%u from a %zu-byte PNG", rw::ThirdName[third], d.image->width, d.image->height, d.bytes);
        else std::snprintf(line, sizeof line, "Frame ring wheel: sub-menu picture %s %s: its drawn mark, silver", rw::ThirdName[third], d.found ? "failed to decode" : "missing from the plugin");
        say(line);
    }
    return d.image;
}
// The On edge (the first frame with the Lab On and a session): the swapchain (once; refused = upstream all session) and
// the worker (the compose pass's setup and the atlases, off the frame thread). Never on wheel open.
bool waitSaid = false;   // X6: "swapchain waits" said once per wait
void Setup(lab::WheelLayerFrame& f) {
    if ((saidUpstream & 1) || !f.session || !f.device || !f.queue || !f.create || !f.enumerate) return;
    // Review-r6 X6: never a second Lab swapchain while a retired one is alive (rapid On / Off with a busy GPU); Reap destroys
    // it once its worker has exited, and the next frame makes the new one. Upstream's wheel shows meanwhile.
    if (!retiring.MayCreate(gpu != nullptr, [](Gpu* g) { return g->chain != XR_NULL_HANDLE; })) {
        if (!gpu && !waitSaid && f.say) { waitSaid = true; f.say("Frame ring wheel: swapchain waits: the last one is still being freed (rapid Off / On); upstream's wheel meanwhile"); }
        return;
    }
    waitSaid = false;
    LARGE_INTEGER freq, t0; QueryPerformanceFrequency(&freq); QueryPerformanceCounter(&t0);
    const XrSession session = *static_cast<const XrSession*>(f.session);
    if (session == XR_NULL_HANDLE) return;
    auto* g = new Gpu;
    g->destroy = reinterpret_cast<PFN_xrDestroySwapchain>(const_cast<void*>(f.destroy));
    XrSwapchainCreateInfo ci{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    ci.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
    ci.format = f.format; ci.width = rw::SwapchainW; ci.height = rw::SwapchainH;
    ci.sampleCount = ci.faceCount = ci.arraySize = ci.mipCount = 1;
    const auto create = reinterpret_cast<PFN_xrCreateSwapchain>(const_cast<void*>(f.create));
    const auto enumerate = reinterpret_cast<XrResult (*)(XrSwapchain, uint32_t, uint32_t*, XrSwapchainImageBaseHeader*)>(const_cast<void*>(f.enumerate));
    const XrResult r = create(session, &ci, &g->chain);
    uint32_t count = 0;
    if (XR_SUCCEEDED(r) && XR_SUCCEEDED(enumerate(g->chain, 0, &count, nullptr)) && count) {
        g->images.assign(count, {XR_TYPE_SWAPCHAIN_IMAGE_D3D12_KHR});
        if (XR_FAILED(enumerate(g->chain, count, &count, reinterpret_cast<XrSwapchainImageBaseHeader*>(g->images.data())))) g->images.clear();
    }
    if (XR_FAILED(r) || g->images.empty()) {
        if (g->chain != XR_NULL_HANDLE && g->destroy) g->destroy(g->chain);
        delete g;
        SayOnce(f, 1, r == XR_ERROR_LIMIT_REACHED ? "swapchain refused (the runtime's limit) for this session" : "swapchain not made for this session");
        return;
    }
    rw::Fonts fonts;
    const bool fontsOk = rw::FontsFromModule(Module(), fonts);
    g->worker.Start(&g->compose, f.device, static_cast<ID3D12CommandQueue*>(f.queue), f.format, std::move(fonts),
        [g, say = f.say](int id) { const int t = rw::menuart::ThirdOfArt(id); return t >= 0 ? MenuArt(t, say) : rw::treeart::SlotOfArt(id) >= 0 ? rw::treeart::Picture(Module(), id) : rw::glyphart::SlotOfArt(id) >= 0 ? rw::glyphart::Picture(Module(), id) : g->icons.Take(id); }, [g] { g->icons.Settle(2500); }, f.say);   // X3: no upstream call on the worker; T036: our sub-menu pictures; T025: our painted tree squares
    gpu = g;
    seatsKey = 0;   // ask the worker for the current seats
    if (f.say) {
        LARGE_INTEGER t1; QueryPerformanceCounter(&t1);
        char line[220];
        std::snprintf(line, sizeof line, "Frame ring wheel: setup swapchain=made %dx%d (%u images) fonts=%s edge=%.1f ms; the pass and atlases build on the worker",
                      rw::SwapchainW, rw::SwapchainH, count, fontsOk ? "embedded" : "missing (no text)", double(t1.QuadPart - t0.QuadPart) * 1000.0 / double(freq.QuadPart));
        f.say(line);
    }
}
}  // namespace

// ---- Job B: pointing with either hand (steamvr/ring_wheel_pointing.h; feedback-1-plan.md item 1, critic FB1 / FS1) ----
namespace pt = framevr::ringwheel::pointing;
pt::Owner pointing;
int pointerOwner = -1;   // this frame's pointing owner (-1 none): Input's trigger and cancel, pulses
bool ringShown = false;                  // Layer: this frame's wheel quad is the ring's (item 4)
rw::Instance composedList[rw::MaxDraw];  // the last composed instance list: what the ring's image shows (item 4; moved from Layer's static)
int composedN = 0; rw::ContentRect composedRect{};
rw::Settings setNow;     // the engine's settings as Input last built them (the pointer's "on a target" test)
int otherTreesSaid = -1; ULONGLONG otherTreesSaidAt = 0;   // round 2026-11-08 item 2: the Other trees row's last logged value
pt::Allow AllowNow() {
    const int v = int(std::lround(labstore::Value(lab::RingWheelPointingHand)));
    return v == 1 ? pt::Allow::Right : v == 2 ? pt::Allow::Left : v == 3 ? pt::Allow::OtherHand : pt::Allow::Either;
}
bool pointingLive = false;   // the Owner holds state (an owner or samples) since its last reset
void DropOwner() { if (pointingLive || pointerOwner >= 0) { pointing.Reset(); pointingLive = false; } pointerOwner = -1; }   // perf nit 3: not every closed frame
// The non-owner's beam (Pointer writes it each frame, OnWheelBeam submits it): upstream's beam picture at 40% of its width
// (the pointer layer has no opacity of its own; a thinner beam reads dimmer), never a dot.
constexpr float BeamWidthM = .003f, Beam2Share = .4f;   // upstream's laser width (transport's motionQuad(laser, ...))
bool beam2 = false;
ia::Pose beam2Pose{};
float beam2Len = 0;
XrCompositionLayerQuad beam2Quad{XR_TYPE_COMPOSITION_LAYER_QUAD};   // lives until xrEndFrame (the submit keeps a pointer)
bool beamNoSlotSaid = false;   // the last "second beam skipped" state said
// One step of the owner: where(hand) = 0 off the panel, 1 on nothing, 2 on a target; dir(hand) = the ray's direction.
template <class Where, class Dir>
int StepOwner(const lab::WheelPointerFrame& f, bool tracked0, bool tracked1, Where where, Dir dir) {
    pt::In in;
    in.nowMs = rw::NowMs(); in.open = f.wheelHand >= 0; in.wheelHand = f.wheelHand; in.allow = AllowNow();
    const bool tracked[2] = {tracked0, tracked1};
    const auto* hi = static_cast<const framevr::skills::HandInput*>(f.hands);
    for (int h = 0; h < 2; ++h) {
        auto& x = in.hand[h];
        x.tracked = tracked[h];
        const auto d = dir(h);
        x.dir[0] = d[0]; x.dir[1] = d[1]; x.dir[2] = d[2];
        const int w = tracked[h] ? where(h) : 0;
        x.onPanel = w > 0; x.onTarget = w == 2;
        if (hi) { x.trigger = hi->trigger[h] > .55f; x.anyDown = x.trigger || hi->face[h][0] || hi->face[h][1] || hi->grip[h] > rw::GripDown; }
        if (quickOut.holding >= 0 && quickOut.hand == h) x.anyDown = true;   // a hold-to-assign running on this hand
    }
    pt::Out o;
    if (in.allow == pt::Allow::OtherHand) {   // the row's Off: before job B exactly, the hand not holding the wheel always points
        pointing.Reset();
        const int other = f.wheelHand >= 0 && tracked[1 - f.wheelHand] ? 1 - f.wheelHand : -1;
        o.owner = other; o.changed = other != pointerOwner; o.why = other >= 0 ? pt::Why::Row : pt::Why::Untracked;
    } else { o = pointing.Step(in); pointingLive = true; }
    const bool changed = o.owner != pointerOwner;   // against what the Lab used last frame (a row change between the two paths)
    pointerOwner = o.owner;
    if (changed && f.say) {
        char line[120];
        std::snprintf(line, sizeof line, "Frame ring wheel: pointer owner=%s why=%s", o.owner < 0 ? "none" : o.owner ? "left" : "right", pt::WhyName(o.why == pt::Why::None ? pt::Why::Row : o.why));
        f.say(line);
    }
    return o.owner;
}

// Item 2: one grab step; open false = the wheel closed or not world-locked (a grab in progress ends). Logs start / end at
// most once a second, and every clear or respawn.
ia::Pose GrabStep(lab::WheelPointerFrame& f, const ia::Pose& head, const ia::Pose& spawn, const rw::Placement* p, bool open, bool spawned) {
    // Review-grip S3: the grabbing hand is job B's pointing owner (last frame's, as the buttons and grabHit are), else the
    // hand not holding the wheel (before job B, and the Pointing hand row's Other hand).
    const int pointer = rw::GrabHand(pointerOwner, f.wheelHand);
    rw::GrabIn in;
    in.on = labstore::On(lab::RingWheelGrab); in.remember = labstore::On(lab::RingWheelGrabKeep);
    in.reset = labstore::On(lab::RingWheelGrabReset);
    if (in.reset) labstore::Set(lab::RingWheelGrabReset, 0);   // a press: the row turns itself Off
    in.open = open; in.spawned = spawned;
    if (p) { in.placementChanged = grabPlaceSet && !(*p == grabPlace); grabPlace = *p; grabPlaceSet = true; }
    in.pageChanged = open && grabPage >= 0 && f.page != grabPage; grabPage = open ? f.page : -1;
    in.base = spawn; in.head = head; in.hand = PoseOf(f.aim); in.handTracked = f.aimTracked;
    if (f.aims && f.aimsTracked) {   // job B: that hand's own aim
        const auto& a = static_cast<const framevr::gameplay::Pose*>(f.aims)[pointer];
        in.hand = {a.p, a.q}; in.handTracked = f.aimsTracked[pointer];
    }
    in.handIndex = pointer; in.grip = gripIn[pointer]; in.click = triggerIn[pointer]; in.face = faceIn[pointer]; in.hit = grabHit;
    const rw::GrabOut o = ringGrab.Step(in);
    const ULONGLONG now = GetTickCount64();
    const bool startEnd = o.event == rw::GrabEvent::Start || o.event == rw::GrabEvent::End;
    if (o.event != rw::GrabEvent::None && f.say && (!startEnd || now - grabSaidAt >= 1000)) {
        if (startEnd) grabSaidAt = now;
        const auto& k = ringGrab.Kept();
        const math::Vec d = o.pose.p - head.p;
        char line[220];
        if (o.event == rw::GrabEvent::Start) std::snprintf(line, sizeof line, "Frame ring wheel: grab start, the %s hand", pointer ? "left" : "right");
        else if (o.event == rw::GrabEvent::End)
            std::snprintf(line, sizeof line, "Frame ring wheel: grab end (%s): moved %.2f, %.2f, %.2f m, turned %.0f deg, tilted %.0f deg, %.2f m from the eyes",
                          o.why, k.position.x, k.position.y, k.position.z, k.yaw * 180 / rw::Pi, k.pitch * 180 / rw::Pi, std::sqrt(math::Dot(d, d)));
        else if (o.event == rw::GrabEvent::Respawned)
            std::snprintf(line, sizeof line, "Frame ring wheel: grab respawn: the remembered place was %.0f deg from the gaze (over %.0f), back to the default", o.gazeDeg, rw::GrabStrandDeg);
        else std::snprintf(line, sizeof line, "Frame ring wheel: grab place cleared (%s)", o.why);
        f.say(line);
    }
    return o.pose;
}
void Pointer(lab::WheelPointerFrame f) {
    placed = false; hit = false; beam2 = false;
    if (!labstore::TabOn(lab::TabRingWheel)) { placer.Reset(); DropOwner(); if (ringGrab.State() != framevr::panels::Grab::State::Idle || !rw::IsZero(ringGrab.Kept())) ringGrab.NewSession(); return; }   // the Off edge's drain: upstream's values stand (item 2: a moved place forgotten)
    const rw::Placement p = Read();
    if (f.wheelHand < 0 || !p.worldLock || f.page == int(ia::MenuPage::Map)) {   // upstream's anchor (and its other-hand pointer)
        placer.Reset(); GrabStep(f, {}, {}, &p, false, false);   // item 2: a grab in progress ends
        if (pointerOwner >= 0 && f.say) f.say(f.wheelHand < 0 ? "Frame ring wheel: pointer owner=none why=closed" : "Frame ring wheel: pointer owner=none why=upstream");
        DropOwner(); return;
    }
    const ULONGLONG now = GetTickCount64();
    if (!(p == tuneTold) && now - tuneToldAt > 1000 && f.say) {
        tuneTold = p; tuneToldAt = now;
        char line[200];
        std::snprintf(line, sizeof line, "Frame ring wheel: Placement %.0f cm ahead, %.0f cm wide, %.0f deg below the eyes, %s",
                      p.distanceM * 100, p.widthM * 100, p.belowDeg, p.worldLock ? "stays in the room" : "upstream's hand anchor");
        f.say(line);
    }
    const ia::Pose head = PoseOf(f.head);
    const ia::Pose& spawn = placer.Step(f.wheelHand, head, p);
    const ia::Pose pose = GrabStep(f, head, spawn, &p, true, placer.Spawned);   // item 2: the spawn, or where the player moved it
    if (placer.Spawned && f.say && now - placedToldAt > 10000) {
        placedToldAt = now;
        const math::Vec d = pose.p - head.p;
        char line[200];
        std::snprintf(line, sizeof line, "Frame ring wheel: placed %.2f m ahead, %.1f deg below the eyes, %.2f m wide (%.1f px a degree), %s hand's wheel",
                      std::sqrt(math::Dot(d, d)), std::asin(std::clamp(-d.y / std::max(1e-4f, std::sqrt(math::Dot(d, d))), -1.f, 1.f)) * 180 / rw::Pi,
                      p.widthM, rw::PxPerDeg(p.widthM, p.distanceM), f.wheelHand ? "left" : "right");
        f.say(line);
    }
    Write(f.palettePose, pose);
    placed = true; placedWidth = p.widthM; placedPose = pose;
    // Each hand's ray against the panel where it now is: the ring's content quad on a ring frame (canvas px into the ring's
    // engine; upstream's hover -1, so its picture never repaints on Lab pointing), else upstream's square picture. Job B: either
    // hand may point; the owner (ring_wheel_pointing.h) gets the beam, the dot, the hover and the hit.
    const bool ring = ringActive && RingShown(f.page);
    f.hover = -1; f.pointerHit = false;
    grabHit = false;
    struct Ray { bool tracked = false, met = false; math::Vec from{}, dir{}; math::PanelHit h{}; float x = 0, y = 0; int cell = -1; };
    Ray rays[2];
    const Quad q = QuadOf(pose, p.widthM, developerNow);
    for (int hand = 0; hand < 2; ++hand) {
        auto& r = rays[hand];
        if (f.aims && f.aimsTracked) {
            r.tracked = f.aimsTracked[hand];
            const auto& a = static_cast<const framevr::gameplay::Pose*>(f.aims)[hand];
            r.from = a.p; r.dir = math::Rotate(a.q, {0, 0, -1});
        } else if (hand == 1 - f.wheelHand) {   // no job B fields: upstream's other hand only
            r.tracked = f.aimTracked;
            const ia::Pose a = PoseOf(f.aim);
            r.from = a.p; r.dir = math::Rotate(a.q, {0, 0, -1});
        }
        if (!r.tracked) continue;
        if (ring) {
            r.met = math::IntersectPanel(r.from, r.dir, q.pose.p, q.pose.q, q.w, q.h, r.h);
            if (r.met) rw::CanvasOf(r.h.u, r.h.v, q.c.x0, q.c.y0, q.c.canvasW, float(rw::ContentRows), r.x, r.y);
        } else {
            r.met = math::IntersectPanel(r.from, r.dir, pose.p, pose.q, p.widthM, p.widthM, r.h);
            if (r.met) r.cell = f.cell ? f.cell(r.h.u, r.h.v) : -1;
        }
    }
    const int owner = StepOwner(f, rays[0].tracked, rays[1].tracked, [&](int hand) {
        const auto& r = rays[hand];
        if (!r.met) return 0;   // 0 off the panel, 1 on nothing, 2 on a target
        if (!ring) return r.cell >= 0 ? 2 : 1;
        if (ringPage == rw::RingPage::Main || ringPage == rw::RingPage::GlyphChoose) {
            const auto k = engine.HitTest(r.x, r.y, setNow).kind;
            return k == rw::Kind::None || k == rw::Kind::Gap ? 1 : 2;
        }
        return 2;   // the Glyphs and Gestures pages: anywhere on the panel counts
    }, [&](int hand) { const auto& d = rays[hand].dir; return std::array<float, 3>{d.x, d.y, d.z}; });
    if (owner < 0 || !rays[owner].tracked) { f.pointerVisible = false; return; }   // no owner: no laser, no hover, no hit
    {   // the other hand's beam, thin and without a dot (OnWheelBeam submits it), when it may point too
        const int other = 1 - owner;
        const auto& o = rays[other];
        const pt::Allow allow = AllowNow();
        if (o.tracked && allow != pt::Allow::OtherHand && pt::Allowed(allow, other, f.wheelHand)) {
            beam2Len = o.met ? o.h.distance : rw::BeamMissM;
            const math::Vec mid = o.from + o.dir * (beam2Len * .5f);
            beam2Pose = {mid, math::BeamOrientation(o.dir, head.p - mid)};
            beam2 = true;
        }
    }
    const auto& r = rays[owner];
    f.pointerHit = r.met;
    if (r.met) {
        if (ring) { hit = true; hitX = r.x; hitY = r.y; } else f.hover = r.cell;
    }
    grabHit = r.met;   // item 2: the next frame's grab arms only with the owner's ray on the panel
    rw::HoldUpstreamWhileMoving(ringGrab.Holding(), ring, f.hover, f.pointerHit);   // review-grip S1: no upstream click mid-move
    f.beamLength = r.met ? r.h.distance : rw::BeamMissM;   // S1: the beam still ends on a moving panel
    const math::Vec mid = r.from + r.dir * (f.beamLength * .5f);
    Write(f.beamPose, {mid, math::BeamOrientation(r.dir, head.p - mid)});
    f.pointerVisible = true;
    if (f.pointerHit) Write(f.dotPose, {r.h.point + math::Rotate(pose.q, {0, 0, .003f}), pose.q});
}

// ---- Bug 9 (bug-autoclose.md): the world-locked ring keeps the wheel open while the resting wheel hand is out of view;
// every open and close is logged with its reason (Jeff: "why did it close?") ----
int pageWas = -1;                         // last frame's upstream page (the Input seam's)
ULONGLONG openedAt = 0, trackSaidAt = 0;
double openedMs = 0, switchedMs = -1;     // the trace for audit bug 6: this open and its last switch on the fine clock (under 0: none)
rw::FooterClick footerClick;              // audit bug 3: upstream's own footer click on its own page
uint64_t sessionWas = 0;
bool clickWas[2]{};
rw::WheelEdges edges;                     // opened / switched / closed and the close's reason (review-bug9 M1: pure, tested)
const char* pausedSaid = nullptr;         // the last "ring paused:" reason said this open (review-bug9 S2: only on a change)
// Round 2026-11-08 #1 (Jeff's rule for closing; ring_wheel_close.h)
rw::closing::Hold hold; rw::closing::FocusBlip focusBlip;
const char* focusWhy = nullptr;   // the clear's name (headset-focus, session-end, xr-sync-failed, copy-failed), from Blip to Cleared in one ClearInput
bool shutBeforeClear = false;     // HandControls had no wheel at that Blip: upstream closed it on the last focused frame
ULONGLONG keptSaidAt = 0;
const char* HandName(int h) { return h == 1 ? "left" : "right"; }
const char* UpPageName(int page) {
    switch (ia::MenuPage(page)) {
    case ia::MenuPage::Spells: return "Spells";
    case ia::MenuPage::Glyphs: return "Glyphs";
    case ia::MenuPage::Motions: return "Gestures";
    case ia::MenuPage::Equipment: return "Equipment";
    case ia::MenuPage::Map: return "Map";
    case ia::MenuPage::Developer: return "DEV";
    default: return "an editor";
    }
}
// Why a cell the Lab released to upstream would close the wheel (the footer's cells as interaction::FooterCell).
const char* CellWhy(int cell) {
    if (cell == CenterCell) return "center";
    if (cell == ia::GlyphsCell) return "glyphs";
    if (cell == ia::MotionsCell) return "gestures";
    if (cell == ia::LabCell) return "footer-lab";
    switch (cell) {
    case 9: return "footer-equipment";
    case 10: return "footer-vr-settings";
    case 11: return "footer-inventory";
    case 12: return "footer-map";
    case 13: return "footer-dev";
    default: return "upstream-cell";
    }
}
// FilterWheelTracking, before HandControls steps: the hold (ring_wheel_close.h); with the wheel closed upstream's values stand.
void Tracking(lab::WheelTrackingFrame f) {
    if (!labstore::TabOn(lab::TabRingWheel) || !f.tracked || !f.aim || !f.active || !f.time || !f.context) return;
    focusBlip.Focused();
    const bool open = f.wheelHand >= 0;
    const auto o = hold.Hand(*f.time, *f.context, *f.active, open, f.session, f.epoch, f.gate, f.localPlayer);
    if (o.moved) placer.Reset();   // recenter or eye height: the wheel places itself again in front of the headset, in reach
    if (!f.say || (!o.gapS && (!open || hold.Safety()))) return;
    const ULONGLONG now = GetTickCount64();
    if (o.gapS > 0 && now - keptSaidAt > 1000) {   // upstream would have closed the wheel on this gap
        keptSaidAt = now;
        char line[100];
        std::snprintf(line, sizeof line, "Frame ring wheel: kept open through a %.0f ms gap", o.gapS * 1000);
        f.say(line);
    }
    const bool all = f.base && f.tracked[0] && f.tracked[1] && f.aim[0] && f.aim[1];
    if (open && !hold.Safety() && !all && now - trackSaidAt > 10000) {
        trackSaidAt = now;
        f.say("Frame ring wheel: tracking: a hand or the headset out of view, the wheel stays open");
    }
}
// The headset unfocused or no valid frame: a blip skips that frame's input, so the open wheel stays (250 ms: a safety close).
bool Blip(lab::WheelBlipFrame f) {
    if (!labstore::TabOn(lab::TabRingWheel)) { focusBlip.Focused(); return false; }
    const bool holdIt = focusBlip.Step(f.wheelHand >= 0, double(f.nowMs), f.ending);
    if (edges.Open() && !holdIt) { focusWhy = f.name ? f.name : f.ending ? "session-end" : "headset-focus"; shutBeforeClear = f.wheelHand < 0; }
    return holdIt;
}
// A close none of WheelEdges' reasons names: an upstream page's own, a held bottle, bad values, or unknown with its source.
std::string NameClose(const std::string& reason, const framevr::skills::HandInput* hi) {
    if (reason != "upstream") return reason;
    if (!ringActive) return "upstream-page";
    if (hi && hi->occupied) return "occupied";
    if (hi && !std::isfinite(hi->trigger[0] + hi->trigger[1] + hi->grip[0] + hi->grip[1])) return "bad-input";
    char source[80];
    std::snprintf(source, sizeof source, "unknown active=%d context=%llu gap=%.0fms", hi ? int(hi->active) : -1, hi ? (unsigned long long)hi->context : 0ull, hold.RealStep() * 1000);
    return source;
}
// OnClearInput (audit 2026-11-10 bug 2): transport's ClearInput closes the wheel (handControls.Suspend, interactions.Reset)
// outside a focused frame, where the Input seam never runs: the headset unfocused past the blip, the session stopping, a
// failed sync or copy. The close line is written here, with the time the wheel was open until now; the next focused frame's
// Edges finds it closed. A clear no Blip named is the session's end (transport's state handler and Shutdown).
void Cleared(lab::NoArgs) {
    const char* why = focusWhy ? focusWhy : "session-end";
    const bool shut = shutBeforeClear;
    focusWhy = nullptr; shutBeforeClear = false;
    if (!edges.Open() || !labstore::TabOn(lab::TabRingWheel)) return;   // closed: one branch. Off: the Off edge says lab-off
    const auto e = edges.Cleared(why, shut);
    if (sayKept) sayKept(rw::ClosedLine(NameClose(e.reason, nullptr).c_str(), double(GetTickCount64() - openedAt) / 1000, UpPageName(pageWas)).c_str());
}
// The wheel's edges, once a frame at the top of the Input seam (after HandControls, before interactions.Step): opened,
// switched, closed with its reason (rw::WheelEdges: the Lab's own write last frame, a stick click, a safety reason, a new
// session, else named here or unknown with its source).
void Edges(const lab::WheelInputFrame& f, const ia::Snapshot* s) {
    bool click[2]{};
    if (f.hands) {
        const auto& hi = *static_cast<const framevr::skills::HandInput*>(f.hands);
        for (int h = 0; h < 2; ++h) { click[h] = hi.click[h] && !clickWas[h]; clickWas[h] = hi.click[h]; }
    }
    const uint64_t session = s ? s->session : sessionWas;
    const ULONGLONG now = GetTickCount64();
    char line[200];
    const auto e = edges.Frame(f.wheelHand, click, hold.Safety(), session);   // a cleared input's close is Cleared's
    if (e.kind == rw::WheelEdges::Closed) {
        if (f.say) f.say(rw::ClosedLine(NameClose(e.reason, static_cast<const framevr::skills::HandInput*>(f.hands)).c_str(), double(now - openedAt) / 1000, UpPageName(pageWas)).c_str());
    } else if (e.kind == rw::WheelEdges::Opened) {
        openedAt = now; pausedSaid = nullptr; openedMs = rw::NowMs(); switchedMs = -1;
        if (f.say) { std::snprintf(line, sizeof line, "Frame ring wheel: opened %s hand's wheel, page %s", HandName(f.wheelHand), UpPageName(f.page)); f.say(line); }
    } else if (e.kind == rw::WheelEdges::Switched) {   // the trace for audit bug 6: which stick clicks, and how soon after the open or the last switch
        const double t = rw::NowMs();
        if (f.say) f.say(rw::SwitchedLine(f.wheelHand, click, t - openedMs, switchedMs < 0 ? -1 : t - switchedMs).c_str());
        switchedMs = t;
    }
    pageWas = f.page; sessionWas = session;
}

// R6Q: what QuickCast reads each frame, on the main ring and on the chooser's order row: both hands' ten slots (upstream's
// rule, hand_controls.h): trigger, primary, secondary, grip + primary, grip + secondary; each slot's skill; the two rows.
void FillQuick(rw::QuickIn& q, const lab::WheelInputFrame& f, const ia::Snapshot& s, double nowMs) {
    q.nowMs = hold.NowMs(nowMs); q.main = true; q.lastHitMs = hold.NowMs(fightNow.lastHitMs);   // round 2026-11-08 review M1: a hitch is not a long hold
    q.holdMs = int(std::lround(labstore::Value(lab::RingWheelHoldMs))); q.tapMs = int(std::lround(labstore::Value(lab::RingWheelTapMs)));
    if (f.hands) {
        const auto& hi = *static_cast<const framevr::skills::HandInput*>(f.hands);
        for (int h = 0; h < 2; ++h) {
            const bool down[3] = {hi.trigger[h] > .55f, hi.face[h][0], hi.face[h][1]};
            const bool grip = hi.grip[h] > .5f;
            q.press[h] = rw::SlotPress(down, grip, quickHeld[h]);
            for (int k = 0; k < rw::Slots; ++k) q.down[h][k] = down[k == 0 ? 0 : (k - 1) % 2 + 1];
        }
    }
    for (int h = 0; h < 2; ++h) for (int k = 0; k < rw::Slots; ++k) { const int slot = rw::SlotOf(h, k); q.bound[size_t(h)][size_t(k)] = slot >= 0 ? s.skills[size_t(slot)].id : -1; }
}
// 2026-11-16 (Jeff: "ONE quick cast"): T036's Quick cast menu button (exp.ring_wheel.quick_button, retired) becomes Quick cast's
// own button (exp.ideas.quick_radial.button) once, unless Quick cast has one already; the old key goes to 0 either way. Stored
// values (Quick cast's switch may be Off: its gated button reads 0). Runs only while the old key holds a button: once.
void MoveQuickMenuButton(void (*say)(const char*)) {
    const auto v = labstore::Stored();
    const float moved = rw::MovedQuickButton(v[lab::RingWheelQuickButton], v[lab::QuickRadialButton]);
    const int slot = rw::ButtonSlotOf(v[lab::RingWheelQuickButton]);
    labstore::Set(lab::RingWheelQuickButton, 0);
    if (moved > 0) { labbuttons::Claim(slot, lab::QuickRadialButton, say); labstore::Set(lab::QuickRadialButton, moved); }
    if (!say) return;
    char line[260];
    if (moved > 0) std::snprintf(line, sizeof line, "Frame quick radial: the old Quick cast menu button (%s) is Quick cast's own button now (exp.ideas.quick_radial.button %.0f)%s",
                                 SlotWords(slot, ctrlLayoutNow).c_str(), moved, v[lab::QuickRadialOn] > .5f ? "" : "; Quick cast is Off: Lab > Experiment: Ideas > Combat > Quick cast radial");
    else if (slot == framevr::skills::TriggerSlot(0) || slot == framevr::skills::TriggerSlot(1))   // review N2
        std::snprintf(line, sizeof line, "Frame quick radial: the old Quick cast menu button (%s) let go: a trigger stays the attack", SlotWords(slot, ctrlLayoutNow).c_str());
    else std::snprintf(line, sizeof line, "Frame quick radial: the old Quick cast menu button (%s) let go: Quick cast keeps its own button (%s)", SlotWords(slot, ctrlLayoutNow).c_str(),
                       SlotWords(rw::ButtonSlotOf(v[lab::QuickRadialButton]), ctrlLayoutNow).c_str());
    say(line);
}
// The Quick cast third tapped (its trigger click, a tap of any button), or its shelf entry (2026-11-16, Jeff: "ring menu > quick
// cast"): its page opens inside the ring, as Glyphs and Gestures do. Its switch Off or a class that keeps upstream: nothing opens,
// three short pulses, said.
void OpenQuickCast(int hand, const ia::Snapshot& s, lab::WheelInputFrame& f, double heldMs, const char* how) {
    char line[240];
    const char* no = !labstore::On(lab::QuickRadialOn) ? "Quick cast is Off (Lab > Experiment: Ideas > Combat > Quick cast radial)"
                   : !framevr::quickradial::ClassKnown(s.skillClass) ? "this class keeps upstream" : nullptr;
    if (no) {
        QueuePulses(hand, 3, .35f);
        if (f.say) { std::snprintf(line, sizeof line, "Frame ring wheel: Quick cast third (%s): nothing opens: %s", how, no); f.say(line); }
        return;
    }
    (void)f;
    quickPageOn = true; quickChoose = -1; QueuePulses(hand, 1, .25f);   // the page from the next frame (rp)
    if (f.say) { std::snprintf(line, sizeof line, "Frame ring wheel: sub-menu Quick cast open (%s, the %s hand) held=%.0f ms", how, hand ? "left" : "right", heldMs); f.say(line); }
}
// The hold while its ring shows (frac over 0): its ticks at 25 / 50 / 75 % (eval #10), its words for the center (L3).
void HoldStep(const rw::QuickOut& e, float frac, const rw::QuickIn& q, const ia::Snapshot& s, int ctrlLayout) {
    if (e.holding >= 0 && frac > 0) {
        if (const int ticks = rw::TicksCrossed(holdFracWas, frac)) QueuePulses(e.hand, ticks, .15f);
        if (holdFracWas <= 0) {
            holdSlotText = (e.hand ? "Left " : "Right ") + rw::SlotName(framevr::controllers::Layout(ctrlLayout), e.hand, e.slot);
            if (e.slot == 0) holdSlotText = e.hand ? "Left trigger" : "Right trigger";
            const int was = q.bound[size_t(e.hand)][size_t(e.slot)];
            const auto* k = was >= 0 ? SkillOf(s, was, -1) : nullptr;
            const std::string skillName = !k ? std::string() : rw::orders::Is(was) ? std::string(rw::orders::Short(was)) : tx::Clean(k->name);   // an order alone on the line: without "Army: "
            const rw::HoldWords words = rw::HoldWordsFor(rw::HeldBy(was, glyphDrawNow, e.hand, e.slot), k ? skillName.c_str() : nullptr, e.holding);   // audit bug 1: a Draw glyph button
            rw::HoldWords lw = words;   // review S1: a button another Lab keeps says so ("Replaces Army X menu")
            if (!lw.bound) if (const char* other = labbuttons::OwnerOf(rw::SlotOf(e.hand, e.slot)); other) lw = {other, true};

            holdStatusText = lw.text; holdBound = lw.bound;
        }
        holdFracWas = frac;
    } else holdFracWas = 0;
}

void Input(lab::WheelInputFrame f) {
    if (f.say) sayKept = f.say;
    if (!labstore::TabOn(lab::TabRingWheel)) { ringActive = false; engineOpen = false; return; }
    if (f.hands) {   // item 2: the grab's buttons, read by the next Pointer
        const auto& hi = *static_cast<const framevr::skills::HandInput*>(f.hands);
        for (int h = 0; h < 2; ++h) { gripIn[h] = hi.grip[h] > rw::GripDown; triggerIn[h] = hi.trigger[h] > .55f; faceIn[h] = hi.face[h][0] || hi.face[h][1]; }
    }
    if (f.engineIn) hold.Engine(*static_cast<ia::Input*>(f.engineIn), f.wheelHand >= 0);
    Edges(f, static_cast<const ia::Snapshot*>(f.snapshot));
    if (f.wheelHand >= 0 && placed && f.hands) {   // job B: clicks come from the pointing owner's trigger only (none: no click), every page
        const auto& hi = *static_cast<const framevr::skills::HandInput*>(f.hands);
        f.trigger = pointerOwner >= 0 ? hi.trigger[pointerOwner] : 0.f;
    }
    if (!f.snapshot) { ringActive = false; engineOpen = false; return; }
    const auto& s = *static_cast<const ia::Snapshot*>(f.snapshot);
    if (s.session != grabSession) { grabSession = s.session; ringGrab.NewSession(); }   // item 2: a moved place lasts one game session
    developerNow = s.developer;   // review R6 X2: the pointer's quad is the layer's (six tabs, 864 px, in developer mode)
    if (const int mode = int(LayoutNow()); mode != layoutSaid && f.say) {   // E7 #10: the row's value at the first frame and on every change
        char line[96]; std::snprintf(line, sizeof line, "Frame ring wheel: tree layout=%s%s", mode ? "Tree orbs" : "Trees shown", layoutSaid < 0 ? "" : " (changed: new layout)");
        f.say(line); layoutSaid = mode;
    }
    AskOtherLabs(s, f.say);   // headset fixes 2026-11-15: Army X menu's shelf entry, the icon remaps (Kick icon); dark seams: one branch each
    Seats(s);
    if (!restSaid && f.say) {   // R8 match stage 1: where each seat's rest glow came from, once a layout
        char line[112]; std::snprintf(line, sizeof line, "Frame ring wheel: rest glow kit=%d tree=%d (per skill from the kit; tree: not in the kit table)", restFromKit, restByTree);
        f.say(line); restSaid = true;
    }
    if (gpu && !gpu->icons.Done())   // X3: the frame thread asks upstream's icon cache (as its Prefetch does), only until the set is in
        gpu->icons.Poll([](int id) -> std::shared_ptr<const ia::icons::Image> {
            if (rw::menuart::ThirdOfArt(id) >= 0 || rw::treeart::SlotOfArt(id) >= 0 || rw::glyphart::SlotOfArt(id) >= 0) {   // (Glyph icon: and its squares) T036: our sub-menu pictures (T025: and tree squares) come from the plugin on the worker (MenuArt), never upstream's cache
                static const auto ours = std::make_shared<const ia::icons::Image>();   // "in" for the box; the worker never takes it
                return ours;
            }
            if (rw::ItemSeat(id)) {   // 2026-11-16: Quick cast's item (a kind: its picture's), by upstream's item key
                ia::Item it; it.handle = 1; it.code = qr::CodeOfExact(qr::PictureId(id));
                return it.code ? ia::icons::Get(ia::icons::ItemKey(it)) : nullptr;
            }
            return ia::icons::Get("s" + std::to_string(id));
        }, rw::NowMs());
    if (gpu && gpu->compose.Ready()) {
        const unsigned flips = gpu->compose.Flips(rw::AtlasKind::Icons) * 65536u + gpu->compose.Flips(rw::AtlasKind::Glyphs);
        if (flips != seenFlips) {   // perf R6 #5: the worker's lock only when an atlas flipped in
            seenFlips = flips;
            auto now = gpu->worker.Current(gpu->compose);
            if (now && now != built) { built = now; ++revision; }
        }
    }
    if (f.wheelHand < 0 || f.page != int(ia::MenuPage::Glyphs)) choose = -1;   // the chooser lives on upstream's open Glyphs page
    KeepButtons(s, f.say);   // T036: each sub-menu's button, for the Hands seam (next frame) and the badges
    if (labstore::Value(lab::RingWheelQuickButton) > .5f) MoveQuickMenuButton(f.say);   // 2026-11-16: once, the retired Quick cast menu button
    fireGate.Watch(f.wheelHand, f.page, rw::NowMs());   // S-B: every frame, the wheel's hand and page
    if (wantMenu.third >= 0 && fireGate.GaveUp(rw::NowMs(), WantMenuMs)) {   // the wheel never got there: given up, said once
        if (f.say) { char line[200]; std::snprintf(line, sizeof line, "Frame ring wheel: sub-menu %s not opened: the wheel did not get there (%d clicks on page %s, %.0f ms since the ask or a page change)", rw::ThirdName[wantMenu.third], fireGate.fires, UpPageName(f.page), rw::NowMs() - fireGate.sinceMs); f.say(line); }
        wantMenu = {};
    }
    rw::RingPage rp = rw::RingPage::Main;
    ringActive = Ready() && placed && f.wheelHand >= 0 && RingPageOf(f.page, s.skillClass, rp);
    // 2026-11-16: Quick cast's page lives on the open ring over upstream's Spells page, while its switch is On for this class
    if (!ringActive || rp != rw::RingPage::Main || !labstore::On(lab::QuickRadialOn) || !framevr::quickradial::ClassKnown(s.skillClass)) { quickPageOn = false; quickChoose = -1; }
    else if (quickPageOn) rp = quickChoose >= 0 ? (quickKind ? rw::RingPage::QuickKind : rw::RingPage::QuickChoose) : rw::RingPage::Quick;
    if (quickChoose < 0) quickKind = 0;   // a kind's page lives over its place's chooser
    if (!quickPageOn || quickChoose >= 0) { quickMoveFrom = -1; quickMovePick = false; }   // a Move lives on the Quick cast page only (the menu shut, Back, a chooser)
    if (!ringActive) {   // a page change or a close mid-hold: no assign; a reopen starts clean
        if (f.wheelHand >= 0 && RingShown(f.page)) {   // bug 9: the wheel open, the ring not drawn; said once an open and on a change (S2)
            const char* why = !Ready() ? "Frame ring wheel: ring paused: not ready" : !placed ? "Frame ring wheel: ring paused: not placed (World lock Off)" : "Frame ring wheel: ring paused: upstream page";
            if (why != pausedSaid && f.say) f.say(why);
            pausedSaid = why;
        }
        engineOpen = false; ringPageSeen = false; quick.Drop(); fightFade.Reset(); fightNow = {}; deadSaid = false; return;
    }
    wheelHandNow = f.wheelHand;
    rw::Settings set;
    set.layout = layout.mode;   // E7 #10: the layout the seats were placed for
    set.quickCast = rp == rw::RingPage::Main;
    set.footerFull = labstore::On(lab::RingWheelFooterFull);
    set.developer = s.developer;
    set.hasMotions = rp == rw::RingPage::GlyphChoose || rp == rw::RingPage::QuickChoose || framevr::spellmotion::HasMotions(s.skillClass);   // the choosers: Back / Clear
    set.growMs = int(std::lround(labstore::Value(lab::RingWheelGrowMs)));
    set.blurbMs = int(std::lround(labstore::Value(lab::RingWheelBlurbMs)));
    set.thirds = rp == rw::RingPage::Main;   // T036: the center in thirds (the chooser keeps its Back / Clear halves)
    set.thirdGrowPct = labstore::Value(lab::RingWheelThirdGrow);
    if (pageIconsSay >= 0 && f.say) {
        char line[96]; std::snprintf(line, sizeof line, "Frame ring wheel: gesture page icons: %d seats + %d more for the cards", layout.count, pageIconsSay);
        f.say(line); pageIconsSay = -1;
    }
    set.othersPct = labstore::Value(lab::RingWheelOtherTrees);   // round 2026-11-08 item 2: the look only (Engine::HitTarget)
    if (const int pct = int(std::lround(set.othersPct)); pct != otherTreesSaid && f.say) {   // the first frame and a change, at most twice a second while the slider moves
        const ULONGLONG t = GetTickCount64();
        if (otherTreesSaid < 0 || t - otherTreesSaidAt >= 500) {
            char line[96]; std::snprintf(line, sizeof line, "Frame ring wheel: other trees=%d%% (0 hidden, 100 fully visible; what can be hit is the same)", pct);
            f.say(line); otherTreesSaid = pct; otherTreesSaidAt = t;
        }
    }
    setNow = set;   // job B: the pointer's target test (next frame's Pointer)
    rw::StepIn in;
    in.nowMs = rw::NowMs();   // review R6 X5: sub-millisecond steps for the tap limit, the latch and the jolt allowance
    {   // R8: the fight dim and the death / load fade (draw only; a stale snapshot = a load)
        const ULONGLONG t = GetTickCount64();
        const bool fresh = s.at && t >= s.at && t - s.at <= 650;
        const double lastHitBefore = fightNow.lastHitMs;
        fightNow = fightFade.Step(in.nowMs, s.vitalsKnown, s.health, fresh, s.session);
        if (fightNow.hitsOff && !deadSaid && f.say) { deadSaid = true; f.say("Frame ring wheel: death or load: the wheel at 15%, no hits until it opens again"); }
        if (fightNow.lastHitMs != lastHitBefore && f.say && t - hitSaidAt > 10000) { hitSaidAt = t; f.say("Frame ring wheel: hit: the wheel dims to 40% for 1.5 s (the pointed icon stays full)"); }
    }
    in.lastHitMs = fightNow.lastHitMs;
    in.hit = hit && !fightNow.hitsOff; in.x = hitX; in.y = hitY;   // R8: dead or loading, no hits
    in.trigger = f.trigger > .5f;
    if (ringGrab.Holding()) { in.hit = false; in.trigger = false; }   // item 2: moving the wheel points at nothing on it
    const bool pageChanged = ringPageSeen && rp != ringPage;
    if (!ringPageSeen || pageChanged) {
        if (f.say) { char line[120]; std::snprintf(line, sizeof line, "Frame ring wheel: page %s%s", PageName(rp), pageChanged ? "" : " (ring shown)"); f.say(line); }
        if (rp == rw::RingPage::Main || rp == rw::RingPage::GlyphChoose || rp == rw::RingPage::QuickChoose) engineOpen = false;
    }
    ringPage = rp; ringPageSeen = true;
    if (rp == rw::RingPage::GlyphChoose && (chooseKey != seatsKey || chooseStale || chooseQuick)) { BuildChooser(s); engineOpen = false; }   // the seats changed under the chooser
    if (rp == rw::RingPage::QuickChoose && (chooseKey != seatsKey || chooseStale || !chooseQuick)) { BuildChooser(s, true); engineOpen = false; }
    if (!engineOpen) {   // buttons held at open: let go first
        engine.Reset(rp == rw::RingPage::GlyphChoose || rp == rw::RingPage::QuickChoose ? chooseLayout : layout); quick.Reset(); badges.Opened(); engineOpen = true;   // badges: a bind made while closed shows without a pop; no re-place unless something changed
        if (pageChanged) engine.PageChanged(in.nowMs, in.trigger, set);   // L2: 250 ms lockout, a held trigger let go first
        for (auto& h : held) for (bool& b : h) b = true;
        for (auto& h : quickHeld) for (bool& b : h) b = true;
    }
    // Upstream never sees ring pointing (its paint key stays steady, perf M2); a released upstream target reaches it once.
    const float triggerIn = f.trigger;
    f.hover = -1; f.trigger = 0;
    const int pointer = pointerOwner >= 0 ? pointerOwner : 1 - f.wheelHand;   // the laser's hand (job B: the owner)
    char pageLine[220];
    if (wantMenu.third >= 0 && rp != rw::RingPage::GlyphChoose && !rw::QuickPage(rp)) {   // T036: a sub-menu asked for: upstream's page there first, one cell a frame
        const rw::NavStep nav = rw::NavigateTo(wantMenu.third, ia::MenuPage(f.page));
        if (nav.nav == rw::Nav::Fire) {   // review F2: a click is one frame at 1 and a frame at 0 (upstream needs the edge and rearm)
            if (fireGate.Step(in.nowMs)) { f.hover = nav.cell; f.trigger = 1; edges.Wrote(CellWhy(nav.cell)); }
            else f.trigger = 0;
            engineOpen = false; return;
        }
        if (nav.nav == rw::Nav::Done) {
            if (f.say) { std::snprintf(pageLine, sizeof pageLine, "Frame ring wheel: sub-menu %s open (asked %.0f ms ago)", rw::ThirdName[wantMenu.third], in.nowMs - wantMenu.since); f.say(pageLine); }
            wantMenu = {};
        }
    }
    if (rp == rw::RingPage::Glyphs || rp == rw::RingPage::Gestures) {
        quick.Drop(); engineOpen = false;
        rw::PageIn pi;
        pi.nowMs = in.nowMs; pi.hit = in.hit; pi.x = hitX; pi.y = hitY; pi.trigger = triggerIn > .5f; pi.page = rp;
        pi.count = rp == rw::RingPage::Glyphs ? framevr::glyphs::ShapeCount : std::min(framevr::spellmotion::PageOf(s.skillClass).count, rw::MaxItems);
        pageOut = pages.Step(pi, set);
        rw::ToStep(pageOut, step);
        if (pageOut.event == rw::PageEvent::Upstream && pageOut.cell >= 0) { f.hover = pageOut.cell; f.trigger = 1; edges.Wrote(pageOut.cell == CenterCell ? "back" : CellWhy(pageOut.cell)); QueuePulses(pointer, 1, .2f); }
        else if (pageOut.event == rw::PageEvent::ChooseSkill && seatsValid) {
            choose = pageOut.shape;
            const char* shapeName = framevr::glyphs::Name(framevr::glyphs::Shape(choose));
            chooseTitle = shapeName;   // the gold part of the title on the top arc ("Choose a skill for" muted; designer R7 answers 2)
            clearLine = chooseTitle + " will cast nothing";
            BuildChooser(s);   // the ring's glyph list and the chooser's seats from it (the orders' row with a summon army)
            QueuePulses(pointer, 1, .2f);
            if (f.say) { std::snprintf(pageLine, sizeof pageLine, "Frame ring wheel: glyph chooser %s, the %s hand's", shapeName, glyphHandNow ? "left" : "right"); f.say(pageLine); }
        }
        return;
    }
    if (rp == rw::RingPage::Quick) {   // 2026-11-16: Quick cast's 12 places (as the Glyphs page's shapes): trigger on one, its chooser; center Back (Move below, 2026-11-17)
        if (quickMoveFrom >= 0 && !QuickPlaceFilled(quickMoveFrom)) {   // review F9: the place being moved emptied under it: the Move is dropped
            if (f.say) { std::snprintf(pageLine, sizeof pageLine, "Frame ring wheel: Quick cast Move from place %d: dropped (the place is empty now)", PlaceNo(quickMoveFrom)); f.say(pageLine); }
            quickMoveFrom = -1;
        }
        quick.Drop(); engineOpen = false;
        rw::PageIn pi;
        pi.nowMs = in.nowMs; pi.hit = in.hit; pi.x = hitX; pi.y = hitY; pi.trigger = triggerIn > .5f; pi.page = rp; pi.count = rw::QuickPlaces;
        pageOut = pages.Step(pi, set);
        rw::ToStep(pageOut, step);
        if (pageOut.event == rw::PageEvent::Upstream && pageOut.cell >= 0) { f.hover = pageOut.cell; f.trigger = 1; edges.Wrote(CellWhy(pageOut.cell)); QueuePulses(pointer, 1, .2f); }   // a footer tab
        else if ((pageOut.event == rw::PageEvent::Back || pageOut.event == rw::PageEvent::Move) && (quickMoveFrom >= 0 || quickMovePick)) {   // 2026-11-17: Back or Move again stops a Move (the page stays)
            if (f.say) {
                if (quickMoveFrom >= 0) std::snprintf(pageLine, sizeof pageLine, "Frame ring wheel: Quick cast Move from place %d: stopped (%s), nothing moved", PlaceNo(quickMoveFrom), pageOut.event == rw::PageEvent::Back ? "Back" : "Move");
                else std::snprintf(pageLine, sizeof pageLine, "Frame ring wheel: Quick cast Move: stopped before a place was picked (%s)", pageOut.event == rw::PageEvent::Back ? "Back" : "Move");
                f.say(pageLine);
            }
            quickMoveFrom = -1; quickMovePick = false; QueuePulses(pointer, 1, .2f);
        } else if (framevr::quickradial::link::View mv; pageOut.event == rw::PageEvent::Move) {   // 2026-11-17: Move, the center's lower half: pick the place to move
            int filled = 0;
            const bool reading = QuickState(s, mv) == rw::QuickRingState::Reading;
            for (int i = 0; i < mv.n && i < int(mv.ids.size()); ++i) filled += mv.ids[size_t(i)] >= 0;
            if (reading || !filled) {
                QueuePulses(pointer, 3, .35f);
                if (f.say) { std::snprintf(pageLine, sizeof pageLine, "Frame ring wheel: Quick cast Move: %s", reading ? "not now (this character's ring is being read)" : "every place is empty, nothing to move"); f.say(pageLine); }
            } else {
                quickMovePick = true; QueuePulses(pointer, 1, .3f);
                if (f.say) f.say("Frame ring wheel: Quick cast Move: trigger the place to move, then the place it goes to (a filled one swaps; Back stops)");
            }
        } else if (pageOut.event == rw::PageEvent::Back) {   // the ring menu's main page again
            quickPageOn = false; QueuePulses(pointer, 1, .2f);
            if (f.say) f.say("Frame ring wheel: sub-menu Quick cast: Back (the ring menu)");
        } else if (framevr::quickradial::link::View qv; pageOut.event == rw::PageEvent::ChooseSkill && QuickState(s, qv) == rw::QuickRingState::Reading) {   // review S3 / S4
            QueuePulses(pointer, 3, .35f); quickMoveFrom = -1; quickMovePick = false;
            if (f.say) { std::snprintf(pageLine, sizeof pageLine, "Frame ring wheel: Quick cast place %d: not now (this character's ring is still being read)", PlaceNo(pageOut.shape)); f.say(pageLine); }
        } else if (pageOut.event == rw::PageEvent::ChooseSkill && quickMovePick && pageOut.shape >= 0 && pageOut.shape < rw::QuickPlaces) {   // 2026-11-17: Move's source
            if (!(pageOut.shape < qv.n && qv.ids[size_t(pageOut.shape)] >= 0)) {
                QueuePulses(pointer, 3, .35f);
                if (f.say) { std::snprintf(pageLine, sizeof pageLine, "Frame ring wheel: Quick cast Move: place %d is empty, pick a filled one", PlaceNo(pageOut.shape)); f.say(pageLine); }
            } else {
                quickMovePick = false; quickMoveFrom = pageOut.shape; QueuePulses(pointer, 1, .3f);
                if (f.say) { std::snprintf(pageLine, sizeof pageLine, "Frame ring wheel: Quick cast Move from place %d: trigger the place it goes to (a filled one swaps; Back stops)", PlaceNo(quickMoveFrom)); f.say(pageLine); }
            }
        } else if (pageOut.event == rw::PageEvent::ChooseSkill && quickMoveFrom >= 0 && pageOut.shape >= 0 && pageOut.shape < rw::QuickPlaces) {   // 2026-11-17: Move's target
            const int from = quickMoveFrom, to = pageOut.shape;
            quickMoveFrom = -1;
            if (to == from) {
                QueuePulses(pointer, 1, .2f);
                if (f.say) { std::snprintf(pageLine, sizeof pageLine, "Frame ring wheel: Quick cast Move from place %d: left where it was", PlaceNo(from)); f.say(pageLine); }
            } else if (!framevr::quickradial::link::Ask({s.session, to, -1, -1, pointer, from})) {
                QueuePulses(pointer, 3, .35f);
                if (f.say) { std::snprintf(pageLine, sizeof pageLine, "Frame ring wheel: Quick cast Move from place %d to place %d: refused (%d edits already waiting)", PlaceNo(from), PlaceNo(to), framevr::quickradial::link::MaxEdits); f.say(pageLine); }
            } else {
                QueuePulses(pointer, 1, .5f);
                const bool swap = to < qv.n && qv.ids[size_t(to)] >= 0;
                if (f.say) { std::snprintf(pageLine, sizeof pageLine, "Frame ring wheel: Quick cast Move from place %d to place %d (%s; asked of the radial)", PlaceNo(from), PlaceNo(to), swap ? "a filled place: they swap" : "an empty place"); f.say(pageLine); }
            }
        } else if (pageOut.event == rw::PageEvent::ChooseSkill && seatsValid && pageOut.shape >= 0 && pageOut.shape < rw::QuickPlaces) {
            quickChoose = pageOut.shape;
            int filled = 0; for (int i = 0; i < qv.n; ++i) filled += qv.ids[size_t(i)] >= 0;
            const bool has = quickChoose < qv.n && qv.ids[size_t(quickChoose)] >= 0;
            chooseTitle = "place " + std::to_string(PlaceNo(quickChoose));
            clearLine = has ? "Place " + std::to_string(PlaceNo(quickChoose)) + " will be empty" : "Place " + std::to_string(PlaceNo(quickChoose)) + " is empty";
            BuildChooser(s, true);
            QueuePulses(pointer, 1, .2f);
            if (f.say) { std::snprintf(pageLine, sizeof pageLine, "Frame ring wheel: Quick cast chooser for place %d (%d of 12 filled%s)", PlaceNo(quickChoose), filled, quickClock ? ", Fixed places" : ""); f.say(pageLine); }
        }
        return;
    }
    const int place = quickChoose;
    auto ask = [&](int id, int owner, const std::string& what) {   // the radial applies it on its next press frame, saves it and says so
        if (framevr::quickradial::link::View qv; QuickState(s, qv) == rw::QuickRingState::Reading) {   // review S3 / S4: the character changed or its ring is being read
            quickChoose = -1; QueuePulses(pointer, 3, .35f);
            if (f.say) { std::snprintf(pageLine, sizeof pageLine, "Frame ring wheel: Quick cast place %d: not asked (this character's ring is being read; the page shows it once read)", PlaceNo(place)); f.say(pageLine); }
            return;
        }
        if (!framevr::quickradial::link::Ask({s.session, place, id, owner, pointer})) {
            QueuePulses(pointer, 3, .35f);
            if (f.say) { std::snprintf(pageLine, sizeof pageLine, "Frame ring wheel: Quick cast place %d: refused (%d edits already waiting)", PlaceNo(place), framevr::quickradial::link::MaxEdits); f.say(pageLine); }
            return;
        }
        quickChoose = -1; QueuePulses(pointer, 1, .5f);   // back on the Quick cast page, for the next place
        if (f.say) { std::snprintf(pageLine, sizeof pageLine, "Frame ring wheel: Quick cast place %d: %s (asked of the radial)", PlaceNo(place), what.c_str()); f.say(pageLine); }
    };
    if (rp == rw::RingPage::QuickKind) {   // review 2026-11-16: one kind's page: the kind, then every tier (carried or not, the others dim); trigger puts it in the place; center Back
        quick.Drop(); engineOpen = false;
        rw::PageIn pi;
        pi.nowMs = in.nowMs; pi.hit = in.hit; pi.x = hitX; pi.y = hitY; pi.trigger = triggerIn > .5f; pi.page = rp; pi.count = quickKindN;
        pageOut = pages.Step(pi, set);
        rw::ToStep(pageOut, step);
        if (pageOut.event == rw::PageEvent::Upstream && pageOut.cell >= 0) { f.hover = pageOut.cell; f.trigger = 1; edges.Wrote(CellWhy(pageOut.cell)); QueuePulses(pointer, 1, .2f); }   // a footer tab
        else if (pageOut.event == rw::PageEvent::Back) {   // the place's chooser again
            if (f.say) { std::snprintf(pageLine, sizeof pageLine, "Frame ring wheel: Quick cast kind page %s: Back (the chooser for place %d)", qr::FamilyName(quickKind), PlaceNo(place)); f.say(pageLine); }
            quickKind = 0; QueuePulses(pointer, 1, .2f);
        } else if (pageOut.event == rw::PageEvent::ChooseSkill && pageOut.shape >= 0 && pageOut.shape < quickKindN) {
            const int id = quickKindIds[size_t(pageOut.shape)];
            ask(id, -1, "put " + (qr::IsFamilyId(id) ? std::string(qr::FamilyName(id & 0xffff)) + " (the best one)" : qr::CodeName(qr::CodeOfExact(id)) + (quickKindDim[size_t(pageOut.shape)] ? " (exactly; none carried now)" : " (exactly)")) + " there");
        }
        return;
    }
    if (rp == rw::RingPage::QuickChoose) {   // the class's skills Quick cast casts lit, its potions and scrolls on the row: trigger puts one in the place; Back / Clear
        pages.Reset(); quick.Drop();
        const rw::Layout& cl = chooseLayout;
        step = engine.Step(in, set);
        if (step.event == rw::Event::Upstream && step.cell == ia::GlyphsCell) { quickChoose = -1; QueuePulses(pointer, 1, .2f); }   // Back: the Quick cast page
        else if (step.event == rw::Event::Upstream && step.cell == ia::MotionsCell) ask(-1, -1, "clear");   // Clear
        else if (step.event == rw::Event::Upstream && step.cell >= 0) { f.hover = step.cell; f.trigger = 1; edges.Wrote(CellWhy(step.cell)); }   // a footer tab
        else if (step.event == rw::Event::Pick && rw::EnginePicks(cl, step.picked)) {
            const auto& seat = cl.seats[size_t(step.picked)];
            const std::string name = rw::ItemSeat(seat.id) ? QuickKindName(seat.id & 0xffff) : SkillWords(s, seat.id, seat.owner);   // Mana (the kind, its page follows); Antidote: "Antidote Potion", no (best)
            if (size_t(step.picked) < chooseDim.size() && chooseDim[size_t(step.picked)]) {   // not one Quick cast casts: refused, the chooser stays
                QueuePulses(pointer, 3, .35f);
                if (f.say) { std::snprintf(pageLine, sizeof pageLine, "Frame ring wheel: Quick cast chooser refused %s: not one Quick cast casts", name.c_str()); f.say(pageLine); }
                engineOpen = false;
            } else if (int carried = 0; qr::IsFamilyId(seat.id) && (carried = KindPage(s, seat.id & 0xffff)) > 0) {   // review 2026-11-16: the best or an exact one
                QueuePulses(pointer, 1, .2f);
                if (f.say) { std::snprintf(pageLine, sizeof pageLine, "Frame ring wheel: Quick cast kind page %s for place %d (best, then %d exact tiers, %d carried)", name.c_str(), PlaceNo(place), carried, quickKindCarried); f.say(pageLine); }
            } else ask(seat.id, seat.owner, "put " + name + " there");   // a skill, or a kind with no tiers (Antidote, Thawing, Stamina): the family
        }
        return;
    }
    if (rp == rw::RingPage::GlyphChoose) {   // the three trees, the ring's glyph list lit (rw::GlyphChoices): trigger picks, the halves Back / Clear
        pages.Reset();   // back on a page: its lockout and let-go (PageEngine sees the change)
        const rw::Layout& cl = chooseLayout;
        step = engine.Step(in, set);
        // The Commander orders' row (Jeff's pick B, 2026-11-14; ring_wheel_pages.h): an order takes button presses as a
        // main-ring seat does, through the same QuickCast; a skill's seat keeps the engine's pick at any press length.
        rw::QuickIn q;
        FillQuick(q, f, s, in.nowMs); q.under = rw::OrderUnder(cl, step.under);
        if (ringGrab.Holding()) { q.under = {rw::Kind::Gap}; for (int h = 0; h < 2; ++h) { q.press[h] = -1; for (bool& d : q.down[h]) d = false; } }   // item 2: no pick or assign while moving
        quickOut = quick.Step(q, cl);
        const auto& e = quickOut;
        chooseHoldFrac = rw::OrderHoldFrac(e, q.holdMs, q.tapMs);   // the ring from the first frame of the press
        HoldStep(e, chooseHoldFrac, q, s, f.layout);
        const rw::OrderAct act = rw::OrderActOf(e, pointer);
        auto pickSeat = [&](int index) {   // the seat goes on the shape, as upstream's picker sends it
            const auto& seat = cl.seats[size_t(index)];
            const auto* k = SkillOf(s, seat.id, seat.owner);
            if (size_t(index) < chooseDim.size() && chooseDim[size_t(index)]) {   // not on the glyph list: refused, the chooser stays
                QueuePulses(pointer, 3, .35f);
                if (f.say) { std::snprintf(pageLine, sizeof pageLine, "Frame ring wheel: glyph chooser refused %s: not on the glyph list (it needs a target)", k ? tx::Clean(k->name).c_str() : "?"); f.say(pageLine); }
                engineOpen = false;
            } else { Pending p; p.glyph = true; p.g = {true, glyphHandNow, choose, seat.id, seat.owner}; Enqueue(p, s, f.say); choose = -1; QueuePulses(pointer, 1, .5f); }
        };
        if (step.event == rw::Event::Upstream && step.cell == ia::GlyphsCell) { choose = -1; QueuePulses(pointer, 1, .2f); }   // Back: the Glyphs page
        else if (step.event == rw::Event::Upstream && step.cell == ia::MotionsCell) {   // Clear glyph
            Pending p; p.glyph = true; p.g = {true, glyphHandNow, choose, -1, -1}; Enqueue(p, s, f.say); choose = -1; QueuePulses(pointer, 1, .4f);
        } else if (step.event == rw::Event::Upstream && step.cell >= 0) { f.hover = step.cell; f.trigger = 1; edges.Wrote(CellWhy(step.cell)); }   // a footer tab
        else if (step.event == rw::Event::Pick && rw::EnginePicks(cl, step.picked)) pickSeat(step.picked);   // a skill: at any press length
        else if (act == rw::OrderAct::Pick && rw::OrderSeat(cl, e.seat)) pickSeat(e.seat);   // an order: the pointing hand's trigger, a tap
        else if (act == rw::OrderAct::Assign && rw::OrderSeat(cl, e.seat)) {   // an order held to full: on that button, through upstream's BindSkill (its CanBind asks HasSummonArmy)
            const int id = cl.seats[size_t(e.seat)].id;
            { Pending p; p.bind = {true, rw::SlotOf(e.hand, e.slot), id, -1, e.slot, e.hand, f.layout}; Enqueue(p, s, f.say); }
            if (f.say) {
                const auto* was = e.replaced >= 0 ? SkillOf(s, e.replaced, -1) : nullptr;
                const std::string wasName = was ? tx::Clean(was->name) : std::string();
                std::snprintf(pageLine, sizeof pageLine, "Frame ring wheel: assign %s -> %s (was %s) held=%.0f ms, an order from the glyph chooser", rw::orders::Label(id),
                              rw::SlotName(framevr::controllers::Layout(f.layout), e.hand, e.slot).c_str(), rw::WasText(rw::HeldBy(e.replaced, glyphDrawNow, e.hand, e.slot), was ? wasName.c_str() : nullptr).c_str(), e.heldMs);
                f.say(pageLine);
            }
            QueuePulses(e.hand, 1, .6f);
        } else if (act == rw::OrderAct::NoChange) {   // let go between the Tap limit and Hold to assign: nothing
            QueuePulses(e.hand, 2, .25f);
            if (f.say) { std::snprintf(pageLine, sizeof pageLine, "Frame ring wheel: no change %s held=%.0f ms (an order, the glyph chooser)", rw::SlotName(framevr::controllers::Layout(f.layout), e.hand, e.slot).c_str(), e.heldMs); f.say(pageLine); }
        }
        return;
    }
    const int hand = f.wheelHand;
    pages.Reset();
    in.glyphClickSpent = rw::GlyphClickSpent(quickOut, pointerOwner, int(std::lround(labstore::Value(lab::RingWheelHoldMs))), int(std::lround(labstore::Value(lab::RingWheelTapMs))));   // #3
    step = engine.Step(in, set);
    if (step.event == rw::Event::Upstream && step.cell >= 0) { f.hover = step.cell; f.trigger = 1; edges.Wrote(CellWhy(step.cell)); }
    if (step.event == rw::Event::OpenQuick) OpenQuickCast(pointer, s, f, 0, "the trigger");   // 2026-11-16: the Quick cast third's click: its page
    if (const int t = rw::ThirdOf(step.under.kind); t != thirdSaid) {   // T036: each change of the pointed third (and its end), once
        if (f.say && (t >= 0 || thirdSaid >= 0)) {
            if (t >= 0) { std::snprintf(pageLine, sizeof pageLine, "Frame ring wheel: center %s pointed (grow %.0f%%, others %.0f%%)", rw::ThirdName[t], set.thirdGrowPct, labstore::Value(lab::RingWheelThirdDim)); f.say(pageLine); }
            else f.say("Frame ring wheel: center none pointed");
        }
        thirdSaid = t;
    }
    // R6Q: both hands' ten slots (upstream's rule, hand_controls.h): trigger, primary, secondary, grip + primary, grip + secondary.
    rw::QuickIn q;
    FillQuick(q, f, s, in.nowMs); q.under = step.under; q.thirds = set.thirds;   // T036: the thirds' pseudo seats
    {   // feedback 1 item 3: the badges' letters; job B: a hand that reads Unknown (asleep) keeps its last real layout, never 1 / 2
        const bool automatic = int(vrsettings::Read().values[framevr::settings::ControllerLabels]) == int(framevr::controllers::Choice::Automatic);
        const int now = int(letterHold.Step(f.wheelHand == 1 ? 1 : 0, framevr::controllers::Layout(f.layout), automatic));
        if (now != f.layout && f.say && now != letterHeldSaid) { char line[120]; std::snprintf(line, sizeof line, "Frame ring wheel: badge letters kept as %s (the %s controller reads %s now)", framevr::controllers::Name(framevr::controllers::Layout(now)), f.wheelHand == 1 ? "left" : "right", framevr::controllers::Name(framevr::controllers::Layout(f.layout))); f.say(line); }
        letterHeldSaid = now != f.layout ? now : -1;
        ctrlLayoutNow = now;
    }
    if (ringGrab.Holding()) { q.under = {}; for (int h = 0; h < 2; ++h) { q.press[h] = -1; for (bool& d : q.down[h]) d = false; } }   // item 2: no cast or assign while moving
    quickOut = quick.Step(q, layout);
    const auto& e = quickOut;
    HoldStep(e, e.holdFrac, q, s, f.layout);   // the hold, from the tap limit
    auto name = [&](int seat) { const auto& st = layout.seats[size_t(seat)]; const auto* k = SkillOf(s, st.id, st.owner); return k ? tx::Clean(k->name) : std::string("?"); };
    auto slotName = [&](int h, int k) { return rw::SlotName(framevr::controllers::Layout(f.layout), h, k); };
    auto wasText = [&](const rw::QuickOut& o) {   // what the button held: its game skill, else Draw glyph (audit bug 1), else a Lab menu (2026-11-16), else none
        const auto* was = o.replaced >= 0 ? SkillOf(s, o.replaced, -1) : nullptr;
        const std::string wasName = was ? tx::Clean(was->name) : std::string();
        if (rw::HeldBy(o.replaced, glyphDrawNow, o.hand, o.slot) == rw::Held::None) if (const char* lab = labbuttons::OwnerOf(rw::SlotOf(o.hand, o.slot))) return std::string(lab);
        return rw::WasText(rw::HeldBy(o.replaced, glyphDrawNow, o.hand, o.slot), was ? wasName.c_str() : nullptr);
    };
    char line[260];
    switch (e.event) {
    case rw::Quick::QuickCast: {   // close through upstream's center, then cast through upstream's glyph press (OnRingCast)
        const auto& st = layout.seats[size_t(e.seat)];
        if (const int k = EntryOf(st.id, st.owner); k == 1 && !entries[1].dim) { OpenQuickCast(e.hand, s, f, e.heldMs, "its shelf entry"); break; }   // T035: a tap opens its page
        if (const int k = EntryOf(st.id, st.owner); k >= 0) {   // headset fixes 2026-11-15: another Lab's entry casts nothing; the wheel stays open
            QueuePulses(e.hand, 2, .25f);
            if (f.say) { std::snprintf(line, sizeof line, "Frame ring wheel: tap on %s: nothing to cast (hold a button on it to put it there) held=%.0f ms", entrySkills[size_t(k)].name, e.heldMs); f.say(line); }
            break;
        }
        if (const int t = st.owner == -1 ? rw::MenuOfEntry(st.id) : -1; t >= 0) {   // T036: a sub-menu's shelf entry: a tap opens it
            wantMenu = {t, e.hand, in.nowMs}; fireGate.Ask(rw::NowMs()); QueuePulses(e.hand, 1, .2f);
            if (f.say) { std::snprintf(line, sizeof line, "Frame ring wheel: sub-menu %s asked (its shelf entry, held=%.0f ms)", rw::ThirdName[t], e.heldMs); f.say(line); }
            break;
        }
        pendingCast = {true, st.id, st.owner, e.hand, e.slot, f.layout, e.heldMs, GetTickCount64()};
        f.hover = CenterCell; f.trigger = 1; edges.Wrote("quick-cast");
        break;
    }
    case rw::Quick::Close:
        f.hover = CenterCell; f.trigger = 1; edges.Wrote("cancel-off-wheel");   // logged once, as closed reason=cancel-off-wheel (review-bug9 N2)
        break;
    case rw::Quick::Assign: {
        const auto& st = layout.seats[size_t(e.seat)];
        if (const int t = st.owner == -1 ? rw::MenuOfEntry(st.id) : -1; t >= 0) { MenuBind(t, e, f.layout, s, f.say); break; }   // T036: a sub-menu's shelf entry
        {
            Pending p; p.bind = {true, rw::SlotOf(e.hand, e.slot), st.id, st.owner, e.slot, e.hand, f.layout};
            if (const int k = EntryOf(st.id, st.owner); k >= 0)   // another Lab's menu (Army X menu, Quick cast): bug 4, the skill it clears moves
                Displaced(p.bind, s, labbuttons::SlotOfLab(k == 0 ? lab::ArmyXButton : lab::QuickRadialButton), entrySkills[size_t(k)].name);
            Enqueue(p, s, f.say);
        }
        if (f.say) {
            std::snprintf(line, sizeof line, "Frame ring wheel: assign %s -> %s (was %s) held=%.0f ms", name(e.seat).c_str(), slotName(e.hand, e.slot).c_str(), wasText(e).c_str(), e.heldMs);
            f.say(line);
        }
        QueuePulses(e.hand, 1, .6f);
        break;
    }
    case rw::Quick::GlyphTap:   // #3: a tap opens the Glyphs page (the pointing hand's trigger already clicked, through the engine)
        if (rw::GlyphTapClicks(e.hand, e.slot, pointerOwner)) { f.hover = ia::GlyphsCell; f.trigger = 1; edges.Wrote(CellWhy(ia::GlyphsCell)); }
        break;
    case rw::Quick::GlyphAssign: {   // #3: upstream's Draw glyph on this button (held draws, release casts: transport)
        { Pending p; p.bind = {true, rw::SlotOf(e.hand, e.slot), -1, -1, e.slot, e.hand, f.layout, true}; Enqueue(p, s, f.say); }
        if (f.say) {
            std::snprintf(line, sizeof line, "Frame ring wheel: assign Draw glyph -> %s (was %s) held=%.0f ms", slotName(e.hand, e.slot).c_str(), wasText(e).c_str(), e.heldMs);
            f.say(line);
        }
        QueuePulses(e.hand, 1, .6f);
        break;
    }
    case rw::Quick::MenuTap: {   // T036: a tap on the Gestures or Quick cast third opens it (the pointing hand's trigger already clicked, through the engine)
        const int t = rw::SeatThird(e.seat);
        if (!rw::GlyphTapClicks(e.hand, e.slot, pointerOwner)) break;
        if (t == rw::ThirdGestures) { f.hover = ia::MotionsCell; f.trigger = 1; edges.Wrote(CellWhy(ia::MotionsCell)); }
        else if (t == rw::ThirdQuick) OpenQuickCast(e.hand, s, f, e.heldMs, "a tap");   // 2026-11-16: its page
        break;
    }
    case rw::Quick::MenuAssign:   // T036: that sub-menu on this button
        MenuBind(rw::SeatThird(e.seat), e, f.layout, s, f.say);
        break;
    case rw::Quick::NoChange:
        QueuePulses(e.hand, 2, .25f);   // two short light pulses (critique-vr-ux Q2)
        if (f.say) { std::snprintf(line, sizeof line, "Frame ring wheel: no change %s held=%.0f ms", slotName(e.hand, e.slot).c_str(), e.heldMs); f.say(line); }
        break;
    default: break;
    }
}

// T036 FilterRingHands: transport Input, before HandControls steps (one frame ahead of this frame's Input seam). A sub-menu's
// button opens that hand's wheel there (its click set for this frame, HandControls' own toggle; with a wheel open already, the
// open wheel goes there) and the press is spent (no hold to assign starts on it). Headset 2026-11-16 bug 1: with the ring's main
// page open and a seat or a center third under the laser (last frame's step), the press is the ring's instead, as any button's: a
// tap casts that seat, a hold of Hold to assign puts it on this button (rw::MenuPressOpens). The sticks are the game's (the Quick
// cast page and its stick select went, 2026-11-16). The Off edge's drain: nothing changed, the state cleared.
void Hands(lab::RingHandsFrame f) {
    if (!labstore::TabOn(lab::TabRingWheel) || !f.hands) { wantMenu = {}; for (auto& h : menuHeld) for (bool& b : h) b = false; return; }
    auto& hi = *static_cast<framevr::skills::HandInput*>(f.hands);
    bool down[2][3], grip[2];
    for (int h = 0; h < 2; ++h) { down[h][0] = hi.trigger[h] > .55f; down[h][1] = hi.face[h][0]; down[h][2] = hi.face[h][1]; grip[h] = hi.grip[h] > .6f; }   // HandControls' own grip rule
    if (f.wheelHand >= 0) for (int h = 0; h < 2; ++h) down[h][0] = false;   // review F1: an open wheel's trigger is its click
    const rw::MenuPress p = rw::MenuPressed(buttonsNow, down, grip, menuHeld);   // a down edge: one line a press
    if (p.third < 0 || !hi.active || hi.occupied) return;
    char line[220];
    if (!rw::MenuPressOpens(f.wheelHand >= 0, ringActive && ringPage == rw::RingPage::Main, step.under.kind)) {   // bug 1: the ring's press
        if (f.say) {
            std::snprintf(line, sizeof line, "Frame ring wheel: sub-menu %s button (%s) on a %s: the ring's press (a tap casts, a hold puts it on this button)", rw::ThirdName[p.third],
                          SlotWords(buttonsNow.slot[size_t(p.third)], ctrlLayoutNow).c_str(), step.under.kind == rw::Kind::Seat ? "seat" : "center third");
            f.say(line);
        }
        return;
    }
    if (f.wheelHand < 0) hi.click[p.hand] = true;   // that hand's wheel opens, as its stick click
    for (int b = 0; b < 3; ++b) if (down[p.hand][b]) quickHeld[p.hand][b] = true;   // the ring's hold to assign never sees this press
    wantMenu = {p.third, p.hand, rw::NowMs()}; fireGate.Ask(rw::NowMs());
    QueuePulses(p.hand, 1, .3f);
    if (f.say) {
        std::snprintf(line, sizeof line, "Frame ring wheel: sub-menu %s button (%s) -> %s", rw::ThirdName[p.third], SlotWords(buttonsNow.slot[size_t(p.third)], ctrlLayoutNow).c_str(),
                      f.wheelHand < 0 ? "opens that hand's wheel there" : "the open wheel goes there");
        f.say(line);
    }
}

void Request(lab::WheelRequestFrame f) {
    if (f.engineIn) {
        auto& in = *static_cast<ia::Input*>(f.engineIn);
        hold.Restore(in);   // the hold is the engine's alone
        // Audit bug 3: upstream's own page, a trigger press on a footer tab that closes the wheel: the hover and trigger its engine just read.
        if (const char* tab = footerClick.Step(labstore::TabOn(lab::TabRingWheel) && in.wheelHand >= 0 && !ringActive, pageWas, in.hover, in.trigger, in.snapshot.developer)) edges.Wrote(tab);
    }
    if (!labstore::TabOn(lab::TabRingWheel) || !f.request || !f.snapshot) { sendQueue.Clear(); bindWait.Clear(); glyphWait.Clear(); return; }
    const auto& s = *static_cast<const ia::Snapshot*>(f.snapshot);
    const double nowMs = rw::NowMs();
    // Review-r6 X4: every ring request is answered once: its reply, or refused when none came in 1.5 s (upstream's limit); the
    // next one goes only after that (transport's queue drops a request silently while another waits). A refusal says so in
    // the log, on the bind ("Refused") and with three short pulses on the hand that pressed.
    auto bindDone = [&](rw::ReplyEnd e) {
        if (e == rw::ReplyEnd::None) return;
        if (rw::IsRefusal(e)) { engine.Refused(); QueuePulses(bindSent.hand, 3, .35f); }
        if (e == rw::ReplyEnd::Applied && bindSent.menu >= 0 && bindSent.menu < rw::ThirdCount) {   // T036: the button is that sub-menu's now
            const rw::MenuButtons b = rw::Assigned(ReadButtons(), bindSent.menu, bindSent.index);
            labbuttons::Claim(bindSent.index, MenuKey(bindSent.menu), f.say);   // review S1: one Lab per button (Army X menu, Quick cast radial)
            for (int t = 0; t < rw::ThirdCount; ++t) labstore::Set(MenuKey(t), float(b.slot[size_t(t)] + 1));
            keptHoldUntil = GetTickCount64() + 2000;   // the snapshot catches up with the clear
            if (f.say) {
                char line[200];
                std::snprintf(line, sizeof line, "Frame ring wheel: sub-menu %s on %s (its button: it opens the wheel there)", rw::MenuEntryName[bindSent.menu], SlotWords(bindSent.index, bindSent.layout).c_str());
                f.say(line);
            }
        }
        if (f.say) {
            const auto* k = bindSent.skill >= 0 ? SkillOf(s, bindSent.skill, bindSent.owner) : nullptr;
            char line[240];
            std::snprintf(line, sizeof line, "Frame ring wheel: bind %s -> %s (%s)", bindSent.menu >= 0 ? rw::MenuEntryName[bindSent.menu] : bindSent.draw ? "Draw glyph" : bindSent.skill < 0 ? "(clear)" : k ? tx::Clean(k->name).c_str() : "?",
                          rw::SlotName(framevr::controllers::Layout(bindSent.layout), bindSent.hand, bindSent.slot).c_str(), rw::ReplyEndName(e));
            f.say(line);
        }
        if (e == rw::ReplyEnd::Applied && rw::SkillTakesButton(bindSent.skill, bindSent.menu, bindSent.draw))
            labbuttons::LetGo(bindSent.index, f.say);   // review N4: a skill put on a Lab menu's button: that menu lets go of it (key 0)
        if (e == rw::ReplyEnd::Applied) MoveDisplaced(bindSent, s, f.say);   // 2026-11-16 bug 4: a Lab menu's clear never drops the skill silently
    };
    auto glyphDone = [&](rw::ReplyEnd e) {
        if (e == rw::ReplyEnd::None) return;
        if (rw::IsRefusal(e)) QueuePulses(glyphSent.hand, 3, .35f);
        if (f.say) {
            const auto* k = glyphSent.skill >= 0 ? SkillOf(s, glyphSent.skill, glyphSent.owner) : nullptr;
            char line[240];
            std::snprintf(line, sizeof line, "Frame ring wheel: glyph %s, the %s hand's -> %s (%s)", framevr::glyphs::Name(framevr::glyphs::Shape(glyphSent.shape)),
                          glyphSent.hand ? "left" : "right", glyphSent.skill < 0 ? "(clear)" : k ? tx::Clean(k->name).c_str() : "?", rw::ReplyEndName(e));
            f.say(line);
        }
    };
    if (f.reply) {
        const auto& reply = *static_cast<const ia::Reply*>(f.reply);
        bindDone(bindWait.Reply(reply.sequence, reply.result));
        // a glyph is applied only on Applied (upstream's picker: Dispatched is not a save)
        glyphDone(glyphWait.Reply(reply.sequence, reply.result == ia::Result::Dispatched ? ia::Result::Refused : reply.result));
    }
    bindDone(bindWait.Step(nowMs));
    glyphDone(glyphWait.Step(nowMs));
    auto& r = *static_cast<ia::Request*>(f.request);
    if (r.action != ia::Action::None) return;
    if (!rw::CanSend(bindWait, glyphWait)) return;   // X4: the next request waits for this one's answer (else transport drops it)
    Pending p;
    if (!sendQueue.Pop(p)) return;   // the oldest made first
    if (!p.glyph && p.bind.index >= 0 && p.bind.index < framevr::skills::Count) {
        r = p.bind.draw ? rw::GlyphDrawRequest(s, p.bind.index, ++bindSequence, f.session, f.now) : rw::BindRequest(s, p.bind.index, p.bind.skill, p.bind.owner, ++bindSequence, f.session, f.now);
        bindSent = p.bind;
        if (r.action == ia::Action::None) bindDone(rw::ReplyEnd::Refused);   // #3: not sent (stale or not editable): refused out loud
        else bindWait.Sent(r.sequence, nowMs);
    } else if (p.glyph) {   // R7: the glyph chooser's skill, as upstream's picker sends it (transport saves it, "Frame glyph:")
        r = rw::GlyphRequest(p.g.hand, p.g.shape, p.g.skill, p.g.owner, ++bindSequence, f.session, f.now);
        if (r.action == ia::Action::BindGlyph) glyphWait.Sent(r.sequence, nowMs);
        glyphSent = p.g;
    }
}

void Layer(lab::WheelLayerFrame f) {
    if (f.say) sayKept = f.say;
    ringShown = false;
    const bool on = labstore::TabOn(lab::TabRingWheel);
    if (on != told) {
        told = on;
        if (f.say) {   // the On line says the Lab's state at that moment, so a later log read knows what was tested
            if (!on) f.say(rw::OffLine());
            else {
                static constexpr const char* anims[5] = {"Off", "Floating icons", "Effect on hover", "All effects", "Custom"};
                const rw::Placement p = Read();
                rw::LabState st;
                st.orbs = LayoutNow() == rw::TreeLayout::Orbs; st.worldLock = p.worldLock;
                st.distanceCm = p.distanceM * 100; st.widthCm = p.widthM * 100; st.belowDeg = p.belowDeg;
                st.holdMs = int(std::lround(labstore::Value(lab::RingWheelHoldMs))); st.tapMs = int(std::lround(labstore::Value(lab::RingWheelTapMs)));
                st.renderPct = RenderPct(); st.animation = anims[std::clamp(int(std::lround(labstore::Value(lab::RingWheelAnim))), 0, 4)];
                f.say(rw::OnLine(st).c_str());
            }
        }
    }
    if (!on && edges.Open()) {   // bug 9: the Off edge with the wheel open (upstream's wheel takes over)
        if (f.say) { char line[160]; std::snprintf(line, sizeof line, "Frame ring wheel: closed reason=lab-off after %.1f s, page %s", double(GetTickCount64() - openedAt) / 1000, UpPageName(pageWas)); f.say(line); }
        edges.Off();
    }
    if (!on) { hold.Reset(); focusBlip.Focused(); focusWhy = nullptr; shutBeforeClear = false; Retire(f.say, "Off edge"); Reap(false, f.say); ringActive = false; engineOpen = false; seatsKey = 0; built.reset(); choose = -1; ringPageSeen = false; layoutSaid = -1; otherTreesSaid = -1; wantMenu = {}; thirdSaid = -2; layersSaid = false; return; }   // layoutSaid: the next On says tree layout= again
    Reap(false, f.say);
    SyncPreset(f.say);
    if (f.output) {   // upstream's glyph and gesture state as its Step left it (the pages' data, plan E6)
        const auto& o = *static_cast<const ia::Output*>(f.output);
        glyphHandNow = o.glyphHand; glyphSets = o.glyphSkills; motionsOffNow = o.motionsOff; glyphDrawNow = o.glyphDraw;
    }
    Setup(f);
    if (!gpu) return;
    gpu->compose.Pump();
    if (!f.open || !placed || !f.palette) { treeSaid = false; glyphSaid = false; policy.Want(rw::Scene{}); pointedFx.Reset(); return; }   // T025: the next open says its tree icons line
    auto& q = *static_cast<XrCompositionLayerQuad*>(f.palette);
    if (!ringActive) {   // upstream's picture at the Lab's pose and width (R3)
        if (q.subImage.swapchain != XR_NULL_HANDLE) q.size = {placedWidth, placedWidth};
        return;
    }
    const auto& s = *static_cast<const ia::Snapshot*>(f.snapshot);
    const Quad quad = QuadOf(placedPose, placedWidth, s.developer);
    // The glyph chooser draws its own seats (the orders' row with a summon army; without one they are the main ring's exactly).
    const bool anyChooser = ringPage == rw::RingPage::GlyphChoose || ringPage == rw::RingPage::QuickChoose;
    const bool choosing = anyChooser && chooseQuick == (ringPage == rw::RingPage::QuickChoose) && chooseKey == seatsKey && !chooseStale && chooseLayout.count > 0;
    const rw::Layout& shown = choosing ? chooseLayout : layout;
    const auto& shownElements = choosing ? chooseElements : elements; const auto& shownCrawl = choosing ? chooseCrawl : crawlIdx; const auto& shownRest = choosing ? chooseRest : restGlows;
    rw::Scene scene;
    scene.open = true; scene.layout = &shown; scene.step = step; scene.tMs = double(f.nowMs);
    scene.anim = ReadAnim();
    scene.layers = ReadLayers();   // T036: Lab > Experiment: Ring wheel > Layers
    if (const ULONGLONG t = GetTickCount64(); f.say && (!layersSaid || !(scene.layers == layersTold)) && t - layersSaidAt > 1000) {   // the first frame and each change, at most once a second
        layersTold = scene.layers; layersSaid = true; layersSaidAt = t;
        char line[200];
        std::snprintf(line, sizeof line, "Frame ring wheel: layers icon=%s color ring=%s element frame=%s float=%s animation=%s (clean icons keyed %d, cleanup %d px)",
                      scene.layers.icon ? "on" : "off", scene.layers.ring ? "on" : "off", scene.layers.frame ? "on" : "off", scene.anim.floats ? "on" : "off",
                      scene.layers.fx ? "on" : "off", built->icons.keyed, built->icons.despeckled);
        f.say(line);
    }
    scene.thirds = ringPage == rw::RingPage::Main;   // T036: the center in thirds; the chooser keeps its halves
    scene.thirdGrowPct = labstore::Value(lab::RingWheelThirdGrow); scene.thirdDimPct = labstore::Value(lab::RingWheelThirdDim);
    scene.quickWord = rw::QuickThirdWord(labstore::On(lab::QuickRadialOn), framevr::quickradial::ClassKnown(s.skillClass));   // 2026-11-16, review N1: Off, or not this class
    // feedback 1 item 4: upstream's footer tabs from the hit test's numbers (the same developer flag as the quad, review R6 X2)
    scene.footer = {true, rw::UpstreamPage(ringPage), developerNow, labstore::On(lab::RingWheelFooterFull), true};
    scene.wheelAlpha = fightNow.wheel; scene.fight = fightNow.dim;
    if (shownElements.size() == size_t(shown.count) && shownCrawl.size() == size_t(shown.count)) { scene.elements = shownElements.data(); scene.crawlIndex = shownCrawl.data(); scene.crawlCount = crawlCount; }
    if (shownRest.size() == size_t(shown.count)) scene.rest = shownRest.data();
    scene.fxTex = gpu->compose.HasAtlas(rw::AtlasKind::FxTex);   // stage 3a: the game effect textures are in
    {   // the pointed icon's effects run from when it was pointed
        const int pointedNow = step.under.kind == rw::Kind::Seat ? step.under.index : -1;
        if (pointedNow != pointedWas) { pointedWas = pointedNow; pointedSince = double(f.nowMs); }
        scene.pointedSinceMs = pointedSince;
        const auto fx = pointedFx.Step(double(f.nowMs), pointedNow);   // R8 match stage 2: one effect at a time (spec 4a)
        scene.fxSeat = fx.seat; scene.fxAgeMs = fx.ageMs; scene.fxAlpha = fx.alpha;
        if (fx.seat >= 0 && fx.seat < shown.count && shownRest.size() == size_t(shown.count) && scene.anim.hoverFx && !scene.anim.still && f.say) {
            const int id = shown.seats[size_t(fx.seat)].id;   // the chain's stage, once per skill a session (spec 7.3 S7)
            if (std::find(fxSaid.begin(), fxSaid.end(), id) == fxSaid.end() && fxSaid.size() < 4096) {
                fxSaid.push_back(id);
                char line[112];
                const rw::RestKind hk = rw::HoverKindOf(shownRest[size_t(fx.seat)]);
                const int stage = rw::FxStageOf(hk, scene.fxTex);
                if (hk == rw::RestKind::None) std::snprintf(line, sizeof line, "Frame ring wheel: fx %d stage=none (no drawn default: the hover ring)", id);
                else std::snprintf(line, sizeof line, "Frame ring wheel: fx %d stage=%d (%s default %s)", id, stage, stage == 3 ? "game-texture" : "element", rw::RestKindName[uint32_t(hk)]);
                f.say(line);
            }
        }
    }
    scene.hasMotions = framevr::spellmotion::HasMotions(s.skillClass);
    scene.atlasPct = built->icons.scalePct; scene.cells = built->icons.cellOf.data(); scene.metrics = &built->metrics;
    scene.initials = initialPtr.data();
    if (scene.thirds && built->icons.cellOf.size() == size_t(request.n))   // T036 (2026-11-16): the thirds' pictures' cells (3 scans of at most 81 ids, open main page only)
        for (int t = 0; t < rw::ThirdCount; ++t) scene.thirdCell[size_t(t)] = int16_t(rw::CellOfSkill(request.ids.data(), built->icons.cellOf.data(), request.n, rw::menuart::ArtId(t)));
    {   // T025 Tree icon: the mode, the size and each tree position's painted picture (an atlas cell; -1 keeps the drawn glyph). Off (Drawn): nothing looked up.
        const rw::treeart::Mode tm = TreeMode(); const int tp = TreeSizePct();
        scene.treeMode = int(tm); scene.treeSizePct = tp;
        int found = 0;
        if (tm != rw::treeart::Mode::Drawn && tp > 0 && built->icons.cellOf.size() == size_t(request.n))   // 3 scans of at most 81 ids, a frame the layout or the atlas changed would do; cheap
            for (int t = 0; t < rw::Trees; ++t) {
                const int slot = rw::treeart::Slot(s.skillClass, t);
                scene.treeCell[size_t(t)] = slot >= 0 && rw::treeart::Painted(slot) ? int16_t(rw::CellOfSkill(request.ids.data(), built->icons.cellOf.data(), request.n, rw::treeart::ArtId(slot))) : int16_t(-1);
                found += scene.treeCell[size_t(t)] >= 0;
            }
        const int key = int(tm) + 4 * tp + 4096 * (s.skillClass + 2) + 65536 * found;
        if (f.say && (!treeSaid || key != treeSaidKey)) {   // once a page open, and when a row, the class or the atlas changes it
            treeSaid = true; treeSaidKey = key;
            char line[200];
            std::snprintf(line, sizeof line, "Frame ring wheel: tree icons mode=%s size=%d%% painted=%d of %d squares (this class %d of %d in the atlas%s)", rw::treeart::ModeName[int(tm)], tp,
                          rw::treeart::PaintedCount(), rw::treeart::Count, found, rw::Trees, LayoutNow() == rw::TreeLayout::Orbs ? "; Tree orbs keep the glyphs" : "");
            f.say(line);
        }
    }
    {   // Glyph icon (Center page): the painted shapes' and sigils' atlas cells, read when the layout or the atlas changed; nothing while Drawn
        const rw::glyphart::Mode gm = GlyphMode(); const int gp = GlyphSizePct();
        scene.glyph.mode = int(gm); scene.glyph.sizePct = gp;
        int found = 0;
        if (gm != rw::glyphart::Mode::Drawn && built->icons.cellOf.size() == size_t(request.n))   // 12 scans of at most 81 ids
            for (int slot = 0; slot < rw::glyphart::Count; ++slot) {
                scene.glyph.cell[size_t(slot)] = rw::glyphart::Painted(slot) ? int16_t(rw::CellOfSkill(request.ids.data(), built->icons.cellOf.data(), request.n, rw::glyphart::ArtId(slot))) : int16_t(-1);
                found += scene.glyph.cell[size_t(slot)] >= 0;
            }
        const int key = int(gm) + 4 * gp + 4096 * found;
        if (f.say && (!glyphSaid || key != glyphSaidKey)) {   // once a page open, and when a row or the atlas changes it
            glyphSaid = true; glyphSaidKey = key;
            char line[200];
            std::snprintf(line, sizeof line, "Frame ring wheel: glyph icons mode=%s size=%d%% painted=%d of %d squares (%d in the atlas; the rest keep the drawn shape or sigil)", rw::glyphart::ModeName[int(gm)], gp,
                          rw::glyphart::PaintedCount(), rw::glyphart::Count, found);
            f.say(line);
        }
    }
    if (choosing) {   // each chooser seat's icon cell by its id (an order's: the one header's), read once per atlas
        if (chooseCellsOf != built.get()) {
            chooseCellsOf = built.get();
            chooseCells.assign(size_t(shown.count), int16_t(-1));
            if (built->icons.cellOf.size() == size_t(request.n))
                for (int i = 0; i < shown.count; ++i) {
                    const int id = shown.seats[size_t(i)].id;
                    chooseCells[size_t(i)] = int16_t(rw::CellOfSkill(request.ids.data(), built->icons.cellOf.data(), request.n, rw::OrderSeat(shown, i) ? rw::orders::IconSkill(id) : qr::PictureId(id)));
                }
        }
        scene.cells = chooseCells.data(); scene.initials = chooseInitialPtr.data();
    }
    std::array<const char*, rw::Trees> names{};
    for (int t = 0; t < rw::Trees; ++t) names[size_t(t)] = framevr::skillcategories::Name(seatClass, rw::PageAt(seatClass, t) + 1);
    scene.treeNames = names; scene.revision = revision + (uint32_t(ringPage) << 28);
    scene.skillClass = seatClass;   // R7c: the tree marks' glyphs (the class the seats were laid out for)
    const bool onPage = ringPage == rw::RingPage::Glyphs || ringPage == rw::RingPage::Gestures || rw::QuickRing(ringPage);
    if (!onPage && !anyChooser) {   // feedback 1 item 3: main page only (FS7)
        scene.badges = Badges(s, double(f.nowMs), layout, seatsKey, f.say);
        scene.drawBadge = rw::DrawBadgeFor(glyphDrawNow, wheelHandNow, framevr::controllers::Layout(ctrlLayoutNow), BadgeLookNow().sizePx);   // #3: upstream's own draw slot
        const int sizePx = BadgeLookNow().sizePx;   // T036: each sub-menu's button on its third's mark (the Size row applies, Angle and Depth do not)
        const int quickSlot = scene.quickWord ? -1 : labbuttons::SlotOfLab(lab::QuickRadialButton);   // 2026-11-16: Quick cast's third shows the radial's button
        for (int t = 0; t < rw::ThirdCount && sizePx > 0; ++t) {
            const int slot = t == rw::ThirdQuick ? (quickSlot >= 0 && s.skills[size_t(quickSlot)].id < 0 ? quickSlot : -1) : buttonsNow.slot[size_t(t)];
            for (int h = 0; h < 2; ++h) for (int k = 0; k < rw::Slots; ++k)
                if (slot >= 0 && rw::SlotOf(h, k) == slot)
                    scene.menuBadge[size_t(t)] = {true, rw::BadgeKindOf(k), rw::BadgeLetter(framevr::controllers::Layout(ctrlLayoutNow), h, k), sizePx};
        }
        if (mainMark.size() == size_t(layout.count)) scene.seatMark = mainMark.data();   // T036: the sub-menus' shelf entries
    }
    auto cellOf = [&](int id) -> int {
        return built->icons.cellOf.size() == size_t(request.n) ? rw::CellOfSkill(request.ids.data(), built->icons.cellOf.data(), request.n, qr::PictureId(id)) : -1;   // item 6: the page's cards too (a kind: its picture's)
    };
    if (onPage) {   // the page's picture, refilled only when upstream's data under it changed (never per frame)
        uint64_t h = 1469598103934665603ull;
        auto mix = [&](int64_t v) { h ^= uint64_t(v); h *= 1099511628211ull; };
        mix(int(ringPage)); mix(glyphHandNow); mix(motionsOffNow); mix(s.skillClass); mix(revision);
        for (const auto& hand : glyphSets) for (int k : hand) mix(k);
        framevr::quickradial::link::View qv;
        rw::QuickRingState qs = rw::QuickRingState::Saved;
        if (ringPage == rw::RingPage::Quick) { qs = QuickState(s, qv); mix(int64_t(qv.rev)); mix(qv.n); mix(int(qs)); mix(int64_t(s.session)); mix(qv.fixed); mix(quickMoveFrom); mix(quickMovePick); }   // 2026-11-16: the radial's ring, as published (reviews S3 / S4: whose, read or not); its layout and a Move (2026-11-17)
        if (ringPage == rw::RingPage::QuickKind) { mix(quickKind); mix(quickChoose); mix(quickKindN); for (int i = 0; i < quickKindN; ++i) { mix(quickKindIds[size_t(i)]); mix(quickKindDim[size_t(i)]); } }
        if (h != viewKey) {
            viewKey = h; ++viewRevision;
            auto nameOf = [&](int id) { const auto* k = SkillOf(s, id, -1); return k ? tx::Clean(k->name) : std::string(framevr::spellmotion::SpellName(id)); };
            auto quickName = [&](int id) {
                if (qr::IsFamilyId(id)) return QuickKindName(id & 0xffff);   // a kind page's "Healing"; Antidote: its potion's name
                if (qr::IsExactId(id)) return qr::CodeName(qr::CodeOfExact(id));
                return SkillWords(s, id, -1);
            };
            if (ringPage == rw::RingPage::Quick) { rw::FillQuick(view, qv.ids.data(), qv.n, quickName, cellOf, qv.fixed); rw::MarkQuick(view, qs); if (qs != rw::QuickRingState::Reading) rw::MarkQuickMove(view, quickMoveFrom, quickMovePick); }
            else if (ringPage == rw::RingPage::QuickKind) rw::FillQuickKind(view, quickKindIds.data(), quickKindN, quickChoose, qr::FamilyName(quickKind), quickName, cellOf, [&](int i) { return quickKindDim[size_t(i)]; });
            else if (ringPage == rw::RingPage::Glyphs) rw::FillGlyphs(view, glyphSets, glyphHandNow, nameOf, cellOf);
            else rw::FillGestures(view, s.skillClass, motionsOffNow, nameOf, cellOf, [&](int id) { return SkillOf(s, id, -1) != nullptr; });
        }
        scene.revision += viewRevision * 7919u;
        if (step.blurb >= 0 && step.blurb < view.count) {
            const uint64_t key = (1ull << 63) | (uint64_t(ringPage) << 56) | (uint64_t(step.blurb) << 40) | viewRevision;
            const size_t i = size_t(step.blurb);
            if (key != blurbKey) { blurbKey = key; blurbs.Get(built->metrics, key, {view.name[i], "", view.line[i], view.help[i]}); }
            scene.blurb = &blurbs.Get(built->metrics, key, {});
        }
    } else {
        const uint64_t ck = (uint64_t(wheelHandNow) << 40) | (uint64_t(uint32_t(motionsOffNow)) << 8) | uint64_t(uint8_t(s.skillClass));
        if (ck != countKey) { countKey = ck; topCount = rw::GlyphsCount(wheelHandNow == 1); bottomCount = rw::GesturesCount(s.skillClass, motionsOffNow); ++revision; }
        scene.topCount = topCount.c_str(); scene.bottomCount = bottomCount.c_str();
        if (anyChooser) {
            scene.hasMotions = true; scene.chooseTitle = chooseTitle.c_str(); scene.clearLine = clearLine.c_str(); scene.seatDim = chooseDim.size() == size_t(shown.count) ? chooseDim.data() : nullptr;
            scene.topCount = scene.bottomCount = nullptr;
            if (ringPage == rw::RingPage::QuickChoose) { scene.chooseLead = "Choose for "; scene.chooseBack = "To Quick cast"; }   // 2026-11-16
        } else if (mainDim.size() == size_t(shown.count)) scene.seatDim = mainDim.data();   // headset fixes 2026-11-15: the entry faint with no army
    }
    const float holdFrac = ringPage == rw::RingPage::QuickChoose ? 0.f : ringPage == rw::RingPage::GlyphChoose ? (choosing ? chooseHoldFrac : 0.f) : quickOut.holdFrac;   // an order's ring: from the first frame
    if (!onPage && quickOut.holding >= 0 && holdFrac > 0) {   // a button held on a skill past the tap limit, or on an order (L2, L3)
        scene.holdFrac = holdFrac; scene.holdSeat = quickOut.holding;
        scene.holdSlot = holdSlotText.c_str();
        const rw::HoldLines words = rw::HoldLinesFor(quickOut, holdBound, holdStatusText.c_str());   // #3 (designer)
        scene.holdTop = words.top; scene.holdStatus = words.line; scene.holdGreen = words.green;
        if (choosing && rw::OrderSeat(shown, quickOut.holding)) scene.holdName = rw::orders::Short(shown.seats[size_t(quickOut.holding)].id);   // designer gate D1: which of the seven
    }
    if (!onPage && step.blurb >= 0 && step.blurb < shown.count) {
        const auto& seat = shown.seats[size_t(step.blurb)];
        const bool chooser = anyChooser, quickChooser = ringPage == rw::RingPage::QuickChoose;
        if (quickChooser && rw::ItemSeat(seat.id)) {   // 2026-11-16: Quick cast's potions and scrolls (no catalog skill): their own words
            const uint64_t key = (1ull << 60) | (uint64_t(uint32_t(seat.id)) << 8) | uint64_t(quickChoose + 1);
            if (key != blurbKey) {
                blurbKey = key;
                blurbs.Get(built->metrics, key, {QuickKindName(seat.id & 0xffff), "Potions and scrolls", qr::HasTiers(seat.id & 0xffff) ? "The best one you carry (Potion order on Quick cast radial's page), or one exact tier: its own page." : "Puts it in the place; it drinks from your belt or inventory.", "Trigger to choose for " + chooseTitle});
            }
            scene.blurb = &blurbs.Get(built->metrics, key, {});
        } else if (const auto* k = SkillOf(s, seat.id, seat.owner)) {
            const int ek = EntryOf(seat.id, seat.owner);   // headset fixes 2026-11-15: another Lab's entry (its words, its dim)
            const bool other = ek >= 0, otherDim = other && entries[size_t(ek)].dim;
            const uint64_t key = (uint64_t(chooser) << 62) | (uint64_t(otherDim) << 61) | (uint64_t(quickChooser) << 59) | (uint64_t(quickChooser ? quickChoose + 1 : 0) << 52) |   // review N1: its place's words
                                 (uint64_t(uint32_t(seat.id)) << 32) | uint32_t(seat.owner + 1);
            if (key != blurbKey) {   // perf R6 #1: strings built only when the pointed skill changes, never per frame
                blurbKey = key;
                const bool order = rw::OrderSeat(shown, step.blurb);   // no level, no mana: its words from the one header
                const std::string sub = order ? std::string(rw::orders::Sub()) : seat.tree >= 0 ? std::string(names[size_t(seat.tree)]) + ", level " + std::to_string(k->level) : std::string(k->owner >= 0 ? "Item skill" : "General");
                const bool listed = choosing ? size_t(step.blurb) < chooseDim.size() && !chooseDim[size_t(step.blurb)] : rw::GlyphPickable(*k);   // the chooser: the ring's one glyph list
                // An order (designer gate D3): every line whole, the short title when the long one does not fit.
                if (order) blurbs.Get(built->metrics, key, tx::WholeBlurb(built->metrics, {k->name, sub, rw::orders::Help(seat.id), rw::orders::ChooserAction()}, rw::orders::Short(seat.id)));
                else if (other) blurbs.Get(built->metrics, key, {k->name, otherDim ? EntryDimBlurb[ek] : "General (Lab)", k->description, chooser ? "Not for glyphs" : "Hold a button on it to assign"});
                else if (rw::MenuOfEntry(seat.id) >= 0 && seat.owner == -1) blurbs.Get(built->metrics, key, {k->name, "Sub-menu (Lab)", k->description, "Tap opens, hold assigns"});   // T036
                else if (quickChooser) blurbs.Get(built->metrics, key, {k->name, sub, k->description, listed ? "Trigger puts it in " + chooseTitle : std::string("Not one Quick cast casts")});   // 2026-11-16
                else blurbs.Get(built->metrics, key, {k->name, sub, k->description, !chooser ? "Tap casts, hold assigns" : listed ? "Trigger puts it on the glyph" : "Needs a target: not for glyphs"});
            }
            scene.blurb = &blurbs.Get(built->metrics, key, {});
        }
    }
    if (policy.Want(scene)) {
        bool composed = false;
        if (gpu->compose.CanDraw()) {
            const auto acquire = reinterpret_cast<PFN_xrAcquireSwapchainImage>(const_cast<void*>(f.acquire));
            const auto wait = reinterpret_cast<PFN_xrWaitSwapchainImage>(const_cast<void*>(f.wait));
            const auto release = reinterpret_cast<PFN_xrReleaseSwapchainImage>(const_cast<void*>(f.release));
            LARGE_INTEGER freq, a0; QueryPerformanceFrequency(&freq); QueryPerformanceCounter(&a0);
            uint32_t index = 0;
            XrSwapchainImageAcquireInfo ai{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
            if (acquire && wait && release && XR_SUCCEEDED(acquire(gpu->chain, &ai, &index)) && index < gpu->images.size()) {
                XrSwapchainImageWaitInfo wi{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO}; wi.timeout = XR_INFINITE_DURATION;
                if (XR_SUCCEEDED(wait(gpu->chain, &wi))) {
                    const int n = onPage ? rw::BuildPage(scene, view, composedList) : rw::Build(scene, composedList);
                    composedN = n; composedRect = quad.c;   // what a screenshot draws again (Shot): with the list just built, even after a Busy frame
                    composed = gpu->compose.Draw(gpu->images[index].texture, quad.c, composedList, n) == rw::Compose::Result::Composed;
                }
                XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
                if (XR_SUCCEEDED(release(gpu->chain, &ri)) && composed) gpu->hasImage = true;
                LARGE_INTEGER a1; QueryPerformanceCounter(&a1);   // perf R6 #2: acquire, wait, draw and release (the 10 s line's acquire max)
                acquireMax = std::max(acquireMax, double(a1.QuadPart - a0.QuadPart) * 1000.0 / double(freq.QuadPart));
            }
        }
        policy.Done(composed);
    }
    if (!gpu->hasImage) return;   // nothing of the ring's drawn yet: upstream's quad this frame
    // One quad, never two (plan E3): upstream's own quad rewritten, or added when upstream skipped it.
    const float pose[7] = {quad.pose.p.x, quad.pose.p.y, quad.pose.p.z, quad.pose.q.x, quad.pose.q.y, quad.pose.q.z, quad.pose.q.w};
    const int rect[4] = {0, 0, quad.c.w, quad.c.h};
    if (q.subImage.swapchain != XR_NULL_HANDLE) {
        q.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;   // premultiplied
        q.pose = {{pose[3], pose[4], pose[5], pose[6]}, {pose[0], pose[1], pose[2]}};
        q.size = {quad.w, quad.h};
        q.subImage.swapchain = gpu->chain; q.subImage.imageRect = {{0, 0}, {quad.c.w, quad.c.h}};
    } else if (f.quad) {
        static XrCompositionLayerQuad own;
        own = {XR_TYPE_COMPOSITION_LAYER_QUAD};
        f.quad(f.quadCtx, &own, pose, quad.w, quad.h, rect, uint64_t(gpu->chain));
    }
    ringShown = q.subImage.swapchain == gpu->chain || f.quad;   // this frame's wheel is the ring's (a screenshot saves it, Shot)
    // "10 s:" while the ring is open (E8; perf R4 #4).
    if (f.nowMs - windowAt >= 10000) {
        windowAt = f.nowMs;
        const auto w = gpu->compose.TakeWindow();
        if (f.say) {
            char line[260];
            std::snprintf(line, sizeof line, "Frame ring wheel: 10 s: composes=%u reused=%u busy=%u cpu mean/p95/max=%.3f/%.3f/%.3f ms gpu mean/p95/max=%.3f/%.3f/%.3f ms (%u) acquire max=%.3f ms render=%d%%",
                          w.composes, policy.reused, policy.busy, w.cpuMean, w.cpuP95, w.cpuMax, w.gpuMean, w.gpuP95, w.gpuMax, w.gpuSamples, acquireMax, RenderPct());
            f.say(line);
        }
        policy.reused = policy.busy = 0; acquireMax = 0;
    }
}

// Round 2026-11-08 item 4: a screenshot's frame (never another), the ring on show: its layer as its own picture, and upstream's
// wheel shot skipped (upstream's atlas holds no ring, and the quad's rectangle is the ring's). The swapchain image cannot be
// read, so Compose::Shot draws the last composed list again into a texture made for this shot and reads that back.
// Round 2026-11-12 Job B (audit bug 4: the save's 42-58 ms sat on the frame, a stutter at every shot): the frame thread
// keeps the GPU's part (the draw, the copy into a readback buffer, the wait for it); reading that buffer, the picture's
// math, the PNG and its "saved" line are the save worker's (rw::shot::SaveQueue: a thread only while a save waits). The
// filled buffer is the job's own, so the Lab going Off or the wheel closing does not touch a save in flight; Reset
// (Shutdown) waits for the files.
rw::shot::SaveQueue* saver = nullptr;   // on the heap from the first screenshot, never freed at DLL unload (as Gpu, review R4 J2)
void Shot(lab::LayerShotFrame f) {
    if (!ringShown || !gpu || !gpu->hasImage || !f.shots || !(*f.shots & f.bit)) return;
    *f.shots &= ~f.bit;
    const double t0 = rw::shot::NowMs();
    const char* why = rw::shot::Queue(saver, t0, f.file,
        [](const char*& no) -> rw::shot::Reader {
            auto rows = std::make_shared<rw::ShotRows>();
            no = gpu->compose.Shot(composedRect, composedList, composedN, 500, *rows);
            return [rows](rw::shot::Raw& raw) { return rows->Read(raw); };   // the worker's: the buffer is let go there
        },
        framevr::png::Save, [say = f.say](const char* line) { if (say) say(line); },
        [] { SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL); });   // a save has no deadline; the frame's threads go first
    if (!why || !f.say) return;   // queued: the worker says saved or not when the file is done
    char line[200];
    std::snprintf(line, sizeof line, "Frame capture: ring wheel not saved (%s; %.1f ms on the frame)", why, rw::shot::NowMs() - t0);
    f.say(line);
}

// Job B: the non-owner's beam, after upstream's laser and dot (Pointer wrote it this frame; nothing when only one hand points).
void Beam(lab::WheelBeamFrame f) {
    const auto b = pt::BeamFor(labstore::TabOn(lab::TabRingWheel), f.open, beam2, f.count, f.limit);
    if (b == pt::Beam::LabOff) beam2 = false;   // the Off edge (perf should-fix 2): never a beam after the Lab went Off
    {   // a beam skipped for want of a layer slot: said once on each change
        const bool noSlot = b == pt::Beam::NoSlot;
        if (noSlot != beamNoSlotSaid && f.say) {
            char line[160];
            std::snprintf(line, sizeof line, noSlot ? "Frame ring wheel: second beam skipped: %u of %u layers used, 2 kept for blood and strain"
                                                    : "Frame ring wheel: second beam back (%u of %u layers used)", f.count, f.limit);
            f.say(line);
        }
        beamNoSlotSaid = noSlot;
    }
    if (b != pt::Beam::Draw || !f.quad || !f.chain || !std::isfinite(beam2Len) || beam2Len <= 0) return;
    const float pose[7] = {beam2Pose.p.x, beam2Pose.p.y, beam2Pose.p.z, beam2Pose.q.x, beam2Pose.q.y, beam2Pose.q.z, beam2Pose.q.w};
    beam2Quad = {XR_TYPE_COMPOSITION_LAYER_QUAD};
    f.quad(f.quadCtx, &beam2Quad, pose, BeamWidthM * Beam2Share, beam2Len, f.beamRect, f.chain, f.premultiplied);
}

// R6Q: once the wheel is shut, the queued quick cast goes through upstream's own glyph press (as a recognised glyph does,
// transport's stroke path): its hand slot, else the right trigger's record (parked first). One branch when nothing waits.
void Cast(lab::RingCastFrame f) {
    if (!pendingCast.on && !pulseCount) return;
    if (!labstore::TabOn(lab::TabRingWheel)) { pendingCast = {}; pulseCount = 0; return; }   // the Off edge's drain: nothing fires after Off (perf R6Q)
    const ULONGLONG now = GetTickCount64();
    for (int i = 0; i < pulseCount;) {
        if (pulses[size_t(i)].at <= now) {
            if (f.pulse) f.pulse(pulses[size_t(i)].hand, pulses[size_t(i)].strength, 40000000);
            pulses[size_t(i)] = pulses[size_t(--pulseCount)];
        } else ++i;
    }
    if (!pendingCast.on || !f.snapshot || !f.cast) return;
    const auto c = pendingCast;
    const auto& s = *static_cast<const ia::Snapshot*>(f.snapshot);
    const ia::Skill* k = SkillOf(s, c.skill, c.owner);
    const std::string skillName = k ? tx::Clean(k->name) : "skill " + std::to_string(c.skill);
    rw::CastWait w{true, c.skill, c.owner, c.hand, c.slot, 0};
    const auto r = rw::StartCast(w, *static_cast<framevr::glyphcast::Cast*>(f.cast), s, labstore::TabOn(lab::TabRingWheel), f.fresh, f.active, !f.wheel && !f.block, f.now, double(now - c.since),
                                 [&](int skill, int owner) { return f.park && f.park(f.session, skill, owner); });
    if (w.on) return;   // still waiting for the wheel to shut
    pendingCast = {};
    char line[260];
    if (r.refused) {
        QueuePulses(c.hand, 3, .35f);   // three short pulses at once (critique-design QS2, vr-ux Q4)
        if (f.say) { std::snprintf(line, sizeof line, "Frame ring wheel: refused %s: %s", skillName.c_str(), r.refused); f.say(line); }
        return;
    }
    if (!r.started) return;
    if (f.press && k) f.press(k);   // the aim stays the player's own (glyphPress.aim zero)
    QueuePulses(c.hand, 1, .5f);
    if (f.say) {
        std::snprintf(line, sizeof line, "Frame ring wheel: tap cast %s slot=%s hand=%s held=%.0f ms via %s", skillName.c_str(),
                      rw::SlotName(framevr::controllers::Layout(c.layout), c.hand, c.slot).c_str(), c.hand ? "L" : "R", c.held,
                      r.how == framevr::glyphcast::How::Slot ? "its hand slot" : r.how == framevr::glyphcast::How::Record ? "the right trigger" : "the right trigger, put on it first");
        f.say(line);
    }
}

void Session(const lab::XrSessionInfo&) { saidUpstream = 0; }
// Transport Shutdown (the GPU idle, before the session goes): join the worker and free everything, waiting as needed.
void Reset(lab::NoArgs) {
    told = false; placed = false; placer.Reset(); hold.Reset(); armyWatch.Reset(); chooseStale = false;
    ringActive = false; engineOpen = false; seatsKey = 0; built.reset(); sendQueue.Clear(); bindWait.Clear();
    quick.Reset(); pendingCast = {}; pulseCount = 0; seenFlips = ~0u; holdFracWas = 0; blurbKey = ~0ull;
    choose = -1; ringPageSeen = false; pages.Reset(); viewKey = ~0ull; countKey = ~0ull; glyphWait.Clear(); wantMenu = {};
    fightFade.Reset(); fightNow = {}; deadSaid = false; animSeen = -1; pointedWas = -1;
    Retire(sayKept, "session end");   // transport waited for its GPU first: idle, destroyed at once; said (audit 2026-11-10 bug 8)
    Reap(true, sayKept);
    if (saver) saver->Drain();   // Job B: a screenshot's file still being written is finished before the session goes
}
}  // namespace labringwheel
