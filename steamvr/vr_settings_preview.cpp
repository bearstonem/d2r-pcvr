// Synthetic VR Settings previews and checks; never loads the game or its configuration.
//   --pages <folder>     every View and its key states as PNGs (PreviewShots), with timings
//   --pages-check        every View for the title screen and four classes, plain and with
//                        warnings: painted without error, and every text fits its element
//   --fit-check          every row's help card at its real size (at its value, changed with
//                        Undo / Default / Feel it, each option pointed), the preset, tab,
//                        tool, pending and confirm cards: the whole help fits, nothing overflows
//   --worker-check       pages painted on a background worker equal this thread's
//   --draw-sheet <png>   the drawing layer's component sheet (vr_settings_draw.h)
#include "vr_settings_ui.h"
#include "latest_worker.h"
#include "vr_settings_draw.h"
#include <windows.h>
#include <objbase.h>
#include <wincodec.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
using namespace framevr::settings;
namespace L=framevr::settings::layout;
namespace C=framevr::settings::content;
static_assert(draw::CanvasWidth==Width&&draw::CanvasHeight==Height);
// Straight RGBA to a PNG file through WIC.
static bool SavePng(const char* path,const std::vector<uint8_t>& rgba,int width,int height){
    const HRESULT co=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    wchar_t wide[MAX_PATH]{};MultiByteToWideChar(CP_UTF8,0,path,-1,wide,MAX_PATH);
    std::vector<uint8_t> bgra=rgba;for(size_t i=0;i<bgra.size();i+=4)std::swap(bgra[i],bgra[i+2]);
    IWICImagingFactory* wic=nullptr;IWICStream* stream=nullptr;IWICBitmapEncoder* encoder=nullptr;IWICBitmapFrameEncode* frame=nullptr;
    WICPixelFormatGUID format=GUID_WICPixelFormat32bppBGRA;
    const bool ok=SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&wic)))&&
        SUCCEEDED(wic->CreateStream(&stream))&&SUCCEEDED(stream->InitializeFromFilename(wide,GENERIC_WRITE))&&
        SUCCEEDED(wic->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder))&&SUCCEEDED(encoder->Initialize(stream,WICBitmapEncoderNoCache))&&
        SUCCEEDED(encoder->CreateNewFrame(&frame,nullptr))&&SUCCEEDED(frame->Initialize(nullptr))&&SUCCEEDED(frame->SetSize(width,height))&&
        SUCCEEDED(frame->SetPixelFormat(&format))&&format==GUID_WICPixelFormat32bppBGRA&&
        SUCCEEDED(frame->WritePixels(height,width*4,UINT(bgra.size()),bgra.data()))&&SUCCEEDED(frame->Commit())&&SUCCEEDED(encoder->Commit());
    for(IUnknown* p:{static_cast<IUnknown*>(frame),static_cast<IUnknown*>(encoder),static_cast<IUnknown*>(stream),static_cast<IUnknown*>(wic)})if(p)p->Release();
    if(SUCCEEDED(co))CoUninitialize();
    return ok;
}
static int DrawSheetPreview(const char* out){
    const auto sheet=draw::DrawSheet(50);
    if(!sheet.error.empty()||sheet.rgba.empty()){std::fprintf(stderr,"Draw sheet failed: %s\n",sheet.error.c_str());return 40;}
    std::printf("Draw sheet: first page %.1f ms (painter, fonts, icons, panel cache); full Aim page repaint %.2f ms average over 50; whole sheet (4 canvases) %.1f ms\n",sheet.firstMs,sheet.repaintMs,sheet.sheetMs);
    if(!SavePng(out,sheet.rgba,sheet.width,sheet.height)){std::fprintf(stderr,"Could not write %s\n",out);return 41;}
    std::printf("Wrote %s (%d x %d)\n",out,sheet.width,sheet.height);return 0;
}
static int Pages(const char* dir){
    for(const auto& s:PreviewShots()){
        const auto path=std::string(dir)+"\\"+s.name+".png";
        if(s.picture.pixels.empty()||!SavePng(path.c_str(),s.picture.pixels,Width,Height)){std::fprintf(stderr,"%s: %s\n",s.name.c_str(),s.picture.error.c_str());return 50;}
        std::printf("%-28s build %.2f ms, draw %.2f ms (first); warm %.2f ms\n",s.name.c_str(),s.picture.buildMs,s.picture.drawMs,s.warmMs);
    }
    return 0;
}

