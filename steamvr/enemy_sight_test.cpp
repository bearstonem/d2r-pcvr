// Exercise pure MonStats sight-distance scaling rules and the live adapter
// against synthetic D2RLoader services, signatures and table buffers.
#include "../vr/enemy_sight.cpp"
#undef NDEBUG
#include <cassert>
#include <cstring>
#include <vector>

namespace {
bool signatures=false;
int clientMode=0,modeCalls=0,uiQueueCalls=0,tableQueryCalls=0;
bool failUiQueue=false;
std::vector<uint8_t> lodTable;
std::vector<uint8_t> rotwTable;

int Mode(){++modeCalls;return clientMode;}

void PopulateCanonicalTable(std::vector<uint8_t>& buf,size_t rows=250) {
    using namespace framevr::enemysight;
    buf.assign(rows*RowSize,0);
    auto setRow=[&](uint16_t idx,uint16_t ai,uint8_t align,uint8_t flags1,uint8_t flags3,
                    uint8_t threat,uint8_t d0,uint8_t d1,uint8_t d2,
                    uint8_t dist0,uint8_t dist1,uint8_t dist2) {
        uint8_t* r=buf.data()+size_t(idx)*RowSize;
        std::memcpy(r+HcIdxOffset,&idx,sizeof idx);
        r[Flags1Offset]=flags1;
        r[Flags3Offset]=flags3;
        std::memcpy(r+AiOffset,&ai,sizeof ai);
        r[AlignOffset]=align;
        r[ThreatOffset]=threat;
        r[AiDelOffset+0]=d0;r[AiDelOffset+1]=d1;r[AiDelOffset+2]=d2;
        r[AiDistOffset+0]=dist0;r[AiDistOffset+1]=dist1;r[AiDistOffset+2]=dist2;
    };
    // Probe rows required by ValidTableLayout:
    // row 0: skeleton1 (hostile, ai=2, aidist=0/0/0)
    setRow(0,2,0,0x80,0x02,10,15,14,13,0,0,0);
    // row 5: zombie1 (hostile, ai=3 -> aip2 awareness={10,15,20}, aidist=0/0/0)
    setRow(5,3,0,0x80,0x02,10,15,14,13,0,0,0);
    {
        uint8_t* r5=buf.data()+5*RowSize;
        const size_t aip2Off=AipOffset+6; // aip2
        WriteI16(r5+aip2Off+0,10);
        WriteI16(r5+aip2Off+2,15);
        WriteI16(r5+aip2Off+4,20);
        // Unrelated aip1 parameter must never change:
        WriteI16(r5+AipOffset+0,77);
    }
    // row 18: fallen1 (hostile, ai=6 -> aip2 approach={10,12,15}, aidist=0/0/0)
    setRow(18,6,0,0x80,0x02,10,15,13,12,0,0,0);
    {
        uint8_t* r18=buf.data()+18*RowSize;
        const size_t aip2Off=AipOffset+6; // aip2
        WriteI16(r18+aip2Off+0,10);
        WriteI16(r18+aip2Off+2,12);
        WriteI16(r18+aip2Off+4,15);
    }
    // row 160: cr_archer1 (hostile, ai=35 -> aip5=20, aip8=12, aidist=0/0/0)
    setRow(160,35,0,0x80,0x02,10,15,13,12,0,0,0);
    {
        uint8_t* r160=buf.data()+160*RowSize;
        const size_t aip5Off=AipOffset+4*6; // aip5
        const size_t aip8Off=AipOffset+7*6; // aip8
        for(int d=0;d<3;++d) {
            WriteI16(r160+aip5Off+d*2,20);
            WriteI16(r160+aip8Off+d*2,12);
        }
    }
    // row 170: sk_archer1 (hostile, ai=37 -> aip1=75, aip2=15, aip3=50, aip4=5, aip5=6, aidist=0/0/0)
    setRow(170,37,0,0x80,0x02,10,15,13,12,0,0,0);
    {
        uint8_t* r170=buf.data()+170*RowSize;
        for(int d=0;d<3;++d) {
            WriteI16(r170+AipOffset+0*6+d*2,75); // aip1
            WriteI16(r170+AipOffset+1*6+d*2,15); // aip2
            WriteI16(r170+AipOffset+2*6+d*2,50); // aip3
            WriteI16(r170+AipOffset+3*6+d*2,5);  // aip4
            WriteI16(r170+AipOffset+4*6+d*2,6);  // aip5
        }
    }
    // row 180: skmage_pois1 (hostile, ai=64 -> aip1=35, aip2=9, aip3=30, aip6=18, aip7=20, aip8=5, aidist=0/0/0)
    setRow(180,64,0,0x80,0x02,10,15,13,12,0,0,0);
    {
        uint8_t* r180=buf.data()+180*RowSize;
        for(int d=0;d<3;++d) {
            WriteI16(r180+AipOffset+0*6+d*2,35); // aip1
            WriteI16(r180+AipOffset+1*6+d*2,9);  // aip2
            WriteI16(r180+AipOffset+2*6+d*2,30); // aip3
            WriteI16(r180+AipOffset+5*6+d*2,18); // aip6
            WriteI16(r180+AipOffset+6*6+d*2,20); // aip7
            WriteI16(r180+AipOffset+7*6+d*2,5);  // aip8
        }
    }
    // row 10: friendly mercenary / ally (align=1, must never be modified)
    setRow(10,3,1,0x80,0x02,10,15,14,13,25,25,25);
    {
        uint8_t* r10=buf.data()+10*RowSize;
        WriteI16(r10+AipOffset+6,12);
    }
    // row 11: town NPC (align=0 but npc|inTown set, must never be modified)
    setRow(11,3,0,0x85,0x02,10,15,14,13,30,30,30);
    // row 242: mephisto (hostile, ai=50, aidist={0,40,46})
    setRow(242,50,0,0x80,0x02,14,15,10,6,0,40,46);
}

D2RL::DataTables::Result __cdecl FakeGetTable(const D2RL::PluginContext*,D2RL::DataTables::Bank bank,
                                              D2RL::DataTables::TableId tableId,D2RL::DataTables::TableView* out) noexcept {
    ++tableQueryCalls;
    if(!out||tableId!=D2RL::DataTables::TableId::MonStats)return D2RL::DataTables::Result::Unavailable;
    if(bank==D2RL::DataTables::Bank::Lod&&!lodTable.empty()) {
        out->rows=lodTable.data();
        out->rowCount=uint32_t(lodTable.size()/framevr::enemysight::RowSize);
        out->rowSize=uint32_t(framevr::enemysight::RowSize);
        return D2RL::DataTables::Result::Success;
    }
    if(bank==D2RL::DataTables::Bank::Rotw&&!rotwTable.empty()) {
        out->rows=rotwTable.data();
        out->rowCount=uint32_t(rotwTable.size()/framevr::enemysight::RowSize);
        out->rowSize=uint32_t(framevr::enemysight::RowSize);
        return D2RL::DataTables::Result::Success;
    }
    return D2RL::DataTables::Result::Unavailable;
}

D2RL::Threads::Result __cdecl FakeRunOnUiThread(const D2RL::PluginContext* ctx,D2RL::Threads::Callback cb,void* user) noexcept {
    ++uiQueueCalls;
    if(failUiQueue)return D2RL::Threads::Result::Unavailable;
    cb(ctx,user);
    return D2RL::Threads::Result::Success;
}

D2RL::DataTableService fakeTables{};
D2RL::ThreadService fakeThreads{};

D2RL::ServiceQueryResult __cdecl FakeQueryService(const D2RL::PluginContext*,D2RL::ServiceId serviceId,uint32_t,const void** outService) noexcept {
    if(serviceId==D2RL::ServiceId::DataTable) {
        *outService=&fakeTables;
        return D2RL::ServiceQueryResult::Success;
    }
    if(serviceId==D2RL::ServiceId::Thread) {
        *outService=&fakeThreads;
        return D2RL::ServiceQueryResult::Success;
    }
    *outService=nullptr;
    return D2RL::ServiceQueryResult::Unavailable;
}
} // namespace

