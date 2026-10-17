// Synthetic UI preview; does not load the game or its configuration.
#include "vr_settings_ui.h"
#include "dev_release_notes.h"
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
int main(int argc,char** argv){
    using namespace framevr::settings;
    Snapshot s;s.ready=true;s.values[Motion]=1;s.values[Snap]=30;s.values[Foveation]=2;
    Model m;m.Open(s);const int page=argc>2?std::atoi(argv[2]):0;
    m.SetDeveloper(argc>3&&std::strcmp(argv[3],"--developer-check")==0);
    m.Click({Kind::Tab,page});
    if(m.developer){
        HDC dc=CreateCompatibleDC(nullptr);
        HFONT font=CreateFontW(27,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Arial");
        auto old=SelectObject(dc,font);
        for(const char* line:framevr::dev::ReleaseLines){
            SIZE size{};GetTextExtentPoint32A(dc,line,int(std::strlen(line)),&size);
            if(size.cx>1170||size.cy>35){std::fprintf(stderr,"Release line too long: %s\n",line);return 12;}
        }
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
        for(int id=0;id<=HelpCalibrate;++id){
            for(int title=0;title<2;++title){
                wchar_t wide[512]{};MultiByteToWideChar(CP_UTF8,0,title?HelpTitle(id):HelpText(id),-1,wide,512);
                SelectObject(dc,title?bold:normal);RECT bounds{0,0,HelpCard.w-48,0};
                DrawTextW(dc,wide,-1,&bounds,DT_CALCRECT|DT_WORDBREAK|DT_NOPREFIX);
                if(bounds.bottom>(title?92:HelpCard.h-152)||bounds.right>HelpCard.w-48){std::fprintf(stderr,"Clipped help: %s\n",HelpTitle(id));return 6;}
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
        SelectObject(dc,old);DeleteObject(normal);DeleteObject(bold);DeleteDC(dc);
        std::puts("All help text fits; help leaves every control pixel unchanged and unused space transparent");
    }
    framevr::calibration::Flow body;body.Begin();body.phase=framevr::calibration::Phase::Hold;body.hint=framevr::calibration::Hint::Pose;body.progress=.55f;
    const auto pixels=Paint(m,{},page==int(Page::Video)?"VR source: 2560 x 2560 per eye":"Foveated shading available. Eye-tracking samples are active.",page==TabCount+1?&body:nullptr,
        framevr::controllers::Labels{{framevr::controllers::Layout::Touch,framevr::controllers::Layout::Touch}},help);
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
