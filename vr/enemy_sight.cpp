// Offline VR monster sight / awareness distance scaling via D2RLoader's
// DataTableService (MonStats aidist and archetype awareness aip parameters).
// Captures the exact baseline bytes before any edit and restores them when set
// to 100%, when switching to desktop mode, or when leaving offline play.
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <mutex>
#include <vector>
#include "enemy_sight.h"
#include "../steamvr/enemy_sight.h"
#include "../steamvr/callback_ticket.h"
#include "offline_difficulty_signatures.h"
#include "sigscan.h"
#include "memory.h"
#include "logging.h"

namespace enemysight {
namespace {
using namespace D2RL;
using ModeFn=int(*)();

struct BankState {
    const void* rows=nullptr;
    uint32_t rowCount=0;
    int appliedPercent=framevr::enemysight::DefaultPercent;
    std::vector<framevr::enemysight::RowBaseline> baseline;
};

const PluginContext* g_ctx=nullptr;
const DataTableService* g_tables=nullptr;
const ThreadService* g_threads=nullptr;
ModeFn mode=nullptr;
std::atomic<int> wanted{framevr::enemysight::DefaultPercent};
std::atomic<int> lastApplied{framevr::enemysight::DefaultPercent};
std::atomic<size_t> lastUpdatedRows{0};
std::atomic<bool> anyModified{false};
bool installed=false,toldMissing=false,toldLayoutMismatch=false;
std::mutex ticketMutex;
framevr::CallbackTicket syncTicket;
BankState banks[3]{}; // Classic, Lod, Rotw

template <class S> const S* Query(const PluginContext* ctx) {
    const S* s=nullptr;
    return ctx&&ctx->QueryService(&s)==ServiceQueryResult::Success?s:nullptr;
}

int EffectivePercent() {
    const int choice=wanted.load();
    if(choice==framevr::enemysight::DefaultPercent)return framevr::enemysight::DefaultPercent;
    if(!mode||mode()!=0)return framevr::enemysight::DefaultPercent;
    return choice;
}

bool WriteHostileRows(void* liveRows,const std::vector<framevr::enemysight::RowBaseline>& baseline,int percent,size_t& updatedOut) {
    using namespace framevr::enemysight;
    const int pct=NormalizePercent(float(percent));
    auto* base=static_cast<uint8_t*>(liveRows);
    size_t updated=0;
    for(size_t i=0;i<baseline.size();++i) {
        const auto& b=baseline[i];
        if(!b.hostile)continue;
        uint8_t aidist[3]{};
        int16_t aip[8][3]{};
        ComputeRowAiFields(b,pct,aidist,aip);
        uint8_t* row=base+i*RowSize;
        if(!framevr::WriteMemory(row+AiDistOffset,aidist,sizeof aidist))return false;
        if(!framevr::WriteMemory(row+AipOffset,aip,sizeof aip))return false;
        ++updated;
    }
    updatedOut=updated;
    return true;
}

void SyncBanks(const PluginContext* ctx) {
    using namespace framevr::enemysight;
    if(!ctx||!g_tables||!g_tables->getTable)return;
    const int target=EffectivePercent();
    static constexpr DataTables::Bank kBanks[3]={
        DataTables::Bank::Classic,
        DataTables::Bank::Lod,
        DataTables::Bank::Rotw,
    };
    bool modifiedNow=false;
    bool syncedAny=false;
    size_t maxRowsUpdated=0;
    for(int b=0;b<3;++b) {
        auto& state=banks[b];
        if(target==DefaultPercent&&state.baseline.empty())continue;
        DataTables::TableView view{};
        view.structSize=DataTables::TableViewSize;
        if(g_tables->getTable(ctx,kBanks[b],DataTables::TableId::MonStats,&view)!=DataTables::Result::Success||
           !view.rows||view.rowSize!=RowSize||view.rowCount<MinProbeRows) {
            if(state.appliedPercent!=DefaultPercent)modifiedNow=true;
            continue;
        }
        if(state.rows!=view.rows||state.rowCount!=view.rowCount||state.baseline.empty()) {
            std::vector<uint8_t> snapshot(size_t(view.rowCount)*RowSize);
            if(!framevr::ReadMemory(snapshot.data(),view.rows,snapshot.size())||
               !CaptureBaseline(snapshot,view.rowCount,state.baseline)) {
                state.baseline.clear();
                if(!toldLayoutMismatch) {
                    toldLayoutMismatch=true;
                    framevr::logging::Write("Frame enemy sight: MonStats layout probe mismatch; leaving monster sight unchanged");
                }
                continue;
            }
            state.rows=view.rows;
            state.rowCount=view.rowCount;
            state.appliedPercent=DefaultPercent;
        }
        if(state.appliedPercent!=target) {
            size_t updated=0;
            if(WriteHostileRows(const_cast<void*>(view.rows),state.baseline,target,updated)) {
                state.appliedPercent=target;
                syncedAny=true;
                maxRowsUpdated=std::max(maxRowsUpdated,updated);
            }
        } else {
            syncedAny=true;
            size_t count=0;
            for(const auto& r:state.baseline)if(r.hostile)++count;
            maxRowsUpdated=std::max(maxRowsUpdated,count);
        }
        if(state.appliedPercent!=DefaultPercent)modifiedNow=true;
    }
    anyModified.store(modifiedNow);
    if(syncedAny||(target==DefaultPercent&&!modifiedNow)) {
        if(maxRowsUpdated>0)lastUpdatedRows.store(maxRowsUpdated);
        lastApplied.store(target);
    }
}

void __cdecl SyncTablesOnUiThread(const PluginContext* ctx,void* user) noexcept {
    const auto token=reinterpret_cast<uintptr_t>(user);
    {
        std::lock_guard lock(ticketMutex);
        if(!syncTicket.Claim(token))return;
    }
    SyncBanks(ctx);
}
} // namespace

void Init(const PluginContext* ctx) {
    g_ctx=ctx;
    g_tables=Query<DataTableService>(ctx);
    g_threads=Query<ThreadService>(ctx);
}

void Configure(int percent,bool vr) {
    using namespace framevr::enemysight;
    const int next=vr?NormalizePercent(float(percent)):DefaultPercent;
    if(wanted.exchange(next)!=next) {
        char line[160];
        std::snprintf(line,sizeof line,
            "Frame enemy sight: requested=%d%% (100%%=native default); offline VR only",next);
        framevr::logging::Write(line);
    }
}

void Install() {
    if(installed)return;
    using namespace offlinedifficulty::abi;
    if(!d2rsig::Check(ModeRva,ModeSig,sizeof ModeSig)) {
        if(!toldMissing) {
            toldMissing=true;
            framevr::logging::Write("Frame enemy sight: native mode signature unavailable; leaving sight unchanged (will retry)");
        }
        return;
    }
    mode=reinterpret_cast<ModeFn>(d2rsig::Addr(ModeRva));
    if(!mode)return;
    installed=true;
    framevr::logging::Write("Frame enemy sight: MonStats sight-distance adapter ready; offline VR only");
}

void Tick() {
    if(!installed||!g_ctx||!g_tables||!g_threads||!g_threads->runOnUiThread)return;
    const int target=EffectivePercent();
    if(target==framevr::enemysight::DefaultPercent&&!anyModified.load()&&lastApplied.load()==framevr::enemysight::DefaultPercent)
        return;
    uintptr_t token=0;
    {
        std::lock_guard lock(ticketMutex);
        const uint64_t now=GetTickCount64();
        if(syncTicket.Expired(now,1000))syncTicket.Cancel();
        token=syncTicket.Start(now);
    }
    if(!token)return;
    if(g_threads->runOnUiThread(g_ctx,&SyncTablesOnUiThread,reinterpret_cast<void*>(token))!=Threads::Result::Success) {
        std::lock_guard lock(ticketMutex);
        syncTicket.Claim(token);
    }
}

void Report() {
    static int reportedPercent=framevr::enemysight::DefaultPercent;
    const int applied=lastApplied.load();
    if(applied!=reportedPercent) {
        reportedPercent=applied;
        char line[160];
        std::snprintf(line,sizeof line,
            "Frame enemy sight: active sight distance=%d%% across %zu hostile monster rows",
            applied,lastUpdatedRows.load());
        framevr::logging::Write(line);
    }
}

void Shutdown() {
    wanted.store(framevr::enemysight::DefaultPercent);
    if(anyModified.load()&&g_ctx&&g_tables) {
        SyncBanks(g_ctx);
    }
    for(auto& b:banks)b=BankState{};
    anyModified.store(false);
    lastApplied.store(framevr::enemysight::DefaultPercent);
    std::lock_guard lock(ticketMutex);
    syncTicket.Cancel();
}

int AppliedPercent() {
    return lastApplied.load();
}
} // namespace enemysight