// ---- the checks' panels ----
static constexpr int Classes[]={-1,heroclass::Amazon,heroclass::Sorceress,heroclass::Paladin,heroclass::Barbarian};
static const char* ClassName(int c){return c<0?"title screen":c==heroclass::Amazon?"Amazon":c==heroclass::Sorceress?"Sorceress":c==heroclass::Paladin?"Paladin":"Barbarian";}
static const char* ViewName(int v){return C::PageOf(View(v)).name;}
struct Setup {Model m;L::LayoutInput in;};
// Every dependent row shown (the outline's hands, ground skill circles, foveation strength, wipe).
static Values AllRows(bool smooth){
    Values v=Defaults();
    v[TargetHighlight]=60;v[GroundAim]=1;v[SpellLock]=1;v[Foveation]=1;v[BloodVisor]=1;v[SmoothTurn]=smooth?1.f:0.f;v[Seated]=0;
    return v;
}
static Setup Base(View v,int hero,const Values& values=AllRows(false),bool adapter=true){
    Setup x;Snapshot s;s.values=values;s.ready=true;s.offlineDifficultyAvailable=adapter;
    x.m.Open(s);x.m.SetNotesPages(ReleaseNotesPages());x.m.SetContext({hero,hero==heroclass::Amazon,hero==heroclass::Sorceress,false});x.m.view=v;
    x.in.nowMs=100000;x.in.detected={{framevr::controllers::Layout::Touch,framevr::controllers::Layout::Touch}};
    // transport.cpp: the Performance view shows render_resolution's Describe (its widest), the others the foveation status.
    x.in.runtimeStatus=v==View::Performance?"VR source: 8192 x 8192 per eye (desktop fallback)":"Foveated shading available. Eye tracking unavailable on this headset/runtime.";
    x.in.performance={true,true,90,11.1f,3.4f,2.6f,4.1f,.9f};
    return x;
}
static int problems=0;
static void Fail(const std::string& what){if(problems<40)std::fprintf(stderr,"%s\n",what.c_str());++problems;}
static void Report(const std::string& where,const std::vector<std::string>& list){for(const auto& p:list)Fail(where+": "+p);}

