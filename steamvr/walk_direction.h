#pragma once
// Walk direction (VR Settings > Body, beside Dominant hand; [stick] walk_direction): the left stick walks
// where you look (0, the default) or where the left controller points (1). Owner, 2026-11-08: "moving where
// the controller points versus where you look, that should be a setting".
//
// Downstream the stick is read in the view's frame (x right, y ahead on the ground), turned by the head's
// yaw (vrcam ViewStickToGame), so pointing is the stick turned here by the controller's yaw relative to
// the head's before it is written. Tracking space is OpenXR's (+x right, +y up, -z ahead); a heading
// grows turning left (frame_math Heading).
#include "frame_math.h"
#include <cmath>

namespace framevr::walkdir {
using math::Vec;using math::Quat;
enum Mode {Look=0,Controller=1};
// The aim pose pointing nearly straight up or down has no heading: keep the last one.
constexpr float MinHorizontal=.3f;
struct Pointing {
    bool known=false;float heading=0;
    // The controller's heading now, or the last good one; false while never known.
    bool Update(Quat aim,bool tracked) {
        if(tracked) {
            const Vec f=math::Rotate(aim,{0,0,-1});
            if(std::sqrt(f.x*f.x+f.z*f.z)>=MinHorizontal){heading=std::atan2(-f.x,-f.z);known=true;}
        }
        return known;
    }
};
// The stick (x right, y ahead) meant in the controller's frame, as the same walk in the head's frame.
inline void ToView(float& x,float& y,float controllerHeading,float headHeading) {
    const float d=controllerHeading-headHeading,c=std::cos(d),s=std::sin(d);
    const float vx=x*c-y*s,vy=x*s+y*c;x=vx;y=vy;
}
}
