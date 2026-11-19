#pragma once
// Ring skill menu (build step): the Glyphs and Gestures pages (plan; look "Glyphs page", "Gestures page",
// "Glyph Choose a skill state"). Pure and Windows-free. Upstream keeps both pages' data and actions: the ring only draws
// them in its own look and turns a release into upstream's cell for one frame (a gesture card, Back, the other hand), or,
// for a glyph's skill, into a BindGlyph request (upstream's paged picker never opens; the picker's list is upstream's rule).
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <string>
#include <vector>
#include "ring_wheel_draw.h"
#include "ring_wheel_orders.h"
#include "glyph_recognizer.h"
#include "quick_radial.h"   // HasTiers: which kinds have a "best" (Healing, Mana, Rejuvenation)
#include "spell_motion.h"

namespace framevr::ringwheel {
// A kind whose entry means "the best one carried": only one with tiers (quick_radial.h HasTiers). Antidote, Thawing and Stamina
// have one potion each, so no "Best" pill and no "best one" line.
inline bool BestSeat(int id) { return KindSeat(id) && framevr::quickradial::HasTiers(id & 0xffff); }
// ("ring menu > quick cast / ring menu > gestures / ring menu > glyph"; "the edit mode is when the user
// is in the ring menu"): Quick cast is a sub-menu of the ring as Glyphs is. Quick: its 12 places (ring Quick cast radial's ring, in
// order) round the ring, center Back; QuickChoose: the class's skill layout to choose what goes in a place, its potions and
// scrolls on the shelf row, center Back / Clear (as the glyph chooser). QuickKind ("if they have normal
// rejuv and a full rejuv, and they select the normal rejuv, it should not drink a full rejuv"): a kind picked on the chooser's row
// while the hero carries some: the kind (the best one, when cast), then each exact one carried, round the ring as Quick's places,
// center Back to the chooser. All the ring's own pages over upstream's Spells page.
enum class RingPage { Main, Glyphs, GlyphChoose, Gestures, Quick, QuickChoose, QuickKind };
inline bool QuickPage(RingPage p) { return p == RingPage::Quick || p == RingPage::QuickChoose || p == RingPage::QuickKind; }
inline bool QuickRing(RingPage p) { return p == RingPage::Quick || p == RingPage::QuickKind; }   // drawn and hit as Quick's 12 places, one Back
constexpr int QuickPlaces = 12;   // quick_radial.h MaxItems
constexpr float QuickR = 205, QuickIconPx = 56, QuickHitR = 34;   // 12 places 107 px apart on r 205: 68 px hit circles clear of each other
constexpr float GlyphR = 200, GlyphIconPx = 64, GlyphHitR = 40, GlyphSkillR = 262, GlyphSkillPx = 36;
constexpr float GestureR = 210, GestureIconPx = 52, GestureHitR = 35, NoPointsDim = .45f;
constexpr int SpellsTabCell = 9;   // upstream's SPELLS tab: Gestures' Back
constexpr int MaxItems = interaction::MotionCards;   // 14: the most cards a class has; 8 shapes
static_assert(MaxItems >= glyphs::ShapeCount);
// The ring page on upstream's page (the footer's own bounds:).
inline interaction::MenuPage UpstreamPage(RingPage p) {
    return p == RingPage::Gestures ? interaction::MenuPage::Motions : p == RingPage::Main || QuickPage(p) ? interaction::MenuPage::Spells : interaction::MenuPage::Glyphs;
}
// a sub-menu wanted (its bound button, its shelf entry's tap) from upstream's page, once a frame until Done: Glyphs and
// Gestures are upstream's own pages (from Spells their center cells; from the other page back to Spells first). Quick cast's page
// is the ring's own (RingPage::Quick, over Spells): Done at once. Fire: release `cell` to upstream this frame (as a click), then ask
// again. Wait: an editor or another page the ring does not draw (the caller gives up after a while).
enum class Nav { Wait, Fire, Done };
struct NavStep { Nav nav = Nav::Wait; int cell = -1; };
inline NavStep NavigateTo(int third, interaction::MenuPage page) {
    using P = interaction::MenuPage;
    if (third != ThirdGlyphs && third != ThirdGestures) return {Nav::Done};
    const P want = third == ThirdGlyphs ? P::Glyphs : P::Motions;
    if (page == want) return {Nav::Done};
    if (page == P::Spells) return {Nav::Fire, third == ThirdGlyphs ? interaction::GlyphsCell : interaction::MotionsCell};
    if (page == P::Glyphs) return {Nav::Fire, interaction::GlyphsCell};   // the Glyphs page's Back
    if (page == P::Motions) return {Nav::Fire, SpellsTabCell};            // the Gestures page's Back
    return {};
}

// upstream clicks only on a rising trigger edge, with rearm clear (a frame with the trigger at 0), and
// never on the frame the wheel's hand changes (interactions.h). So a Fire is one frame at 1, then at least one frame at 0, and
// none on the frame the wheel opens or changes hand. the wheel's hand is watched every frame (so the first
// click after an open is not lost on a stale hand), and the give-up runs from the ask or the last page change (progress), never
// from the last click, with at most MaxFires clicks a page (a grip held keeps upstream's rearm set: every click is ignored).
struct FireGate {
    static constexpr int MaxFires = 6;
    int handWas = -2; bool changedNow = true, firedLast = false; double lastMs = 0;
    int pageWas = -1, fires = 0; double sinceMs = 0;
    void Reset() { *this = FireGate{}; }
    // A sub-menu asked for: a fresh count and clock (the hand watch is kept).
    void Ask(double nowMs) { firedLast = false; pageWas = -1; fires = 0; sinceMs = nowMs; }
    // Every frame: the wheel's hand (-1 shut) and upstream's page; a new page is progress.
    void Watch(int wheelHand, int page, double nowMs) {
        changedNow = wheelHand != handWas; handWas = wheelHand;
        if (page != pageWas) { pageWas = page; fires = 0; sinceMs = nowMs; }
    }
    // true: press cell this frame (trigger 1); false: leave the trigger at 0 this frame.
    bool Step(double nowMs) {
        if (changedNow || firedLast || fires >= MaxFires) { firedLast = false; return false; }
        firedLast = true; lastMs = nowMs; ++fires;
        return true;
    }
    bool GaveUp(double nowMs, double limitMs) const { return nowMs - sinceMs > limitMs || fires >= MaxFires; }
};
// a sub-menu never goes on a trigger (it is the attack and the ring's own click). slot: the hand's button, 0 trigger.
inline bool MenuButtonAllowed(int slot) { return slot > 0; }

// Upstream's glyph picker list (interaction::Engine::OpenGlyphSkills): the hero's own skills (owner -1), not 357, none that
// needs a target except Town Portal (359).
inline bool GlyphPickable(const interaction::Skill& k) { return k.id >= 0 && k.id != 357 && k.owner == -1 && (!k.directed || k.id == 359); }
// The ring's glyph list: everything a glyph shape may cast, as upstream's picker offers it: the catalog's
// pickable skills, then the 7 Commander orders when the hero has a summon army. The orders and the army test are upstream's
// own (interaction::HasSummonArmy, interaction::MakeCommanderSkill; no target on a glyph, as OpenGlyphSkills makes them),
// never a copy of their rules. THE ONE LIST: the chooser lights its seats and builds its order row from it (ChooserLayout),
// and ring-wheel-test #20 holds it equal to upstream's picker. Built when a shape is chosen, never per frame.
inline std::vector<interaction::Skill> GlyphChoices(const interaction::Snapshot& s) {
    std::vector<interaction::Skill> list;
    for (unsigned i = 0; i < s.skillCount && i < s.catalog.size(); ++i) if (GlyphPickable(s.catalog[i])) list.push_back(s.catalog[i]);
    if (interaction::HasSummonArmy(s))
        for (int id = orders::First; id <= orders::Last; ++id) { auto order = interaction::MakeCommanderSkill(id, true); order.directed = false; list.push_back(order); }
    return list;
}
inline const interaction::Skill* ChoiceOf(const std::vector<interaction::Skill>& list, int id, int owner) {
    for (const auto& k : list) if (k.id == id && k.owner == owner) return &k;
    return nullptr;
}
// The chooser's seats from the main ring's and that list. With no order on
// the list: the main layout exactly, as before. With orders: every tree seat stays where it is (same index, same place);
// the shelf row becomes the shelf skills a glyph can take, a gap, then the orders in upstream's order, centered on C at the
// shelf's own height and pitch. Shelf skills a glyph cannot take step out (they were drawn at 25% there). ChooserRow: the same
// with any row (`row`: its seats' ids, owner -1; Quick cast's chooser: its potions and scrolls).
inline Layout ChooserRow(const Layout& main, const std::vector<interaction::Skill>& list, const std::vector<int>& row) {
    const int orderCount = int(row.size());
    if (!orderCount) return main;
    Layout l = main;
    l.count = 0; l.shelf = 0;
    for (int i = 0; i < main.count; ++i) if (main.seats[size_t(i)].tree >= 0) l.seats[size_t(l.count++)] = main.seats[size_t(i)];
    int kept = 0;
    for (int i = 0; i < main.count; ++i) { const auto& m = main.seats[size_t(i)]; kept += m.tree < 0 && ChoiceOf(list, m.id, m.owner) != nullptr; }
    kept = std::min(kept, MaxSeats - l.count - orderCount);
    // The row inside the wheel: the gap narrows first, then the last shelf skills step out (5 or more that a glyph can take:
    // a hero has two, Town Portal and Identify).
    auto gapFor = [&](int shelf) { return shelf > 0 ? std::clamp(WheelW - HitPx - float(shelf + orderCount - 1) * ShelfPitch, 0.f, ShelfGap) : 0.f; };
    while (kept > 0 && float(kept + orderCount - 1) * ShelfPitch + gapFor(kept) + HitPx > WheelW) --kept;
    const float gap = gapFor(kept);
    const float width = float(kept + orderCount - 1) * ShelfPitch + gap;
    float x = C - width / 2;
    for (int i = 0, k = 0; i < main.count && k < kept; ++i) {
        const auto& m = main.seats[size_t(i)];
        if (m.tree >= 0 || !ChoiceOf(list, m.id, m.owner)) continue;
        auto& seat = l.seats[size_t(l.count++)]; seat = m; seat.shelfItem = false; seat.x = x; seat.y = ShelfY;
        x += ShelfPitch; ++l.shelf; ++k;
    }
    x += gap;
    for (const int id : row) {
        if (l.count >= MaxSeats) continue;
        auto& seat = l.seats[size_t(l.count++)]; seat = Seat{}; seat.id = id; seat.owner = -1; seat.tree = -1; seat.shelfItem = true; seat.x = x; seat.y = ShelfY;
        x += ShelfPitch; ++l.shelf;
    }
    return l;
}
inline Layout ChooserLayout(const Layout& main, const std::vector<interaction::Skill>& list) {
    std::vector<int> row;
    for (const auto& k : list) if (orders::Is(k.id) && k.owner == -1) row.push_back(k.id);
    return ChooserRow(main, list, row);
}
// Quick cast's chooser list: every known skill the radial casts (a skill id under 4096: the class's, the general and
// item skills; quick_radial.h IsSkillId), never a ring entry (Draw glyph 9000, the modules' 9001 on) or a Commander order (the
// radial casts the catalog's skills only). Built when a place is chosen, never per frame.
inline bool QuickPickable(const interaction::Skill& k) { return k.known && k.id >= 0 && k.id < 4096 && !orders::Is(k.id); }
inline std::vector<interaction::Skill> QuickChoices(const interaction::Snapshot& s) {
    std::vector<interaction::Skill> list;
    for (unsigned i = 0; i < s.skillCount && i < s.catalog.size(); ++i) if (QuickPickable(s.catalog[i])) list.push_back(s.catalog[i]);
    return list;
}
// The army watch. Whether the hero has a summon army is NOT part of the seats' key: a change of
// that key resets the main ring's engine and quick cast, rebuilds the seats and asks the atlas again, and the live army
// bit drops whenever the companions copy is over a second old (a load, a hitch, a failed read). The watch asks upstream's
// own HasSummonArmy only when the catalog changed (the caller's key for it) or the live bit flipped, and says `changed`
// only when the answer itself changed: then the chooser's seats are stale, and nothing else. `icons`: this hero has had an
// army, so the orders' icons belong in the atlas request and stay there (a second flip costs no atlas work); `iconsNew`:
// from this step on. Another class: another hero, the icons start over.
class ArmyWatch {
    uint64_t catalog_ = 0;
    int cls_ = -2, asked_ = 0;
    bool known_ = false, live_ = false, army_ = false, icons_ = false;
public:
    struct Out { bool army = false, changed = false, icons = false, iconsNew = false; };
    Out Step(const interaction::Snapshot& s, uint64_t catalogKey) {
        const bool live = (s.companions.relevant & companions::ArmyTypeMask) != 0;
        if (known_ && catalogKey == catalog_ && live == live_) return {army_, false, icons_, false};   // the usual frame: one AND, two compares
        if (s.skillClass != cls_) { cls_ = s.skillClass; icons_ = false; }
        known_ = true; catalog_ = catalogKey; live_ = live; ++asked_;
        const bool army = interaction::HasSummonArmy(s);
        const Out o{army, army != army_, icons_ || army, army && !icons_};
        army_ = army; icons_ = o.icons;
        return o;
    }
    int Asked() const { return asked_; }   // how often upstream was asked (the tests)
    void Reset() { *this = ArmyWatch{}; }
};
inline bool OrderSeat(const Layout& l, int index) { return index >= 0 && index < l.count && l.seats[size_t(index)].owner == -1 && orders::Is(l.seats[size_t(index)].id); }
// The orders' pictures in the atlas request (the seats' ids, then the page cards'): each order's icon id once.
inline int AddOrderIcons(int* ids, int n, int cap) {
    for (int id = orders::First; id <= orders::Last; ++id) {
        const int icon = orders::IconSkill(id);
        if (n < cap && std::find(ids, ids + n, icon) == ids + n) ids[n++] = icon;
    }
    return n;
}
// ---- A button on an order ----
// On the chooser an order takes presses as a main-ring seat does, through the same QuickCast: held to Hold to assign it
// goes on that button; the pointing hand's trigger released under the Tap limit picks it for the glyph; a release between
// the two does nothing. Any other button's tap does nothing (no order is given from the chooser). A skill's seat there
// keeps the chooser's own rule: the trigger's release picks at any press length (the engine's Pick).
// What QuickCast sees under the laser: an order's seat, else a gap (no hold starts, and no cancel-off-wheel close).
inline Target OrderUnder(const Layout& l, const Target& under) { return under.kind == Kind::Seat && OrderSeat(l, under.index) ? under : Target{Kind::Gap}; }
// The engine's own pick (a trigger release on a seat) stands for a skill, never for an order.
inline bool EnginePicks(const Layout& l, int picked) { return picked >= 0 && picked < l.count && !OrderSeat(l, picked); }
enum class OrderAct { None, Pick, Assign, NoChange };
inline OrderAct OrderActOf(const QuickOut& e, int pointer) {
    if (e.event == Quick::Assign) return OrderAct::Assign;
    if (e.event == Quick::NoChange) return OrderAct::NoChange;
    if (e.event == Quick::QuickCast) return e.slot == 0 && e.hand == pointer ? OrderAct::Pick : OrderAct::None;
    return OrderAct::None;
}
// The hold ring on an order shows from the first frame of the press, filling to Hold to assign; a seat
// on the main ring shows it from the Tap limit (QuickOut::holdFrac). Hold to assign Off: no ring.
inline float OrderHoldFrac(const QuickOut& e, int holdMs, int tapMs) {
    const int full = EffectiveHoldMs(holdMs, tapMs);
    if (e.holding < 0 || full <= 0) return 0;
    if (e.assigned) return 1;
    return float(std::clamp(e.heldMs / double(full), .001, 1.0));
}
// The glyph bind upstream's picker would send (Engine: request(BindGlyph, GlyphIndex(glyphHand, glyphEdit)), its skill and
// owner; -1 = Clear glyph, the left hand's then casts the right's). The sequence's high bit: never one of upstream's.
inline interaction::Request GlyphRequest(int hand, int shape, int skill, int owner, uint64_t number, uint64_t session, uint64_t now) {
    interaction::Request r;
    if (shape < 0 || shape >= glyphs::ShapeCount) return r;
    r.action = interaction::Action::BindGlyph;
    r.sequence = (1ull << 63) | number; r.session = session; r.at = now;
    r.index = interaction::GlyphIndex(hand, shape); r.skill = skill < 0 ? -1 : skill; r.skillOwner = skill < 0 ? -1 : owner;
    return r;
}
// Item i of n on a ring, clockwise from the top (canvas y down), wheel px.
inline void RingPos(int i, int n, float r, float& x, float& y) {
    const float a = -Pi / 2 + Tau * float(i) / float(std::max(n, 1));
    x = C + r * std::cos(a); y = C + r * std::sin(a);
}

// ---- Pointing and releases on the two pages ----
struct PageIn { double nowMs = 0; bool hit = false; float x = 0, y = 0; bool trigger = false; RingPage page = RingPage::Glyphs; int count = 0; };
enum class PageEvent { None, Upstream, ChooseSkill, Back, Move };   // Back: Quick cast's center (the ring's page, no upstream cell); Move: its page's lower half
// Copied into a Scene's StepOut (ToStep) for the draw and the redraw key: under, pressed, locked, centerGrown, blurb.
struct PageOut { Target under{}, pressed{}; PageEvent event = PageEvent::None; int cell = -1, shape = -1; bool locked = false, grown = false; int blurb = -1; };
class PageEngine {
    Target held{}, lastUnder{};
    bool triggerWas = false, mustRelease = false, grown = false;
    double lockUntil = 0, itemSince = -1, centerSince = -1;
    RingPage was = RingPage::Main;
public:
    void Reset() { *this = PageEngine{}; }
    // Footer first (upstream's cells on upstream's page), then the center (Glyphs: Back above, the other hand below, a 6 px
    // dead band keeping the last; Gestures: one Back), then the ring's items (nearest within the hit radius, kept until the
    // hysteresis past it), else a gap inside the wheel or nothing.
    Target HitTest(RingPage page, float cx, float cy, int count, const Settings& s) const {
        if (const int cell = FooterAt(cx, cy, s.developer, s.footerFull, UpstreamPage(page)); cell >= 0) return {Kind::Footer, cell};
        const float x = cx - CanvasX, y = cy - CanvasY, r = std::hypot(x - C, y - C);
        const bool inCenter = lastUnder.kind == Kind::Back || lastUnder.kind == Kind::Hand;
        if (r <= (inCenter && grown ? CenterGrownR : CenterR)) {   // the grown circle's edge only once it has grown
            if (page == RingPage::Gestures || page == RingPage::QuickKind || y < C - CenterDeadBand) return {Kind::Back};   // Quick cast's page: Back above, Move below
            if (y > C + CenterDeadBand) return {Kind::Hand};
            return inCenter ? lastUnder : Target{Kind::Gap};
        }
        const bool gestures = page == RingPage::Gestures, quick = QuickRing(page);
        const float ring = gestures ? GestureR : quick ? QuickR : GlyphR, hitR = gestures ? GestureHitR : quick ? QuickHitR : GlyphHitR;
        auto dist = [&](int i) { float ix, iy; RingPos(i, count, ring, ix, iy); return std::hypot(x - ix, y - iy); };
        if (lastUnder.kind == Kind::Seat && lastUnder.index < count && dist(lastUnder.index) <= hitR + s.hysteresisPx) return lastUnder;
        int best = -1; float bestD = hitR;
        for (int i = 0; i < count; ++i) if (const float d = dist(i); d <= bestD) { best = i; bestD = d; }
        if (best >= 0) return {Kind::Seat, best};
        return r <= ContentR ? Target{Kind::Gap} : Target{Kind::None};
    }
    PageOut Step(const PageIn& in, const Settings& s) {
        PageOut out;
        if (in.page != was) {   // a page change: presses locked out 250 ms, a trigger already down let go first, timers reset
            was = in.page; held = {}; lastUnder = {}; lockUntil = in.nowMs + s.lockoutMs; mustRelease = in.trigger; triggerWas = in.trigger;
            itemSince = centerSince = -1; grown = false;
        }
        const Target under = in.hit ? HitTest(in.page, in.x, in.y, in.count, s) : Target{};
        const bool center = under.kind == Kind::Back || under.kind == Kind::Hand;
        if (center) { if (centerSince < 0) centerSince = in.nowMs; } else centerSince = -1;
        if (under.kind == Kind::Seat) { if (!(lastUnder == under) || itemSince < 0) itemSince = in.nowMs; } else itemSince = -1;
        lastUnder = under; out.under = under;
        out.blurb = itemSince >= 0 && in.nowMs - itemSince >= s.blurbMs ? under.index : -1;   // P11: the item's lines after its hold
        out.grown = grown = out.blurb >= 0 || (centerSince >= 0 && in.nowMs - centerSince >= s.growMs);   // P10: never on a sweep through
        out.locked = in.nowMs < lockUntil;
        const bool rise = in.trigger && !triggerWas, fall = !in.trigger && triggerWas;
        triggerWas = in.trigger;
        if (fall && mustRelease) mustRelease = false;
        else if (rise && !out.locked && !mustRelease) held = under.kind == Kind::None ? Target{Kind::Gap} : under;
        Target fired{};
        if (fall && held.kind != Kind::None) {   // fires on release, on the target pressed
            if (held == under) fired = held;
            held = {};
        }
        out.pressed = held;
        switch (fired.kind) {
        case Kind::Footer: out.event = PageEvent::Upstream; out.cell = fired.index; break;
        case Kind::Back:
            if (QuickRing(in.page)) { out.event = PageEvent::Back; break; }   // Quick: the ring menu's main page again; QuickKind: the chooser
            out.event = PageEvent::Upstream; out.cell = in.page == RingPage::Gestures ? SpellsTabCell : interaction::GlyphsCell; break;
        case Kind::Hand:
            if (in.page == RingPage::Quick) { out.event = PageEvent::Move; break; }   // Quick cast's page: its lower half, Move
            out.event = PageEvent::Upstream; out.cell = interaction::GlyphHandCell; break;
        case Kind::Seat:
            if (in.page == RingPage::Gestures) { out.event = PageEvent::Upstream; out.cell = interaction::MotionCardCell(fired.index); }
            else { out.event = PageEvent::ChooseSkill; out.shape = fired.index; }   // a glyph's shape, or Quick cast's place
            break;
        default: break;
        }
        return out;
    }
};
inline void ToStep(const PageOut& p, StepOut& s) {
    s = {}; s.under = p.under; s.pressed = p.pressed; s.locked = p.locked; s.centerGrown = p.grown; s.blurb = p.blurb;
}

// ---- What the page shows (the ring fills it on a change, never per frame) ----
struct PageView {
    RingPage page = RingPage::Glyphs;
    int count = 0;                              // 8 shapes, or the class's gesture cards
    std::array<int16_t, MaxItems> cell{};       // per item: the icon atlas cell of its skill, -1 none
    std::array<bool, MaxItems> on{}, follows{}, has{};   // gesture on; a left shape showing the right's skill; a skill set
    std::array<bool, MaxItems> best{};          // Quick cast: a kind (the best one carried), its "Best" pill (an exact one has none)
    std::array<bool, MaxItems> dim{};           // Quick cast's kind page: a tier not carried, drawn dim (it can still be put in a place)
    std::array<int, MaxItems> skill{};          // per item: its skill (a shape's cast, a card's), -1 none
    std::string title, top, bottom, below;      // the arc title; the center's halves (nothing pointed); under the label
    std::string bottomCount;                    // Glyphs: the hand half's count line when pointed ("8 shapes, left hand"; design RS4); Quick cast: Move's line
    std::string bottomHelp;                     // Quick cast: Move's trigger line when pointed
    std::array<std::string, MaxItems> name, line, help;   // the pointed item's blurb: name, its line, the trigger's help
    bool clock = false;                         // Quick cast, Fixed places: the places named as a clock's hours (12 at the top)
    int moveFrom = -1;                          // Quick cast's Move: the place whose item is being moved (ringed), -1 none
    bool operator==(const PageView&) const = default;
};
// item 6: the Gestures page shows every card, learned or not, and the seats hold only the skills the hero has.
// Appends to ids[0, n) the class's cards and the skills a family card can show (spellmotion::CastSkillOf: Clay, Blood, Fire
// Golem; Shadow Warrior, Master) that are not there yet; the new n. Called when the atlas request is built, never per frame.
inline int AddGesturePageIcons(int skillClass, int* ids, int n, int cap) {
    auto add = [&](int id) { if (n < cap && std::find(ids, ids + n, id) == ids + n) ids[n++] = id; };
    const auto page = spellmotion::PageOf(skillClass);
    for (int i = 0; i < page.count; ++i) {
        add(page.skills[i]);
        if (page.skills[i] == 75 || page.skills[i] == 85) { add(94); add(85); add(75); }
        else if (page.skills[i] == 268) add(279);
    }
    return n;
}
// A skill's icon cell by its id in the atlas request (seats and page cards): -1 when it has none.
inline int CellOfSkill(const int* ids, const int16_t* cells, int n, int id) {
    for (int i = 0; i < n; ++i) if (ids[i] == id) return cells[i];
    return -1;
}
// The Glyphs page for the hand shown (upstream's Output: glyphHand, glyphSkills). nameOf(skill) and cellOf(skill) are the
// caller's (the catalog's names, the atlas's cells).
template <class NameOf, class CellOf>
inline void FillGlyphs(PageView& v, const interaction::GlyphSets& sets, int hand, NameOf nameOf, CellOf cellOf) {
    v = PageView{}; v.page = RingPage::Glyphs; v.count = glyphs::ShapeCount;
    v.title = hand ? "Left hand's glyphs" : "Right hand's glyphs";
    v.top = "Back"; v.bottom = hand ? "Right" : "Left";   // one word: "Right hand" at 22 px overflows the r 80 split
    v.bottomCount = hand ? "Right hand's 8 shapes" : "Left hand's 8 shapes";
    for (int i = 0; i < v.count; ++i) {
        const size_t k = size_t(i);
        const int casts = interaction::GlyphCasts(sets, hand, i);
        v.skill[k] = casts; v.has[k] = casts >= 0; v.follows[k] = interaction::GlyphFollows(sets, hand, i);
        v.cell[k] = int16_t(casts >= 0 ? cellOf(casts) : -1);
        v.name[k] = glyphs::Name(glyphs::Shape(i));
        v.line[k] = casts < 0 ? std::string("No skill yet") : v.follows[k] ? "Casts the right hand's " + std::string(nameOf(casts)) : "Casts " + std::string(nameOf(casts));
        v.help[k] = "Trigger to choose a skill";
    }
}
// Quick cast's page: its 12 places, place k holding the radial's item k (ids, in the ring's order; n of them), the
// rest empty. nameOf(id) and cellOf(id) are the caller's (a skill's or an item's name; the atlas cell, -1 none). fixed (Quick cast
// layout Fixed places): ids[k] is place k's item, -1 an empty one, each place named as a clock's hour (quick_radial.h
// PlaceNumber: 12 at the top, 3 to the right); an empty place fills exactly there.
template <class NameOf, class CellOf>
inline void FillQuick(PageView& v, const int* ids, int n, NameOf nameOf, CellOf cellOf, bool fixed = false) {
    v = PageView{}; v.page = RingPage::Quick; v.count = QuickPlaces; v.clock = fixed;
    n = std::clamp(n, 0, QuickPlaces);
    int filled = 0;
    for (int i = 0; i < n && ids; ++i) filled += ids[i] >= 0;
    for (int i = 0; i < v.count; ++i) {
        const size_t k = size_t(i);
        const bool has = i < n && ids && ids[i] >= 0;
        const std::string place = "Place " + std::to_string(fixed ? (i == 0 ? 12 : i) : i + 1);
        v.has[k] = v.on[k] = has; v.skill[k] = has ? ids[i] : -1;
        v.cell[k] = int16_t(has ? cellOf(ids[i]) : -1); v.best[k] = has && BestSeat(ids[i]);
        v.name[k] = has ? std::string(nameOf(ids[i])) : place;
        if (fixed) v.line[k] = has ? place + " (" + std::to_string(i == 0 ? 12 : i) + " o'clock)" : std::string("Empty: fills exactly here");
        else v.line[k] = has ? place + " of " + std::to_string(n) : i == n ? std::string("Empty: the next place") : "Empty (fills place " + std::to_string(n + 1) + ")";
        v.help[k] = has ? "Trigger to change it" : "Trigger to fill it";
    }
    v.title = "Quick cast"; v.top = "Back";
    v.below = std::to_string(filled) + " of " + std::to_string(QuickPlaces) + " filled";
    // the center's lower half: Move, one place's item to another (no seat on any chooser)
    v.bottom = "Move"; v.bottomCount = filled ? "One place to another" : "Nothing to move yet"; v.bottomHelp = filled ? "Trigger, then two places" : "Fill a place first";
}
// Quick cast's Move, from the center's lower half: picking (no place yet), then from (the place whose item waits for where
// it goes, ringed). Each place says what a trigger there does; Back or Move again stops it (the page stays). A `from` that is empty
// now (the ring changed under it) marks nothing: the caller drops it.
inline void MarkQuickMove(PageView& v, int from, bool picking = false) {
    const bool moving = from >= 0 && from < v.count && v.has[size_t(from)];
    if (!moving && !picking) return;
    v.bottomCount = moving ? "Moving place " + std::to_string(v.clock ? (from == 0 ? 12 : from) : from + 1) : std::string("Picking a place");
    v.bottomHelp = "Trigger to stop";
    if (!moving) {
        v.title = "Move which place?"; v.below = "Trigger the one to move";
        for (int i = 0; i < v.count; ++i) {
            const size_t k = size_t(i);
            v.line[k] = v.has[k] ? std::string("Moves this one") : std::string("Empty: nothing to move");
            v.help[k] = v.has[k] ? "Trigger to move this one" : "Pick a filled place";
        }
        return;
    }
    v.moveFrom = from;
    const int number = v.clock ? (from == 0 ? 12 : from) : from + 1;
    v.title = "Move place " + std::to_string(number);
    v.below = "Trigger where it goes";
    for (int i = 0; i < v.count; ++i) {
        const size_t k = size_t(i);
        if (i == from) { v.line[k] = "Moving this one"; v.help[k] = "Trigger to leave it here"; continue; }
        v.line[k] = v.has[k] ? "Swaps with place " + std::to_string(number) : v.clock ? std::string("Empty: it goes exactly here") : std::string("Empty: it goes last");
        v.help[k] = v.has[k] ? "Trigger to swap them" : "Trigger to move it here";
    }
}
// Reviews / the radial's ring is this character's only once the radial has it for this game (its session and
// class) and its save is read (Saved), or there is none to tell it by (ThisGame: kept for this game, added to the save once read).
// Reading: no places shown as fact and no edits asked; ThisGame: the page says so.
enum class QuickRingState { Reading, Saved, ThisGame };
inline void MarkQuick(PageView& v, QuickRingState state) {
    if (state == QuickRingState::ThisGame) { v.below = "This game only"; return; }
    if (state != QuickRingState::Reading) return;
    for (int i = 0; i < v.count; ++i) {
        const size_t k = size_t(i);
        v.has[k] = v.on[k] = v.best[k] = false; v.skill[k] = -1; v.cell[k] = -1;
        v.name[k] = "Place " + std::to_string(i + 1); v.line[k] = "Reading this character's ring"; v.help[k] = "A moment: its places show once read";
    }
    v.below = "Reading its ring"; v.bottomCount = "Reading its ring"; v.bottomHelp = "A moment";
}
// One kind's page (QuickKind): ids[0] the kind, then every tier the game has, strongest first (quick_radial.h TiersOf); n of
// them (at most 12). dimOf(i): item i is a tier not carried (dim, still choosable). place: the Quick cast place it fills
// (0-based); title: the kind's name.
template <class NameOf, class CellOf, class DimOf>
inline void FillQuickKind(PageView& v, const int* ids, int n, int place, const std::string& title, NameOf nameOf, CellOf cellOf, DimOf dimOf) {
    v = PageView{}; v.page = RingPage::QuickKind; v.count = ids ? std::clamp(n, 0, QuickPlaces) : 0;
    const std::string to = "Trigger puts it in place " + std::to_string(place + 1);
    for (int i = 0; i < v.count; ++i) {
        const size_t k = size_t(i);
        v.has[k] = v.on[k] = true; v.skill[k] = ids[i]; v.best[k] = BestSeat(ids[i]);
        v.cell[k] = int16_t(cellOf(ids[i]));
        v.name[k] = std::string(nameOf(ids[i]));
        v.dim[k] = !v.best[k] && dimOf(i);
        v.line[k] = v.best[k] ? std::string("The best one you carry, when cast") : v.dim[k] ? std::string("Exactly this one; none carried now") : std::string("Exactly this one, never a stronger one");
        v.help[k] = to;
    }
    v.title = title; v.top = "Back";
    v.below = "For place " + std::to_string(place + 1);
}
template <class NameOf, class CellOf>
inline void FillQuickKind(PageView& v, const int* ids, int n, int place, const std::string& title, NameOf nameOf, CellOf cellOf) {
    FillQuickKind(v, ids, n, place, title, nameOf, cellOf, [](int) { return false; });
}
// The Gestures page (upstream's MOTIONS page: spellmotion::PageOf, its on bits; the card's own title and cue strings).
template <class NameOf, class CellOf, class Known>
inline void FillGestures(PageView& v, int skillClass, int off, NameOf nameOf, CellOf cellOf, Known known) {
    v = PageView{}; v.page = RingPage::Gestures;
    const auto page = spellmotion::PageOf(skillClass);
    v.count = std::min(page.count, MaxItems);
    int onCount = 0;
    for (int i = 0; i < v.count; ++i) {
        const size_t k = size_t(i);
        const int id = page.skills[i];
        const int shown = spellmotion::Family(id) ? spellmotion::CastSkillOf(id, known) : id;   // as upstream's card
        const bool have = known(shown);
        v.skill[k] = shown; v.on[k] = spellmotion::On(skillClass, id, off); v.has[k] = have; onCount += v.on[k];
        v.cell[k] = int16_t(cellOf(shown));
        v.name[k] = std::string(have ? nameOf(shown) : spellmotion::SpellName(shown)) + spellmotion::CardArm(id);
        std::string cue = std::string(spellmotion::CueOf(id)) + (have ? "." : " (no points yet).");
        cue[0] = char(std::toupper(uint8_t(cue[0])));
        v.line[k] = cue;
        v.help[k] = v.on[k] ? "On: trigger to turn off" : "Off: trigger to turn on";
    }
    v.title = "Gestures"; v.top = "Back";
    v.below = std::to_string(onCount) + " of " + std::to_string(v.count) + " on";
}
// The main page's halves: a count for the pointed half: "8 shapes, right hand", "11 of 14 on".
inline std::string GlyphsCount(int hand) { return hand ? "8 shapes, left hand" : "8 shapes, right hand"; }
inline std::string GesturesCount(int skillClass, int off) {
    const auto page = spellmotion::PageOf(skillClass);
    int on = 0;
    for (int i = 0; i < page.count; ++i) on += spellmotion::On(skillClass, page.skills[i], off);
    return std::to_string(on) + " of " + std::to_string(page.count) + " on";
}

// Each shape's stroke as 12 segments in a unit box (y up, upstream's templates), made once.
inline const std::array<std::array<glyphs::Point, 13>, glyphs::ShapeCount>& ShapeStrokes() {
    static const auto strokes = [] {
        std::array<std::array<glyphs::Point, 13>, glyphs::ShapeCount> s{};
        for (int k = 0; k < glyphs::ShapeCount; ++k) {
            const auto c = glyphs::Template(glyphs::Shape(k));
            for (size_t i = 0; i <= 12; ++i) s[size_t(k)][i] = c.empty() ? glyphs::Point{} : c[std::min(c.size() - 1, i * (c.size() - 1) / 12)];
        }
        return s;
    }();
    return strokes;
}
inline void PushStroke(Instance* out, int& n, int max, float x0, float y0, float x1, float y1, float w, Rgb c, float a) {
    const float dx = x1 - x0, dy = y1 - y0, len = std::hypot(dx, dy);
    if (len < .01f) return;
    Push(out, n, max, {(x0 + x1) / 2, (y0 + y1) / 2, len + w * .5f, w, std::atan2(dy, dx), 0, 0, 0, 0, c.r, c.g, c.b, a, RecipeRect, 0});
}
// The Glyphs or Gestures page: its items on their ring, the center (backing, rune ring, halves or the pointed item's blurb in
// s.blurb), the title on the top arc. Shapes first (within maxShapes), then strokes and text (within maxText).
inline int BuildPage(const Scene& s, const PageView& v, Instance* out, int maxShapes = MaxInstances, int maxText = MaxText) {
    int n = 0;
    if (!s.open || s.wheelAlpha <= 0) return 0;
    const float A = std::clamp(s.wheelAlpha, 0.f, 1.f) * (s.step.locked ? .8f : 1.f), cx = CanvasCx, cy = CanvasCy;
    const bool quickPage = QuickRing(v.page), glyphsPage = v.page != RingPage::Gestures && !quickPage;
    const float g = s.grow >= 0 ? std::clamp(s.grow, 0.f, 1.f) : s.step.centerGrown ? 1.f : 0.f, cr = CenterR + (CenterGrownR - CenterR) * g;
    const bool pointedItem = s.step.under.kind == Kind::Seat && s.step.under.index < v.count;
    Push(out, n, maxShapes, {cx, cy, 2 * cr + 20, 2 * cr + 20, 0, 0, 0, 0, 0, 0, 0, 0, .75f * A, RecipeDisc, 10});
    Push(out, n, maxShapes, {cx, cy, 2 * cr, 2 * cr, 0, 0, 0, 0, 0, Gold.r, Gold.g, Gold.b, A, RecipeRing, 2});
    Push(out, n, maxShapes, {cx, cy, 2 * (cr - 11), 2 * (cr - 11), 0, 0, 0, 0, 0, Gold.r, Gold.g, Gold.b, .8f * A, RecipeRing, 1.5f});
    const bool quickSplit = v.page == RingPage::Quick;   // Quick cast's page: Back above, Move below
    if ((glyphsPage || quickSplit) && !pointedItem) {   // the split: hairline and its 6 px diamond
        Push(out, n, maxShapes, {cx, cy, 2 * (cr - 15), 2, 0, 0, 0, 0, 0, Gold.r, Gold.g, Gold.b, A, RecipeRect, 0});
        Push(out, n, maxShapes, {cx, cy, 6, 6, Pi / 4, 0, 0, 0, 0, GoldLight.r, GoldLight.g, GoldLight.b, A, RecipeRect, 0});
    }
    const int count = std::min(v.count, MaxItems);
    const float ring = glyphsPage ? GlyphR : quickPage ? QuickR : GestureR;
    for (int i = 0; i < count && n < maxShapes - 5; ++i) {
        const size_t k = size_t(i);
        float x, y; RingPos(i, count, ring, x, y); x += CanvasX; y += CanvasY;
        const bool pointed = pointedItem && s.step.under.index == i;
        const Float f = s.anim.floats && !pointed ? FloatAt(1000 + i, s.tMs) : Float{};
        x += f.dx; y += f.dy;
        if (glyphsPage) {   // a dark round face, the shape in gold light (strokes below), its skill outside at r 262
            const float base = GlyphIconPx * (pointed ? 1.15f : 1.f);
            const bool painted = i < glyphart::Shapes && s.glyph.Shows(i);   // Glyph icon: the shape's picture in place of the face and its strokes
            const float size = painted ? base * s.glyph.Scale() : base;
            Push(out, n, maxShapes, {x, y, size + 10, size + 10, 0, 0, 0, 0, 0, Gold.r, Gold.g, Gold.b, .18f * A, RecipeDisc, 8});   // faint glow
            if (painted) PushArtSeat(out, n, maxShapes, s.glyph.cell[size_t(i)], s.atlasPct, x, y, std::max(4.f, size - 7.f), Gold, 2.f, A, 1.f, s.glyph.mode == int(glyphart::Mode::Square) ? RecipeSquare : RecipeSeat);
            else {
                Push(out, n, maxShapes, {x, y, size, size, 0, 0, 0, 0, 0, .06f, .05f, .04f, .92f * A, RecipeDisc, 1.5f});
                Push(out, n, maxShapes, {x, y, size, size, 0, 0, 0, 0, 0, Gold.r, Gold.g, Gold.b, A, RecipeRing, 2});
            }
            if (pointed) Push(out, n, maxShapes, {x, y, size + 12, size + 12, 0, 0, 0, 0, 0, HoverRing.r, HoverRing.g, HoverRing.b, A, RecipeRing, 3});
            float sx, sy; RingPos(i, count, GlyphSkillR, sx, sy); sx += CanvasX + f.dx; sy += CanvasY + f.dy;
            const float sa = A * (v.follows[k] ? .65f : 1.f);
            if (v.has[k] && v.cell[k] >= 0) {
                Instance ic{sx, sy, GlyphSkillPx, GlyphSkillPx, 0};
                CellRect(v.cell[k], s.atlasPct, ic.u0, ic.v0, ic.u1, ic.v1);
                ic.r = Gold.r; ic.g = Gold.g; ic.b = Gold.b; ic.a = sa; ic.recipe = RecipeIcon; ic.param = 2 + IconDisc;
                Push(out, n, maxShapes, ic);
            } else if (v.has[k]) {   // a skill with no icon in the atlas: a filled disc in its rim
                Push(out, n, maxShapes, {sx, sy, GlyphSkillPx, GlyphSkillPx, 0, 0, 0, 0, 0, .16f, .12f, .09f, sa, RecipeDisc, 1.5f});
                Push(out, n, maxShapes, {sx, sy, GlyphSkillPx, GlyphSkillPx, 0, 0, 0, 0, 0, Gold.r, Gold.g, Gold.b, sa, RecipeRing, 2});
            } else Push(out, n, maxShapes, {sx, sy, GlyphSkillPx, GlyphSkillPx, 0, 10, 0, 0, 0, Muted.r, Muted.g, Muted.b, .6f * A, RecipeRing, 2});   // a socket: dashed 6 / 4, 2 px, muted 60% (RS5)
            if (v.follows[k]) Push(out, n, maxShapes, {sx + 20, sy + 20, 22, 22, 0, 0, 0, 0, 0, .08f, .07f, .06f, .9f * A, RecipeDisc, 1.5f});   // the "R" pill, lower right (RS3)
        } else if (quickPage) {   // Quick cast's place: what it holds (its icon, else a disc in its rim), or an empty socket (dashed, muted)
            const float size = QuickIconPx * (pointed ? 1.2f : 1.f);
            if (v.has[k] && v.cell[k] >= 0) {
                Instance ic{x, y, size, size, f.rot};
                CellRect(v.cell[k], s.atlasPct, ic.u0, ic.v0, ic.u1, ic.v1);
                const float ia = v.dim[k] ? .45f : 1.f;   // a tier not carried: gray at 45% (as a skill with no points)
                if (v.dim[k]) Push(out, n, maxShapes, {x, y, size, size, 0, 0, 0, 0, 0, 0, 0, 0, A, RecipeDisc, 1.5f});
                ic.r = (v.dim[k] ? Muted : GoldLight).r; ic.g = (v.dim[k] ? Muted : GoldLight).g; ic.b = (v.dim[k] ? Muted : GoldLight).b; ic.a = A * ia; ic.recipe = RecipeIcon; ic.param = v.dim[k] ? -2 - IconDisc : 2 + IconDisc;
                Push(out, n, maxShapes, ic);
            } else if (v.has[k]) {
                const float ia = v.dim[k] ? .45f : 1.f;
                Push(out, n, maxShapes, {x, y, size, size, 0, 0, 0, 0, 0, .16f, .12f, .09f, A * ia, RecipeDisc, 1.5f});
                Push(out, n, maxShapes, {x, y, size, size, 0, 0, 0, 0, 0, Gold.r, Gold.g, Gold.b, A * ia, RecipeRing, 2});
            } else Push(out, n, maxShapes, {x, y, size * .8f, size * .8f, 0, 10, 0, 0, 0, Muted.r, Muted.g, Muted.b, .6f * A, RecipeRing, 2});
            if (v.best[k]) Push(out, n, maxShapes, {x + 24, y + 24, 66, 26, 0, 0, 0, 0, 0, .08f, .07f, .06f, .92f * A, RecipeRect, 0});   // the "Best" pill, lower right
            if (i == v.moveFrom) Push(out, n, maxShapes, {x, y, size + 22, size + 22, 0, 0, 0, 0, 0, GoldLight.r, GoldLight.g, GoldLight.b, A, RecipeRing, 2.5f});   // Move: the place being moved, an outer gold ring
            if (pointed) Push(out, n, maxShapes, {x, y, size + 12, size + 12, 0, 0, 0, 0, 0, HoverRing.r, HoverRing.g, HoverRing.b, A, RecipeRing, 3});
        } else {   // a gesture: its skill's icon; Off: dimmed with an "Off" pill (never color alone); no points (
            // item 6): gray at upstream's 45% brightness (interaction_ui.cpp icon) over a black disc, no pill (its line says "no points yet")
            const bool noPoints = !v.has[k] && v.on[k], lit = v.on[k] && !noPoints;
            const float size = GestureIconPx * (pointed ? 1.3f : 1.f), a = A * (noPoints ? NoPointsDim : v.on[k] ? 1.f : .5f);
            const Rgb rim = lit ? GoldLight : Muted;
            if (v.cell[k] >= 0) {
                if (noPoints) Push(out, n, maxShapes, {x, y, size, size, 0, 0, 0, 0, 0, 0, 0, 0, A, RecipeDisc, 1.5f});
                Instance ic{x, y, size, size, f.rot};
                CellRect(v.cell[k], s.atlasPct, ic.u0, ic.v0, ic.u1, ic.v1);
                ic.r = rim.r; ic.g = rim.g; ic.b = rim.b; ic.a = a; ic.recipe = RecipeIcon; ic.param = lit ? 2 + IconDisc : -2 - IconDisc;   // Off or no points: gray (RS1)
                Push(out, n, maxShapes, ic);
            } else {
                Push(out, n, maxShapes, {x, y, size, size, 0, 0, 0, 0, 0, .16f, .12f, .09f, a, RecipeDisc, 1.5f});
                Push(out, n, maxShapes, {x, y, size, size, 0, 0, 0, 0, 0, rim.r, rim.g, rim.b, a, RecipeRing, 2});
            }
            if (!v.on[k]) Push(out, n, maxShapes, {x + 20, y + 20, 34, 22, 0, 0, 0, 0, 0, .08f, .07f, .06f, .92f * A, RecipeRect, 0});
            if (pointed) Push(out, n, maxShapes, {x, y, size + 12, size + 12, 0, 0, 0, 0, 0, HoverRing.r, HoverRing.g, HoverRing.b, A, RecipeRing, 3});
        }
    }
    PushFooter(s, std::clamp(s.wheelAlpha, 0.f, 1.f), out, n, maxShapes);   // 
    const int max = n + maxText;
    if (glyphsPage) {   // the shapes' strokes: 3 px gold light on a 5 px dark underlay
        const auto& strokes = ShapeStrokes();
        for (int pass = 0; pass < 2; ++pass)
            for (int i = 0; i < count && i < glyphs::ShapeCount; ++i) {
                if (i < glyphart::Shapes && s.glyph.Shows(i)) continue;   // painted: no strokes
                float x, y; RingPos(i, count, GlyphR, x, y); x += CanvasX; y += CanvasY;
                const bool pointed = pointedItem && s.step.under.index == i;
                const Float f = s.anim.floats && !pointed ? FloatAt(1000 + i, s.tMs) : Float{};
                const float k = GlyphIconPx * (pointed ? 1.15f : 1.f) * .55f;
                auto P = [&](const glyphs::Point& p, float& px, float& py) { px = x + f.dx + (p.x - .5f) * k; py = y + f.dy - (p.y - .5f) * k; };
                for (size_t j = 1; j <= 12; ++j) {
                    float x0, y0, x1, y1; P(strokes[size_t(i)][j - 1], x0, y0); P(strokes[size_t(i)][j], x1, y1);
                    if (pass == 0) PushStroke(out, n, max, x0, y0, x1, y1, 5, {0, 0, 0}, .7f * A);
                    else PushStroke(out, n, max, x0, y0, x1, y1, 3, GoldLight, A);
                }
            }
    }
    if (!s.metrics || !s.metrics->ok) return n;
    const auto& m = *s.metrics;
    FooterText(s, m, std::clamp(s.wheelAlpha, 0.f, 1.f), out, n, max);
    if (!v.title.empty()) EmitArc(m, text::Name24, v.title, cx, cy, 304, -Pi / 2, GoldLight, A, out, n, max);
    for (int i = 0; i < count; ++i) {
        float x, y; RingPos(i, count, glyphsPage ? GlyphSkillR : quickPage ? QuickR : GestureR, x, y); x += CanvasX; y += CanvasY;
        if (quickPage) {   // an empty place: its number; a kind: "Best" on its pill
            if (!v.has[size_t(i)]) EmitLine(m, text::Body22, std::to_string(v.clock ? (i == 0 ? 12 : i) : i + 1), x, y, Muted, .8f * A, out, n, max);   // Fixed places: the clock's hour
            else if (v.best[size_t(i)]) EmitLine(m, text::Bold22, "Best", x + 24, y + 24, Parchment, A, out, n, max);
            continue;
        }
        if (!glyphsPage && !v.on[size_t(i)]) EmitLine(m, text::Bold22, "Off", x + 20, y + 20, Muted, A, out, n, max);
        if (glyphsPage && v.follows[size_t(i)]) EmitLine(m, text::Bold22, "R", x + 20, y + 20, Parchment, A, out, n, max);
    }
    if (pointedItem) {   // the pointed item's lines, wrapped to the grown circle (BlurbCache), once the circle is 55% grown
        if (s.blurb && s.step.blurb >= 0) {
            const float ta = A * Ease((g - .55f) / .45f);
            const int lines = int(s.blurb->lines.size());
            for (int k = 0; k < lines; ++k) {
                const auto& ln = s.blurb->lines[size_t(k)];
                const Rgb c = ln.kind == text::KName ? GoldLight : ln.kind == text::KHelp ? Muted : Parchment;
                EmitLine(m, ln.style, ln.text, cx, cy + (k - (lines - 1) / 2.f) * text::LineH, c, ta, out, n, max);
            }
        }
    } else if (glyphsPage) {   // the split (kit centerSplit): Back with its arrow, the other hand with its hand sigil
        const int pointed = s.step.under.kind == Kind::Back ? 1 : s.step.under.kind == Kind::Hand ? 2 : 0;
        DrawSplit(m, {v.top.c_str(), Sigil::Back, "To the wheel", "Trigger to go back"}, {v.bottom.c_str(), Sigil::Hand, v.bottomCount.c_str(), "Trigger to open"},
                  pointed, g, A, out, n, max, &s);
    } else if (quickSplit) {   // Quick cast: Back with its arrow and the count; Move below, costing no seat on any chooser
        const int pointed = s.step.under.kind == Kind::Back ? 1 : s.step.under.kind == Kind::Hand ? 2 : 0;
        DrawSplit(m, {v.top.c_str(), Sigil::Back, v.below.c_str(), "Trigger to go back"}, {v.bottom.c_str(), Sigil::Star, v.bottomCount.c_str(), v.bottomHelp.c_str()},
                  pointed, g, A, out, n, max, &s);
    } else {   // Gestures and Quick cast's kind page: one Back (its arrow above it at rest), "N of M on" / "N of 12 filled"; pointed: "Trigger to go back"
        const bool on = s.step.under.kind == Kind::Back;
        if (!on) PushSigilOrArt(&s, Sigil::Back, out, n, max, cx, cy - 48, Gold, A);
        EmitLine(m, on ? text::Label26 : text::Label22, v.top, cx, cy - (on ? 26 : 12), on ? HoverRing : GoldLight, A, out, n, max);
        EmitLine(m, text::Body22, v.below, cx, cy + (on ? 0 : 20), Muted, A, out, n, max);
        if (on) EmitLine(m, text::Bold22, "Trigger to go back", cx, cy + 26, Parchment, A * Ease((g - .55f) / .45f), out, n, max);
    }
    return n;
}
}  // namespace framevr::ringwheel
