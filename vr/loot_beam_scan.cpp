// Loot beams: the beam-worthy items lying near the hero, found on the game thread.
#include "loot_beam_scan.h"
#include <windows.h>
#include <D2RLPlugin/api.h>
#include "melee.h"
#include "settings_menu.h"
#include "../steamvr/memory.h"
#include "../steamvr/logging.h"
#include <atomic>
#include <cstdio>
#include <mutex>

namespace lootscan {
namespace {
namespace beams=framevr::lootbeams;
const D2RL::PluginContext* context=nullptr;
const D2RL::ThreadService* threads=nullptr;
const D2RL::DataTableService* tables=nullptr;
beams::Wanted wanted{};
std::atomic<bool> enabled{false},posted{false};
std::atomic<uint64_t> nextScan{0},postedAt{0};
std::mutex shownMutex;
Shown shown;

template<class T>bool Read(uintptr_t address,T& out) {
    return address>0x10000&&address<=UINTPTR_MAX-sizeof out&&
        framevr::ReadMemory(&out,reinterpret_cast<void*>(address),sizeof out);
}

// Rune class ids (rows of the combined Items table), per data bank. Game thread.
struct Runes {uint8_t bank=0;std::array<uint32_t,beams::LastRune+1> classId{};bool tried=false;};
Runes runes;
void KnowRunes(uintptr_t game) {
    uint8_t bank=0;
    if(!tables||!tables->findRowByCode||!Read(game+0x106,bank)||bank<1||bank>3)return;
    if(runes.tried&&runes.bank==bank)return;   // once per bank, as soon as any rune is found
    Runes found;found.bank=bank;
    for(int n=beams::FirstRune;n<=beams::LastRune;++n) {
        D2RL::DataTables::RowView row{};row.structSize=D2RL::DataTables::RowViewSize;
        const uint32_t code=uint32_t('r')|uint32_t('0'+n/10)<<8|uint32_t('0'+n%10)<<16|uint32_t(' ')<<24;
        if(tables->findRowByCode(context,D2RL::DataTables::Bank(bank),D2RL::DataTables::TableId::Items,code,&row)==D2RL::DataTables::Result::Success)
            {found.classId[size_t(n)]=row.rowIndex;found.tried=true;}
    }
    runes=found;   // none found (the table busy, say): asked again next scan
}
int RuneOf(uint32_t classId) {
    if(!classId)return 0;
    for(int n=beams::FirstRune;n<=beams::LastRune;++n)if(runes.classId[size_t(n)]==classId)return n;
    return 0;
}

void __cdecl Scan(const D2RL::PluginContext*,void*) noexcept {
    posted.store(false);
    const uint64_t now=GetTickCount64();
    uintptr_t game=0,self=0,heroPath=0;uint16_t hero[4]{};
    if(!melee::LocalServer(game,self)||!Read(self+0x38,heroPath)||!Read(heroPath,hero)) {
        std::lock_guard lock(shownMutex);shown={};return;
    }
    KnowRunes(game);
    // VR Settings > Interface > Loot beams: which rarities, read live (Apply takes at the next scan).
    const auto values=vrsettings::Read().values;
    beams::Wanted want=wanted;   // the rune threshold from [loot] rune_min
    want.unique=values[framevr::settings::LootBeamUnique]>.5f;
    want.set=values[framevr::settings::LootBeamSet]>.5f;
    want.rare=values[framevr::settings::LootBeamRare]>.5f;
    want.runes=values[framevr::settings::LootBeamRunes]>.5f;
    const float strength=beams::Strength(values[framevr::settings::LootBeamBrightness]);
    // Item units in the server's item table, the floor's beam-worthy ones within range of the
    // hero (bounded walk). The range is checked here, before the 64 slots fill: rares left
    // lying round the act must not crowd out the one at the hero's feet.
    uintptr_t buckets[128]{};
    if(!Read(game+beams::ServerItemTable,buckets))return;
    std::array<beams::Lying,64> lying{};size_t kept=0;
    unsigned visited=0;
    for(uintptr_t unit:buckets)for(unsigned chain=0;unit&&chain<256&&visited<4096;++chain,++visited) {
        uintptr_t next=0,data=0,path=0;uint32_t header[4]{};
        if(!Read(unit+0x158,next)||!Read(unit,header))break;
        if(header[0]==4&&beams::OnFloor(header[3])&&kept<lying.size()) {
            uint32_t quality=0;int x=0,y=0;
            if(Read(unit+0x10,data)&&Read(data,quality)&&Read(unit+0x38,path)&&Read(path+0x10,x)&&Read(path+0x14,y)&&
               beams::InRange(x,y,hero[1],hero[3])) {
                const auto beam=beams::Classify(quality,RuneOf(header[1]),want);
                if(beam!=beams::Beam::None)lying[kept++]={header[2],x,y,beam};
            }
        }
        unit=next;
    }
    Shown latest;
    latest.items=beams::Select(lying.data(),kept,hero[1],hero[3],latest.count);
    latest.at=now;
    latest.strength=strength;
    size_t before=0;
    {std::lock_guard lock(shownMutex);before=shown.count;shown=latest;}
#ifdef D2RVR_DEVELOPER
    if(latest.count!=before) {
        char line[96];
        snprintf(line,sizeof line,"Frame loot beams: %zu shown (was %zu)",latest.count,before);
        framevr::logging::Write(line);
    }
#else
    (void)before;
#endif
}
}

void Init(const D2RL::PluginContext* ctx,bool on,const beams::Wanted& want) {
    context=ctx;wanted=want;
    if(!ctx||ctx->QueryService(&threads)!=D2RL::ServiceQueryResult::Success)threads=nullptr;
    if(!ctx||ctx->QueryService(&tables)!=D2RL::ServiceQueryResult::Success||
       !D2RL::HasDataTableServiceField(tables,D2RL::DataTableServiceRequiredSize))tables=nullptr;
    enabled.store(on&&threads&&threads->runOnGameThread);
    char line[160];
    snprintf(line,sizeof line,"Frame loot beams: %s (runes from r%02d; rarities: VR Settings > Interface)",
             enabled.load()?"on":on?"off: no game-thread service":"off ([loot] beams=0)",wanted.highRune);
    framevr::logging::Write(line);
}
void Tick(uint64_t now) {
    if(!enabled.load()||now<nextScan.load())return;
    // One scan in flight; one the game never ran (a menu, a level load) is let go after 2 s.
    if(posted.load()&&now-postedAt.load()<2000)return;
    nextScan.store(now+250);postedAt.store(now);posted.store(true);
    if(threads->runOnGameThread(context,&Scan,nullptr)!=D2RL::Threads::Result::Success)posted.store(false);
}
Shown Latest(uint64_t now,uint64_t maxAgeMs) {
    std::lock_guard lock(shownMutex);
    if(!shown.at||now<shown.at||now-shown.at>maxAgeMs)return {};
    return shown;
}
}
