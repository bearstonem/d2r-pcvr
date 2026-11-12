#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string_view>
#include "vr_resolution.h"

// The game's cinematics on a screen in the world (owner 2026-11-12: "making sure that the cutscenes
// are playing on a in 3D worlds quad plane").
//
// How D2R plays them: each is a WebM (data/hd/local/video/<name>.webm, with its .flac) shown by a
// UI widget, MoviePlayerWidget, in a movie panel - InGameMoviePlayerPanel in a game (the act videos
// and the ending, asked for by 'ProgressionMessage' 'PlayVideo'), MoviePlayerPanel from the main
// menu's Cinematics, LogoMenuPanel for the logos and the first launch's intro. TitleScreenPanel
// loops its logo ('PlayLooped'): a menu, not a cinematic. Being a widget, the movie is drawn in the
// interface's full-screen box: the UI canvas band at the top of the frame (resolution::UiCanvas),
// the whole frame when it is 16:9.
//
// What the headset showed: in a game the world stops being drawn while one plays (the 2026-10-15
// Act II -> III video: no stereo pair for its 4 min 22 s, 'MoviePlayerMessage' 'Play' at
// 16:40:57.044 to 'Cinematics' 'MovieComplete' at 16:45:19.164, pairs again two seconds later),
// but the area stays a world and no menu is open, so the frames stayed on the stereo path with
// the eye of the last pair: one eye's copy a frame and never the other's - no layer submitted, or
// a frame never ended. Out of a game a movie was the flat menu panel's whole frame, ~1 m away,
// the movie in its upper part.
//
// Told by the game's own UI messages (data, not code bytes: no game build is assumed), with two
// failsafes: in a game, the world drawn again for a while (a message missed), and a cap.
namespace framevr::cinema {

enum class Signal : uint8_t { None, Start, End };

// The movie panels: one closed or unloaded ends what played in it.
inline bool MoviePanel(std::string_view name) {
    return name=="InGameMoviePlayerPanel"||name=="MoviePlayerPanel"||name=="LogoMenuPanel";
}

// One UI message (target, command, text) as the D2RLoader UI message event gives it.
inline Signal Classify(std::string_view target,std::string_view command,std::string_view text) {
    if(target=="MoviePlayerMessage") {
        if(command=="Play")return Signal::Start;
        // PlayLooped: the title screen's loop, a menu. Cancel: sent as a movie panel closes. Stop: not seen yet.
        if(command=="PlayLooped"||command=="Cancel"||command=="Stop")return Signal::End;
        return Signal::None;   // DisableHoldToSkip and the like: still playing
    }
    if(target=="Cinematics")   // MovieComplete: an act video done; Close: the main menu's Cinematics left
        return command=="MovieComplete"||command=="Close"?Signal::End:Signal::None;
    if(target=="FrontEndNavigation")return Signal::End;   // to the main menu, a game, the exit: menus
    if(target=="PanelManager") {
        if(command=="UnloadAll"||command=="CloseAll")return Signal::End;
        if((command=="ClosePanel"||command=="UnloadPanel")&&MoviePanel(text))return Signal::End;
    }
    return Signal::None;
}

enum class Why : uint8_t { None, Message, WorldDrawn, TooLong };
inline const char* Reason(Why why) {
    switch(why) {
    case Why::Message:return "the game's message";
    case Why::WorldDrawn:return "the game drew its world again (no end message seen)";
    case Why::TooLong:return "longer than any cinematic (a stuck start?)";
    default:return "";
    }
}
struct Change { bool changed=false,playing=false; Why why=Why::None; };

// Whether a cinematic plays. Not thread-safe: the caller holds one lock round all of it.
class Tracker {
public:
    // The game's world drawn (DrawGameScreen) this long without a gap ends a cinematic whose end
    // message was missed: none draws it while a movie plays. Draws in the first GraceMs of a
    // movie are the frames already on their way.
    static constexpr uint64_t GraceMs=2000,WorldMs=750,GapMs=250;
    // The longest of D2R's cinematics is a few minutes; past this a start was stuck.
    static constexpr uint64_t MaxMs=12*60*1000;

    Change Message(Signal signal,uint64_t now) {
        if(signal==Signal::Start) {
            const bool was=playing;
            playing=true;since=now;worldFrom=worldLast=0;
            return {!was,true,Why::Message};
        }
        if(signal==Signal::End&&playing)return Stop(Why::Message);
        return {};
    }
    Change WorldDrawn(uint64_t now) {
        if(!playing||now<since+GraceMs)return {};
        if(!worldFrom||now>worldLast+GapMs)worldFrom=now;
        worldLast=now;
        return now-worldFrom>=WorldMs?Stop(Why::WorldDrawn):Change{};
    }
    Change Tick(uint64_t now) {
        return playing&&now>=since+MaxMs?Stop(Why::TooLong):Change{};
    }
    bool Playing() const {return playing;}
    uint64_t Since() const {return since;}
private:
    Change Stop(Why why){playing=false;worldFrom=worldLast=0;return {true,false,why};}
    bool playing=false;
    uint64_t since=0,worldFrom=0,worldLast=0;
};

// The part of the frame the movie is in: the interface's full-screen box.
struct Crop { uint32_t x=0,y=0,w=0,h=0; };
inline Crop MovieBand(uint32_t width,uint32_t height) {
    const auto band=resolution::UiCanvas({width,height});
    return {0,0,band.w,band.h};
}
// [cinema] distance and width (metres): a screen ahead at eye height, about 53 degrees across by
// default - most of the view, none of it at the edges of the lenses.
struct Screen { float distance=3.f,width=3.f; };
inline Screen Clamped(float distance,float width) {
    const auto keep=[](float v,float fallback,float lo,float hi){return std::isfinite(v)?std::clamp(v,lo,hi):fallback;};
    return {keep(distance,3.f,1.f,10.f),keep(width,3.f,.5f,12.f)};
}
struct Size { float width=0,height=0; };
// The screen's size in metres: the band's own shape at the width asked for.
inline Size ScreenSize(Crop band,float width) {
    if(!band.w||!band.h)return {width,width*9.f/16.f};
    return {width,width*float(band.h)/float(band.w)};
}
// A laser hit (u across, v down the screen) as a share of the whole frame, which is what the
// game's window stands for (its pointer conversions take the full VR source).
inline void ToFrame(float u,float v,Crop band,uint32_t width,uint32_t height,float& x,float& y) {
    if(!width||!height||!band.w||!band.h){x=u;y=v;return;}
    x=(float(band.x)+std::clamp(u,0.f,1.f)*float(band.w))/float(width);
    y=(float(band.y)+std::clamp(v,0.f,1.f)*float(band.h))/float(height);
}
}
