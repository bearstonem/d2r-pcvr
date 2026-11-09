#pragma once
#include "respec_button.h"
#include "menu_controls.h"

namespace framevr::stashbuttons {
// Bank is the game's stash layout; inventory alone also appears at merchants.
template<class Names> inline bool Shown(const Names& open) {
    bool stash=false;
    for(const auto& name:open) {
        const std::string_view n(name);
        if(n.starts_with("HoradricCube")||n.starts_with("Vendor")||
           n.starts_with("Trade")||n.starts_with("Gamble"))return false;
        stash=stash||n.starts_with("Bank")||n.starts_with("Stash");
    }
    return stash;
}
constexpr int Width=240,Height=144;
constexpr int LabelHeight=26;
inline constexpr const wchar_t* Labels[]={L"< Previous tab",L"Next tab >"};
// Inset into the native recesses flanking the stash gold field. Estimated
// from the in-game screenshot against the previous known button positions.
// Rendered faces and pointer hit boxes share this 16:9-band placement.
constexpr float ButtonWidth=.076f,ButtonHeight=.082f,CenterY=.715f;
constexpr float PreviousX=.218f,NextX=.404f;
inline respecbutton::Spot Place(float width,float height,int direction) {
    if(!(width>0)||!(height>0))return {};
    const float band=std::min(height,width*9.f/16.f),bandWidth=band*16.f/9.f;
    const float h=ButtonHeight*band;
    return {((direction?NextX:PreviousX)-.5f)*bandWidth,
        height*.5f-CenterY*band,ButtonWidth*bandWidth,h};
}
// Dark inset stone with nearly square corners and a thin, muted bevel.
// Keep this styling local to stash navigation; other panel buttons keep theirs.
inline std::vector<uint8_t> PaintFace(int width,int height,bool highlighted) {
    std::vector<uint8_t> pixels(size_t(std::max(width,0))*size_t(std::max(height,0))*4,0);
    if(width<=0||height<=0)return pixels;
    const float aspect=float(width)/height;
    const auto roundedBox=[](float x,float y,float halfWidth,float halfHeight,float radius) {
        const float dx=std::fabs(x)-halfWidth+radius,dy=std::fabs(y)-halfHeight+radius;
        return std::hypot(std::max(dx,0.f),std::max(dy,0.f))+std::min(std::max(dx,dy),0.f)-radius;
    };
    constexpr int Samples=4;
    for(int py=0;py<height;++py)for(int px=0;px<width;++px) {
        float accumulated[4]{};
        for(int sy=0;sy<Samples;++sy)for(int sx=0;sx<Samples;++sx) {
            const float x=(px+(sx+.5f)/Samples)/height,y=(py+(sy+.5f)/Samples)/height;
            const float inset=1.25f/height;
            const float edge=roundedBox(x-aspect*.5f,y-.5f,aspect*.5f-inset,.5f-inset,3.f/height);
            if(edge>0)continue;
            const float grain=.65f*statsbutton::StoneNoise(x*11,y*11)+.35f*statsbutton::StoneNoise(x*31+3,y*31+5);
            float stone=(highlighted?.12f:.085f)+.035f*(grain-.5f);
            const bool rim=edge>-1.6f/height,innerLip=!rim&&edge>-3.5f/height;
            if(innerLip)stone*=.45f; // inner shadow makes the center look recessed
            if(!rim)stone*=.82f+.18f*y;
            float color[3]={stone,stone*.98f,stone*.94f};
            if(rim) {
                const float brightness=.75f+.25f*y;
                color[0]=(highlighted?.48f:.27f)*brightness;
                color[1]=(highlighted?.40f:.25f)*brightness;
                color[2]=(highlighted?.25f:.20f)*brightness;
            }
            for(int channel=0;channel<3;++channel)accumulated[channel]+=std::clamp(color[channel],0.f,1.f);
            accumulated[3]+=1;
        }
        auto* output=&pixels[(size_t(py)*width+px)*4];
        for(int channel=0;channel<4;++channel)
            output[channel]=uint8_t(std::clamp(accumulated[channel]/float(Samples*Samples),0.f,1.f)*255.f+.5f);
    }
    return pixels;
}
// One fresh pull owns one direction until release, even if the ray crosses
// to the other button. Cancel immediately when the stash/focus is lost.
class Input {
    statsbutton::Press press;
    int selected=-1;
    uint64_t pulseUntil=0;
public:
    void Reset(){press.Reset();selected=-1;pulseUntil=0;}
    uint16_t Step(bool active,int hover,bool trigger,uint64_t now) {
        if(!active){Reset();return 0;}
        if(press.Step(hover>=0,trigger,now)) {
            selected=hover;
            pulseUntil=now+statsbutton::MinMs;
        }
        // A short pulse lets the game poll the bumper without auto-repeat.
        return press.Held()&&now<pulseUntil?(selected==0?menus::LeftBumper:menus::RightBumper):0;
    }
    bool Held() const{return press.Held();}
};
}
