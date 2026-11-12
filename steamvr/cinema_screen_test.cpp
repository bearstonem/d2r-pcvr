#include "cinema_screen.h"
#include "native_panels.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
using namespace framevr;
using cinema::Signal;
static void Check(bool ok,const std::string& label){if(!ok){std::fprintf(stderr,"FAIL: %s\n",label.c_str());std::exit(1);}}
static bool Near(double a,double b,double within=1e-4){return std::fabs(a-b)<=within;}
static std::string Slurp(const char* path){   // line ends as LF, whatever the checkout wrote
    std::ifstream f(path,std::ios::binary);std::string text{std::istreambuf_iterator<char>(f),{}};
    std::erase(text,'\r');return text;
}
// A source file holds the line (whitespace as written).
static void Has(const std::string& text,const char* file,const char* line,const char* label){
    Check(text.find(line)!=std::string::npos,std::string(file)+": "+label+" (missing: "+line+")");
}

struct Ui {const char* target;const char* command;const char* text;};
// Replays UI messages through Classify and a tracker, one millisecond apart from `at`.
static bool Replay(cinema::Tracker& t,const std::vector<Ui>& messages,uint64_t& at){
    for(const auto& m:messages)t.Message(cinema::Classify(m.target,m.command,m.text),at++);
    return t.Playing();
}