// --pages-check
static int PagesCheck(){
    int pages=0;
    auto paint=[&](const Setup& x,const std::string& where){
        const auto picture=PaintPage(x.m,x.in);++pages;
        if(picture.pixels.size()!=size_t(Width)*Height*4){Fail(where+": paint error: "+picture.error);return;}
        if(picture.pixels[(size_t(Height/2)*Width+Width/2)*4+3]!=255)Fail(where+": the panel is not opaque");
        Report(where,FitProblems(picture.layout));
    };
    for(int hero:Classes)for(int v=0;v<int(View::Count);++v){
        const std::string where=std::string(ViewName(v))+", "+ClassName(hero);
        paint(Base(View(v),hero),where);
        paint(Base(View(v),hero,AllRows(true)),where+", smooth turning");
        // Warnings: Mono audio, the GPU wait not deferred, the game's video options, the widest frame times.
        Values w=AllRows(false);w[DeferGpuWait]=0;
        Setup x=Base(View(v),hero,w);x.in.monoAudio=true;x.in.game={true,true};x.in.frameGen={framevr::framegen::State::Active,1,1,2,1,1,0,0,0};   // frame generation's longest line
        x.in.performance={true,false,144,13.9f,33.3f,99.9f,33.3f,33.3f};x.in.performance.mixed=true;   // three-digit fps, CPU 99.9 ms
        paint(x,where+", warnings");
        Setup off=Base(View(v),hero);off.m.ready=false;paint(off,where+", unavailable");
        paint(Base(View(v),hero,AllRows(false),false),where+", no offline adapter");
    }
    {   // every popover open (the rows whose named list is too long for chips), every class, with and without a pointed option
        for(int hero:Classes)for(int v=0;v<int(View::Count);++v){
            const Setup probe=Base(View(v),hero);
            const auto rows=L::RowsOf(View(v),probe.m.Get(),probe.m.context);
            for(int i=0;i<rows.count;++i){
                const auto* r=L::ContentTables().Row(rows.keys[size_t(i)]);
                {const auto control=L::ControlOf(*r,probe.m.Get(),probe.m.context,ThisThreadMeasure());if(control!=L::Control::Popover&&control!=L::Control::Color)continue;}
                for(bool pointed:{false,true}){
                    Setup x=Base(View(v),hero);x.m.popoverKey=r->key;x.m.popoverAt=0;x.in.card=r->key;
                    if(pointed){int n=0;const auto* o=C::OptionsFor(*r,x.m.context,n);if(n)x.in.hover={Kind::Set,r->key,0,o[n-1].value};}
                    const auto picture=PaintPage(x.m,x.in);++pages;
                    const std::string where=std::string("popover ")+r->label+", "+ClassName(hero)+(pointed?", pointed":"");
                    if(picture.pixels.size()!=size_t(Width)*Height*4){Fail(where+": paint error: "+picture.error);continue;}
                    if(picture.layout.popover.w<=0)Fail(where+": no popover");
                    Report(where,FitProblems(picture.layout));
                }
            }
        }
    }
    {   // frame generation's status on its row and card, every state, NVIDIA and FidelityFX, by turns or not
        for(int state=0;state<=int(framevr::framegen::State::NeedsBothEyes);++state)for(int mode:{1,2})for(int hero:Classes){
            Values v=AllRows(false);v[FrameGeneration]=float(mode);
            Setup x=Base(View::Performance,hero,v);x.in.card=FrameGeneration;
            x.in.frameGen={framevr::framegen::State(state),mode==2?1:0,1,2,1,1,0,1,-1};
            const auto picture=PaintPage(x.m,x.in);++pages;
            Report(std::string("frame generation ")+framevr::framegen::StateName(framevr::framegen::State(state))+", "+ClassName(hero),FitProblems(picture.layout));
        }
    }
    {   // the fallback rows (content::FallbackDemo): every view, both More pages, with and without the Mono audio warning
        static const L::Tables demo=L::TablesOf(C::FallbackDemo());
        for(int hero:Classes)for(int v=0;v<int(View::Count);++v)for(int p=0;p<2;++p)for(bool mono:{false,true}){
            Setup x=Base(View(v),hero);x.m.morePage=p;x.in.monoAudio=mono;
            const auto picture=PaintPage(x.m,x.in,&demo);++pages;
            const std::string where=std::string("fallback ")+ViewName(v)+", "+ClassName(hero)+(mono?", mono":"");
            if(picture.pixels.size()!=size_t(Width)*Height*4){Fail(where+": paint error: "+picture.error);continue;}
            Report(where,FitProblems(picture.layout));
        }
    }
    {   // body calibration, both phases
        Setup x=Base(View::Body,-1);framevr::calibration::Flow f;f.Begin();f.phase=framevr::calibration::Phase::Hold;f.hint=framevr::calibration::Hint::Arms;f.progress=.55f;
        x.in.calibration=CalibrationOf(f);paint(x,"calibration");
        x.in.calibration.hint=int(framevr::calibration::Hint::Saved);x.in.calibration.measured=true;x.in.calibration.eyeHeight=2.3f;x.in.calibration.armSpan=2.6f;paint(x,"calibration saved");
    }
    {   // the longest release (NotesMaxLines lines): every page fits above the card with Previous / Next,
        // and Next walks to the last page, ready or not (paging needs no settings)
        std::vector<std::string> text;std::vector<const char*> lines;
        for(int i=0;i<NotesMaxLines;++i)text.push_back("Line "+std::to_string(i+1)+": a release line about as long as the notes allow on one line");
        for(const auto& t:text)lines.push_back(t.c_str());
        for(bool ready:{true,false}){
            Setup x=Base(View::WhatsNew,-1);x.m.ready=ready;
            x.in.notes=ReleaseNotesView();x.in.notes.lines=lines.data();x.in.notes.lineCount=NotesMaxLines;
            x.m.SetNotesPages(NotesPages(NotesMaxLines));
            for(int p=0;p<x.m.notesPages;++p){
                const std::string where="What's new page "+std::to_string(p+1)+(ready?"":", unavailable");
                paint(x,where);
                const auto l=MeasuredLayout(x.m,x.in);int buttons=0,bottom=0;
                for(const auto& e:l.elements){
                    if(e.hit.kind==Kind::NotesPage)++buttons;
                    if(e.role==L::Role::NotesText||e.hit.kind==Kind::NotesPage)bottom=std::max(bottom,e.rect.y+e.rect.h);
                }
                if(buttons!=2)Fail(where+": no Previous / Next");
                if(bottom>l.card.y)Fail(where+": the lines run into the help card");
                L::Press(x.m,{Kind::NotesPage,0,1},100000);
            }
            if(x.m.notesPage!=x.m.notesPages-1)Fail("What's new: Next did not reach the last page");
        }
    }
    if(problems){std::fprintf(stderr,"%d problems on %d pages\n",problems,pages);return 60;}
    std::printf("%d pages painted without error; every text fits\n",pages);return 0;
}

