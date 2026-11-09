// light_radius.h: the hero's Light Radius in VR (#440, "Light Radius ... doesnt seem to expand the light
// radius at all"). The game's own light round the hero is left as it is. What widens with Light Radius is the
// mod's own: the shader vault's light over the hero (CeilLightRadius) and the hero light in dungeons whose
// ceiling the game draws (reshade/D2R_Lights.fxh HeroLight). vrcam feeds both; this is their arithmetic.
#pragma once
#include <algorithm>

namespace framevr::lightradius {

// ItemStatCost 89, item_lightradius: what the hero's items and skills add to his light (a ring's +1, the
// minus some armour has). Read with the vitals on the UI thread (vr/native_ui.cpp), full value (layer 0).
inline constexpr int Stat = 89;

// [light] in d2r_pcvr.ini.
struct Tuning {
    float base = 0.0f;        // radius_base: what the stat reads with no Light Radius gear (the log prints it)
    float points = 10.0f;     // radius_points: the hero's own light in the stat's points; one point widens it by 1/points
    float boostPct = 150.0f;  // radius_boost_pct: each point counts this much in VR, 100 = as above (VR Settings)
};
inline constexpr float MinScale = 0.5f, MaxScale = 2.5f;
inline constexpr float MinBoostPct = 100.0f, MaxBoostPct = 300.0f;

// The reach of the hero's light against none of that gear: 1 with none (the reach as it was tuned), wider
// with +Light Radius, narrower with a minus; between MinScale and MaxScale. Not read yet: 1.
inline float Scale(int stat, bool known, const Tuning& t) {
    if (!known || !(t.points > 0.0f) || !(t.boostPct >= 0.0f)) return 1.0f;
    const float s = 1.0f + (float(stat) - t.base) * (t.boostPct / 100.0f) / t.points;
    return std::clamp(s, MinScale, MaxScale);
}

// The hero light's place, in the shader's frame (x, z less the hero; y over the area's floor): his shoulder,
// under the eyes. eyeOverFeet: the eyes over his feet; feetOverFloor: his feet over the area's floor.
inline constexpr float ShoulderShare = 0.8f;   // a shoulder is about 0.8 of the eyes' height
struct Place { float x, y, z; };
inline Place HeroLightAt(float eyeX, float eyeOverFeet, float eyeZ, float feetOverFloor) {
    return {eyeX, feetOverFloor + ShoulderShare * std::max(eyeOverFeet, 0.0f), eyeZ};
}

}  // namespace framevr::lightradius
