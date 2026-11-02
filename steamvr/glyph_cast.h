#pragma once
// Glyph casting (glyph_recognizer.h, glyph_trail.h): the stroke while the "Draw glyph" button is held,
// and the cast once it is recognised.
//
// Stroke: while held, the drawing hand's path is kept (a point each time the hand has moved StepMetres),
// in the room and, for its trail, on the upright plane facing the player as the stroke began. Letting go
// flattens the room path onto the plane it was drawn in (glyphs::Flatten) and recognises it; a hand that
// stops being tracked, or a menu, wheel or other use of the hands, cancels it.
//
// Cast: the skill the player put on that shape is cast through a native button press, so mana, cast
// rate, the buff and its sound stay the game's. In order (Plan):
//   1. the skill is on one of the ten hand-button slots: that slot is pressed (as Holy Shield's sign);
//   2. the game's spare right-trigger skill record (record 5, which no hand button reaches and which the
//      mod never presses otherwise) already holds it: RT is pressed;
//   3. else it is put on that record first (the UI thread, nativeui) and RT pressed once the binding
//      reads back; ParkWaitSeconds without it, the cast is dropped.
// Value-only: steamvr/glyph_cast_test.cpp.
#include "glyph_recognizer.h"
#include "skill_slots.h"
#include <array>
#include <cmath>
#include <vector>

