#include "map_zoom.h"
#include "wrist_full_map.h"
#include <algorithm>
#include <cmath>
#include "stick_gestures.h"
#include <cstdio>
#include <cstdlib>
using namespace framevr;
static void Check(bool b,const char* label){if(!b){std::fprintf(stderr,"FAIL: %s\n",label);std::exit(1);}}
struct Sim {
    mapzoom::Gesture g;mapzoom::Input in;mapzoom::Output out;int ticks=0,stops=0,starts=0,ends=0;
    Sim(){in.time=1;Step();}
    mapzoom::Output Step(int n=1){while(n--){in.time+=.02;out=g.Step(in);ticks+=out.tick==1;stops+=out.tick==2;starts+=out.started;ends+=out.stopped;}return out;}
};
// The map's center `degrees` off the head's forward, half a meter away.
static math::Vec At(float degrees){const float r=degrees*.0174532925f;return {.5f*std::sin(r),1.6f,-.5f*std::cos(r)};}
int main(){
    {
        const math::Vec head{0,1.6f,0};const math::Quat q{0,0,0,1};
        Check(mapzoom::Looking(head,q,At(0),false)&&mapzoom::Looking(head,q,At(20),false),"a map ahead is looked at");
        Check(!mapzoom::Looking(head,q,At(26),false),"past 22 degrees a glance does not start it");
        Check(mapzoom::Looking(head,q,At(26),true)&&mapzoom::Looking(head,q,At(29),true),"once looking, it holds to 30 degrees");
        Check(!mapzoom::Looking(head,q,At(32),true),"looking further away ends it");
        Check(!mapzoom::Looking(head,q,At(180),true),"a map behind is never looked at");
        Check(!mapzoom::Looking(head,q,head,false)&&!mapzoom::Looking(head,q,{NAN,0,0},true)&&!mapzoom::Looking(head,{NAN,0,0,1},At(0),true),
              "a missing pose is not looking");
        const math::Quat down{-.38268343f,0,0,.92387953f};   // head pitched 45 degrees down
        Check(mapzoom::Looking(head,down,{0,1.1f,-.5f},false)&&!mapzoom::Looking(head,q,{0,1.1f,-.5f},false),"the head's pitch counts, not just its heading");
    }
    {
        Sim s;Check(!s.out.owned&&s.out.zoom==1,"not looking: the stick is the game's and the map keeps its size");
        s.in.stick=1;Check(!s.Step(10).owned&&s.out.zoom==1&&!s.starts,"the stick alone never zooms");
        s.in.stick=0;s.in.looking=true;Check(s.Step().owned&&!s.out.zooming,"looking takes the stick's up / down, even at rest");
        s.in.stick=1;s.Step(50);
        Check(s.out.zooming&&s.out.zoom>2.75f&&s.out.zoom<3,"up zooms in smoothly, 1.5 doublings a second");
        Check(s.starts==1&&s.ticks==7&&s.stops==0,"one start, one light tick per quarter crossed");
        s.Step(50);Check(s.out.zoom==mapzoom::Max&&s.stops==1,"zoom stops at the top with one firm tick");
        s.in.stick=-1;s.Step(200);Check(s.out.zoom==mapzoom::Min&&s.stops==2&&s.starts==1,"down returns exactly to the whole map");
        s.in.stick=.6f;s.Step(10);const float z=s.out.zoom;Check(z>1,"part-way stick zooms in");
        s.in.stick=.2f;Check(s.Step(10).zoom==z&&s.ends==1,"inside the dead zone the zoom holds and the gesture ends");
        Check(s.out.owned,"the stick still belongs to the map while it is looked at");
        s.in.looking=false;s.in.stick=1;Check(!s.Step().owned&&!s.out.turnOwned&&s.out.zoom==z,"looking away gives the stick back");
        Check(s.Step(50).zoom==z&&s.g.Zoom()==z,"the zoom stays for this game");
    }
    {
        // Only a mostly vertical push zooms, and it holds the turn until the center.
        Sim s;s.in.looking=true;
        s.in.x=.8f;s.in.stick=.35f;s.Step(10);
        Check(!s.out.zooming&&!s.out.turnOwned&&s.out.zoom==1,"a sideways turn flick with some up does not zoom");
        s.in.stick=.6f;s.Step(10);
        Check(!s.out.zooming&&!s.out.turnOwned&&s.out.zoom==1,"a turn that drifts up stays a turn");
        s.in.x=0;s.in.stick=0;s.Step();
        s.in.x=.4f;s.in.stick=1;s.Step(10);
        Check(s.out.zooming&&s.out.turnOwned&&s.out.zoom>1,"a slightly diagonal push zooms and does not turn");
        s.in.x=.9f;s.in.stick=.1f;s.Step();
        Check(!s.out.zooming&&s.out.turnOwned,"sliding sideways mid-push still turns nothing");
        s.in.x=.1f;s.in.stick=.1f;s.Step();
        Check(!s.out.turnOwned,"back near the center the stick turns again");
        s.in.x=.9f;s.in.stick=0;Check(!s.Step().turnOwned,"then a sideways push turns");
    }
    {
        // A new game (a new local player) goes back to the whole map.
        uint64_t seen=0;
        Check(mapzoom::NewGame(seen,7)&&seen==7,"the first game counts as new");
        Check(!mapzoom::NewGame(seen,7),"the same game is not new");
        Check(!mapzoom::NewGame(seen,0)&&seen==7,"between games nothing changes yet");
        Check(mapzoom::NewGame(seen,9)&&!mapzoom::NewGame(seen,9),"another player is a new game");
        Sim s;s.in.looking=true;s.in.stick=1;s.Step(30);
        Check(s.g.Zoom()>1,"zoomed in");
        s.g.NewGame();Check(s.g.Zoom()==1,"a new game starts at 1x");
    }
    {
        Sim s;s.in.looking=true;s.in.stick=-1;s.Step(10);
        Check(s.out.zoom==1&&s.out.zooming,"down at the whole map stays there");
        s.in.stick=1;s.Step(10);s.in.looking=false;
        Check(s.Step().stopped&&!s.out.zooming,"looking away mid-zoom ends the gesture");
        s.in.looking=true;const float z=s.Step().zoom;s.g.Reset();
        Check(s.g.Zoom()==z,"a reset (tracking or focus lost) keeps the zoom");
        Check(s.Step().zooming&&s.out.started,"after a reset a held stick starts a new gesture");
    }
    {
        // The map and the world gestures share the stick's up / down, as transport.cpp
        // runs them: the world gestures first (allowed unless the map was looked at),
        // then the zoom with whatever the gestures left.
        stickgestures::Gestures world;mapzoom::Gesture zoom;mapzoom::Input in;in.time=1;
        int runs=0;bool loot=false;
        auto Frame=[&](bool looking,float y){
            const auto r=world.Step(mapzoom::WorldGestures(true,looking),0,y);
            runs+=r.toggleRun;loot=r.showLoot;
            in.time+=.02;in.looking=looking;in.stick=r.owned?0:y;return zoom.Step(in);
        };
        for(int i=0;i<5;++i)Frame(false,0);
        for(int i=0;i<50;++i)Frame(true,1);
        Check(runs==0&&zoom.Zoom()>2,"pushing up at the looked-at map zooms and never toggles running");
        for(int i=0;i<30;++i)Frame(true,-1);
        Check(!loot&&zoom.Zoom()<2,"pulling down at the map zooms out and shows no loot labels");
        const float z=zoom.Zoom();
        for(int i=0;i<10;++i)Frame(false,1);
        Check(runs==0&&zoom.Zoom()==z,"a push still held after looking away does not run");
        Frame(false,0);Frame(false,1);
        Check(runs==1&&zoom.Zoom()==z,"after the stick returns to the center, up runs / walks as before");
        Frame(false,0);
        for(int i=0;i<5;++i)Frame(false,-1);
        Check(loot,"held down away from the map shows the loot labels");
        Frame(true,-1);Check(!loot,"looking at the map drops the loot labels");
    }
    {
        mapzoom::Gesture g;Check(g.Zoom()==1,"the first game starts at the whole map");
        mapzoom::Input in;in.time=1;in.looking=true;g.Step(in);in.stick=NAN;in.time+=.02;
        Check(!g.Step(in).zooming&&g.Zoom()==1,"a lost stick reading does nothing");
        Check(mapzoom::Clamp(.5f)==1&&mapzoom::Clamp(5)==4&&mapzoom::Clamp(2.5f)==2.5f&&mapzoom::Clamp(INFINITY)==1,"zoom clamps to 1x-4x");
        Check(mapzoom::NotchOf(1)==0&&mapzoom::NotchOf(1.26f)==1&&mapzoom::NotchOf(4)==12,"quarter notches");
        Check(mapzoom::NotchOf(4.9f)==12&&mapzoom::NotchOf(5)==13&&mapzoom::NotchOf(16)==24&&mapzoom::NotchOf(99)==24,"past 4x a notch per whole step");
    }
    {
        // Map view = Full map: the same gesture zooms to 16x; turned off, back under 4x at once.
        Sim s;s.in.looking=true;s.in.max=mapzoom::FullMax;s.in.stick=1;s.Step(200);
        Check(s.out.zoom==mapzoom::FullMax&&s.stops==1,"the full map zooms in to 16x and stops there with one firm tick");
        Check(s.ticks<=24,"the ticks stay a count, not a buzz");
        s.in.max=mapzoom::Max;s.in.stick=0;s.Step();
        Check(s.out.zoom==mapzoom::Max&&s.g.Zoom()==mapzoom::Max,"the corner map's ceiling takes the zoom back to 4x");
        s.in.max=NAN;s.Step();Check(s.out.zoom==mapzoom::Max,"an unreadable ceiling is the corner map's");
    }
    {
        namespace fm=framevr::fullmap;
        // the box: the layer's width, its 16:9 band from the top
        const auto b=fm::FullBox(2112,2304);
        Check(b.left==0&&b.top==0&&b.right==2112&&b.bottom==1188,"the full map's box on the 2112x2304 layer");
        Check(fm::IsFullBox(0,0,2112,1188,2112,2304)&&fm::IsFullBox(0,0,2112,1187,2112,2304)&&fm::IsFullBox(0,0,2112,1189,2112,2304),"a pixel either way");
        Check(!fm::IsFullBox(0,53,639,412,2112,2304)&&!fm::IsFullBox(0,0,2112,2304,2112,2304)&&!fm::IsFullBox(22,501,682,924,2112,2304),"the corner map, the whole layer and a panel are not it");
        Check(!fm::IsFullBox(0,0,1920,1080,1920,1080)&&!fm::IsFullBox(0,0,2112,1188,0,0),"never on a 16:9 layer (that is the whole screen)");
        Check(fm::Active(true,true,0,true)&&!fm::Active(false,true,0,true)&&!fm::Active(true,false,0,true)&&!fm::Active(true,true,1,true)&&!fm::Active(true,true,0,false),
              "only with the setting on, the wrist capturing, the full layout and the map open");
        // the game's zoom: its own rounding, the step at or below, never under its floor
        Check(fm::GameRounded(.5f)==.5f&&fm::GameRounded(.3f)==.25f&&fm::GameRounded(.01f)==.125f&&fm::GameRounded(0)==0&&fm::GameRounded(NAN)==0,"the game's rounding");
        Check(fm::GameStep(.125f)==.125f&&fm::GameStep(.2f)==.125f&&fm::GameStep(.25f)==.25f&&fm::GameStep(.3f)==.25f&&fm::GameStep(.05f)==.125f&&fm::GameStep(2.f)==2.f&&fm::GameStep(NAN)==.125f,"the step at or below");
        Check(fm::ZoomArg(.5f,.125f,true)==.125f&&fm::ZoomArg(.5f,1.f,true)==1.f&&fm::ZoomArg(.5f,.3f,true)==.25f,"the gates hold: the step for the wanted zoom");
        Check(fm::ZoomArg(.5f,.125f,false)==.5f&&fm::ZoomArg(.5f,0.f,true)==.5f&&fm::ZoomArg(0.f,.125f,true)==0.f&&fm::ZoomArg(100.f,.125f,true)==100.f,"no gates, nothing wanted or an insane game value: the game's own");
        // the span: at 1x the fill, at the widest game zoom
        for(bool turning:{false,true})for(bool topDown:{false,true}) {
            const float fill=fm::WristFill(2112,1188,turning,topDown);
            Check(std::fabs(fm::Span(2112,1188,turning,topDown,1,fm::Base)-fill)<1e-3f,"1x: the fill at game zoom 0.125");
            // smooth: what one wrist pixel covers of the level (span / side / drawn zoom) falls as 1/magnify,
            // whatever step the game drew at, so a step never jumps, and the step keeps the
            // magnification on the wrist under 2 (sharp)
            for(float m=1;m<=mapzoom::FullMax;m*=1.07f) {
                const float drawn=fm::GameStep(fm::Base*m),span=fm::Span(2112,1188,turning,topDown,m,drawn);
                const float world=span/drawn,expected=fill/fm::Base/m;
                Check(std::fabs(world-expected)<expected*1e-4f,"the level shown follows the zoom exactly");
                Check(span<=fill*1.0001f&&span>fill/2-1e-3f,"the share is never magnified 2x or more");
                // a share still drawn at the step below (the hook a frame behind) shows the same level
                const float before=std::max(drawn-.125f,fm::Base),late=fm::Span(2112,1188,turning,topDown,m,before);
                Check(std::fabs(late/before-expected)<expected*1e-4f,"a share a step behind shows the same level");
            }
            Check(fm::Span(2112,1188,turning,topDown,1,0)>fill&&fm::Span(2112,1188,turning,topDown,0.f,fm::Base)==fill&&fm::Span(0,0,turning,topDown,1,fm::Base)==0,
                  "no hook: the game's own 0.5 shown wider (never past the box); nonsense: 1x");
        }
        // one full map a frame: the first pass draws it, a later one reuses it unless the frame start is long gone
        Check(fm::DrawForWrist(false,0)&&!fm::DrawForWrist(true,0)&&!fm::DrawForWrist(true,fm::ReuseMs)&&fm::DrawForWrist(true,fm::ReuseMs+1)&&fm::DrawForWrist(false,5000),
              "the frame's first pass draws; a later pass within the frame reuses; no frame start for long: draws");
        // the arrow from the X, as the shader sizes it
        Check(fm::ArrowRadius(512,1188,0,false)==512*fm::ArrowSize,"no X: the corner map's size");
        Check(std::fabs(fm::ArrowRadius(512,1188,30,false)-30*512/1188.f*1.2f)<1e-4f,"the X's reach on the wrist, 1.2 times");
        Check(fm::ArrowRadius(512,594,30,false)>fm::ArrowRadius(512,1188,30,false),"it grows as the wrist zooms in");
        Check(fm::ArrowRadius(512,100,40,true)==512*fm::ArrowMax&&fm::ArrowRadius(512,4000,2,false)==512*fm::ArrowMin,"within its floor and cap");
        // the X's search: small at the widest zoom (water 40 px off stays out), wide enough for the X at the closest
        {   // the X's search: where the game draws it, sized to it (headset trace: half 4.5 px at 0.125 to 67 at 2.0, up to 31 px up)
            const auto none=fm::XSearchAt(0,1188),noShare=fm::XSearchAt(.5f,0),inf=fm::XSearchAt(INFINITY,1188);
            Check(none.reach==0&&noShare.reach==0&&inf.reach==0,"no drawn zoom: the wide search");
            const auto wide=fm::XSearchAt(.125f,1188),close=fm::XSearchAt(2.f,1188);
            Check(wide.reach>4.5f+3&&wide.reach<16,"0.125: the 4.5 px X in, water 16 px off out");
            Check(std::fabs(close.down+34)<1e-3f&&close.right>0&&close.reach>67+5&&close.reach<100,"2.0: centered up on the 67 px X, not 150 px below it");
            const auto big=fm::XSearchAt(1.75f,1188);
            Check(-big.down+big.reach>=31+58.5f&&big.reach+big.down<58.5f-31+30,"1.75: the X's top in, under 30 px below its bottom");
            const auto twice=fm::XSearchAt(.5f,2376),once=fm::XSearchAt(.5f,1188);
            Check(std::fabs(twice.reach-2*once.reach)<1e-3f&&std::fabs(twice.down-2*once.down)<1e-3f,"in the share's own pixels");
        }
        Check(fm::WristTaps(512,512)==1&&fm::WristTaps(1188,512)==3&&fm::WristTaps(4000,512)==4,"taps a pixel");
    }
    return 0;
}
