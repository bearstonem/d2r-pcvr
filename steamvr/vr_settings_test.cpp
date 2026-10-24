#include "vr_settings_help.h"
#include "vr_settings_pointer.h"
#include "../vr/shield_pose.h"
#include "reshade_addons.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
using namespace framevr::settings;
static void Check(bool b,const char* label){if(!b){std::fprintf(stderr,"FAIL: %s\n",label);std::exit(1);}}
// A press on a row's control, as the layout's - / + / value box sends it to Model::Click.
static Hit Step(Key key,int delta=1){return {Kind::Change,key,delta};}
// The view a setting's row lives on (the content tables).
static View PageOf(Key key){const auto* r=content::RowOf(key);return r?r->page:View::Count;}
static_assert((ChangeMask{1}<<64)==ChangeMask(0,1)&&(ChangeMask{1}<<127)==ChangeMask(0,uint64_t{1}<<63),"shifts cross into the high word");
static_assert((ChangeMask(0,1)>>64)==ChangeMask{1}&&(ChangeMask{3}<<63)==ChangeMask(uint64_t{1}<<63,1),"shifts carry between words");
// The menu's non-fatal report (docs/VR_SETTINGS_MENU.md): settings with no words yet,
// orphan overrides and preset entries, name-count mismatches, a non-empty More
// page. Printed on every run; --tidy prints only it. Never fails the test.
static void PrintReport(){
    const auto& report=content::Menu().report;
    std::printf("VR settings menu report: %zu item%s\n",report.size(),report.size()==1?"":"s");
    for(const auto& line:report)std::printf("  report: %s\n",line.c_str());
}
int main(int argc,char** argv){
    if(argc>1&&std::strcmp(argv[1],"--tidy")==0){PrintReport();return 0;}
    {
        int supported=0;
        for(int i=0;i<ChangeMask::Bits;++i){
            const ChangeMask bit=ChangeMask{1}<<i;
            Check(bit&&(bit>>i)==ChangeMask{1}&&(bit&bit)==bit&&!(bit&~bit),"every mask bit, below and above 63, round-trips");
            for(int j=0;j<ChangeMask::Bits;j+=7)Check(bool(bit&(ChangeMask{1}<<j))==(i==j),"mask bits stay distinct across both words");
            supported+=bool(bit&SupportedChanges);
            Check(bool(bit&~SupportedChanges)==(i>=Count),"only bits past the last setting are unsupported");
        }
        Check(supported==Count,"one supported bit per setting");
        ChangeMask mask=0;mask|=ChangeMask{1}<<5;mask|=ChangeMask{1}<<100;
        Check(mask==((ChangeMask{1}<<100)|(ChangeMask{1}<<5))&&mask!=0&&(mask&(ChangeMask{1}<<100))&&!(mask&(ChangeMask{1}<<64)),"low and high bits combine");
        Values a=Defaults(),b=a;b[Count-1]+=1;b[0]+=1;
        Check(Changed(a,b)==((ChangeMask{1}<<(Count-1))|ChangeMask{1})&&Conflict(b,a,ChangeMask{1}<<(Count-1))&&!Conflict(b,a,ChangeMask{1}<<1),"Changed and Conflict use the wide mask");
    }
    {   // What's new is read-only and opens without configuration.
        Snapshot state;state.ready=false;Model model;model.Open(state);
        layout::Press(model,{Kind::Open,int(View::WhatsNew),-1},0);
        Check(model.view==View::WhatsNew&&!model.Dirty(),"release notes remain readable without configuration");
    }
    {   // What's new pages a long release (main's Notes tab): Previous / Next under the lines, with or without settings
        Model notes;Snapshot none;notes.Open(none);notes.SetNotesPages(3);
        layout::Press(notes,{Kind::Open,int(View::WhatsNew),-1},0);
        const Hit next{Kind::NotesPage,0,1},previous{Kind::NotesPage,0,-1};
        Check(!notes.ready&&notes.notesPage==0,"opens on page 1, before any settings load");
        for(int i=0;i<3;++i)layout::Press(notes,next,0);
        Check(notes.notesPage==2,"Next stops at the last page");
        layout::Press(notes,previous,0);Check(notes.notesPage==1&&!notes.Dirty(),"Previous goes back; paging edits nothing");
        layout::Press(notes,{Kind::Open,int(View::Body),-1},0);layout::Press(notes,next,0);Check(notes.notesPage==1,"other views never page the notes");
        std::array<const char*,30> lines;lines.fill("A release line");
        layout::LayoutInput in;in.notes.title="This release";in.notes.lines=lines.data();in.notes.lineCount=int(lines.size());
        layout::Press(notes,{Kind::Open,int(View::WhatsNew),-1},0);
        const auto shownLines=[](const layout::Layout& l){int n=0;for(const auto& e:l.elements)if(e.role==layout::Role::NotesText&&e.text=="A release line")++n;return n;};
        const auto page2=layout::Build(notes,notes.context,in);
        const auto* nextButton=layout::Find(page2,next);const auto* previousButton=layout::Find(page2,previous);
        Check(shownLines(page2)==NotesPerPage&&nextButton&&previousButton&&nextButton->Has(layout::Enabled)&&previousButton->Has(layout::Enabled),
              "page 2 of 3: twelve lines, Previous and Next");
        Check(nextButton&&layout::HitTest(page2,nextButton->rect.x+10.f,nextButton->rect.y+10.f)==next,"Next is pressable where it is drawn");
        notes.notesPage=2;const auto last=layout::Build(notes,notes.context,in);
        Check(shownLines(last)==30-2*NotesPerPage&&layout::Find(last,next)&&!layout::Find(last,next)->Has(layout::Enabled),"the last page: the rest, Next grayed");
        Check(nextButton&&layout::Find(last,next)&&layout::Find(last,next)->rect==nextButton->rect,"Next stays put from page to page");
        notes.Open(none);Check(notes.notesPage==0,"reopening starts on page 1");
        notes.SetNotesPages(1);layout::Press(notes,{Kind::Open,int(View::WhatsNew),-1},0);layout::Press(notes,next,0);Check(notes.notesPage==0,"one page: nothing to turn");
        in.notes.lineCount=NotesPerPage;const auto one=layout::Build(notes,notes.context,in);
        Check(!layout::Find(one,next)&&shownLines(one)==NotesPerPage,"one page: every line, no Previous / Next");
        Check(NotesPages(0)==1&&NotesPages(12)==1&&NotesPages(13)==2&&NotesPages(NotesMaxLines)==6,"12 lines a page");
    }
    {   // Every row has help, short enough for its card.
        for(const auto& row:content::Menu().rows){const Key key=row.key;Check(HelpText(key)[0]&&HelpTitle(key)[0],"every row has help");}
        for(int id=0;id<=HelpLast;++id)Check(std::string(HelpText(id)).size()<450,"every help text is short enough for its card");
    }
    Snapshot s;s.ready=true;
    {Model effects;effects.Open(s);
        Check(s.values[ElementalHands]==0,"experimental hand effects default off");
        effects.Click(Step(ElementalHands));const auto request=effects.Click({Kind::Apply});
        Check(request.changed==(ChangeMask{1}<<ElementalHands)&&request.values[ElementalHands]==1,"the elemental toggle saves alone");
        auto external=request.base;external[Motion]=1;
        Check(!Conflict(external,request.base,request.changed),"its edit tolerates unrelated setting changes");
        external[ElementalHands]=1;Check(Conflict(external,request.base,request.changed),"its conflicts are detected");
    }
    {
        for(Key k:{StereoRendering,RenderResolution,DrawDistance,ObjectDistance,Foveation,FoveationStrength})
            Check(PageOf(k)==View::Performance,"everything that changes the frame rate is on the Performance page");
        for(Key k:{Sky,Fog,Ceiling,ElementalHands,BottlePhysics,PropPhysics,TorchFlame})
            Check(PageOf(k)==View::World,"the World page keeps the mod's effects, with bottle and prop physics and the torch flame");
        Check(!content::RowOf(DesktopOneEye)&&Defaults()[DesktopOneEye]==1&&std::wstring(Definitions[DesktopOneEye].key)==L"desktop_one_eye",
              "the desktop mirror is [render] desktop_one_eye in the INI only, on by default");
        Check(Defaults()[TorchFlame]==1&&std::wstring(Definitions[TorchFlame].section)==L"torch"&&std::wstring(Definitions[TorchFlame].key)==L"flame"&&
              HelpText(TorchFlame)[0]!=0,"the torch flame: on by default, [torch] flame, explained");
        Check(Defaults()[BottlePhysics]==1&&Defaults()[PropPhysics]==1,"both physics switches start on, as their INI defaults");
        Model strength;strength.Open(s);
        Check(strength.Get()[Foveation]==1&&strength.Get()[FoveationStrength]==1,"fixed foveation at Medium strength by default");
        strength.Click(Step(FoveationStrength));
        Check(strength.Get()[FoveationStrength]==2&&strength.Dirty(),"strength adjusts while foveation is selected");
        for(int i=0;i<5;++i)strength.Click(Step(FoveationStrength));
        Check(strength.Get()[FoveationStrength]==3,"Maximum is the strongest level");
        strength.Click(Step(Foveation,-1));Check(strength.Get()[Foveation]==0,"foveation can be turned off");
        Check(!RowEnabled(strength.Get(),FoveationStrength)&&RowEnabled(strength.Get(),Foveation),"Off disables only the strength row");
        strength.Click(Step(FoveationStrength,-1));strength.Click(Step(FoveationStrength,-1));
        Check(strength.Get()[FoveationStrength]==3,"strength ignores edits while foveation is off");
        strength.Click(Step(Foveation));strength.Click(Step(FoveationStrength,-1));
        Check(strength.Get()[FoveationStrength]==2,"strength edits resume when foveation is selected again");
        Check(Normalize(FoveationStrength,1.5f)==1&&Normalize(FoveationStrength,7)==1&&Normalize(FoveationStrength,-1)==1,"malformed strength shows Medium");
    }
    {
        Model stereo;stereo.Open(s);
        Check(stereo.Get()[StereoRendering]==1&&RowEnabled(stereo.Get(),StereoRendering),"By turns by default");
        // Two choices since Replay left: switched like the other two-choice rows
        stereo.Click(Step(StereoRendering));
        Check(stereo.Get()[StereoRendering]==0&&stereo.Dirty(),"a step switches to Both eyes");
        stereo.Click(Step(StereoRendering));
        Check(stereo.Get()[StereoRendering]==1&&!stereo.Dirty(),"and back to By turns");
        stereo.Click(Step(StereoRendering,-1));
        Check(stereo.Get()[StereoRendering]==0,"the other way to Both eyes as well");
        stereo.Click(Step(StereoRendering,-1));
        Check(stereo.Get()[StereoRendering]==1&&!stereo.Dirty(),"and back to By turns");
        Check(Normalize(StereoRendering,NAN)==1&&Normalize(StereoRendering,-3)==1&&Normalize(StereoRendering,3)==1&&Normalize(StereoRendering,1.5f)==1&&
              Normalize(StereoRendering,0)==0,"malformed stereo rendering goes by turns");
        Check(Normalize(StereoRendering,2)==1,"a profile saved with Replay goes by turns");
    }
    {   // The GPU wait not deferred: the Performance offer turns it on, once, in every mode
        Model defer;defer.Open(s);
        Check(DeferOffered(defer.Get())&&defer.Get()[DeferGpuWait]==0,"the deferred GPU wait is offered while it is off");
        defer.Click(Step(DeferGpuWait));
        Check(defer.Get()[DeferGpuWait]==1&&defer.Dirty()&&!DeferOffered(defer.Get()),"the offer turns it on; Apply saves it");
        defer.Click(Step(DeferGpuWait));
        Check(defer.Get()[DeferGpuWait]==1,"it never turns it off");
        for(int mode:{0}){
            Model other;other.Open(s);
            other.Click(Step(StereoRendering,-1));
            Check(other.Get()[StereoRendering]==mode&&DeferOffered(other.Get()),"offered with Both eyes too");
            other.Click(Step(DeferGpuWait));
            Check(other.Get()[DeferGpuWait]==1,"the offer turns it on in every mode");
        }
        Snapshot on=s;on.values[DeferGpuWait]=1;
        Model deferred;deferred.Open(on);deferred.Click(Step(DeferGpuWait));
        Check(!DeferOffered(deferred.Get())&&deferred.Get()[DeferGpuWait]==1&&!deferred.Dirty(),"no offer and no change once deferred");
    }
    {   // The game's own video options (its Settings.json): Vertical Sync off, FXAA
        const auto code=[](const char* json){return ParseGameVideo(json).Code();};
        Check(code("{\n    \"Anti Aliasing\": 1,\n    \"VSync\": 0\n}")==0&&!GameVideoWarning(ParseGameVideo("{\"Anti Aliasing\": 1, \"VSync\": 0}")),"FXAA without VSync is fine");
        Check(code("{\"Anti Aliasing\": 1, \"VSync\": 1}")==1,"VSync on is warned about");
        Check(code("{\"Anti Aliasing\": 3, \"VSync\": 0}")==2&&code("{\"Anti Aliasing\": 2}")==2&&code("{\"Anti Aliasing\":0}")==2,"every anti-aliasing but FXAA is warned about");
        Check(code("{\"Anti Aliasing\" :\r\n 2,\"VSync\"\t: 1}")==3,"spacing around the colon");
        Check(code("{}")==0&&code("")==0&&code("{\"VSync Mode\": 1, \"My Anti Aliasing\": 3, \"VSync\": \"x\"}")==0,"missing, other or malformed keys are not warned about");
        {   // ...and what the Perf page shows of them: how much the game draws
            const auto game=ParseGameVideo(R"({"Shadow Quality": 4, "Character Detail": 5, "Ambient Occlusion Quality": 2, "Atmospherics Quality": 3,)"
                                           R"( "Light Quality": 2, "Texture Quality": 4, "Resolution Scale": 100, "NVIDIA DLSS": 0})");
            Check(game.shadows==4&&game.characterDetail==5&&game.ambientOcclusion==2&&game.atmospherics==3&&game.lights==2&&
                  game.resolutionScale==100&&game.dlss==0,"the game's draw options are read");
            Check(GameVideoLine(game)=="Game: Shadows 4 \xc2\xb7 Character detail 5 \xc2\xb7 Lights 2 \xc2\xb7 AO 2 \xc2\xb7 Atmospherics 3 \xc2\xb7 Scale 100% \xc2\xb7 DLSS off",
                  "one line of them, in the game's own numbers: the CPU's first");
            Check(GameVideoLine(ParseGameVideo(R"({"Shadow Quality": 1})"))=="Game: Shadows 1"&&GameVideoLine(ParseGameVideo("{}")).empty(),
                  "only the options the file has");
            Check(ParseGameVideo("{}").shadows==-1&&!(ParseGameVideo("{}")==game),"absent options are -1 and a change repaints");
        }
        Check(std::string(GameVideoWarning({true,true}))=="Game Options > Video: turn Vertical Sync off and set Anti-Aliasing to FXAA"&&
              std::string(GameVideoWarning({true,false})).find("Vertical Sync off")!=std::string::npos&&
              std::string(GameVideoWarning({false,true})).find("FXAA")!=std::string::npos,"each warning names the game's option");
        const std::wstring saved=L"C:\\Users\\p\\Saved Games";
        Check(ModSettingsFile(saved,L"C:\\steam\\steamapps\\common\\Diablo II Resurrected\\mods\\pcvr-dev\\d2rloader\\plugins\\d2rl-pcvr.dll")==
              L"C:\\Users\\p\\Saved Games\\Diablo II Resurrected\\mods\\pcvr-dev\\Settings.json","a mod's game settings are its own");
        Check(ModSettingsFile(saved,L"D:/Games/D2R/MODS/D2R-PCVR/D2RLoader/Plugins/d2rl-pcvr.dll")==
              L"C:\\Users\\p\\Saved Games\\Diablo II Resurrected\\mods\\D2R-PCVR\\Settings.json","any case and slashes; the mod's name as spelled");
        Check(ModSettingsFile(saved,L"C:\\D2R\\d2rloader\\plugins\\d2rl-pcvr.dll").empty()&&ModSettingsFile(saved,L"C:\\D2R\\mods\\x\\other\\a.dll").empty()&&
              ModSettingsFile(L"",L"C:\\D2R\\mods\\x\\d2rloader\\plugins\\a.dll").empty(),"no mod folder: the unmodded game's file");
    }
    {   // The Perf page's numbers: the last second's frames against the headset's frame
        PerformanceReadout r{true,true,52,11.1f,1.0f,7.0f,9.4f,1.5f};
        Check(std::abs(r.CpuMs()-11.9f)<.001f,"the CPU's part: game, drawing and the mod's own work, not the headset wait");
        Check(PerformanceLine(r)=="Now: 52 fps \xc2\xb7 drawing 9.4 ms (one eye) \xc2\xb7 game 1.0 \xc2\xb7 VR 1.5 \xc2\xb7 headset wait 7.0","the readout line");
        Check(PerformanceVerdict(r).find("11.9 ms of the headset's 11.1 ms frame: draw less")!=std::string::npos&&PerformanceVerdict(r).find("resolution won't help")!=std::string::npos,
              "past the headset's frame: draw less");
        r.drawMs=7;r.byTurns=false;
        Check(PerformanceVerdict(r)=="CPU 9.5 ms of the headset's 11.1 ms frame: it fits"&&PerformanceLine(r).find("(both eyes)")!=std::string::npos,"within it; both eyes named");
        Check(PerformanceLine({}).find("waiting")!=std::string::npos&&PerformanceVerdict({}).empty(),"no numbers before a second of frames");
        r.mixed=true;Check(PerformanceLine(r).find("(mixed)")!=std::string::npos,"a second of turns and pairs says so");r.mixed=false;
        Check(HelpText(HelpPerformance)[0]&&HelpTitle(HelpPerformance)[0],"and the readout's help reads");
    }
    {   // ReShade depth: d2r_pcvr.ini [render] reshade_depth (not in VR Settings), set in ReShade.ini before ReShade loads
        namespace addons=framevr::reshadeaddons;
        Check(!addons::Disabled(L"",addons::GenericDepth)&&addons::Disabled(L"Generic Depth",addons::GenericDepth)&&
              addons::Disabled(L"Effect Runtime Sync, generic depth ",addons::GenericDepth)&&!addons::Disabled(L"Generic Depth Plus",addons::GenericDepth),
              "the add-on is found by its whole name, any case or spacing");
        Check(addons::WithEnabled(L"",addons::GenericDepth,false)==L"Generic Depth"&&addons::WithEnabled(L"Generic Depth",addons::GenericDepth,true).empty(),
              "off lists it, on removes it");
        Check(addons::WithEnabled(L"A, Generic Depth ,B",addons::GenericDepth,true)==L"A,B"&&addons::WithEnabled(L"A,B",addons::GenericDepth,false)==L"A,B,Generic Depth"&&
              addons::WithEnabled(L"Generic Depth,A,generic depth",addons::GenericDepth,false)==L"A,Generic Depth","other add-ons keep their place; listed once");
    }
    {   // Map view and Rotate map: their drafts
        Model map;map.Open(s);
        const Hit view{Kind::Change,MapTopDown,1},rotate{Kind::Change,MapRotate,1};
        Check(s.values[MapTopDown]==0&&s.values[MapRotate]==0,"map defaults remain isometric and north-up");
        map.Click(view);map.Click(rotate);
        Check(map.Dirty()&&map.Get()[MapTopDown]==1&&map.Get()[MapRotate]==1&&s.values[MapTopDown]==0,"map changes remain drafts");
        map.Click({Kind::Close});map.Open(s);
        Check(!map.Dirty()&&map.Get()[MapTopDown]==0&&map.Get()[MapRotate]==0,"Close discards unapplied map changes");
        map.Click(rotate);map.Click(view);
        const auto request=map.Click({Kind::Apply});
        Check(request.changed==((ChangeMask{1}<<MapTopDown)|(ChangeMask{1}<<MapRotate))&&
              request.values[MapTopDown]==1&&request.values[MapRotate]==1,"Apply saves both map choices without changing other settings");
        map.Click(view);Check(map.Get()[MapTopDown]==1,"map cannot change while saving");
        auto saved=s;saved.values=request.values;saved.sequence=request.sequence;saved.result=Result::Saved;
        map.Observe(saved);map.Close();map.Open(saved);
        Check(!map.Dirty()&&map.Get()[MapTopDown]==1&&map.Get()[MapRotate]==1,"reopening retains saved map choices");
    }
    {
        Check(s.values[LootLabelSize]==45,"loot labels retain original shipped size by default");
        Check(Normalize(LootLabelSize,0)==20&&Normalize(LootLabelSize,200)==150&&
              Normalize(LootLabelSize,NAN)==45&&Normalize(LootLabelSize,47.5f)==47.5f,"loot scale bounds, invalid default and custom values");
        Model loot;loot.Open(s);
        loot.Click(Step(LootLabelSize));Check(loot.Get()[LootLabelSize]==50,"loot size increases by five percentage points");
        auto request=loot.Click({Kind::Apply});
        Check(request.changed==(ChangeMask{1}<<LootLabelSize)&&request.values[LootLabelSize]==50,"loot Apply uses its own high mask bit");
        loot.Open(s);loot.Click(Step(LootLabelSize,-1));Check(loot.Get()[LootLabelSize]==40,"loot size decreases by five percentage points");
        loot.Click({Kind::Close});loot.Open(s);Check(loot.Get()[LootLabelSize]==45&&!loot.Dirty(),"Close discards unsaved loot size");
    }
    for(int i=HapticStrength;i<=HapticAlert;++i){
        const auto key=Key(i);
        Check(s.values[key]==100,"haptics default to normal strength");
        Check(Normalize(key,-1)==0&&Normalize(key,250)==200&&Normalize(key,54)==50&&Normalize(key,NAN)==100,"haptic settings clamp, step and reject nonfinite values");
        Check(PageOf(key)==View::Vibration,"each haptic row is on the Vibration page");
        Model h;h.Open(s);h.view=View::Vibration;
        h.Click(Step(key,-1));Check(h.Get()[key]==90,"strength moves in ten-percent steps");
        if(i!=HapticStrength){
            const auto category=framevr::haptics::Category(i-HapticMelee);
            for(int choice=0;choice<framevr::haptics::PreviewCount(category);++choice){
                const Hit test{Kind::TestHaptics,i-HapticMelee,choice+1};
                Check(h.CanTestHaptics(test.index),"category Test selects its action preview with unsaved settings");
                Check(!h.Click(test).sequence&&h.Dirty()&&h.Get()[key]==90,"Test does not persist draft changes");
            }
        }
        const auto request=h.Click({Kind::Apply});
        Check(request.changed==(ChangeMask{1}<<key)&&request.values[key]==90&&!h.CanTestHaptics(0),"Apply saves only selected haptic strength and blocks preview while saving");
        h.Close();h.Open(s);Check(h.Get()[key]==100&&!h.CanTestHaptics(0),"close discards edits and the opening page has no preview");
    }
    Check(!Model{}.CanTestHaptics(0),"closed menu cannot preview haptics");
    {Model h;h.Open(s);h.view=View::Vibration;
        Check(s.values[HapticShortMelee]==0&&PageOf(HapticShortMelee)==View::Vibration,"default longer melee pulse has its own toggle");
        const auto normal=HapticSample(h.Get(),framevr::haptics::Category::Melee);
        Check(normal.milliseconds==90&&normal.frequency==0&&HapticSample(h.Get(),framevr::haptics::Category::Melee,1).milliseconds==45,"weapon impact lasts longer through a swing while shield keeps its existing tap");
        h.Click(Step(HapticShortMelee));const auto sharp=HapticSample(h.Get(),framevr::haptics::Category::Melee);
        Check(sharp.milliseconds==10&&sharp.frequency==180&&sharp.amplitude==normal.amplitude,"draft test immediately uses short pulse at the same strength");
        const auto request=h.Click({Kind::Apply});
        Check(request.changed==(ChangeMask{1}<<HapticShortMelee)&&request.values[HapticMelee]==100,"pulse shape saves without changing strength");
        h.Close();h.Open(s);Check(HapticSample(h.Get(),framevr::haptics::Category::Melee).milliseconds==90,"closing discards unsaved short-pulse choice");
    }
    {auto missing=s;missing.ready=false;Model h;h.Open(missing);h.view=View::Vibration;
        Check(!h.CanTestHaptics(0)&&!h.CanTestHaptics(-1)&&!h.CanTestHaptics(framevr::haptics::CategoryCount),"unavailable settings cannot preview");}
    {auto values=s.values;values[HapticStrength]=50;
        for(int i=0;i<framevr::haptics::CategoryCount;++i)values[HapticMelee+i]=float(i*10);
        const auto levels=HapticLevels(values);Check(levels.master==50,"master level reaches runtime configuration");
        for(int i=0;i<framevr::haptics::CategoryCount;++i)Check(levels.category[i]==i*10,"each saved category maps to its runtime channel");}
    Check(s.values[MeleeHitSpeed]==12,"melee defaults to twelve m/s");
    {Model melee;melee.Open(s);
        melee.Click(Step(MeleeHitSpeed));Check(melee.Get()[MeleeHitSpeed]==12.25f,"melee speed quarter step");
        const auto request=melee.Click({Kind::Apply});
        Check(request.changed==(1u<<MeleeHitSpeed),"melee mask bit saves only melee speed");
        Check(Normalize(MeleeHitSpeed,NAN)==12&&Normalize(MeleeHitSpeed,0)==1.5f&&Normalize(MeleeHitSpeed,99)==99,"melee speed UI has no upper cap");
        s.values[MeleeHitSpeed]=6;melee.Open(s);melee.Click(Step(MeleeHitSpeed));
        Check(melee.Get()[MeleeHitSpeed]==6.25f,"plus button goes beyond old cap");
        s.values[MeleeHitSpeed]=12;
    }
    {   // Melee is Unleashed only: no Melee rules setting or row
        bool rulesKey=false;
        for(const auto& d:Definitions)rulesKey|=std::wstring(d.key)==L"melee_rules";
        Check(!rulesKey&&PageOf(MeleeHitSpeed)==View::Combat,"swing speed stays on the Combat page; there are no melee rules to choose");
    }
    {Model thrust;thrust.Open(s);
        Check(s.values[MeleeThrustSpeed]==2&&PageOf(MeleeThrustSpeed)==View::Combat,"thrust has its own Combat row and two m/s default");
        thrust.Click(Step(MeleeThrustSpeed));Check(thrust.Get()[MeleeThrustSpeed]==2.25f,"thrust speed adjusts in quarter steps");
        const auto request=thrust.Click({Kind::Apply});
        Check(request.changed==(ChangeMask{1}<<MeleeThrustSpeed),"thrust edit does not change swing speed");
        Check(Normalize(MeleeThrustSpeed,NAN)==2&&Normalize(MeleeThrustSpeed,0)==.75f&&Normalize(MeleeThrustSpeed,99)==10,"thrust speed has finite usable bounds");
    }
    {Model strain;strain.Open(s);
        const auto* row=content::RowOf(StrainVignette);
        Check(PageOf(StrainVignette)==View::Shield&&row&&row->kind==content::RowKind::Toggle,"the guard strain shadow is one Off / On row on the Shield page");
        Check(s.values[StrainVignette]==1&&Definitions[StrainVignette].max==1,"the shadow is On by default, On / Off only");
        layout::Press(strain,{Kind::Set,StrainVignette,0,0},0);
        Check(strain.Get()[StrainVignette]==0,"its toggle turns the shadow Off");
        layout::Press(strain,{Kind::Set,StrainVignette,0,1},0);
        Check(strain.Get()[StrainVignette]==1,"and back On");
        strain.Click(Step(StrainVignette,-1));
        Check(strain.Get()[StrainVignette]==0,"a step also switches it");
        const auto request=strain.Click({Kind::Apply});
        Check(request.changed==(ChangeMask{1}<<StrainVignette),"the shadow switch saves only its own setting");
    }
    {Model shield;shield.Open(s);
        for(Key k:{ShieldGrip,ShieldOut,ShieldUp,ShieldFollow,ShieldRoll})Check(PageOf(k)==View::Shield,"the Shield page holds grip, out, up, follows and roll");
        Check(Definitions[ShieldFollow].section==std::wstring_view(L"gameplay")&&Definitions[ShieldFollow].key==std::wstring_view(L"shield_follow_pct")&&
              Definitions[ShieldRoll].section==std::wstring_view(L"gameplay")&&Definitions[ShieldRoll].key==std::wstring_view(L"shield_roll_deg"),"follows and roll keep their INI keys");
        Check(s.values[ShieldFollow]==skel::shield::kFollowDefaultPct&&s.values[ShieldRoll]==0&&Definitions[ShieldFollow].defaultValue==skel::shield::FollowSetting(NAN)&&
              Definitions[ShieldRoll].defaultValue==skel::shield::RollSetting(NAN),"follows 50% and roll 0 by default, as the pose reads an unset value");
        Check(Definitions[ShieldFollow].min==0&&Definitions[ShieldFollow].max==skel::shield::kFollowMaxPct&&Definitions[ShieldFollow].step==skel::shield::kFollowStepPct&&
              Definitions[ShieldRoll].min==-skel::shield::kRollMaxDeg&&Definitions[ShieldRoll].max==skel::shield::kRollMaxDeg&&Definitions[ShieldRoll].step==skel::shield::kRollStepDeg,
              "follows and roll span the pose's range in its own steps");
        for(float v:{-500.f,-181.f,-93.f,-7.f,-1.f,0.f,2.f,3.f,34.f,35.f,92.f,93.f,101.f,179.f,500.f}){
            Check(Normalize(ShieldFollow,v)==skel::shield::FollowSetting(v),"Shield follows normalizes as the pose snaps it");
            Check(Normalize(ShieldRoll,v)==skel::shield::RollSetting(v),"Shield roll normalizes as the pose snaps it");
        }
        Check(Normalize(ShieldFollow,NAN)==50&&Normalize(ShieldRoll,NAN)==0&&!std::signbit(Normalize(ShieldRoll,-1)),"unreadable values fall back; no -0 degrees");
        {Model turn;turn.Open(s);
            turn.Click(Step(ShieldFollow));turn.Click(Step(ShieldRoll,-1));
            Check(turn.Get()[ShieldFollow]==60&&turn.Get()[ShieldRoll]==-5,"follows steps 10%, roll 5 degrees");
            for(int i=0;i<20;++i)turn.Click(Step(ShieldFollow));
            for(int i=0;i<50;++i)turn.Click(Step(ShieldRoll,-1));
            Check(turn.Get()[ShieldFollow]==100&&turn.Get()[ShieldRoll]==-180,"follows stops at 100% (Wrist), roll at -180");
            for(int i=0;i<20;++i)turn.Click(Step(ShieldFollow,-1));
            for(int i=0;i<80;++i)turn.Click(Step(ShieldRoll));
            Check(turn.Get()[ShieldFollow]==0&&turn.Get()[ShieldRoll]==180,"follows stops at 0% (Forearm), roll at +180");
            const auto request=turn.Click({Kind::Apply});
            Check(request.changed==((ChangeMask{1}<<ShieldFollow)|(ChangeMask{1}<<ShieldRoll))&&request.values[ShieldFollow]==0&&request.values[ShieldRoll]==180,
                  "follows and roll save through their own mask bits, nothing else");
        }
        Check(s.values[ShieldGrip]==0&&s.values[ShieldOut]==0&&s.values[ShieldUp]==0,"the shield keeps its original position (and blocking surface) by default");
        shield.Click(Step(ShieldGrip));
        for(int i=0;i<3;++i)shield.Click(Step(ShieldOut,-1));
        Check(shield.Get()[ShieldGrip]==1&&shield.Get()[ShieldOut]==-3,"grip steps up by a centimeter; out goes below zero");
        for(int i=0;i<50;++i)shield.Click(Step(ShieldGrip));
        Check(shield.Get()[ShieldGrip]==40,"grip stops at 40 cm, room for large shields");
        for(int i=0;i<40;++i)shield.Click(Step(ShieldOut,-1));
        Check(shield.Get()[ShieldOut]==-30,"out stops at -30 cm");
        Check(Normalize(ShieldGrip,-20)==-10&&Normalize(ShieldUp,45)==30&&Normalize(ShieldUp,NAN)==0,"shield offsets clamp to their ranges");
        Check(Definitions[ShieldGrip].min==skel::shield::kGripMinCm&&Definitions[ShieldGrip].max==skel::shield::kGripMaxCm&&Definitions[ShieldOut].min==-skel::shield::kSideMaxCm&&
              Definitions[ShieldOut].max==skel::shield::kSideMaxCm&&Definitions[ShieldUp].min==-skel::shield::kSideMaxCm&&Definitions[ShieldUp].max==skel::shield::kSideMaxCm&&
              Definitions[ShieldGrip].step==1&&Definitions[ShieldOut].step==1&&Definitions[ShieldUp].step==1,"the Shield rows span what the pose accepts, 1 cm a step");
        const auto request=shield.Click({Kind::Apply});
        Check(request.changed==((ChangeMask{1}<<ShieldGrip)|(ChangeMask{1}<<ShieldOut)),"shield edits change nothing else");
    }
    {Model laser;laser.Open(s);
        const auto& d=Definitions[BowLaser];
        Check(s.values[BowLaser]==LaserOff&&!LaserOn(s.values),"the bow path laser is Off by default");
        Check(std::wstring(d.section)==L"bow"&&std::wstring(d.key)==L"path_laser_hue"&&d.step==15,"one [bow] path_laser_hue value in 15-degree steps");
        laser.Click(Step(BowLaser));Check(laser.Get()[BowLaser]==0&&LaserOn(laser.Get()),"plus from Off is the first color, red 0 degrees");
        laser.Click(Step(BowLaser));Check(laser.Get()[BowLaser]==15,"plus steps 15 degrees");
        for(int i=0;i<22;++i)laser.Click(Step(BowLaser));
        Check(laser.Get()[BowLaser]==345,"the last color is 345 degrees");
        laser.Click(Step(BowLaser));Check(laser.Get()[BowLaser]==LaserOff&&!LaserOn(laser.Get()),"past the last color comes Off");
        laser.Click(Step(BowLaser));Check(laser.Get()[BowLaser]==0,"then the first color");
        laser.Click(Step(BowLaser,-1));Check(laser.Get()[BowLaser]==LaserOff,"back from the first color is Off");
        laser.Click(Step(BowLaser,-1));Check(laser.Get()[BowLaser]==345,"and back from Off the last color");
        for(int i=0;i<25;++i)laser.Click(Step(BowLaser));
        Check(laser.Get()[BowLaser]==345,"Off and 24 colors make a whole turn");
        const auto save=laser.Click({Kind::Apply});
        Check(save.changed==(ChangeMask{1}<<BowLaser)&&save.values[BowLaser]==345,"Apply saves only the laser");
        Check(Normalize(BowLaser,-1)==LaserOff&&Normalize(BowLaser,-200)==LaserOff&&Normalize(BowLaser,400)==345&&Normalize(BowLaser,NAN)==LaserOff,
              "any negative saved value is Off; others stay on the wheel");}
    {Model aim;aim.Open(s);
        // The defaults (outline Off, Ground skills Game) leave out the rows that cannot apply.
        Check(RowShown(SpellLock,s.values,{})&&RowShown(StaffPoint,s.values,{})&&RowShown(TargetHighlight,s.values,{})&&RowShown(GroundAim,s.values,{})&&
              RowShown(BowLaser,s.values,{})&&RowShown(ThrowLift,s.values,{})&&!RowShown(AimHighlights,s.values,{})&&!RowShown(HighlightLeft,s.values,{})&&
              !RowShown(LandingCircles,s.values,{})&&!RowShown(AimFollowsView,s.values,{}),
              "by default: the aimed monster, the staff, the outline, ground skills, the laser, the throws");
        Check(s.values[ThrowLift]==10&&std::wstring(Definitions[ThrowLift].key)==L"throw_lift_deg"&&Definitions[ThrowLift].min==-10&&
              Definitions[ThrowLift].max==30&&Definitions[ThrowLift].step==5,"[gameplay] throw_lift_deg: 10 degrees up by default, -10 to 30 in 5s");
        auto lit=s.values;lit[TargetHighlight]=50;lit[GroundAim]=1;
        Check(RowShown(AimHighlights,lit,{})&&RowShown(HighlightLeft,lit,{})&&RowShown(HighlightRight,lit,{})&&RowShown(LandingCircles,lit,{})&&RowShown(AimFollowsView,lit,{}),
              "the outline brings its hands and colors and Aim follows your view, a placed ground skill its circles");
        // Every option that changes how a cast aims is Off by default: the casts aim as before the aim rules (#159 review).
        Check(s.values[SpellLock]==0&&s.values[StaffPoint]==0&&s.values[GroundAim]==0&&s.values[LandingCircles]==1,
              "aim options Off (Game) by default; circles On for when they apply");
        Check(std::wstring(Definitions[GroundAim].key)==L"ground_aim"&&Definitions[GroundAim].max==2,"one [gameplay] ground_aim row: Game, Where you point, Aimed monster");
        {auto off=s.values;off[TargetHighlight]=0;
            Check(RowEnabled(off,GroundAim)&&!RowEnabled(off,LandingCircles)&&!RowShown(LandingCircles,off,{}),"no circles while the game aims ground skills");
            off[GroundAim]=1;Check(RowEnabled(off,LandingCircles)&&RowShown(LandingCircles,off,{}),"and back once the mod places them");}
        {Model ground;ground.Open(s);
            ground.Click(Step(GroundAim));Check(ground.Get()[GroundAim]==1&&RowShown(LandingCircles,ground.Get(),{}),"plus: Where you point, and its circles row");
            ground.Click(Step(GroundAim));Check(ground.Get()[GroundAim]==2,"then Aimed monster");
            ground.Click(Step(GroundAim));Check(ground.Get()[GroundAim]==0&&!RowShown(LandingCircles,ground.Get(),{}),"and round to Game, without circles");
            ground.Click(Step(GroundAim,-1));Check(ground.Get()[GroundAim]==2,"minus from Game: Aimed monster");
            Check(ground.Click({Kind::Apply}).changed==(ChangeMask{1}<<GroundAim),"Apply saves only the ground row");
            Check(Normalize(GroundAim,1.5f)==0&&Normalize(GroundAim,3)==0&&Normalize(GroundAim,NAN)==0&&Normalize(GroundAim,2)==2,"a malformed ground_aim reads as Game");}
        Check(s.values[TargetHighlight]==0,"the monster outline is Off by default");
        Check(s.values[AimHighlights]==1&&s.values[HighlightRight]==45&&s.values[HighlightLeft]==210,"both hands; gold right, blue left by default");
        Check(Normalize(HighlightLeft,200)==195&&Normalize(HighlightLeft,203)==210&&Normalize(HighlightRight,-5)==0&&Normalize(HighlightRight,400)==360&&Normalize(HighlightRight,NAN)==45,"colors snap to 15 degrees, 0 (By range) to 360");
        Check(!RowShown(AimHighlights,s.values,{})&&!RowShown(HighlightLeft,s.values,{}),"the outline's rows are left out while it is Off");
        {Model colors;auto on=s;on.values[TargetHighlight]=50;colors.Open(on);
            colors.Click(Step(AimHighlights));Check(colors.Get()[AimHighlights]==0,"a step switches the highlights to Dominant hand");
            colors.Click(Step(AimHighlights));Check(colors.Get()[AimHighlights]==1,"and back to Both hands");
            for(int i=0;i<30;++i)colors.Click(Step(HighlightRight));
            Check(colors.Get()[HighlightRight]==360&&colors.Get()[HighlightLeft]==210,"the right hand's steps turn its wheel to 360 degrees, alone");
            for(int i=0;i<30;++i)colors.Click(Step(HighlightRight,-1));
            Check(colors.Get()[HighlightRight]==0,"and back to By range");
            colors.Click(Step(HighlightRight));Check(colors.Get()[HighlightRight]==15,"the first hue past By range");
            colors.Click(Step(HighlightLeft));Check(colors.Get()[HighlightLeft]==225,"the left hand's steps are the left hand's");
            float rgb[3];HueRgb(45,rgb);Check(rgb[0]==1&&rgb[1]==.75f&&rgb[2]==0,"45 degrees is gold");}
        aim.Click(Step(TargetHighlight));Check(aim.Get()[TargetHighlight]==10&&RowShown(AimHighlights,aim.Get(),{}),"plus turns the outline on at 10%, with its rows");
        aim.Click(Step(TargetHighlight,-1));aim.Click(Step(TargetHighlight,-1));Check(aim.Get()[TargetHighlight]==0,"Off is the lower bound");
        aim.Click(Step(TargetHighlight));aim.Click(Step(TargetHighlight));
        const auto request=aim.Click({Kind::Apply});
        Check(request.changed==(ChangeMask{1}<<TargetHighlight)&&request.values[TargetHighlight]==20,"the outline strength changes nothing else");
    }
    {   // Skills go to the aimed monster: one row for spells and bow skills
        Check(std::wstring(Definitions[SpellLock].key)==L"spell_lock"&&Definitions[SpellLock].max==3,"spell_lock: Off, Spells, Bow skills, Both");
        Check(!LockSpells(0)&&LockSpells(1)&&!LockBowSkills(1)&&LockBowSkills(2)&&LockSpells(3)&&LockBowSkills(3),"bit 1 spells, bit 2 bow skills");
        Check(Normalize(SpellLock,4)==0&&Normalize(SpellLock,1.5f)==0&&Normalize(SpellLock,NAN)==0&&Normalize(SpellLock,3)==3,"a malformed spell_lock is Off");
        Model amazon;amazon.Open(s);amazon.SetContext({heroclass::Amazon});
        for(float want:{1.f,2.f,3.f,0.f}){amazon.Click(Step(SpellLock));Check(amazon.Get()[SpellLock]==want,"an Amazon steps Off, Spells, Bow skills, Both");}
        amazon.Click(Step(SpellLock,-1));Check(amazon.Get()[SpellLock]==3,"minus from Off: Both");
        const auto* row=content::RowOf(SpellLock);
        Check(row&&std::strcmp(content::LabelFor(*row,amazon.context),"Spells go to the aimed monster")!=0,"an Amazon's row: skills");
        auto bows=s;bows.values[SpellLock]=2;
        Model sorc;sorc.Open(bows);sorc.SetContext({heroclass::Sorceress});
        Check(std::strcmp(content::LabelFor(*row,sorc.context),"Spells go to the aimed monster")==0,"a class without bow skills sees its spells only");
        sorc.Click(Step(SpellLock));Check(sorc.Get()[SpellLock]==3,"its plus turns spells on and keeps the Amazon's bow skills");
        sorc.Click(Step(SpellLock));Check(sorc.Get()[SpellLock]==2,"and the next step off again");
    }
    {   // Aim follows your view: one On/Off row, Off by default, shown once something takes the aimed monster
        const auto& d=Definitions[AimFollowsView];
        Check(std::wstring(d.section)==L"gameplay"&&std::wstring(d.key)==L"aim_follows_view"&&d.defaultValue==0&&d.min==0&&d.max==1&&d.step==1&&
              Defaults()[AimFollowsView]==0&&std::strcmp(d.label,"Aim follows your view")==0,"[gameplay] aim_follows_view: On or Off, Off by default");
        auto v=Defaults();
        Check(!RowShown(AimFollowsView,v,{}),"left out while nothing takes the aimed monster");
        v[GroundAim]=1;Check(!RowShown(AimFollowsView,v,{}),"Ground skills on Where you point take no monster");
        for(const Key k:{TargetHighlight,SpellLock,GroundAim}){
            auto w=Defaults();w[k]=k==GroundAim?2.f:k==TargetHighlight?50.f:1.f;
            Check(RowShown(AimFollowsView,w,{}),"the outline, Skills go to the aimed monster or Ground skills on Aimed monster bring it");
        }
        Check(std::string(HelpText(AimFollowsView)).size()<450&&std::strstr(HelpText(AimFollowsView),"17 degrees")&&std::strstr(HelpText(AimFollowsView),"Off by default"),
              "its help names the tunnel and fits the card");
        Model m;auto on=s;on.values[SpellLock]=1;m.Open(on);
        m.Click(Step(AimFollowsView));Check(m.Get()[AimFollowsView]==1,"its toggle turns it On");
        Check(m.Click({Kind::Apply}).changed==(ChangeMask{1}<<AimFollowsView),"Apply saves only it");
    }
    {   // Rows follow the character: another class's or weapon's are left out; unknown (the menus) shows them all
        const auto v=Defaults();
        Check(RowShown(ElementalHands,v,{})&&RowShown(ElementalHands,v,{heroclass::Sorceress})&&!RowShown(ElementalHands,v,{heroclass::Amazon}),"elemental hands: the Sorceress's");
        for(int k:{int(heroclass::Sorceress),int(heroclass::Assassin),int(heroclass::Druid),int(heroclass::Necromancer)})
            Check(RowShown(GroundAim,v,{k}),"ground skills for the classes that have them");
        for(int k:{int(heroclass::Amazon),int(heroclass::Barbarian),int(heroclass::Paladin)})Check(!RowShown(GroundAim,v,{k}),"none for those that do not");
        auto placed=v;placed[GroundAim]=1;
        Check(!RowShown(LandingCircles,placed,{heroclass::Paladin})&&RowShown(LandingCircles,placed,{heroclass::Druid}),"circles go with them");
        Check(RowShown(BowLaser,v,{heroclass::Amazon})&&!RowShown(BowLaser,v,{heroclass::Paladin})&&RowShown(BowLaser,v,{heroclass::Paladin,true}),
              "the bow laser: an Amazon's, or any bow in hand");
        Check(RowShown(StaffPoint,v,{heroclass::Druid})&&!RowShown(StaffPoint,v,{heroclass::Assassin})&&RowShown(StaffPoint,v,{heroclass::Assassin,false,true}),
              "staff aim: the Sorceress's and Druid's, or a staff in hand");
        const Context barbarian{heroclass::Barbarian};
        Check(RowShown(SpellLock,v,barbarian)&&RowShown(TargetHighlight,v,barbarian)&&RowShown(ThrowLift,v,barbarian)&&!RowShown(StaffPoint,v,barbarian)&&
              !RowShown(GroundAim,v,barbarian)&&!RowShown(BowLaser,v,barbarian),"a Barbarian: the aimed monster, the outline and the throws");
        Check(RowShown(ThrowLift,v,{heroclass::Amazon})&&RowShown(ThrowLift,v,{heroclass::Assassin})&&!RowShown(ThrowLift,v,{heroclass::Sorceress})&&
              RowShown(ThrowLift,v,{heroclass::Sorceress,false,false,true}),"throw lift: the Amazon's, Barbarian's and Assassin's (traps), or a thrown weapon in hand");
        Model druid;druid.Open(s);druid.SetContext({heroclass::Druid});
        Check(RowShown(StaffPoint,druid.Get(),druid.context)&&RowShown(GroundAim,druid.Get(),druid.context)&&!RowShown(BowLaser,druid.Get(),druid.context),
              "a Druid: staff aim and ground skills, no laser");
        const auto revision=druid.revision;druid.SetContext({heroclass::Druid});Check(druid.revision==revision,"the same character repaints nothing");
        druid.SetContext({heroclass::Druid,true});Check(druid.revision!=revision&&RowShown(BowLaser,druid.Get(),druid.context),"a bow in hand brings the laser");
    }
    {   // Rows that only matter with another row's choice
        auto v=Defaults();
        Check(RowShown(Snap,v,{})&&!RowShown(TurnSpeed,v,{}),"snap turning: its angle, not the smooth speed");
        v[SmoothTurn]=1;Check(!RowShown(Snap,v,{})&&RowShown(TurnSpeed,v,{}),"smooth turning: its speed");
        v[Seated]=1;Check(!RowShown(Waist,v,{}),"seated: no standing belt drop");
        v[Foveation]=0;Check(!RowShown(FoveationStrength,v,{}),"no foveation strength while it is Off");
        v[BloodVisor]=0;Check(!RowShown(BloodWipe,v,{}),"no face wipe without blood");
    }
    Check(std::string(HelpTitle(HelpMonoAudio))=="Windows Mono audio"&&std::string(HelpText(HelpMonoAudio)).find("Accessibility > Audio")!=std::string::npos,
          "the Mono audio help says where to turn it off");
    {Model heart;heart.Open(s);
        Check(s.values[HeartbeatStart]==60,"heartbeat start is 60% by default");
        Check(Normalize(HeartbeatStart,NAN)==60&&Normalize(HeartbeatStart,45)==50&&Normalize(HeartbeatStart,-5)==0&&Normalize(HeartbeatStart,100)==90,"heartbeat start snaps to 10% steps, 0-90");
        heart.Click(Step(HeartbeatStart,-1));Check(heart.Get()[HeartbeatStart]==50,"minus lowers the start by 10%");
        for(int i=0;i<6;++i)heart.Click(Step(HeartbeatStart,-1));
        Check(heart.Get()[HeartbeatStart]==0,"Off is the lower bound");
        for(int i=0;i<10;++i)heart.Click(Step(HeartbeatStart));
        Check(heart.Get()[HeartbeatStart]==90,"90% is the upper bound");
        Check(heart.Click({Kind::Apply}).changed==(ChangeMask{1}<<HeartbeatStart),"heartbeat start saves only its own setting");
    }
    {Model sight;sight.Open(s);
        Check(s.values[EnemySight]==100,"enemy sight distance is 100% by default");
        Check(Normalize(EnemySight,NAN)==100&&Normalize(EnemySight,145)==150&&Normalize(EnemySight,50)==100&&Normalize(EnemySight,350)==300,"enemy sight snaps to 10% steps, 100-300");
        sight.Click(Step(EnemySight,-1));Check(sight.Get()[EnemySight]==100,"100% is the lower bound");
        sight.Click(Step(EnemySight));Check(sight.Get()[EnemySight]==110,"plus raises enemy sight by 10%");
        for(int i=0;i<25;++i)sight.Click(Step(EnemySight));
        Check(sight.Get()[EnemySight]==300,"300% is the upper bound");
        Check(sight.Click({Kind::Apply}).changed==(ChangeMask{1}<<EnemySight),"enemy sight saves only its own setting");
    }
    {Model companion;companion.Open(s);
        Check(s.values[CompanionAi]==1,"companion AI is Enabled by default");
        Check(Normalize(CompanionAi,NAN)==1&&Normalize(CompanionAi,0)==0&&Normalize(CompanionAi,1)==1&&Normalize(CompanionAi,2)==1,"companion AI normalizes to binary choice");
        companion.Click(Step(CompanionAi));
        Check(companion.Get()[CompanionAi]==0,"a step toggles to Legacy (0)");
        companion.Click(Step(CompanionAi));
        Check(companion.Get()[CompanionAi]==1,"and back to Enabled (1)");
        companion.Click(Step(CompanionAi,-1));
        Check(companion.Get()[CompanionAi]==0,"decrease selects Legacy (0)");
        companion.Click(Step(CompanionAi));
        Check(companion.Get()[CompanionAi]==1,"increase selects Enabled (1)");
        companion.Click(Step(CompanionAi,-1));
        Check(companion.Click({Kind::Apply}).changed==(ChangeMask{1}<<CompanionAi),"companion AI saves only its own setting");
    }
    {Model difficulty;difficulty.Open(s);
        Check(s.values[OfflinePlayers]==0,"difficulty preserves native settings by default");
        Check(Normalize(OfflinePlayers,NAN)==0&&Normalize(OfflinePlayers,9)==0&&Normalize(OfflinePlayers,2.5f)==0,"invalid offline counts fail back to native");
        for(int i=1;i<=8;++i){difficulty.Click(Step(OfflinePlayers));Check(difficulty.Get()[OfflinePlayers]==i,"increase difficulty through every native count");}
        difficulty.Click(Step(OfflinePlayers));Check(difficulty.Get()[OfflinePlayers]==8,"upper bound does not wrap");
        const auto request=difficulty.Click({Kind::Apply});
        Check(request.changed==(ChangeMask{1}<<OfflinePlayers),"difficulty does not alter melee rules or speeds");
        auto saved=s;saved.sequence=request.sequence;saved.result=Result::Saved;saved.values=request.values;
        difficulty.Observe(saved);
        for(int i=7;i>=0;--i){difficulty.Click(Step(OfflinePlayers,-1));Check(difficulty.Get()[OfflinePlayers]==i,"decrease through native default");}
        difficulty.Click(Step(OfflinePlayers,-1));Check(difficulty.Get()[OfflinePlayers]==0,"native default is lower bound");
        const auto draft=difficulty.Get();saved.offlineDifficultyAvailable=true;difficulty.Observe(saved);
        Check(difficulty.offlineDifficultyAvailable&&difficulty.Get()==draft,"capability updates preserve unsaved edits");
    }
    {Model bash;bash.Open(s);
        Check(s.values[ShieldBashSpeed]==2&&PageOf(ShieldBashSpeed)!=View::Count,"shield bash has its own speed row");
        bash.Click(Step(ShieldBashSpeed));
        Check(bash.Get()[ShieldBashSpeed]==2.25f&&bash.Click({Kind::Apply}).changed==(ChangeMask{1}<<ShieldBashSpeed),"bash speed edits only its own setting");
        Check(Normalize(ShieldBashSpeed,NAN)==2&&Normalize(ShieldBashSpeed,0)==.75f&&Normalize(ShieldBashSpeed,99)==10,"bash speed UI matches contact bounds");
    }
    Check(s.values[MenuDistance]==1&&s.values[TooltipTiming]==0,"comfortable Medium distance and Instant hover defaults");
    for(auto key:{MenuDistance,TooltipTiming}) {
        Check(Normalize(key,1.5f)==Definitions[key].defaultValue&&Normalize(key,9)==Definitions[key].defaultValue,"invalid comfort enums fall back safely");
        Model comfort;comfort.Open(s);comfort.Click(Step(key));
        Check(comfort.Click({Kind::Apply}).changed==(1u<<key),"comfort Apply affects only its selected setting");
    }
    Check(s.values[ProjectileAimAssist]==2,"aim assist defaults to Strong");
    for(float bad:{-1.f,3.f,1.5f,NAN})Check(Normalize(ProjectileAimAssist,bad)==2,"invalid aim-assist preference falls back to Strong");
    {Model assist;assist.Open(s);const Hit hit=Step(ProjectileAimAssist);
        for(float choice:{0.f,1.f,2.f,0.f,1.f}){assist.Click(hit);Check(assist.Get()[ProjectileAimAssist]==choice,"aim assist cycles Off Light Strong");}
        auto request=assist.Click({Kind::Apply});
        Check(request.changed==(1u<<ProjectileAimAssist)&&request.values[ProjectileAimAssist]==1,"assist saves independently");
        assist.Click(hit);Check(assist.Get()[ProjectileAimAssist]==1,"assist cannot change while saving");
        assist.Close();assist.Open(s);Check(assist.Get()[ProjectileAimAssist]==2,"discarding edits restores saved assistance");}
    Check(s.values[OrdinaryMobBars]==0,"ordinary mob bars are off by default");
    {Model bars;bars.Open(s);
        bars.Click(Step(OrdinaryMobBars));auto request=bars.Click({Kind::Apply});
        Check(request.changed==(1u<<OrdinaryMobBars)&&request.values[OrdinaryMobBars]==1,"toggle saves only mob visibility");}
    Check(s.values[SmoothTurn]==0&&s.values[Snap]==30,"snap turning remains the default");
    Check(EffectiveSnapAngle(30,-1)==30&&EffectiveSnapAngle(0,-1)==0,"legacy snap and smooth profiles retain their mode");
    Check(EffectiveSnapAngle(45,1)==0&&EffectiveSnapAngle(45,0)==45,"explicit toggle selects camera mode without losing angle");
    Check(EffectiveSnapAngle(0,0)==30,"turning off legacy smooth mode restores default snap angle");
    Check(SnapDegrees(NAN)==30&&SnapDegrees(-20)==15&&SnapDegrees(999)==180,"camera and menu share snap bounds");
    {Model turning;turning.Open(s);
        turning.Click(Step(SmoothTurn));
        Check(turning.Get()[SmoothTurn]==1&&turning.Get()[Snap]==30,"the toggle enables smooth turning independently");
        turning.Click({Kind::Close});turning.Open(s);
        Check(turning.Get()[SmoothTurn]==0&&!turning.Dirty(),"closing without Apply discards smooth toggle");
        turning.Click(Step(SmoothTurn));
        auto request=turning.Click({Kind::Apply});
        Check(request.changed==(1u<<SmoothTurn),"Apply saves only the turning mode");
        turning.Click(Step(SmoothTurn));
        Check(turning.Get()[SmoothTurn]==1,"toggle cannot change while saving");
        auto saved=s;saved.values=request.values;saved.sequence=request.sequence;saved.result=Result::Saved;
        turning.Observe(saved);turning.Close();turning.Open(saved);
        turning.Click(Step(SmoothTurn));
        Check(turning.Get()[SmoothTurn]==0&&turning.Get()[Snap]==30,"reopened toggle restores snap turning");
    }
    Check(s.values[NavigationLocation]==0,"combined navigation defaults to Wrist");
    Check(Normalize(NavigationLocation,99)==0&&Normalize(NavigationLocation,1.5f)==0,"invalid navigation location falls back to Wrist");
    {Model navigation;navigation.Open(s);
        for(int location=1;location<=3;++location){
            navigation.Click(Step(NavigationLocation));
            Check(navigation.Get()[NavigationLocation]==location,"navigation offers Palm, Either and Off");
        }
        auto request=navigation.Click({Kind::Apply});
        Check(request.changed==(1u<<NavigationLocation)&&request.values[NavigationLocation]==3&&request.values[VitalsLocation]==0,
            "combined navigation can turn off without changing vitals");}
    Check(s.values[VitalsLocation]==0&&s.values[CriticalAlerts]==0,"existing wrist placement remains default; automatic alerts require opt-in");
    Check(Normalize(VitalsLocation,99)==0&&Normalize(VitalsLocation,1.5f)==0,"unknown vitals locations fall back to wrist");
    {Model hud;hud.Open(s);
        hud.Click(Step(VitalsLocation));
        Check(hud.Get()[VitalsLocation]==1&&!hud.Get()[CriticalAlerts],"Palm selection does not enable automatic alerts");
        auto request=hud.Click({Kind::Apply});
        Check(request.changed==(1u<<VitalsLocation),"palm placement saves independently");
        hud.Open(s);hud.Click(Step(CriticalAlerts));request=hud.Click({Kind::Apply});
        Check(request.changed==(1u<<CriticalAlerts)&&request.values[CriticalAlerts]==1,"alert opt-in saves independently");}
    Check(s.values[BloodVisor]==1,"blood on visor defaults on");
    {Model blood;blood.Open(s);
        blood.Click(Step(BloodVisor));auto request=blood.Click({Kind::Apply});
        Check(request.changed==(ChangeMask{1}<<BloodVisor)&&request.values[BloodVisor]==0,"blood toggle saves independently");}
    Check(s.values[BloodWipe]==1,"face wipe gesture defaults on");
    {Model wipe;wipe.Open(s);
        wipe.Click(Step(BloodWipe));auto request=wipe.Click({Kind::Apply});
        Check(request.changed==(ChangeMask{1}<<BloodWipe)&&request.values[BloodWipe]==0&&request.values[BloodVisor]==1,"wipe toggle saves without touching the blood toggle");}
    Check(s.values[VitalsTilt]==0,"vitals tilt keeps the original angle by default");
    Check(Normalize(VitalsTilt,NAN)==0&&Normalize(VitalsTilt,INFINITY)==0&&
        Normalize(VitalsTilt,-999)==-60&&Normalize(VitalsTilt,999)==60,"tilt rejects nonfinite input and clamps at sixty degrees");
    {Model tilt;tilt.Open(s);
        tilt.Click(Step(VitalsTilt));Check(tilt.Get()[VitalsTilt]==5,"plus tilts five degrees");
        const auto request=tilt.Click({Kind::Apply});
        Check(request.changed==(ChangeMask{1}<<VitalsTilt)&&!(request.changed&ChangeMask{UINT32_MAX}),
            "Apply preserves the new setting bit above the previous 32-bit mask");
        Check(request.values[VitalsLocation]==0&&request.values[NavigationLocation]==0&&request.values[CriticalAlerts]==0,
            "tilt saves independently of placement, navigation and alerts");
        auto external=s.values;external[VitalsTilt]=-5;Check(Conflict(external,request.base,request.changed),"high-bit tilt conflicts are detected");
        external=s.values;external[NavigationLocation]=1;Check(!Conflict(external,request.base,request.changed),"navigation edits do not conflict with tilt");
        tilt.Open(s);tilt.Click(Step(VitalsTilt,-1));Check(tilt.Get()[VitalsTilt]==-5,"minus tilts in the opposite direction");
        tilt.Close();tilt.Open(s);Check(tilt.Get()[VitalsTilt]==0&&!tilt.Dirty(),"Close discards unapplied tilt");
        for(int i=0;i<30;++i)tilt.Click(Step(VitalsTilt));
        Check(tilt.Get()[VitalsTilt]==60,"tilt upper limit clamps");
        for(int i=0;i<60;++i)tilt.Click(Step(VitalsTilt,-1));
        Check(tilt.Get()[VitalsTilt]==-60,"tilt lower limit clamps");}
    Check(s.values[PlanarSpellAim]==0,"spell aiming defaults to free vertical aim");
    {Model aim;aim.Open(s);
        aim.Click(Step(PlanarSpellAim));Check(s.values[PlanarSpellAim]==0&&aim.Get()[PlanarSpellAim]==1,"aim change is a draft");
        const auto request=aim.Click({Kind::Apply});
        Check(request.changed==(1u<<PlanarSpellAim)&&request.values[PlanarSpellAim]==1,"planar aim saves only its preference");}
    Check(s.values[WeaponSkillIndicators]==0,"weapon skill indicators default off");
    {Model indicators;indicators.Open(s);
        indicators.Click(Step(WeaponSkillIndicators));auto request=indicators.Click({Kind::Apply});
        Check(request.changed==(1u<<WeaponSkillIndicators)&&request.values[WeaponSkillIndicators]==1,"indicator opt-in saves only its preference");}
    Check(s.values[ControllerLabels]==0&&Normalize(ControllerLabels,99)==0&&Normalize(ControllerLabels,1.5f)==0,"unknown controller preferences use automatic");
    {Model chooser;chooser.Open(s);   // Button names: a press on Index (a chip or a popover option)
        layout::Press(chooser,{Kind::Set,ControllerLabels,0,2},0);
        Check(chooser.Get()[ControllerLabels]==2&&chooser.Dirty(),"Index option changes draft only");
        auto pending=chooser.Click({Kind::Apply});
        Check(pending.changed==(1u<<ControllerLabels)&&pending.values[ControllerLabels]==2,"Apply queues preference alone");
        layout::Press(chooser,{Kind::Set,ControllerLabels,0,3},0);Check(chooser.Get()[ControllerLabels]==2,"cannot edit while saving");
        chooser.Close();chooser.Open(s);Check(chooser.Get()[ControllerLabels]==0,"reopen discards unsaved choice");
    }
    Check(s.values[DrawDistance]==2&&s.values[ObjectDistance]==210,"distance fallbacks match release configuration: objects just past the fog's end");
    {Model distance;distance.Open(s);
        distance.Click({Kind::Change,ObjectDistance,-1});
        Check(distance.Get()[ObjectDistance]==160,"object distance steps by 50, never jumping to the minimum");
        distance.Click({Kind::Change,ObjectDistance,1});distance.Click({Kind::Change,ObjectDistance,1});
        Check(distance.Get()[ObjectDistance]==260,"and back up past the default");}
    Model m;m.Open(s);
    Check(m.open&&m.ready&&!m.Dirty()&&m.view==View::Guide,"open actual configuration on the Controller guide");
    Check(Normalize(BeltForward,.06f)==.06f,"settings preserve the closer belt placement");
    m.Click(Step(Motion));
    Check(m.Get()[Motion]==1&&m.Dirty()&&!m.Busy(),"edit remains a draft until Apply");
    m.Click({Kind::Close});m.Open(s);
    Check(!m.Dirty()&&m.Get()[Motion]==0,"Close discards unsubmitted edits");
    m.Click({Kind::Change,Foveation,1});m.Click({Kind::Change,Snap,1});
    auto r=m.Click({Kind::Apply});
    Check(r.sequence&&r.changed==((1u<<Foveation)|(1u<<Snap))&&r.values[Foveation]==2,"Apply submits exactly changed values");
    Check(m.Busy()&&!m.Click({Kind::Apply}).sequence,"Apply cannot double submit");
    m.Click({Kind::Change,Motion,1});Check(m.Get()[Motion]==0,"editing blocked during save");
    Snapshot done=s;done.values=r.values;done.sequence=r.sequence;done.result=Result::Saved;m.Observe(done);
    Check(!m.Busy()&&!m.Dirty()&&m.result==Result::Saved,"save acknowledgement resets draft");
    auto changed=done;changed.values[Sky]=1;m.Observe(changed);Check(m.Get()[Sky]==1,"idle panel follows external edits");
    m.Click({Kind::Change,Motion,1});changed.values[Fog]=1;m.Observe(changed);
    Check(m.Get()[Fog]==0&&m.Get()[Motion]==1,"external polling preserves unsaved draft");
    r=m.Click({Kind::Apply});
    Check(!Conflict(changed.values,r.base,r.changed),"unrelated external edits do not conflict");
    changed.values[Motion]=1;Check(Conflict(changed.values,r.base,r.changed),"same-key external edits conflict");
    changed.sequence=r.sequence;changed.result=Result::Conflict;m.Observe(changed);
    Check(!m.Busy()&&!m.Dirty()&&m.Get()[Fog]==1,"conflict reloads current configuration");
    m.Click({Kind::Change,Foveation,-1});r=m.Click({Kind::Apply});
    changed.sequence=r.sequence;changed.result=Result::Failed;m.Observe(changed);
    Check(!m.Busy()&&m.Dirty(),"failed save retains draft for retry");
    Snapshot pending=s;pending.sequence=90;pending.result=Result::Saving;m.Open(pending);
    Check(m.Busy(),"reopening while saving waits for acknowledgement");
    pending.result=Result::Saved;m.Observe(pending);m.Click({Kind::Change,Motion,1});
    Check(m.Click({Kind::Apply}).sequence>90,"new request sequence exceeds observed save sequence");
    m.Open(s);for(int i=0;i<200;++i)m.Click({Kind::Change,Snap,1});
    Check(m.Get()[Snap]==180,"upper limit clamps");
    for(int i=0;i<200;++i)m.Click({Kind::Change,Snap,-1});
    Check(m.Get()[Snap]==15&&Normalize(Waist,INFINITY)==Definitions[Waist].defaultValue,"lower and nonfinite limits");
    s.ready=false;m.Open(s);m.Click({Kind::Change,Motion,1});
    Check(!m.Dirty()&&!m.Click({Kind::Apply}).sequence,"missing config remains read-only");
    InputGate gate;   // the settings chord and Right B as press edges (trigger presses: pointer::PointerFilter)
    auto e=gate.Step(true,true,true);Check(!e.chord&&!e.cancel,"initial held controls swallowed");
    gate.Step(true,false,false);e=gate.Step(true,true,false);Check(e.chord,"fresh chord opens");
    gate.Consume();Check(!gate.Step(true,true,true).cancel,"opening consumes a B already held");
    gate.Step(false,true,true);e=gate.Step(true,true,true);
    Check(!e.chord&&!e.cancel,"tracking return cannot replay held inputs");
    gate.Step(true,false,false);Check(gate.Step(true,false,true).cancel,"fresh B cancels");
    Check(Defaults()[CompanionCounts]==1,"companion counts default on");
    s.ready=true;s.values=Defaults();m.Open(s);
    m.Click(Step(CompanionCounts));
    Check(m.Get()[CompanionCounts]==0&&m.Get()[NavigationLocation]==0,"companion toggle is independent of map placement");
    Check(m.Click({Kind::Apply}).changed==(ChangeMask{1}<<CompanionCounts),"companion toggle uses its own high change-mask bit");
    {   // The interfaces hold together: content, Press, the pointer and the color tokens.
        Check(content::PageOf(View::Aim).id==View::Aim&&!content::PageOf(View::Guide).settings,"one page per view, in View order");
        const auto* row=content::RowOf(ProjectileAimAssist);
        Check(row&&content::ValueText(*row,2)=="Strong","the sample row names its options");
        Model redesign;s.ready=true;s.values=Defaults();redesign.Open(s);
        layout::Press(redesign,{Kind::Set,ProjectileAimAssist,0,1},0);
        Check(redesign.Get()[ProjectileAimAssist]==1&&redesign.Applied()[ProjectileAimAssist]==2&&redesign.Dirty(),"Set changes the draft only");
        pointer::PointerFilter filter;pointer::CardFocus card;
        const auto built=layout::Build(redesign,redesign.context,{});
        Check(filter.Step(built,0,true,10,10,false).hover==Hit{}&&card.Step(0,NoHelp,0,0,true,built.card)==NoHelp,"nothing pointed off the controls");
        Check(draw::Rgb(draw::Color::Changed)==0xFFA800,"color tokens hold the art's values");
    }
    {   // An injected paint failure (the painter returns no pixels): close and tell once, no resubmits, retry on the next Open.
        PaintGuard guard;guard.Open();
        Check(guard.CanRequest()&&!guard.Failed(false),"a drawn picture is not a failure");
        Check(guard.Failed(true),"the first empty picture after Open closes the panel and tells the player");
        Check(!guard.CanRequest(),"then nothing more goes to the painter");
        Check(!guard.Failed(true),"and a late empty picture tells nothing twice");
        guard.Open();Check(guard.CanRequest(),"the next Open tries again");
        Check(guard.Failed(true),"and its own failure tells again");
        const std::string notice=PaintFailureNotice("VR Settings","DWriteCreateFactory(IDWriteFactory5) failed (0x80004002)");
        Check(notice=="VR Settings could not draw: DWriteCreateFactory(IDWriteFactory5) failed (0x80004002)","the notice names the error");
        const std::string longer=PaintFailureNotice("VR Settings",std::string(300,'x'));
        Check(longer.size()==std::string("VR Settings could not draw: ").size()+72&&longer.ends_with("..."),"a long error is cut to fit the card");
        Check(PaintFailureNotice("Body calibration","")=="Body calibration could not draw: unknown error","an empty error still says something");
    }
    {   // Left-hand strikes: set from the skill wheel's Left claw strikes box (Druid), on no settings page
        Check(s.values[LeftHandStrikes]==0&&std::wstring(Definitions[LeftHandStrikes].key)==L"left_hand_strikes",
              "left-hand strikes: own trigger skill by default");
        Check(Normalize(LeftHandStrikes,2)==0&&Normalize(LeftHandStrikes,1)==1&&Normalize(LeftHandStrikes,-1)==0,"the retired Basic attack (2) reads Own trigger skill");
        Check(!content::RowOf(LeftHandStrikes),"left-hand strikes on no settings page");
    }
    {   // Frame generation's settings (main's #313), as on main.
        Check(Defaults()[FrameGeneration]==0&&Defaults()[FrameGenPreset]==1&&Defaults()[FrameGenScale]==2,"off by default; NVIDIA Medium at 50% to start");
        Check(Normalize(FrameGeneration,3)==0&&Normalize(FrameGeneration,1.5f)==0&&Normalize(FrameGenScale,-1)==2&&Normalize(FrameGenPreset,2)==2,
              "out-of-range frame generation values read as their defaults");
        Check(std::wstring(Definitions[FrameGeneration].key)==L"frame_generation"&&std::wstring(Definitions[FrameGeneration].section)==L"render"&&
              std::wstring(Definitions[FrameGenPreset].key)==L"frame_generation_preset"&&std::wstring(Definitions[FrameGenScale].key)==L"frame_generation_scale",
              "frame generation's INI keys");
        auto nvidia=Defaults();nvidia[FrameGeneration]=2;auto fidelity=Defaults();fidelity[FrameGeneration]=1;
        Check(RowShown(FrameGenPreset,nvidia,{})&&RowShown(FrameGenScale,nvidia,{})&&!RowShown(FrameGenPreset,fidelity,{})&&!RowShown(FrameGenScale,fidelity,{}),
              "NVIDIA's quality and scale only with NVIDIA");
        Check(HelpText(FrameGeneration)[0]&&content::RowOf(FrameGeneration)&&content::RowOf(FrameGeneration)->help[0],"frame generation's help reads");
        Model frames;frames.Open(s);layout::Press(frames,{Kind::Set,StereoRendering,0,0},0);   // Both eyes first (main: frame generation turns on only then)
        layout::Press(frames,{Kind::Set,FrameGeneration,0,2},0);Check(frames.Get()[FrameGeneration]==2&&frames.Dirty(),"NVIDIA is one press, a draft");
        layout::Press(frames,{Kind::Set,FrameGenScale,0,1},0);Check(frames.Get()[FrameGenScale]==1,"75%");
        Check(frames.Click({Kind::Apply}).changed==((ChangeMask{1}<<StereoRendering)|(ChangeMask{1}<<FrameGeneration)|(ChangeMask{1}<<FrameGenScale)),"Apply saves only those three (Both eyes, NVIDIA, 75%)");
    }
    {   // The merge (docs/VR_SETTINGS_MENU.md): made-up overrides against the real Definitions.
        static constexpr content::Option twoNames[]={{0,"Isometric","a"},{1,"Top-down","b"},{7,"Past the end","c"}};
        static constexpr content::ValueName farWord[]={{0,"Off"},{999,"Far"}};
        static constexpr content::Override made[]={
            {.id="gameplay.no_such_setting",.page=View::Combat,.section="Melee",.label="Gone"},
            {.id="interactions.map_top_down",.page=View::Wrist,.section="Map",.label="Map view",.options=twoNames,.optionCount=3},
            {.id="gameplay.melee_hit_speed",.page=View::Combat,.section="Melee",.kind=content::RowKind::Toggle},
            {.id="gameplay.target_outline_pct",.page=View::Targets,.names=farWord,.nameCount=2},
            {.id="gameplay.target_outline_pct",.page=View::Targets,.label="Twice"},
            {.id="sky.enabled",.page=View::World,.section="Sky and dungeons"},
            {.id="fog.enabled",.page=content::NotInMenu},
            {.id="calibration.eye_height_m",.page=View::Body,.section="Measurements"},   // no long help upstream: its detail
        };
        static constexpr content::PresetSetting gone[]={{"gameplay.no_such_setting",1},{"sky.enabled",1},{"gameplay.target_outline_pct",900}};
        static constexpr content::PresetSpec specs[]={{"test","Test",draw::Icon::Comfort,"A test preset.",gone,3}};
        const auto menu=content::BuildMenu(made,std::size(made),specs,1);
        auto reported=[&](const char* words){for(const auto& l:menu.report)if(l.find(words)!=std::string::npos)return true;return false;};
        Check(reported("orphan override: gameplay.no_such_setting")&&std::count_if(menu.rows.begin(),menu.rows.end(),[](const content::ContentRow& r){return !r.fallback;})==5,"an override for a removed setting is skipped and reported, never a build error");
        const content::ContentRow* map=nullptr;const content::ContentRow* speed=nullptr;const content::ContentRow* outline=nullptr;const content::ContentRow* sky=nullptr;
        for(const auto& r:menu.rows){if(r.key==MapTopDown)map=&r;if(r.key==MeleeHitSpeed)speed=&r;if(r.key==TargetHighlight)outline=&r;if(r.key==Sky)sky=&r;}
        Check(map&&map->optionCount==4&&std::string(map->options[1].label)=="Top-down"&&std::string(map->options[2].label)=="Choice 3"&&
              std::string(map->options[3].label)=="Choice 4"&&reported("interactions.map_top_down: 3 names, 4 values"),
              "a partly named list keeps its names, reads Choice n for the rest, drops names past the range and says so");
        Check(speed&&speed->kind==content::RowKind::Stepper&&reported("gameplay.melee_hit_speed: its row kind no longer fits"),"a kind that no longer fits falls back to the data's own control");
        Check(outline&&outline->nameCount==1&&std::string(outline->label)==Definitions[TargetHighlight].label&&
              std::string(outline->help)==HelpText(TargetHighlight)&&reported("second override ignored")&&reported("value words past the range dropped"),
              "missing words fall back to upstream's label and long help; one override per id");
        {const content::ContentRow* eye=nullptr;for(const auto& r:menu.rows)if(r.key==EyeHeight)eye=&r;
         Check(eye&&std::string(eye->help)==Definitions[EyeHeight].detail,"a setting with neither our words nor a long help shows its Definitions detail");}
        Check(sky&&sky->kind==content::RowKind::Toggle&&sky->section==0,"an override with only a page and section: the switch comes from the data");
        bool fogRow=false;for(const auto& r:menu.rows)if(r.key==Fog)fogRow=true;
        Check(!fogRow&&!reported("no override: fog.enabled")&&reported("no words yet: ceiling.enabled"),"NotInMenu keeps a setting off the menu quietly; unplaced settings are listed");
        Check(menu.presets.size()==1&&menu.presets[0].count==1&&menu.presets[0].values[0].key==Sky&&reported("orphan preset entry: test gameplay.no_such_setting")&&
              reported("preset value out of range"),"preset entries by ini id: a removed setting or a value out of range is skipped and reported");
    }
    PrintReport();
    std::puts("VR settings draft, persistence requests, conflicts, row rules and input-gating tests passed");
}
