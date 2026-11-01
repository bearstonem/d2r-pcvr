#pragma once
// The mercenary's tactical commands (vr/companion_ai.cpp, transport StepCompanionTacticalGestures) take
// Grip + Trigger on one hand. Grip alone took them first (owner 2026-11-01): a fist made for a Paladin
// gesture (Fist of the Heavens, the Blessed Hammer pump, the Charge punch: grip held, trigger free) with
// the mercenary in the aim line latched a command, and letting the fist go ordered a move.
//
// Now the grip only arms the hand, when it is pressed while the hand points at the mercenary. From then
// the hand's trigger is the command's, never the game's (no skill cast at the mercenary), until it is let
// go. A trigger pull while armed latches the command: aim it with the hand, and letting go of the trigger
// or the grip carries it out. Letting go of the grip with no pull does nothing. A trigger already held
// when the grip closes (an attack) never arms.
//
// Value-only: the transport supplies each hand's grip and raw trigger (0..1) and whether it points at the
// mercenary; steamvr/tactical_chord_test.cpp.

namespace framevr::tacticalchord {

constexpr float GripOn = .6f, GripOff = .4f;          // the grip closes above GripOn, opens below GripOff
constexpr float TriggerOn = .55f, TriggerOff = .2f;   // the trigger is pulled above TriggerOn, let go below TriggerOff

enum class Event {
    None,      // nothing changed (a latched hand: aim the command)
    Armed,     // the grip closed on the mercenary: the trigger is the command's
    Latched,   // the trigger pulled while armed: the command is being aimed
    Release,   // trigger or grip let go while latched: carry the command out
    Disarmed,  // the grip let go with no pull, or the hand stopped (busy, untracked): nothing is ordered
};

struct Hand {
    bool grip = false, trigger = false;   // this hand's grip closed / trigger pulled, with hysteresis
    bool armed = false, latched = false;
    bool triggerOwned = false;            // the trigger is kept from the game (armed, latched, or not yet let go after)

    // Would this sample close the grip? (The transport only aims, which costs a ray test, on that frame
    // or while Engaged.)
    bool Closing(float gripValue) const { return !grip && gripValue > GripOn; }
    bool Engaged() const { return armed || latched; }

    // allowed: the hand may command (tactical commands on, the world in play, the hand tracked and not on
    // the belt, a holster, the wheel or a drawn bow). pointing: the hand points at the mercenary.
    Event Step(float gripValue, float triggerValue, bool pointing, bool allowed) {
        const bool gripWas = grip, triggerWas = trigger;
        grip = gripWas ? gripValue >= GripOff : gripValue > GripOn;
        trigger = triggerWas ? triggerValue >= TriggerOff : triggerValue > TriggerOn;
        Event e = Event::None;
        if (!allowed) {
            if (Engaged()) e = Event::Disarmed;
            armed = latched = false;
        } else if (latched) {
            if (!grip || !trigger) { armed = latched = false; e = Event::Release; }
        } else if (armed) {
            if (!grip) { armed = false; e = Event::Disarmed; }
            else if (trigger && !triggerWas) { latched = true; e = Event::Latched; }
        } else if (grip && !gripWas && pointing && !trigger) {
            armed = true; e = Event::Armed;
        }
        triggerOwned = Engaged() || (triggerOwned && trigger);
        return e;
    }
    void Reset() { *this = Hand{}; }
};

}  // namespace framevr::tacticalchord
