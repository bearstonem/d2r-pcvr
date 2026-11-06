#include "glyph_cast.h"
#include <cmath>
#include <cstdio>
#include <string>
using namespace framevr;
using namespace framevr::glyphcast;
static int failures = 0;
static void Check(bool ok, const std::string& what) { if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what.c_str()); ++failures; } }

// Draw a circle 35 cm across in front of the player (facing -z: right is +x), at 90 Hz over 1.2 s.
static StrokeOutput DrawCircle(Stroke& s, double& t, float cx = 0, float cy = 1.4f, float cz = -.5f) {
    StrokeInput in; in.allowed = true; in.tracked = true; in.held = true; in.rightX = 1; in.rightZ = 0;
    StrokeOutput out;
    const int frames = 108;
    for (int i = 0; i <= frames; ++i) {
        const float a = 6.2831853f * float(i) / frames;
        in.time = t; in.x = cx + .175f * std::cos(a); in.y = cy + .175f * std::sin(a); in.z = cz;
        out = s.Step(in); t += 1.0 / 90;
        if (i == 0) Check(out.event == Event::Began, "the press begins a stroke");
    }
    in.held = false; in.time = t; out = s.Step(in); t += 1.0 / 90;
    return out;
}

int main() {
    {   // a circle drawn and let go: recognised
        Stroke s; double t = 10;
        const auto out = DrawCircle(s, t);
        Check(out.event == Event::Recognised && out.result.shape == glyphs::Shape::Circle, "a drawn circle is recognised on release");
        Check(s.Points().size() > 50 && !s.Drawing(), "its points are kept for the trail");
    }
    {   // the plane follows the player's facing: drawn while facing +x (right is +z)
        Stroke s; double t = 0;
        StrokeInput in; in.allowed = in.tracked = in.held = true; in.rightX = 0; in.rightZ = 1;
        for (int i = 0; i <= 108; ++i) {
            const float a = 6.2831853f * float(i) / 108;
            in.time = t; in.x = .5f; in.y = 1.4f + .175f * std::sin(a); in.z = .175f * std::cos(a);
            s.Step(in); t += 1.0 / 90;
        }
        in.held = false; in.time = t;
        const auto out = s.Step(in);
        Check(out.result.shape == glyphs::Shape::Circle, "a circle drawn facing another way is still a circle");
    }
    {   // a held button when glyphs become allowed does not start a stroke; a fresh press does
        Stroke s; StrokeInput in; in.tracked = true; in.held = true; in.allowed = false; in.time = 1;
        s.Step(in); in.allowed = true; in.time = 1.02;
        Check(s.Step(in).event == Event::None && !s.Drawing(), "already held: no stroke");
        in.held = false; in.time = 1.04; s.Step(in); in.held = true; in.time = 1.06;
        Check(s.Step(in).event == Event::Began, "pressed again: a stroke");
    }
    {   // losing tracking or a wheel opening cancels
        Stroke s; StrokeInput in; in.tracked = in.allowed = true; in.held = false; in.time = 1; s.Step(in);
        in.held = true; in.time = 1.01; s.Step(in);
        in.tracked = false; in.time = 1.02;
        Check(s.Step(in).event == Event::Cancelled && !s.Drawing(), "tracking lost: cancelled");
    }
    {   // a tap: too short to be a glyph
        Stroke s; StrokeInput in; in.tracked = in.allowed = true; in.time = 1; s.Step(in);
        in.held = true; in.time = 1.01; s.Step(in); in.x = .02f; in.time = 1.05; s.Step(in);
        in.held = false; in.time = 1.1;
        const auto out = s.Step(in);
        Check(out.event == Event::Refused && std::string(out.result.why) == "too few points", "a tap is refused");
    }
    // Dispatch: a slot first, then the record, else park.
    std::array<Slot, skills::Count> slots{};
    slots[3] = {149, -1, true, true};   // Battle Orders on slot 3
    Check(Dispatch(149, -1, slots, -1, -1).how == How::Slot && Dispatch(149, -1, slots, -1, -1).slot == 3, "on a button: that button");
    Check(Dispatch(138, -1, slots, 138, -1).how == How::Record, "already on the record: RT");
    Check(Dispatch(138, -1, slots, 155, -1).how == How::Park, "else: put it on the record");
    Check(Dispatch(-1, -1, slots, -1, -1).how == How::None, "no skill on the glyph: nothing");
    Check(Dispatch(149, 3, slots, -1, -1).how == How::Park, "an item's skill (another owner) is not the class skill");
    {   // a park waits for the record and ParkSettleSeconds more, then presses RT for PressSeconds
        Cast c; c.Begin(Dispatch(138, -1, slots, 155, -1), 138, -1, 5.0);
        Check(c.state == Cast::State::Waiting && !c.Step(5.1, 155, -1).rt, "waiting: no press yet");
        Check(!c.Step(5.2, 138, -1).rt && c.state == Cast::State::Waiting, "the record holds it: not pressed at once");
        Check(!c.Step(5.3, 138, -1).rt, "nor 100 ms after the read-back (the game's target for it is not found yet)");
        Check(c.Step(5.36, 138, -1).rt, "ParkSettleSeconds after the read-back: RT pressed");
        Check(c.Step(5.41, 138, -1).rt, "still pressed");
        c.Step(5.47, 138, -1);
        Check(c.state == Cast::State::Done && !c.Step(5.48, 138, -1).rt, "released after PressSeconds");
    }
    {   // Leap and Teleport, landed by the mod, press as soon as the record reads back
        for (const int id : {132, 54}) {
            Cast c; c.Begin({How::Park, GlyphRecord}, id, -1, 2.0);
            Check(!c.Step(2.05, 155, -1).rt && c.Step(2.1, id, -1).rt, "Leap and Teleport: pressed at the read-back, no settle");
        }
        Cast raise; raise.Begin({How::Park, GlyphRecord}, 70, -1, 2.0);
        Check(!raise.Step(2.1, 70, -1).rt, "Raise Skeleton still settles first");
    }
    {   // a read-back just before ParkWaitSeconds still settles and presses
        Cast c; c.Begin({How::Park, GlyphRecord}, 138, -1, 1.0);
        c.Step(1.3, -1, -1); c.Step(1.55, 138, -1); c.Step(1.65, 138, -1);
        Check(c.state == Cast::State::Waiting && c.Step(1.71, 138, -1).rt, "read back at 0.55 s: pressed at 0.71 s");
    }
    {   // the record losing it while settling waits again, still within ParkWaitSeconds
        Cast c; c.Begin({How::Park, GlyphRecord}, 138, -1, 1.0);
        c.Step(1.1, 138, -1); c.Step(1.2, 155, -1);
        Check(!c.Step(1.3, 138, -1).rt && !c.Step(1.4, 138, -1).rt && c.Step(1.46, 138, -1).rt, "settled again from the second read-back");
        Cast late; late.Begin({How::Park, GlyphRecord}, 138, -1, 1.0);
        late.Step(1.5, 138, -1); late.Step(1.62, 155, -1);
        Check(late.state == Cast::State::TimedOut, "lost after ParkWaitSeconds: timed out");
    }
    {   // a park that never reads back times out
        Cast c; c.Begin({How::Park, GlyphRecord}, 138, -1, 1.0);
        c.Step(1.3, -1, -1); c.Step(1.7, -1, -1);
        Check(c.state == Cast::State::TimedOut, "no read back: timed out, nothing pressed");
    }
    {   // a slot press
        Cast c; c.Begin({How::Slot, 3}, 149, -1, 2.0);
        const auto p = c.Step(2.01, -1, -1);
        Check(p.slot == 3 && !p.rt, "the slot is pressed");
        c.Step(2.1, -1, -1);
        Check(c.state == Cast::State::Done, "and released");
    }
    {   // an LT slot is held longer, its modifier prepared first
        Cast c; c.Begin({How::Slot, 5}, 149, -1, 3.0);
        c.Step(3.1, -1, -1);
        Check(c.state == Cast::State::Pressing && c.Step(3.15, -1, -1).slot == 5, "an LT slot: still pressed at 150 ms");
        c.Step(3.2, -1, -1);
        Check(c.state == Cast::State::Done, "and released after 180 ms");
    }
    Check(DrawSlot(3) == 3 && DrawSlot(-1) == -1 && DrawSlot(10) == -1 && DrawSlot(std::nanf("")) == -1, "the draw slot setting");
    Check(SkillOf(149) == 149 && SkillOf(-1) == -1, "a shape's skill setting");
    if (failures) { std::fprintf(stderr, "%d failures\n", failures); return 1; }
    std::puts("glyph-cast-test: ok");
    return 0;
}
