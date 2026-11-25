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
inline bool UseHistory(bool surface,bool lastWasChat,bool spatial){return surface&&lastWasChat&&!spatial;}
// The chat is for multiplayer games only (owner, 2026-11-24): a game (a local player) whose client mode, as the game's
// own getter answers it (offlinedifficulty::ClientMode, verified by its bytes), is not 0, a local offline game; 5 and 7
// are TCP/IP, hosted or joined through D2RLoader's multiplayer panel. Not verified yet (-1) counts as not. Without it
// there is no CHAT tab (Snapshot::multiplayer) and the transport runs the chat as outside a game (session 0), so one
// open, pinned or typing closes as the game stops being a multiplayer one.
inline bool Available(bool inGame,int clientMode){return inGame&&clientMode>0;}
// Keep the native history image on the HUD capture side, independent of the
// XR surface also used by inventory and other menus. A missed draw is not a reset.
struct HistoryFrame {
 uint64_t session=0;bool available=false;
 void Begin(uint64_t s){if(session!=s)available=false;session=s;}
 bool Select(bool stock)const{return session&&(!stock||!available);}
 void Publish(bool stock){if(Select(stock))available=true;}
};
// Capture the initial native history before the wrist tab is first visited.
struct FirstHistory {
 uint64_t session=0,until=0;bool done=false;
 bool Want(uint64_t s,uint64_t now,bool gameplay,bool captured){
  if(s!=session){session=s;until=0;done=false;}
  if(!s||done)return false;
  if(captured||(until&&now>=until)){done=true;return false;}
  if(!gameplay)return false;
  if(!until)until=now+10000;
  return true;
 }
};
struct Composer {
 uint64_t next=0;bool Allow(uint64_t now)const{return now>=next;}
 void Submitted(uint64_t now){next=now+1000;}void Reset(){next=0;}
};
struct Model {
 uint64_t session=0;bool open=false,pinned=false,editing=false,dragging=false;int hand=1;bool tab=false;
 void Session(uint64_t s){if(s!=session){*this={};session=s;}}
 void Open(int h){open=tab=true;hand=h==0?0:1;}
 void LeaveTab(){tab=editing=false;if(!pinned)open=false;}
 void Close(){open=pinned=editing=dragging=tab=false;}
 bool OwnsWheel(bool visible,int wheel)const{return visible&&session&&open&&tab&&wheel==hand;}
 bool NeedsSource(bool visible,int wheel)const{return visible&&session&&open&&(pinned||OwnsWheel(visible,wheel));}
 bool CanType()const{return session!=0&&open&&tab&&!dragging;}
 void Focus(bool nativeReady){editing=CanType()&&nativeReady;}
 void Detach(){pinned=true;tab=editing=false;}
 static bool AtDock(math::Vec delta,float width,float height){return std::abs(delta.x)<=width*.42f&&std::abs(delta.y)<=height*.36f&&std::abs(delta.z)<=.075f;}
 void Dock(){pinned=false;open=tab=true;editing=dragging=false;}
};
}
