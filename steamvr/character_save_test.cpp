// Synthetic saves only: the header layout of D2R's .d2s version 105 (and the older one) as read from real files.
#include "character_save.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <vector>
using namespace framevr::charsave;
// A statement with its string literals removed (the words in a message are not what it prints).
static std::string Code(std::string s){   // also without the word savepath: modinfo.json's relative value ("../"), not a place on the disk
    std::string out;bool in=false;
    for(size_t i=0;i<s.size();++i){
        if(s[i]=='"'&&(i==0||s[i-1]!='\\'))in=!in;else if(!in)out+=s[i];
    }
    for(size_t p;(p=out.find("savepath"))!=std::string::npos;)out.erase(p,8);
    return out;
}
static std::vector<uint8_t> Save(int version,int heroClass,int level){
    std::vector<uint8_t> d(0x700,0);
    const uint32_t magic=0xaa55aa55u;for(int i=0;i<4;++i){d[i]=uint8_t(magic>>8*i);d[4+i]=uint8_t(version>>8*i);}
    const bool moved=version>=99;
    d[moved?0x18:0x28]=uint8_t(heroClass);d[(moved?0x18:0x28)+3]=uint8_t(level);
    std::memcpy(&d[moved?0x12b:0x14],"Tester",6);
    return d;
}
int main(){
    {   // version 105: class, level and name from the new layout
        auto d=Save(105,6,31);
        Status s;assert(Parse(d.data(),d.size(),s)&&s.valid&&s.problem==Problem::None);
        assert(s.version==105&&s.heroClass==6&&s.level==31&&std::string(s.name)=="Tester");
    }
    {   // the older header layout (before version 99)
        auto d=Save(97,4,12);
        Status s;assert(Parse(d.data(),d.size(),s)&&s.heroClass==4&&s.level==12&&std::string(s.name)=="Tester");
    }
    {   // not a save, cut short, a version nobody wrote: nothing shown, a problem named
        auto d=Save(105,6,31);Status s;
        assert(!Parse(d.data(),0x100,s)&&!s.valid&&s.problem==Problem::Unreadable);
        d[0]=0;assert(!Parse(d.data(),d.size(),s));
        d=Save(60,6,31);assert(!Parse(d.data(),d.size(),s));
        assert(!Parse(nullptr,0,s));
    }
    {   // a name with a control byte stops there
        auto d=Save(105,0,1);d[0x12b+3]=0x01;
        Status s;assert(Parse(d.data(),d.size(),s)&&std::string(s.name)=="Tes");
    }
    {   // the save folder from the mod's modinfo.json
        std::string path;
        assert(SavePathOf("{\n  \"name\": \"Mod\",\n  \"savepath\": \"../\",\n}",path)&&path=="../");
        assert(!SavePathOf("{\"name\":\"x\"}",path));
        assert(SaveFolder(L"C:\\Users\\Someone\\Saved Games",  "Mod",true,"../")==L"C:\\Users\\Someone\\Saved Games\\Diablo II Resurrected\\");
        assert(SaveFolder(L"S:\\",  "pcvr-dev",true,"pcvr-dev/")==L"S:\\Diablo II Resurrected\\mods\\pcvr-dev\\");
        assert(SaveFolder(L"S:",  "Other",false,"")==L"S:\\Diablo II Resurrected\\mods\\Other\\");
        assert(SaveFolder(L"S:",  "",false,"")==L"S:\\Diablo II Resurrected\\");
        assert(SaveFolder(L"S:",  "x",true,"../../../Windows")==L"S:\\Diablo II Resurrected\\Windows\\");   // never above the game's folder
    }
    {   // privacy and sharing, pinned on the reader's source: the save file is opened for reading with shared read, write and delete (it
        // never blocks the game writing its save), and no log line prints a path (the folder is shown only below Saved Games)
        std::string here=__FILE__;here=here.substr(0,here.find_last_of("/\\")+1);
        std::ifstream in(here+"../vr/character_save.cpp",std::ios::binary);std::stringstream ss;ss<<in.rdbuf();const std::string src=ss.str();
        assert(!src.empty());
        assert(src.find("CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING")!=std::string::npos);
        assert(src.find("GENERIC_WRITE")==std::string::npos&&src.find("GENERIC_ALL")==std::string::npos&&src.find("CREATE_ALWAYS")==std::string::npos);
        size_t logs=0,at=0;
        while((at=src.find("logging::Write(",at))!=std::string::npos){
            ++logs;const size_t end=src.find(';',at);const std::string call=Code(src.substr(at,end-at));
            assert(call.find("folder")==std::string::npos&&call.find("savedGames")==std::string::npos&&call.find("path")==std::string::npos);
            at=end;
        }
        assert(logs>=3);
        at=0;   // and no line text is built from a path: the snprintf calls that fill the log's line take names, numbers and the derived `where` only
        while((at=src.find("snprintf(line",at))!=std::string::npos){
            const size_t end=src.find(';',at);const std::string call=Code(src.substr(at,end-at));
            assert(call.find("folder")==std::string::npos&&call.find("savedGames")==std::string::npos&&call.find("path")==std::string::npos);
            at=end;
        }
        assert(src.find("\"Saved Games\"+Narrow(folder.substr(savedGames.size()))")!=std::string::npos);   // the user folder never reaches the log
    }
    std::puts("character save: ok");
}
