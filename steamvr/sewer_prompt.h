#pragma once
#include "frame_math.h"
#include "world_coordinates.h"
#include <cstdint>

namespace framevr::sewerprompt {
constexpr int AtlasX=512,AtlasY=384,Width=512,Height=128;
// Left-handed play (hand_role.h): the same prompt naming the main hand's button there, "Left X", drawn at
// 7/8 size into the atlas's free corner (the bow's old text, gone 2026-11-09). The panel is the same size.
constexpr int LeftAtlasX=576,LeftAtlasY=768,LeftWidth=448,LeftHeight=112;
inline constexpr const wchar_t* Shortcut=L"Right A";
inline constexpr const wchar_t* LeftShortcut=L"Left X";
constexpr float PanelWidth=.38f,PanelHeight=.095f;
// Value-only UI -> XR message. No native unit or path pointer crosses threads.
struct Target {uint64_t session=0,at=0;int level=0;uint32_t id=0;float x=0,y=0;};
inline bool Fresh(const Target& t,uint64_t session,uint64_t now,int level) {
    return session&&t.session==session&&now>=t.at&&now-t.at<=200&&
        (level==80||level==81)&&t.level==level&&std::isfinite(t.x)&&std::isfinite(t.y)&&
        t.x>0&&t.y>0&&t.x<=65535&&t.y<=65535;
}
inline bool Position(const Target& t,math::Vec origin,math::Vec hero,math::Quat base,
                     float upm,float eyeHeight,math::Vec head,math::Quat headRotation,math::Vec& out) {
    const auto unit=[](math::Quat q){const float n=q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w;return std::isfinite(n)&&std::abs(n-1)<.01f;};
    if(!coordinates::Finite(origin)||!coordinates::Finite(hero)||!coordinates::Finite(head)||
       !unit(base)||!unit(headRotation)||!std::isfinite(upm)||upm<1||upm>12||!std::isfinite(eyeHeight)||eyeHeight<=0)return false;
    // Same grid shear and room/world mapping used by physical weapon contacts.
    const auto native=coordinates::RenderGround({2*t.x/upm,0,2*t.y/upm},hero.y/upm);
    const math::Vec eye={hero.x/upm,(hero.y+eyeHeight)/upm,hero.z/upm};
    out=origin+math::Rotate(math::Conjugate(base),native-eye);
    auto offset=out-head;offset.y=0;
    float distance=std::hypot(offset.x,offset.z);
    if(!std::isfinite(distance)||distance>8)return false;
    if(distance<.9f) {
        offset=math::Rotate(headRotation,{0,0,-1});offset.y=0;
        distance=std::hypot(offset.x,offset.z);if(distance<.05f)return false;
        offset=offset*(.9f/distance);distance=.9f;
    }
    // Preserve entrance bearing, while keeping text readable on the trigger.
    out=head+offset*(std::max(.9f,distance)/distance);out.y=head.y-.22f;
    return coordinates::Finite(out);
}
}
