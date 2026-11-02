#pragma once
// The mercenary's tactical commands (vr/companion_ai.cpp, transport StepCompanionTacticalGestures) take
// Grip + Trigger on one hand. Grip alone took them first (owner 2026-11-01): a fist made for a Paladin
// gesture (Fist of the Heavens, the Blessed Hammer pump, the Charge punch: grip held, trigger free) with
// the mercenary in the aim line latched a command, and letting the fist go ordered a move.
//
// The hand is armed while its grip is held and it points at the mercenary (or did, GraceSeconds ago):
// gripped first and then pointed, or pointed first, alike. From then the hand's trigger is the
// command's, never the game's (no skill cast at the mercenary). A trigger pull while armed, or with the
// grip in one go, latches the command: aim it with the hand, and letting go of the trigger or the grip
// carries it out. Without a pull nothing is ordered (a Paladin's fist across the mercenary). An attack
// already under way (the trigger pulled before) keeps its trigger.
//
// The game takes this frame's trigger before the step runs, so Withholds decides it first, from the
// raw grip and trigger and where the hand last pointed: the pull that latches never reaches the game
// (owner 2026-11-01: Grip + Trigger "somehow triggers the primary action").
//
// Value-only: the transport supplies each hand's grip and raw trigger (0..1) and whether it points at the
// mercenary; steamvr/tactical_chord_test.cpp.

namespace framevr::tacticalchord {

constexpr float GripOn = .6f, GripOff = .4f;          // the grip closes above GripOn, opens below GripOff
constexpr float TriggerOn = .55f, TriggerOff = .2f;   // the trigger is pulled above TriggerOn, let go below TriggerOff
constexpr double GraceSeconds = .25;                  // the hand still counts as on the mercenary this long after

enum class Event {
    None,      // nothing changed (a latched hand: aim the command)
    Armed,     // the grip held on the mercenary: the trigger is the command's
    Latched,   // the trigger pulled while armed: the command is being aimed
    Release,   // trigger or grip let go while latched: carry the command out
    Disarmed,  // the grip let go or the hand off the mercenary with no pull, or the hand stopped: nothing is ordered
};

struct Hand {
    bool grip = false, trigger = false;   // this hand's grip closed / trigger pulled, with hysteresis
    bool armed = false, latched = false;
    bool attacking = false;               // the trigger was pulled for the game (no command): it keeps it until let go
    bool triggerOwned = false;            // the trigger is kept from the game (armed, latched, or not yet let go after)
    double pointedAt = -1;                // when the hand last pointed at the mercenary (seconds), -1 never

    bool Engaged() const { return armed || latched; }
    bool OnMercenary(double time) const { return pointedAt >= 0 && time - pointedAt <= GraceSeconds; }

    // Before the game sees this frame's trigger: is it the command's? Engaged or still owned, or a new pull
    // with the grip held on the mercenary (both pressed in one go).
    bool Withholds(double time, float gripValue, float triggerValue) const {
        if (Engaged() || triggerOwned) return true;
        const bool gripNow = grip ? gripValue >= GripOff : gripValue > GripOn;
        const bool pulled = !trigger && triggerValue > TriggerOn;
        return gripNow && pulled && !attacking && OnMercenary(time);
    }

    // allowed: the hand may command (tactical commands on, the world in play, the hand tracked and not on
    // the belt, a holster, the wheel or a drawn bow). pointing: the hand points at the mercenary now.
    Event Step(double time, float gripValue, float triggerValue, bool pointing, bool allowed) {
        const bool triggerWas = trigger;
        grip = grip ? gripValue >= GripOff : gripValue > GripOn;
        trigger = trigger ? triggerValue >= TriggerOff : triggerValue > TriggerOn;
        if (pointing && allowed) pointedAt = time;
        if (!trigger) attacking = false;
        Event e = Event::None;
        if (!allowed) {
            if (Engaged()) e = Event::Disarmed;
            armed = latched = false;
            pointedAt = -1;
        } else if (latched) {
            if (!grip || !trigger) { armed = latched = false; e = Event::Release; }
        } else {
            const bool arm = grip && OnMercenary(time) && !attacking;
            if (arm && trigger && !triggerWas) { armed = latched = true; e = Event::Latched; }
            else if (arm && !trigger && !armed) { armed = true; e = Event::Armed; }
            else if (armed && !arm) { armed = false; e = Event::Disarmed; }
        }
        if (trigger && !triggerWas && !latched && !triggerOwned) attacking = true;   // a pull for the game
        triggerOwned = Engaged() || (triggerOwned && trigger);
        return e;
    }
    void Reset() { *this = Hand{}; }
};

}  // namespace framevr::tacticalchord
