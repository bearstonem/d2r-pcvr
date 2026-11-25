#include "voice_runtime.h"
#include "vr_settings_layout.h"
#include <cassert>
#include <cstdio>
int main(){
    using namespace framevr;
    voice::Editor e;e.choices={{-1,"Unassigned"},{149,"Battle Orders"},{3906,"Regroup"}};
    for(char c:std::string("battle orders"))assert(e.Edit(1,c));
    assert(e.Edit(4,1)&&e.bindings[0].skill==149);
    assert(e.Edit(0,1)&&e.slot==1);assert(e.Edit(1,'a'));assert(e.Edit(2,0)&&e.bindings[1].phrase.empty());
    assert(e.Edit(0,-1)&&e.bindings[0].phrase=="battle orders");
    assert(e.Edit(5,0)&&e.mode==1);assert(e.Edit(5,0)&&e.mode==2);assert(e.Edit(5,0)&&e.mode==0);
    settings::Model model;settings::Snapshot snapshot;snapshot.ready=true;model.Open(snapshot);model.view=settings::View::Voice;model.voice=e;
    auto layout=settings::layout::Build(model,{},{});int keys=0;bool save=false;
    for(const auto& element:layout.elements){if(element.hit.kind==settings::Kind::VoiceEdit){++keys;assert(element.rect.w>=64&&element.rect.h>=64);auto hit=settings::layout::HitTest(layout,element.rect.x+element.rect.w/2.f,element.rect.y+element.rect.h/2.f);assert(hit==element.hit);}if(element.hit.kind==settings::Kind::VoiceSave)save=true;}
    assert(keys==35&&save);
    // Standalone test executable lives in an isolated fixture directory.
    voice::Runtime runtime;voice::Editor saved;runtime.Load(saved);saved.bindings=e.bindings;assert(runtime.Save(saved));runtime.Close();
    voice::Runtime reopened;voice::Editor restored;reopened.Load(restored);assert(restored.bindings[0].phrase=="battle orders"&&restored.bindings[0].skill==149&&restored.mode==0);
    std::puts("VR voice keyboard, action assignment, hit targets and saved phrase round-trip passed");
}
