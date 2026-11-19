#pragma once
// Ring skill menu: pointing with either hand (T025 job B; the design notes item 1; Jeff 2026-11-07: "laser from either
// hand, go"). Either hand's laser can point at the ring; one hand owns it at a time:
//  - With no owner, a hand claims it by moving its ray 1.5 deg within 300 ms, or by pressing its trigger, with its ray on the
//    panel. A trigger press also takes the wheel at once from an owner with no button down (a held button pins its owner until
//    it is let go). A move takes it from an owner only when deliberate (T025 round 2026-11-08 #5; Jeff's log: 361 of 420
//    owner changes under 50 ms apart, the two hands' small moves trading it every frame): TakeDeg within MoveMs, TakeRatio x
//    the owner's own turn (no ratio when the owner's ray is off the panel), and the owner has held it OwnMinMs.
//    After an idle owner lets go, the other hand also needs TakeDeg to claim while that last hand stays tracked and allowed
//    (review S1: on a gap the two hands' noise traded it); the last hand reclaims on the plain turn, and a trigger press wins.
//  - The owner keeps it while its ray is on something the ring can fire (a seat, the footer, a center half, Back) or any of
//    its buttons or a hold is down (critic FB1: a 1 s hold-to-assign or a long blurb read never loses it). Its 1 s release
//    timer runs only while its ray is off the panel or on nothing.
//  - It is lost at once when its aim is untracked, the "Pointing hand" row forbids that hand, or the wheel closes.
//  - With no owner there is no hover and no laser. B / X still close the wheel (Jeff's closing rule, 2026-11-08).
// Pure (no game, no clock): the ring feeds each frame's facts and its milliseconds; ring_wheel_test.cpp drives it.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace framevr::ringwheel::pointing {
// The "Pointing hand" row (exp.ring_wheel.pointing_hand). Other hand = upstream's rule (the hand not holding the wheel), the
// row's Off: the ring then skips Owner and points with that hand whenever it is tracked, as before job B (no claim needed).
enum class Allow { Either = 0, Right = 1, Left = 2, OtherHand = 3 };
inline bool Allowed(Allow a, int hand, int wheelHand) {
    return a == Allow::Either || (a == Allow::Right && hand == 0) || (a == Allow::Left && hand == 1) || (a == Allow::OtherHand && hand != wheelHand);
}
constexpr float MoveDeg = 1.5f;      // a claim: the ray turned this much ...
constexpr double MoveMs = 300;       // ... within this long
constexpr double ReleaseMs = 1000;   // the owner's timer, off the panel or on nothing
constexpr float TakeDeg = 5.f;       // a move that takes the wheel from an owner: the ray turned this much within MoveMs (17 deg/s) ...
constexpr float TakeRatio = 2.f;     // ... and this many times the owner's own turn (body sway turns both rays alike) ...
constexpr double OwnMinMs = 300;     // ... once the owner has held it this long

struct HandIn {
    bool tracked = false;             // the aim pose is tracked
    float dir[3] = {0, 0, -1};        // the aim ray's direction (unit)
    bool onPanel = false;             // its ray meets the panel
    bool onTarget = false;            // ... on something the ring can fire (seat, footer, center half, Back)
    bool trigger = false;             // its trigger down (over .55)
    bool anyDown = false;             // any of its buttons down (trigger, face, grip), or a hold running on this hand
};
struct In { double nowMs = 0; bool open = false; int wheelHand = -1; Allow allow = Allow::Either; HandIn hand[2]; };
enum class Why { None, Moved, Trigger, Released, Untracked, Row, Closed };
inline const char* WhyName(Why w) {
    switch (w) {
    case Why::Moved: return "moved"; case Why::Trigger: return "trigger"; case Why::Released: return "released";
    case Why::Untracked: return "untracked"; case Why::Row: return "row"; case Why::Closed: return "closed";
    default: return "none";
    }
}
struct Out { int owner = -1; bool changed = false; Why why = Why::None; };

// The non-owner's thin beam, once a headset frame (OnWheelBeam): drawn only with the ring On, the wheel's laser showing, a
// beam written this frame, and at least SpareSlots composition layers left after it (perf 'Pointing check' must-fix: the
// blood visor and the guard strain quads go in after it and need their slots). Off at the ring's Off edge (TabOn false).
constexpr uint32_t SpareSlots = 2;
enum class Beam { Draw, LabOff, Closed, None, NoSlot };
inline Beam BeamFor(bool tabOn, bool open, bool written, uint32_t count, uint32_t limit) {
    if (!tabOn) return Beam::LabOff;
    if (!open) return Beam::Closed;
    if (!written) return Beam::None;
    if (uint64_t(count) + 1 + SpareSlots > uint64_t(limit)) return Beam::NoSlot;
    return Beam::Draw;
}

