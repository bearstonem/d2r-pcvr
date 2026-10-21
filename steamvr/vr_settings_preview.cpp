// Synthetic UI preview; does not load the game or its configuration.
#include "vr_settings_ui.h"
#include "release_notes.h"
#include "latest_worker.h"
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
int main(int argc,char** argv){
    using namespace framevr::settings;
    Snapshot s;s.ready=true;s.offlineDifficultyAvailable=true;s.values[Motion]=1;s.values[Snap]=30;s.values[Foveation]=2;
    if(argc>3&&std::strcmp(argv[3],"--foveation-off")==0)s.values[Foveation]=0; // greyed Foveation strength row
    Model m;m.Open(s);const int page=argc>2?std::atoi(argv[2]):0;
    m.Click({Kind::Tab,page});
    if(argc>3&&std::strcmp(argv[3],"--release-notes-check")==0){
        if(m.page!=Page::ReleaseNotes||m.Dirty())return 13;
        HDC dc=CreateCompatibleDC(nullptr);
        HFONT font=CreateFontW(27,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Arial");
        auto old=SelectObject(dc,font);
        for(const char* line:framevr::releasenotes::ReleaseLines){
            SIZE size{};GetTextExtentPoint32A(dc,line,int(std::strlen(line)),&size);
            if(size.cx>1170||size.cy>35){std::fprintf(stderr,"Release line too long: %s\n",line);return 12;}
        }
        for(const char* line:{framevr::releasenotes::Author,framevr::releasenotes::ProjectUrl,framevr::releasenotes::Credits}){
            SIZE size{};GetTextExtentPoint32A(dc,line,int(std::strlen(line)),&size);
            if(size.cx>1100||size.cy>35)return 15;
        }
        HFONT bold=CreateFontW(32,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Arial");
        SelectObject(dc,bold);
        SIZE title{};GetTextExtentPoint32A(dc,framevr::releasenotes::ReleaseTitle,int(std::strlen(framevr::releasenotes::ReleaseTitle)),&title);
        if(title.cx>1200||title.cy>45)return 16;
        for(int i=0;i<TabCount;++i){
            SIZE size{};GetTextExtentPoint32A(dc,TabNames[i],int(std::strlen(TabNames[i])),&size);
            if(size.cx>TabBox(i).w-12||size.cy>TabBox(i).h-13){std::fprintf(stderr,"Tab label too long: %s\n",TabNames[i]);return 14;}
        }
        SelectObject(dc,font);DeleteObject(bold);
        SelectObject(dc,old);DeleteObject(font);DeleteDC(dc);
    }
    if(argc>3&&std::strcmp(argv[3],"--dropdown")==0)m.Click({Kind::ControllerMenu});
    const int help=argc>4&&std::strcmp(argv[3],"--help")==0?std::atoi(argv[4]):NoHelp;
    if(argc>3&&std::strcmp(argv[3],"--help-check")==0){
        const auto plain=Paint(m,{},"");
        if(plain.size()!=AtlasWidth*Height*4)return 5;
        HDC dc=CreateCompatibleDC(nullptr);
        HFONT normal=CreateFontW(27,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Arial");
        HFONT bold=CreateFontW(32,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Arial");
        auto old=SelectObject(dc,normal);
        for(int id=0;id<=HelpLast;++id){
            for(int title=0;title<2;++title){
                wchar_t wide[512]{};MultiByteToWideChar(CP_UTF8,0,title?HelpTitle(id):HelpText(id),-1,wide,512);
                SelectObject(dc,title?bold:normal);RECT bounds{0,0,HelpCard.w-48,0};
                DrawTextW(dc,wide,-1,&bounds,DT_CALCRECT|DT_WORDBREAK|DT_NOPREFIX);
                if(bounds.bottom>(title?92:HelpCard.h-152)||bounds.right>HelpCard.w-48){std::fprintf(stderr,"Clipped help: %s\n",HelpTitle(id));return 6;}
            }
            // Every choice's name on one line, and its text under the general help.
            for(int c=0;c<HelpChoices(id);++c) {
                const auto choice=HelpChoice(id,float(c));
                const auto measure=[&](const char* value,HFONT font){
                    wchar_t wide[512]{};MultiByteToWideChar(CP_UTF8,0,value,-1,wide,512);
                    SelectObject(dc,font);RECT bounds{0,0,HelpCard.w-48,0};
                    DrawTextW(dc,wide,-1,&bounds,DT_CALCRECT|DT_WORDBREAK|DT_NOPREFIX);return bounds;
                };
                const auto general=measure(HelpText(id),normal),name=measure(choice.name,bold),text=measure(choice.text,normal);
                if(name.bottom>HelpChoiceNameHeight||general.bottom+HelpChoiceGap+HelpChoiceNameHeight+text.bottom>HelpCard.h-152||text.right>HelpCard.w-48){
                    std::fprintf(stderr,"Clipped help: %s, %s\n",HelpTitle(id),choice.name);return 12;
                }
            }
            const auto shown=Paint(m,{},"",nullptr,{},id);
            if(shown.size()!=plain.size())return 7;
            bool content=false;
            for(int y=0;y<Height;++y)for(int x=0;x<AtlasWidth;++x){
                const int i=(y*AtlasWidth+x)*4;
                if(x<Width&&std::memcmp(shown.data()+i,plain.data()+i,4))return 8;
                if(x>=Width){
                    if(plain[i+3])return 9;
                    if(!HelpCard.Contains(float(x),float(y))&&shown[i+3])return 10;
                    if(x>HelpCard.x+24&&y>HelpCard.y+128&&shown[i]>100&&shown[i+3])content=true;
                }
            }
            if(!content)return 11;
        }
        // The Perf page's warning lines: one line each, left of the Defer GPU wait button.
        SelectObject(dc,normal);
        {   // the Mono audio warning: one line in the title row's hint box
            wchar_t wide[128]{};MultiByteToWideChar(CP_UTF8,0,MonoAudioWarning,-1,wide,128);RECT bounds{0,0,420,0};
            DrawTextW(dc,wide,-1,&bounds,DT_CALCRECT|DT_WORDBREAK|DT_NOPREFIX);
            if(bounds.bottom>42||bounds.right>420){std::fprintf(stderr,"Mono audio warning too long\n");return 18;}
        }
        for(const char* line:{DeferWarning,GameVideoWarning({true,true}),GameVideoWarning({true,false}),GameVideoWarning({false,true})}){
            wchar_t wide[512]{};MultiByteToWideChar(CP_UTF8,0,line,-1,wide,512);RECT bounds{0,0,DeferBox.x-50,0};
            DrawTextW(dc,wide,-1,&bounds,DT_CALCRECT|DT_WORDBREAK|DT_NOPREFIX);
            if(bounds.bottom>29||bounds.right>DeferBox.x-50){std::fprintf(stderr,"Video warning too long: %s\n",line);return 17;}
        }
        SelectObject(dc,old);DeleteObject(normal);DeleteObject(bold);DeleteDC(dc);
        std::puts("All help text fits; help leaves every control pixel unchanged and unused space transparent");
    }
    framevr::calibration::Flow body;body.Begin();body.phase=framevr::calibration::Phase::Hold;body.hint=framevr::calibration::Hint::Pose;body.progress=.55f;
    // --performance-check: each of the Perf page's lines stays on one line,
    // at its widest numbers.
    if(argc>3&&std::strcmp(argv[3],"--performance-check")==0){
        HDC dc=CreateCompatibleDC(nullptr);
        HFONT normal=CreateFontW(27,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Arial");
        auto old=SelectObject(dc,normal);
        const auto fits=[&](const std::string& value){
            wchar_t wide[512]{};MultiByteToWideChar(CP_UTF8,0,value.c_str(),-1,wide,512);
            RECT bounds{0,0,0,0};DrawTextW(dc,wide,-1,&bounds,DT_CALCRECT|DT_SINGLELINE|DT_NOPREFIX);
            if(bounds.right<=1170)return true;
            std::fprintf(stderr,"Perf page line too long (%ld px): %s\n",long(bounds.right),value.c_str());return false;
        };
        const PerformanceReadout widest{true,false,999,99.9f,99.9f,99.9f,99.9f,99.9f};
        const auto most=ParseGameVideo(R"({"Shadow Quality": 44, "Character Detail": 55, "Light Quality": 44, "Ambient Occlusion Quality": 44,)"
                                       R"( "Atmospherics Quality": 44, "Resolution Scale": 200, "NVIDIA DLSS": 1})");
        const bool ok=fits(PerformanceLine(widest))&&fits(PerformanceVerdict(widest))&&fits(PerformanceLine({}))&&fits(GameVideoLine(most))&&
                      fits(GameVideoUnsaved)&&fits(DepthRestartNote);
        SelectObject(dc,old);DeleteObject(normal);DeleteDC(dc);
        if(!ok)return 22;
        std::puts("Perf page lines fit at their widest");return 0;
    }
    // --worker-check: the transport paints VR Settings on a background painter;
    // every page, with a hover and a help card, and the calibration page must
    // come out there exactly as on this thread.
    if(argc>3&&std::strcmp(argv[3],"--worker-check")==0){
        struct Job {Model model;Hit hover;int help=NoHelp;bool calibrating=false;};
        const auto paint=[&](const Job& j){
            return Paint(j.model,j.hover,"Foveated shading available.",j.calibrating?&body:nullptr,
                         framevr::controllers::Labels{{framevr::controllers::Layout::Touch,framevr::controllers::Layout::Touch}},j.help,GameVideo{true,false},true);
        };
        framevr::LatestWorker<Job,std::vector<uint8_t>> worker(paint);
        if(!worker.Start())return 20;
        for(int tab=0;tab<=TabCount;++tab){
            Job job;job.model=m;job.calibrating=tab==TabCount;
            if(!job.calibrating){job.model.Click({Kind::Tab,tab});job.hover={Kind::Tab,(tab+1)%TabCount};job.help=tab%2?HelpClose:NoHelp;}
            const auto here=paint(job);const auto id=worker.Submit(job);std::vector<uint8_t> there;
            for(int wait=0;wait<5000&&!worker.Take(id,there);++wait)Sleep(1);
            if(here.size()!=AtlasWidth*Height*4||there!=here){std::fprintf(stderr,"Background paint differs: %s\n",job.calibrating?"calibration":TabNames[tab]);return 21;}
        }
        worker.Stop();std::puts("VR Settings pages and calibration paint identically on the background painter");return 0;
    }
    // --game-video: the Perf page as with Vertical Sync on and TAA; --deferred: the GPU wait already deferred.
    if(argc>3&&std::strcmp(argv[3],"--deferred")==0){Snapshot d=s;d.values[DeferGpuWait]=1;m.Open(d);m.Click({Kind::Tab,page});}
    GameVideo game=argc>3&&std::strcmp(argv[3],"--game-video")==0?GameVideo{true,true}:GameVideo{};
    const bool monoAudio=argc>3&&std::strcmp(argv[3],"--mono-audio")==0;   // the title row's Windows Mono audio warning
    // --performance: the Perf page with a second of frames over the headset's
    // frame, the game's options as its Settings.json has them, and a ReShade
    // depth change waiting for a restart.
    const bool sample=argc>3&&std::strcmp(argv[3],"--performance")==0;
    PerformanceReadout performance;
    if(sample){
        performance={true,true,52,11.1f,1.0f,7.0f,9.4f,1.5f};
        game=ParseGameVideo(R"({"Shadow Quality": 4, "Character Detail": 5, "Ambient Occlusion Quality": 4, "Atmospherics Quality": 4,)"
                            R"( "Light Quality": 2, "Texture Quality": 4, "Resolution Scale": 100, "NVIDIA DLSS": 0, "Anti Aliasing": 1, "VSync": 0})");
    }
    const auto pixels=Paint(m,{},page==int(Page::Performance)?"VR source: 2560 x 2560 per eye":"Foveated shading available. Eye-tracking samples are active.",page==TabCount?&body:nullptr,
        framevr::controllers::Labels{{framevr::controllers::Layout::Touch,framevr::controllers::Layout::Touch}},help,game,monoAudio,performance,sample);
    if(pixels.size()!=AtlasWidth*Height*4)return 1;
    FILE* file=std::fopen(argc>1?argv[1]:"vr-settings-preview.ppm","wb");if(!file)return 2;
    const char* name=argc>1?argv[1]:"";
    if(std::strstr(name,".bmp")){
        BITMAPFILEHEADER header{};header.bfType=0x4d42;header.bfOffBits=sizeof header+sizeof(BITMAPINFOHEADER);header.bfSize=header.bfOffBits+uint32_t(pixels.size());
        BITMAPINFOHEADER info{};info.biSize=sizeof info;info.biWidth=AtlasWidth;info.biHeight=-Height;info.biPlanes=1;info.biBitCount=32;
        if(std::fwrite(&header,1,sizeof header,file)!=sizeof header||std::fwrite(&info,1,sizeof info,file)!=sizeof info){std::fclose(file);return 3;}
        auto bgra=pixels;for(size_t i=0;i<bgra.size();i+=4)std::swap(bgra[i],bgra[i+2]);
        if(std::fwrite(bgra.data(),1,bgra.size(),file)!=bgra.size()){std::fclose(file);return 3;}
    }else{
        std::fprintf(file,"P6\n%d %d\n255\n",AtlasWidth,Height);
        for(size_t i=0;i<pixels.size();i+=4)if(std::fwrite(pixels.data()+i,1,3,file)!=3){std::fclose(file);return 3;}
    }
    return std::fclose(file)==0?0:4;
}
