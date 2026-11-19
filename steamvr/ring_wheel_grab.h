#pragma once
// Lab Ring wheel (T025, feedback 1 item 2; build-plan E5): grip to move the wheel, as plain data. Pure and Windows-free
// (ring-wheel-test). Reuses upstream's panels::Grab unchanged (steamvr/native_panels.h, the VR Settings helper): grip on
// the panel arms it, 3 cm of hand travel starts the move, the grabbed point stays in the hand (move, angle, tilt, push and
// pull in one hold), hand roll ignored, tilt clamped, the distance kept 0.40-1.50 m from the head. The result is an offset
// from the Placer's spawn pose, so a remembered place follows the player when they walk or turn.
//
// The ring's rules on top (the design notes item 2 and the critic's FS3, FS4 as accepted by gameplay-dev):
//  - Only the pointing (laser) hand grabs; its grip with a face button or trigger down never starts a move (FS3: the grip
//    layer's binds and holds keep working); once moving, the ring takes no presses until the grip is up (the caller drops
//    its events while Moving() is true).
//  - A page change or the wheel closing ends a grab (Suspend); the fight fade never does.
//  - Kept between opens until the game session ends (nothing saved); a Placement row change, the Reset place row or
//    Remember Off clears it; a remembered place more than 60 deg from the gaze at an open respawns at the default (FS4).
//  - Grab to move Off: the spawn pose exactly, as before this step.
#include <cmath>
#include "native_panels.h"
#include "ring_wheel.h"

