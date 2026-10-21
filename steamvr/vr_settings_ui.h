#pragma once
#include "vr_settings_help.h"
#include "body_calibration.h"
#include <vector>
namespace framevr::settings {
// game: the game's own video options, warned about and shown on the Perf page;
// monoAudio: Windows Mono audio is on (MonoAudioWarning in the title row);
// performance: the Perf page's frame times; depthRestart: a saved ReShade
// depth change waits for a restart.
std::vector<uint8_t> Paint(const Model& model,Hit hover,const char* runtimeStatus,const calibration::Flow* body=nullptr,const controllers::Labels& detected={},int help=NoHelp,GameVideo game={},bool monoAudio=false,
                           const PerformanceReadout& performance={},bool depthRestart=false);
}
