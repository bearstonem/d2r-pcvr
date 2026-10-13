#pragma once
#include "vr_settings.h"
#include "body_calibration.h"
#include <vector>
namespace framevr::settings {
std::vector<uint8_t> Paint(const Model& model,Hit hover,const char* runtimeStatus,const calibration::Flow* body=nullptr,bool rendererUnavailable=false);
}
