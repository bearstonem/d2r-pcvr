#include "area_banner.h"
#include <cstdlib>
#include <string>
#include <cmath>
using namespace framevr::areabanner;
void Check(bool ok){if(!ok){std::puts("Area banner check failed");std::exit(1);}}
int main(){
    Check(!CanUpdate(false,1,2));
    Check(!CanUpdate(true,0,0));
    Check(!CanUpdate(true,0,2));
    Check(!CanUpdate(true,1,0));
    Check(CanUpdate(true,1,2));
    // The same visible baseline on widescreen, square and portrait eye sources.
    for(auto size:{std::pair{1920u,1080u},std::pair{1536u,1536u},std::pair{2000u,2200u}}){
        const float band=std::min(float(size.second),float(size.first)*9.f/16.f);
        Check(std::abs(ViewAnchor(size.first,size.second,.4f)*band/size.second-.44f)<.0001f);
    }
    Check(ViewAnchor(0,0,.4f)==.4f);
    char text[544]{};TerrorInfo info{85,1,{},2};
    Check(WithLevel("Terrorized",info,text,sizeof text));
    Check(std::string_view(text)=="Terrorized (85)");
    // Preserve the native localized UTF-8 bytes and colour escape sequences.
    const std::string localized="\xc3\xbf" "cM" "Terrifi\xc3\xa9";
    Check(WithLevel(localized,info,text,sizeof text));
    Check(std::string(text)==localized+" (85)");
    info.active=0;info.level=99;
    Check(!WithLevel("Terrorized",info,text,sizeof text)); // Old level must not leak into ordinary areas.
    info.active=1;
    for(int invalid:{0,-1,256}){info.level=invalid;Check(!WithLevel("Terrorized",info,text,sizeof text));}
    info.level=96;
    Check(WithLevel("Terrorized",info,text,sizeof text)&&std::string_view(text)=="Terrorized (96)");
    Check(!WithLevel("",info,text,sizeof text));
    Check(!WithLevel(std::string_view("a\0b",3),info,text,sizeof text));
    Check(!WithLevel(std::string(513,'a'),info,text,sizeof text));
    Check(!WithLevel("Terrorized",info,text,10));
    std::puts("Native area banner: terror level, inactive areas, localization and bounds passed");
}
