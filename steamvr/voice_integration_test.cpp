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
    assert(e.Edit(5,1)&&e.mode==1);assert(e.Edit(5,2)&&e.mode==2);assert(e.Edit(5,0)&&e.mode==0);
    settings::Model model;settings::Snapshot snapshot;snapshot.ready=true;model.Open(snapshot);model.view=settings::View::Voice;model.voice=e;
    auto layout=settings::layout::Build(model,{},{});int keys=0;bool save=false;
    for(const auto& element:layout.elements){if(element.hit.kind==settings::Kind::VoiceEdit){++keys;assert(element.rect.w>=64&&element.rect.h>=64);auto hit=settings::layout::HitTest(layout,element.rect.x+element.rect.w/2.f,element.rect.y+element.rect.h/2.f);assert(hit==element.hit);}if(element.hit.kind==settings::Kind::VoiceSave)save=true;}
    assert(keys==10&&save);
    assert(e.Edit(9,0)&&e.picker);assert(e.Edit(11,2)&&!e.picker&&e.bindings[0].skill==3906);
    model.voice=e;model.voice.picker=true;auto picker=settings::layout::Build(model,{},{});int skills=0;
    for(const auto& element:picker.elements)if(element.role==settings::layout::Role::VoiceSkill){++skills;assert(element.hit.kind==settings::Kind::VoiceEdit&&element.hit.index==11);assert(element.option==e.choices[size_t(element.hit.delta)].id);}
    assert(skills==3);
    assert(e.Edit(7,1)&&e.sample==1);assert(e.Edit(8,0)&&e.tolerance==2);
    // Standalone test executable lives in an isolated fixture directory.
    voice::Runtime runtime;voice::Editor saved;runtime.Load(saved);saved.bindings=e.bindings;saved.tolerance=e.tolerance;assert(runtime.Save(saved));runtime.Close();
    voice::Runtime reopened;voice::Editor restored;reopened.Load(restored);assert(restored.bindings[0].phrase=="battle orders"&&restored.bindings[0].skill==3906&&restored.mode==0&&restored.tolerance==2);
    std::puts("VR recorded sound controls, action assignment, hit targets and saved tolerance round-trip passed");
}
