#pragma once
#include "interactions.h"
#include "controller_labels.h"
#include <vector>
namespace framevr::interaction {
std::vector<uint8_t> Paint(const Snapshot&,const Output&,int selection,const controllers::Labels& labels={});
// Hand-made visor blood frames (RCDATA D2RVR_BLOOD_VISOR), decoded at the
// requested atlas size. Empty when not embedded; callers use blood::Paint().
std::vector<uint8_t> BloodArt(int width,int height);
}
