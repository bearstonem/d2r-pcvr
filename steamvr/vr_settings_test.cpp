#include "vr_settings_help.h"
#include "reshade_addons.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
using namespace framevr::settings;
static void Check(bool b,const char* label){if(!b){std::fprintf(stderr,"FAIL: %s\n",label);std::exit(1);}}
// The rows a page shows (with the defaults unless given), and a click at a
// control's centre: on a page's default rows, or on a model's own.
static int RowCount(Page p,const Values& v=Defaults(),const Context& c={}){return Rows(p,v,c).count;}
static Key RowKey(Page p,int row,const Values& v=Defaults(),const Context& c={}){return Rows(p,v,c)[row];}
static Hit At(Page p,Rect r){return HitTest(p,(r.x+r.w*.5f)/Width,(r.y+r.h*.5f)/Height);}
static Hit At(const Model& m,Page p,Rect r){const auto rows=m.Rows(p);return HitTest(p,(r.x+r.w*.5f)/Width,(r.y+r.h*.5f)/Height,false,&rows);}
static_assert((ChangeMask{1}<<64)==ChangeMask(0,1)&&(ChangeMask{1}<<127)==ChangeMask(0,uint64_t{1}<<63),"shifts cross into the high word");
static_assert((ChangeMask(0,1)>>64)==ChangeMask{1}&&(ChangeMask{3}<<63)==ChangeMask(uint64_t{1}<<63,1),"shifts carry between words");
int main(){
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
    {
        Snapshot state;state.ready=true;Model model;model.Open(state);
        for(int page=0;page<TabCount;++page){
            const auto box=TabBox(page);
            const auto hit=HitTest(model.page,(box.x+box.w/2.f)/Width,(box.y+box.h/2.f)/Height);
            Check(hit.kind==Kind::Tab&&hit.index==page,"ordinary settings tabs match visible bounds");model.Click(hit);
        }
        Check(model.page==Page::ReleaseNotes&&!model.Dirty(),"release notes open in regular play without editing settings");
        Check(HitTest(Page::ReleaseNotes,.5f,.5f).kind==Kind::None,"release lines are read-only");
        model.Click({Kind::Tab,TabCount});Check(model.page==Page::ReleaseNotes,"no hidden developer settings tab");
        state.ready=false;model.Open(state);model.Click(At(model,Page::Controls,TabBox(int(Page::ReleaseNotes))));
        Check(model.page==Page::ReleaseNotes&&!model.Dirty(),"release notes remain readable without configuration");
    }
    {
        HoverHelp help;
        Check(help.Step(MeleeRules,100)==NoHelp&&help.Step(MeleeRules,549)==NoHelp,"passing over a setting does not flash help");
        Check(help.Step(MeleeRules,550)==MeleeRules,"steady hover reveals help after 450 ms");
        Check(help.Step(MeleeHitSpeed,600)==NoHelp&&help.Step(MeleeHitSpeed,1050)==MeleeHitSpeed,"changing settings restarts dwell and clears old help");
        Check(help.Step(NoHelp,1100)==NoHelp&&help.Step(MeleeHitSpeed,1600)==NoHelp,"leaving panel or losing tracking clears dwell");
        help.Clear();Check(help.Step(MeleeHitSpeed,3000)==NoHelp,"close or focus loss cannot retain old dwell");
        Check(HelpAt(Page::Combat,NAN,.5f)==NoHelp&&HelpAt(Page::Combat,1,.5f)==NoHelp,"invalid help coordinates ignored");
        for(int p=0;p<TabCount;++p)for(int row=0;row<RowCount(Page(p));++row){
            const float v=float(RowY+row*RowStep+20)/Height;
            const auto key=RowKey(Page(p),row);
            Check(HelpAt(Page(p),100.f/Width,v)==key&&HelpAt(Page(p),1100.f/Width,v)==(Paired(key)?PairOf(key):key),
                  "labels and values explain the same setting; a paired row's right half its right setting");
            Check(HelpText(key)[0]&&HelpTitle(key)[0],"every visible row has help");
            Check(HitTest(Page(p),100.f/Width,v).kind==Kind::None,"hovering or clicking a label does not edit settings");
        }
        // Help converts at its own length (wide_text.h); keep it short enough for its card.
        for(int id=0;id<=HelpLast;++id)Check(std::string(HelpText(id)).size()<450,"every help text is short enough for its card");
        Check(HelpAt(Page::VR,900.f/Width,250.f/Height,true)==NoHelp,"dropdown blocks help for covered settings");
        Check(HelpAt(Page::Controls,100.f/Width,750.f/Height)==PlanarSpellAim,"controls page label has help");
        Check(HelpAt(Page::HUD,1000.f/Width,950.f/Height)==HelpApply,"footer buttons have help");
        Check(HelpCard.x>=Width&&HelpCard.y+HelpCard.h<Apply.y,"help never covers controls or footer");
    }
    Snapshot s;s.ready=true;
    {Model effects;effects.Open(s);effects.Click({Kind::Tab,int(Page::Video)});
        Check(s.values[ElementalHands]==0,"experimental hand effects default off");
        const auto hit=At(Page::Video,ValueBox(3));
        Check(hit.index==ElementalHands&&hit.kind==Kind::Change,"elemental hand toggle hit target");
        effects.Click(hit);const auto request=effects.Click({Kind::Apply});
        Check(request.changed==(ChangeMask{1}<<ElementalHands)&&request.values[ElementalHands]==1,"the elemental toggle saves alone");
        auto external=request.base;external[Motion]=1;
        Check(!Conflict(external,request.base,request.changed),"its edit tolerates unrelated setting changes");
        external[ElementalHands]=1;Check(Conflict(external,request.base,request.changed),"its conflicts are detected");
    }
    {
        Check(RowCount(Page::Performance)==8&&RowKey(Page::Performance,4)==Foveation&&RowKey(Page::Performance,5)==FoveationStrength&&
              RowKey(Page::Performance,1)==RenderResolution&&RowKey(Page::Performance,7)==DesktopOneEye,"strength sits directly under foveated rendering");
        Check(RowKey(Page::Performance,0)==StereoRendering&&RowKey(Page::Performance,2)==DrawDistance&&RowKey(Page::Performance,3)==ObjectDistance&&
              RowKey(Page::Performance,6)==ReShadeDepth,"everything that changes the frame rate is on the Perf page");
        Check(RowCount(Page::Video)==4&&RowKey(Page::Video,0)==Sky&&RowKey(Page::Video,1)==Fog&&RowKey(Page::Video,2)==Ceiling&&
              RowKey(Page::Video,3)==ElementalHands,"the Video page keeps the mod's effects");
        const auto last=Increase(RowCount(Page::Performance)-1);
        Check(last.y+last.h<PerformanceBox.y&&PerformanceBox.y+PerformanceBox.h<=GameVideoBox.y&&GameVideoBox.y+GameVideoBox.h<=DepthRestartBox.y&&
              DepthRestartBox.y+DepthRestartBox.h<876,"Perf rows, then its numbers, end above the save status");
        Model strength;strength.Open(s);strength.Click({Kind::Tab,int(Page::Performance)});
        Check(strength.Get()[Foveation]==1&&strength.Get()[FoveationStrength]==1,"fixed foveation at Medium strength by default");
        strength.Click(At(strength,Page::Performance,Increase(5)));
        Check(strength.Get()[FoveationStrength]==2&&strength.Dirty(),"strength adjusts while foveation is selected");
        for(int i=0;i<5;++i)strength.Click(At(strength,Page::Performance,Increase(5)));
        Check(strength.Get()[FoveationStrength]==3,"Maximum is the strongest level");
        strength.Click(At(strength,Page::Performance,Decrease(4)));Check(strength.Get()[Foveation]==0,"foveation can be turned off");
        Check(!RowEnabled(strength.Get(),FoveationStrength)&&RowEnabled(strength.Get(),Foveation),"Off disables only the strength row");
        strength.Click(At(strength,Page::Performance,Decrease(5)));strength.Click(At(strength,Page::Performance,Decrease(5)));
        Check(strength.Get()[FoveationStrength]==3,"strength ignores edits while foveation is off");
        strength.Click(At(strength,Page::Performance,Increase(4)));strength.Click(At(strength,Page::Performance,Decrease(5)));
        Check(strength.Get()[FoveationStrength]==2,"strength edits resume when foveation is selected again");
        Check(Normalize(FoveationStrength,1.5f)==1&&Normalize(FoveationStrength,7)==1&&Normalize(FoveationStrength,-1)==1,"malformed strength shows Medium");
    }
    {
        Model stereo;stereo.Open(s);stereo.Click({Kind::Tab,int(Page::Performance)});
        Check(stereo.Get()[StereoRendering]==1&&RowEnabled(stereo.Get(),StereoRendering),"By turns by default");
        // Two choices since Replay left: switched like the other two-choice rows
        stereo.Click(At(stereo,Page::Performance,Increase(0)));
        Check(stereo.Get()[StereoRendering]==0&&stereo.Dirty(),"an arrow switches to Both eyes");
        stereo.Click(At(stereo,Page::Performance,ValueBox(0)));
        Check(stereo.Get()[StereoRendering]==1&&!stereo.Dirty(),"the value box switches back to By turns");
        stereo.Click(At(stereo,Page::Performance,Decrease(0)));
        Check(stereo.Get()[StereoRendering]==0,"the other arrow to Both eyes as well");
        stereo.Click(At(stereo,Page::Performance,Decrease(0)));
        Check(stereo.Get()[StereoRendering]==1&&!stereo.Dirty(),"and back to By turns");
        Check(std::size(StereoRenderingNames)==2&&std::string(StereoRenderingNames[0])=="Both eyes"&&std::string(StereoRenderingNames[1])=="By turns",
              "stereo rendering names");
        Check(Normalize(StereoRendering,NAN)==1&&Normalize(StereoRendering,-3)==1&&Normalize(StereoRendering,3)==1&&Normalize(StereoRendering,1.5f)==1&&
              Normalize(StereoRendering,0)==0,"malformed stereo rendering goes by turns");
        Check(Normalize(StereoRendering,2)==1,"a profile saved with Replay goes by turns");
    }
    {   // The GPU wait not deferred: the Perf page's button turns it on, once, in every mode
        Model defer;defer.Open(s);defer.Click({Kind::Tab,int(Page::Performance)});
        const auto box=[&]{return At(Page::Performance,DeferBox);};
        Check(DeferOffered(defer.Get())&&defer.Get()[DeferGpuWait]==0,"the deferred GPU wait is offered while it is off");
        Check(HelpAt(Page::Performance,(DeferBox.x+10.f)/Width,(DeferBox.y+10.f)/Height,false,&defer.Get())==DeferGpuWait,"the button has help");
        defer.Click(box());
        Check(defer.Get()[DeferGpuWait]==1&&defer.Dirty()&&!DeferOffered(defer.Get()),"the button turns it on; Apply saves it");
        defer.Click(box());
        Check(defer.Get()[DeferGpuWait]==1,"the button never turns it off");
        for(int mode:{0}){
            Model other;other.Open(s);other.Click({Kind::Tab,int(Page::Performance)});
            other.Click(At(other,Page::Performance,Decrease(0)));
            Check(other.Get()[StereoRendering]==mode&&DeferOffered(other.Get()),"offered with Both eyes too");
            other.Click(box());
            Check(other.Get()[DeferGpuWait]==1,"the button turns it on in every mode");
        }
        Snapshot on=s;on.values[DeferGpuWait]=1;
        Model deferred;deferred.Open(on);deferred.Click({Kind::Tab,int(Page::Performance)});deferred.Click(box());
        Check(!DeferOffered(deferred.Get())&&deferred.Get()[DeferGpuWait]==1&&!deferred.Dirty(),"no button and no change once deferred");
        Check(HelpAt(Page::Performance,(DeferBox.x+10.f)/Width,(DeferBox.y+10.f)/Height,false,&deferred.Get())==NoHelp,"no help for a hidden button");
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
        Check(HelpAt(Page::Performance,600.f/Width,(PerformanceBox.y+10.f)/Height)==HelpPerformance&&
              HelpAt(Page::Performance,600.f/Width,(GameVideoBox.y+10.f)/Height)==HelpGameVideo,"each of the Perf page's lines has help");
        Check(HelpAt(Page::Performance,600.f/Width,(DepthRestartBox.y+10.f)/Height)==NoHelp,"no card over the restart note's strip, drawn or not");
        r.mixed=true;Check(PerformanceLine(r).find("(mixed)")!=std::string::npos,"a second of turns and pairs says so");r.mixed=false;
        Check(HelpText(HelpPerformance)[0]&&HelpText(HelpGameVideo)[0]&&HelpTitle(HelpPerformance)[0]&&HelpTitle(HelpGameVideo)[0]&&HelpLast==HelpGameVideo,"and their help reads");
        Check(HelpAt(Page::Video,600.f/Width,(PerformanceBox.y+10.f)/Height)==NoHelp,"not on other pages");
    }
    {   // ReShade depth: d2r_pcvr.ini [render] reshade_depth, set in ReShade.ini before ReShade loads
        const auto& d=Definitions[ReShadeDepth];
        Check(std::wstring(d.section)==L"render"&&std::wstring(d.key)==L"reshade_depth"&&d.defaultValue==1&&d.min==0&&d.max==1,"an on/off [render] setting");
        Model depth;depth.Open(s);depth.Click({Kind::Tab,int(Page::Performance)});
        depth.Click(At(depth,Page::Performance,ValueBox(6)));
        Check(depth.Get()[ReShadeDepth]==0&&depth.Dirty(),"its value box switches it off");
        const auto save=depth.Click({Kind::Apply});
        Check(save.changed==(ChangeMask{1}<<ReShadeDepth)&&save.values[ReShadeDepth]==0,"Apply saves only it");
        namespace addons=framevr::reshadeaddons;
        Check(!addons::Disabled(L"",addons::GenericDepth)&&addons::Disabled(L"Generic Depth",addons::GenericDepth)&&
              addons::Disabled(L"Effect Runtime Sync, generic depth ",addons::GenericDepth)&&!addons::Disabled(L"Generic Depth Plus",addons::GenericDepth),
              "the add-on is found by its whole name, any case or spacing");
        Check(addons::WithEnabled(L"",addons::GenericDepth,false)==L"Generic Depth"&&addons::WithEnabled(L"Generic Depth",addons::GenericDepth,true).empty(),
              "off lists it, on removes it");
        Check(addons::WithEnabled(L"A, Generic Depth ,B",addons::GenericDepth,true)==L"A,B"&&addons::WithEnabled(L"A,B",addons::GenericDepth,false)==L"A,B,Generic Depth"&&
              addons::WithEnabled(L"Generic Depth,A,generic depth",addons::GenericDepth,false)==L"A,Generic Depth","other add-ons keep their place; listed once");
    }
    {
        for(int id=0;id<Count;++id)if(const int n=HelpChoices(id)) {
            Check(n==int(Definitions[id].max-Definitions[id].min)+1,"every choice of a row is explained");
            for(int c=0;c<n;++c){const auto h=HelpChoice(id,Definitions[id].min+c);Check(h.name&&h.name[0]&&h.text&&h.text[0]&&std::string(h.text)!=HelpText(id),"each choice has a name and its own text");}
        }
        Check(std::string(HelpChoice(StereoRendering,1).name)=="By turns"&&std::string(HelpChoice(Foveation,2).name)=="Eye tracked"&&
              std::string(HelpChoice(Dominant,1).name)=="Left"&&std::string(HelpChoice(RenderResolution,4).name)=="2560 x 2560","the help names the choice the row shows");
        Check(!HelpChoice(Motion,1).name&&!HelpChoice(Snap,30).name&&!HelpChoice(HelpApply,0).name&&!HelpChoice(Foveation,NAN).name,"toggles, numbers and buttons keep their general help");
    }
    {
        Model map;map.Open(s);map.Click({Kind::Tab,int(Page::HUD)});
        const auto view=At(Page::HUD,HudMapViewBox),rotate=At(Page::HUD,HudMapRotateBox);
        Check(view.kind==Kind::Change&&view.index==MapTopDown&&rotate.kind==Kind::Change&&rotate.index==MapRotate,"HUD map controls target separate preferences");
        Check(s.values[MapTopDown]==0&&s.values[MapRotate]==0,"map defaults remain isometric and north-up");
        for(const auto box:{HudMapViewBox,HudMapRotateBox}){
            Check(box.y>TabBox(TabCount-1).y+TabBox(TabCount-1).h&&box.y+box.h<RowY-4,"map controls fit between tabs and existing HUD rows");
            for(int page=0;page<TabCount;++page)if(Page(page)!=Page::HUD){
                const auto hit=At(Page(page),box);
                Check(hit.kind!=Kind::Change||(hit.index!=MapTopDown&&hit.index!=MapRotate),"other pages have no invisible map controls");
            }
        }
        Check(HelpAt(Page::HUD,100.f/Width,205.f/Height)==MapTopDown&&
              HelpAt(Page::HUD,700.f/Width,205.f/Height)==MapRotate,"map labels explain their own settings");
        Check(HelpAt(Page::HUD,400.f/Width,205.f/Height)==MapTopDown&&
              HelpAt(Page::HUD,1050.f/Width,205.f/Height)==MapRotate,"map values have the same help as their labels");
        map.Click(view);map.Click(rotate);
        Check(map.Dirty()&&map.Get()[MapTopDown]==1&&map.Get()[MapRotate]==1&&s.values[MapTopDown]==0,"map changes remain drafts");
        map.Click({Kind::Close});map.Open(s);map.Click({Kind::Tab,int(Page::HUD)});
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
        Model loot;loot.Open(s);loot.Click({Kind::Tab,int(Page::HUD)});
        Check(RowKey(Page::HUD,8)==LootLabelSize,"HUD exposes loot sizing");
        loot.Click(At(loot,Page::HUD,Increase(8)));Check(loot.Get()[LootLabelSize]==50,"loot size increases by five percentage points");
        auto request=loot.Click({Kind::Apply});
        Check(request.changed==(ChangeMask{1}<<LootLabelSize)&&request.values[LootLabelSize]==50,"loot Apply uses its own high mask bit");
        loot.Open(s);loot.Click(At(loot,Page::HUD,Decrease(8)));Check(loot.Get()[LootLabelSize]==40,"loot size decreases by five percentage points");
        loot.Click({Kind::Close});loot.Open(s);Check(loot.Get()[LootLabelSize]==45&&!loot.Dirty(),"Close discards unsaved loot size");
    }
    for(int i=HapticStrength;i<=HapticAlert;++i){
        const auto key=Key(i);const int row=i-HapticStrength+(i>=HapticBowDraw?1:0);
        Check(s.values[key]==100,"haptics default to normal strength");
        Check(Normalize(key,-1)==0&&Normalize(key,250)==200&&Normalize(key,54)==50&&Normalize(key,NAN)==100,"haptic settings clamp, step and reject nonfinite values");
        Model h;h.Open(s);h.Click({Kind::Tab,int(Page::Haptics)});
        Check(RowKey(Page::Haptics,row)==key&&At(Page::Haptics,Increase(row)).index==key,"each haptic row targets its own setting");
        h.Click(At(h,Page::Haptics,Decrease(row)));Check(h.Get()[key]==90,"strength moves in ten-percent steps");
        if(row){
            const auto category=framevr::haptics::Category(i-HapticMelee);
            for(int choice=0;choice<framevr::haptics::PreviewCount(category);++choice){
                const auto box=TestHapticsBox(row,choice,category);
                const auto test=At(Page::Haptics,box);
                Check(test.kind==Kind::TestHaptics&&test.index==i-HapticMelee&&test.delta==choice+1&&h.CanTestHaptics(test.index),"category Test selects its action preview with unsaved settings");
                if(framevr::haptics::PreviewCount(category)==2){
                    const auto other=TestHapticsBox(row,choice^1,category);
                    Check(!other.Contains(box.x+box.w*.5f,box.y+box.h*.5f),"action preview buttons do not overlap");
                }else Check(HitTest(Page::Haptics,620.f/Width,(box.y+box.h*.5f)/Height).kind==Kind::None,"single-preview row has no invisible second test");
                Check(box.x+box.w<Decrease(row).x,"tests do not overlap strength controls");
                Check(!h.Click(test).sequence&&h.Dirty()&&h.Get()[key]==90,"Test does not persist draft changes");
            }
        }
        const auto request=h.Click({Kind::Apply});
        Check(request.changed==(ChangeMask{1}<<key)&&request.values[key]==90&&!h.CanTestHaptics(0),"Apply saves only selected haptic strength and blocks preview while saving");
        h.Close();h.Open(s);Check(h.Get()[key]==100&&!h.CanTestHaptics(0),"close discards edits and Controls page has no preview");
    }
    Check(!Model{}.CanTestHaptics(0),"closed menu cannot preview haptics");
    {Model h;h.Open(s);h.Click({Kind::Tab,int(Page::Haptics)});
        const auto toggle=At(Page::Haptics,ValueBox(2));
        Check(s.values[HapticShortMelee]==0&&toggle.kind==Kind::Change&&toggle.index==HapticShortMelee,"default longer melee pulse has its own toggle");
        const auto normal=HapticSample(h.Get(),framevr::haptics::Category::Melee);
        Check(normal.milliseconds==90&&normal.frequency==0&&HapticSample(h.Get(),framevr::haptics::Category::Melee,1).milliseconds==45,"weapon impact lasts longer through a swing while shield keeps its existing tap");
        for(int choice=0;choice<2;++choice)Check(At(Page::Haptics,TestHapticsBox(2,choice,framevr::haptics::Category::Melee)).kind==Kind::None,"toggle row does not steal a category Test button");
        h.Click(toggle);const auto sharp=HapticSample(h.Get(),framevr::haptics::Category::Melee);
        Check(sharp.milliseconds==10&&sharp.frequency==180&&sharp.amplitude==normal.amplitude,"draft test immediately uses short pulse at the same strength");
        const auto request=h.Click({Kind::Apply});
        Check(request.changed==(ChangeMask{1}<<HapticShortMelee)&&request.values[HapticMelee]==100,"pulse shape saves without changing strength");
        h.Close();h.Open(s);Check(HapticSample(h.Get(),framevr::haptics::Category::Melee).milliseconds==90,"closing discards unsaved short-pulse choice");
    }
    {auto missing=s;missing.ready=false;Model h;h.Open(missing);h.Click({Kind::Tab,int(Page::Haptics)});
        Check(!h.CanTestHaptics(0)&&!h.CanTestHaptics(-1)&&!h.CanTestHaptics(framevr::haptics::CategoryCount),"unavailable settings cannot preview");}
    {auto values=s.values;values[HapticStrength]=50;
        for(int i=0;i<framevr::haptics::CategoryCount;++i)values[HapticMelee+i]=float(i*10);
        const auto levels=HapticLevels(values);Check(levels.master==50,"master level reaches runtime configuration");
        for(int i=0;i<framevr::haptics::CategoryCount;++i)Check(levels.category[i]==i*10,"each saved category maps to its runtime channel");}
    Check(s.values[MeleeHitSpeed]==12,"melee defaults to twelve m/s");
    {Model melee;melee.Open(s);melee.Click({Kind::Tab,int(Page::Combat)});
        const auto hit=At(Page::Combat,Increase(1));
        Check(hit.index==MeleeHitSpeed&&hit.kind==Kind::Change,"combat speed control hit target");
        melee.Click(hit);Check(melee.Get()[MeleeHitSpeed]==12.25f,"melee speed quarter step");
        const auto request=melee.Click({Kind::Apply});
        Check(request.changed==(1u<<MeleeHitSpeed),"melee mask bit saves only melee speed");
        Check(Normalize(MeleeHitSpeed,NAN)==12&&Normalize(MeleeHitSpeed,0)==1.5f&&Normalize(MeleeHitSpeed,99)==99,"melee speed UI has no upper cap");
        s.values[MeleeHitSpeed]=6;melee.Open(s);melee.Click(hit);
        Check(melee.Get()[MeleeHitSpeed]==6.25f,"plus button goes beyond old cap");
        s.values[MeleeHitSpeed]=12;
    }
    {Model rules;rules.Open(s);rules.Click({Kind::Tab,int(Page::Combat)});
        Check(s.values[MeleeRules]==0&&RowKey(Page::Combat,0)==MeleeRules,"melee rules default to Unleashed, first on the Combat page");
        rules.Click(At(rules,Page::Combat,ValueBox(0)));
        Check(rules.Get()[MeleeRules]==1,"clicking the value switches to Classic");
        const auto request=rules.Click({Kind::Apply});
        Check(request.changed==(ChangeMask{1}<<MeleeRules),"melee rules save only their own setting");
    }
    {Model thrust;thrust.Open(s);thrust.Click({Kind::Tab,int(Page::Combat)});
        const auto hit=At(Page::Combat,Increase(2));
        Check(s.values[MeleeThrustSpeed]==2&&hit.index==MeleeThrustSpeed&&hit.kind==Kind::Change,"thrust has its own Combat row and two m/s default");
        thrust.Click(hit);Check(thrust.Get()[MeleeThrustSpeed]==2.25f,"thrust speed adjusts in quarter steps");
        const auto request=thrust.Click({Kind::Apply});
        Check(request.changed==(ChangeMask{1}<<MeleeThrustSpeed),"thrust edit does not change swing speed or combat rules");
        Check(RowCount(Page::Combat)==8&&RowKey(Page::Combat,3)==ShieldBashSpeed,"eight Combat rows retain independent bash speed; parry window is fixed");
        Check(Normalize(MeleeThrustSpeed,NAN)==2&&Normalize(MeleeThrustSpeed,0)==.75f&&Normalize(MeleeThrustSpeed,99)==10,"thrust speed has finite usable bounds");
    }
    {Model strain;strain.Open(s);strain.Click({Kind::Tab,int(Page::Combat)});
        Check(RowKey(Page::Combat,4)==StrainDark&&std::strcmp(RowLabel(StrainDark,{}),"Guard strain")==0&&s.values[StrainDark]==0,"guard strain colour is the fifth Combat row's left, White by default");
        strain.Click(At(strain,Page::Combat,PairValue(4,0)));
        Check(strain.Get()[StrainDark]==1,"clicking the value switches the strain to Black");
        const auto request=strain.Click({Kind::Apply});
        Check(request.changed==(ChangeMask{1}<<StrainDark),"strain colour saves only its own setting");
    }
    {Model look;look.Open(s);look.Click({Kind::Tab,int(Page::Combat)});
        look.Click(At(look,Page::Combat,PairValue(4,0)));look.Click(At(look,Page::Combat,PairValue(4,0)));
        Check(look.Get()[StrainDark]==2&&std::string(StrainColorNames[2])=="Off","a second click on the value turns the strain light Off");
        look.Click(At(look,Page::Combat,PairValue(4,0)));
        Check(look.Get()[StrainDark]==0,"a third click returns to White");
        look.Click(At(look,Page::Combat,PairDecrease(4,0)));
        Check(look.Get()[StrainDark]==2,"minus from White wraps to Off");
        look.Click(At(look,Page::Combat,PairIncrease(4,0)));
        Check(look.Get()[StrainDark]==0,"plus from Off wraps to White");
        Check(Normalize(StrainDark,2)==2&&Normalize(StrainDark,1)==1&&Normalize(StrainDark,0)==0,"White, Black and Off persist");
        for(float bad:{-1.f,3.f,.5f,NAN})Check(Normalize(StrainDark,bad)==0,"an invalid strain colour uses White");
        Check(HelpChoices(StrainDark)==3&&std::string(HelpChoice(StrainDark,2).name)=="Off","the help card describes Off");
    }
    {Model speed;speed.Open(s);speed.Click({Kind::Tab,int(Page::Combat)});
        Check(s.values[StrainSpeed]==1&&PairOf(RowKey(Page::Combat,4))==StrainSpeed,"guard strain buildup defaults to Medium, on the same row's right");
        for(float bad:{-1.f,3.f,1.5f,NAN})Check(Normalize(StrainSpeed,bad)==1,"invalid buildup preference uses Medium");
        speed.Click(At(speed,Page::Combat,PairDecrease(4,1)));Check(speed.Get()[StrainSpeed]==0,"minus selects Slow");
        speed.Click(At(speed,Page::Combat,PairDecrease(4,1)));Check(speed.Get()[StrainSpeed]==0,"Slow is the lower bound");
        speed.Click(At(speed,Page::Combat,PairIncrease(4,1)));speed.Click(At(speed,Page::Combat,PairIncrease(4,1)));
        Check(speed.Get()[StrainSpeed]==2,"plus selects Fast");
        speed.Click(At(speed,Page::Combat,PairIncrease(4,1)));Check(speed.Get()[StrainSpeed]==2,"Fast is the upper bound");
        Check(speed.Click({Kind::Apply}).changed==(ChangeMask{1}<<StrainSpeed),"buildup edits preserve combat rules, difficulty and colour");
    }
    {Model laser;laser.Open(s);laser.Click({Kind::Tab,int(Page::Aim)});
        const auto& d=Definitions[BowLaser];
        Check(RowKey(Page::Aim,4)==BowLaser&&s.values[BowLaser]==LaserOff&&!LaserOn(s.values),"the bow path laser is last on the Aim page, Off by default");
        Check(std::wstring(d.section)==L"bow"&&std::wstring(d.key)==L"path_laser_hue"&&d.step==15,"one [bow] path_laser_hue value in 15-degree steps");
        laser.Click(At(laser,Page::Aim,Increase(4)));Check(laser.Get()[BowLaser]==0&&LaserOn(laser.Get()),"plus from Off is the first color, red 0 degrees");
        laser.Click(At(laser,Page::Aim,Increase(4)));Check(laser.Get()[BowLaser]==15,"plus steps 15 degrees");
        for(int i=0;i<22;++i)laser.Click(At(laser,Page::Aim,Increase(4)));
        Check(laser.Get()[BowLaser]==345,"the last color is 345 degrees");
        laser.Click(At(laser,Page::Aim,Increase(4)));Check(laser.Get()[BowLaser]==LaserOff&&!LaserOn(laser.Get()),"past the last color comes Off");
        laser.Click(At(laser,Page::Aim,Increase(4)));Check(laser.Get()[BowLaser]==0,"then the first color");
        laser.Click(At(laser,Page::Aim,Decrease(4)));Check(laser.Get()[BowLaser]==LaserOff,"back from the first color is Off");
        laser.Click(At(laser,Page::Aim,Decrease(4)));Check(laser.Get()[BowLaser]==345,"and back from Off the last color");
        for(int i=0;i<25;++i)laser.Click(At(laser,Page::Aim,Increase(4)));
        Check(laser.Get()[BowLaser]==345,"Off and 24 colors make a whole turn");
        laser.Click(At(laser,Page::Aim,ValueBox(4)));Check(laser.Get()[BowLaser]==345,"the value box is no toggle");
        const auto save=laser.Click({Kind::Apply});
        Check(save.changed==(ChangeMask{1}<<BowLaser)&&save.values[BowLaser]==345,"Apply saves only the laser");
        Check(Normalize(BowLaser,-1)==LaserOff&&Normalize(BowLaser,-200)==LaserOff&&Normalize(BowLaser,400)==345&&Normalize(BowLaser,NAN)==LaserOff,
              "any negative saved value is Off; others stay on the wheel");}
    {Model aim;aim.Open(s);aim.Click({Kind::Tab,int(Page::Aim)});
        // The defaults (outline Off, Ground skills Game) leave out the rows that cannot apply.
        Check(RowCount(Page::Aim)==6&&RowKey(Page::Aim,0)==SpellLock&&RowKey(Page::Aim,1)==StaffPoint&&RowKey(Page::Aim,2)==TargetHighlight&&
              RowKey(Page::Aim,3)==GroundAim&&RowKey(Page::Aim,4)==BowLaser&&RowKey(Page::Aim,5)==ThrowLift,
              "by default: the aimed monster, the staff, the outline, ground skills, the laser, the throws");
        Check(s.values[ThrowLift]==10&&std::wstring(Definitions[ThrowLift].key)==L"throw_lift_deg"&&Definitions[ThrowLift].min==-10&&
              Definitions[ThrowLift].max==30&&Definitions[ThrowLift].step==5,"[gameplay] throw_lift_deg: 10 degrees up by default, -10 to 30 in 5s");
        auto lit=s.values;lit[TargetHighlight]=50;lit[GroundAim]=1;
        Check(RowCount(Page::Aim,lit)==9&&RowKey(Page::Aim,3,lit)==AimHighlights&&RowKey(Page::Aim,4,lit)==HighlightLeft&&
              RowKey(Page::Aim,5,lit)==GroundAim&&RowKey(Page::Aim,6,lit)==LandingCircles&&RowKey(Page::Aim,7,lit)==BowLaser,
              "the outline brings its hands and colors, a placed ground skill its circles");
        Check(Paired(HighlightLeft)&&PairOf(HighlightLeft)==HighlightRight&&std::strcmp(RowLabel(HighlightLeft,{}),"Hand colors")==0,"one row for both hands' colors");
        // Every option that changes how a cast aims is Off by default: the casts aim as before the aim rules (#159 review).
        Check(s.values[SpellLock]==0&&s.values[StaffPoint]==0&&s.values[GroundAim]==0&&s.values[LandingCircles]==1,
              "aim options Off (Game) by default; circles On for when they apply");
        Check(std::wstring(Definitions[GroundAim].key)==L"ground_aim"&&Definitions[GroundAim].max==2&&std::strcmp(GroundAimNames[0],"Game")==0&&
              std::strcmp(GroundAimNames[1],"Where you point")==0&&std::strcmp(GroundAimNames[2],"Aimed monster")==0,"one [gameplay] ground_aim row: Game, Where you point, Aimed monster");
        Check(HelpAt(Page::Aim,(ValueBox(3).x+5.f)/Width,(ValueBox(3).y+5.f)/Height)==GroundAim&&
              HelpAt(Page::Aim,(ValueBox(6).x+5.f)/Width,(ValueBox(6).y+5.f)/Height,false,&lit)==LandingCircles,"the ground skills and circles rows explain themselves");
        Check(HelpChoices(GroundAim)==3&&std::strcmp(HelpChoice(GroundAim,1).name,"Where you point")==0,"the ground skills row names its three choices");
        {auto off=s.values;off[TargetHighlight]=0;
            Check(RowEnabled(off,GroundAim)&&!RowEnabled(off,LandingCircles)&&!RowShown(LandingCircles,off,{}),"no circles while the game aims ground skills");
            off[GroundAim]=1;Check(RowEnabled(off,LandingCircles)&&RowShown(LandingCircles,off,{}),"and back once the mod places them");}
        {Model ground;ground.Open(s);ground.Click({Kind::Tab,int(Page::Aim)});
            ground.Click(At(ground,Page::Aim,Increase(3)));Check(ground.Get()[GroundAim]==1&&ground.Rows()[4]==LandingCircles,"plus: Where you point, and its circles row");
            ground.Click(At(ground,Page::Aim,Increase(3)));Check(ground.Get()[GroundAim]==2,"then Aimed monster");
            ground.Click(At(ground,Page::Aim,Increase(3)));Check(ground.Get()[GroundAim]==0&&ground.Rows().count==6,"and round to Game, without circles");
            ground.Click(At(ground,Page::Aim,Decrease(3)));Check(ground.Get()[GroundAim]==2,"minus from Game: Aimed monster");
            Check(ground.Click({Kind::Apply}).changed==(ChangeMask{1}<<GroundAim),"Apply saves only the ground row");
            Check(Normalize(GroundAim,1.5f)==0&&Normalize(GroundAim,3)==0&&Normalize(GroundAim,NAN)==0&&Normalize(GroundAim,2)==2,"a malformed ground_aim reads as Game");}
        Check(HelpAt(Page::Aim,(ValueBox(1).x+5.f)/Width,(ValueBox(1).y+5.f)/Height)==StaffPoint&&HelpAt(Page::Aim,(ValueBox(0).x+5.f)/Width,(ValueBox(0).y+5.f)/Height)==SpellLock,"the spell and staff rows explain themselves");
        Check(s.values[TargetHighlight]==0,"the monster outline is Off by default");
        Check(s.values[AimHighlights]==1&&s.values[HighlightRight]==45&&s.values[HighlightLeft]==210,"both hands; gold right, blue left by default");
        Check(Normalize(HighlightLeft,200)==195&&Normalize(HighlightLeft,203)==210&&Normalize(HighlightRight,-5)==0&&Normalize(HighlightRight,400)==360&&Normalize(HighlightRight,NAN)==45,"colors snap to 15 degrees, 0 (By range) to 360");
        Check(!RowShown(AimHighlights,s.values,{})&&!RowShown(HighlightLeft,s.values,{}),"the outline's rows are left out while it is Off");
        Check(HelpChoices(AimHighlights)==2&&std::strcmp(HelpChoice(AimHighlights,0).name,"Dominant hand")==0&&std::strcmp(HelpChoice(AimHighlights,1).name,"Both hands")==0,"the highlights row names its two choices");
        {Model colors;auto on=s;on.values[TargetHighlight]=50;colors.Open(on);colors.Click({Kind::Tab,int(Page::Aim)});
            colors.Click(At(colors,Page::Aim,ValueBox(3)));Check(colors.Get()[AimHighlights]==0,"selecting the highlights value switches to Dominant hand");
            colors.Click(At(colors,Page::Aim,ValueBox(3)));Check(colors.Get()[AimHighlights]==1,"and back to Both hands");
            for(int i=0;i<30;++i)colors.Click(At(colors,Page::Aim,PairIncrease(4,1)));
            Check(colors.Get()[HighlightRight]==360&&colors.Get()[HighlightLeft]==210,"the right stepper turns the right hand's wheel to 360 degrees, alone");
            for(int i=0;i<30;++i)colors.Click(At(colors,Page::Aim,PairDecrease(4,1)));
            Check(colors.Get()[HighlightRight]==0,"and back to By range");
            colors.Click(At(colors,Page::Aim,PairIncrease(4,1)));Check(colors.Get()[HighlightRight]==15,"the first hue past By range");
            colors.Click(At(colors,Page::Aim,PairIncrease(4,0)));Check(colors.Get()[HighlightLeft]==225,"the left stepper is the left hand's");
            Check(HelpAt(Page::Aim,(PairValue(4,0).x+5.f)/Width,(PairValue(4,0).y+5.f)/Height,false,&colors.Get())==HighlightLeft&&
                  HelpAt(Page::Aim,(PairValue(4,1).x+5.f)/Width,(PairValue(4,1).y+5.f)/Height,false,&colors.Get())==HighlightRight,"each side explains its own hand");
            float rgb[3];HueRgb(45,rgb);Check(rgb[0]==1&&rgb[1]==.75f&&rgb[2]==0,"45 degrees is gold");}
        Check(HelpAt(Page::Aim,(ValueBox(2).x+5.f)/Width,(ValueBox(2).y+5.f)/Height)==TargetHighlight,"the outline row explains itself");
        aim.Click(At(aim,Page::Aim,Increase(2)));Check(aim.Get()[TargetHighlight]==10&&aim.Rows().count==8,"plus turns the outline on at 10%, with its rows");
        aim.Click(At(aim,Page::Aim,Decrease(2)));aim.Click(At(aim,Page::Aim,Decrease(2)));Check(aim.Get()[TargetHighlight]==0,"Off is the lower bound");
        aim.Click(At(aim,Page::Aim,Increase(2)));aim.Click(At(aim,Page::Aim,Increase(2)));
        const auto request=aim.Click({Kind::Apply});
        Check(request.changed==(ChangeMask{1}<<TargetHighlight)&&request.values[TargetHighlight]==20,"the outline strength changes nothing else");
    }
    {   // Skills go to the aimed monster: one row for spells and bow skills
        Check(std::wstring(Definitions[SpellLock].key)==L"spell_lock"&&Definitions[SpellLock].max==3&&HelpChoices(SpellLock)==4&&
              std::strcmp(HelpChoice(SpellLock,2).name,"Bow skills")==0,"spell_lock: Off, Spells, Bow skills, Both");
        Check(!LockSpells(0)&&LockSpells(1)&&!LockBowSkills(1)&&LockBowSkills(2)&&LockSpells(3)&&LockBowSkills(3),"bit 1 spells, bit 2 bow skills");
        Check(Normalize(SpellLock,4)==0&&Normalize(SpellLock,1.5f)==0&&Normalize(SpellLock,NAN)==0&&Normalize(SpellLock,3)==3,"a malformed spell_lock is Off");
        Model amazon;amazon.Open(s);amazon.SetContext({heroclass::Amazon});amazon.Click({Kind::Tab,int(Page::Aim)});
        for(float want:{1.f,2.f,3.f,0.f}){amazon.Click(At(amazon,Page::Aim,Increase(0)));Check(amazon.Get()[SpellLock]==want,"an Amazon steps Off, Spells, Bow skills, Both");}
        amazon.Click(At(amazon,Page::Aim,Decrease(0)));Check(amazon.Get()[SpellLock]==3,"minus from Off: Both");
        amazon.Click(At(amazon,Page::Aim,ValueBox(0)));Check(amazon.Get()[SpellLock]==0,"the value box steps on too");
        Check(std::strcmp(RowLabel(SpellLock,amazon.context),"Skills go to the aimed monster")==0,"an Amazon's row: skills");
        auto bows=s;bows.values[SpellLock]=2;
        Model sorc;sorc.Open(bows);sorc.SetContext({heroclass::Sorceress});sorc.Click({Kind::Tab,int(Page::Aim)});
        Check(std::strcmp(RowLabel(SpellLock,sorc.context),"Spells go to the aimed monster")==0,"a class without bow skills sees its spells only");
        sorc.Click(At(sorc,Page::Aim,Increase(0)));Check(sorc.Get()[SpellLock]==3,"its plus turns spells on and keeps the Amazon's bow skills");
        sorc.Click(At(sorc,Page::Aim,ValueBox(0)));Check(sorc.Get()[SpellLock]==2,"and the value box off again");
    }
    {   // Rows follow the character: another class's or weapon's are left out; unknown (the menus) shows them all
        const auto v=Defaults();
        Check(RowShown(ElementalHands,v,{})&&RowShown(ElementalHands,v,{heroclass::Sorceress})&&!RowShown(ElementalHands,v,{heroclass::Amazon}),"elemental hands: the Sorceress's");
        Check(RowCount(Page::Video,v,{heroclass::Barbarian})==3&&RowCount(Page::Video,v,{heroclass::Sorceress})==4,"the Video page drops them for others");
        for(int k:{int(heroclass::Sorceress),int(heroclass::Assassin),int(heroclass::Druid),int(heroclass::Necromancer)})
            Check(RowShown(GroundAim,v,{k}),"ground skills for the classes that have them");
        for(int k:{int(heroclass::Amazon),int(heroclass::Barbarian),int(heroclass::Paladin)})Check(!RowShown(GroundAim,v,{k}),"none for those that do not");
        auto placed=v;placed[GroundAim]=1;
        Check(!RowShown(LandingCircles,placed,{heroclass::Paladin})&&RowShown(LandingCircles,placed,{heroclass::Druid}),"circles go with them");
        Check(RowShown(BowLaser,v,{heroclass::Amazon})&&!RowShown(BowLaser,v,{heroclass::Paladin})&&RowShown(BowLaser,v,{heroclass::Paladin,true}),
              "the bow laser: an Amazon's, or any bow in hand");
        Check(RowShown(StaffPoint,v,{heroclass::Druid})&&!RowShown(StaffPoint,v,{heroclass::Assassin})&&RowShown(StaffPoint,v,{heroclass::Assassin,false,true}),
              "staff aim: the Sorceress's and Druid's, or a staff in hand");
        Check(RowCount(Page::Aim,v,{heroclass::Barbarian})==3&&RowKey(Page::Aim,0,v,{heroclass::Barbarian})==SpellLock&&
              RowKey(Page::Aim,1,v,{heroclass::Barbarian})==TargetHighlight&&RowKey(Page::Aim,2,v,{heroclass::Barbarian})==ThrowLift,
              "a Barbarian's Aim page: the aimed monster, the outline and the throws");
        Check(RowShown(ThrowLift,v,{heroclass::Amazon})&&!RowShown(ThrowLift,v,{heroclass::Sorceress})&&RowShown(ThrowLift,v,{heroclass::Sorceress,false,false,true}),
              "throw lift: the Amazon's and Barbarian's, or a thrown weapon in hand");
        Model druid;druid.Open(s);druid.SetContext({heroclass::Druid});druid.Click({Kind::Tab,int(Page::Aim)});
        const auto rows=druid.Rows();Check(rows.count==4&&rows[1]==StaffPoint&&rows[3]==GroundAim,"a Druid: staff aim and ground skills, no laser");
        const auto revision=druid.revision;druid.SetContext({heroclass::Druid});Check(druid.revision==revision,"the same character repaints nothing");
        druid.SetContext({heroclass::Druid,true});Check(druid.revision!=revision&&druid.Rows().count==5,"a bow in hand brings the laser");
        Check(PairDecrease(0,0).x==PairLeft&&PairIncrease(0,1).x+PairIncrease(0,1).w==1240&&PairIncrease(0,0).x+PairIncrease(0,0).w<PairDecrease(0,1).x,
              "paired steppers fit the value column side by side");
    }
    {   // Rows that only matter with another row's choice
        auto v=Defaults();
        Check(RowShown(Snap,v,{})&&!RowShown(TurnSpeed,v,{}),"snap turning: its angle, not the smooth speed");
        v[SmoothTurn]=1;Check(!RowShown(Snap,v,{})&&RowShown(TurnSpeed,v,{}),"smooth turning: its speed");
        v[Seated]=1;Check(!RowShown(Waist,v,{}),"seated: no standing belt drop");
        v[Foveation]=0;Check(!RowShown(FoveationStrength,v,{}),"no foveation strength while it is Off");
        v[BloodVisor]=0;Check(!RowShown(BloodWipe,v,{}),"no face wipe without blood");
        Check(RowCount(Page::VR,v)==8&&RowCount(Page::Performance,v)==7&&RowCount(Page::HUD,v)==9,"each page drops what cannot apply");
    }
    {   // Windows Mono audio: the title row warns on every page; pointing at it explains
        const float u=(MonoAudioBox.x+20.f)/Width,v=(MonoAudioBox.y+20.f)/Height;
        for(int page=0;page<TabCount;++page)
            Check(HelpAt(Page(page),u,v,false,nullptr,true)==HelpMonoAudio,"the Mono audio warning explains itself on every page");
        Check(HelpAt(Page::Video,u,v)==NoHelp,"no Mono audio help while it is off");
        Check(std::string(HelpTitle(HelpMonoAudio))=="Windows Mono audio"&&std::string(HelpText(HelpMonoAudio)).find("Accessibility > Audio")!=std::string::npos,
              "the help says where to turn it off");
        Check(!MonoAudioBox.Contains(float(TabBox(0).x),float(TabBox(0).y))&&MonoAudioBox.y+MonoAudioBox.h<=TabBox(0).y,"the warning stays above the tabs");
    }
    {Model heart;heart.Open(s);heart.Click({Kind::Tab,int(Page::Combat)});
        Check(s.values[HeartbeatStart]==60&&RowKey(Page::Combat,6)==HeartbeatStart,"heartbeat start is the seventh Combat row, 60% by default");
        Check(ValueBox(6).y+ValueBox(6).h<744,"heartbeat row avoids Combat notes");
        Check(Normalize(HeartbeatStart,NAN)==60&&Normalize(HeartbeatStart,45)==50&&Normalize(HeartbeatStart,-5)==0&&Normalize(HeartbeatStart,100)==90,"heartbeat start snaps to 10% steps, 0-90");
        heart.Click(At(heart,Page::Combat,Decrease(6)));Check(heart.Get()[HeartbeatStart]==50,"minus lowers the start by 10%");
        for(int i=0;i<6;++i)heart.Click(At(heart,Page::Combat,Decrease(6)));
        Check(heart.Get()[HeartbeatStart]==0,"Off is the lower bound");
        for(int i=0;i<10;++i)heart.Click(At(heart,Page::Combat,Increase(6)));
        Check(heart.Get()[HeartbeatStart]==90,"90% is the upper bound");
        Check(heart.Click({Kind::Apply}).changed==(ChangeMask{1}<<HeartbeatStart),"heartbeat start saves only its own setting");
    }
    {Model sight;sight.Open(s);sight.Click({Kind::Tab,int(Page::Combat)});
        Check(s.values[EnemySight]==100&&RowKey(Page::Combat,7)==EnemySight,"enemy sight distance is the eighth Combat row, 100% by default");
        Check(ValueBox(7).y+ValueBox(7).h<804,"enemy sight row avoids Combat footer");
        Check(Normalize(EnemySight,NAN)==100&&Normalize(EnemySight,145)==150&&Normalize(EnemySight,50)==100&&Normalize(EnemySight,350)==300,"enemy sight snaps to 10% steps, 100-300");
        sight.Click(At(sight,Page::Combat,Decrease(7)));Check(sight.Get()[EnemySight]==100,"100% is the lower bound");
        sight.Click(At(sight,Page::Combat,Increase(7)));Check(sight.Get()[EnemySight]==110,"plus raises enemy sight by 10%");
        for(int i=0;i<25;++i)sight.Click(At(sight,Page::Combat,Increase(7)));
        Check(sight.Get()[EnemySight]==300,"300% is the upper bound");
        Check(sight.Click({Kind::Apply}).changed==(ChangeMask{1}<<EnemySight),"enemy sight saves only its own setting");
    }
    {Model difficulty;difficulty.Open(s);difficulty.Click({Kind::Tab,int(Page::Combat)});
        Check(s.values[OfflinePlayers]==0&&RowKey(Page::Combat,5)==OfflinePlayers,"difficulty preserves native settings by default");
        Check(ValueBox(5).y+ValueBox(5).h<698,"difficulty avoids Combat notes");
        Check(Normalize(OfflinePlayers,NAN)==0&&Normalize(OfflinePlayers,9)==0&&Normalize(OfflinePlayers,2.5f)==0,"invalid offline counts fail back to native");
        for(int i=1;i<=8;++i){difficulty.Click(At(difficulty,Page::Combat,Increase(5)));Check(difficulty.Get()[OfflinePlayers]==i,"increase difficulty through every native count");}
        difficulty.Click(At(difficulty,Page::Combat,Increase(5)));Check(difficulty.Get()[OfflinePlayers]==8,"upper bound does not wrap");
        const auto request=difficulty.Click({Kind::Apply});
        Check(request.changed==(ChangeMask{1}<<OfflinePlayers),"difficulty does not alter melee rules or speeds");
        auto saved=s;saved.sequence=request.sequence;saved.result=Result::Saved;saved.values=request.values;
        difficulty.Observe(saved);
        for(int i=7;i>=0;--i){difficulty.Click(At(difficulty,Page::Combat,Decrease(5)));Check(difficulty.Get()[OfflinePlayers]==i,"decrease through native default");}
        difficulty.Click(At(difficulty,Page::Combat,Decrease(5)));Check(difficulty.Get()[OfflinePlayers]==0,"native default is lower bound");
        const auto draft=difficulty.Get();saved.offlineDifficultyAvailable=true;difficulty.Observe(saved);
        Check(difficulty.offlineDifficultyAvailable&&difficulty.Get()==draft,"capability updates preserve unsaved edits");
    }
    {Model bash;bash.Open(s);bash.Click({Kind::Tab,int(Page::Combat)});
        Check(s.values[ShieldBashSpeed]==2&&At(Page::Combat,Increase(3)).index==ShieldBashSpeed,"shield bash has its own speed row");
        bash.Click(At(bash,Page::Combat,Increase(3)));
        Check(bash.Get()[ShieldBashSpeed]==2.25f&&bash.Click({Kind::Apply}).changed==(ChangeMask{1}<<ShieldBashSpeed),"bash speed edits only its own setting");
        Check(Normalize(ShieldBashSpeed,NAN)==2&&Normalize(ShieldBashSpeed,0)==.75f&&Normalize(ShieldBashSpeed,99)==10,"bash speed UI matches contact bounds");
    }
    Check(s.values[MenuDistance]==1&&s.values[TooltipTiming]==0,"comfortable Medium distance and Instant hover defaults");
    for(auto key:{MenuDistance,TooltipTiming}) {
        Check(Normalize(key,1.5f)==Definitions[key].defaultValue&&Normalize(key,9)==Definitions[key].defaultValue,"invalid comfort enums fall back safely");
        const int row=key==MenuDistance?4:5;
        const Rect plus=key==MenuDistance?PairIncrease(row,0):Increase(row);   // Menu distance: the Game menus row's left stepper
        Check(RowKey(Page::HUD,row)==key&&At(Page::HUD,plus).index==key,"HUD comfort rows hit their own settings");
        Model comfort;comfort.Open(s);comfort.Click(At(comfort,Page::HUD,plus));
        Check(comfort.Click({Kind::Apply}).changed==(1u<<key),"comfort Apply affects only its selected setting");
    }
    Check(s.values[MenuInput]==0&&PairOf(MenuDistance)==MenuInput&&std::strcmp(RowLabel(MenuDistance,{}),"Game menus")==0,"menu pointer defaults to Auto, on the Game menus row's right");
    for(float bad:{-1.f,3.f,1.5f,NAN})Check(Normalize(MenuInput,bad)==0,"invalid menu pointer falls back to Auto");
    {Model pointer;pointer.Open(s);
        Check(At(pointer,Page::HUD,PairIncrease(4,1)).index==MenuInput&&At(pointer,Page::HUD,PairDecrease(4,1)).index==MenuInput,"the right stepper sets the menu pointer");
        // Cycles both ways, like the other three-choice rows (aim assist, guard strain color, ground skills).
        for(float choice:{1.f,2.f,0.f,1.f}){pointer.Click(At(pointer,Page::HUD,PairIncrease(4,1)));Check(pointer.Get()[MenuInput]==choice,"menu pointer steps Auto, Laser, Controller, then Auto");}
        for(float choice:{0.f,2.f}){pointer.Click(At(pointer,Page::HUD,PairDecrease(4,1)));Check(pointer.Get()[MenuInput]==choice,"minus from Auto wraps to Controller");}
        Check(pointer.Get()[MenuDistance]==s.values[MenuDistance]&&pointer.Click({Kind::Apply}).changed==(ChangeMask{1}<<MenuInput),"the pointer edits only its own setting");
        Check(HelpAt(Page::HUD,1100.f/Width,float(RowY+4*RowStep+20)/Height)==MenuInput&&HelpChoices(MenuInput)==3,"its half of the row explains each choice");}
    Check(s.values[ProjectileAimAssist]==2,"aim assist defaults to Strong");
    for(float bad:{-1.f,3.f,1.5f,NAN})Check(Normalize(ProjectileAimAssist,bad)==2,"invalid aim-assist preference falls back to Strong");
    {Model assist;assist.Open(s);const auto hit=At(Page::Controls,AimAssistBox);
        Check(hit.kind==Kind::Change&&hit.index==ProjectileAimAssist,"controls tab exposes aim assist");
        for(float choice:{0.f,1.f,2.f,0.f,1.f}){assist.Click(hit);Check(assist.Get()[ProjectileAimAssist]==choice,"aim assist cycles Off Light Strong");}
        auto request=assist.Click({Kind::Apply});
        Check(request.changed==(1u<<ProjectileAimAssist)&&request.values[ProjectileAimAssist]==1,"assist saves independently");
        assist.Click(hit);Check(assist.Get()[ProjectileAimAssist]==1,"assist cannot change while saving");
        assist.Close();assist.Open(s);Check(assist.Get()[ProjectileAimAssist]==2,"discarding edits restores saved assistance");
        Check(AimAssistBox.y>=PlanarSpellAimBox.y+PlanarSpellAimBox.h&&AimAssistBox.y+AimAssistBox.h<845,"assist avoids planar control and footer");}
    Check(s.values[OrdinaryMobBars]==0,"ordinary mob bars are off by default");
    {Model bars;bars.Open(s);bars.Click({Kind::Tab,int(Page::VR)});
        const auto hit=At(Page::VR,ValueBox(8));   // snap turning: no smooth speed row
        Check(hit.kind==Kind::Change&&hit.index==OrdinaryMobBars,"VR tab exposes ordinary mob toggle");
        Check(RowY+9*RowStep+72<876,"last row leaves room for save status");
        bars.Click(hit);auto request=bars.Click({Kind::Apply});
        Check(request.changed==(1u<<OrdinaryMobBars)&&request.values[OrdinaryMobBars]==1,"toggle saves only mob visibility");}
    Check(s.values[SmoothTurn]==0&&s.values[Snap]==30,"snap turning remains the default");
    Check(EffectiveSnapAngle(30,-1)==30&&EffectiveSnapAngle(0,-1)==0,"legacy snap and smooth profiles retain their mode");
    Check(EffectiveSnapAngle(45,1)==0&&EffectiveSnapAngle(45,0)==45,"explicit toggle selects camera mode without losing angle");
    Check(EffectiveSnapAngle(0,0)==30,"turning off legacy smooth mode restores default snap angle");
    Check(SnapDegrees(NAN)==30&&SnapDegrees(-20)==15&&SnapDegrees(999)==180,"camera and menu share snap bounds");
    {Model turning;turning.Open(s);
        turning.Click(At(turning,Page::VR,ValueBox(3)));
        Check(turning.Get()[SmoothTurn]==1&&turning.Get()[Snap]==30,"value-box click enables smooth turning independently");
        turning.Click({Kind::Close});turning.Open(s);
        Check(turning.Get()[SmoothTurn]==0&&!turning.Dirty(),"closing without Apply discards smooth toggle");
        turning.Click(At(turning,Page::VR,ValueBox(3)));
        auto request=turning.Click({Kind::Apply});
        Check(request.changed==(1u<<SmoothTurn),"Apply saves only the turning mode");
        turning.Click(At(turning,Page::VR,ValueBox(3)));
        Check(turning.Get()[SmoothTurn]==1,"toggle cannot change while saving");
        auto saved=s;saved.values=request.values;saved.sequence=request.sequence;saved.result=Result::Saved;
        turning.Observe(saved);turning.Close();turning.Open(saved);
        turning.Click(At(turning,Page::VR,ValueBox(3)));
        Check(turning.Get()[SmoothTurn]==0&&turning.Get()[Snap]==30,"reopened toggle restores snap turning");
    }
    Check(s.values[NavigationLocation]==0,"combined navigation defaults to Wrist");
    Check(Normalize(NavigationLocation,99)==0&&Normalize(NavigationLocation,1.5f)==0,"invalid navigation location falls back to Wrist");
    {Model navigation;navigation.Open(s);navigation.Click(At(navigation,Page::Controls,Tabs[int(Page::HUD)]));
        const int row=3;
        Check(RowCount(Page::HUD)==10&&RowKey(Page::HUD,row)==NavigationLocation,"one navigation row replaces separate map/compass switches");
        for(int location=1;location<=3;++location){
            navigation.Click(At(navigation,Page::HUD,Increase(row)));
            Check(navigation.Get()[NavigationLocation]==location,"navigation offers Palm, Either and Off");
        }
        auto request=navigation.Click({Kind::Apply});
        Check(request.changed==(1u<<NavigationLocation)&&request.values[NavigationLocation]==3&&request.values[VitalsLocation]==0,
            "combined navigation can turn off without changing vitals");}
    Check(s.values[VitalsLocation]==0&&s.values[CriticalAlerts]==0,"existing wrist placement remains default; automatic alerts require opt-in");
    Check(Normalize(VitalsLocation,99)==0&&Normalize(VitalsLocation,1.5f)==0,"unknown vitals locations fall back to wrist");
    {Model hud;hud.Open(s);hud.Click(At(hud.page,Tabs[int(Page::HUD)]));
        hud.Click(At(hud.page,Increase(0)));
        Check(hud.Get()[VitalsLocation]==1&&!hud.Get()[CriticalAlerts],"Palm selection does not enable automatic alerts");
        auto request=hud.Click({Kind::Apply});
        Check(request.changed==(1u<<VitalsLocation),"palm placement saves independently");
        hud.Open(s);hud.Click(At(hud,Page::HUD,ValueBox(2)));request=hud.Click({Kind::Apply});
        Check(request.changed==(1u<<CriticalAlerts)&&request.values[CriticalAlerts]==1,"alert opt-in saves independently");}
    Check(s.values[BloodVisor]==1,"blood on visor defaults on");
    {Model blood;blood.Open(s);blood.Click({Kind::Tab,int(Page::HUD)});
        const auto hit=At(Page::HUD,ValueBox(6));
        Check(RowKey(Page::HUD,6)==BloodVisor&&hit.kind==Kind::Change&&hit.index==BloodVisor,"HUD tab exposes the blood toggle");
        blood.Click(hit);auto request=blood.Click({Kind::Apply});
        Check(request.changed==(ChangeMask{1}<<BloodVisor)&&request.values[BloodVisor]==0,"blood toggle saves independently");}
    Check(s.values[BloodWipe]==1,"face wipe gesture defaults on");
    {Model wipe;wipe.Open(s);wipe.Click({Kind::Tab,int(Page::HUD)});
        const auto hit=At(Page::HUD,ValueBox(7));
        Check(RowKey(Page::HUD,7)==BloodWipe&&hit.kind==Kind::Change&&hit.index==BloodWipe,"face wipe toggle sits under the blood toggle");
        Check(RowY+7*RowStep+RowStep<=742,"eighth HUD row clears the help text");
        wipe.Click(hit);auto request=wipe.Click({Kind::Apply});
        Check(request.changed==(ChangeMask{1}<<BloodWipe)&&request.values[BloodWipe]==0&&request.values[BloodVisor]==1,"wipe toggle saves without touching the blood toggle");}
    Check(s.values[VitalsTilt]==0,"vitals tilt keeps the original angle by default");
    Check(Normalize(VitalsTilt,NAN)==0&&Normalize(VitalsTilt,INFINITY)==0&&
        Normalize(VitalsTilt,-999)==-60&&Normalize(VitalsTilt,999)==60,"tilt rejects nonfinite input and clamps at sixty degrees");
    {Model tilt;tilt.Open(s);tilt.Click({Kind::Tab,int(Page::HUD)});
        Check(RowKey(Page::HUD,1)==VitalsTilt&&At(Page::HUD,Increase(1)).index==VitalsTilt&&
            At(Page::HUD,Decrease(1)).index==VitalsTilt,"tilt controls sit next to vitals placement");
        tilt.Click(At(tilt,Page::HUD,Increase(1)));Check(tilt.Get()[VitalsTilt]==5,"plus tilts five degrees");
        const auto request=tilt.Click({Kind::Apply});
        Check(request.changed==(ChangeMask{1}<<VitalsTilt)&&!(request.changed&ChangeMask{UINT32_MAX}),
            "Apply preserves the new setting bit above the previous 32-bit mask");
        Check(request.values[VitalsLocation]==0&&request.values[NavigationLocation]==0&&request.values[CriticalAlerts]==0,
            "tilt saves independently of placement, navigation and alerts");
        auto external=s.values;external[VitalsTilt]=-5;Check(Conflict(external,request.base,request.changed),"high-bit tilt conflicts are detected");
        external=s.values;external[NavigationLocation]=1;Check(!Conflict(external,request.base,request.changed),"navigation edits do not conflict with tilt");
        tilt.Open(s);tilt.Click(At(tilt,Page::HUD,Decrease(1)));Check(tilt.Get()[VitalsTilt]==-5,"minus tilts in the opposite direction");
        tilt.Close();tilt.Open(s);Check(tilt.Get()[VitalsTilt]==0&&!tilt.Dirty(),"Close discards unapplied tilt");
        for(int i=0;i<30;++i)tilt.Click(At(tilt,Page::HUD,Increase(1)));
        Check(tilt.Get()[VitalsTilt]==60,"tilt upper limit clamps");
        for(int i=0;i<60;++i)tilt.Click(At(tilt,Page::HUD,Decrease(1)));
        Check(tilt.Get()[VitalsTilt]==-60,"tilt lower limit clamps");}
    Check(s.values[PlanarSpellAim]==0,"spell aiming defaults to free vertical aim");
    {Model aim;aim.Open(s);const auto hit=At(Page::Controls,PlanarSpellAimBox);
        Check(hit.kind==Kind::Change&&hit.index==PlanarSpellAim,"controls tab exposes planar aiming toggle");
        aim.Click(hit);Check(s.values[PlanarSpellAim]==0&&aim.Get()[PlanarSpellAim]==1,"aim change is a draft");
        const auto request=aim.Click({Kind::Apply});
        Check(request.changed==(1u<<PlanarSpellAim)&&request.values[PlanarSpellAim]==1,"planar aim saves only its preference");
        Check(!WeaponIndicatorsBox.Contains(PlanarSpellAimBox.x,PlanarSpellAimBox.y)&&
              PlanarSpellAimBox.y+PlanarSpellAimBox.h<876,"aim toggle avoids other controls and footer");}
    Check(s.values[WeaponSkillIndicators]==0,"weapon skill indicators default off");
    {Model indicators;indicators.Open(s);const auto hit=At(Page::Controls,WeaponIndicatorsBox);
        Check(hit.kind==Kind::Change&&hit.index==WeaponSkillIndicators,"controls tab exposes weapon indicator toggle");
        indicators.Click(hit);auto request=indicators.Click({Kind::Apply});
        Check(request.changed==(1u<<WeaponSkillIndicators)&&request.values[WeaponSkillIndicators]==1,"indicator opt-in saves only its preference");}
    Check(s.values[ControllerLabels]==0&&Normalize(ControllerLabels,99)==0&&Normalize(ControllerLabels,1.5f)==0,"unknown controller preferences use automatic");
    {Model chooser;chooser.Open(s);chooser.Click({Kind::Tab,int(Page::VR)});
        auto hit=[&](Rect r){return HitTest(chooser.page,(r.x+r.w*.5f)/Width,(r.y+r.h*.5f)/Height,chooser.controllerMenu);};
        chooser.Click(hit(ControllerBox));Check(chooser.controllerMenu&&!chooser.Dirty(),"dropdown opens without changing selection");
        chooser.Click(hit(ControllerOption(2)));
        Check(!chooser.controllerMenu&&chooser.Get()[ControllerLabels]==2&&chooser.Dirty(),"Index option changes draft only");
        chooser.Click(hit(ControllerBox));
        const auto covered=hit(ValueBox(0));
        Check(covered.kind==Kind::ControllerChoice,"dropdown options own covered setting rows");
        const auto outside=hit({58,RowY,100,40});
        Check(outside.kind==Kind::DismissMenu,"open dropdown consumes outside clicks instead of changing rows");
        chooser.Click(outside);Check(!chooser.controllerMenu&&chooser.Get()[Motion]==0,"outside click only dismisses");
        chooser.Click(hit(ControllerBox));Check(chooser.DismissControllerMenu()&&chooser.open,"Back dismisses dropdown first");
        auto pending=chooser.Click({Kind::Apply});
        Check(pending.changed==(1u<<ControllerLabels)&&pending.values[ControllerLabels]==2,"Apply queues preference alone");
        chooser.Click(hit(ControllerBox));Check(!chooser.controllerMenu,"cannot edit while saving");
        chooser.Close();chooser.Open(s);Check(chooser.Get()[ControllerLabels]==0&&!chooser.controllerMenu,"reopen discards unsaved choice");
    }
    Check(s.values[DrawDistance]==2&&s.values[ObjectDistance]==210,"distance fallbacks match release configuration: objects just past the fog's end");
    {Model distance;distance.Open(s);
        distance.Click({Kind::Change,ObjectDistance,-1});
        Check(distance.Get()[ObjectDistance]==160,"object distance steps by 50, never jumping to the minimum");
        distance.Click({Kind::Change,ObjectDistance,1});distance.Click({Kind::Change,ObjectDistance,1});
        Check(distance.Get()[ObjectDistance]==260,"and back up past the default");}
    Model m;m.Open(s);
    Check(m.open&&m.ready&&!m.Dirty()&&m.page==Page::Controls,"open actual configuration");
    Check(HitTest(Page::VR,-.01f,.5f).kind==Kind::None&&HitTest(Page::VR,NAN,.5f).kind==Kind::None,"invalid laser coordinates ignored");
    Check(At(Page::VR,Calibrate).kind==Kind::Calibrate&&At(Page::VR,Recenter).kind==Kind::Recenter,"separate calibration and recenter buttons");
    Check(Normalize(BeltForward,.06f)==.06f,"settings preserve the closer belt placement");
    for(int p=0;p<TabCount;++p){
        auto hit=At(Page(p),Tabs[p]);Check(hit.kind==Kind::Tab&&hit.index==p,"tab rectangle matches input");
        for(int row=0;row<RowCount(Page(p));++row){
            const Key key=RowKey(Page(p),row);const bool paired=Paired(key);
            auto plus=At(Page(p),paired?PairIncrease(row,0):Increase(row)),minus=At(Page(p),paired?PairDecrease(row,0):Decrease(row));
            Check(plus.kind==Kind::Change&&plus.index==key&&plus.delta==1&&minus.delta==-1,"row controls match drawing");
            if(paired)Check(At(Page(p),PairIncrease(row,1)).index==PairOf(key)&&At(Page(p),PairDecrease(row,1)).index==PairOf(key),"a paired row's right stepper is its right setting");
        }
    }
    m.Click(At(m,Page::Controls,Tabs[1]));
    m.Click(At(m.page,ValueBox(0)));
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
    InputGate gate;
    auto e=gate.Step(true,true,1,true);Check(!e.chord&&!e.click&&!e.cancel,"initial held controls swallowed");
    gate.Step(true,false,0,false);e=gate.Step(true,true,0,false);Check(e.chord&&!e.click,"fresh chord opens");
    gate.Consume();Check(!gate.Step(true,true,1,false).click,"opening consumes trigger already held");
    gate.Step(true,true,0,false);Check(gate.Step(true,true,.8f,false).click,"fresh press clicks");
    Check(!gate.Step(true,true,.4f,false).click&&!gate.Step(true,true,.8f,false).click,"trigger noise cannot repeat click");
    gate.Step(false,true,1,true);e=gate.Step(true,true,1,true);
    Check(!e.chord&&!e.click&&!e.cancel,"tracking return cannot replay held inputs");
    gate.Step(true,false,0,false);Check(gate.Step(true,false,0,true).cancel,"fresh B cancels");
    Check(Defaults()[CompanionCounts]==1&&RowKey(Page::HUD,9)==CompanionCounts,"companion counts default on in HUD");
    s.ready=true;s.values=Defaults();m.Open(s);m.Click({Kind::Tab,int(Page::HUD)});
    m.Click(At(m,Page::HUD,ValueBox(9)));
    Check(m.Get()[CompanionCounts]==0&&m.Get()[NavigationLocation]==0,"companion toggle is independent of map placement");
    Check(m.Click({Kind::Apply}).changed==(ChangeMask{1}<<CompanionCounts),"companion toggle uses its own high change-mask bit");
    std::puts("VR settings draft, persistence requests, conflicts, hit regions and input-gating tests passed");
}
