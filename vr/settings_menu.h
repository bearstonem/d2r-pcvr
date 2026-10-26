#pragma once
#include "../steamvr/vr_settings.h"
namespace vrsettings {
framevr::settings::Snapshot Read();
bool Submit(const framevr::settings::Request& request);
// Only the settings/update thread reads or writes the INI. XR queues values.
bool Poll(const wchar_t* path);
// Runtime capability, separate from persisted preferences and pending edits.
void SetOfflineDifficultyAvailable(bool available);
// ReShade's Generic Depth add-on as it runs this session, set before ReShade
// loads from d2r_pcvr.ini [render] reshade_depth.
void SetReShadeDepth(bool active);
bool ReShadeDepthActive();
// A hand's outline color came from the old single color (LegacyHighlight) at the last load.
bool LegacyHighlightUsed();
}