class Owner {
    static constexpr int History = 96;   // over 300 ms at any frame rate up to 300 Hz
    struct Sample { double t; float d[3]; };
    struct Track { std::array<Sample, History> s{}; int head = 0, count = 0; bool trigger = true; double active = -1e18; };
    Track track[2];
    int owner = -1;
    int last = -1;          // the owner an idle release left (review S1); cleared when it is untracked, forbidden, or the wheel closes
    double gotAt = -1e18;   // when the owner got it (OwnMinMs)
    // The largest turn of this hand's ray within the last MoveMs, in degrees.
    static float Turned(const Track& k, const float* d, double now) {
        float best = 0;
        for (int i = 0; i < k.count; ++i) {
            const auto& s = k.s[size_t((k.head - 1 - i + History) % History)];
            if (now - s.t > MoveMs) break;
            const float dot = std::clamp(s.d[0] * d[0] + s.d[1] * d[1] + s.d[2] * d[2], -1.f, 1.f);
            best = std::max(best, std::acos(dot) * 57.29578f);
        }
        return best;
    }
public:
    int Current() const { return owner; }
    void Reset() { *this = Owner{}; }
    Out Step(const In& in) {
        Out out;
        const int was = owner;
        auto drop = [&](Why w) { owner = -1; out.why = w; };
        if (!in.open) { for (auto& k : track) k = Track{}; last = -1; gotAt = -1e18; if (owner >= 0) drop(Why::Closed); out.owner = owner; out.changed = owner != was; return out; }
        bool claim[2] = {}, pressed[2] = {};
        float turned[2] = {};
        for (int h = 0; h < 2; ++h) {
            auto& k = track[size_t(h)];
            const auto& hi = in.hand[h];
            pressed[h] = hi.trigger && !k.trigger;   // a held trigger at open is not a press (k.trigger starts true)
            k.trigger = hi.trigger;
            if (!hi.tracked) { k.count = 0; continue; }
            turned[h] = Turned(k, hi.dir, in.nowMs);
            k.s[size_t(k.head)] = {in.nowMs, {hi.dir[0], hi.dir[1], hi.dir[2]}};
            k.head = (k.head + 1) % History; k.count = std::min(k.count + 1, History);
            const bool active = turned[h] >= MoveDeg || pressed[h];
            if (active || hi.onTarget || hi.anyDown) k.active = in.nowMs;   // the owner's timer restarts while it acts or is pinned
            claim[h] = Allowed(in.allow, h, in.wheelHand) && hi.onPanel && active;
        }
        // The owner first: lost when untracked, forbidden by the row, or idle off the panel / on nothing for ReleaseMs.
        if (owner >= 0) {
            const auto& hi = in.hand[owner];
            if (!Allowed(in.allow, owner, in.wheelHand)) drop(Why::Row);
            else if (!hi.tracked) drop(Why::Untracked);
            else if (!hi.onTarget && !hi.anyDown && in.nowMs - track[size_t(owner)].active > ReleaseMs) drop(Why::Released);
        }
        if (out.why == Why::Released) last = was;
        else if (out.why == Why::Untracked || out.why == Why::Row) last = -1;
        if (owner < 0 && last >= 0 && (!in.hand[last].tracked || !Allowed(in.allow, last, in.wheelHand))) last = -1;
        // A claim: the other hand (or either, with no owner). A trigger press wins over a move; then the larger turn. A move
        // against an owner must be deliberate (the header's rule).
        const bool pinned = owner >= 0 && in.hand[owner].anyDown;
        if (!pinned) {
            const bool held = owner >= 0 && in.nowMs - gotAt < OwnMinMs;
            int best = -1;
            for (int h = 0; h < 2; ++h) {
                if (!claim[h] || h == owner) continue;
                if (owner < 0 && last >= 0 && h != last && !pressed[h] && turned[h] < TakeDeg) continue;   // review S1
                if (owner >= 0 && !pressed[h] && (held || turned[h] < TakeDeg || (in.hand[owner].onPanel && turned[h] < TakeRatio * turned[owner]))) continue;
                if (best < 0 || (pressed[h] && !pressed[best]) || (pressed[h] == pressed[best] && turned[h] > turned[best])) best = h;
            }
            if (best >= 0) { owner = best; gotAt = in.nowMs; out.why = pressed[best] ? Why::Trigger : Why::Moved; }
        }
        out.owner = owner; out.changed = owner != was;
        if (!out.changed) out.why = Why::None;
        return out;
    }
};
}  // namespace framevr::ringwheel::pointing
