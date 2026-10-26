#pragma once
#include "portal_billboard.h"

// Paladin aura overlays (data/hd/vfx/particles/overlays/paladin/, also used
// by monsters and hirelings with those auras) push their particles toward the camera when they spawn: local point =
// -view.direction * k (Holy Freeze: k = 3), baked into each particle. On the
// flat screen that slides a particle along its own line of sight, so it only
// changes draw order and keeps the ring under the hero. In VR the push is
// sideways: toward the head, or toward the flat camera's fixed spot when the
// effect spawned with the game's view, which left the ring a constant
// direction off the feet. These effects get a straight-down view direction
// from the game camera's own vertical part, so they lose the sideways part
// and keep a smaller lift (Lift). Other overlays (shrine symbols, Bone Armor,
// curses) keep their native view direction until each is checked in VR.
namespace billboards::overlay_view {
// data/hd/vfx/particles/overlays/paladin/..., any case or separator.
inline bool Asset(std::string_view path) {
    constexpr std::string_view prefix="data/hd/vfx/particles/overlays/paladin/";
    constexpr std::string_view suffix=".particles";
    if(path.size()<=prefix.size()+suffix.size()||path.size()>159)return false;
    auto lower=[](char c){return c>='A'&&c<='Z'?char(c+'a'-'A'):(c=='\\'?'/':c);};
    for(size_t i=0;i<prefix.size();++i)if(lower(path[i])!=prefix[i])return false;
    for(size_t i=0;i<suffix.size();++i)if(lower(path[path.size()-suffix.size()+i])!=suffix[i])return false;
    return path.find("..")==path.npos;
}
// Share of the game camera's lift kept. The full lift (k * 0.5 = 1.5 units,
// about 29 cm, for k = 3) floats the ring above the floor in VR, where the
// push is no longer along the line of sight; 0.475 lowers it about 15 cm
// (headset tests 2026-10-21: 20 cm lower, then 5 cm back up; ~5.25 units
// per metre).
constexpr float Lift=.475f;
// gameForward: the game camera's own view direction this frame (world, unit
// length, looking down). The replacement keeps only Lift of its vertical part.
inline bool Upright(portal::Vec gameForward,portal::Vec& result) {
    if(!portal::Finite(gameForward))return false;
    const float length2=gameForward.x*gameForward.x+gameForward.y*gameForward.y+gameForward.z*gameForward.z;
    if(std::fabs(length2-1.f)>.01f||gameForward.y>=0.f)return false;
    result={0,gameForward.y*Lift,0};return true;
}
}