namespace framevr::glyphcast {

constexpr float StepMetres = .006f;      // a new stroke point each time the hand has moved this far
constexpr size_t MaxPoints = 800;
constexpr int GlyphRecord = 5;            // the controller binding record RT answers to (ButtonSlot probe, 2026-11-01)
constexpr double ParkWaitSeconds = .6;
constexpr double PressSeconds = .1;       // RT held this long (the game polls the pad at about 50 ms)
constexpr double SlotPressSeconds = .065; // a hand slot's press, as weaponskills::Pulse (LT slots longer)

struct StrokeInput {
    double time = 0;
    bool allowed = false;   // glyphs bound, in the world, hands free (no wheel, menu, bottle or bow)
    bool held = false;      // the Draw glyph button
    bool tracked = false;
    float x = 0, y = 0, z = 0;   // the drawing hand, room metres
    float rightX = 1, rightZ = 0; // the head's level right vector as the stroke begins
};
enum class Event { None, Began, Recognised, Refused, Cancelled };
struct StrokeOutput {
    Event event = Event::None;
    bool drawing = false;
    glyphs::Result result;   // with Recognised / Refused
    float seconds = 0;       // how long the stroke took
    bool ownPlane = false;   // flattened onto the plane it was drawn in (else the one facing the player)
    float planeDegrees = 0;  // that plane's turn from facing the player
};

class Stroke {
    bool drawing = false, wasHeld = false;
    double began = 0;
    glyphs::Plane plane;
    std::vector<glyphs::Point> points;
    std::vector<glyphs::Spot> room;
    glyphs::Cloud flat;
public:
    // The stroke so far (or the last one, while it fades), relative to where it began; the plane it lies in.
    const std::vector<glyphs::Point>& Points() const { return points; }
    const glyphs::Plane& Where() const { return plane; }
    // The last stroke as it was recognised: metres on the plane it was drawn in, from its first point.
    const glyphs::Cloud& Flattened() const { return flat; }
    bool Drawing() const { return drawing; }
    StrokeOutput Step(const StrokeInput& in) {
        StrokeOutput out;
        const bool pressed = in.held && !wasHeld;
        wasHeld = in.held;
        if (drawing && (!in.allowed || !in.tracked || !std::isfinite(in.x + in.y + in.z))) {
            drawing = false; out.event = Event::Cancelled; return out;
        }
        if (!drawing) {
            // Only a fresh press begins one: a button already held as glyphs became allowed does not.
            if (pressed && in.allowed && in.tracked && std::isfinite(in.x + in.y + in.z)) {
                const float len = std::hypot(in.rightX, in.rightZ);
                plane.originX = in.x; plane.originY = in.y; plane.originZ = in.z;
                plane.rightX = len > 1e-4f ? in.rightX / len : 1; plane.rightZ = len > 1e-4f ? in.rightZ / len : 0;
                points.assign(1, glyphs::Point{0, 0});
                room.assign(1, glyphs::Spot{in.x, in.y, in.z});
                flat.clear();
                began = in.time; drawing = true; out.event = Event::Began;
            }
            out.drawing = drawing;
            return out;
        }
        if (in.held) {
            const auto p = plane.Project(in.x, in.y, in.z);
            if (points.size() < MaxPoints && glyphs::Distance(p, points.back()) >= StepMetres) points.push_back(p);
            const auto& last = room.back();
            if (room.size() < MaxPoints && std::hypot(in.x - last.x, in.y - last.y, in.z - last.z) >= StepMetres) room.push_back({in.x, in.y, in.z});
            out.drawing = true;
            return out;
        }
        // Let go: recognise.
        drawing = false;
        out.seconds = float(in.time - began);
        auto flattened = glyphs::Flatten(room, plane.rightX, plane.rightZ);
        flat = std::move(flattened.stroke);
        out.ownPlane = flattened.own; out.planeDegrees = flattened.degrees;
        out.result = glyphs::Recognize(flat, out.seconds);
        out.event = out.result.shape == glyphs::Shape::None ? Event::Refused : Event::Recognised;
        return out;
    }
};

// [glyphs] draw_slot as a hand slot, -1 none; a shape's skill id, -1 none.
inline int DrawSlot(float value) {
    if (!std::isfinite(value)) return -1;
    const long slot = std::lround(value);
    return slot >= 0 && slot < skills::Count ? int(slot) : -1;
}
inline int SkillOf(float value) { return std::isfinite(value) && value >= 0 ? int(std::lround(value)) : -1; }

// What a hand slot holds, as the snapshot has it.
struct Slot { int id = -1, owner = -1; bool known = false, available = false; };
enum class How { None, Slot, Record, Park };
struct Plan { How how = How::None; int slot = -1; };
inline Plan Dispatch(int skill, int owner, const std::array<Slot, skills::Count>& slots, int recordSkill, int recordOwner) {
    if (skill < 0) return {};
    for (int i = 0; i < skills::Count; ++i)
        if (slots[size_t(i)].known && slots[size_t(i)].id == skill && slots[size_t(i)].owner == owner) return {How::Slot, i};
    if (recordSkill == skill && recordOwner == owner) return {How::Record, GlyphRecord};
    return {How::Park, GlyphRecord};
}

// One cast at a time: Begin with the plan, then Step each frame with whether the record holds the
// skill now; it says whether to press RT this frame (Record), or which slot to press (Slot).
class Cast {
    How how = How::None;
    int slot = -1, skill = -1, owner = -1;
    double since = 0, pressFrom = -1;
public:
    enum class State { Idle, Waiting, Pressing, Done, TimedOut };
    State state = State::Idle;
    bool Busy() const { return state == State::Waiting || state == State::Pressing; }
    void Begin(const Plan& plan, int skillId, int skillOwner, double now) {
        how = plan.how; slot = plan.slot; skill = skillId; owner = skillOwner; since = now; pressFrom = -1;
        state = how == How::None ? State::Done : how == How::Park ? State::Waiting : State::Pressing;
        if (state == State::Pressing) pressFrom = now;
    }
    void Cancel() { state = State::Idle; how = How::None; }
    struct Press { bool rt = false; int slot = -1; };
    // recordSkill/Owner: the record as the snapshot reads it now.
    Press Step(double now, int recordSkill, int recordOwner) {
        Press out;
        if (state == State::Waiting) {
            if (recordSkill == skill && recordOwner == owner) { state = State::Pressing; pressFrom = now; how = How::Record; }
            else if (now - since > ParkWaitSeconds) { state = State::TimedOut; return out; }
            else return out;
        }
        if (state != State::Pressing) return out;
        // An LT slot's modifier needs preparing first: held as long as Smite's (weaponskills::Pulse).
        const double hold = how == How::Slot ? (skills::Alternate(slot) ? .18 : SlotPressSeconds) : PressSeconds;
        if (now - pressFrom > hold) { state = State::Done; return out; }
        if (how == How::Slot) out.slot = slot; else out.rt = true;
        return out;
    }
    How Way() const { return how; }
};

}  // namespace framevr::glyphcast