int main(int argc,char** argv){
    Check(argc>=4,"usage: transport.cpp vrcam.cpp gamestate.cpp");
    {   // The messages the game sent in the owner's logs.
        Check(cinema::Classify("MoviePlayerMessage","Play","")==Signal::Start,"a movie played starts a cinematic");
        Check(cinema::Classify("MoviePlayerMessage","PlayLooped","")==Signal::End,"the title screen's loop is a menu, not a cinematic");
        Check(cinema::Classify("MoviePlayerMessage","Cancel","")==Signal::End,"Cancel ends it");
        Check(cinema::Classify("MoviePlayerMessage","DisableHoldToSkip","")==Signal::None,"hold-to-skip changes nothing");
        Check(cinema::Classify("Cinematics","MovieComplete","")==Signal::End,"an act video complete");
        Check(cinema::Classify("Cinematics","Close","")==Signal::End,"the main menu's Cinematics left");
        Check(cinema::Classify("FrontEndNavigation","ToFrontEnd","")==Signal::End,"menus");
        Check(cinema::Classify("PanelManager","UnloadAll","")==Signal::End,"every panel gone");
        for(const char* panel:{"InGameMoviePlayerPanel","MoviePlayerPanel","LogoMenuPanel"}) {
            Check(cinema::Classify("PanelManager","ClosePanel",panel)==Signal::End,std::string(panel)+" closed ends it");
            Check(cinema::Classify("PanelManager","UnloadPanel",panel)==Signal::End,std::string(panel)+" unloaded ends it");
            Check(cinema::Classify("PanelManager","OpenPanel",panel)==Signal::None,std::string(panel)+" opened is not a movie yet");
        }
        // The in-game movie panel is open all game (it is in every "menu OPEN" list): only a movie counts.
        Check(cinema::Classify("PanelManager","ClosePanel","NpcDialogPanel")==Signal::None,"other panels change nothing");
        Check(cinema::Classify("ProgressionMessage","PlayVideo","")==Signal::None,"the request alone waits for the movie's own Play");
        Check(cinema::Classify("LogoMenu","NextMovie","")==Signal::None,"the next logo: still playing");
        Check(cinema::Classify("InputMessage","Play","")==Signal::None,"another target's Play is not a movie");
    }
    {   // 2026-10-15 16:40:57: Meshif's boat to Kurast - the Act II -> III video in a game.
        cinema::Tracker t;uint64_t at=1000;
        Check(Replay(t,{{"PanelManager","ClosePanel","NpcDialogPanel"},{"ProgressionMessage","PlayVideo",""},
                        {"MoviePlayerMessage","Play",""}},at),"the act video plays");
        Check(!Replay(t,{{"Cinematics","MovieComplete",""}},at),"and ends with MovieComplete");
    }
    {   // 2026-10-14 16:24: a first launch - the logos, the title screen, calibration, the intro, the main menu.
        cinema::Tracker t;uint64_t at=1000;
        Check(Replay(t,{{"PanelManager","OpenPanel","LogoMenuPanel"},{"MoviePlayerMessage","Play",""},
                        {"MoviePlayerMessage","DisableHoldToSkip",""},{"LogoMenu","NextMovie",""}},at),"the logos play");
        Check(!Replay(t,{{"PanelManager","ClosePanel","LogoMenuPanel"},{"MoviePlayerMessage","Cancel",""},
                         {"PanelManager","OpenExclusivePanel","TitleScreenPanel"},{"MoviePlayerMessage","PlayLooped",""}},at),
              "the title screen's loop is the menu panel's");
        Check(Replay(t,{{"PanelManager","UnloadPanel","GammaCalibrationPanel"},{"LogoMenu","PlayIntro",""},{"MoviePlayerMessage","Play",""}},at),
              "the intro plays");
        Check(!Replay(t,{{"PanelManager","UnloadPanel","LogoMenuPanel"},{"PanelManager","OpenPanel","TitleScreenPanel"}},at),
              "and ends as its panel goes");
    }
    {   // A launch that skips the logos: the panel opens and goes, no movie.
        cinema::Tracker t;uint64_t at=1000;
        Check(!Replay(t,{{"PanelManager","OpenPanel","LogoMenuPanel"},{"PanelManager","UnloadPanel","LogoMenuPanel"},
                         {"MoviePlayerMessage","Cancel",""},{"PanelManager","OpenExclusivePanel","TitleScreenPanel"}},at),"nothing plays");
    }
    {   // The main menu's Cinematics: picked, played, left (2026-10-15 23:23 opened and closed it).
        cinema::Tracker t;uint64_t at=1000;
        Check(!Replay(t,{{"FrontEndNavigation","ToCinematics",""},{"PanelManager","OpenPanel","CinematicsModal"},
                         {"PanelManager","OpenPanel","MoviePlayerPanel"}},at),"the list is a menu");
        Check(Replay(t,{{"MoviePlayerMessage","Play",""}},at),"one picked plays");
        Check(!Replay(t,{{"Cinematics","Close",""},{"PanelManager","UnloadPanel","MoviePlayerPanel"}},at),"leaving the list ends it");
    }
    {   // A game left during a video: every panel unloaded.
        cinema::Tracker t;uint64_t at=1000;
        Replay(t,{{"MoviePlayerMessage","Play",""}},at);
        Check(!Replay(t,{{"PanelManager","UnloadPanel","InGameMoviePlayerPanel"}},at),"the in-game movie panel unloaded ends it");
    }
    {   // Changes are told once each way.
        cinema::Tracker t;
        auto c=t.Message(Signal::Start,10);Check(c.changed&&c.playing&&c.why==cinema::Why::Message,"a start is a change");
        c=t.Message(Signal::Start,20);Check(!c.changed&&c.playing&&t.Since()==20,"a second movie (the next logo) is no change, timed anew");
        c=t.Message(Signal::End,30);Check(c.changed&&!c.playing,"an end is a change");
        c=t.Message(Signal::End,40);Check(!c.changed,"a second end is none");
        c=t.Message(Signal::None,50);Check(!c.changed&&!t.Playing(),"no signal, nothing");
    }
    {   // The failsafe in a game: no world is drawn while a movie plays.
        using T=cinema::Tracker;
        cinema::Tracker t;t.Message(Signal::Start,1000);
        for(uint64_t now=1000;now<1000+T::GraceMs;now+=11)Check(!t.WorldDrawn(now).changed,"frames on their way when it starts end nothing");
        Check(t.Playing(),"still playing after the grace");
        // A few stray draws, then a gap: no end.
        uint64_t now=1000+T::GraceMs+100;
        for(int i=0;i<10;++i,now+=11)t.WorldDrawn(now);
        now+=T::GapMs+50;
        for(int i=0;i<40;++i,now+=11)Check(!t.WorldDrawn(now).changed,"a run of draws begun again after a gap");
        Check(t.Playing(),"440 ms of world is not yet the game back");
        cinema::Change last;
        for(int i=0;i<40&&t.Playing();++i,now+=11)last=t.WorldDrawn(now);
        Check(!t.Playing()&&last.changed&&last.why==cinema::Why::WorldDrawn,"750 ms of world drawn ends a cinematic whose end was missed");
        Check(!t.WorldDrawn(now+11).changed,"drawn on, nothing more");
        // Not playing: draws do nothing.
        cinema::Tracker idle;Check(!idle.WorldDrawn(5000).changed&&!idle.Playing(),"no cinematic, no change");
    }
    {   // The cap.
        using T=cinema::Tracker;
        cinema::Tracker t;t.Message(Signal::Start,1000);
        Check(!t.Tick(1000+T::MaxMs-1).changed&&t.Playing(),"under the cap: playing");
        const auto c=t.Tick(1000+T::MaxMs);
        Check(c.changed&&!c.playing&&c.why==cinema::Why::TooLong,"past the cap: over");
        Check(T::MaxMs>=6*60*1000,"the cap is longer than any of D2R's cinematics");
        Check(*cinema::Reason(cinema::Why::WorldDrawn)&&*cinema::Reason(cinema::Why::TooLong)&&*cinema::Reason(cinema::Why::Message),"every end has a reason");
    }
    {   // The movie's band: the interface's full-screen box, at the top.
        auto band=cinema::MovieBand(3072,3072);
        Check(band.x==0&&band.y==0&&band.w==3072&&band.h==1728,"a square 3072 source: the top 3072x1728");
        band=cinema::MovieBand(2560,1440);Check(band.w==2560&&band.h==1440,"a 16:9 source: all of it");
        band=cinema::MovieBand(2160,2400);Check(band.w==2160&&band.h==1215,"a portrait source: its top 16:9");
        band=cinema::MovieBand(3840,1600);Check(band.w==3840&&band.h==1600,"wider than 16:9: all of it (the movie keeps its shape in it)");
        const auto canvas=resolution::UiCanvas({4096,4096});
        band=cinema::MovieBand(4096,4096);Check(band.w==canvas.w&&band.h==canvas.h,"the same box the game's interface is given");
        // The movie at the window's own size, top left (owner 2026-11-12: it played in the upper left of the screen):
        // a 1920x1080 window under a 3072x3072 frame - the session's log - is the top left 1920x1080.
        band=cinema::MovieBand(3072,3072,1920,1080);
        Check(band.x==0&&band.y==0&&band.w==1920&&band.h==1080,"the window's size, top left: the whole film on the screen");
        band=cinema::MovieBand(3072,3072,2560,1600);Check(band.w==2560&&band.h==1600,"a 16:10 window: its own shape (the film letterboxed in it)");
        band=cinema::MovieBand(2560,1440,3840,2160);Check(band.w==2560&&band.h==1440,"a window larger than the frame: the frame");
        band=cinema::MovieBand(3072,3072,0,1080);Check(band.w==3072&&band.h==1728,"no window size: the interface's box, as before");
        band=cinema::MovieBand(1920,1080,1920,1080);Check(band.w==1920&&band.h==1080,"VR resolution off (frame = window): all of it");
        const auto screen=cinema::ScreenSize(cinema::MovieBand(3072,3072,1920,1080),3);
        Check(Near(screen.width,3)&&Near(screen.height,1.6875),"a 16:9 window: a 16:9 screen");
        float fx=0,fy=0;cinema::ToFrame(1,1,cinema::MovieBand(3072,3072,1920,1080),3072,3072,fx,fy);
        Check(Near(fx,1920/3072.)&&Near(fy,1080/3072.),"the laser's far corner is the film's, in the frame");
    }
    {   // The screen: the band's shape at the width asked for, comfortably ahead.
        const cinema::Screen d;
        Check(Near(d.distance,5)&&Near(d.width,6),"6 m wide, 5 m ahead by default");
        const double across=2*std::atan(d.width*.5/d.distance)*57.29578;
        Check(across>55&&across<70,"about 62 degrees across: the whole film in view, not close to the face");
        auto s=cinema::ScreenSize(cinema::MovieBand(3072,3072),3);
        Check(Near(s.width,3)&&Near(s.height,1.6875),"a 16:9 film, 3 x 1.69 m - not stretched to the square frame");
        s=cinema::ScreenSize(cinema::MovieBand(3840,1600),3.2f);Check(Near(s.height,3.2*1600/3840.),"other shapes kept");
        s=cinema::ScreenSize({},2);Check(Near(s.width,2)&&Near(s.height,1.125),"no frame yet: 16:9");
        auto c=cinema::Clamped(0,100);Check(Near(c.distance,1)&&Near(c.width,48),"limits");
        c=cinema::Clamped(NAN,INFINITY);Check(Near(c.distance,5)&&Near(c.width,6),"a broken value: the default");
        c=cinema::Clamped(100,6);Check(Near(c.distance,20),"20 m at most");
        c=cinema::Clamped(4,3.5f);Check(Near(c.distance,4)&&Near(c.width,3.5),"in range: as written");
        // World-locked: placed once from the head; the head moving later moves nothing.
        const auto placed=panels::AtEyeLevel({.2f,1.65f,.1f},math::Yaw(.6f),d.distance);
        Check(Near(placed.position.y,1.65f),"at eye height");
        const auto ahead=placed.position-math::Vec{.2f,1.65f,.1f};
        Check(Near(std::sqrt(math::Dot(ahead,ahead)),d.distance),"the distance asked for");
        Check(Near(placed.orientation.x,0)&&Near(placed.orientation.z,0),"upright: turned only about the vertical");
        // Why the transport poses it directly: the menus' grab limits would pull it in to 1.5 m.
        const math::Vec head{.2f,1.65f,.1f};
        const auto limited=panels::Apply(placed,panels::Limit(placed,{},head)).position-head;
        Check(Near(std::sqrt(math::Dot(limited,limited)),panels::FarDistance),"Limit keeps a menu within 1.5 m - not for the cinema screen");
    }
    {   // The laser on the screen points at the band's own pixels of the frame.
        float x=0,y=0;const auto band=cinema::MovieBand(3072,3072);
        cinema::ToFrame(0,0,band,3072,3072,x,y);Check(Near(x,0)&&Near(y,0),"top left");
        cinema::ToFrame(1,1,band,3072,3072,x,y);Check(Near(x,1)&&Near(y,1728/3072.),"bottom right: the band's bottom, not the frame's");
        cinema::ToFrame(.5f,.5f,band,3072,3072,x,y);Check(Near(x,.5)&&Near(y,864/3072.),"the middle");
        cinema::ToFrame(.25f,2,cinema::MovieBand(2560,1440),2560,1440,x,y);Check(Near(x,.25)&&Near(y,1),"a 16:9 frame maps one to one, clamped");
        cinema::ToFrame(.3f,.7f,{},0,0,x,y);Check(Near(x,.3f)&&Near(y,.7f),"no frame: as hit");
    }
    {   // Wiring: the cinematic reaches every place that decides what the headset shows.
        const std::string transport=Slurp(argv[1]),vrcam=Slurp(argv[2]),gamestate=Slurp(argv[3]);
        Check(!transport.empty()&&!vrcam.empty()&&!gamestate.empty(),"sources read");
        Has(gamestate,"gamestate.cpp","CinemaMessage(target, command, text);","every UI message is classified");
        Has(gamestate,"gamestate.cpp","bool CinemaPlaying() { return g_cinemaOn.load() && g_cinema.load(); }","[cinema] enabled=0 leaves things as they were");
        Has(vrcam,"vrcam.cpp","||gamestate::CinemaPlaying();}, +[](){return g_inWorld.load();}","the flat panel wanted while one plays");
        Has(vrcam,"vrcam.cpp","&& !gamestate::CinemaPlaying()\n#endif","no stereo eyes or pairs while one plays");
        Has(vrcam,"vrcam.cpp","if(gamestate::CinemaPlaying())return true;","no sky or fog over the movie");
        Has(vrcam,"vrcam.cpp","hud::SetMenuOpen(gamestate::MenuOpen() || !g_inWorld.load() || gamestate::CinemaPlaying());","no toolbar strip cleared out of the movie");
        Has(vrcam,"vrcam.cpp","if (!depth) gamestate::CinemaWorldDrawn(GetTickCount64());","the world drawn again is seen");
        Has(vrcam,"vrcam.cpp","gamestate::CinemaTick(nowMs);","the cap is ticked");
        Has(vrcam,"vrcam.cpp","framevr::SetCinemaScreen(screen.distance, screen.width);","[cinema] distance and width reach the transport");
        Has(transport,"transport.cpp","const bool spatial=!cinema&&SpatialMenu();","never the spatial menus over a world not drawn");
        Has(transport,"transport.cpp","const bool panel=cinema||(","in a game too, the flat panel");
        Has(transport,"transport.cpp","bool SpatialMenu(){return !gamestate::CinemaPlaying()&&","vrcam's stereo checks agree");
        Has(transport,"transport.cpp","quad.subImage.imageRect=flat.rect;","the quad shows the movie band only");
        Has(transport,"transport.cpp","cinema::MovieBand(width,height,window.w,window.h)","the movie at the window's own size, top left");
        Has(transport,"transport.cpp","menuPose=PanelPose(menuBase);menuPlaced=true;","placed once, world-locked, at its own distance (not the menus' grab limits)");
    }
    std::puts("cinema-screen-test: ok");
    return 0;
}
