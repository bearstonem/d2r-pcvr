// Quick cast radial: the engine's layout for N = 1..12, the stick as a clock
// (nearest item, dead zone, hysteresis, center hold), hold / release / cancel, Toggle, the ring menu's place edits, the
// per-character text, the bind, the draw list and the atlas regions (steamvr/quick_radial.h, quick_radial_atlas.h).
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include "quick_radial.h"
#include "quick_radial_atlas.h"
#include "quick_cast.h"
#include "skill_slots.h"

using namespace framevr::quickradial;
namespace ia = framevr::interaction;
static int checks = 0;
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); return 1; } ++checks; } while (0)
static bool Near(float a, float b, float e = 1e-3f) { return std::fabs(a - b) <= e; }

// A press's frames: the radial on slot 2 (a right-hand button), upstream's slot while it is down, the stick.
struct Sim {
    Controller c; Ring ring; Tuning t; FrameIn in; double now = 1000;
    int heldSlot = -2;   // upstream's held slot when it is not the bound one (the same button's grip layer); -2: from in.down
    // Open by: Hold here (sections 3-5b test the hold ring); section 3b tests Toggle, the default. Quick cast
    // layout: Growing here (the packed ring these sections were written for); section 3d tests Fixed places, the default
    Sim() { in.on = true; in.bound = 2; in.hand = 0; in.slotFree = true; in.classOk = true; in.tracked = true; t.centerMs = 120; t.toggle = false; t.fixed = false; }
    FrameOut Step(double dtMs = 11) {
        now += dtMs; in.nowMs = now; in.held = heldSlot != -2 ? heldSlot : in.down ? in.bound : -1;
        return c.Step(in, ring, t);
    }
    void Stick(float x, float y) { in.stick[0] = x; in.stick[1] = y; }
    FrameOut Press() { in.down = true; return Step(); }
    FrameOut Release() { in.down = false; return Step(); }
    void Fill(int n) { for (int i = 0; i < n; ++i) ring.Add({100 + i, -1}); }
};