namespace d2rsig {
bool Check(uint64_t rva,const void*,uint32_t) {
    assert(rva==offlinedifficulty::abi::ModeRva);
    return signatures;
}
uintptr_t Addr(uint64_t rva) {
    assert(rva==offlinedifficulty::abi::ModeRva);
    return reinterpret_cast<uintptr_t>(&Mode);
}
} // namespace d2rsig

namespace framevr::logging {
void Write(const char*){}
} // namespace framevr::logging

int main() {
    using namespace framevr::enemysight;

    // 1. Pure normalization and scaling rules
    assert(NormalizePercent(NAN)==100&&NormalizePercent(INFINITY)==100);
    assert(NormalizePercent(-50.f)==100&&NormalizePercent(20.f)==100&&NormalizePercent(94.f)==100&&NormalizePercent(104.f)==100&&NormalizePercent(106.f)==110);
    assert(NormalizePercent(100.f)==100&&NormalizePercent(200.f)==200&&NormalizePercent(296.f)==300&&NormalizePercent(999.f)==300);
    assert(AwarenessAipIndex(3)==2&&AwarenessAipIndex(6)==2&&AwarenessAipIndex(28)==3&&AwarenessAipIndex(52)==8&&AwarenessAipIndex(2)==0&&AwarenessAipIndex(50)==0);

    std::vector<uint8_t> original;
    PopulateCanonicalTable(original);
    std::vector<uint8_t> table=original;
    std::vector<RowBaseline> baseline;
    assert(CaptureBaseline(table,250,baseline));
    assert(baseline[0].hostile&&baseline[5].hostile&&baseline[18].hostile&&baseline[160].hostile&&baseline[170].hostile&&baseline[180].hostile&&baseline[242].hostile);
    assert(!baseline[10].hostile&&!baseline[11].hostile);

    // 150% sight scaling:
    assert(ApplySightPercent(table,baseline,150)==7);
    const uint8_t* r0=table.data()+0*RowSize;
    const uint8_t* r5=table.data()+5*RowSize;
    const uint8_t* r10=table.data()+10*RowSize;
    const uint8_t* r11=table.data()+11*RowSize;
    const uint8_t* r18=table.data()+18*RowSize;
    const uint8_t* r160=table.data()+160*RowSize;
    const uint8_t* r170=table.data()+170*RowSize;
    const uint8_t* r180=table.data()+180*RowSize;
    const uint8_t* r242=table.data()+242*RowSize;
    // skeleton1 (base 0 -> default 35 -> 150% = 53)
    assert(r0[AiDistOffset+0]==53&&r0[AiDistOffset+1]==53&&r0[AiDistOffset+2]==53);
    // zombie1 (base aidist 0 -> 53, gate aip2 covers scaledDist+1 = 54, unrelated aip1 stays 77)
    assert(r5[AiDistOffset+0]==53&&ReadI16(r5+AipOffset+6)==54&&ReadI16(r5+AipOffset+8)==54&&ReadI16(r5+AipOffset+10)==54);
    assert(ReadI16(r5+AipOffset+0)==77);
    // fallen1 (ai=6: base aidist 0 -> 53, approach gate aip2 covers scaledDist+1 = 54)
    assert(r18[AiDistOffset+0]==53&&ReadI16(r18+AipOffset+6)==54&&ReadI16(r18+AipOffset+8)==54&&ReadI16(r18+AipOffset+10)==54);
    // cr_archer1 (ai=35: aidist capped at MaxLosAiDist=49, aip5=17 < PATH_MAX_STEPNUM, aip8=0 disables slow-walk)
    assert(r160[AiDistOffset+0]==49&&ReadI16(r160+AipOffset+4*6)==17&&ReadI16(r160+AipOffset+7*6)==0);
    // sk_archer1 (ai=37: aidist capped at MaxLosAiDist=49, aip1=100% shoot, aip2=4 stall, aip3=100% approach, aip4=6 steps, aip5=16 target dist)
    assert(r170[AiDistOffset+0]==49&&ReadI16(r170+AipOffset+0*6)==100&&ReadI16(r170+AipOffset+1*6)==4&&ReadI16(r170+AipOffset+2*6)==100&&ReadI16(r170+AipOffset+3*6)==6&&ReadI16(r170+AipOffset+4*6)==16);
    // skmage_pois1 (ai=64: aidist capped at MaxLosAiDist=49, aip1=100% shoot, aip2=14 approach dist, aip3=0 disables slow-walk, aip6=27 fire dist, aip7=0 no circle, aip8=3 stall)
    assert(r180[AiDistOffset+0]==49&&ReadI16(r180+AipOffset+0*6)==100&&ReadI16(r180+AipOffset+1*6)==14&&ReadI16(r180+AipOffset+2*6)==0&&ReadI16(r180+AipOffset+5*6)==27&&ReadI16(r180+AipOffset+6*6)==0&&ReadI16(r180+AipOffset+7*6)==3);
    // mephisto (base aidist {0,40,46} -> {53,60,69})
    assert(r242[AiDistOffset+0]==53&&r242[AiDistOffset+1]==60&&r242[AiDistOffset+2]==69);
    // friendly and town NPC untouched
    assert(r10[AiDistOffset+0]==25&&ReadI16(r10+AipOffset+6)==12);
    assert(r11[AiDistOffset+0]==30);

    // 300% sight scaling:
    assert(ApplySightPercent(table,baseline,300)==7);
    assert(r0[AiDistOffset+0]==105);
    assert(ReadI16(r5+AipOffset+6)==106&&ReadI16(r18+AipOffset+6)==106);
    assert(r160[AiDistOffset+0]==49&&ReadI16(r160+AipOffset+4*6)==17&&ReadI16(r160+AipOffset+7*6)==0);
    assert(r170[AiDistOffset+0]==49&&ReadI16(r170+AipOffset+0*6)==100&&ReadI16(r170+AipOffset+3*6)==6);
    assert(r180[AiDistOffset+0]==49&&ReadI16(r180+AipOffset+2*6)==0&&ReadI16(r180+AipOffset+5*6)==48);
    assert(r242[AiDistOffset+0]==105&&r242[AiDistOffset+1]==120&&r242[AiDistOffset+2]==138);

    // Returning to 100% restores exact original bytes (including 0s and all aip slots!)
    assert(ApplySightPercent(table,baseline,100)==7);
    assert(table==original);

    // Corrupted probe row must fail ValidTableLayout and CaptureBaseline:
    auto corrupted=original;
    corrupted[242*RowSize+ThreatOffset]=99;
    std::vector<RowBaseline> rejected;
    assert(!ValidTableLayout(corrupted,250)&&!CaptureBaseline(corrupted,250,rejected)&&rejected.empty());

    // 2. Live adapter tests against synthetic D2RLoader context
    fakeTables.serviceSize=sizeof fakeTables;
    fakeTables.serviceVersion=D2RL::DataTableService::AbiVersion;
    fakeTables.getTable=&FakeGetTable;
    fakeThreads.serviceSize=sizeof fakeThreads;
    fakeThreads.serviceVersion=D2RL::ThreadService::AbiVersion;
    fakeThreads.runOnUiThread=&FakeRunOnUiThread;
    D2RL::PluginApi api{};
    api.apiSize=sizeof api;
    api.queryService=&FakeQueryService;
    D2RL::PluginContext ctx{};
    ctx.contextSize=sizeof ctx;
    ctx.abiVersion=D2RL_PLUGIN_ABI_VERSION;
    ctx.api=&api;

    PopulateCanonicalTable(lodTable);
    enemysight::Init(&ctx);
    enemysight::Install();
    assert(!enemysight::installed); // signature not ready yet

    signatures=true;
    enemysight::Install();
    assert(enemysight::installed);

    // At 100% default, Tick() does zero UI thread calls
    enemysight::Configure(100,true);
    enemysight::Tick();
    assert(uiQueueCalls==0&&lodTable==original);

    // Configure 200% in offline VR (clientMode == 0)
    enemysight::Configure(200,true);
    enemysight::Tick();
    assert(uiQueueCalls==1&&modeCalls>0);
    assert(lodTable[0*RowSize+AiDistOffset]==70&&lodTable[242*RowSize+AiDistOffset+1]==80);
    enemysight::Report();

    // If RotW bank finishes loading later, next Tick() scales it automatically
    PopulateCanonicalTable(rotwTable);
    enemysight::Tick();
    assert(rotwTable[0*RowSize+AiDistOffset]==70&&rotwTable[242*RowSize+AiDistOffset+2]==92);

    // Leaving offline mode (clientMode != 0) restores exact baseline across all banks
    clientMode=1;
    enemysight::Tick();
    assert(lodTable==original&&rotwTable==original);

    // Returning to offline mode re-applies 200%
    clientMode=0;
    enemysight::Tick();
    assert(lodTable[0*RowSize+AiDistOffset]==70&&rotwTable[0*RowSize+AiDistOffset]==70);

    // Desktop mode (vr == false) restores baseline
    enemysight::Configure(200,false);
    enemysight::Tick();
    assert(lodTable==original&&rotwTable==original);

    // Re-enable VR at 300%, then verify Shutdown() restores baseline immediately
    enemysight::Configure(300,true);
    enemysight::Tick();
    assert(lodTable[0*RowSize+AiDistOffset]==105);
    enemysight::Shutdown();
    assert(lodTable==original&&rotwTable==original);

    std::puts("Enemy sight distance: normalization, MonStats probes, archetype aip scaling, offline/VR guards and lossless baseline restore passed");
}