// --fit-check
static std::string Expected(Key k,bool adapter){
    const auto* r=C::RowOf(k);
    std::string help=r&&r->help?r->help:HelpText(k);
    if(k==OfflinePlayers)help=std::string(adapter?C::OfflineNote:C::OfflineUnavailable)+" "+help;
    return help;
}
static const L::Element* FindRole(const L::Layout& l,L::Role role){for(const auto& e:l.elements)if(e.role==role)return &e;return nullptr;}
static int CountRole(const L::Layout& l,L::Role role){int n=0;for(const auto& e:l.elements)if(e.role==role)++n;return n;}
static int FitCheck(){
    int cards=0;
    auto check=[&](const Setup& x,const std::string& where)->L::Layout{
        auto l=MeasuredLayout(x.m,x.in);++cards;
        Report(where,FitProblems(l));
        if(!FindRole(l,L::Role::CardTitle))Fail(where+": no card title");
        return l;
    };
    // The whole help, word for word: never cut mid-sentence or dropped.
    auto whole=[&](const L::Layout& l,const std::string& expected,const std::string& where){
        const auto* t=FindRole(l,L::Role::CardText);
        if(t&&t->text==expected)return;
        std::string card;char part[96];
        std::snprintf(part,sizeof part," (card %d,%d %dx%d:",l.card.x,l.card.y,l.card.w,l.card.h);card=part;
        for(const auto& e:l.elements)if(e.rect.y>=l.card.y&&e.rect.y<l.card.y+l.card.h&&e.role!=L::Role::HelpCard){
            std::snprintf(part,sizeof part," [role %d %d,%d %dx%d]",int(e.role),e.rect.x,e.rect.y,e.rect.w,e.rect.h);card+=part;}
        Fail(where+": the help is cut: \""+(t?t->text:std::string("(none)"))+"\""+card+")");
    };
    for(int hero:Classes)for(bool smooth:{false,true})for(int v=0;v<int(View::Count);++v){
        if(!C::PageOf(View(v)).settings)continue;
        const Values values=AllRows(smooth);
        const std::string page=std::string(ViewName(v))+", "+ClassName(hero)+(smooth?", smooth turning":"");
        const auto idle=check(Base(View(v),hero,values),page+" idle card");
        for(const auto& row:idle.elements){
            if(row.role!=L::Role::Row)continue;
            const Key k=row.key;const auto* content=C::RowOf(k);
            const std::string where=page+" / "+(content?content->label:Definitions[k].label);
            {Setup x=Base(View(v),hero,values);x.in.card=k;whole(check(x,where),Expected(k,true),where);}
            if(!content||content->kind==C::RowKind::Button)continue;
            {   // changed and not applied, away from the default: Undo and Default (and Feel it)
                Setup x=Base(View(v),hero,values);const auto& d=Definitions[k];
                x.m.Set(k,Normalize(k,d.defaultValue==d.min?d.min+d.step*2:d.min));x.in.card=k;
                if(!x.m.Dirty())continue;
                const auto l=check(x,where+", changed");whole(l,Expected(k,true),where+", changed");
                const int want=(HapticCategory(k)>=0?1:0)+1+(L::Same(x.m.Get()[k],d.defaultValue)?0:1);
                if(CountRole(l,L::Role::CardButton)!=want)Fail(where+", changed: the card shows "+std::to_string(CountRole(l,L::Role::CardButton))+" of its "+std::to_string(want)+" buttons");
            }
            for(const auto& e:idle.elements){   // each option pointed: its hint
                if(e.key!=k||e.role!=L::Role::Chip)continue;
                Setup x=Base(View(v),hero,values);x.in.card=k;x.in.hover=e.hit;check(x,where+", option "+e.text);
            }
        }
        {Setup x=Base(View(v),hero,values);x.in.card=HelpTabFirst+v;check(x,page+" tab card");}
        {Setup x=Base(View(v),hero,values);x.in.card=HelpResetPage;check(x,page+" Reset page card");}
    }
    {   // offline difficulty without its adapter: main's notice, whole
        Setup x=Base(View::Survival,-1,AllRows(false),false);x.in.card=OfflinePlayers;
        whole(check(x,"offline difficulty without the adapter"),Expected(OfflinePlayers,false),"offline difficulty without the adapter");
    }
    const auto& t=L::ContentTables();
    for(int hero:Classes)for(size_t p=0;p<t.presetCount;++p){   // presets from the defaults and from changed settings
        Setup x=Base(View::Presets,hero,Defaults());x.in.card=HelpPresetFirst+int(p);check(x,std::string("preset ")+t.presets[p].name+", "+ClassName(hero));
        Setup y=Base(View::Presets,hero,AllRows(true));y.in.card=HelpPresetFirst+int(p);check(y,std::string("preset ")+t.presets[p].name+" from changed settings, "+ClassName(hero));
    }
    {   // a draft on every row: the pending lists, the tools' cards, confirm close
        Setup x=Base(View::Body,-1);
        for(size_t i=0;i<t.rowCount;++i){
            const auto& r=t.rows[i];if(r.kind==C::RowKind::Button)continue;
            const auto& d=Definitions[r.key];x.m.Set(r.key,Normalize(r.key,d.defaultValue==d.min?d.min+d.step*2:d.min));
        }
        for(int v=0;v<int(View::Count);++v){Setup y=x;y.m.view=View(v);check(y,std::string("pending, ")+ViewName(v));}
        for(int id:{HelpApply,HelpClose,HelpRecenter,HelpCalibrate,HelpMonoAudio,HelpPerformance,HelpResetPage,HelpUndoReset,HelpGuide,HelpWhatsNew,HelpPresets,HelpDiscard,HelpKeepEditing,HelpApplyClose,HelpPending}){
            Setup y=x;y.in.card=id;check(y,"tool card "+std::to_string(id));
        }
        Setup c=x;c.m.confirmClose=true;check(c,"confirm close");
    }
    if(problems){std::fprintf(stderr,"%d problems in %d cards\n",problems,cards);return 61;}
    std::printf("%d help cards checked: every help whole, every text fits\n",cards);return 0;
}

