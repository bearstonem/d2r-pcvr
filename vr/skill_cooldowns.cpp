#include <windows.h>
#include "skill_cooldowns.h"
#include "melee.h"
#include "memory.h"
#include "sigscan.h"
#include "logging.h"
#include <cstdio>
namespace gamestate {uint64_t LocalPlayer();}
namespace cooldownstate {
namespace {
using namespace framevr::cooldowns;
template<class T>bool Read(uintptr_t p,T& out){return p>=0x10000&&p<=UINTPTR_MAX-sizeof out&&framevr::ReadMemory(&out,reinterpret_cast<void*>(p),sizeof out);}
// The game's code these reads stand on (steamvr/skill_cooldowns.h), D2R 3.3.93787's unhooked bytes; 3.3.94995 keeps
// them. The calls: the use check's delay test, a unit's stat list for a state, and the state test active_effects.cpp
// already uses. The layouts: each checked where the game itself reads or writes them.
constexpr uint64_t DelayTestRva=0x3404a0,StatListRva=0x2f5940,CheckStateRva=0x3351b0;
constexpr uint8_t DelayTestSig[]={0x40,0x53,0x56,0x57,0x41,0x54,0x41,0x57,0x48,0x83,0xec,0x30,0x48,0x8b,0xda,0x48,0x8b,0xf9};   // (unit, skill node): 0 blocked
constexpr uint8_t StatListSig[]={0x48,0x89,0x5c,0x24,0x10,0x57,0x48,0x83,0xec,0x20,0x8b,0xda,0x48,0x8b,0xf9,0x48,0x85,0xc9};    // (unit, state)
constexpr uint8_t CheckStateSig[]={0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x20,0x8b,0xda,0x48,0x8b,0xf1};
constexpr uint8_t CheckStateTailSig[]={0x5f,0xe9,0x9a,0x0f,0xfc,0xff};
constexpr uint8_t TestOwnSig[]={0xba,0xb9,0x00,0x00,0x00,0x48,0x8b,0xcf};      // 3404C7: the delay test asks state 185 first,
constexpr uint8_t TestStatSig[]={0x41,0xb8,0x67,0x01,0x00,0x00};               // 340516: its stat 359 with the skill as layer,
constexpr uint8_t TestSharedSig[]={0xba,0x79,0x00,0x00,0x00,0x48,0x8b,0xcf};   // 34052D: then state 121
constexpr uint8_t OwnFrameSig[]={0x03,0x9e,0x70,0x01,0x00,0x00};               // 439550: a skill's own delay ends at its frames + game+0x170,
constexpr uint8_t OwnStateSig[]={0x41,0xb9,0xb9,0x00,0x00,0x00};               // 439573: kept with state 185
constexpr uint8_t OwnLayerSig[]={0x41,0xb8,0x67,0x01,0x00,0x00};               // 339FB6: as stat 359 = that frame, layer the skill
constexpr uint8_t StatsAtSig[]={0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0xf1,0x49,0x8b,0xf8,0x48,0x83,0xc1,0x30};   // 2F9A80: a list's stats at +0x30
constexpr uint8_t StatsSearchSig[]={0x40,0x53,0x48,0x83,0xec,0x30,0x4c,0x8b,0x11,0x48,0x8b,0xd9,0x4c,0x8b,0x59,0x08};   // 2F99D0: their entries, then their count
constexpr uint8_t ExpireSig[]={0x48,0x85,0xc9,0x74,0x14,0x85,0xd2,0x7e,0x04,0x83,0x49,0x1c,0x02,0x66,0x0f,0x6e,0xc2,0x0f,0x5b,0xc0,0xf3,0x0f,0x11,0x41,0x24,0xc3};   // 2F7AB0: the end frame
struct Site {uint64_t rva;const uint8_t* bytes;uint32_t size;};
constexpr Site Sites[]={
    {DelayTestRva,DelayTestSig,sizeof DelayTestSig},{StatListRva,StatListSig,sizeof StatListSig},
    {CheckStateRva,CheckStateSig,sizeof CheckStateSig},{0x335280,CheckStateTailSig,sizeof CheckStateTailSig},
    {0x3404c7,TestOwnSig,sizeof TestOwnSig},{0x340516,TestStatSig,sizeof TestStatSig},{0x34052d,TestSharedSig,sizeof TestSharedSig},
    {0x439550,OwnFrameSig,sizeof OwnFrameSig},{0x439573,OwnStateSig,sizeof OwnStateSig},{0x339fb6,OwnLayerSig,sizeof OwnLayerSig},
    {0x2f9a80,StatsAtSig,sizeof StatsAtSig},{0x2f99d0,StatsSearchSig,sizeof StatsSearchSig},{0x2f7ab0,ExpireSig,sizeof ExpireSig}};
constexpr uintptr_t GameFrameOffset=0x170,UnitSkillsOffset=0x100,ListFlagsOffset=0x1c,ListExpireOffset=0x24,
                    ListStatsOffset=0x30,ListStatCountOffset=0x38,NodeNextOffset=0x8;
// The game decrypts its code page by page as it first runs it: until every site reads as studied, look again
// every 2 s (the delay test runs with the first skill the hero uses).
bool ready=false;uint64_t retryAt=0;unsigned tries=0;
bool Ready() {
    if(ready)return true;
    const auto now=GetTickCount64();
    if(now<retryAt)return false;
    retryAt=now+2000;
    size_t missing=0;
    for(const auto& s:Sites)if(!d2rsig::Check(s.rva,s.bytes,s.size)){missing=size_t(&s-Sites)+1;break;}
    if(!missing) {
        ready=true;
        framevr::logging::Write("Frame skill cooldowns: the use check's delay test, each skill's own delay (state 185, stat 359) and the shared delay (state 121) read as studied");
        return true;
    }
    if(++tries==30) {   // a minute without: say so once (the code may not have run yet, or differs)
        char line[200];std::snprintf(line,sizeof line,"Frame skill cooldowns: the game's code at 0x%llX does not read as studied yet; no wrist cooldowns until it does",
            (unsigned long long)Sites[missing-1].rva);
        framevr::logging::Write(line);
    }
    return false;
}
// The log: the first cooldowns a session, then every 50th (Frozen Orb casts one a second).
uint64_t logSession=0;unsigned starts=0;Board last;
void Report(uint64_t session,const Board& now) {
    if(session!=logSession){logSession=session;starts=0;last={};}
    for(int i=0;i<now.count;++i) {
        const auto& c=now.list[size_t(i)];
        bool before=false;
        for(int j=0;j<last.count;++j)before=before||(last.list[size_t(j)].skill==c.skill&&last.list[size_t(j)].endFrame==c.endFrame);
        if(before)continue;
        ++starts;
        if(starts>12&&starts%50)continue;
        char line[200];std::snprintf(line,sizeof line,"Frame skill cooldowns: skill %d blocked by its delay, ready in %d frames (%u ms); %u this session",
            c.skill,c.endFrame-now.frame,unsigned(std::max(0,c.endFrame-now.frame))*FrameMs,starts);
        framevr::logging::Write(line);
    }
    last=now;
}
}
Board Sample(uint64_t session) {
    Board board;uintptr_t game=0,player=0;uint32_t header[4]{};
    if(!session||session!=gamestate::LocalPlayer()||!Ready()||!melee::LocalServer(game,player)||!Read(player,header)||header[0]!=0)return board;
    int frame=0;uintptr_t skills=0,node=0;
    if(!Read(game+GameFrameOffset,frame)||frame<=0||!Read(player+UnitSkillsOffset,skills)||!Read(skills,node))return board;
    const auto state=reinterpret_cast<int(*)(uintptr_t,int)>(d2rsig::Addr(CheckStateRva));
    const auto listOf=reinterpret_cast<uintptr_t(*)(uintptr_t,int)>(d2rsig::Addr(StatListRva));
    const auto delayFree=reinterpret_cast<int(*)(uintptr_t,uintptr_t)>(d2rsig::Addr(DelayTestRva));
    // The shared delay: state 121's stat list, ending on its expiry frame.
    const bool sharedOn=state(player,SharedDelayState)!=0;
    int sharedEnd=0;
    if(sharedOn)if(const auto list=listOf(player,SharedDelayState)) {
        uint32_t flags=0;float expire=0;
        if(Read(list+ListFlagsOffset,flags)&&Read(list+ListExpireOffset,expire))sharedEnd=SharedEnd(flags,expire);
    }
    // Each skill's own: stat 359 in state 185's stat list, its layer the skill, valued its end frame.
    uintptr_t stats=0;uint64_t count=0;
    if(state(player,OwnDelayState))if(const auto list=listOf(player,OwnDelayState))
        if(!Read(list+ListStatsOffset,stats)||!Read(list+ListStatCountOffset,count))stats=0;
    board.known=true;board.frame=frame;board.at=GetTickCount64();
    unsigned visited=0;
    for(;node&&visited<256;++visited) {
        uintptr_t row=0,next=0;int16_t id=-1;
        if(!Read(node,row)||!Read(node+NodeNextOffset,next)||!Read(row,id)||id<0){board={};return board;}
        if(!delayFree(player,node)) {
            int32_t own=0;
            const bool ownSet=stats&&FindStat(count,OwnDelayStat,id,[&](uint64_t i,StatEntry& e){return Read(stats+uintptr_t(i)*sizeof(StatEntry),e);},own);
            board.Add(id,ReadyFrame(frame,ownSet?own:0,sharedOn,sharedEnd));
        }
        if(next==node)break;
        node=next;
    }
    if(node&&visited>=256){board={};return board;}   // not a list's end: the read is not trusted
    Report(session,board);
    return board;
}
}
