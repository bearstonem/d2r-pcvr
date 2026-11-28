#pragma once
// Screenshots from inside the headset.
//
// The game's own frame (RequestScreenshot -> shot::Take, Ctrl+F11 / [debug] shot) carries
// NO UI: the mod draws the menu out of a separate layer, so a tab bar or a stash page never
// appears in it. SaveUiPicture is the other half: transport reads that captured UI layer back
// from the GPU and hands the pixels here to be written as a PNG, so the interface itself can
// be looked at off the headset.
#include <cstdint>
#include <vector>

namespace framevr {
void RequestScreenshot();
// Straight RGBA, width x height, saved as a PNG in pcvr_shots\ beside the plugin.
void SaveUiPicture(std::vector<uint8_t> rgba, int width, int height);
}
