#include "wrist_chat.h"
#include <cassert>
#include <cstdio>
using namespace framevr;
int main(){
 wristchat::HistoryFrame history;history.Begin(11);assert(!history.available);
 assert(history.Select(true));history.Publish(true);history.Begin(11);assert(history.available&&!history.Select(true));
 assert(history.Select(false));history.Publish(false);history.Begin(11);assert(history.available);
 assert(!history.Select(true));assert(history.Select(false)); // popup must never replace full history
 for(int i=0;i<9000;++i){history.Begin(11);assert(history.available);} // missing redraws are not a history reset
 history.Begin(12);assert(!history.available);history.Begin(0);assert(!history.Select(true));
 wristchat::Model route;route.Session(9);route.Open(1);
 assert(route.NeedsSource(true,1));assert(!route.NeedsSource(false,1)); // inventory owns the surface
 assert(!route.NeedsSource(true,-1));route.Detach();assert(route.NeedsSource(true,-1));
 assert(!route.OwnsWheel(true,1));route.Open(1);assert(route.OwnsWheel(true,1));
 assert(!route.OwnsWheel(false,1)&&!route.OwnsWheel(true,0));route.Close();assert(!route.NeedsSource(true,1));
 wristchat::FirstHistory seed;assert(!seed.Want(0,10,true,false));
 assert(!seed.Want(11,100,false,false));assert(seed.Want(11,1000,true,false));
 assert(!seed.Want(11,1100,false,false));assert(!seed.Want(11,1200,true,true));assert(!seed.Want(11,1300,true,false));
 assert(seed.Want(12,5000,true,false));assert(!seed.Want(12,16000,true,false));
 wristchat::Composer gate;assert(gate.Allow(1));gate.Submitted(100);assert(!gate.Allow(101));assert(gate.Allow(1100));gate.Reset();assert(gate.Allow(1));
 assert(!wristchat::UseHistory(true,false,false)&&wristchat::UseHistory(true,true,false)&&!wristchat::UseHistory(true,true,true));
 wristchat::Model m;m.Open(1);assert(!m.CanType());m.Session(1);m.Open(1);assert(m.open&&!m.editing);
 m.Focus(true);assert(m.editing);m.Detach();assert(m.pinned&&!m.editing&&!m.CanType());
 m.Open(0);assert(m.pinned&&m.CanType()&&m.hand==0); // selecting Chat must not redock a HUD copy
 m.Focus(true);assert(m.editing);m.LeaveTab();assert(m.pinned&&m.open&&!m.CanType());m.Open(1);m.Dock();assert(!m.pinned&&!m.editing&&m.CanType());
 m.dragging=true;m.Focus(true);assert(!m.editing&&!m.CanType());m.dragging=false;
 m.Focus(false);assert(!m.editing);m.Focus(true);assert(m.editing);m.Close();assert(!m.open&&!m.editing);
 assert(m.AtDock({0,0,.03f},.45f,.45f));assert(!m.AtDock({0,0,.2f},.45f,.45f));assert(!m.AtDock({.3f,0,0},.45f,.45f));
 m.Open(0);m.Detach();m.Session(2);assert(!m.open&&!m.pinned&&!m.editing);
 assert(wristchat::InputHit(.5f,.87f));assert(!wristchat::InputHit(.5f,.2f));
 assert(wristchat::Scissor(1181,342,1891,797,3072,3072,.740f)); // actual VR history list
 assert(!wristchat::Scissor(423,763,1133,1218,3072,3072,.740f)); // stock, not private history
 assert(wristchat::StockList(423,763,1133,1218,3072,3072,.740f));
 assert(wristchat::Scissor(1320,200,2520,970,3840,2160,1));
 assert(!wristchat::Scissor(0,0,1200,770,3840,2160,1));
 assert(!wristchat::Scissor(1320,200,2520,970,3840,2160,0));
 keyboard::Typist type;auto space=type.Press(keyboard::Name,keyboard::Name.count-3,true);assert(space.ch==L' ');
 type.Press(keyboard::Name,keyboard::Name.count-2,true);assert(type.Symbols());assert(type.Press(keyboard::Name,0,true).ch==L'1');
 type.Reset();assert(type.Press(keyboard::Name,keyboard::Name.count-3).ch==L'-');
 for(int i=0;i<keyboard::Name.count;++i){
  assert(!keyboard::Label(keyboard::Name,i,false,true,false).empty());assert(!keyboard::Label(keyboard::Name,i,false,true,true).empty());
 }
 assert(keyboard::Label(keyboard::Name,keyboard::Name.count-3,false,true)==L"Space");
 assert(keyboard::Label(keyboard::Name,keyboard::Name.count-2,false,true,true)==L"ABC");
 const floatmap::Pose head{{1,2,3},math::Yaw(.7f)},panel{{.2f,1.2f,-.5f},math::Yaw(-.1f)};
 const auto relative=floatmap::Relative(head,panel),restored=floatmap::Compose(head,relative);
 assert(math::Dot(restored.p-panel.p,restored.p-panel.p)<.00001f);
 puts("wrist chat: detached read-only, lifecycle, isolated capture, chat keyboard passed");
}
