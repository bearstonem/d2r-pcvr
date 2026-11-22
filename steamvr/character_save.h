#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

// A character's name, class and level, read from its .d2s save file on disk (the game's documented header, so no live memory
// offsets and no signatures). Quick cast keeps each character's ring under its save name. Verified on D2R saves of file version
// 105; the older layout (before 99) is read too.
namespace framevr::charsave {
enum class Problem : uint8_t { None, NoFolder, NoSave, NotThisClass, Unreadable };
struct Status {
    bool valid=false;
    Problem problem=Problem::NoFolder;
    int version=0,heroClass=-1,level=0;
    char name[17]{};
    bool operator==(const Status&) const=default;
};
inline uint32_t Le32(const uint8_t* p){return p[0]|p[1]<<8|p[2]<<16|uint32_t(p[3])<<24;}
// Header fields sit 16 bytes earlier from version 99 on (the name moved to 0x12b):
// class 0x18, level 0x1b (verified on 105). Before: 0x28, 0x2b, name 0x14.
inline bool Parse(const uint8_t* d,size_t size,Status& out){
    out={};out.problem=Problem::Unreadable;
    if(!d||size<0x200||Le32(d)!=0xaa55aa55u)return false;
    const int version=int(Le32(d+4));
    if(version<96||version>255)return false;
    const bool moved=version>=99;
    const size_t classAt=moved?0x18:0x28,nameAt=moved?0x12b:0x14;
    out.version=version;out.heroClass=d[classAt];out.level=d[classAt+3];
    for(int i=0;i<16&&d[nameAt+i]>=0x20&&d[nameAt+i]<0x7f;++i)out.name[i]=char(d[nameAt+i]);
    out.valid=true;out.problem=Problem::None;return true;
}
// modinfo.json's "savepath"; false when it has none.
inline bool SavePathOf(std::string_view json,std::string& out){
    const auto key=json.find("\"savepath\"");if(key==std::string_view::npos)return false;
    const auto colon=json.find(':',key);if(colon==std::string_view::npos)return false;
    const auto open=json.find('"',colon);if(open==std::string_view::npos)return false;
    const auto close=json.find('"',open+1);if(close==std::string_view::npos)return false;
    out=std::string(json.substr(open+1,close-open-1));return true;
}
// Where the game keeps a mod's characters: <Saved Games>\Diablo II Resurrected\mods\<savepath>
// ("../" is the unmodded folder), else mods\<mod>\. No mod: the unmodded folder. Always ends in a backslash.
inline std::wstring SaveFolder(std::wstring base,std::string_view mod,bool hasSavePath,std::string_view savepath){
    while(!base.empty()&&(base.back()==L'\\'||base.back()==L'/'))base.pop_back();
    base+=L"\\Diablo II Resurrected";
    if(mod.empty())return base+L'\\';
    std::string rest="mods/";rest+=hasSavePath?savepath:mod;
    std::wstring path;size_t depth=0;   // segments added below the base, so ".." never climbs above it
    for(size_t i=0;i<=rest.size();){
        size_t j=rest.find_first_of("/\\",i);if(j==std::string::npos)j=rest.size();
        const auto part=std::string_view(rest).substr(i,j-i);i=j+1;
        if(part.empty()||part==".")continue;
        if(part==".."){if(depth){path.erase(path.rfind(L'\\'));--depth;}continue;}
        path+=L'\\';for(char c:part)path+=wchar_t(static_cast<unsigned char>(c));++depth;
    }
    return base+path+L'\\';
}
}
