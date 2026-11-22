#pragma once
// Ring skill menu (rows 16-18): the bound-button badge beside each skill, as
// plain data. Pure and Windows-free (ring-wheel-test). One badge per bound slot, in the player's button style (the design notes
// item 3; the kit's badges=quest, drawQuestBadge): a dark disc with the face letter (A / B
// right, X / Y left on Quest), a gold coin with a dark letter for the grip layer, a dark rounded square R / L for the
// trigger; a free skill has none. Placed by the kit's badgePlaces search, once per layout, bind or row change, never per
// frame (BadgeBoard); ring_wheel_draw.h Build draws them on the main page only (never on the glyph chooser or a page).
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include "ring_wheel.h"

namespace framevr::ringwheel {
enum class BadgeKind : uint8_t { Disc, Coin, Square };   // the shader's RecipeBadge param
enum class BadgeDepth { Outside, Outline, Inside, Center };
// The three rows: angle -1 = Auto (the kit's outer side plus the clear-of-neighbors search) or 0-345 deg
// clockwise from 12 o'clock (exact, no search); depth; size in px, 0 = Off (no badges).
constexpr int BadgeAuto = -1, BadgeDefaultPx = 28;
struct BadgeLook {
    int angleDeg = BadgeAuto;
    BadgeDepth depth = BadgeDepth::Outline;
    int sizePx = BadgeDefaultPx;
    bool operator==(const BadgeLook&) const = default;
};
inline const char* DepthName(BadgeDepth d) {
    switch (d) { case BadgeDepth::Outside: return "Outside"; case BadgeDepth::Inside: return "Inside"; case BadgeDepth::Center: return "Center"; default: return "On outline"; }
}
// slot: 0 trigger, 1 primary, 2 secondary, 3 grip + primary, 4 grip + secondary (ring_wheel.h SlotOf).
inline BadgeKind BadgeKindOf(int slot) { return slot == 0 ? BadgeKind::Square : slot >= 3 ? BadgeKind::Coin : BadgeKind::Disc; }
// One letter, the controller's own (controller_labels.h Face): Quest A / B / X / Y, Index A / B; the trigger R / L. A
// face with a longer name (Frame's left D-pad, a generic pad) takes U / D or 1 / 2 so the badge never wraps.
inline char BadgeLetter(controllers::Layout layout, int hand, int slot) {
    if (slot == 0) return hand ? 'L' : 'R';
    const bool secondary = slot % 2 == 0;
    const char* f = controllers::Face(layout, hand, secondary);
    if (f && f[0] && !f[1]) return f[0];
    if (layout == controllers::Layout::Frame && hand) return secondary ? 'U' : 'D';
    return secondary ? '2' : '1';
}
// Job B: a controller that sleeps or loses tracking reports
// no interaction profile, which transport reads as Layout::Generic ("Unknown"; the logs show `Frame controllers: right=Touch
// left=Unknown` dozens of times a session), and Generic's faces are 1 / 2. The badges keep the last real layout of that hand
// instead. Only in Automatic: a chosen layout (Button names row) is the player's and is shown as it is.
struct LetterLayoutHold {
    std::array<controllers::Layout, 2> known{controllers::Layout::Generic, controllers::Layout::Generic};
    controllers::Layout Step(int physicalHand, controllers::Layout now, bool automatic) {
        const size_t h = size_t(physicalHand == 1);
        if (!automatic || now != controllers::Layout::Generic) { known[h] = now; return now; }
        return known[h];
    }
};
// The badge center's distance from its icon's center (row 17), R = size / 2: Outside touches the outline
// from outside, On outline straddles it a little outward (the kit's r x 0.98 + 4), Inside touches it from inside, Center 0.
inline float BadgeDist(const BadgeLook& k) {
    const float R = k.sizePx / 2.f, r = IconPx / 2;
    switch (k.depth) {
    case BadgeDepth::Outside: return r + R + 1;
    case BadgeDepth::Inside: return std::max(0.f, r - R - 1);
    case BadgeDepth::Center: return 0;
    default: return r * .98f + 4;
    }
}
// A pointed icon grows 1.3x and carries its badge out by the same factor (+3 px on the outline, the kit's badgeAt).
inline float BadgeDistPointed(const BadgeLook& k, float dist) { return dist * 1.3f + (k.depth == BadgeDepth::Outline ? 3.f : 0.f); }
// The first angle tried (radians, canvas y down): a fixed row angle exactly; Auto: the shelf lower right, a ring seat on its
// outer side turned 0.55 rad clockwise, a Trees shown outer-band seat (r > 250) on its inner side, clear of the name arc.
inline float BadgePref(const Layout& l, const Seat& s, int angleDeg) {
    if (angleDeg >= 0) return (float(angleDeg) - 90) * Pi / 180;
    if (s.tree < 0) return Pi / 4;
    const bool inward = l.mode == TreeLayout::Shown && std::hypot(s.x - C, s.y - C) > 250;
    return std::atan2(s.y - C, s.x - C) + (inward ? Pi - .55f : .55f);
}
// Which seats a badge keeps clear of (the kit's same): every seat in Trees shown; with Tree orbs only its own tree's
// (one tree shows at a time) and the shelf.
inline bool BadgeSame(const Layout& l, const Seat& a, const Seat& b) { return l.mode == TreeLayout::Shown || a.tree < 0 || b.tree < 0 || a.tree == b.tree; }
// The tree-name arc's inner edge less the badge (kit: 296 Trees shown, 320 Tree orbs) and the grown center's edge (r 140).
constexpr float BadgeArcShown = 296, BadgeArcOrbs = 320, BadgeCenterEdge = 140;

// The skill on each hand's five slots (id, owner; id -1 free), read from the snapshot the ring already uses.
struct BoundSlots {
    std::array<std::array<int, Slots>, 2> id{{{-1, -1, -1, -1, -1}, {-1, -1, -1, -1, -1}}};
    std::array<std::array<int, Slots>, 2> owner{{{-1, -1, -1, -1, -1}, {-1, -1, -1, -1, -1}}};
    bool operator==(const BoundSlots&) const = default;
};
constexpr int MaxBadges = 2 * Slots;
struct Badge {
    int seat = -1, hand = 0, slot = 0;
    BadgeKind kind = BadgeKind::Disc;
    char letter = '?';
    float angle = 0, dist = 0;       // from its icon's center (radians, wheel px)
    float x = 0, y = 0;              // its center at rest, wheel px
    float clear = 0;                 // its smallest clearance at rest (px; info under a fixed angle)
    int tries = 0;                   // search steps taken (0: the first choice)
    double bornMs = -1e18;           // when this slot got this skill while watched (the 150 ms pop, the 600 ms green tick)
};
struct BadgeSet {
    std::array<Badge, MaxBadges> b{};
    int count = 0;
    BadgeLook look{};
    uint32_t revision = 0;           // bumped on every placement (the draw key)
    int moved = 0;                   // badges the search moved off their first choice
    float worst = 1e9f;              // the smallest clearance (px)
};
// A badge at (x, y), radius R, for seat i: its clearance to every other kept-clear icon (r 22), every badge placed so far
// (its own seat's too), and for a ring seat the name arc and the grown center.
inline float BadgeClearance(const Layout& l, int i, float x, float y, float R, const BadgeSet& placed) {
    const Seat& me = l.seats[size_t(i)];
    float m = 1e9f;
    for (int j = 0; j < l.count; ++j)
        if (j != i && BadgeSame(l, l.seats[size_t(j)], me)) m = std::min(m, std::hypot(x - l.seats[size_t(j)].x, y - l.seats[size_t(j)].y) - IconPx / 2 - R);
    for (int k = 0; k < placed.count; ++k)
        if (BadgeSame(l, l.seats[size_t(placed.b[size_t(k)].seat)], me)) m = std::min(m, std::hypot(x - placed.b[size_t(k)].x, y - placed.b[size_t(k)].y) - 2 * R);
    if (me.tree >= 0) {
        const float r = std::hypot(x - C, y - C);
        m = std::min({m, (l.mode == TreeLayout::Shown ? BadgeArcShown : BadgeArcOrbs) - (r + R), r - R - BadgeCenterEdge});
    }
    return m;
}
// Every bound slot's badge (the kit's badgePlaces): seats in layout order, each hand's slots in order. Auto tries the first
// choice, then 0.25 rad steps alternating sides up to half a turn, and takes the first spot 2 px clear (else the clearest).
// A fixed angle is exact; a second badge on the same skill (one skill on both hands) steps round by the angle that keeps
// the two 2 px apart. Size Off: no badges.
inline void PlaceBadges(const Layout& l, const BoundSlots& bound, controllers::Layout ctrl, const BadgeLook& look, BadgeSet& out) {
    const uint32_t rev = out.revision + 1;
    out = BadgeSet{}; out.look = look; out.revision = rev;
    if (look.sizePx <= 0) return;
    const float R = look.sizePx / 2.f, d0 = BadgeDist(look);
    for (int i = 0; i < l.count; ++i) {
        const Seat& s = l.seats[size_t(i)];
        int onSeat = 0;
        for (int h = 0; h < 2; ++h) for (int k = 0; k < Slots && out.count < MaxBadges; ++k) {
            if (bound.id[size_t(h)][size_t(k)] < 0 || bound.id[size_t(h)][size_t(k)] != s.id || bound.owner[size_t(h)][size_t(k)] != s.owner) continue;
            Badge best;
            best.seat = i; best.hand = h; best.slot = k; best.kind = BadgeKindOf(k); best.letter = BadgeLetter(ctrl, h, k);
            const float a0 = BadgePref(l, s, look.angleDeg);
            if (look.angleDeg >= 0) {
                const float d = std::max(d0, R + 1), step = 2 * std::asin(std::min(1.f, (R + 1) / d));
                best.angle = a0 + onSeat * step; best.dist = onSeat ? d : d0;
                best.x = s.x + std::cos(best.angle) * best.dist; best.y = s.y + std::sin(best.angle) * best.dist;
                best.clear = BadgeClearance(l, i, best.x, best.y, R, out);
            } else {
                float bestM = -1e9f;
                for (int t = 0; t <= 24; ++t) {
                    const float a = a0 + (t % 2 ? 1.f : -1.f) * float((t + 1) / 2) * .25f, x = s.x + std::cos(a) * d0, y = s.y + std::sin(a) * d0;
                    const float m = BadgeClearance(l, i, x, y, R, out);
                    if (t == 0 || m > bestM) { bestM = m; best.angle = a; best.dist = d0; best.x = x; best.y = y; best.clear = m; best.tries = t; }
                    if (m >= 2) break;
                }
            }
            out.moved += best.tries > 0;
            out.worst = std::min(out.worst, best.clear);
            out.b[size_t(out.count++)] = best;
            ++onSeat;
        }
    }
}
// The frame thread's keeper: re-places only when the seats (their key), the binds, the controller or a row changed; a slot
// that took a new skill while the wheel was watched pops its badge (bornMs). Update returns true when it placed again.
class BadgeBoard {
    BadgeSet set{};
    BoundSlots last{};
    std::array<std::array<double, Slots>, 2> born{};
    uint64_t seatsKey = 0; BadgeLook lookWas{}; int ctrlWas = -1; bool have = false;
    bool opened = false;   // the wheel just opened: the next Update shows changes made while closed without a pop
public:
    bool Update(const Layout& l, uint64_t layoutKey, const BoundSlots& now, controllers::Layout ctrl, const BadgeLook& look, double nowMs) {
        const bool pops = have && !opened;
        opened = false;
        if (have && layoutKey == seatsKey && now == last && look == lookWas && int(ctrl) == ctrlWas) return false;
        for (int h = 0; h < 2; ++h) for (int k = 0; k < Slots; ++k) {
            const bool changed = now.id[size_t(h)][size_t(k)] != last.id[size_t(h)][size_t(k)] || now.owner[size_t(h)][size_t(k)] != last.owner[size_t(h)][size_t(k)];
            if (!pops) born[size_t(h)][size_t(k)] = -1e18;
            else if (changed) born[size_t(h)][size_t(k)] = now.id[size_t(h)][size_t(k)] >= 0 ? nowMs : -1e18;
        }
        PlaceBadges(l, now, ctrl, look, set);
        for (int i = 0; i < set.count; ++i) set.b[size_t(i)].bornMs = born[size_t(set.b[size_t(i)].hand)][size_t(set.b[size_t(i)].slot)];
        last = now; seatsKey = layoutKey; lookWas = look; ctrlWas = int(ctrl); have = true;
        return true;
    }
    void Reset() { have = false; }
    // Each wheel open: no pop for what changed while it was closed, and no new placement unless the
    // seats, binds, rows or controller changed (a re-place on every open cost up to 0.1 ms and a log line per open).
    void Opened() { opened = true; }
    const BadgeSet& Set() const { return set; }
};
// #3: the wheel hand's own Draw glyph button, on the Glyphs half: a skill's badge riding the center ring,
// 45 deg upper right (offset = ring radius x DrawBadgeRing: 56.6 at rest, 96.2 grown); the Size row applies, Angle and Depth do
// not. drawSlots: upstream's [glyphs] slots per hand (interaction::Output::glyphDraw), never a copy of them.
constexpr float DrawBadgeRing = .7071f, DrawBadgeDimmed = .55f;   // Dimmed: the Gestures half is the one pointed
struct DrawBadge {
    bool on = false;
    BadgeKind kind = BadgeKind::Disc;
    char letter = '?';
    int sizePx = BadgeDefaultPx;
    bool operator==(const DrawBadge&) const = default;
};
inline DrawBadge DrawBadgeFor(const std::array<int, 2>& drawSlots, int wheelHand, controllers::Layout ctrl, int sizePx) {
    DrawBadge d;
    if ((wheelHand != 0 && wheelHand != 1) || sizePx <= 0 || drawSlots[size_t(wheelHand)] < 0) return d;
    for (int k = 0; k < Slots; ++k)
        if (SlotOf(wheelHand, k) == drawSlots[size_t(wheelHand)]) { d.on = true; d.kind = BadgeKindOf(k); d.letter = BadgeLetter(ctrl, wheelHand, k); d.sizePx = sizePx; }
    return d;
}
// The pop: 1.35x at 75 ms back to 1 at 150 ms; the green tick fades over 600 ms.
inline float BadgePop(double ageMs) { return ageMs >= 0 && ageMs < 150 ? 1 + .35f * float(std::sin(ageMs / 150 * Pi)) : 1.f; }
inline float BadgeTick(double ageMs) { return ageMs >= 0 && ageMs < 600 ? 1 - float(ageMs / 600) : 0.f; }
}  // namespace framevr::ringwheel
