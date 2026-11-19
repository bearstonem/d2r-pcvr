#pragma once
// Ring skill menu (round 2026-11-08 #1, the player's rule for closing): with the ring On the wheel closes only on a deliberate
// action or a named safety reason. A hitch of any length, a lost hand, a lost laser or the headset's tracking never closes it.
// Upstream closes the wheel on an input gap over 100 ms (hand_controls.h:44) and the engine resets on one over 150 ms
// (interactions.h:787); the ring shows both a clock that never makes a step that long while the wheel is open. Pure: the ring
// feeds each frame's facts; ring_wheel_test.cpp drives it with upstream's real HandControls and Engine.
#include <algorithm>
#include <cstdint>
#include "interactions.h"
#include "loot_aim.h"

namespace framevr::ringwheel::closing {
constexpr double MaxStepS = .09;       // under both gates (.1 HandControls, .15 the engine)
constexpr double MinStepS = 1e-5;      // strictly increasing, as both gates need
constexpr double GapLogS = .1;         // a real gap upstream would have closed on: the "kept open" log line
constexpr double FocusBlipMs = 250;    // the headset unfocused this long closes the wheel (a safety); shorter is a blip

// The wheel's clock: real time minus an offset. While the wheel is open a real step longer than MaxStepS shows as MaxStepS and
// the excess joins the offset, so the frame after a hitch shows no second jump. Closed, the offset is a constant bias: every
// step and every real gap is exactly upstream's, and with no hitch yet the clock is real time exactly. It never snaps back at
// a close (a jump over .15 s would fire the Engine's Reset on the close frame). Only Clear and a new Clock end the bias.
class Clock {
    double real_ = 0, offset_ = 0, step_ = 0;
    bool started_ = false;
public:
    struct Out { double shown = 0, gapS = 0; };   // gapS: the real step, when it would have closed the open wheel; else 0
    Out Step(double real, bool open) {
        Out o;
        if (!started_) { started_ = true; real_ = real; o.shown = real - offset_; return o; }
        const double step = real - real_;
        real_ = real; step_ = step;
        if (open) {
            const double shown = std::clamp(step, MinStepS, MaxStepS);
            offset_ += step - shown;
            if (step > GapLogS) o.gapS = step;
        }
        o.shown = real - offset_;
        return o;
    }
    double Offset() const { return offset_; }
    double RealStep() const { return step_; }   // the last real step, for a close the ring cannot name
    void Clear() { offset_ = 0; }
};

// The headset unfocused (or a dropped frame): a blip keeps the open wheel (the caller skips that frame's input), and only
// FocusBlipMs of it closes. True: skip this frame's input.
class FocusBlip {
    double since_ = -1;
public:
    bool Step(bool wheelOpen, double nowMs, bool ending) {
        if (!wheelOpen || ending) { since_ = -1; return false; }
        if (since_ < 0) since_ = nowMs;
        if (nowMs - since_ < FocusBlipMs) return true;
        since_ = -1;
        return false;
    }
    void Focused() { since_ = -1; }
};

// The safety reason in upstream's motionInput.active terms (transport lootGate, one bit a term), the named close; nullptr: no
// safety reason (the headset's own tracking, GateHead, alone is not one). A local player gone is the session's.
inline const char* SafetyOf(uint32_t gate, bool localPlayer) {
    using namespace lootaim;
    if (!localPlayer) return "session";
    if (gate & GateWorld) return "load";
    if (gate & (GatePanel | GateSpatial | GateSettings)) return "panel";
    if (gate & GateInventory) return "inventory-button";
    if (gate & GateMenu) return "menu-button";
    if (gate & GateChord) return "settings-chord";
    if (gate & (GateControls | GateChain)) return "gameplay-off";
    return nullptr;
}
// Death, from upstream's own vitals test (transport.cpp `lifeKnown = vitals.vitalsKnown && interaction::Fresh(...)`): vitals
// not known or stale never close the wheel.
inline bool Dead(bool vitalsKnown, bool fresh, float health) { return vitalsKnown && fresh && health <= 0; }

// What HandControls and the Engine see while the wheel is open. Good input goes in; the close is never caught afterward (the
// Engine's Reset would already have latched the wheel). With the wheel closed and no offset every value stays upstream's.
class Hold {
    Clock clock_;
    double shown_ = 0;
    uint64_t context_ = 0, session_ = 0, epoch_ = 0, epochSeen_ = 0;
    const char* safety_ = nullptr;
    bool dead_ = false;
    interaction::Pose lastHead_{}, lastHand_[2]{};
    struct Saved { bool on = false; double time = 0; uint64_t epoch = 0; bool active = false, tracked[2]{}; interaction::Pose head{}, hand[2]{}; } saved_;
public:
    struct HandOut { double gapS = 0; bool moved = false; };   // moved: recenter or eye height while held (the wheel places itself again)
    // Before HandControls steps (handInput's time, context and active, edited in place).
    HandOut Hand(double& time, uint64_t& context, bool& active, bool open, uint64_t session, uint64_t epoch, uint32_t gate, bool localPlayer) {
        HandOut o;
        const char* safety = dead_ ? "death" : SafetyOf(gate, localPlayer);
        safety_ = open ? safety : nullptr;
        if (session != session_) clock_.Clear();   // a new game session: hidden by upstream's own reset (context and engine session change this frame)
        const auto c = clock_.Step(time, open && !safety);
        time = shown_ = c.shown;
        o.gapS = c.gapS;
        if (!open || safety || session != session_) { context_ = context; session_ = session; epoch_ = epoch; }
        else { context = context_; o.moved = epoch != epochSeen_; }   // weapon set, gear, recenter, eye height: no close
        epochSeen_ = epoch;
        if (open) active = dead_ ? false : safety ? active : true;   // death goes through upstream's own close, on every page
        return o;
    }
    // Before Engine::Step; Restore puts back everything it changed, for every later reader of the input.
    void Engine(interaction::Input& in, bool open) {
        saved_.on = false;
        if (interaction::Valid(in.head)) lastHead_ = in.head;
        for (int h = 0; h < 2; ++h) if (interaction::Valid(in.hand[h])) lastHand_[h] = in.hand[h];
        dead_ = open && Dead(in.snapshot.vitalsKnown, interaction::Fresh(in.snapshot, in.session, in.at), in.snapshot.health);
        if (!open && clock_.Offset() == 0) return;
        saved_ = {true, in.time, in.epoch, in.active, {in.tracked[0], in.tracked[1]}, in.head, {in.hand[0], in.hand[1]}};
        in.time = shown_;
        if (!open || safety_) return;
        in.epoch = epoch_; in.active = true; in.tracked[0] = in.tracked[1] = true;
        if (!interaction::Valid(in.head)) in.head = lastHead_;
        for (int h = 0; h < 2; ++h) if (!interaction::Valid(in.hand[h])) in.hand[h] = lastHand_[h];
    }
    void Restore(interaction::Input& in) {
        if (!saved_.on) return;
        saved_.on = false;
        in.time = saved_.time; in.epoch = saved_.epoch; in.active = saved_.active;
        in.tracked[0] = saved_.tracked[0]; in.tracked[1] = saved_.tracked[1];
        in.head = saved_.head; in.hand[0] = saved_.hand[0]; in.hand[1] = saved_.hand[1];
    }
    const char* Safety() const { return safety_; }   // this frame's safety reason while the wheel was open, nullptr none
    double Offset() const { return clock_.Offset(); }
    double RealStep() const { return clock_.RealStep(); }
    // The wheel's own timers (quick cast, hold to assign): a hitch must not count as a long hold, so they run on the shown clock.
    double NowMs(double realMs) const { return realMs - clock_.Offset() * 1000; }
    // the ring's Off edge and the XR session's end. One jump to real time: on that frame upstream sees what any real gap shows.
    void Reset() { *this = Hold{}; }
};
}  // namespace framevr::ringwheel::closing
