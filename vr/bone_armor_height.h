#pragma once
#include "portal_billboard.h"
#include <cmath>
#include <string_view>

// Necromancer Bone Armor (data/hd/vfx/particles/overlays/necro/bonearmor_front/
// vfx_BoneArmor_Front.particles) places its orbit by one mask-19 local point,
// (0, 3, 0) above the unit's origin: waist height in VR. Unlike the Paladin
// auras (overlay_view.h) it never asks for the view direction, so the aura
// fix cannot lower it. Developer capture 2026-10-25: 192 samples, every one
// local (0,3,0), stride 0, world output = origin + 3.00 units straight up,
// yaw-only transform. That centre is replaced by (0, Height, 0); the native
// transform keeps the orbit on the unit as it moves.
namespace billboards::bone_armor {
inline bool Asset(std::string_view path) {
    constexpr std::string_view exact="data/hd/vfx/particles/overlays/necro/bonearmor_front/vfx_bonearmor_front.particles";
    if(path.size()!=exact.size())return false;
    for(size_t i=0;i<exact.size();++i) {
        char c=path[i];if(c>='A'&&c<='Z')c=char(c+'a'-'A');if(c=='\\')c='/';
        if(c!=exact[i])return false;
    }
    return true;
}
// Native centre height in game units (~5.25 per metre: about 57 cm).
constexpr float Native=3.f;
// Default: just above the floor (Cal, 2026-10-25), about 6 cm.
constexpr float Default=.3f;
// [render] bone_armor_height: 0 (the floor) .. 3 (native). Anything else,
// or a non-number, falls back to the default.
inline float Height(float configured) {
    return std::isfinite(configured)&&configured>=0.f&&configured<=Native?configured:Default;
}
// Only the captured native centre moves; any other local point an emitter
// converts stays native. False when the height is the native one.
inline bool Lowered(portal::Vec local,float height,portal::Vec& result) {
    if(!portal::Finite(local)||std::fabs(local.x)>1e-3f||std::fabs(local.z)>1e-3f||
       std::fabs(local.y-Native)>1e-3f||std::fabs(height-Native)<1e-4f)return false;
    result={0.f,height,0.f};return true;
}
}
