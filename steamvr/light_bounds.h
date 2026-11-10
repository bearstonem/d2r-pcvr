#pragma once
namespace framevr::lightbounds {
void Register(void (*log)(const char*));
void SetActive(bool);
// The campfire scene (character creation in stereo) draws its eyes in turns, outside the eye
// rendering that SetActive follows: while it is up the correction stays on regardless.
void SetScene(bool);
}
