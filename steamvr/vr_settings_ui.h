#pragma once
#include "vr_settings_help.h"
#include "body_calibration.h"
#include <vector>
namespace framevr::settings {
// game: the game's own video options, warned about and shown on the Perf page;
// monoAudio: Windows Mono audio is on (MonoAudioWarning in the title row);
// performance: the Perf page's frame times; frameGen: what OFXR Bridge is doing, under them.
// The Notes tab's pages for this build's release lines (Model::SetNotesPages).
int ReleaseNotesPages();
std::vector<uint8_t> Paint(const Model& model,Hit hover,const char* runtimeStatus,const calibration::Flow* body=nullptr,const controllers::Labels& detected={},int help=NoHelp,GameVideo game={},bool monoAudio=false,
                           const PerformanceReadout& performance={},const framegen::Readout& frameGen={});
}
