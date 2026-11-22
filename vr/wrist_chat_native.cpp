// Native game chat history, not a second network/chat protocol.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <D2RLPlugin/api.h>
#include <atomic>
#include <mutex>
#include "pcvr_identity.h"
#include <cstring>
#include "wrist_chat_native.h"
namespace chatfocus {
using namespace D2RL;
using H=Widgets::WidgetHandle;using R=Widgets::Result;using C=const PluginContext*;
struct State{bool visible,enabled,focused;};struct Rect{float x,y,w,h;};
// Append-only WidgetService ABI from PluginSDK 03a5af9. Keep the base SDK pin for combat.
struct Api{
 using S=R(__cdecl*)(C,H,State*) noexcept; using F=R(__cdecl*)(C,H) noexcept;using G=R(__cdecl*)(C,H,Rect*) noexcept;
 template<class T>static T slot(const WidgetService* p,size_t n){T out{};if(p&&p->serviceVersion==1&&p->serviceSize>=n+sizeof(out))std::memcpy(&out,reinterpret_cast<const char*>(p)+n,sizeof(out));return out;}
 S state; F focus; G rect;
 explicit Api(const WidgetService* p):state(slot<S>(p,104)),focus(slot<F>(p,136)),rect(slot<G>(p,200)){} bool ready()const{return state&&focus&&rect;}
};
}
namespace wristchatnative {
namespace {
constexpr char Name[]=D2RPCVR_PLUGIN_ID "/WristChat";
// All visible children are inside this list's scissor, redirected by hud.cpp.
// No input box or click catcher: the source widget must not take gameplay input.
constexpr char Layout[]=R"json({
 "type":"Panel","name":"d2r-pcvr-vrcam/WristChat",
 "fields":{"anchor":{"x":0,"y":0},"rect":{"x":1320,"y":200,"width":1200,"height":770},"priority":8500,"consumeMouseClicks":false},
 "children":[{"type":"ChatListWidget","name":"ListContainer","fields":{
 "fitToParent":true,"isHoverList":false,"direction":"vertical","scrollControllerName":"ScrollBar","chatDataSource":"ChatHistory",
 "growsBottomUp":true,"itemsContainerSizeMultiplier":5.0,"consumeMouseClicks":false},
 "children":[{"type":"ListItemWidget","name":"ChatEntryTemplate","fields":{
 "rect":{"x":8,"y":0,"width":1184,"height":50},"consumeMouseClicks":false},
 "children":[{"type":"TextBoxWithLinksWidget","name":"MessageText","fields":{
 "fitToParent":true,"consumeMouseClicks":false,"text/fontType":"Chat","text/style":{
 "fontFace":"BlizzardGlobal","pointSize":42,"fontColor":{"r":255,"g":255,"b":255,"a":255},
 "options":{"pipeCharacterBehavior":"handle","newlineHandling":"standard","lineWrap":true}}}}]}]},{"type":"ScrollControllerWidget","name":"ScrollBar","fields":{"rect":{"x":1198,"y":4,"width":1,"height":756},"viewName":"ListContainer","focusOnMouseOver":false,"focusIndicatorVisible":false,"consumeMouseClicks":false}}]})json";
