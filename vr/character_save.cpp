#include "character_save.h"
#include <windows.h>
#include <shlobj.h>
#include <D2RLPlugin/api.h>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <string>
#include <vector>
#include "../steamvr/logging.h"

namespace charsave {
namespace {
using namespace framevr::charsave;
std::mutex mutex;
std::wstring folder;   // this mod's save folder under Saved Games
Status shown;
ULONGLONG lookedAt=0;
int lookedClass=-2;
struct Parsed {FILETIME time;Status status;};
std::map<std::wstring,Parsed> parsed;   // per file: read again only when it is written

std::string Narrow(const std::wstring& w){std::string s;for(wchar_t c:w)s+=c<128?char(c):'?';return s;}
bool Same(FILETIME a,FILETIME b){return CompareFileTime(&a,&b)==0;}
bool ReadSave(const std::wstring& path,Status& out){
    HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);
    if(file==INVALID_HANDLE_VALUE)return false;
    uint8_t data[1024];DWORD got=0;   // the header ends near 0x14b
    const bool read=ReadFile(file,data,sizeof data,&got,nullptr);CloseHandle(file);
    return read&&Parse(data,got,out);
}
void Look(int heroClass){
    Status none;none.problem=Problem::NoFolder;
    if(folder.empty()){shown=none;return;}
    struct File {std::wstring name;FILETIME time;};
    std::vector<File> files;
    WIN32_FIND_DATAW f{};
    const HANDLE find=FindFirstFileW((folder+L"*.d2s").c_str(),&f);
    if(find!=INVALID_HANDLE_VALUE){
        do if(!(f.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY))files.push_back({f.cFileName,f.ftLastWriteTime});while(FindNextFileW(find,&f));
        FindClose(find);
    }
    none.problem=files.empty()?Problem::NoSave:Problem::NotThisClass;
    std::sort(files.begin(),files.end(),[](const File& a,const File& b){return CompareFileTime(&a.time,&b.time)>0;});
    // The newest save of this class: the character being played was saved last.
    for(size_t i=0;i<files.size()&&i<16;++i){
        auto cached=parsed.find(files[i].name);
        if(cached==parsed.end()||!Same(cached->second.time,files[i].time)){
            Status s;
            if(!ReadSave(folder+files[i].name,s)){none.problem=Problem::Unreadable;continue;}   // mid-write: next look
            const auto stem=files[i].name.substr(0,files[i].name.size()-4);
            if(!s.name[0])std::snprintf(s.name,sizeof s.name,"%s",Narrow(stem).c_str());
            cached=parsed.insert_or_assign(files[i].name,Parsed{files[i].time,s}).first;
        }
        if(heroClass<0||cached->second.status.heroClass==heroClass){shown=cached->second.status;return;}
    }
    shown=none;
}
}
void Init(const D2RL::PluginContext* ctx){
    PWSTR saved=nullptr;std::wstring savedGames;
    if(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_SavedGames,0,nullptr,&saved))&&saved)savedGames=saved;
    CoTaskMemFree(saved);
    if(savedGames.empty()){framevr::logging::Write("Frame character save: no Saved Games folder; the character's save cannot be read");return;}
    // the active mod's modinfo.json names its save folder ("../" is the unmodded one)
    const std::string mod=ctx&&ctx->activeMod?ctx->activeMod:"";
    std::string savepath;bool has=false;
    if(!mod.empty()&&ctx->modDirectory)try{
        std::ifstream input(std::filesystem::path(ctx->modDirectory)/L"modinfo.json",std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(input)),std::istreambuf_iterator<char>());
        has=SavePathOf(text,savepath);
    }catch(...){}
    std::lock_guard lock(mutex);
    folder=SaveFolder(savedGames,mod,has,savepath);
    const std::string where="Saved Games"+Narrow(folder.substr(savedGames.size()));   // no user name in the log
    char line[260];std::snprintf(line,sizeof line,"Frame character save: saves read from %s (mod '%s', savepath %s)",where.c_str(),mod.c_str(),has?savepath.c_str():"none");
    framevr::logging::Write(line);
}
// Quick cast: how many saves of this class are at this level (each header read once a write, as Look does). More than one:
// the newest is not proof of who is played; the radial keeps its ring for this game only.
int SameLevel(int heroClass,int level){
    std::lock_guard lock(mutex);
    if(folder.empty())return 0;
    int n=0;WIN32_FIND_DATAW f{};
    const HANDLE find=FindFirstFileW((folder+L"*.d2s").c_str(),&f);
    if(find==INVALID_HANDLE_VALUE)return 0;
    do{
        if(f.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)continue;
        Status s;auto cached=parsed.find(f.cFileName);
        if(cached!=parsed.end()&&Same(cached->second.time,f.ftLastWriteTime))s=cached->second.status;
        else if(!ReadSave(folder+f.cFileName,s))continue;
        n+=s.heroClass==heroClass&&s.level==level;
    }while(FindNextFileW(find,&f));
    FindClose(find);
    return n;
}
Status Read(int heroClass){
    std::lock_guard lock(mutex);
    const auto now=GetTickCount64();
    if(!lookedAt||now-lookedAt>=1500||heroClass!=lookedClass){lookedAt=now;lookedClass=heroClass;Look(heroClass);}
    return shown;
}
}
