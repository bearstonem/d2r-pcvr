// Standalone GDI atlas check. Uses synthetic state; never opens game data.
#include <windows.h>
#include <wincodec.h>
#include "interaction_ui.h"
#include "interaction_icons.h"
#include "interaction_paint.h"
#include "latest_worker.h"
#include "wrist_compass.h"
#include <cstdio>
#include <cstring>
#include <limits>
static bool SavePng(const char* path,const std::vector<uint8_t>& rgba,UINT width,UINT height) {
    wchar_t file[1024]{};MultiByteToWideChar(CP_UTF8,0,path,-1,file,1024);
    const HRESULT co=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    IWICImagingFactory* wic=nullptr;IWICStream* stream=nullptr;
    IWICBitmapEncoder* encoder=nullptr;IWICBitmapFrameEncode* frame=nullptr;
    auto bgra=rgba;for(size_t i=0;i<bgra.size();i+=4)std::swap(bgra[i],bgra[i+2]);
    WICPixelFormatGUID format=GUID_WICPixelFormat32bppBGRA;
    const bool ok=SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&wic)))&&
        SUCCEEDED(wic->CreateStream(&stream))&&SUCCEEDED(stream->InitializeFromFilename(file,GENERIC_WRITE))&&
        SUCCEEDED(wic->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder))&&
        SUCCEEDED(encoder->Initialize(stream,WICBitmapEncoderNoCache))&&SUCCEEDED(encoder->CreateNewFrame(&frame,nullptr))&&
        SUCCEEDED(frame->Initialize(nullptr))&&SUCCEEDED(frame->SetSize(width,height))&&
        SUCCEEDED(frame->SetPixelFormat(&format))&&IsEqualGUID(format,GUID_WICPixelFormat32bppBGRA)&&
        SUCCEEDED(frame->WritePixels(height,width*4,UINT(bgra.size()),bgra.data()))&&
        SUCCEEDED(frame->Commit())&&SUCCEEDED(encoder->Commit());
    for(IUnknown* p:{static_cast<IUnknown*>(frame),static_cast<IUnknown*>(encoder),static_cast<IUnknown*>(stream),static_cast<IUnknown*>(wic)})if(p)p->Release();
    if(SUCCEEDED(co))CoUninitialize();return ok;
}
int main(int argc,char** argv) {
    using namespace framevr::interaction;
    struct Cleanup {~Cleanup(){icons::Shutdown();}} cleanup;
    if(argc>2&&!std::strcmp(argv[2],"--effects-check")) {
        using namespace framevr;
        if(argc>3){wchar_t path[32768]{};MultiByteToWideChar(CP_UTF8,0,argv[3],-1,path,32768);icons::Directory(path);}
        Snapshot s;s.effects.known=true;s.companions.known=true;
        for(int t:{4,5,3,6,7}){s.companions.relevant|=companions::Bit(t);s.companions.alive[t]=t==4?8:1;}
        for(int state:{2,11,14,32,33,101,137})s.effects.Set(state);
        icons::Prefetch(s);if(!icons::Wait(10000))return 100;
        const auto pixels=PaintWristStatus(s.companions,s.effects);
        if(pixels.size()!=wriststatus::Width*wriststatus::Height*4)return 101;
        const int height=wriststatus::UsedHeight(s.companions,s.effects);
        if(height!=companions::RowHeight*2)return 102;
        for(int i=0;i<wriststatus::Width*height;++i)if(pixels[i*4+3]!=255)return 103;
        for(size_t i=wriststatus::Width*height*4;i<pixels.size();++i)if(pixels[i])return 104;
        // Effect-only characters, no-effect companions, and an unavailable sample.
        auto solo=PaintWristStatus({},s.effects);if(solo.empty()||solo[3]!=255)return 105;
        const auto old=PaintCompanions(s.companions),pets=PaintWristStatus(s.companions,{});
        if(!std::equal(old.begin(),old.end(),pets.begin()))return 106;
        for(auto b:PaintWristStatus({},{}))if(b)return 107;
        effects::Status all;all.known=true;for(const auto& d:effects::Definitions)all.Set(d.state);
        if(PaintWristStatus(s.companions,all).size()!=pixels.size())return 108;
        // Save only used rows for easy visual review; the runtime uses the full atlas.
        std::vector<uint8_t> crop(pixels.begin(),pixels.begin()+wriststatus::Width*height*4);
        if(!SavePng(argv[1],crop,wriststatus::Width,height))return 109;
        std::puts("Active effect art: pets + buffs + debuffs, no pets, unchanged companion art and transparent unused rows passed");return 0;
    }
    if(argc>2&&!std::strcmp(argv[2],"--companions-check")) {
        using namespace framevr;
        if(argc>3){wchar_t path[32768]{};MultiByteToWideChar(CP_UTF8,0,argv[3],-1,path,32768);icons::Directory(path);}
        companions::Roster necro;necro.known=true;
        for(int t:{4,5,3,6,7})necro.relevant|=companions::Bit(t);
        necro.alive[4]=8;necro.alive[5]=5;necro.alive[3]=1;necro.alive[6]=12;necro.alive[7]=1;
        Snapshot prefetch;prefetch.companions=necro;icons::Prefetch(prefetch);if(!icons::Wait(10000))return 90;
        constexpr int rows=4;std::vector<uint8_t> gallery(companions::Width*companions::RowHeight*rows*4);
        for(int r=0;r<rows;++r) {
            auto state=necro;
            if(r==1){state.alive[4]=5;state.alive[5]=0;state.alive[3]=0;state.alive[7]=0;}
            if(r==2){state={};state.known=true;for(int t:{10,11,12,13,14,15,7}){state.relevant|=companions::Bit(t);state.alive[t]=t==10?5:t==11?5:t==12?3:1;}}
            if(r==3){state={};state.known=true;for(int t:{2,16,17,19,20,21}){state.relevant|=companions::Bit(t);state.alive[t]=t==17?5:1;}}
            prefetch.companions=state;icons::Prefetch(prefetch);if(!icons::Wait(10000))return 91;
            auto pixels=PaintCompanions(state);if(pixels.size()!=companions::Width*companions::Height*4)return 92;
            for(int i=0;i<companions::Width*companions::RowHeight;++i)if(pixels[i*4+3]!=255)return 93;
            for(size_t i=companions::Width*companions::RowHeight*4;i<pixels.size();++i)if(pixels[i])return 94;
            std::memcpy(gallery.data()+r*companions::Width*companions::RowHeight*4,pixels.data(),companions::Width*companions::RowHeight*4);
        }
        const auto blank=PaintCompanions({});for(auto b:blank)if(b)return 95;
        if(!SavePng(argv[1],gallery,companions::Width,companions::RowHeight*rows))return 96;
        std::puts("Companion art: Necromancer deaths, Druid, other summons, hidden unknown and transparent unused rows passed");return 0;
    }
    if(argc>2&&!std::strcmp(argv[2],"--compass-check")) {
        using namespace framevr;
        const auto atlas=compass::Paint();if(atlas.size()!=compass::Width*compass::Height*4)return 60;
        for(int y=0;y<compass::StripHeight;++y)for(int x=0;x<compass::Window;++x)
            if(std::memcmp(&atlas[(y*compass::Width+x)*4],&atlas[(y*compass::Width+x+compass::Period)*4],4))return 61;
        const int w=compass::Window,h=8*(compass::StripHeight+16);
        std::vector<uint8_t> gallery(w*h*4);
        for(int row=0;row<8;++row) {
            const int crop=compass::Crop(row*compass::Tau/8),top=row*(compass::StripHeight+16);
            for(int y=0;y<compass::StripHeight;++y)
                std::memcpy(&gallery[((top+y)*w)*4],&atlas[(y*compass::Width+crop)*4],w*4);
            for(int y=0;y<compass::MarkerHeight;++y)for(int x=0;x<compass::MarkerWidth;++x) {
                const auto* p=&atlas[((compass::StripHeight+y)*compass::Width+x)*4];
                if(p[3])std::memcpy(&gallery[((top+y)*w+(w-compass::MarkerWidth)/2+x)*4],p,4);
            }
        }
        if(!SavePng(argv[1],gallery,w,h))return 62;
        std::puts("Compass preview: eight bearings, fixed marker and seamless north wrap passed");return 0;
    }
    if(argc>2&&!std::strcmp(argv[2],"--navigation-check")) {
        using namespace framevr;
        Snapshot s;s.session=1;Output o;
        const auto baseline=Paint(s,o,-1);
        constexpr int rows=7,w=navigation::Width,h=navigation::Height;
        const char* areas[]={"Rogue Encampment","Tower Cellar Level 5","Durance of Hate Level 3",
            "Worldstone Keep Level 3","A very long test area name with a floor number 12",
            "\xe5\xb4\x94\xe6\x96\xaf\xe7\x89\xb9", ""};
        const int acts[]={1,1,3,5,2,4,0};
        std::vector<uint8_t> gallery(w*rows*h*4);
        for(int row=0;row<rows;++row) {
            s.location={};s.location.level=row==rows-1?0:row+1;s.location.act=acts[row];
            std::snprintf(s.location.name,sizeof s.location.name,"%s",areas[row]);
            const auto atlas=Paint(s,o,-1);if(atlas.size()!=1024*1024*4)return 70;
            int changed=0;
            for(int y=0;y<1024;++y)for(int x=0;x<1024;++x) {
                const bool inside=x>=navigation::AtlasX&&y>=navigation::AtlasY&&y<navigation::AtlasY+h;
                const bool different=std::memcmp(&atlas[(y*1024+x)*4],&baseline[(y*1024+x)*4],4)!=0;
                if(!inside&&different)return 71;
                if(inside&&different)++changed;
            }
            if(row!=rows-1&&changed<100)return 72;
            for(int y=0;y<h;++y)std::memcpy(&gallery[((row*h+y)*w)*4],
                &atlas[((navigation::AtlasY+y)*1024+navigation::AtlasX)*4],w*4);
        }
        if(!SavePng(argv[1],gallery,w,rows*h))return 73;
        std::puts("Navigation preview: area/floor names, acts, Unicode, long/unknown labels and atlas isolation passed");return 0;
    }
    if(argc>3){wchar_t path[32768]{};MultiByteToWideChar(CP_UTF8,0,argv[3],-1,path,32768);icons::Directory(path);}
    Snapshot s;s.session=1;s.equipmentKnown=s.beltKnown=s.vitalsKnown=true;s.activeSet=1;
    if(argc>2&&!std::strcmp(argv[2],"--developer-check")) {
        s.developer=true;s.devClass=3;Output o;o.wheel=o.developer=true;o.wheelHand=0;o.devPage=1;
        const auto ready=Paint(s,o,-1);o.hover=DeveloperCell(framevr::dev::CrossbowChoice);const auto hovered=Paint(s,o,-1);
        if(ready.empty()||ready==hovered)return 70;
        o.devPage=2;o.hover=DeveloperCell(4);
        const auto scenic=Paint(s,o,-1);
        if(scenic.empty()||scenic==hovered)return 74;
        if(!SavePng((std::string(argv[1])+".scenic.png").c_str(),scenic,1024,1024))return 75;
        o.devPage=1;o.hover=DeveloperCell(framevr::dev::CrossbowChoice);
        for(int cls=0;cls<8;++cls) {
            s.devClass=cls;const auto other=Paint(s,o,-1);
            if(other.empty()||(cls!=3&&other==hovered))return 73;
            if(cls==4&&!SavePng((std::string(argv[1])+".barbarian.png").c_str(),other,1024,1024))return 76;
        }
        s.devClass=4;
        for(int page=0;page<framevr::dev::PageCount;++page){
            o.devPage=page;o.hover=-1;const auto pixels=Paint(s,o,-1);
            if(!SavePng((std::string(argv[1])+".page"+std::to_string(page)+".png").c_str(),pixels,1024,1024))return 77;
        }
        o.devPage=1;s.devClass=-1;const auto unavailable=Paint(s,o,-1);
        if(unavailable==hovered)return 71;
        if(!SavePng(argv[1],hovered,1024,1024))return 72;
        std::puts("Development preview: page, hover and unavailable state passed");return 0;
    }
    s.health=74;s.maxHealth=100;s.mana=39;s.maxMana=80;
    s.experience={7,99,15450,14175,22680,true};
    const char* names[]={"Fire Bolt","Raise Skeleton","Corpse Explosion","Teleport","Frozen Armor","Bone Spear","Amplify Damage","Unassigned"};
    const int ids[]={36,70,74,54,40,84,66,-1};
    for(int i=0;i<8;++i){s.skills[i].id=ids[i];s.skills[i].known=true;s.skills[i].available=i!=7;std::snprintf(s.skills[i].name,64,"%s",names[i]);}
    for(int i=0;i<4;++i){auto& b=s.belt[i];b.known=true;b.count=i==3?0:4-i;b.items[0]={uint64_t(i+1),uint32_t(i),i==1?Code('m','p','2'):i==2?Code('r','v','s'):Code('h','p','3')};}
    s.sets[0][0]={1,1,Code('w','n','d')};s.sets[1][0]={2,2,Code('h','b','w')};
    const bool wristCheck=argc>2&&std::strcmp(argv[2],"--wrist-check")==0;
    const bool wheelCheck=argc>2&&std::strcmp(argv[2],"--wheel-check")==0;
    const bool editorCheck=argc>2&&std::strcmp(argv[2],"--editor-check")==0;
    const bool weaponSkillsCheck=argc>2&&std::strcmp(argv[2],"--weapon-skills-check")==0;
    const bool mapCheck=argc>2&&std::strcmp(argv[2],"--map-check")==0;
    const bool questHands=argc>2&&std::strcmp(argv[2],"--quest-hands")==0;
    const bool indexHands=argc>2&&std::strcmp(argv[2],"--index-hands")==0;
    const bool frameHands=argc>2&&std::strcmp(argv[2],"--frame-hands")==0;
    const bool handsCheck=questHands||indexHands||frameHands||(argc>2&&std::strcmp(argv[2],"--hands-check")==0);
    if(handsCheck){
        std::vector<uint8_t> gallery(1536*768*4);
        s.bindingsEditable=true;s.bindingBank=0;
        s.skills[8].known=s.skills[8].available=true;s.skills[8].id=357;std::snprintf(s.skills[8].name,64,"Interact / pick up");
        s.skills[9].known=true;std::snprintf(s.skills[9].name,64,"Unassigned");
        for(int hand=0;hand<2;++hand){
            Output o;o.wheel=true;o.wheelHand=hand;o.hover=3;
            const auto choice=questHands?framevr::controllers::Choice::Quest:indexHands?framevr::controllers::Choice::Index:
                frameHands?framevr::controllers::Choice::Frame:framevr::controllers::Choice::Generic;
            const auto atlas=Paint(s,o,framevr::skills::TriggerSlot(hand),framevr::controllers::Resolve(choice,framevr::controllers::Labels{}));if(atlas.size()!=1024*1024*4)return 60;
            for(int y=0;y<768;++y)std::memcpy(&gallery[(y*1536+hand*768)*4],&atlas[y*1024*4],768*4);
        }
        if(!SavePng(argv[1],gallery,1536,768))return 61;
        puts("Both hand wheels painted with five button slots and utility access");return 0;
    }
    Output o;o.hover=1;o.wheel=true;o.utility=argc>2&&!wristCheck;
    // Match runtime: queue the snapshot as soon as it is known, before either
    // wheel page has ever been painted. Opening/toggling must not be necessary.
    icons::Prefetch(s);
    if(!icons::Wait(10000))return 20;
    const auto pixels=Paint(s,o,2);if(pixels.size()!=1024*1024*4)return 1;
    if(mapCheck) {
        o.utility=false;o.hover=-1;const auto spells=Paint(s,o,2);
        o.map=true;const auto map=Paint(s,o,2);
        // Moving the map must leave wrist vitals, belt, bottle and weapon cues unchanged.
        for(int y=0;y<1024;++y)for(int x=0;x<1024;++x)if(x>=768||y>=768)
            if(std::memcmp(&spells[(y*1024+x)*4],&map[(y*1024+x)*4],4))return 50;
        int changed=0;
        for(int y=92;y<645;++y)for(int x=107;x<660;++x)
            if(std::memcmp(&spells[(y*1024+x)*4],&map[(y*1024+x)*4],4))++changed;
        if(changed<100000)return 51;
        o.hover=4;const auto hovered=Paint(s,o,2);
        const auto tab=FooterBounds(1,MenuPage::Map);
        for(int y=int(tab.top*768);y<int(tab.bottom*768);++y)for(int x=int(tab.left*768);x<int(tab.right*768);++x)
            if(std::memcmp(&map[(y*1024+x)*4],&hovered[(y*1024+x)*4],4))return 52;
        std::vector<uint8_t> gallery(1536*768*4);
        for(int y=0;y<768;++y) {
            std::memcpy(&gallery[y*1536*4],&spells[y*1024*4],768*4);
            std::memcpy(&gallery[(y*1536+768)*4],&map[y*1024*4],768*4);
        }
        if(!SavePng(argv[1],gallery,1536,768))return 53;
        std::puts("Map tab preview: map body replaces spells, active tab persists, other atlas tiles unchanged");return 0;
    }
    if(weaponSkillsCheck) {
        std::vector<uint8_t> gallery(1024*768*4);
        const int selectedIds[]={12,26,97,-1};
        const char* selectedNames[]={"Multiple Shot","Strafe","Smite","Basic Arrow"};
        const char* hints[]={"Draw & release","Volley active","Shield swing / thrust","Draw & release"};
        o.utility=false;o.hover=1;
        for(int row=0;row<4;++row) {
            auto& cue=o.cue;cue={};cue.id=selectedIds[row];cue.slot=row==3?-1:2;cue.known=cue.available=cue.expanded=true;
            cue.busy=row==1;
            std::snprintf(cue.name,sizeof cue.name,"%s",selectedNames[row]);std::snprintf(cue.hint,sizeof cue.hint,"%s",hints[row]);
            if(row!=3){s.skills[2].id=cue.id;std::snprintf(s.skills[2].name,64,"%s",cue.name);}
            icons::Prefetch(s);if(!icons::Wait(10000))return 40;
            const auto atlas=Paint(s,o,cue.slot);
            for(int y=0;y<192;++y)std::memcpy(&gallery[((row*192+y)*1024+768)*4],&atlas[(y*1024+768)*4],256*4);
            if(row==0)for(int y=0;y<768;++y)std::memcpy(&gallery[y*1024*4],&atlas[y*1024*4],768*4);
            // Hovering another slot must not erase the active marker/icon.
            const auto before=atlas;o.hover=4;const auto after=Paint(s,o,cue.slot);o.hover=1;
            if(row!=3) {
                const float a=2*Pi/4+WheelPhase;const int x=int(384+std::sin(a)*218),y=int(345.6f-std::cos(a)*218);
                int gold=0;
                for(int dy=-48;dy<33;++dy)for(int dx=-42;dx<43;++dx)
                {
                    const auto* pixel=&before[((y+dy)*1024+x+dx)*4];
                    if(std::memcmp(pixel,&after[((y+dy)*1024+x+dx)*4],4))return 41;
                    if(pixel[0]==230&&pixel[1]==190&&pixel[2]==96&&pixel[3])++gold;
                }
                if(gold<100)return 43;
            }
        }
        o.cue.temporary=true;const auto quick=Paint(s,o,-1);o.cue.temporary=false;
        const auto persistent=Paint(s,o,-1);int quickPixels=0;
        for(int y=0;y<96;++y)for(int x=866;x<1024;++x)
            if(std::memcmp(&quick[(y*1024+x)*4],&persistent[(y*1024+x)*4],4))++quickPixels;
        if(quickPixels<100)return 44;
        o.cue.motionUnavailable=true;const auto fallback=Paint(s,o,-1);o.cue.motionUnavailable=false;
        int fallbackPixels=0;
        for(int y=8;y<55;++y)for(int x=866;x<1018;++x)
            if(std::memcmp(&fallback[(y*1024+x)*4],&persistent[(y*1024+x)*4],4))++fallbackPixels;
        if(fallbackPixels<100)return 45;
        if(!SavePng(argv[1],gallery,1024,768))return 42;
        std::puts("Weapon skill preview: persistent selection differs from hover; quick-cast and trigger-only badges rendered");return 0;
    }
    if(argc>2&&std::strcmp(argv[2],"--async-check")==0) {
        framevr::LatestWorker<PaintJob,std::vector<uint8_t>> painter([](const PaintJob& job){return Paint(job.snapshot,job.output,job.selection);});
        if(!painter.Start())return 30;
        LARGE_INTEGER frequency{};QueryPerformanceFrequency(&frequency);double paintTotal=0;
        for(int i=0;i<12;++i) {
            s.health=100-i*8;s.mana=i*6;o.utility=i%2;o.hover=i%8;o.nearHolster=i%3-1;
            LARGE_INTEGER start{},end{};QueryPerformanceCounter(&start);const auto expected=Paint(s,o,2);QueryPerformanceCounter(&end);
            paintTotal+=(end.QuadPart-start.QuadPart)*1000./frequency.QuadPart;
            auto request=painter.Submit({s,o,2});std::vector<uint8_t> actual;const auto until=GetTickCount64()+3000;
            while(!painter.Take(request,actual)){if(GetTickCount64()>until)return 31;Sleep(1);}
            if(actual!=expected)return 32;
        }
        painter.Stop();FILE* file=std::fopen(argv[1],"w");if(!file)return 33;
        std::fprintf(file,"12 native-GDI worker images exactly match synchronous painting. Mean synchronous paint %.3f ms. Shutdown joined before icon cleanup.\n",paintTotal/12);
        std::fclose(file);return 0;
    }
    if(editorCheck) {
        s.bindingsEditable=true;s.bindingBank=0;s.skillCount=7;
        std::copy_n(s.skills.begin(),7,s.catalog.begin());
        std::vector<uint8_t> gallery(1536*768*4);
        for(int page=0;page<2;++page) {
            o.utility=false;o.editor=page+1;o.editSlot=1;o.editCount=7;
            std::copy_n(s.skills.begin(),o.editOptions.size(),o.editOptions.begin());o.hover=page?3:1;
            const auto atlas=Paint(s,o,2);
            for(int y=0;y<768;++y)std::memcpy(&gallery[(y*1536+page*768)*4],&atlas[y*1024*4],768*4);
        }
        if(!SavePng(argv[1],gallery,1536,768))return 25;
        std::puts("Skill editor preview: slot selection and native skill icon list rendered");return 0;
    }
    if(wheelCheck) {
        if(argc<4||!icons::Get("s36")||!icons::Get(s.belt[0].Next())||!icons::Get(s.sets[0][0]))return 21;
        std::vector<uint8_t> gallery(1536*768*4);
        for(int page=0;page<2;++page) {
            o.utility=page;const auto atlas=Paint(s,o,2);
            for(int y=0;y<768;++y)std::memcpy(&gallery[(y*1536+page*768)*4],&atlas[y*1024*4],768*4);
            if(atlas[3]!=0)return 22;
        }
        const auto before=Paint(s,o,2);
        s.belt[0].items[0]=s.belt[1].items[0];const auto after=Paint(s,o,2);
        // A different actual item must update its wedge, not merely its count.
        const float a=4*Pi/4+WheelPhase;
        const int x=int(384+std::sin(a)*218),y=int(345.6f-std::cos(a)*218);
        int changed=0;
        for(int dy=-60;dy<30;++dy)for(int dx=-50;dx<50;++dx)
            if(std::memcmp(&before[((y+dy)*1024+x+dx)*4],&after[((y+dy)*1024+x+dx)*4],4))++changed;
        if(changed<100)return 23;
        if(!SavePng(argv[1],gallery,1536,768))return 24;
        std::puts("Wheel artwork: native skills/items, changing slot contents, transparent frame and both pages passed");return 0;
    }
    if(wristCheck) {
        std::vector<uint8_t> gallery(512*1024*4);
        const float values[]={100,74,0,std::numeric_limits<float>::quiet_NaN()};
        for(int row=0;row<4;++row) {
            s.health=values[row];s.mana=row==1?39:values[row]*.8f;
            s.vitalsKnown=row!=3;
            s.experience=row==0?framevr::wrist::Experience{1,99,0,0,500,true}:
                row==1?framevr::wrist::Experience{2,99,1142,500,1500,true}:
                row==2?framevr::wrist::Experience{99,99,3520485254u,3520485254u,3837739017u,true}:framevr::wrist::Experience{};
            const auto atlas=Paint(s,o,2);if(atlas.size()!=pixels.size())return 10;
            for(int y=0;y<256;++y)std::memcpy(&gallery[((row*256+y)*512)*4],&atlas[((y+768)*1024+512)*4],512*4);
            // Vitals updates must leave every other interactive tile unchanged.
            for(int y=0;y<1024;++y)for(int x=0;x<(y<768?1024:512);++x)
                if(std::memcmp(&atlas[(y*1024+x)*4],&pixels[(y*1024+x)*4],4))return 11;
        }
        auto p=[&](int row,int x,int y){return &gallery[((row*256+y)*512+x)*4];};
        const auto red=p(0,148,125),blue=p(0,364,125);
        if(red[0]<red[2]*2||blue[2]<blue[0]*2)return 12; // actual embedded art decoded
        for(int row=0;row<4;++row)if(p(row,0,0)[3]!=0)return 13;
        if(p(1,148,90)[0]>=red[0]||p(1,148,155)[0]<p(1,148,155)[2]*2)return 14;
        if(p(1,364,100)[2]>=blue[2]||p(1,364,155)[2]<p(1,364,155)[0]*2)return 15;
        if(std::memcmp(p(2,148,125),p(3,148,125),4)||p(2,148,155)[0]>p(2,148,155)[2])return 16;
        int partial=0;for(size_t i=3;i<512*256*4;i+=4)if(gallery[i]>0&&gallery[i]<255)++partial;
        if(!partial)return 17;
        if(!SavePng(argv[1],gallery,512,1024))return 18;
        std::puts("Wrist artwork: embedded PNG, live levels, unknown/empty state, alpha and atlas isolation passed");
        return 0;
    }
    FILE* file=std::fopen(argc>1?argv[1]:"interaction-preview.ppm","wb");if(!file)return 2;
    std::fprintf(file,"P6\n1024 1024\n255\n");
    for(size_t i=0;i<pixels.size();i+=4)if(std::fwrite(pixels.data()+i,1,3,file)!=3){std::fclose(file);return 3;}
    return std::fclose(file)==0?0:4;
}
