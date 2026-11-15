#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>
#include "frame_math.h"

// Floating map grab hint: a sign at the raised wrist map that it can be pulled off (floating_map.h has the pull).
//  - When: only while a Right Grip press would pull the wrist map off: the pull's own gates (in the world, the head
//    tracked, the pulling hand free, its laser tracked, the wrist map shown, Right Grip up), no copy out, and the laser
//    within the pull band (floatmap::AimAt's miss against PointDeg; 0 at a belt bottle or holster, as the pull's). It
//    hides only OutExtraDeg past the band, so it never flickers at the band's edge.
//  - What: beside the map on the pulling hand's side, a small controller sign in a 1.25 s loop (a pause, idle, Right Grip
//    squeezed, the controller sliding toward the pulling hand, back). The map's border glows, brighter while the grip is
//    squeezed, and while the controller slides a glow reaches out from the border toward the pulling hand. It fades in
//    over FadeInS and out over FadeOutS.
//  - A light tick in the pulling hand when it appears, after it was gone at least TickRearmS (no tick on jitter).
//  - Left-handed (the map on the right wrist, pulled with the left hand) everything is on the map's left and the sign's
//    pictures are mirrored: a left hand on a left controller, sliding left.
//  - After PullsLimit pulls in a game the sign stops; the glow stays. A new game starts over; nothing is saved.
// Nothing in the pull changes: the hint only reads. VR Settings > Interface > Wrist > Map grab hint turns it off.
// Pure: no game, no OpenXR (floating_map_frame.cpp steps it and draws it; its pictures are painted into the static
// image's region panelatlas::GrabHint once a session).
namespace framevr::grabhint {
namespace math=framevr::math;

// ---- The shipped values, tuned in the headset 2026-11-15. One line each.
constexpr float SignM=.03f;           // the sign's side, m (about 3 degrees at the wrist's distance)
constexpr float SignGapM=.015f;       // from the map's edge to the sign, m
constexpr float SlideSigns=1;         // how far the sign slides toward the pulling hand, in its own sizes
constexpr int PullsLimit=3;           // the sign stops after this many pulls in a game (0: never)
constexpr float Buzz=.15f;            // the tick's strength (0: none)
constexpr float Glow=.70f;            // the glow's strength, 0..1
constexpr float EdgeM=.0025f;         // the border glow's width past the map's edge, m
constexpr float ReachM=.01f;          // how far the glow reaches out past the edge toward the pulling hand, m (0: border only)

constexpr float OutExtraDeg=5;        // it hides only this far past the band, degrees
constexpr double FadeInS=.15,FadeOutS=.20;
constexpr double GapS=.5;             // no step this long (a load screen, the setting Off): it starts hidden again
constexpr double TickRearmS=.3;       // the tick again only after the hint was gone this long (the floating map's AimRearmS)
constexpr float EdgeInner=.5f;        // the border glow reaches this share of EdgeM inside the map's edge
constexpr float InnerM=EdgeM*EdgeInner;
constexpr float LobeOver=.08f;        // the glow reaching out runs this share of the map's side past its top and bottom
constexpr float Lift=.002f;           // the hint's quads sit this far in front of the map, m (as the menu map's)
constexpr float MapSideM=.24f;        // the wrist map's side the border's corners are painted for (interaction::WristMapSize)
// The sign's loop (the art: 8 frames, 0-1 idle, 2-3 Right Grip squeezed, 4-6 the slide, 7 back): a pause on frame 0,
// then the frames at Fps. 1.25 s a loop: under one pulse a second.
constexpr int Frames=8,PressFirst=2,SlideFirst=4,SlideLast=6;
constexpr double Fps=8,PauseS=.25,CycleS=PauseS+Frames/Fps;

struct Tuning {   // the values above, so the tests can try the others
    int pullsLimit=PullsLimit;
    float glow=Glow,reachM=ReachM,buzz=Buzz;
};
struct Input {
    double time=0;            // s
    uint64_t session=0;       // the game's local player: a new one starts the pull count over
    bool ready=false;         // the pull's own gates pass and Right Grip is up
    bool copyOut=false;       // a copy is out (pulled and not put away)
    float miss=180;           // degrees from the laser to the wrist map's nearest point (0: on it)
    float band=15;            // the pull band, degrees (0: exactly on it)
    bool pulled=false;        // a copy was pulled off the wrist this frame
};
enum class Event { None, Shown, Hidden };
struct Output {
    bool in=false;            // inside the band (with the exit gap)
    float level=0;            // the fade, eased, 0..1
    float sign=0;             // the sign's strength
    float ring=0;             // the border glow's
    float lobe=0;             // the glow reaching out: its strength
    float lobeM=0;            // and how far it reaches now, m
    float slide=0;            // the sign's slide toward the pulling hand, 0..1 of SlideSigns (0 on the last frame: back)
    int frame=0;              // the loop's frame, 0..Frames-1
    float buzz=0;             // > 0: tick the pulling hand this strong
    Event event=Event::None;
    bool pulledNow=false;     // a pull counted this frame
    bool signStops=false;     // that pull reached pullsLimit: the sign stops from now
    unsigned pulls=0;         // pulls this game
    bool Any()const{return sign>0||ring>0||lobe>0;}
};

class Engine {
    bool in=false;
    float fade=0;
    double last=-1,shownAt=0,hiddenAt=-1e9;
    uint64_t session=0;
    unsigned pulls=0;
public:
    Output Step(const Input& x,const Tuning& t={}) {
        Output o;
        const bool gap=last<0||x.time<last||x.time-last>GapS;
        const double dt=gap?0:x.time-last;
        last=x.time;
        if(gap){in=false;fade=0;}
        if(x.session!=session){session=x.session;pulls=0;}
        if(x.pulled){++pulls;o.pulledNow=true;o.signStops=t.pullsLimit>0&&pulls==unsigned(t.pullsLimit);}
        const bool could=x.ready&&!x.copyOut&&!x.pulled;
        const bool now=could&&x.miss<=(in?x.band+OutExtraDeg:x.band);
        if(now&&!in){o.event=Event::Shown;o.buzz=x.time-hiddenAt>=TickRearmS?std::max(0.f,t.buzz):0;shownAt=x.time;}
        else if(!now&&in){o.event=Event::Hidden;hiddenAt=x.time;}
        in=now;
        fade=in?std::min(1.f,fade+float(dt/FadeInS)):std::max(0.f,fade-float(dt/FadeOutS));
        const float a=fade*fade*(3-2*fade);
        o.in=in;o.level=a;o.pulls=pulls;
        if(a<=0)return o;
        o.sign=t.pullsLimit<=0||pulls<unsigned(t.pullsLimit)?a:0;
        // The loop: the pause on frame 0, the squeeze, then the slide; the glow in step with it.
        const float glow=std::clamp(t.glow,0.f,1.f);
        const double play=std::fmod(x.time-shownAt,CycleS)-PauseS;
        o.frame=play<0?0:std::min(Frames-1,int(play*Fps));
        const bool press=play>=0&&o.frame>=PressFirst&&o.frame<SlideFirst;
        const bool sliding=play>=0&&o.frame>=SlideFirst&&o.frame<=SlideLast;
        const float s=sliding?float(std::clamp((play-SlideFirst/Fps)/((SlideLast+1-SlideFirst)/Fps),0.0,1.0)):0.f;
        const float ease=1-(1-s)*(1-s);
        o.slide=sliding?ease:0;
        o.ring=glow*a*(press?1.f:.7f);
        if(t.reachM>0&&sliding){o.lobe=glow*a*(1-.7f*s);o.lobeM=t.reachM*ease;}
        if(o.lobeM<=.001f){o.lobe=0;o.lobeM=0;}
        return o;
    }
};

// Which strength draws a value: -1 none, else 0 .. levels-1 (level i holds (i+1)/levels).
constexpr int Levels=9;               // each glow picture at this many strengths
constexpr int SignLevels=2;           // the sign at two: half (the fades) and full
inline int LevelOf(float strength,int levels=Levels){
    if(!(strength>0))return -1;
    return std::clamp(int(strength*levels+.5f)-1,-1,levels-1);
}

// Where the quads go, from the wrist map's pose (center p, turn q, side `size` m). side: +1 the pulling hand is on the
// map's right as you see it (right-handed), -1 its left. lobeM: the glow's reach now; slideM: the sign's slide, m.
// The border is four thin strips, one along each edge (a single picture over the whole map could not draw a glow
// thinner than one of its texels, stretched over 24 cm). Each strip is the same kind of picture turned a quarter turn
// more about the map's normal, so its x runs outward: InnerM inside the edge to EdgeM past it. The top and bottom
// strips run the map's full width and round its corners; the left and right ones stop where those begin, so no two
// strips overlap (an overlap would glow twice as bright). Every quad sits Lift in front of the map; quads have no depth
// test, so what is drawn over what is the layer list's order (transport keeps the hint's just above the wrist panels).
// Place's side from the physical hand that pulls (transport: UiHand(0, WristHud)): 0, the right one: +1. Never the hand's
// role: the roles turn with the wrist HUD, so the pulling hand's role is 0 for right- and left-handed play alike.
inline int SideOf(int physicalHand){return physicalHand==0?1:-1;}
struct Quad {math::Vec p{};math::Quat q{0,0,0,1};float w=0,h=0;};
constexpr int Edges=4;   // the border's strips: 0 right, 1 top, 2 left, 3 bottom (as seen on the map's face)
constexpr int MaxQuads=Edges+2;       // the strips, the glow reaching out, the sign
struct Placement {Quad edge[Edges],lobe,sign;};
inline Placement Place(math::Vec p,math::Quat q,float size,int side,float lobeM,float slideM=0){
    Placement out;
    const float s=side<0?-1.f:1.f;
    const auto right=math::Rotate(q,{1,0,0}),up=math::Rotate(q,{0,1,0}),lift=math::Rotate(q,{0,0,1})*Lift;
    const float across=InnerM+EdgeM,mid=size*.5f+(EdgeM-InnerM)*.5f;
    constexpr float h=.70710678f;
    const math::Quat turn[Edges]={{0,0,0,1},{0,0,h,h},{0,0,1,0},{0,0,-h,h}};
    for(int i=0;i<Edges;++i){
        const math::Quat qi=math::Multiply(q,turn[i]);
        out.edge[i]={p+math::Rotate(qi,{mid,0,0})+lift,qi,across,i%2?size+2*EdgeM:size-2*InnerM};
    }
    // The glow reaching out: the same picture turned half a turn about the map's normal for the left (it is symmetric
    // top to bottom).
    const math::Quat half=s<0?math::Multiply(q,math::Quat{0,0,1,0}):q;
    out.lobe={p+right*(s*(size*.5f+lobeM*.5f))+lift,half,lobeM,size*(1+2*LobeOver)};
    out.sign={p+right*(s*(size*.5f+SignGapM+SignM*.5f+slideM))+up*((SignM-size)*.5f)+lift,q,SignM,SignM};
    return out;
}

// The pictures, in the static image's region panelatlas::GrabHint (AtlasWidth x AtlasHeight; cells in its own texels),
// premultiplied. The sign: the 8 frames at full strength, then frame 0 at half for the fades (a fade shows the half
// picture; a fade out from a later frame shows frame 0's); then the same nine mirrored left to right, for left-handed
// play (a left hand on a left controller, sliding left). The glow reaching out at Levels strengths. The border strips
// at Levels strengths: the left and right strips' picture (its profile across, alike along; in the last sign row's free
// end), and the top and bottom's (the same profile, its ends rounding the map's corners, so StripLong texels along: a
// corner about 7 texels). Each strip picture has one texel of gutter round it that copies its edge texels, so filtering
// at the quad's edge reads the strip itself.
constexpr int SignTile=96,Pad=8,SignsPerRow=4,SignSet=Frames+1,SignSlots=2*SignSet;
constexpr int LobeTile=40;
constexpr int StripAcross=16,StripShort=4,StripLong=454;
constexpr float GoldR=1.f,GoldG=.78f,GoldB=.38f;
struct Cell {int x=0,y=0,w=0,h=0;};
inline Cell SlotCell(int i){return {(i%SignsPerRow)*(SignTile+Pad),(i/SignsPerRow)*(SignTile+Pad),SignTile,SignTile};}
// A sign picture: the loop's frame at level 1 (full), or frame 0 at half (level 0); mirrored: left-handed.
inline Cell SignCell(int frame,int level,bool mirrored=false){
    return SlotCell((mirrored?SignSet:0)+(level>=SignLevels-1?std::clamp(frame,0,Frames-1):Frames));
}
constexpr int SignRows=(SignSlots+SignsPerRow-1)/SignsPerRow;
constexpr int LobeTop=SignRows*(SignTile+Pad),LongTop=LobeTop+LobeTile+Pad;
inline Cell LobeCell(int level){return {level*(LobeTile+Pad),LobeTop,LobeTile,LobeTile};}
// The side strips: five a row, right of the last sign row's last slot.
constexpr int SideLeft=(SignSlots-(SignRows-1)*SignsPerRow)*(SignTile+Pad),SideTop=(SignRows-1)*(SignTile+Pad);
inline Cell SideCell(int level){return {SideLeft+1+level%5*(StripAcross+2+Pad),SideTop+1+level/5*(StripShort+2+Pad),StripAcross,StripShort};}
inline Cell AlongCell(int level){return {1+level*(StripAcross+2+Pad),LongTop+1,StripAcross,StripLong};}
constexpr int AtlasWidth=std::max({SignsPerRow*(SignTile+Pad)-Pad,Levels*(LobeTile+Pad)-Pad,Levels*(StripAcross+2+Pad)-Pad});
constexpr int AtlasHeight=LongTop+StripLong+2;
static_assert(SideLeft+5*(StripAcross+2+Pad)-Pad<=AtlasWidth&&SideTop+((Levels+4)/5)*(StripShort+2+Pad)-Pad<=LobeTop-Pad,
              "the side strips fit the last sign row's free end");

// The border glow at s m from the map's outline (+ outside): brightest on it, almost nothing InnerM inside and EdgeM past.
inline float EdgeAlpha(float s){const float w=(s<0?InnerM:EdgeM)*.5f;return std::exp(-(s/w)*(s/w));}
// The signed distance from the outline of a square of side `size` centered at 0 (+ outside), at (x, y).
inline float OutlineDistance(float x,float y,float size){
    const float dx=std::fabs(x)-size*.5f,dy=std::fabs(y)-size*.5f;
    return dx>0&&dy>0?std::sqrt(dx*dx+dy*dy):std::max(dx,dy);
}
// A border strip's texel: i across (0 its inner side, over the map), j along; along: the top or bottom strip's picture
// (StripLong along, the map's full width and both corners), else the left or right's (all alike along).
inline float StripAlpha(int i,int j,bool along){
    const float s=-InnerM+(i+.5f)/StripAcross*(InnerM+EdgeM);
    if(!along)return EdgeAlpha(s);
    const float length=MapSideM+2*EdgeM,x=-length*.5f+(j+.5f)/StripLong*length;
    return EdgeAlpha(OutlineDistance(x,MapSideM*.5f+s,MapSideM));
}
// The glow reaching out: strongest at the map's edge (u = 0), gone at its reach (u = 1); soft past the map's top and bottom.
inline float LobeAlpha(float u,float v){
    const float half=.5f/(1+2*LobeOver);
    const float dy=std::max(0.f,std::fabs(v-.5f)-half*.8f);
    const float side=std::exp(-(dy/.08f)*(dy/.08f));
    const float k=1-std::clamp(u,0.f,1.f);
    return k*k*side;
}

// Premultiplied RGBA. srgb: the image is an sRGB format, so a premultiplied color is linear color times alpha, encoded
// again (premultiplying the stored bytes would darken every soft edge).
inline float ToLinear(float c){return c<=.04045f?c/12.92f:std::pow((c+.055f)/1.055f,2.4f);}
inline float ToSrgb(float c){return c<=.0031308f?c*12.92f:1.055f*std::pow(c,1/2.4f)-.055f;}
struct SrgbTables {   // a few thousand pow calls a paint, not millions
    float linear[256];uint8_t encode[4096];
    SrgbTables(){
        for(int i=0;i<256;++i)linear[i]=ToLinear(i/255.f);
        for(int i=0;i<4096;++i)encode[i]=uint8_t(ToSrgb(i/4095.f)*255+.5f);
    }
};
inline void Put(std::vector<uint8_t>& image,int x,int y,float a,float r,float g,float b,bool srgb,const SrgbTables& t){
    a=std::clamp(a,0.f,1.f);
    const auto pre=[&](float c){
        c=std::clamp(c,0.f,1.f);
        return srgb?t.encode[std::clamp(int(t.linear[int(c*255+.5f)]*a*4095+.5f),0,4095)]/255.f:c*a;
    };
    uint8_t* p=&image[(size_t(y)*AtlasWidth+x)*4];
    p[0]=uint8_t(pre(r)*255+.5f);p[1]=uint8_t(pre(g)*255+.5f);p[2]=uint8_t(pre(b)*255+.5f);p[3]=uint8_t(a*255+.5f);
}
// anim: the loop's Frames frames of SignTile side by side (straight RGBA, SignTile*Frames x SignTile), or null (no sign
// pictures: the hint shows its glow only). AtlasWidth x AtlasHeight RGBA, for the GrabHint region.
inline std::vector<uint8_t> Paint(const uint8_t* anim,bool srgb){
    std::vector<uint8_t> image(size_t(AtlasWidth)*AtlasHeight*4);
    const SrgbTables t;
    if(anim)for(int slot=0;slot<SignSlots;++slot){
        const int i=slot%SignSet;const bool mirrored=slot>=SignSet;
        const int frame=i<Frames?i:0;const float k=i<Frames?1.f:.5f;
        const Cell c=SlotCell(slot);
        for(int y=0;y<SignTile;++y)for(int x=0;x<SignTile;++x){
            const int from=mirrored?SignTile-1-x:x;
            const uint8_t* s=anim+(size_t(y)*SignTile*Frames+size_t(frame)*SignTile+from)*4;
            Put(image,c.x+x,c.y+y,s[3]/255.f*k,s[0]/255.f,s[1]/255.f,s[2]/255.f,srgb,t);
        }
    }
    for(int l=0;l<Levels;++l){
        const float k=float(l+1)/Levels;
        const Cell c=LobeCell(l);
        for(int y=0;y<LobeTile;++y)for(int x=0;x<LobeTile;++x)
            Put(image,c.x+x,c.y+y,LobeAlpha((x+.5f)/LobeTile,(y+.5f)/LobeTile)*k,GoldR,GoldG,GoldB,srgb,t);
        for(const bool along:{false,true}){
            const Cell e=along?AlongCell(l):SideCell(l);
            for(int y=-1;y<=e.h;++y)for(int x=-1;x<=e.w;++x)   // with the gutter: the edge texels copied one texel out
                Put(image,e.x+x,e.y+y,StripAlpha(std::clamp(x,0,e.w-1),std::clamp(y,0,e.h-1),along)*k,GoldR,GoldG,GoldB,srgb,t);
        }
    }
    return image;
}

// What to draw this frame: the border strips (two pictures: left and right, top and bottom), the glow reaching out
// and the sign, each a cell (none: w 0); need: how many quads this frame; most: how many the loop draws at its fullest
// (the strips, the glow out, and the sign while it has not stopped). mirrored: the left-handed sign.
struct Draw {Cell side,along,lobe,sign;uint32_t need=0,most=0;};
inline Draw Plan(const Output& o,bool signs,bool mirrored=false){
    Draw d;
    const int ring=LevelOf(o.ring),lobe=LevelOf(o.lobe),sign=signs?LevelOf(o.sign,SignLevels):-1;
    if(ring>=0){d.side=SideCell(ring);d.along=AlongCell(ring);d.need+=Edges;}
    if(lobe>=0&&o.lobeM>0){d.lobe=LobeCell(lobe);++d.need;}
    if(sign>=0){d.sign=SignCell(o.frame,sign,mirrored);++d.need;}
    d.most=Edges+1+(signs&&o.sign>0?1u:0u);
    return d;
}
// Fitted to `room` free layer slots, judged against the loop's fullest frame (most), so nothing comes and goes with the
// glow out: room for all of it: all; else never the border's four strips, the sign and the glow out; short of both of
// those, only the sign (the glow out alone while the sign has stopped); else nothing. The hint only ever takes slots no
// other layer of the frame wants.
inline Draw Fit(Draw d,uint32_t room){
    if(room>=d.most)return d;
    d.side=d.along={};
    if(room<d.most-Edges&&d.sign.w>0)d.lobe={};
    d.need=uint32_t(d.lobe.w>0)+uint32_t(d.sign.w>0);
    return d.need<=room?d:Draw{};
}

// The log, quiet in play: a line for the unusual only (a pull while it showed; the layer budget leaving part of it out,
// at most one a DropQuietS, the rest counted onto the next), and a summary of the showings at most once a SummaryS.
constexpr double SummaryS=60,DropQuietS=10;
struct ShowLog {
    double shownAt=0;
    int dropped=0;            // the layer budget this showing: 1 the strips left out, 2 more
    unsigned shows=0;double shownS=0,longestS=0,summaryAt=-1e9;   // the showings since the last summary
    double dropAt=-1e9;unsigned dropsUntold=0;
    void Shown(double t){shownAt=t;dropped=0;}
    void Ended(double t){++shows;const double s=t-shownAt;shownS+=s;longestS=std::max(longestS,s);}
    // a summary is due (the first showing's at once, then at most one a SummaryS); Summarized starts the next count
    bool SummaryDue(double t){if(!shows||t-summaryAt<SummaryS)return false;summaryAt=t;return true;}
    void Summarized(){shows=0;shownS=longestS=0;}
    bool DropDue(double t){if(t-dropAt>=DropQuietS){dropAt=t;return true;}++dropsUntold;return false;}
};
}  // namespace framevr::grabhint
