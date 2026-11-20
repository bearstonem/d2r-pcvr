#pragma once
#include <cmath>
#include <cstdint>
#include "vr_keyboard.h"
#include "floating_map.h"
namespace framevr::wristchat {
constexpr int Width=768,Height=512;
constexpr int SourceX=1320,SourceY=200,SourceWidth=1200,SourceHeight=770;
constexpr float PanelWidth=.48f,PanelHeight=.32f;
inline bool InputHit(float u,float v){return u>.02f&&u<.98f&&v>.81f&&v<.94f;}
inline bool CloseHit(float u,float v){return u>.88f&&u<.99f&&v>.01f&&v<.11f;}
// The custom list has a deliberately unique, fixed HD-layout rectangle. Never redirect
// the full-screen scissor or an approximate corner of the native HUD.
inline bool LayoutClip(int l,int t,int r,int b,int w,int h,float safe,int sx,int sy,int sw,int sh) {
 if(w<=0||h<=0||!std::isfinite(safe)||safe<=0||safe>1)return false;
 const float canvas=std::min(float(w)/3840.f,float(h)/2160.f),unit=canvas*safe;
 const float x=(w-3840.f*unit)*.5f+sx*unit,y=1080.f*canvas*(1-safe)+sy*unit;
 return std::abs(l-x)<=2&&std::abs(t-y)<=2&&std::abs(r-(x+sw*unit))<=2&&std::abs(b-(y+sh*unit))<=2;
}
inline bool Scissor(int l,int t,int r,int b,int w,int h,float safe){return LayoutClip(l,t,r,b,w,h,safe,SourceX,SourceY,SourceWidth,SourceHeight);}
inline bool StockList(int l,int t,int r,int b,int w,int h,float safe){return LayoutClip(l,t,r,b,w,h,safe,40,910,1200,770);}
struct Model {
 uint64_t session=0;bool open=false,pinned=false,editing=false,dragging=false;int hand=1;
 void Session(uint64_t s){if(s!=session){*this={};session=s;}}
 void Open(int h){if(pinned)return;open=true;pinned=editing=dragging=false;hand=h==0?0:1;}
 void Close(){open=pinned=editing=dragging=false;}
 bool CanType()const{return session!=0&&open&&!pinned&&!dragging;}
 void Focus(bool nativeReady){editing=CanType()&&nativeReady;}
 void Detach(){pinned=true;editing=false;}
 static bool AtDock(math::Vec delta,float width,float height){return std::abs(delta.x)<=width*.42f&&std::abs(delta.y)<=height*.36f&&std::abs(delta.z)<=.075f;}
 void Dock(){pinned=false;editing=dragging=false;}
};
}
