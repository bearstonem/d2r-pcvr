// The Map view = Full map patterns (cleanroom/sigscan/game_sigs.h MapPanelDraw, AutomapScale,
// MapPanelVtable, AutomapSmall) against minimal byte windows of D2R 3.3.93847's code at each
// pattern's site (unhooked bytes): each pattern matches at its site and resolves to the address
// the feature uses, and a code entry's hook bytes are its first bytes. Uniqueness across the whole
// image was checked once against an in-memory image (not committed).
#include "game_sigs.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static void Check(bool ok,const char* what){if(!ok){std::printf("FAILED: %s\n",what);std::exit(1);}}

struct Window {const char* name;uint64_t site;uint8_t bytes[64];size_t size;};
static const Window kWindows[]={
    {"MapPanelDraw",0x14E3480,{0x4c,0x8b,0xdc,0x55,0x53,0x56,0x57,0x41,0x54,0x49,0x8d,0x6b,0xa1,0x48,0x81,0xec,0xe0,0x00,0x00,0x00,0x48,0x8b,0x05,0x2d,0x7e,0x4e,0x01,0x48,0x33,0xc4,0x48,0x89,0x45,0xff,0x4d,0x89,0x7b,0x18,0x48,0x8b,0xf9,0x41,0x0f,0x29,0x73,0xc8,0x41,0x0f,0x29,0x7b,0xb8,0xe8,0x68,0x25,0xdc,0xff,0x0f,0xb6,0x88,0x18,0x01,0x00,0x00,0x84},64},
    {"AutomapScale",0xD1940,{0x4c,0x89,0x44,0x24,0x18,0x53,0x55,0x56,0x57,0x41,0x54,0x41,0x56,0x41,0x57,0x48,0x83,0xec,0x50,0x0f,0x10,0x02,0x33,0xc0,0x49,0x8b,0xf8,0xf3,0x0f,0x59,0x1d,0xc9,0x27,0xbf,0x01,0x4c,0x8b,0xfa,0x48,0x89},40},
    {"MapPanelVtable",0x14E3385,{0x4c,0x8b,0xc7,0x48,0x8b,0xd6,0x48,0x8b,0xc8,0xe8,0xed,0x45,0x37,0xff,0x48,0x8d,0x05,0x0e,0x95,0xaf,0x00,0x33,0xf6,0x48,0x89,0x03,0x48,0x8d,0xbb,0x68,0x01,0x00,0x00,0x66,0x66,0x0f},36},
    {"AutomapSmall",0xD3D84,{0x83,0xf9,0x01,0x0f,0x94,0x05,0x47,0x70,0x95,0x02,0x3b,0x0d,0x3c,0x70,0x95,0x02,0x74,0x1d,0x33,0xc0,0x85,0xc9,0x0f,0x95,0xc0,0x89,0x05,0x2d,0x70,0x95,0x02,0xe8},32},
};
// "4C ?? 8B" -> bytes, -1 for ??
static std::vector<int> Parse(const char* p){
    std::vector<int> v;
    for(const char* c=p;*c;){
        while(*c==' ')++c;
        if(!*c)break;
        if(c[0]=='?'){v.push_back(-1);c+=2;continue;}
        v.push_back(int(std::strtol(std::string(c,2).c_str(),nullptr,16)));c+=2;
    }
    return v;
}
static bool Matches(const uint8_t* at,size_t size,const std::vector<int>& p){
    if(p.size()>size)return false;
    for(size_t i=0;i<p.size();++i)if(p[i]>=0&&at[i]!=p[i])return false;
    return true;
}
static const d2rsig::Sig* Find(const char* name){
    for(const auto& s:d2rsig::kSigs)if(std::strcmp(s.name,name)==0)return &s;
    return nullptr;
}

int main(){
    for(const Window& w:kWindows){
        const d2rsig::Sig* s=Find(w.name);
        std::string what=std::string(w.name)+": ";
        Check(s!=nullptr,(what+"in the table").c_str());
        Check(s->site==w.site,(what+"site").c_str());
        const auto pattern=Parse(s->pattern);
        Check(pattern.size()>=24,(what+"a long pattern").c_str());
        Check(Matches(w.bytes,w.size,pattern),(what+"matches at its site").c_str());
        uint64_t got=0;
        if(s->kind==d2rsig::Kind::Code)got=w.site+s->offset;
        else {
            Check(s->kind==d2rsig::Kind::Ref&&size_t(s->offset)+s->dispAt+4<=w.size,(what+"a reference inside the window").c_str());
            int32_t disp;std::memcpy(&disp,w.bytes+s->offset+s->dispAt,4);
            got=w.site+s->offset+s->insnLen+int64_t(disp)+s->addend;
        }
        Check(got==s->rva,(what+"resolves to the address the feature uses").c_str());
        if(s->body)Check(Matches(w.bytes,w.size,Parse(s->body)),(what+"hook bytes are its first bytes").c_str());
        Check(std::strcmp(s->area,"Map")==0,(what+"in the Map group").c_str());
    }
    // the group is one block at the end of the table (the settings program groups neighbors)
    const size_t n=sizeof(d2rsig::kSigs)/sizeof(d2rsig::kSigs[0]);
    Check(std::strcmp(d2rsig::kSigs[n-4].name,"MapPanelDraw")==0&&std::strcmp(d2rsig::kSigs[n-1].name,"AutomapSmall")==0,"the Map entries last, together");
    std::printf("map sigs: all passed\n");
    return 0;
}