int main() {
    // 1. Layout: N items split the circle into N equal parts, item 1 at 12 o'clock, clockwise; N = 1..12.
    {
        Tuning t;
        for (int n = 1; n <= MaxItems; ++n) {
            for (int i = 0; i < n; ++i) {
                CHECK(Near(ItemClock(i, n), 360.f * float(i) / float(n)));
                CHECK(NearestItem(ItemClock(i, n), n) == i);                   // each item's own angle picks it
                if (n > 1) CHECK(Near(ClockDiff(ItemClock(i, n), ItemClock((i + 1) % n, n)), 360.f / float(n)));   // even spacing
            }
            const Vec2f first = AtClock(ItemClock(0, n), 10);
            CHECK(Near(first.x, 0) && Near(first.y, 10));                      // item 1 straight up
            if (n >= 3) { const Vec2f second = AtClock(ItemClock(1, n), 10); CHECK(second.x > 0); }   // clockwise: item 2 to the right
            const float icon = IconDegFor(n, t);
            CHECK(icon > 0 && icon <= t.iconDeg);
            if (n > 1) CHECK(icon < 2 * t.radiusDeg * std::sin(3.14159265f / float(n)));   // neighbors never touch
        }
        CHECK(Near(IconDegFor(4, t), t.iconDeg) && IconDegFor(12, t) < t.iconDeg);   // few: full size; 12: shrunk to fit
        for (float c = 0; c < 360; c += 7.5f) CHECK(NearestItem(c, 1) == 0);        // one item fills the whole circle
        CHECK(NearestItem(10, 0) == -1);
        CHECK(Near(ClockOf(0, 1), 0) && Near(ClockOf(1, 0), 90) && Near(ClockOf(0, -1), 180) && Near(ClockOf(-1, 0), 270));
        CHECK(NearestItem(ClockOf(-1, 0), 4) == 3);    // stick left on 4 items: the 9 o'clock item
        CHECK(NearestItem(ClockOf(-1, 0), 12) == 9);   // on 12: the 9 o'clock item
        CHECK(NearestItem(ClockOf(1, -1), 4) == 1 || NearestItem(ClockOf(1, -1), 4) == 2);   // 4:30 sits on the line (either neighbor)
        CHECK(NearestItem(ClockOf(1, -.9f), 4) == 1);  // a little above it: 3 o'clock
    }
    // 2. The stick as a clock: dead zone, hysteresis at the half-way line (capped at 40% of the spacing), center hold.
    {
        Tuning t; t.deadZone = .35f; t.hysteresisDeg = 8; t.centerMs = 120;
        Selector s;
        CHECK(!s.Step(.2f, 0, 4, 0, t) && s.pick == -1);           // inside the dead zone: nothing
        CHECK(s.Step(-1, 0, 4, 10, t) && s.pick == 3);             // left: 9 o'clock
        const auto at = [](float clock) { const float a = clock * 0.01745329f; return Vec2f{std::sin(a), std::cos(a)}; };
        s.Reset(); s.Step(at(10).x, at(10).y, 4, 0, t); CHECK(s.pick == 0);
        Vec2f p = at(50); CHECK(!s.Step(p.x, p.y, 4, 1, t) && s.pick == 0);   // 5 deg past the 45 deg line: held (8 deg hysteresis)
        p = at(52.5f); CHECK(!s.Step(p.x, p.y, 4, 2, t) && s.pick == 0);
        p = at(54); CHECK(s.Step(p.x, p.y, 4, 3, t) && s.pick == 1);          // 9 deg past: the neighbor
        p = at(41); CHECK(!s.Step(p.x, p.y, 4, 4, t) && s.pick == 1);          // and back: held by the neighbor now (no flicker)
        Selector fresh; p = at(50); fresh.Step(p.x, p.y, 4, 0, t); CHECK(fresh.pick == 1);   // no pick yet: the nearest
        Tuning big = t; big.hysteresisDeg = 45;                     // capped at 40% of 30 deg on 12 items
        Selector c; p = at(0); c.Step(p.x, p.y, 12, 0, big); CHECK(c.pick == 0);
        p = at(30); c.Step(p.x, p.y, 12, 1, big); CHECK(c.pick == 1);          // the neighbor's own center always moves it
        CHECK(Near(HysteresisFor(12, big), 12) && Near(HysteresisFor(4, t), 8));
        Tuning off = t; off.hysteresisDeg = 0;                       // Off: the nearest at once
        Selector o; p = at(44); o.Step(p.x, p.y, 4, 0, off); CHECK(o.pick == 0);
        p = at(46); o.Step(p.x, p.y, 4, 1, off); CHECK(o.pick == 1);
        // Center hold: back in the dead zone the pick stays centerMs, then clears; 80% of the zone once picked.
        Selector h; h.Step(-1, 0, 4, 0, t); CHECK(h.pick == 3);
        CHECK(!h.Step(-.3f, 0, 4, 10, t) && h.pick == 3);                     // .30 > 80% of .35: still out
        CHECK(!h.Step(0, 0, 4, 20, t) && h.pick == 3);                        // centered, 0 ms
        CHECK(!h.Step(0, 0, 4, 139, t) && h.pick == 3);                       // 119 ms
        CHECK(h.Step(0, 0, 4, 141, t) && h.pick == -1);                       // 121 ms: the middle
        Tuning now = t; now.centerMs = 0;
        Selector z; z.Step(-1, 0, 4, 0, now); CHECK(z.Step(0, 0, 4, 1, now) && z.pick == -1);   // Off: the middle at once
        Tuning any = t; any.deadZone = 0;                             // Off: any push picks
        Selector a; a.Step(-.05f, 0, 4, 0, any); CHECK(a.pick == 3);
        Selector none; CHECK(!none.Step(-1, 0, 0, 0, t) && none.pick == -1);   // no items: nothing
    }
    // 3. Hold opens, release on an item casts (once, its ring index), release in the middle cancels; the press is the radial's.
    {
        Sim s; s.Fill(4);
        auto o = s.Press();
        CHECK(o.event == Event::Opened && o.mute && o.ownStick && s.c.Open() && s.c.shown.n == 4);
        s.Stick(-1, 0); o = s.Step();
        CHECK(o.tick && s.c.sel.pick == 3 && o.mute);
        o = s.Step(); CHECK(!o.tick);                                   // one tick a new pick
        o = s.Release();
        CHECK(o.event == Event::Cast && o.index == 3 && !s.c.Open() && o.mute);
        o = s.Step(); CHECK(o.event == Event::None && o.ownStick);      // the stick still pushed: the ring's until it comes back
        s.Stick(0, 0); o = s.Step(); CHECK(!o.ownStick);                // back in the middle: the game's again
        // the middle: nothing
        o = s.Press(); CHECK(o.event == Event::Opened);
        o = s.Release(); CHECK(o.event == Event::Canceled && o.why == Why::Center);
        // a flick let go as the thumb comes back (inside the center hold) still casts; after it, nothing
        s.Press(); s.Stick(0, 1); s.Step(); s.Stick(0, 0); s.Step(40);
        o = s.Release(); CHECK(o.event == Event::Cast && o.index == 0);
        s.Press(); s.Stick(0, 1); s.Step(); s.Stick(0, 0); s.Step(40); s.Step(130);
        o = s.Release(); CHECK(o.event == Event::Canceled && o.why == Why::Center);
        // empty ring: opens (the Empty hint), a release casts nothing
        Sim e; o = e.Press(); CHECK(o.event == Event::Opened && e.c.shown.n == 0);
        const auto d = e.c.DrawNow(e.t); CHECK(d.n == 2 && d.q[1].rect.y == atlas::LabelCell(atlas::LabelEmpty).r.y && d.q[1].region == atlas::LabelCell(atlas::LabelEmpty).region);
        o = e.Release(); CHECK(o.event == Event::Canceled && o.why == Why::Empty);
        CHECK(std::string(EntryName) == "Quick cast");   // one quick cast, one name in the ring menu
        // one item fills the circle: any push picks it
        Sim one; one.Fill(1); one.Press(); one.Stick(.7f, -.7f); one.Step(); o = one.Release(); CHECK(o.event == Event::Cast && o.index == 0);
    }
    // 3b. Open by: Toggle (the pick follows the stick round the clock, back in the middle casts the item picked then and shuts; a tap
    // shuts with nothing cast; back in the middle with nothing picked casts nothing; from an empty place: two pulses, still open).
    {
        CHECK(Tuning{}.toggle && Tuning{}.closeAfterCast);   // the defaults
        Sim s; s.t.toggle = true; s.Fill(4);
        auto o = s.Press();
        CHECK(o.event == Event::Opened && o.mute && o.ownStick && s.c.Open());
        o = s.Step(300); CHECK(o.event == Event::None && s.c.Open() && o.mute);       // held: open, its press muted
        o = s.Release(); CHECK(o.event == Event::None && s.c.Open() && !o.mute && o.ownStick);   // let go: still open; the hand's other presses are the game's
        o = s.Step(2000); CHECK(o.event == Event::None && s.c.Open());                // the stick never left the middle: nothing, still open
        // the pick follows the stick round the clock (sweeping is free), ticks on each new pick
        s.Stick(-1, 0); o = s.Step(); CHECK(o.tick && s.c.sel.pick == 3 && o.event == Event::None);
        s.Stick(0, 1); o = s.Step(); CHECK(o.tick && s.c.sel.pick == 0 && o.event == Event::None);
        s.Stick(1, 0); o = s.Step(); CHECK(o.tick && s.c.sel.pick == 1 && o.event == Event::None);
        // back in the middle: the pick holds through Center hold (a sweep across the middle casts nothing), then it is cast
        s.Stick(0, 0); o = s.Step(); CHECK(o.event == Event::None && s.c.Open() && s.c.sel.pick == 1);
        o = s.Step(60); CHECK(o.event == Event::None && s.c.Open());
        o = s.Step(70); CHECK(o.event == Event::Cast && o.index == 1 && !s.c.Open());   // 130 ms > 120: cast, shut (Close after cast)
        o = s.Step(); CHECK(o.event == Event::None && !s.c.Open() && !o.ownStick);
        // a sweep through the middle quicker than Center hold keeps choosing: the item where it stops is cast
        s.Press(); s.Release();
        s.Stick(-1, 0); s.Step(); s.Stick(0, 0); s.Step(40); s.Stick(0, -1); o = s.Step(); CHECK(o.event == Event::None && s.c.sel.pick == 2);
        s.Stick(0, 0); s.Step(); o = s.Step(130); CHECK(o.event == Event::Cast && o.index == 2);
        // Center hold Off: the cast as the stick reaches the middle
        Sim z; z.t.toggle = true; z.t.centerMs = 0; z.Fill(4); z.Press(); z.Release();
        z.Stick(-1, 0); z.Step(); z.Stick(0, 0); o = z.Step(); CHECK(o.event == Event::Cast && o.index == 3 && !z.c.Open());
        // a tap of its button shuts it, nothing cast, even with an item picked; that press is muted until it comes up
        s.Press(); s.Release(); s.Stick(-1, 0); s.Step();
        o = s.Press(); CHECK(o.event == Event::Canceled && o.why == Why::Closed && !s.c.Open() && o.mute);
        s.Stick(0, 0);
        o = s.Step(400); CHECK(o.event == Event::None && !s.c.Open() && o.mute);      // still held: muted, never reopens
        o = s.Release(); CHECK(!o.mute && !s.c.Open());
        o = s.Press(); CHECK(o.event == Event::Opened); s.Release();                   // a fresh tap opens again
        o = s.Press(); CHECK(o.event == Event::Canceled && o.why == Why::Closed); s.Release();
        // Close after cast Off: it stays open for the next cast
        Sim m; m.t.toggle = true; m.t.closeAfterCast = false; m.Fill(4); m.Press(); m.Release();
        m.Stick(-1, 0); m.Step(); m.Stick(0, 0); m.Step(); o = m.Step(130); CHECK(o.event == Event::Cast && o.index == 3 && m.c.Open() && m.c.sel.pick == -1);
        m.Stick(1, 0); m.Step(); m.Stick(0, 0); m.Step(); o = m.Step(130); CHECK(o.event == Event::Cast && o.index == 1 && m.c.Open());
        o = m.Step(1000); CHECK(o.event == Event::None && m.c.Open());                 // nothing more without a new pick
        o = m.Press(); CHECK(o.event == Event::Canceled && o.why == Why::Closed); m.Release();
        // an empty ring: opens (the Empty hint); the stick out and back casts nothing, two pulses, still open
        Sim e; e.t.toggle = true; o = e.Press(); CHECK(o.event == Event::Opened && e.c.shown.n == 0); e.Release();
        e.Stick(0, 1); o = e.Step(); CHECK(o.event == Event::None && !o.emptyBack);
        e.Stick(0, 0); o = e.Step(); CHECK(o.event == Event::None && o.emptyBack && e.c.Open());
        o = e.Step(500); CHECK(!o.emptyBack && e.c.Open());                              // once per push
        o = e.Press(); CHECK(o.event == Event::Canceled && o.why == Why::Closed);
        // open, then a wheel, Off, a potion: shut, nothing cast (as the hold ring)
        Sim w; w.t.toggle = true; w.Fill(3); w.Press(); w.Release(); w.Stick(-1, 0); w.Step(); w.in.wheelOpen = true;
        o = w.Step(); CHECK(o.event == Event::Canceled && o.why == Why::Wheel && !w.c.Open());
        // the trigger stays the game's while it is open
        Sim g; g.t.toggle = true; g.Fill(3); g.Press(); g.Release(); o = g.Step(); CHECK(o.event == Event::None && !o.mute && g.c.Open());   // its press only
        // cast while its button is still held (a Hold habit): shut, and that press stays muted until it comes up
        Sim f; f.t.toggle = true; f.Fill(4); o = f.Press(); CHECK(o.event == Event::Opened);
        f.Stick(-1, 0); f.Step(); f.Stick(0, 0); f.Step(); o = f.Step(130);
        CHECK(o.event == Event::Cast && o.index == 3 && !f.c.Open() && o.mute);
        o = f.Step(); CHECK(o.mute && !f.c.Open()); o = f.Step(400); CHECK(o.mute && o.event == Event::None && !f.c.Open());
        o = f.Release(); CHECK(!o.mute && !f.c.Open());
        o = f.Press(); CHECK(o.event == Event::Opened);                                    // a fresh press opens again
        // the same face button's other layer (a game skill on grip + it) is the game's: no tap, never muted
        Sim a; a.t.toggle = true; a.Fill(4); a.Press(); a.Release();
        a.heldSlot = 5; o = a.Press(); CHECK(o.event == Event::None && a.c.Open() && !o.mute);
        o = a.Step(200); CHECK(!o.mute && a.c.Open()); a.Release(); a.heldSlot = -2;
        o = a.Press(); CHECK(o.event == Event::Canceled && o.why == Why::Closed && o.mute);   // its own slot: the tap
        a.heldSlot = 5; o = a.Step(); CHECK(!o.mute);                                     // closed, still down, grip added: the game's
        a.heldSlot = -2; a.Release();
        // opened with the stick pushed (a trigger-bound ring mid-walk): no pick until the stick is back in the middle once
        Sim b; b.t.toggle = true; b.Fill(4); b.Stick(0, 1); o = b.Press(); CHECK(o.event == Event::Opened && b.c.stickWait); b.Release();
        o = b.Step(); CHECK(b.c.sel.pick == -1 && !o.tick);
        b.Stick(0, 0); b.Step(); o = b.Step(300); CHECK(o.event == Event::None && b.c.Open() && !b.c.stickWait);
        b.Stick(0, 1); o = b.Step(); CHECK(o.tick && b.c.sel.pick == 0);
        b.Stick(0, 0); b.Step(); o = b.Step(130); CHECK(o.event == Event::Cast && o.index == 0);
        // Dead zone 0, a stick that drifts at rest still reaches the middle
        Sim d; d.t.toggle = true; d.t.deadZone = 0; d.Fill(4); d.Press(); d.Release();
        d.Stick(-1, 0); d.Step(); d.Stick(.02f, .01f); d.Step(); o = d.Step(130); CHECK(o.event == Event::Cast && o.index == 3);
        // Open by switched to Hold while open: the open ring keeps Toggle (no cast on the button being up)
        Sim l; l.t.toggle = true; l.Fill(4); l.Press(); l.Release(); l.Stick(-1, 0); l.Step();
        l.t.toggle = false; o = l.Step(); CHECK(o.event == Event::None && l.c.Open());
        o = l.Press(); CHECK(o.event == Event::Canceled && o.why == Why::Closed); l.Release();
    }
    // 3c. The ring menu's Quick cast page : an item put in
    // a place, or cleared; a place past the last fills the next; an item on the ring already trades places (never twice).
    {
        Ring r; for (int id : {10, 11, 12}) r.Add({id, -1});
        CHECK(PutAt(r, 1, {20, -1}) && Encode(r) == "10,20,12");             // replaces place 2
        CHECK(PutAt(r, 7, {21, -1}) && Encode(r) == "10,20,12,21");          // past the last: the next place
        CHECK(PutAt(r, 0, {12, -1}) && Encode(r) == "12,20,10,21");          // on the ring already: trades places
        CHECK(PutAt(r, 9, {20, -1}) && Encode(r) == "12,10,21,20");          // on the ring, an empty place: it goes last
        CHECK(!PutAt(r, 3, {20, -1}) && Encode(r) == "12,10,21,20");         // where it is: nothing
        CHECK(PutAt(r, 3, {20, 7}) && r.items[3].owner == 7);                // the same skill, a new owner (an item skill's)
        CHECK(!PutAt(r, -1, {30, -1}) && !PutAt(r, MaxItems, {30, -1}) && !PutAt(r, 0, {-1, -1}) && r.n == 4);
        CHECK(ClearAt(r, 1) && Encode(r) == "12,21,20" && !ClearAt(r, 3) && !ClearAt(r, -1));
        Ring full; for (int i = 0; i < MaxItems; ++i) full.Add({300 + i, -1});
        CHECK(PutAt(full, 11, {400, -1}) && full.items[11].skill == 400 && full.n == MaxItems);   // full: a place's item replaced
        CHECK(!PutAt(full, 11, {400, -1}));
        const int fam = FamilyId(FamHealth);
        CHECK(PutAt(full, 0, {fam, -1}) && full.items[0].skill == fam);
        for (const int f : {FamHealth, FamMana, FamRejuv, FamAntidote, FamThawing, FamStamina, FamPortal, FamIdentify}) CHECK(std::strlen(FamilyInitials(f)) == 2);
    }
    // 3d. Quick cast layout (an item at number 9 and 3, not locked in as item 1 and 2):
    // Fixed places, the default: 12 clock places, an item exactly where it is put, Clear empties one place and nothing else
    // moves, Move takes a place's item to another (a filled one: they swap), the stick never picks an empty place, the save keeps
    // the gaps and a save from before loads into places 1..n. Growing keeps the packed ring (sections 3 to 3c).
    {
        CHECK(Tuning{}.fixed);   // the default
        CHECK(PlaceNumber(0, true) == 12 && PlaceNumber(3, true) == 3 && PlaceNumber(9, true) == 9 && PlaceNumber(0, false) == 1 && PlaceNumber(11, false) == 12);
        const int heal = FamilyId(FamHealth);
        // place: 3 o'clock and 9 o'clock on an empty ring; the places between stay empty
        Ring r;
        CHECK(PutAt(r, 3, {54, -1}, true) && PutAt(r, 9, {heal, -1}, true));
        CHECK(r.n == 10 && r.Count() == 2 && r.Filled(3) && r.Filled(9) && !r.Filled(0) && !r.Filled(4) && !r.Filled(11));
        CHECK(Encode(r) == "-,-,-,54,-,-,-,-,-,f:1");
        CHECK(PutAt(r, 3, {59, -1}, true) && r.items[3].skill == 59 && !r.Has(54) && !PutAt(r, 3, {59, -1}, true));   // replaced in place; again: nothing
        CHECK(PutAt(r, 6, {59, -1}, true) && r.Filled(6) && !r.Filled(3) && Encode(r) == "-,-,-,-,-,-,59,-,-,f:1");   // on the ring, an empty place: it moves there
        CHECK(PutAt(r, 9, {59, -1}, true) && r.items[9].skill == 59 && r.items[6].skill == heal);                    // a filled place: the two trade
        CHECK(!PutAt(r, -1, {30, -1}, true) && !PutAt(r, MaxItems, {30, -1}, true) && !PutAt(r, 0, {-1, -1}, true) && r.Count() == 2);
        // clear: that place only; trailing empty places go; an empty place: nothing
        CHECK(ClearAt(r, 6, true) && r.n == 10 && r.Count() == 1 && r.items[9].skill == 59 && !ClearAt(r, 6, true) && !ClearAt(r, 0, true) && !ClearAt(r, -1, true));
        CHECK(ClearAt(r, 9, true) && r.n == 0 && Encode(r).empty());
        // move: to an empty place (exactly there), to a filled one (swap); from an empty place, onto itself, off the clock: nothing
        Ring m; CHECK(PutAt(m, 1, {10, -1}, true) && PutAt(m, 2, {11, -1}, true));
        CHECK(MoveTo(m, 1, 9, true) && Encode(m) == "-,-,11,-,-,-,-,-,-,10");
        CHECK(MoveTo(m, 9, 2, true) && m.items[2].skill == 10 && m.items[9].skill == 11);
        CHECK(!MoveTo(m, 5, 2, true) && !MoveTo(m, 2, 2, true) && !MoveTo(m, 2, MaxItems, true) && !MoveTo(m, 2, -1, true) && !MoveTo(m, -1, 2, true));
        CHECK(MoveTo(m, 9, 0, true) && m.n == 3 && Encode(m) == "11,-,10");   // the last one moved down: the ring's span shrinks
        // Growing's Move: a filled place swaps; an empty place: it goes last (no gaps); already last: nothing
        Ring g; for (int id : {10, 11, 12}) g.Add({id, -1});
        CHECK(MoveTo(g, 0, 2) && Encode(g) == "12,11,10" && MoveTo(g, 0, 7) && Encode(g) == "11,10,12" && !MoveTo(g, 2, 9) && !MoveTo(g, 5, 0));
        // the save: gaps round-trip; a save from before (no gaps) loads into places 1..n as it was; trailing empties trimmed
        Ring back; CHECK(Decode("-,-,-,54,-,-,-,-,-,f:1", back) && back.n == 10 && back.Count() == 2 && back.items[3].skill == 54 && back.items[9].skill == heal);
        CHECK(Encode(back) == "-,-,-,54,-,-,-,-,-,f:1");
        Ring old; CHECK(Decode("64,54,i:rvs", old) && old.n == 3 && old.Count() == 3 && old.items[0].skill == 64 && old.items[1].skill == 54 && old.items[2].skill == ExactId(C3('r', 'v', 's')));
        CHECK(EntriesFor(Mode::Cast, old, true).Holds(0) && EntriesFor(Mode::Cast, old, true).Holds(2) && !EntriesFor(Mode::Cast, old, true).Holds(3));
        Ring t3; CHECK(Decode("-,-,54,-", t3) && t3.n == 3 && Decode("-", t3) && t3.n == 0 && Decode(" - , 54 ", t3) && t3.n == 2 && t3.items[1].skill == 54);
        Ring keep = old;
        for (const char* bad : {"-3", "--", "-,54,54", "-,-,-,-,-,-,-,-,-,-,-,-,1", "-,", "-x"}) { CHECK(!Decode(bad, keep)); CHECK(keep == old); }
        // Growing over a ring Fixed left with gaps: its items in order, no gaps (the radial's entries; an edit packs it first)
        CHECK(EntriesFor(Mode::Cast, back).n == 2 && EntriesFor(Mode::Cast, back).e[0].index == 3 && EntriesFor(Mode::Cast, back).e[1].index == 9);
        Ring gp = back; CHECK(PutAt(gp, 5, {70, -1}) && Encode(gp) == "54,f:1,70");
        Ring gc = back; CHECK(ClearAt(gc, 0) && Encode(gc) == "f:1");
        // Growing's page numbers over the gaps (page place 1 is real place 3, 2 is 9), so an edit that keeps the order is
        // made on the real places and Fixed's places survive (the glue's ApplyEdits; only a move to the end packs)
        CHECK(RealPlace(back, 0) == 3 && RealPlace(back, 1) == 9 && RealPlace(back, 2) == -1 && PackedIndex(back, 9) == 1 && PackedIndex(back, 3) == 0);
        Ring gk = back; CHECK(PutAt(gk, RealPlace(gk, 1), {70, -1}, true) && Encode(gk) == "-,-,-,54,-,-,-,-,-,70");          // a put in a filled place
        Ring gs = back; CHECK(MoveTo(gs, RealPlace(gs, 0), RealPlace(gs, 1), true) && Encode(gs) == "-,-,-,f:1,-,-,-,-,-,54");   // a swap
        Ring ga = back; CHECK(PutAt(ga, ga.n, {70, -1}, true) && Encode(ga) == "-,-,-,54,-,-,-,-,-,f:1,70" && EntriesFor(Mode::Cast, ga).n == 3);   // past the last: last
        Ring gl = back; CHECK(ClearAt(gl, RealPlace(gl, 0), true) && Encode(gl) == "-,-,-,-,-,-,-,-,-,f:1" && EntriesFor(Mode::Cast, gl).n == 1);   // a clear: Growing shows 1 item
        // a skill moved in from outside the page (a menu took its button): the first empty place; on the ring or full: no
        Ring a = back; CHECK(a.AddFree({70, -1}) && a.items[0].skill == 70 && a.Count() == 3 && !a.AddFree({70, -1}) && !a.AddFree({-1, -1}));
        Ring full; for (int i = 0; i < MaxItems; ++i) full.Set(i, {300 + i, -1});
        CHECK(full.n == MaxItems && !full.AddFree({1, -1}) && full.Count() == MaxItems);
        // the entries: 12 places, the empty ones held as -1, never picked; the draw: a pick on an empty place shows no rim
        const auto es = EntriesFor(Mode::Cast, back, true);
        CHECK(es.n == MaxItems && es.count == 2 && es.fixed && es.Holds(3) && es.Holds(9) && !es.Holds(0) && es.e[0].index == -1 && es.e[3].index == 3 && es.e[9].index == 9);
        Tuning tt; Plane pl; pl.dist = 1;
        CHECK(Draw(pl, Mode::Cast, es, 3, tt).n == 3 && Draw(pl, Mode::Cast, es, 0, tt).n == 2);
        const auto none = EntriesFor(Mode::Cast, Ring{}, true);
        CHECK(none.n == MaxItems && none.count == 0 && none.filled == 0);
        const auto dn = Draw(pl, Mode::Cast, none, -1, tt);
        CHECK(dn.n == 2 && dn.q[1].rect.x == atlas::LabelCell(atlas::LabelEmpty).r.x && dn.q[1].rect.y == atlas::LabelCell(atlas::LabelEmpty).r.y && dn.q[1].region == atlas::LabelCell(atlas::LabelEmpty).region);
        // the stick skips the empty places: items at 3 and 9 o'clock only
        Sim s; s.t.fixed = true; s.ring = back;
        CHECK(s.Press().event == Event::Opened && s.c.shown.n == MaxItems && s.c.shown.count == 2);
        s.Stick(0, 1); s.Step(); CHECK(s.c.sel.pick == 3);    // 12 o'clock is empty: the nearest filled (a tie: the lower place)
        s.Stick(-1, 0); s.Step(); CHECK(s.c.sel.pick == 9);   // 9 o'clock
        s.Stick(.2f, 1); s.Step(); CHECK(s.c.sel.pick == 3);  // just right of 12: 3 o'clock is nearer by more than the hysteresis
        s.Stick(0, -1); s.Step(); CHECK(s.c.sel.pick == 3);   // 6 o'clock, half-way: the pick holds
        for (int i = 0; i < MaxItems; ++i) { const auto v = AtClock(ItemClock(i, MaxItems), 1.f); s.Stick(v.x, v.y); s.Step(); CHECK(s.c.sel.pick == 3 || s.c.sel.pick == 9); }
        s.Stick(1, 0); s.Step();
        auto o = s.Release(); CHECK(o.event == Event::Cast && o.index == 3);   // the item in place 3 is cast
        // an empty Fixed ring: nothing picked, its release says so
        Sim e; e.t.fixed = true; e.Press(); e.Stick(1, 0); e.Step(); CHECK(e.c.sel.pick == -1);
        o = e.Release(); CHECK(o.event == Event::Canceled && o.why == Why::Empty);
        // all 12 filled: each clock place picks itself, as Growing's 12
        Sim f12; f12.t.fixed = true; for (int i = 0; i < MaxItems; ++i) f12.ring.Set(i, {200 + i, -1});
        f12.Press();
        for (int i = 0; i < MaxItems; ++i) { const auto v = AtClock(ItemClock(i, MaxItems), 1.f); f12.Stick(v.x, v.y); f12.Step(); CHECK(f12.c.sel.pick == i); }
        // an edit while open (a place cleared): the pick on it goes, the ring re-reads its places
        Sim w; w.t.fixed = true; w.ring = back; w.Press(); w.Stick(1, 0); w.Step(); CHECK(w.c.sel.pick == 3);
        CHECK(ClearAt(w.ring, 3, true)); w.Step(); CHECK(w.c.shown.count == 1 && w.c.sel.pick == 9);
        // StepFilled with every item filled is Step's rule (one stick sweep both ways, every frame the same pick)
        {
            framevr::stickselect::Selector a1, b1; const framevr::stickselect::Feel feel{.35f, 8.f, 120.f};
            double ms = 0; bool same = true;
            for (int k = 0; k <= 480; ++k) {
                const float deg = float(k <= 240 ? k * 3 : (480 - k) * 3) + .7f;
                const auto v = AtClock(deg, .9f);
                a1.Step(v.x, v.y, 7, ms, feel); b1.StepFilled(v.x, v.y, 7, 0x7f, ms, feel); ms += 11;
                same = same && a1.pick == b1.pick;
            }
            CHECK(same);
            framevr::stickselect::Selector c1; CHECK(!c1.StepFilled(1, 0, 12, 0, 0, feel) && c1.pick == -1);   // none filled: nothing
        }
    }
    // 4. Who owns the button: not bound, taken by a skill or glyph, Off, an unknown class (the Warlock), a wheel, a potion.
    {
        const auto opens = [](auto edit) { Sim s; s.Fill(3); edit(s); return s.Press().event == Event::Opened; };
        CHECK(opens([](Sim&) {}));
        CHECK(!opens([](Sim& s) { s.in.on = false; }));
        CHECK(!opens([](Sim& s) { s.in.bound = -1; }));
        CHECK(!opens([](Sim& s) { s.in.slotFree = false; }));
        CHECK(!opens([](Sim& s) { s.in.classOk = false; }));             // the Warlock (7) and unknown classes keep upstream
        CHECK(!ClassKnown(7) && !ClassKnown(-1) && ClassKnown(0) && ClassKnown(6));
    { const auto w = ShelfEntryFor(true, 7, 3, 10), z = ShelfEntryFor(true, 1, 0, 10), u = ShelfEntryFor(false, 1, 3, 10); CHECK(w.id == ItemId && w.dim && w.slot == 2 && !z.dim && z.slot == -1 && u.dim); }   // the shelf entry: faint for the Warlock or no hero; its button
        CHECK(!opens([](Sim& s) { s.in.wheelOpen = true; }));
        CHECK(!opens([](Sim& s) { s.in.occupied = true; }));
        CHECK(!opens([](Sim& s) { s.in.tracked = false; }));
        // open, then a wheel / Off / a potion: closed, nothing cast; the button must come up before it opens again
        for (int why = 0; why < 3; ++why) {
            Sim s; s.Fill(3); s.Press(); s.Stick(-1, 0); s.Step();
            if (why == 0) s.in.wheelOpen = true; else if (why == 1) s.in.on = false; else s.in.occupied = true;
            const auto o = s.Step();
            CHECK(o.event == Event::Canceled && !s.c.Open() && o.why == (why == 0 ? Why::Wheel : why == 1 ? Why::Off : Why::Occupied));
            s.in.wheelOpen = false; s.in.on = true; s.in.occupied = false;
            CHECK(s.Step().event == Event::None && !s.c.Open());          // still held: stays shut (waitUp)
            s.Release(); CHECK(s.Press().event == Event::Opened);         // a fresh press opens
        }
        // another slot's press never opens it (upstream's one press at a time: another menu's button held)
        Sim s; s.Fill(3); s.in.down = true; s.now += 11; s.in.nowMs = s.now; s.in.held = 5;
        CHECK(s.c.Step(s.in, s.ring, s.t).event == Event::None && !s.c.Open());
    }
    // 5. The edit ring and its Add page retired: the ring menu's Quick cast page fills the ring (ring-wheel-test,
    // section 3c above: PutAt, ClearAt). The hold ring never edits: no trigger, no Done.
    {
        Sim s; s.Fill(3); s.Press(); s.Stick(-1, 0); s.Step();
        CHECK(s.c.mode == Mode::Cast && s.c.shown.n == 3 && EntriesFor(Mode::Cast, s.ring).n == 3 && EntriesFor(Mode::Closed, s.ring).n == 0);
        const auto o = s.Release(); CHECK(o.event == Event::Cast && Encode(s.ring) == "100,101,102");   // casting never changes the ring
    }
    // 6b. Save: the save starts from a copy of the ini; only "no ini yet" starts fresh; any other copy failure
    // keeps the lines and tries again, ahead of what was queued since (the newer text of a key written last, so it wins).
    {
        CHECK(SaveStartOf(true, false) == SaveStart::Use && SaveStartOf(true, true) == SaveStart::Use);
        CHECK(SaveStartOf(false, true) == SaveStart::Fresh);
        CHECK(SaveStartOf(false, false) == SaveStart::Retry);   // a sharing violation: never a fresh file over the others' rings
        SaveLines queue{{"c1_bob", "5,6"}}, failed{{"c1_bob", "5"}, {"c3_amy", "54"}};
        Requeue(queue, std::move(failed));
        CHECK(queue.size() == 3 && queue[0].second == "5" && queue[1].first == "c3_amy" && queue[2].second == "5,6");
        SaveLines empty; Requeue(empty, {{"k", "1"}}); CHECK(empty.size() == 1);
    }
    // 6. The ring's edits and its text, saved per character.
    {
        Ring r;
        CHECK(r.Add({5, -1}) && r.Add({9, 3}) && !r.Add({5, -1}) && !r.Add({-1, -1}) && r.n == 2);
        CHECK(r.Move(0, 1) && r.items[0].skill == 9 && !r.Move(0, 0) && !r.Move(0, 2));
        CHECK(r.Remove(0) && r.n == 1 && r.items[0].skill == 5 && !r.Remove(1));
        Ring a; for (int id : {64, 54, 59, 53, 47, 58}) a.Add({id, -1});
        const std::string text = Encode(a);
        CHECK(text == "64,54,59,53,47,58");
        Ring b; CHECK(Decode(text.c_str(), b) && b.n == 6 && b.items[0].skill == 64 && b.items[5].skill == 58);
        Ring e; CHECK(Decode("", e) && e.n == 0);   // an empty ring
        Ring keep = b;
        for (const char* bad : {"1,1", "x", "1,", "1;2", "-3", "5000", "1,2,3,4,5,6,7,8,9,10,11,12,13"}) { CHECK(!Decode(bad, keep)); CHECK(keep == b); }
        CHECK(Decode(" 3 , 4 ", e) && e.n == 2);
        CHECK(Key(1, "Frosty") == "class1_Frosty" && Key(1, "Fr o/sty") == "class1_Frosty" && Key(2, "Frosty") != Key(1, "Frosty"));
    }
    // 7. The bind (the ring wheel's hold to assign): upstream's own clear, the slot kept on Applied.
    {
        ia::Request r; r.action = ia::Action::BindSkill; r.skill = ItemId; r.skillOwner = -1; r.index = 2; r.sequence = (1ull << 63) | 7;
        BindWatch w;
        CHECK(w.Take(r) && r.skill == -1 && r.skillOwner == -1 && r.index == 2);
        CHECK(!w.Take(r));
        ia::Request other; other.action = ia::Action::BindSkill; other.skill = 9001;   // another menu's bind id
        CHECK(!w.Take(other) && other.skill == 9001);
        int slot = -1; ia::Reply rep; rep.sequence = r.sequence; rep.result = ia::Result::Pending;
        CHECK(w.Reply(rep, slot) == BindWatch::Done::None);
        rep.result = ia::Result::Applied;
        CHECK(w.Reply(rep, slot) == BindWatch::Done::Applied && slot == 2 && w.Reply(rep, slot) == BindWatch::Done::None);
        CHECK(SlotOfButton(3, framevr::skills::Count) == 2 && SlotOfButton(0, framevr::skills::Count) == -1 && SlotOfButton(11, framevr::skills::Count) == -1);
        CHECK(ItemId != 9001 && ItemId != 9002 && ItemId > 4095);   // never a game skill, never another menu's ids
    }
    // 8. The draw list: the ring, the pick's rim at its item, the middle's label; three quads at most; labels in their cells.
    {
        Tuning t; Ring r; for (int i = 0; i < 6; ++i) r.Add({i, -1});
        Plane p; p.dist = 1;
        const auto es = EntriesFor(Mode::Cast, r);
        auto d = Draw(p, Mode::Cast, es, 2, t);
        CHECK(d.n == 3 && d.q[0].region == atlas::A && d.q[1].rect.w == atlas::RimPx);
        const Vec2f at = AtClock(ItemClock(2, 6), t.radiusDeg);
        CHECK(Near(std::atan(d.q[1].pose[0]) * 57.29578f, at.x, .01f) && Near(std::atan(d.q[1].pose[1]) * 57.29578f, at.y, .01f));
        CHECK(d.q[2].rect.y == atlas::LabelCell(2).r.y);               // the third skill's name
        d = Draw(p, Mode::Cast, es, -1, t);
        CHECK(d.n == 2 && d.q[1].rect.y == atlas::LabelCell(atlas::LabelHint).r.y);
        CHECK(Draw(p, Mode::Closed, es, -1, t).n == 0);
        const auto none = EntriesFor(Mode::Cast, Ring{});
        d = Draw(p, Mode::Cast, none, -1, t);
        CHECK(d.n == 2 && d.q[1].rect.y == atlas::LabelCell(atlas::LabelEmpty).r.y && d.q[1].region == atlas::LabelCell(atlas::LabelEmpty).region);   // empty: where to fill it
        CHECK(LabelOf(es, 5) == 5 && LabelOf(es, 6) == -1 && LabelOf(es, -1) == -1 && atlas::LabelHint >= MaxItems && atlas::LabelEmpty < atlas::LabelCount);
        for (int i = 0; i < atlas::LabelCount; ++i) {
            const auto c = atlas::LabelCell(i);
            CHECK(c.r.x >= 0 && c.r.y >= 0 && c.r.x + c.r.w <= (c.region == atlas::A ? atlas::AW : atlas::BW) && c.r.y + c.r.h <= (c.region == atlas::A ? atlas::AH : atlas::BH));
            if (c.region == atlas::A) CHECK(c.r.y >= atlas::RimRect.y + atlas::RimPx);   // under the ring and the rim
        }
        // the regions sit where nothing upstream paints (static_asserts in quick_radial_atlas.h; values here for the log)
        CHECK(shared::A.x == 1552 && shared::A.y == 1200 && shared::B.x == 1032 && shared::B.y == 1816);
        CHECK(shared::A.x + shared::A.w <= framevr::panelatlas::SharedWidth && shared::A.y + shared::A.h <= framevr::panelatlas::SharedHeight);
        CHECK(MaxQuads == 3);
    }
    // 9. Upstream's quick cast and native slots: untouched (the radial adds no slot and no binding of its own).
    {
        CHECK(framevr::skills::Count == 10 && framevr::skills::TriggerSlot(0) == 0 && framevr::skills::HandSlot(0, 2) == 1);
        framevr::skills::QuickCast q;   // upstream's eight LT shortcuts: the class is upstream's, untouched (quick-cast-test runs it)
        CHECK(q.Capture(true, 1.f, 1) && q.Layer());
    }
    // 11. Items: exact entries use only their code; family entries follow Potion order; belt bottom first, then
    // the inventory; the belt key never reaches a potion above the bottom; counts are belt + inventory; the save text.
    {
        using framevr::interaction::Code;
        const uint32_t hp2 = Code('h', 'p', '2'), hp5 = Code('h', 'p', '5'), hp3 = Code('h', 'p', '3'), rvs = Code('r', 'v', 's'), rvl = Code('r', 'v', 'l');
        const uint32_t vps = Code('v', 'p', 's'), tbk = Code('t', 'b', 'k'), tsc = Code('t', 's', 'c'), mp1 = Code('m', 'p', '1');
        std::array<BeltCol, 4> belt{};
        belt[0] = {{hp2, hp5, 0, 0}, 2};   // a Light Healing at the bottom, a Super above it
        belt[1] = {{rvs, rvl, 0, 0}, 2};
        belt[2] = {{hp3, 0, 0, 0}, 1};
        belt[3] = {{mp1, 0, 0, 0}, 1};
        InvItem inv[3] = {{rvl, 1}, {tbk, 7}, {vps, 0}};
        // exact Rejuvenation: its belt column, never the Full one
        auto p = PickFor(ExactId(rvs), belt, inv, 3, true);
        CHECK(p.ok && p.column == 1 && Code3(p.code) == Code3(rvs) && p.count == 1);
        // exact Full Rejuvenation: above the bottom in the belt, so the inventory's (count 2: belt + inventory)
        p = PickFor(ExactId(rvl), belt, inv, 3, true);
        CHECK(p.ok && p.column == -1 && Code3(p.code) == Code3(rvl) && p.count == 2);
        // Rejuvenation (best): the Full one (inventory) beats the plain one at a belt bottom; weakest first takes the plain one
        p = PickFor(FamilyId(FamRejuv), belt, inv, 3, true);
        CHECK(p.ok && Code3(p.code) == Code3(rvl) && p.column == -1 && p.count == 3);
        p = PickFor(FamilyId(FamRejuv), belt, inv, 3, false);
        CHECK(p.ok && Code3(p.code) == Code3(rvs) && p.column == 1);
        // Healing (best): the Super is only above a bottom: the reachable best is the Greater (hp3) in column 3, counts all three
        p = PickFor(FamilyId(FamHealth), belt, inv, 3, true);
        CHECK(p.ok && Code3(p.code) == Code3(hp3) && p.column == 2 && p.count == 3);
        p = PickFor(FamilyId(FamHealth), belt, inv, 3, false);
        CHECK(p.ok && Code3(p.code) == Code3(hp2) && p.column == 0);
        // exact Super Healing: above the bottom, none in the inventory: dim with its reason, count 1
        p = PickFor(ExactId(hp5), belt, inv, 3, true);
        CHECK(!p.ok && p.count == 1 && std::strstr(p.why, "higher up a belt column") && Code3(p.code) == Code3(hp5));
        // none left: dim, count 0
        p = PickFor(FamilyId(FamStamina), belt, inv, 3, true);
        CHECK(!p.ok && p.count == 0 && std::strstr(p.why, "none left"));
        // a family with nothing carried still has its kind's picture (dim): the weakest potion, the scroll
        CHECK(Code3(p.code) == Code3(vps));
        p = PickFor(FamilyId(FamAntidote), belt, inv, 3, true);
        CHECK(!p.ok && p.count == 0 && Code3(p.code) == Code3(Code('y', 'p', 's')));
        p = PickFor(FamilyId(FamIdentify), belt, inv, 3, true);
        CHECK(!p.ok && p.count == 0 && Code3(p.code) == Code3(Code('i', 's', 'c')));
        for (const int fam : {FamHealth, FamMana, FamRejuv, FamAntidote, FamThawing, FamStamina, FamPortal, FamIdentify})
            CHECK(FamilyPictureCode(fam) != 0 && FamilyOf(FamilyPictureCode(fam)) == fam);
        CHECK(FamilyPictureCode(FamNone) == 0);
        // Town Portal: the tome from the inventory (no scroll in the belt); a belt scroll first when there is one
        p = PickFor(FamilyId(FamPortal), belt, inv, 3, true);
        CHECK(p.ok && Code3(p.code) == Code3(tbk) && p.column == -1 && p.count == 7);
        belt[3] = {{tsc, 0, 0, 0}, 1};
        p = PickFor(FamilyId(FamPortal), belt, inv, 3, true);
        CHECK(p.ok && Code3(p.code) == Code3(tsc) && p.column == 3 && p.count == 8);
        // families and ranks from the game's codes
        CHECK(FamilyOf(hp5) == FamHealth && FamilyOf(mp1) == FamMana && FamilyOf(rvl) == FamRejuv && FamilyOf(tsc) == FamPortal && FamilyOf(Code('b', 'o', 'x')) == FamNone);
        CHECK(RankOf(hp5) > RankOf(hp2) && RankOf(rvl) > RankOf(rvs) && RankOf(tbk) > RankOf(tsc));
        CHECK(CodeName(hp5) == "Super Healing Potion" && CodeName(rvl) == "Full Rejuvenation Potion");
        // ids: skills, exact items and families apart; the save text round-trips
        CHECK(IsSkillId(64) && !IsItemId(64) && IsExactId(ExactId(rvs)) && IsFamilyId(FamilyId(FamHealth)) && !IsSkillId(ExactId(rvs)));
        Ring r; CHECK(r.Add({64, -1}) && r.Add({ExactId(rvs), -1}) && r.Add({FamilyId(FamHealth), -1}) && !r.Add({ExactId(rvs), -1}));
        CHECK(Encode(r) == "64,i:rvs,f:1");
        Ring back; CHECK(Decode("64,i:rvs,f:1", back) && back == r);
        CHECK(!Decode("i:", back) && !Decode("f:9", back) && !Decode("64,i:rvs,i:rvs", back));
        // items reach the ring from the ring menu's Quick cast chooser, its potions and scrolls row (a family id each)
        Ring viaMenu; CHECK(PutAt(viaMenu, 0, {FamilyId(FamHealth), -1}) && viaMenu.items[0].skill == FamilyId(FamHealth));
        // exact items: the kind page offers each exact one the hero carries, belt and inventory, each code
        // once, strongest first; a plain Rejuvenation put exactly never drinks the Full one.
        int got[12] = {};
        int n = CarriedOf(FamRejuv, belt, inv, 3, got, 12);
        CHECK(n == 2 && got[0] == ExactId(rvl) && got[1] == ExactId(rvs));   // the belt's Full above a bottom counts as carried too
        CHECK(PutAt(viaMenu, 1, {got[1], -1}) && Code3(PickFor(viaMenu.items[1].skill, belt, inv, 3, true).code) == Code3(rvs));
        n = CarriedOf(FamHealth, belt, inv, 3, got, 12);
        CHECK(n == 3 && got[0] == ExactId(hp5) && got[1] == ExactId(hp3) && got[2] == ExactId(hp2));
        CHECK(CarriedOf(FamHealth, belt, inv, 3, got, 2) == 2 && got[0] == ExactId(hp5));   // the cap
        n = CarriedOf(FamPortal, belt, inv, 3, got, 12);   // the tome (inventory) before the belt's scroll
        CHECK(n == 2 && got[0] == ExactId(tbk) && got[1] == ExactId(tsc));
        CHECK(CarriedOf(FamThawing, belt, inv, 3, got, 12) == 0 && CarriedOf(FamNone, belt, inv, 3, got, 12) == 0);
        CHECK(CarriedOf(FamStamina, belt, inv, 3, got, 12) == 1 && got[0] == ExactId(vps));   // a stack of 0 still listed: the code is carried
        // Potion choice: Healing, Mana and Rejuvenation always offer Best plus every tier the game has (carried or not,
        // strongest first); Antidote, Thawing and Stamina have one potion: no page, no "(best)", the family goes straight in.
        {
            int t[16] = {};
            CHECK(TiersOf(FamMana, t, 16) == 5 && t[0] == ExactId(Code('m', 'p', '5')) && t[4] == ExactId(Code('m', 'p', '1')));   // Super .. Minor, none carried needed
            CHECK(TiersOf(FamHealth, t, 16) == 5 && t[0] == ExactId(hp5) && t[4] == ExactId(Code('h', 'p', '1')));
            CHECK(TiersOf(FamRejuv, t, 16) == 2 && t[0] == ExactId(rvl) && t[1] == ExactId(rvs));
            CHECK(TiersOf(FamMana, t, 3) == 3 && TiersOf(FamNone, t, 16) == 0);
            CHECK(HasTiers(FamHealth) && HasTiers(FamMana) && HasTiers(FamRejuv));
            CHECK(!HasTiers(FamAntidote) && !HasTiers(FamThawing) && !HasTiers(FamStamina) && !HasTiers(FamPortal) && !HasTiers(FamIdentify) && !HasTiers(FamNone));
            CHECK(FamilyEntryName(FamHealth, true) == "Healing (best)" && FamilyEntryName(FamMana, false) == "Mana (weakest)" && FamilyEntryName(FamRejuv, true) == "Rejuvenation (best)");
            CHECK(FamilyEntryName(FamAntidote, true) == "Antidote Potion" && FamilyEntryName(FamThawing, false) == "Thawing Potion" && FamilyEntryName(FamStamina, true) == "Stamina Potion");
            CHECK(FamilyEntryName(FamPortal, true) == "Town Portal" && FamilyEntryName(FamIdentify, false) == "Identify");
            // the pick for a potion with no tiers is the family itself: it still drinks from the belt or the inventory
            const InvItem stam[1] = {{vps, 2}};
            const auto ps = PickFor(FamilyId(FamStamina), belt, stam, 1, true);
            CHECK(ps.ok && Code3(ps.code) == Code3(vps) && ps.column == -1);   // the family id, no exact tier, found in the inventory
        }
        // a kind's picture is an exact one's (one atlas cell); every exact code a kind can list is on the atlas's list
        CHECK(PictureId(FamilyId(FamHealth)) == ExactId(Code('h', 'p', '1')) && PictureId(ExactId(rvl)) == ExactId(rvl) && PictureId(64) == 64);
        for (const int fam : {FamHealth, FamMana, FamRejuv, FamAntidote, FamThawing, FamStamina, FamPortal, FamIdentify}) {
            bool listed = false;
            for (const uint32_t c : ExactCodes) listed |= ExactId(c) == PictureId(FamilyId(fam));
            CHECK(listed);
        }
        int kinds = 0;
        for (const uint32_t c : ExactCodes) kinds += FamilyOf(c) != FamNone;
        CHECK(kinds == int(std::size(ExactCodes)) && std::size(ExactCodes) == 19);
        // the snapshot's belt and quick-use list, as the radial and the ring menu read them
        framevr::interaction::Snapshot snap;
        snap.beltKnown = true; snap.belt[0].known = true; snap.belt[0].count = 2;
        snap.belt[0].items[0].handle = 1; snap.belt[0].items[0].code = hp2; snap.belt[0].items[1].handle = 2; snap.belt[0].items[1].code = hp5;
        snap.quick.known = true; snap.quick.count = 1; snap.quick.items[0].item.handle = 3; snap.quick.items[0].item.code = rvl; snap.quick.items[0].quantity = 2;
        const auto sb = BeltOf(snap);
        InvItem si[4]; const int sn = InvOf(snap, si, 4);
        CHECK(sb[0].n == 2 && Code3(sb[0].codes[1]) == Code3(hp5) && sb[1].n == 0 && sn == 1 && Code3(si[0].code) == Code3(rvl) && si[0].qty == 2);
        snap.beltKnown = false; snap.quick.known = false;
        CHECK(BeltOf(snap)[0].n == 0 && InvOf(snap, si, 4) == 0);
    }
    // 10. Cost: a closed frame and an open frame of the controller (no allocation; the glue's own closed path is one branch).
    {
        Sim s; s.Fill(12);
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < 1000000; ++i) { s.in.nowMs += 1; s.c.Step(s.in, s.ring, s.t); }
        const double closedNs = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count() / 1e6;
        s.Press(); s.Stick(-1, 0);
        const auto t1 = std::chrono::steady_clock::now();
        for (int i = 0; i < 100000; ++i) { s.in.nowMs += 1; s.c.Step(s.in, s.ring, s.t); (void)s.c.DrawNow(s.t); }
        const double openUs = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t1).count() / 1e5;
        std::printf("cost: closed Step %.1f ns, open Step + Draw %.2f us (12 items)\n", closedNs, openUs);
        CHECK(closedNs < 500 && openUs < 50);
    }
    // 11. The state the ring menu shares (Enabled, the button): on and unbound by default; SetButton keeps slot + 1 (0 none, 1..10), the
    // slot reads back (-1 none), every call counts so the glue saves the button, and the ring may switch the radial off.
    {
        CHECK(Enabled() && ButtonValue() == 0 && ButtonSlot() == -1);
        const uint32_t was = state::buttonSet.load();
        SetButton(3); CHECK(ButtonValue() == 3 && ButtonSlot() == 2 && state::buttonSet.load() == was + 1);
        SetButton(10); CHECK(ButtonSlot() == 9);
        SetButton(11); CHECK(ButtonValue() == 10);
        SetButton(0); CHECK(ButtonValue() == 0 && ButtonSlot() == -1 && state::buttonSet.load() == was + 4);
        SetEnabled(false); CHECK(!Enabled()); SetEnabled(true); CHECK(Enabled());
    }    std::printf("quick-radial-test: %d checks passed\n", checks);
    return 0;
}
