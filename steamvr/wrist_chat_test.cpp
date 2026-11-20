#include "wrist_chat.h"
#include <cassert>
#include <cstdio>
using namespace framevr;
int main(){
 wristchat::Model m;m.Open(1);assert(!m.CanType());m.Session(1);m.Open(1);assert(m.open&&!m.editing);
 m.Focus(true);assert(m.editing);m.Detach();assert(m.pinned&&!m.editing&&!m.CanType());
 m.Open(0);assert(m.pinned&&!m.CanType()); // selecting Chat must not redock a HUD copy
 m.Focus(true);assert(!m.editing);m.Dock();assert(!m.pinned&&!m.editing);
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
 keyboard::Typist type;auto space=type.Press(keyboard::KeyCount-3,true);assert(space.ch==L' ');
 type.Press(keyboard::KeyCount-2,true);assert(type.Symbols());assert(type.Press(0,true).ch==L'1');
 type.Reset();assert(type.Press(keyboard::KeyCount-3).ch==L'-');
 for(int i=0;i<keyboard::KeyCount;++i){
  assert(!keyboard::Label(i,false,true,false).empty());assert(!keyboard::Label(i,false,true,true).empty());
 }
 assert(keyboard::Label(keyboard::KeyCount-3,false,true)==L"Space");
 assert(keyboard::Label(keyboard::KeyCount-2,false,true,true)==L"ABC");
 const floatmap::Pose head{{1,2,3},math::Yaw(.7f)},panel{{.2f,1.2f,-.5f},math::Yaw(-.1f)};
 const auto relative=floatmap::Relative(head,panel),restored=floatmap::Compose(head,relative);
 assert(math::Dot(restored.p-panel.p,restored.p-panel.p)<.00001f);
 puts("wrist chat: detached read-only, lifecycle, isolated capture, chat keyboard passed");
}
