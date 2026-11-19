#pragma once
// The summon army forms where you face. Before, the commander was handed the hero model's forward, and the game turns the
// model to its walk; its HeadingFilter (summon_commander.h) re-latches to that after a 1.5 sub-tile walk, so walking
// backward from a monster swung the whole formation behind you.
// This engine (pure, tested in army_face_test.cpp) keeps its own latch, which the glue (vr/army_face.cpp) hands to the
// commander's HeadingFilter so the coordinator's Step forms on it:
//   - The facing: the headset view's forward on the ground plane.
//   - Turn: the army turns to the facing only once the facing is more than the tolerance from the army's heading (the
//     filter's own 22.5 degrees) and has stayed past it for the hold time: a head glance never reshuffles the army.
//   - Walk: every WalkStep sub-tiles walked (the filter's own WalkTriggerDist), the walk's direction is the model's
//     forward. Within the backpedal angle of the facing the army follows it, exactly as the filter's walk re-latch does;
//     farther from it (a backpedal, a strafe) the heading is held, and only the Turn rule may still turn it.
//   - No facing (the view unknown): the heading is held, a walk follows the model as before.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include "summon_commander.h"

namespace armyface {
using Vec2 = framevr::commander::Vec2;

// The commander's HeadingFilter numbers, read from it, never copied (summon_commander.h).
inline constexpr float WalkStep = framevr::commander::HeadingFilter::WalkTriggerDist;   // sub-tiles walked per walk check
inline constexpr float CommanderToleranceCos = framevr::commander::HeadingFilter::HysteresisCos;   // cos(22.5 deg)

struct Tuning {
    float backpedalDeg = 60.0f;    // a walk MORE than this from the facing is a backpedal (held); 180: never
    float toleranceDeg = 22.5f;    // the facing turns MORE than this from the heading before the army turns; 180: never
    float holdMs = 400.0f;         // and stays past it this long; 0: at once
};
inline Tuning tuning;   // what the glue runs with

// What vr/companion_ai.cpp hands the glue, twice an army tick, each under the coordinator's lock: step false before the
// corridor check (the tick's heading is decided, fx / fy replaced), step true before the coordinator's Step (the same heading
// handed on, the coordinator's filter seeded with it). fx / fy: the model's forward, edited in place.
struct HeadingFrame {
    float& fx; float& fy;
    float heroX, heroY;
    uint64_t nowMs;
    bool step;
    framevr::commander::HeadingFilter& filter;
    void (*say)(const char*);
};
void Facing(bool ok, float x, float y);   // vr/army_face.cpp, from vrcam's update loop: the view's forward on the ground plane
void Heading(HeadingFrame f);             // vr/army_face.cpp, from the army step

enum class Why : uint8_t {
    Kept,        // nothing changed the heading
    First,       // armed: the heading starts at the facing
    Turn,        // the facing stayed past Turn tolerance for Turn hold
    Walk,        // a walk within Backpedal angle of the facing: the heading follows the walk (the commander)
    Backpedal,   // a walk more than Backpedal angle from the facing: the heading held
    NoFacing,    // not armed and no facing yet: nothing to hand on (the handler leaves the commander's value)
};

struct Step {
    bool apply = false;          // false: the handler leaves the commander's raw value and filter alone this tick
    Vec2 heading{0.0f, -1.0f};   // the army's heading (unit), what the coordinator forms on
    Why why = Why::NoFacing;
    float walkDeg = -1.0f;       // a walk check this tick: the walk's angle from the facing (deg); -1: none
    float stepDeg = -1.0f;       // the same for the hero's own step (sub-tile delta), for the log's frame check
    float turnDeg = 0.0f;        // the facing's angle from the heading before this tick's change (deg)
    bool holdStarted = false;    // a backpedal began this tick (the first held walk after a follow, a turn or a pause)
};

inline bool Unit(Vec2 v, Vec2& out) noexcept {
    const float len = std::hypot(v.x, v.y);
    if (!(len > 1e-4f) || !std::isfinite(len)) return false;
    out = {v.x / len, v.y / len};
    return true;
}
inline float AngleDeg(Vec2 a, Vec2 b) noexcept {   // both unit
    return std::acos(std::clamp(a.x * b.x + a.y * b.y, -1.0f, 1.0f)) * 57.2957795f;
}

class Latch {
public:
    void Reset() noexcept { *this = Latch{}; }
    bool Armed() const noexcept { return armed_; }
    Vec2 Heading() const noexcept { return heading_; }

    // hero: the hero's place (sub-tiles); walkForward: the commander's raw forward (the model's); faceOk/face: the headset
    // view's forward this tick (same plane as walkForward); nowMs: the tick's time.
    Step Update(Vec2 hero, Vec2 walkForward, bool faceOk, Vec2 face, uint64_t nowMs, const Tuning& t) noexcept {
        Step s{};
        Vec2 f{}, m{};
        const bool haveFace = faceOk && Unit(face, f);
        const bool haveModel = Unit(walkForward, m);
        if (!armed_) {
            if (!haveFace) return s;   // NoFacing, apply false: the commander's value
            armed_ = true;
            heading_ = f;
            walkFrom_ = hero;
            s.apply = true; s.heading = heading_; s.why = Why::First;
            return s;
        }
        s.apply = true;
        s.why = haveFace ? Why::Kept : Why::NoFacing;
        s.turnDeg = haveFace ? AngleDeg(heading_, f) : 0.0f;
        const float dx = hero.x - walkFrom_.x, dy = hero.y - walkFrom_.y;
        const float moved = std::hypot(dx, dy);
        if (moved >= WalkStep) {
            walkFrom_ = hero;
            const Vec2 stepDir{dx / moved, dy / moved};
            const Vec2 walk = haveModel ? m : stepDir;
            if (haveFace) {
                s.walkDeg = AngleDeg(walk, f);
                s.stepDeg = AngleDeg(stepDir, f);
            }
            if (haveFace && s.walkDeg > t.backpedalDeg) {
                s.why = Why::Backpedal;
                s.holdStarted = !backpedaling_ || nowMs - lastBackpedalMs_ > PauseMs;
                backpedaling_ = true;
                lastBackpedalMs_ = nowMs;
                // held: no walk re-latch; the Turn rule below still runs on the facing
            } else {
                heading_ = walk;   // the commander's walk re-latch
                turning_ = false;
                backpedaling_ = false;
                s.why = Why::Walk;
                s.heading = heading_;
                return s;
            }
        }
        if (haveFace) {
            const float tol = std::clamp(t.toleranceDeg, 0.0f, 180.0f);
            if (tol < 180.0f && s.turnDeg > tol) {
                if (!turning_) { turning_ = true; turnSinceMs_ = nowMs; }
                if (float(nowMs - turnSinceMs_) >= t.holdMs) {
                    heading_ = f;
                    turning_ = false;
                    backpedaling_ = false;
                    s.why = Why::Turn;
                }
            } else {
                turning_ = false;   // back within tolerance: a glance; the hold starts over next time
            }
        }
        s.heading = heading_;
        return s;
    }

    // A backpedal after this long without a held walk is a new hold (its own log line).
    static constexpr uint64_t PauseMs = 1500;

private:
    bool armed_ = false;
    Vec2 heading_{0.0f, -1.0f};
    Vec2 walkFrom_{};
    bool turning_ = false;
    uint64_t turnSinceMs_ = 0;
    bool backpedaling_ = false;
    uint64_t lastBackpedalMs_ = 0;
};
}  // namespace armyface
