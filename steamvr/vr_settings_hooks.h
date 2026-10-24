#pragma once
// VR Settings hooks: the only place the menu's layout names a setting by its
// Key (docs/VR_SETTINGS_MENU.md). Everything else the layout does comes
// from the data: Definitions and the overrides (vr_settings_content.h). When
// upstream removes or renames one of these settings, the build stops here,
// and the error points at the behavior to drop or move.
#include "vr_settings.h"
namespace framevr::settings::hooks {
// Body's Measurements row: Calibrate body and Set eye height, locked while a
// draft waits (Calibrate saves its own measurements).
inline bool BodyMeasurements(Key k){return k==BodyCalibration;}
// Offline difficulty: main's notice under the row and on its card while the
// offline scaling adapter is missing, the difficulty note on its card otherwise.
inline bool OfflineAdapterNotice(Key k){return k==OfflinePlayers;}
// Button names: the controllers the headset reports, under the row.
inline bool ShowsDetectedControllers(Key k){return k==ControllerLabels;}
// Frame generation: what OFXR Bridge is doing (main's Perf status line) on its row and card.
inline bool ShowsFrameGenStatus(Key k){return k==FrameGeneration;}
// Why a row RowEnabled locks is locked: frame generation turns on only with
// Both eyes (main's #316; once on it can still be changed or turned off).
inline const char* LockedReason(Key k){return k==FrameGeneration?"Needs Both eyes drawn":"Not available now";}
// The guide's button names: Button names' choice.
inline controllers::Choice ButtonNames(const Values& v){return controllers::Choice(int(v[ControllerLabels]));}
// Skills go to the aimed monster: a class without bow skills sees Off / On,
// which flips only the spell bit and keeps a saved bow-skills bit (LockStep).
inline bool SpellsOnly(Key k,const Context& c){return k==SpellLock&&!BowSkillClass(c);}
inline float ClassValue(Key k,float value,float current,const Context& c){
    return SpellsOnly(k,c)?float((int(value)&1)|(int(current)&2)):value;
}
// Rows whose steps Model::Click owns: LockStep's class rule, and the arrow
// path line's wheel through Off (... 345, Off, 0 ...).
inline bool ModelSteps(Key k){return k==SpellLock||k==BowLaser;}
// Presets never set these: the owner's calls (stereo rendering, the GPU wait,
// elemental hands, the smarter mercenary) and calibration's own keys.
inline bool PresetSkips(Key k){
    return k==StereoRendering||k==DeferGpuWait||k==ElementalHands||k==CompanionAi||k==EyeHeight||k==ArmSpan||k==BodyCalibration;
}
// A preset card lists these first, in the comfort color: they change how the
// game feels on the stomach.
inline bool ComfortKey(Key k){return k==SmoothTurn||k==TurnSpeed||k==Snap||k==StereoRendering||k==BloodVisor||k==HeartbeatStart;}
}
