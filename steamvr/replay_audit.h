#pragma once
#ifdef D2RVR_NATIVE_OPENXR
namespace framevr::replayaudit {
void Register(bool enabled);
void BeginPair(bool xrActive);
void BeginEye(unsigned eye);
void View(unsigned eye,const float matrix[16]);
void Projection(unsigned eye,const float matrix[16]);
void EndEye();
void EndPair();
void SkipPair();
void Shutdown(); // Call after ReShade has unregistered the owning add-on.
}
#endif