// --worker-check
static int WorkerCheck(){
    const auto paint=[](const Setup& x){return PaintPage(x.m,x.in).pixels;};
    framevr::LatestWorker<Setup,std::vector<uint8_t>> worker(paint);
    if(!worker.Start())return 20;
    int pages=0;
    for(int hero:{-1,int(heroclass::Sorceress)})for(int v=0;v<int(View::Count);++v){
        Setup x=Base(View(v),hero);x.in.hover={Kind::Open,(v+1)%int(View::Guide),-1};x.in.card=v%2?HelpClose:NoHelp;
        const auto here=paint(x);const auto id=worker.Submit(x);std::vector<uint8_t> there;
        for(int wait=0;wait<5000&&!worker.Take(id,there);++wait)Sleep(1);
        if(here.size()!=size_t(Width)*Height*4||there!=here){std::fprintf(stderr,"Background paint differs: %s\n",ViewName(v));return 21;}
        ++pages;
    }
    worker.Stop();std::printf("%d VR Settings pages paint identically on the background painter\n",pages);return 0;
}

int main(int argc,char** argv){
    if(argc>2&&std::strcmp(argv[1],"--draw-sheet")==0)return DrawSheetPreview(argv[2]);
    if(argc>2&&std::strcmp(argv[1],"--pages")==0)return Pages(argv[2]);
    if(argc>1&&std::strcmp(argv[1],"--pages-check")==0)return PagesCheck();
    if(argc>1&&std::strcmp(argv[1],"--fit-check")==0)return FitCheck();
    if(argc>1&&std::strcmp(argv[1],"--worker-check")==0)return WorkerCheck();
    std::fprintf(stderr,"usage: vr-settings-preview --pages <folder> | --pages-check | --fit-check | --worker-check | --draw-sheet <out.png>\n");
    return 2;
}
