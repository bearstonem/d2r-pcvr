#pragma once
#include "interactions.h"
#include "controller_labels.h"
#include <vector>
namespace framevr::interaction {
std::vector<uint8_t> Paint(const Snapshot&,const Output&,int selection,const controllers::Labels& labels={});
std::vector<uint8_t> PaintCompanions(const companions::Roster&);
std::vector<uint8_t> PaintWristStatus(const companions::Roster&,const effects::Status&);
// Hand-made visor blood frames (RCDATA D2RVR_BLOOD_VISOR), decoded at the
// requested atlas size. Empty when not embedded; callers use blood::Paint().
std::vector<uint8_t> BloodArt(int width,int height);
// The floating map grab hint's sign, its loop's frames side by side (RCDATA D2RVR_GRAB_HINT, grab_hint.h), decoded at
// the requested size, straight RGBA. Empty when not embedded; the hint then shows its glow only.
std::vector<uint8_t> GrabHintArt(int width,int height);
}
