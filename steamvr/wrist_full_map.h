#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>

// The wrist map from the game's full (Tab) map, VR Settings > HUD > Map view = Full map /
// Full top-down. The corner map the wrist copies otherwise is culled by the game to its
// own box, so 1x already shows all of it and nothing further out exists. The full map is
// a screen-sized window around the hero at a zoom the game takes per call (D2R 3.3.93787):
//  - while the wrist captures, the game's automap layout is borrowed as full (0) instead
//    of the left corner (gamestate MaintainMapLayout, the same lease);
//  - the full map draws its tiles first in the interface pass, under one scissor: the
//    layer's width by its 16:9 band from the top-left (2112x1188 on a 2112x2304 layer).
//    While the wrist captures, those draws go to the wrist's target instead of the eyes
//    (vr/hud.cpp OnScissor), and that box is the wrist's source;
//  - its draw passes a zoom to the scale function at RVA 0xD1940, which uses
//    max(round(zoom*8)/8, 0.125) and 4 pixels a unit times that (the full map's own is
//    0.5). For the wrist's capture only, that argument is replaced (nothing in the game's
//    memory is written): the widest view is the game's floor, 0.125.
// The right stick's map zoom (map_zoom.h) then zooms in smoothly from there: the game gets
// the 1/8 step at or below the zoom wanted and the wrist magnifies the rest (under 2x), so
// it stays sharp at every zoom and never steps: each share is tagged with the zoom it was
// really drawn at, and the wrist scales by wanted / drawn.
// Each map piece in that box is its own draw: ~2,050 a pass on a 3072-px layer, at 0.125 or
// 0.25 alike (the game draws every explored piece; owner, Steam Frame, 2026-10-24). They are
// drawn for the wrist in a frame's first pass only (DrawForWrist). The frame rate the full
// map cost (By turns 88 -> 39 frames a second) was not these draws but the share's size:
// the hero marker's search, one GPU thread over the 3072x1728 share, took 23.6 ms a frame
// (RTX 5090); in tiles it takes 0.14 (ui_blit.cpp FindTiles).
namespace framevr::fullmap {
// Set by the transport from the setting; read by gamestate (the lease, the hooks) and hud.
inline std::atomic<bool> g_on{false};
// Set by gamestate on the UI thread: the wrist capture's lease is held, the game's layout
// (0 full, 1 left, 2 right, -1 unread), the AutoMap panel is open.
inline std::atomic<bool> g_capture{false},g_panel{false};
inline std::atomic<int> g_layout{-1};
// Set by the transport: the zoom wanted for the wrist (0.125 = the widest; 0 = none).
// Set by the hook: the step it gave the full map's last draw (0: none given).
inline std::atomic<float> g_wanted{0.f},g_applied{0.f};

struct Box {int32_t left=0,top=0,right=0,bottom=0;};
// The full map's box on a w x h layer: the layer's width, 16:9 tall, from the top-left.
inline Box FullBox(uint32_t w,uint32_t h){
    const uint32_t band=uint32_t((uint64_t(w)*9+8)/16);
    return {0,0,int32_t(w),int32_t(band<h?band:h)};
}
// A scissor is the full map's box (a pixel either way for its rounding). Never when the
// band is the whole layer (a 16:9 layer): that box is the interface's full-screen one too.
inline bool IsFullBox(int32_t l,int32_t t,int32_t r,int32_t b,uint32_t w,uint32_t h){
    if(!w||!h)return false;
    const Box f=FullBox(w,h);
    if(f.bottom+2>=int32_t(h))return false;
    return l==0&&t==0&&r==int32_t(w)&&b>=f.bottom-1&&b<=f.bottom+1&&b>0;
}
// A frame whose full map goes to the wrist: the setting on, the wrist capturing, the
// game's layout full, the AutoMap panel open.
inline bool Active(bool on,bool capture,int layout,bool panel){return on&&capture&&layout==0&&panel;}

// The game's zoom. Base: the widest view (the game's floor); Own: the full map's own.
constexpr float Base=.125f,Own=.5f,ArgLo=.05f,ArgHi=4.f;
// The zoom the game really uses for z (0xD1940: max(round(z*8)/8, 0.125)); 0 for none.
inline float GameRounded(float z){return std::isfinite(z)&&z>0?std::max(std::round(z*8)/8,Base):0.f;}
// The step the game is given for a wanted zoom: the 1/8 step at or below (it shows at
// least the area wanted, and the wrist magnifies the rest by under 2).
inline float GameStep(float wanted){return std::isfinite(wanted)&&wanted>0?std::max(std::floor(wanted*8+1e-3f)/8,Base):Base;}
// The argument the hook passes on: the game's own unless the gates hold, a zoom is wanted
// and the game's value is sane; then the step for the wanted zoom.
inline float ZoomArg(float game,float wanted,bool gates){
    if(!gates||!(wanted>0)||!std::isfinite(wanted)||!(game>=ArgLo&&game<=ArgHi))return game;
    return GameStep(wanted);
}

// One full map a frame. Both eyes draw the interface, and the wrist copies one share a frame
// (the transport, at the frame's first eye), so a later pass's full map would only be drawn
// over it. A pass draws it for the wrist unless this frame already did; with no frame start
// for longer than ReuseMs (nobody begins frames) every pass draws, as before.
constexpr uint64_t ReuseMs=100;
inline bool DrawForWrist(bool drawnThisFrame,uint64_t sinceMs){return !drawnThisFrame||sinceMs>ReuseMs;}

// The wrist square shows a square of `span` source pixels around the hero's X (the
// blit's heroAt), turned with you when the map turns, top-down undone (its diamond span
// wide). 1x is the largest span whose cover stays on the w x h box: flat, the box's
// height (/sqrt 2 when it turns, so its corners stay on); top-down, the diamond within it.
constexpr float Root2=1.41421356f;
inline float WristFill(uint32_t w,uint32_t h,bool turning,bool topDown){
    if(!w||!h)return 0.f;
    const float fw=float(w),fh=float(h);
    if(topDown)return std::min(fw,2*fh);
    return std::min(fw,fh)/(turning?Root2:1.f);
}
// The source pixels per wrist pixel the blit samples at most (it takes up to 4x4 taps).
inline int WristTaps(float span,float side){
    const float k=side>0&&span>0?span/side:1.f;
    return std::clamp(int(std::ceil(k-.001f)),1,4);
}
// The span for magnify (the map zoom, 1 = the widest view) when the share was drawn at
// game zoom `drawn` (0: the game's own, the hook gave none): the fill at the widest zoom,
// divided by what the share still has to be magnified. Never wider than the whole box
// seen at every turn (a share drawn closer than wanted, e.g. a frame before the hook).
inline float Span(uint32_t w,uint32_t h,bool turning,bool topDown,float magnify,float drawn){
    const float fill=WristFill(w,h,turning,topDown);
    if(fill<=0)return 0.f;
    const float m=std::isfinite(magnify)&&magnify>1?magnify:1.f;
    const float d=std::isfinite(drawn)&&drawn>0?drawn:Own;
    const float fw=float(w),fh=float(h);
    const float whole=topDown?(turning?Root2*std::sqrt(fw*fw+4*fh*fh):fw+2*fh):(turning?std::sqrt(fw*fw+fh*fh):std::max(fw,fh));
    return std::min(fill*d/(m*Base),std::max(whole,fill));
}
// The arrow sized from the game's X (FindX measures its half size in source pixels): as
// the X shows on the wrist at side/span wrist pixels a source pixel (top-down undoes the
// slant: its up arm grows 2*sqrt(2)), 1.2 times it, within 1.6% and 12% of the side. No X
// found: the corner map's size. ui_blit.cpp's PS does the same.
constexpr float ArrowSize=.024f,ArrowMin=.016f,ArrowMax=.12f,ArrowOverX=1.2f,TopDownXGain=2.8284271f;
// Where the X is searched on the full map (ui_blit.cpp xSearch): the game draws the hero at the
// share's center and its X grows with the game zoom, a little up and right of the center (headset
// trace 2026-10-24 on a 2112x1188 share, game zoom 0.125 to 2.0: half size about 33 px a unit of
// zoom plus 1, 4.5 px at 0.125 to 67 at 2.0; its middle about 17 px a unit above the center and
// 4 to the right, 31 up and 7 right at 1.75). The square is centered where the X's middle is
// expected and reaches 1.25 times its half size plus 6 px each way (12 px at 0.125, 90 at 2.0),
// in the share's own pixels (scaled by its height), so blue beside the X (water) stays out at
// every zoom. At 0.125 the old search (8% of the longer side each way, then every blue pixel in
// the 16 px tiles within 2% of the nearest) took in water 40 px off and sized the arrow 3-6x.
// No drawn zoom (the hook gave none): reach 0, the wide search.
constexpr float XHalfPerZoom=33.f,XHalfBase=1.f,XRisePerZoom=17.f,XRightPerZoom=4.f,XReachOverHalf=1.25f,XReachMargin=6.f,XShareHeight=1188.f;
struct XSearch {float right=0,down=0,reach=0;};   // the square's center from the share's center (px), and its reach each way
inline XSearch XSearchAt(float drawnZoom,uint32_t shareHeight){
    if(!(drawnZoom>0)||!std::isfinite(drawnZoom)||!shareHeight)return {};
    const float s=float(shareHeight)/XShareHeight;
    return {XRightPerZoom*drawnZoom*s,-XRisePerZoom*drawnZoom*s,(XReachOverHalf*(XHalfPerZoom*drawnZoom+XHalfBase)+XReachMargin)*s};
}
inline float ArrowRadius(float side,float span,float xHalf,bool topDown){
    if(!(xHalf>0)||!(span>0)||!(side>0))return side*ArrowSize;
    const float r=xHalf*side/span*(topDown?TopDownXGain:1.f)*ArrowOverX;
    return std::clamp(r,side*ArrowMin,side*ArrowMax);
}
}
