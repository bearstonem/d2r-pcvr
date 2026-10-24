#pragma once
#include <cstdint>
namespace reshade::api { struct effect_runtime; }
#include "melee_contact.h"
#include "bow_flight.h"
#include "sewer_prompt.h"

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
// [render] frame_generation (0 Off, 1 FidelityFX, 2 NVIDIA), frame_generation_preset
// and frame_generation_scale: OFXR Bridge (frame_generation.h). On or off at
// the next OpenXR start; the backend and NVIDIA's options reach a loaded layer
// at once, and Off bypasses it until then.
void ConfigureFrameGeneration(float mode,float preset,float scale);
// [render] frame_generation_monsters (1): game motion takes what moves by
// itself (monsters, missiles) from the game's own motion vectors; 0 the
// scene's depth alone (what moves by itself is blended).
void ConfigureFrameMotion(bool monsters);
// [stereo] turns_from_drawn_eye (1): by turns, the eye not drawn mostly from
// the drawn eye's picture of this frame (TurnDepth); 0: its own image alone.
void ConfigureTurnsFromDrawnEye(bool enabled);
// [stereo] turns_keep_own (3): the eye not drawn keeps its own older
// picture where that shows the same surface as the drawn eye's, so glows and
// hair stand where this eye sees them (TurnDepth::keep) - 1 round near things
// only (TurnDepth::keepNear), 2 everywhere, 3 where the game's effects
// layer has something (TurnDepth::keepEffects), 4 both near things and
// effects; 0: the drawn eye's picture
// wherever it reaches. Applied when the ini's value changes; DEV > Equipment
// switches it for the session (Set), and reads it back.
void ConfigureTurnsKeepOwn(int mode);
void SetTurnsKeepOwn(int mode);
int TurnsKeepOwn();
// [stereo] turns_keep_gap (48), turns_keep_reach (96), a 4096-wide eye's
// pixels: near things are nearer than what is round them by more than the
// gap in parallax; their own picture is kept within the reach of them.
void ConfigureTurnsKeepNear(float gap,float reach);
// [stereo] turns_effects_level (0.02), turns_effects_reach (32, a 4096-wide
// eye's pixels): with turns_keep_own 3 or 4, the eye's own picture is kept
// where the game's effects layer has this much (its strongest channel)
// within the reach.
void ConfigureTurnsKeepEffects(float level,float reach);
// [stereo] turns_sharp (1): by turns, the pictures turned, moved and laid
// over the eye not drawn read Catmull-Rom, sharper than bilinear
// (TurnWarp::Sharpen); 0 bilinear. Applied when the ini's value changes;
// DEV > Equipment switches it for the session (Set), and reads it back.
void ConfigureTurnsSharp(bool on);
void SetTurnsSharp(bool on);
bool TurnsSharp();
// [stereo] turns_fill (1): by turns, in the strips beside near things the
// drawn eye's picture does not reach, where the eye's own frame-old picture
// shows something nearer since gone (a monster that walked on), what is
// behind beside the strip instead (TurnWarp::Fill); 0 its own picture as
// before. Applied when the ini's value changes; DEV > Equipment switches it
// for the session (Set), and reads it back.
void ConfigureTurnsFill(bool on);
void SetTurnsFill(bool on);
bool TurnsFill();
void ConfigureEyeTracking(bool enabled);
void ConfigureGameplay(bool enabled);
void ConfigureQuiver(bool enabled,bool preview); // [bow] quiver, quiver_preview
void ConfigureInteractions(bool seated,float waistBelowEyes,float beltForward,int dominantHand);
void ConfigureBottlePhysics(bool on); // [interactions] bottle_physics: emptied bottles fall into the world
void ConfigurePropPhysics(bool on);   // [interactions] prop_physics: weapons knock the level's small props about
void PublishSewerPrompt(const sewerprompt::Target&); // Values only; XR follows no game pointers.
// Current motion-combat aim in the existing head-yaw-relative hand frame.
bool SettingsOpen();
bool OwnsControllerRumble(); // lock-free; VR input owns feedback instead of gamepad motors
bool SpellSelected();
bool LeftHandAiming();
bool CrossbowAiming();
bool NativeAimOwnership(); // hand casting or wheel: preserve locomotion, suppress legacy aim modes
bool NativeSkillCasting(); // any active trigger/quick cast; retain native locomotion input
bool DirectedSpellActive(); // fresh native assignment, actually casting (wheel or quick shortcut)
bool BowSkillActive();   // a bow or crossbow skill is being cast from a button (bow ready)
int GroundSpellSkill();   // a ground skill (spellaim::GroundTarget) being cast that lands where it is aimed: its id, or -1
int NativeCastSkill(); // current fresh native cast id, -1 when idle/blocked/stale
bool GameplayRay(float origin[3],float direction[3]);
// Aim rules: each hand's own aim in GameplayRay's frame - the controller's
// pointer, or the bow's forward for the left hand holding a bow.
bool HandAimRay(int hand,float origin[3],float direction[3]);
int CastButtonHand();        // the hand whose button holds a native cast now, -1 none
bool HandSpellHeld(int hand); // that hand's physical spell button is held
int PointSpellSkill();       // the cast skill when the aim rules place it at a point, else -1
int AimRulesSkill();         // the cast skill (any), -1 none; with its game "directed" flag
// A trap or bomb thrown to place (#224): where its arc landed, in grid cells,
// for that skill's cast (skill -1: any) just after the throw.
bool TossTarget(int skill,float& x,float& y);
bool AimRulesDirected();
bool MeleeSample(uint32_t poseStamp,melee::Sample& sample,int hand=0);
// The posed melee capsule (world metres) that also pushes dropped bottles:
// slot 0 right hand, 1 left hand, 2 shield. Null clears it. Any thread.
void BottleWeapon(int slot,const melee::Blade* blade);
bool ShieldSample(uint32_t poseStamp,melee::Sample& sample);
bool RequestShieldSkill(const melee::Sample& sample,uint32_t target,math::Vec gridTarget);
int TakeShieldTarget(uint32_t& target); // 1 physical target, -1 cancelled cast, 0 native fallback
bool ShieldAim(float& x,float& y);
void FinishShieldSkill(bool accepted);
void MeleeImpact(int hand=0); // thread-safe; haptic delivered by XR input update
void ShieldGuardImpact(bool parry=false,float weight=1); // thread-safe; a physical block taps the left controller and strains the guard by weight (the attacker's rank; a parry does not)
bool GuardBroken(); // thread-safe; guard strain is full (broken) this frame
void ShieldParryMiss(); // thread-safe; a parry swing that missed buzzes the left controller softly
// Thread-safe, every guard tick: shield eligible (first person, shield, no menu) and raised.
void ShieldGuardState(bool eligible,bool raised);
// [gameplay] guard_strain_*: strain per block (1 = broken), lowered reset time,
// clear radius at full swell (1 = visor edge), broken breathing period, inner softness,
// opacity (0 = none; strain still runs), colour (0xRRGGBB) and
// density (1 = as drawn; higher thickens the partly clear band, for the black rim).
void ConfigureGuardStrain(float perBlock,float resetSeconds,float clear,float breatheSeconds,float softness,float brightness,uint32_t color,float density=1);
// [gameplay] guard_strain_recover_ms: with the shield still raised, strain drains once no
// hit (block or parry) has reached it this long. 0 = only lowering drains. Any thread.
void ConfigureGuardStrainRecover(float seconds);
// [gameplay] low_life_heartbeat (x the game's Master x Combat volume, 0 = off), tempo
// at the first 10 % step and below 10 % life, wind-down time from full, and an optional
// one-beat 16-bit PCM WAV (empty: the built-in beat). Reloads the WAV only when the path changes.
void ConfigureHeartbeat(float volume,float bpmMin,float bpmMax,float fadeSeconds,const wchar_t* file);
// [gameplay] guard_break_volume (x the game's Master x Combat volume, 0 = off) and an
// optional 16-bit PCM WAV (empty: the built-in bone snap) played once as the guard breaks.
void ConfigureGuardBreakSound(float volume,const wchar_t* file);
void SpellCast(int hand,uint64_t session); // one successful hand spell, not each projectile
void SpellVisualCast(const bowflight::Shot&); // confirmed hand cast; cosmetic only
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
// One headset frame as the render thread measured it, for VR Settings > Perf:
// the runtime's wait (xrWaitFrame), the game's drawing (one eye by turns, both
// in a pair), the mod's own work around it without that wait, and the
// headset's frame (budgetMs). periodMs is the time since the last frame ended
// and gameMs the game's update between them; both 0 after menus or pairs.
void NoteFrame(double gameMs,double waitMs,double drawMs,double vrMs,double periodMs,double budgetMs,bool byTurns);
void Shutdown(bool preserveResolution=false);
// byTurns: this frame draws one eye; EndPair submits the other eye's last
// image again at the pose and FOV it was drawn with. replay: the right eye is
// drawn from the left pass (vr/stereo_replay.h) as the left eye moved along
// its own x, and placed so.
void BeginPair(bool byTurns=false,bool replay=false);
void BeginEye(int eye);
// VR Settings > Perf > Stereo rendering: 0 both eyes, 1 By turns (the eye not
// drawn turned and moved with the scene's depth; the default). Replay (2) is
// no longer a choice (vr_settings.h).
int StereoRendering();
// [render] defer_gpu_wait: frames end without waiting for the GPU.
bool GpuWaitDeferred();
// Replay: no capture waits for a pair of two passes.
bool ReplayReady();
// After BeginPair(...,true): the frame begun is a stereo one the replay can
// draw (rendering, views located, no flat panel).
bool ReplayPair();
// The eyes' gap along the left eye's x, in the world's units (View), and half
// the left minus right eye's off-axis projection term: the replay's camera.
float ReplayShift();
float ReplayProjShift();
// At a held present (the replay's left eye): its picture to the headset as at
// the present's effects.
void CaptureEffects(reshade::api::effect_runtime* rt);
// Both eyes have a submitted image to reuse and no capture waits for a pair.
bool TurnReady();
// By turns: scenedepth copies the depth every present for it.
bool TurnDepthWanted();
// Both eyes' last images have their depth and place kept: walking can go by turns.
bool TurnDepthReady();
// How the eyes not drawn went since the last call: moved with depth, or turned only.
// By turns, the eye not drawn: its own image moved with its depth, or turned
// only; and of those, how many had the drawn eye's picture laid over them.
struct TurnCounts {uint32_t moved=0,turned=0,drawn=0;};
TurnCounts TakeTurnCounts();
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
// scaleHeight: the eye height the world's scale is taken from (units per metre over
// userEyeHeight), 0 = eyeHeight; a druid's form stands its eyes apart from it.
bool View(float out[16], const float hero[3], float eyeHeight,
          float userEyeHeight, float bodyHeading, float recenterYaw, int eye, float mapNorth, float scaleHeight = 0);
bool Projection(float out[16], float nearWorld, int eye);
}
