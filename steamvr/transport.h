#pragma once
#include <cstdint>
#include "melee_contact.h"
#include "bow_flight.h"

// Called by the existing game-side stereo draw hook and ReShade integration.
// The OpenXR session uses the game's D3D12 device/queue.
namespace framevr {
using LogFn = void (*)(const char*);
using PanelFn = bool (*)();
void Register(LogFn log, PanelFn wantsPanel, PanelFn inWorld, PanelFn toggleMenus);
// Called by the owning window's WndProc, without the transport/render lock.
bool HandleWindowFocusMessage(void* window,uint32_t message,uint64_t token,int64_t deadline);
void ConfigureDeferredWait(bool enabled);
void ConfigureSymmetricProjection(bool enabled);
void ConfigureEyeTracking(bool enabled);
void ConfigureGameplay(bool enabled);
void ConfigureQuiver(bool enabled,bool preview); // [bow] quiver, quiver_preview
void ConfigureInteractions(bool seated,float waistBelowEyes,float beltForward,int dominantHand);
// Current motion-combat aim in the existing head-yaw-relative hand frame.
bool SettingsOpen();
bool OwnsControllerRumble(); // lock-free; VR input owns feedback instead of gamepad motors
bool SpellSelected();
bool LeftHandAiming();
bool CrossbowAiming();
bool NativeAimOwnership(); // hand casting or wheel: preserve locomotion, suppress legacy aim modes
bool NativeSkillCasting(); // any active trigger/quick cast; retain native locomotion input
bool DirectedSpellActive(); // fresh native assignment, actually casting (wheel or quick shortcut)
bool GameplayRay(float origin[3],float direction[3]);
bool MeleeSample(uint32_t poseStamp,melee::Sample& sample,int hand=0);
bool ShieldSample(uint32_t poseStamp,melee::Sample& sample);
bool RequestShieldSkill(const melee::Sample& sample,uint32_t target,math::Vec gridTarget);
int TakeShieldTarget(uint32_t& target); // 1 physical target, -1 cancelled cast, 0 native fallback
bool ShieldAim(float& x,float& y);
void FinishShieldSkill(bool accepted);
void MeleeImpact(int hand=0); // thread-safe; haptic delivered by XR input update
void ShieldGuardImpact(bool parry=false); // thread-safe; a physical block taps the left controller and strains the guard (a parry does not)
// Thread-safe, every guard tick: shield eligible (first person, shield, no menu) and raised.
void ShieldGuardState(bool eligible,bool raised);
// [gameplay] guard_strain_*: strain per block (1 = broken), lowered reset time,
// clear radius at full swell (1 = visor edge), broken breathing period, inner softness,
// opacity (0 = none; strain and the heartbeat still run), colour (0xRRGGBB) and
// density (1 = as drawn; higher thickens the partly clear band, for the black rim).
void ConfigureGuardStrain(float perBlock,float resetSeconds,float clear,float breatheSeconds,float softness,float brightness,uint32_t color,float density=1);
// [gameplay] guard_strain_heartbeat (x the game's Master x Combat volume, 0 = off),
// tempo at first strain and when broken, wind-down time from full, and an optional
// one-beat 16-bit PCM WAV (empty: the built-in beat). Reloads the WAV only when the path changes.
void ConfigureHeartbeat(float volume,float bpmMin,float bpmMax,float fadeSeconds,const wchar_t* file);
void SpellCast(int hand,uint64_t session); // one successful hand spell, not each projectile
bool TakeBowShot(bowflight::Shot& shot);
bool TakeCrossbowShot(bowflight::Shot& shot);
bool CrossbowHeld(const bowflight::Shot& shot);
bool TakeSpellShot(int hand,bowflight::Shot& shot);
bool SpellHeld(const bowflight::Shot& shot);
bool BowPreviewPose(bowflight::Shot& pose,bool& visible);
bowflight::Gate BowGate();
void SetBowWorldEpoch(uint32_t epoch);
uint64_t BowRenderTime(); // one timestamp for both eyes, lock-free
struct FrameTiming {
    double waitFrameMs=0,waitGpuMs=0,submitMs=0,maxPipelineMs=0,locateMs=0,inputMs=0,releaseMs=0;
    double displayPeriodMs=0; // Runtime-predicted application period; may differ from panel refresh.
    double acquireMs[2]{},copySubmitMs[2]{},gpuSceneMs[2]{},gpuEffectsMs[2]{},gpuCopyMs[2]{};
    uint32_t pipelines=0;bool gpuValid=false;
};
FrameTiming LastTiming();
void Shutdown(bool preserveResolution=false);
void BeginPair();
void BeginEye(int eye);
// Diagnostic only: F8 saves the next complete gameplay pair via ReShade.
void RequestStereoCapture();
void EndPair();
void ResetHeight();
void Recenter();
bool TakeWorldRecenter();
bool Active();
// Any thread: a focused VR session with motion controls ran input recently.
// False in desktop/flat play, so VR-only world effects stay out of it.
bool VrGameplay(uint64_t now);
bool SpatialMenu();
// mapNorth is the unmodified native camera's horizontal heading (map-up).
bool View(float out[16], const float hero[3], float eyeHeight,
          float userEyeHeight, float bodyHeading, float recenterYaw, int eye, float mapNorth);
bool Projection(float out[16], float nearWorld, int eye);
}
