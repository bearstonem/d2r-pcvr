#pragma once
#include "bow_flight.h"
#include "vr_settings.h"
#include <vector>

// Bow path laser: a faint straight line from the drawn arrow's tip along the
// direction a release would fly. Physical arrows have no gravity, so the line
// is the real path. It stops where the flight meets the launch floor; walls
// and monsters are tested only by the flight itself, so the line otherwise
// ends at MaxLength. Room meters throughout; yaw and scale keep it straight.
namespace framevr::bowlaser {
using math::Vec;
inline constexpr float MaxLength=15; // meters, without a wall or monster test
struct Path {Vec start{},end{};float length=0;bool floor=false;};
// Shown only while a release now would shoot: the same full-draw gate and
// forward check as the bow's release.
inline bool Shown(bool charged,Vec direction,Vec bowForward) {
    return charged&&bowflight::Finite(direction)&&math::Dot(direction,bowForward)>.15f;
}
// tail is the nocked arrow's tail (the string hand); floorY the launch floor's
// height in the same space. Length 0 hides the line.
inline Path Trace(Vec tail,Vec direction,bool floorKnown,float floorY) {
    Path p;
    if(!bowflight::Finite(tail)||!bowflight::Finite(direction)||std::fabs(bowflight::Length(direction)-1)>.01f)return p;
    p.start=p.end=tail+direction*bowflight::ArrowLength;p.length=MaxLength;
    if(floorKnown&&std::isfinite(floorY)) {
        const float floor=floorY+bowflight::FloorClearance,above=p.start.y-floor;
        if(above<=0)return {p.start,p.start,0,true};
        if(direction.y<-1e-4f&&above/-direction.y<MaxLength){p.length=above/-direction.y;p.floor=true;}
    }
    p.end=p.start+direction*p.length;return p;
}
// Its own small RGBA atlas in the chosen hue (VR Settings > Combat > Bow path
// laser), lightened a little: a steady faint beam (to the floor), a beam
// fading out toward its far end, and a brighter end dot. A beam quad's top
// edge is its far end.
inline constexpr int AtlasWidth=64,AtlasHeight=160;
inline constexpr int SteadyY=0,SteadyHeight=32,FadeY=32,FadeHeight=64,DotY=96,DotSize=64;
inline constexpr uint8_t BeamAlpha=96,DotAlpha=170;
inline std::vector<uint8_t> Paint(float hue) {
    float rgb[3];settings::HueRgb(hue,rgb);
    std::vector<uint8_t> pixels(AtlasWidth*AtlasHeight*4);
    for(int y=0;y<AtlasHeight;++y)for(int x=0;x<AtlasWidth;++x) {
        auto* p=pixels.data()+(y*AtlasWidth+x)*4;
        for(int i=0;i<3;++i)p[i]=uint8_t(std::lround((rgb[i]*.75f+.25f)*255));
        const float radius=std::hypot((x-31.5f)/30.f,(y-DotY-31.5f)/30.f);
        p[3]=y<FadeY?BeamAlpha:y<DotY?uint8_t(BeamAlpha*(y-FadeY)/(FadeHeight-1)):
            uint8_t(std::clamp((1-radius)*10,0.f,1.f)*DotAlpha);
    }
    return pixels;
}
}
