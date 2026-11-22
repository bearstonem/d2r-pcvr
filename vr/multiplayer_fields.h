#pragma once
#include <D2RLPlugin/widgets.h>
#include <cstring>
namespace mpfields {
using namespace D2RL;
using H=Widgets::WidgetHandle;using R=Widgets::Result;using C=const PluginContext*;
struct State{bool visible,enabled,focused;};struct Rect{float x,y,w,h;};
struct Box{bool available=false,focused=false;float x=0,y=0,w=0,h=0;H handle=0;};
// Append-only WidgetService ABI from PluginSDK 03a5af9. Keep the base SDK pin for combat.
struct Api{
 using S=R(__cdecl*)(C,H,State*) noexcept; using F=R(__cdecl*)(C,H) noexcept;using G=R(__cdecl*)(C,H,Rect*) noexcept;
 template<class T>static T slot(const WidgetService* p,size_t n){T out{};if(p&&p->serviceVersion==1&&p->serviceSize>=n+sizeof(out))std::memcpy(&out,reinterpret_cast<const char*>(p)+n,sizeof(out));return out;}
 S state; F focus; G rect;
 explicit Api(const WidgetService* p):state(slot<S>(p,104)),focus(slot<F>(p,136)),rect(slot<G>(p,200)){} bool ready()const{return state&&focus&&rect;}
};
inline int First(const Box* b){return b[6].available?6:(b[4].available||b[5].available?4:0);}
inline int End(const Box* b){return First(b)==0?4:First(b)==4?6:7;}
inline int Pick(const Box* b,float x,float y){for(int i=First(b);i<End(b);++i){auto& v=b[i];if(v.available&&x>=v.x&&y>=v.y&&x<v.x+v.w&&y<v.y+v.h)return i;}return -1;}
inline int Active(const Box* b){for(int i=First(b);i<End(b);++i)if(b[i].available&&b[i].focused)return i;return -1;}
inline int Scan(C c,const WidgetService* svc,int x,int y){Api a(svc);if(!a.ready()||!svc->findWidget||!svc->findPanel)return -1;
 constexpr const char* names[]={"HostGameNameInput","HostPasswordInput","HostDescriptionInput","SearchInput","DirectJoinAddressInput","DirectJoinPasswordInput","JoinPasswordInput"};
 constexpr const char* overlays[]={"HostGameNameDisabledOverlay","HostPasswordDisabledOverlay","HostDescriptionDisabledOverlay"};Box boxes[7]{};H panel{};
 if(svc->findPanel(c,"D2RLoaderMultiplayerPanel",&panel)!=R::Success)return -1;
 for(int i=0;i<7;++i){H h{};State st{};Rect r{};if(svc->findWidget(c,panel,names[i],&h)!=R::Success||a.state(c,h,&st)!=R::Success||!st.visible||!st.enabled||a.rect(c,h,&r)!=R::Success||!(r.w>0&&r.h>0))continue;
 if(i<3){H cover{};State s{};if(svc->findWidget(c,panel,overlays[i],&cover)==R::Success&&a.state(c,cover,&s)==R::Success&&s.visible)continue;}
 boxes[i]={true,st.focused,r.x,r.y,r.w,r.h,h};}
 if(x<0)return Active(boxes);int i=Pick(boxes,float(x),float(y));if(i<0)return -1;State after{};auto h=boxes[i].handle;
 return a.focus(c,h)==R::Success&&a.state(c,h,&after)==R::Success&&after.visible&&after.enabled&&after.focused?i:-1;
}
}