const D2RL::PluginContext* ctx=nullptr;
const D2RL::PanelService* panels=nullptr;
const D2RL::ThreadService* threads=nullptr;
const D2RL::WidgetService* widgets=nullptr;
D2RL::Panels::RegistrationHandle panel=0;
std::atomic<bool> wanted{false},ready{false},dead{true},queued{false};
std::atomic<uint64_t> session{0};
bool (*typingReader)()=nullptr;bool (*supportReader)()=nullptr;
std::atomic<bool> editing{false},inputReady{false};
std::mutex textMutex;std::string draft;
void __cdecl Update(const D2RL::PluginContext* c,void*) noexcept {
 queued=false;if(dead||!panels||!panel)return;
 const bool want=wanted&&session.load();
 D2RL::Panels::PanelInfo info{};info.structSize=sizeof info;
 if(panels->getPanelInfo(c,panel,&info)!=D2RL::Panels::Result::Success){ready=false;return;}
 const bool open=info.presentationState==D2RL::Panels::PresentationState::Open;
 if(want&&!open)panels->openPanel(c,panel);
 else if(!want&&open)panels->closePanel(c,panel);
 info={};info.structSize=sizeof info;
 ready=want&&panels->getPanelInfo(c,panel,&info)==D2RL::Panels::Result::Success&&info.presentationState==D2RL::Panels::PresentationState::Open;
 std::string text;bool usable=false;
 if(want&&Typing()&&widgets) {
  D2RL::Widgets::WidgetHandle p=0,input=0;char buffer[2048]{};uint32_t need=0;
  if(widgets->findPanel(c,"ChatPanel",&p)==D2RL::Widgets::Result::Success&&
     widgets->findWidget(c,p,"InputBox",&input)==D2RL::Widgets::Result::Success){
   usable=true;chatfocus::Api api(widgets);
   if(editing&&api.state&&api.focus){chatfocus::State state{};
    usable=api.state(c,input,&state)==chatfocus::R::Success&&state.visible&&state.enabled;
    if(usable&&!state.focused)usable=api.focus(c,input)==chatfocus::R::Success&&api.state(c,input,&state)==chatfocus::R::Success&&state.focused;
   }
   if(widgets->getInputText(c,input,buffer,sizeof buffer,&need)==D2RL::Widgets::Result::Success)text=buffer;
  }
 }
 inputReady=editing&&usable;
 {std::lock_guard lock(textMutex);draft=std::move(text);}
}
}
void Init(const D2RL::PluginContext* c,bool (*typing)(),bool (*supported)()) {
 if(!c)return;ctx=c;typingReader=typing;supportReader=supported;
 const D2RL::ResourceService* resources=nullptr;
 if(c->QueryService(&resources)!=D2RL::ServiceQueryResult::Success||!D2RL::HasResourceServiceField(resources,D2RL::ResourceServiceRequiredSize)||!resources->registerResource||
    c->QueryService(&panels)!=D2RL::ServiceQueryResult::Success||!D2RL::HasPanelServiceField(panels,D2RL::PanelServiceRequiredSize)||!panels->registerPanel||!panels->getPanelInfo||!panels->openPanel||!panels->closePanel||
    c->QueryService(&threads)!=D2RL::ServiceQueryResult::Success||!D2RL::HasThreadServiceField(threads,D2RL::ThreadServiceRequiredSize)||!threads->runOnUiThread)return;
 if(c->QueryService(&widgets)!=D2RL::ServiceQueryResult::Success||!D2RL::HasWidgetServiceField(widgets,D2RL::WidgetServiceRequiredSize)||!widgets->findPanel||!widgets->findWidget||!widgets->getInputText)widgets=nullptr;
 D2RL::Resources::ResourceRegistration resource{};resource.structSize=D2RL::Resources::ResourceRegistrationSize;resource.flags=0;
 resource.path="data/global/ui/layouts/" D2RPCVR_PLUGIN_ID "/WristChathd.json";resource.bytes=Layout;resource.byteCount=sizeof Layout-1;
 D2RL::Resources::RegistrationHandle handle=0;
 if(resources->registerResource(c,&resource,&handle)!=D2RL::Resources::Result::Success)return;
 D2RL::Panels::PanelRegistration registration{};registration.structSize=D2RL::Panels::PanelRegistrationSize;registration.flags=D2RL::Panels::PanelFlags::None;registration.localId="WristChat";
 if(panels->registerPanel(c,&registration,&panel)!=D2RL::Panels::Result::Success)return;
 dead=false;c->LogInfo("Wrist chat: native ChatHistory panel registered (local candidate)");
}
void Want(bool visible,uint64_t s){session=s;wanted=visible&&s;}
void Tick(){if(!dead&&ctx&&threads&&!queued.exchange(true)&&threads->runOnUiThread(ctx,&Update,nullptr)!=D2RL::Threads::Result::Success)queued=false;}
void Stop(){wanted=ready=false;dead=true;}
void Editing(bool on){editing=on;if(!on)inputReady=false;}
bool InputReady(){return inputReady&&editing&&Typing();}
bool Ready(){return ready&&!dead;}
bool Typing(){return typingReader&&typingReader();}
bool Supported(){return !dead&&supportReader&&supportReader();}
std::string Draft(){std::lock_guard lock(textMutex);return draft;}
}
