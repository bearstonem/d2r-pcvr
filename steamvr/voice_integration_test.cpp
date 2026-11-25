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
    assert(e.Edit(5,1)&&e.mode==2);assert(e.Edit(5,2)&&e.mode==2);assert(e.Edit(5,0)&&e.mode==0);
    settings::Model model;settings::Snapshot snapshot;snapshot.ready=true;model.Open(snapshot);model.view=settings::View::Voice;model.voice=e;
    auto layout=settings::layout::Build(model,{},{});int keys=0;bool save=false;
    for(const auto& element:layout.elements){if(element.hit.kind==settings::Kind::VoiceEdit){++keys;assert(element.rect.w>=64&&element.rect.h>=64);auto hit=settings::layout::HitTest(layout,element.rect.x+element.rect.w/2.f,element.rect.y+element.rect.h/2.f);assert(hit==element.hit);}if(element.hit.kind==settings::Kind::VoiceSave)save=true;}
    assert(keys>=9&&save);
    assert(e.Edit(9,0)&&e.picker);assert(e.Edit(11,2)&&!e.picker&&e.bindings[0].skill==3906);
    model.voice=e;model.voice.picker=true;auto picker=settings::layout::Build(model,{},{});int skills=0;
    for(const auto& element:picker.elements)if(element.role==settings::layout::Role::VoiceSkill){++skills;assert(element.hit.kind==settings::Kind::VoiceEdit&&element.hit.index==11);assert(element.option==e.choices[size_t(element.hit.delta)].id);}
    assert(skills==3);
    assert(e.Edit(7,1)&&e.sample==1);assert(e.Edit(8,0)&&e.tolerance==0);
    // Standalone test executable lives in an isolated fixture directory.
    voice::Runtime runtime;voice::Editor saved;runtime.SelectCharacter(saved,"Fixture",4,0);runtime.Load(saved);saved.bindings=e.bindings;saved.tolerance=e.tolerance;assert(runtime.Save(saved));runtime.Close();
    voice::Runtime reopened;voice::Editor restored;reopened.SelectCharacter(restored,"Fixture",4,0);reopened.Load(restored);assert(restored.bindings[0].phrase=="battle orders"&&restored.bindings[0].skill==3906&&restored.mode==0&&restored.tolerance==0);
    voice::Editor six;
    assert(six.tolerance==2); // High is the old Loose acceptance distance.
    for(int i=0;i<6;++i){assert(six.slot==i);six.bindings[size_t(i)].skill=149+i;assert(!six.AllowsCast(149+i));six.Edit(0,1);}assert(six.slot==0);
    six.Edit(5,2);assert(six.dirty);for(int i=0;i<6;++i)assert(!six.AllowsCast(149+i)); // Unsaved On cannot enable casts.
    six.Commit();for(int i=0;i<6;++i)assert(six.AllowsCast(149+i));assert(!six.AllowsCast(999));
    six.Edit(5,0);assert(six.dirty&&six.AllowsCast(149)); // Applied state stays explicit until Save.
    six.Edit(12,0);assert(!six.dirty&&six.mode==2&&six.AllowsCast(149));
    six.Edit(5,0);six.Commit();for(int i=0;i<6;++i)assert(!six.AllowsCast(149+i));
    voice::Runtime sixRuntime;voice::Editor sixSaved;sixRuntime.SelectCharacter(sixSaved,"Fixture",4,0);sixRuntime.Load(sixSaved);sixSaved.mode=0;sixSaved.bindings=six.bindings;assert(sixRuntime.Save(sixSaved));sixRuntime.Close();
    voice::Runtime sixReload;voice::Editor sixRestored;sixReload.SelectCharacter(sixRestored,"Fixture",4,0);sixReload.Load(sixRestored);for(int i=0;i<6;++i)assert(sixRestored.bindings[size_t(i)].skill==149+i);assert(sixRestored.mode==0);
    // The controller restores the saved gameplay mode after settings and sample actions.
    voice::Runtime flow(false);voice::Editor draft;flow.SelectCharacter(draft,"Fixture",4,0);flow.Load(draft);draft.mode=0;assert(flow.Save(draft));
    assert(flow.Sync(draft,true));
    const auto mapName=L"Local\\D2RVRVoiceIntegrated"+std::to_wstring(GetCurrentProcessId());
    HANDLE mapping=OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,mapName.c_str());assert(mapping);
    auto* control=static_cast<voice::Control*>(MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(voice::Control)));assert(control&&control->mode==1);
    draft.Edit(5,2);assert(draft.dirty&&!draft.AllowsCast(149));assert(flow.Sync(draft,false)&&control->mode==0);
    assert(flow.Save(draft)&&draft.savedMode==2&&!draft.dirty);assert(flow.Sync(draft,false)&&control->mode==2);
    assert(flow.Save(draft,3)&&control->mode==3&&draft.mode==2&&draft.savedMode==2);
    assert(flow.Sync(draft,true)&&control->mode==3); // A frame update must not cancel recording.
    ++control->telemetry.padding;flow.Poll(draft);assert(flow.Sync(draft,true)&&control->mode==1);
    assert(flow.Sync(draft,false)&&control->mode==2); // Closing restores On, not Off.
    draft.Edit(5,0);assert(flow.Save(draft)&&control->mode==0);
    // A failed action publication must not latch recording and block page sync.
    voice::Runtime missingHelper;voice::Editor missing;missingHelper.SelectCharacter(missing,"Fixture",4,0);missingHelper.Load(missing);
    assert(!missingHelper.Save(missing,3));
    assert(!missingHelper.Sync(missing,true));
    missingHelper.Close();
    UnmapViewOfFile(control);CloseHandle(mapping);flow.Close();
    const auto barb="Barb"+std::to_string(GetCurrentProcessId()),sorc="Sorc"+std::to_string(GetCurrentProcessId());
    voice::Runtime profiles(false);voice::Editor character;
    profiles.SelectCharacter(character,barb,4,0);
    character.bindings[0].skill=149;character.mode=2;assert(profiles.Save(character));
    profiles.SelectCharacter(character,sorc,1,0);
    assert(character.savedMode==0&&character.bindings[0].skill==-1);
    character.bindings[0].skill=54;assert(profiles.Save(character));
    profiles.SelectCharacter(character,barb,4,0);
    assert(character.savedMode==2&&character.bindings[0].skill==149);
    character.Edit(5,0);assert(character.dirty);
    profiles.SelectCharacter(character,sorc,1,0);
    assert(character.bindings[0].skill==54&&!character.dirty);
    profiles.SelectCharacter(character,barb,4,0);
    assert(character.dirty&&character.mode==0&&character.savedMode==2);
    profiles.SelectCharacter(character,"",-1,0);assert(!profiles.Save(character)&&!character.AllowsCast(149));
    profiles.Close();
    voice::Runtime restart(false);voice::Editor reload;
    restart.SelectCharacter(reload,barb,4,0);assert(reload.savedMode==2&&reload.bindings[0].skill==149);
    restart.SelectCharacter(reload,barb,4,1);assert(reload.savedMode==0&&reload.bindings[0].skill==-1);
    std::puts("Six saved bindings, unsaved isolation, automatic validation, recording and saved On resume passed");
    std::puts("VR recorded sound controls, action assignment, hit targets and saved tolerance round-trip passed");
}


