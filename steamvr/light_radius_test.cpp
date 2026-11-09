// light_radius.h: how the hero's Light Radius (stat 89) widens the mod's dungeon light (#440).
#include "light_radius.h"
#include <cassert>
#include <cmath>
#include <cstdio>

using namespace framevr::lightradius;

static bool Near(float a, float b) { return std::fabs(a - b) < 1e-4f; }

int main() {
    const Tuning t;   // the defaults: base 0, 10 points, a 150% boost in VR
    // No gear, or not read yet: the reach as it was tuned.
    assert(Near(Scale(0, true, t), 1.0f));
    assert(Near(Scale(5, false, t), 1.0f));
    // +Light Radius widens it, each point 1/10 at 100%, 1.5 times that by default.
    assert(Near(Scale(1, true, t), 1.15f));
    assert(Near(Scale(3, true, t), 1.45f));
    assert(Near(Scale(5, true, t), 1.75f));
    Tuning flat = t; flat.boostPct = 100.0f;
    assert(Near(Scale(5, true, flat), 1.5f));
    Tuning strong = t; strong.boostPct = 300.0f;
    assert(Near(Scale(2, true, strong), 1.6f));
    // A minus narrows it.
    assert(Near(Scale(-2, true, t), 0.7f));
    // Never past the bounds, however much gear.
    assert(Near(Scale(40, true, t), MaxScale));
    assert(Near(Scale(-40, true, t), MinScale));
    // Each point never narrows it: the scale only grows with the stat.
    float last = 0.0f;
    for (int s = -20; s <= 20; ++s) { const float now = Scale(s, true, t); assert(now >= last); last = now; }
    // A stat that holds a base of its own (radius_base): no gear reads 1, gear above it widens.
    Tuning based = t; based.base = 13.0f;
    assert(Near(Scale(13, true, based), 1.0f));
    assert(Near(Scale(16, true, based), 1.45f));
    // Broken tuning keeps the old reach.
    Tuning none = t; none.points = 0.0f;
    assert(Near(Scale(5, true, none), 1.0f));
    none = t; none.boostPct = -1.0f;
    assert(Near(Scale(5, true, none), 1.0f));
    // The light at the shoulder, under the eyes; his feet 2 over the area's floor, the eyes 6.5 over his feet.
    const Place p = HeroLightAt(0.3f, 6.5f, -0.2f, 2.0f);
    assert(Near(p.x, 0.3f) && Near(p.z, -0.2f) && Near(p.y, 2.0f + 0.8f * 6.5f));
    assert(Near(HeroLightAt(0, -1.0f, 0, 1.0f).y, 1.0f));   // eyes under the feet (a bad pose): at the feet
    std::puts("light radius: scale from stat 89 (none, +, -, bounds, base, boost) and the shoulder passed");
    return 0;
}