namespace framevr::ringwheel {
constexpr float GrabStrandDeg = 60;   // FS4: a remembered place farther than this from the gaze at an open respawns
inline panels::Placement PlacementOf(const interaction::Pose& p) { return {p.p, p.q}; }
inline interaction::Pose PoseOf(const panels::Placement& p) { return {p.position, p.orientation}; }
inline bool IsZero(const panels::Offset& o) { return o.position.x == 0 && o.position.y == 0 && o.position.z == 0 && o.yaw == 0 && o.pitch == 0; }
// The angle (deg) between the head's gaze and the direction from the head to a point.
inline float GazeDeg(const interaction::Pose& head, math::Vec at) {
    const math::Vec f = math::Rotate(head.q, {0, 0, -1}), d = at - head.p;
    const float n = std::sqrt(math::Dot(d, d));
    if (n < 1e-4f) return 0;
    return std::acos(std::clamp(math::Dot(f, d) / n, -1.f, 1.f)) * 180 / Pi;
}

// Batch1 merge (review-grip S3): the grabbing hand is job B's pointing owner; with none, the hand not holding the wheel.
// Review-grip S4: one grip threshold for the grab and the pointing owner's pin (a grip that grabs always keeps its owner).
constexpr float GripDown = .5f;
inline int GrabHand(int owner, int wheelHand) { return owner == 0 || owner == 1 ? owner : wheelHand == 0 ? 1 : 0; }
// Review-grip S1: while the wheel moves, upstream's pages (Equipment, DEV, the editors) get no hover and no hit, so a trigger
// squeezed mid-move clicks nothing there. The ring's pages drop their own events in Input (ringPage true: untouched).
inline void HoldUpstreamWhileMoving(bool moving, bool ringPage, int& hover, bool& pointerHit) {
    if (moving && !ringPage) { hover = -1; pointerHit = false; }
}

struct GrabIn {
    bool on = true;               // Grab to move
    bool remember = true;         // Remember moved place
    bool reset = false;           // the Reset place row pressed this frame
    bool open = false;            // the ring's panel is placed this frame (wheel open, world-locked)
    bool spawned = false;         // the Placer respawned it this frame (an open, a hand switch or a Placement change)
    bool placementChanged = false;   // a Placement row changed
    bool pageChanged = false;     // upstream's page changed while open
    interaction::Pose base{}, head{}, hand{};   // the spawn pose, the head, the pointing hand's aim pose
    bool handTracked = false;
    bool grip = false, hit = false, click = false;   // the pointing hand's grip and trigger; its ray on the panel (last frame)
    bool face = false;            // a face button of the pointing hand is down
    int handIndex = -1;           // which hand grabs (review-grip S4: a change while not Idle suspends, a fresh grip is needed)
};
enum class GrabEvent { None, Start, End, Cleared, Respawned };
struct GrabOut {
    interaction::Pose pose{};     // where the wheel is this frame
    GrabEvent event = GrabEvent::None;
    const char* why = "";         // Cleared / Respawned / End: why
    float gazeDeg = 0;            // Respawned: how far the remembered place was from the gaze
};
class RingGrab {
    panels::Grab grab{};
    panels::Offset offset{};
    bool moving = false;
    int handWas = -1;
public:
    GrabOut Step(const GrabIn& in) {
        GrabOut o; o.pose = in.base;
        if (!in.on) {   // Off: today's spawn, nothing kept
            const bool had = !IsZero(offset);
            grab = {}; offset = {}; moving = false;
            if (had) { o.event = GrabEvent::Cleared; o.why = "Grab to move Off"; }
            return o;
        }
        auto clear = [&](const char* why) {
            if (!IsZero(offset)) { o.event = GrabEvent::Cleared; o.why = why; }
            offset = {};
            if (moving) grab.Suspend();
            moving = false;
        };
        if (in.reset) clear("Reset place");   // open or closed
        else if (in.placementChanged) clear("Placement changed");
        if (!in.open) {   // closed: a grab in progress ends, the offset stays (remembered)
            if (moving && o.event == GrabEvent::None) { o.event = GrabEvent::End; o.why = "wheel closed"; }
            if (grab.state != panels::Grab::State::Idle) grab.Suspend();
            if (!in.grip) grab.state = panels::Grab::State::Idle;
            moving = false;
            return o;
        }
        if (in.reset || in.placementChanged) {}
        else if (in.spawned && !in.remember) clear("Remember moved place Off");
        else if (in.spawned && !IsZero(offset)) {
            const float deg = GazeDeg(in.head, panels::Apply(PlacementOf(in.base), offset).position);
            if (deg > GrabStrandDeg) { offset = {}; o.event = GrabEvent::Respawned; o.why = "remembered place out of view"; o.gazeDeg = deg; }
        }
        if (in.pageChanged && grab.state != panels::Grab::State::Idle) grab.Suspend();
        if (in.grip && in.face && grab.state != panels::Grab::State::Moving) grab.Suspend();   // FS3: a grip-layer press, never a move
        if (in.handIndex != handWas && grab.state != panels::Grab::State::Idle) grab.Suspend();   // S4: the other hand took over: no jump
        handWas = in.handIndex;
        const bool grip = in.grip && in.handTracked;   // a lost hand lets go
        const bool now = grab.Step(grip, in.hit, in.click, PlacementOf(in.hand), PlacementOf(in.base), in.head.p, offset) &&
                         grab.state == panels::Grab::State::Moving;
        if (now != moving && o.event == GrabEvent::None) { o.event = now ? GrabEvent::Start : GrabEvent::End; o.why = now ? "grip" : "released"; }
        moving = now;
        o.pose = IsZero(offset) ? in.base : PoseOf(panels::Apply(PlacementOf(in.base), offset));
        return o;
    }
    bool Moving() const { return moving; }   // the start / end log
    // Review-grip M1: what upstream's Grab::Step consumes, Moving and Held (the grip let go with the trigger still down): the
    // ring, upstream's pages and quick cast drop the trigger until it is let go too.
    bool Holding() const { return grab.state == panels::Grab::State::Moving || grab.state == panels::Grab::State::Held; }
    const panels::Offset& Kept() const { return offset; }
    panels::Grab::State State() const { return grab.state; }
    void NewSession() { grab = {}; offset = {}; moving = false; handWas = -1; }
};
}  // namespace framevr::ringwheel
