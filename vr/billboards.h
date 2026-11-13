#pragma once
namespace billboards {
void Install(void (*log)(const char*), bool (*active)());
// The cyclopean position used by the current stereo pair, in native world units.
void SetCenter(const float position[3]);
void ConfigurePortalCenters(bool enabled);
// Necromancer Bone Armor's orbit centre in game units above the unit: 0..3,
// 3 = native (waist), default 0.3 (just above the floor). bone_armor_height.h.
void ConfigureBoneArmorHeight(float units);
// With the forearm grafts on (steamvr/bone_graft.h), the hero's own Bone Armor
// orbit is never shown, in any view or menu; off: the native orbit.
void ConfigureBoneArmorHidden(bool hidden);
// Hide only the archive-confirmed permanent ice asset near our hero, leased
// by the active Frozen Armor forearm preview (never by the selected skill).
void ConfigureFrozenArmorHidden(bool hidden);
void SetFrozenArmorGraftActive(bool active);
// The hero's point (vrcam's camera target, the feet, game world units), every
// view build, menus included: which Bone Armor is the hero's. Stale after 500 ms.
void SetHero(const float position[3]);
// The game camera's own view direction this frame (world, unit length). Unit
// overlays (auras) take its vertical part as their view direction.
void SetGameForward(const float forward[3]);
}
