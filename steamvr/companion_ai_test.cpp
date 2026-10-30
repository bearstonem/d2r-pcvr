#include "../vr/companion_ai.cpp"
#undef NDEBUG
#include <cassert>
#include <cmath>
#include <cstring>
#include <vector>

namespace {
bool signatures = false;
int clientMode = 0;
int modeCalls = 0;
int uiQueueCalls = 0;
int gameThreadCalls = 0;
int tableQueryCalls = 0;
int relocateCalls = 0;
uintptr_t lastRelocatedUnit = 0;
uintptr_t lastRelocatedRoom = 0;
int lastRelocatedX = 0;
int lastRelocatedY = 0;

std::vector<uint8_t> lodTable;
std::vector<uint8_t> rotwTable;

int FakeMode() {
    ++modeCalls;
    return clientMode;
}

int FakeRelocate(uintptr_t game, uintptr_t unit, uintptr_t room, int x, int y, int unk1, int unk2) {
    (void)game; (void)unk1; (void)unk2;
    ++relocateCalls;
    lastRelocatedUnit = unit;
    lastRelocatedRoom = room;
    lastRelocatedX = x;
    lastRelocatedY = y;
    return 1;
}

uintptr_t FakeRoomPoint(uintptr_t room, int x, int y) {
    (void)x; (void)y;
    return room ? room + 0x10 : 0;
}

int serverMoveCoordCalls = 0;
uintptr_t lastServerMoveGame = 0;
uintptr_t lastServerMoveUnit = 0;
int lastServerMoveX = 0;
int lastServerMoveY = 0;

int FakeServerMoveToCoord(uintptr_t game, uintptr_t unit, int x, int y) {
    ++serverMoveCoordCalls;
    lastServerMoveGame = game;
    lastServerMoveUnit = unit;
    lastServerMoveX = x;
    lastServerMoveY = y;
    return 1;
}

int origServerMoveUnitCalls = 0;
int FakeOriginalServerMoveUnit(uintptr_t game, uintptr_t unit, uintptr_t targetUnit, int moveMode, int x, int y, uint8_t pathType, uint16_t flags) {
    (void)game; (void)unit; (void)targetUnit; (void)moveMode; (void)x; (void)y; (void)pathType; (void)flags;
    ++origServerMoveUnitCalls;
    return 1;
}

int origServerStartWalkCalls = 0;
int FakeOriginalServerStartWalk(uintptr_t game, uintptr_t moveStruct) {
    (void)game; (void)moveStruct;
    ++origServerStartWalkCalls;
    return 1;
}

int origServerStartRunCalls = 0;
int FakeOriginalServerStartRun(uintptr_t game, uintptr_t moveStruct) {
    (void)game; (void)moveStruct;
    ++origServerStartRunCalls;
    return 1;
}

int testAttackCalls = 0;
int FakeAttack(uintptr_t game, uintptr_t unit, int skillId, int arg4) {
    (void)game; (void)unit; (void)skillId; (void)arg4;
    ++testAttackCalls;
    return 1;
}

int fakeOriginalSkillLevelCalls = 0;
int fakeBaseSkillLevel = 10;
int FakeOriginalSkillLevel(uintptr_t unit, uintptr_t skillNode, int bIncludeBonuses, int unk) {
    (void)unit; (void)skillNode; (void)bIncludeBonuses; (void)unk;
    ++fakeOriginalSkillLevelCalls;
    return fakeBaseSkillLevel;
}

struct TestMockItem {
    uint32_t header[3] = { 4, 0, 100 }; // dwType = 4 (UNIT_ITEM)
    std::vector<std::tuple<int, int, int>> stats; // stat, param, value
    int GetStat(int stat, int param) const {
        for (const auto& [s, p, v] : stats) {
            if (s == stat && p == param) return v;
        }
        return 0;
    }
};

std::unordered_map<int, TestMockItem> g_testEquippedSlots;

uintptr_t FakeInventory(uintptr_t unit, const char*, int) {
    return unit ? 0x90000 : 0;
}

uintptr_t FakeEquipped(uintptr_t inv, int bodyLoc) {
    if (!inv) return 0;
    auto it = g_testEquippedSlots.find(bodyLoc);
    if (it != g_testEquippedSlots.end()) {
        return reinterpret_cast<uintptr_t>(&it->second);
    }
    return 0;
}

} // anonymous namespace

namespace nativeui {
std::string SkillName(int id) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "Skill %d", id);
    return buf;
}
const void* GetSkillRow(int id) {
    static uint8_t mockRow[748]{};
    return id > 0 ? mockRow : nullptr;
}
}

namespace {

int FakeGetStat(uintptr_t item, int stat, int param) {
    if (!item) return 0;
    const auto* itm = reinterpret_cast<const TestMockItem*>(item);
    return itm->GetStat(stat, param);
}

void PopulateCanonicalMonStats(std::vector<uint8_t>& buf, size_t rows = 600) {
    using namespace framevr::companionai;
    buf.assign(rows * RowSize, 0);

    for (uint16_t r : HirelingRows) {
        uint8_t* rowPtr = buf.data() + size_t(r) * RowSize;
        std::memcpy(rowPtr + 0x00, &r, sizeof(r));
        uint16_t ai = 61;
        std::memcpy(rowPtr + AiOffset, &ai, sizeof(ai));
        rowPtr[AlignOffset] = 1; // Friendly ally
        rowPtr[AiDistOffset + 0] = 0;
        rowPtr[AiDistOffset + 1] = 0;
        rowPtr[AiDistOffset + 2] = 0;
    }
}

D2RL::DataTables::Result __cdecl FakeGetTable(const D2RL::PluginContext*, D2RL::DataTables::Bank bank,
                                              D2RL::DataTables::TableId tableId, D2RL::DataTables::TableView* out) noexcept {
    ++tableQueryCalls;
    if (!out) return D2RL::DataTables::Result::Unavailable;
    if (tableId == D2RL::DataTables::TableId::Skills) {
        static std::vector<uint8_t> mockSkillsTable(1000 * 748, 0);
        out->rows = mockSkillsTable.data();
        out->rowCount = 1000;
        out->rowSize = 748;
        return D2RL::DataTables::Result::Success;
    }
    if (tableId != D2RL::DataTables::TableId::MonStats) return D2RL::DataTables::Result::Unavailable;
    if (bank == D2RL::DataTables::Bank::Lod && !lodTable.empty()) {
        out->rows = lodTable.data();
        out->rowCount = uint32_t(lodTable.size() / framevr::companionai::RowSize);
        out->rowSize = uint32_t(framevr::companionai::RowSize);
        return D2RL::DataTables::Result::Success;
    }
    if (bank == D2RL::DataTables::Bank::Rotw && !rotwTable.empty()) {
        out->rows = rotwTable.data();
        out->rowCount = uint32_t(rotwTable.size() / framevr::companionai::RowSize);
        out->rowSize = uint32_t(framevr::companionai::RowSize);
        return D2RL::DataTables::Result::Success;
    }
    return D2RL::DataTables::Result::Unavailable;
}

D2RL::Threads::Result __cdecl FakeRunOnUiThread(const D2RL::PluginContext* ctx, D2RL::Threads::Callback cb, void* user) noexcept {
    ++uiQueueCalls;
    cb(ctx, user);
    return D2RL::Threads::Result::Success;
}

D2RL::Threads::Result __cdecl FakeRunOnGameThread(const D2RL::PluginContext* ctx, D2RL::Threads::Callback cb, void* user) noexcept {
    ++gameThreadCalls;
    cb(ctx, user);
    return D2RL::Threads::Result::Success;
}

D2RL::DataTableService fakeTables{};
D2RL::ThreadService fakeThreads{};

D2RL::ServiceQueryResult __cdecl FakeQueryService(const D2RL::PluginContext*, D2RL::ServiceId serviceId, uint32_t, const void** outService) noexcept {
    if (serviceId == D2RL::ServiceId::DataTable) {
        *outService = &fakeTables;
        return D2RL::ServiceQueryResult::Success;
    }
    if (serviceId == D2RL::ServiceId::Thread) {
        *outService = &fakeThreads;
        return D2RL::ServiceQueryResult::Success;
    }
    *outService = nullptr;
    return D2RL::ServiceQueryResult::Unavailable;
}
} // namespace


static int g_lastStartedSkill = 0;
static int g_lastStartedLevel = 0;
static int g_lastDispatchedSkill = 0;
static int g_lastDispatchedLevel = 0;
static int g_lastDispatchedDestX = 0;
static int g_lastDispatchedDestY = 0;
static int g_dispatchCount = 0;

static int __fastcall FakeStartSkill(uintptr_t, uintptr_t, int skillId, int level, int) {
    g_lastStartedSkill = skillId;
    g_lastStartedLevel = level;
    return 1;
}

static int __fastcall FakeDispatchSkill(uintptr_t, uintptr_t, int skillId, int level, int, int targetX, int targetY) {
    g_lastDispatchedSkill = skillId;
    g_lastDispatchedLevel = level;
    g_lastDispatchedDestX = targetX;
    g_lastDispatchedDestY = targetY;
    g_dispatchCount++;
    return 1;
}

static int g_lastSetStatId = 0;
static int g_lastSetStatVal = 0;
static int g_lastSetStatParam = 0;
static int g_setStatCount = 0;

static void __fastcall FakeSetStat(uintptr_t, int stat, int val, int param) {
    g_lastSetStatId = stat;
    g_lastSetStatVal = val;
    g_lastSetStatParam = param;
    g_setStatCount++;
}

static uintptr_t __fastcall FakeOriginalSkillLookup(uintptr_t, int, int) {
    return 0;
}

namespace d2rsig {
bool Check(uint64_t rva, const void*, uint32_t) {
    if (rva == companionai::abi::ModeRva ||
        rva == companionai::abi::RelocateRva ||
        rva == companionai::abi::RoomPointRva ||
        rva == companionai::abi::PathMoveRva ||
        rva == companionai::abi::ServerMoveToCoordRva ||
        rva == companionai::abi::ServerMoveUnitRva ||
        rva == companionai::abi::ServerStartWalkRva ||
        rva == companionai::abi::ServerStartRunRva ||
        rva == companionai::abi::AttackRva ||
        rva == companionai::abi::SkillLevelRva ||
        rva == companionai::abi::InventoryRva ||
        rva == companionai::abi::EquippedRva ||
        rva == companionai::abi::GetStatRva ||
        rva == companionai::abi::StartRva ||
        rva == companionai::abi::DispatchRva ||
        rva == companionai::abi::SetStatRva ||
        rva == companionai::abi::SkillRva) {
        return signatures;
    }
    return false;
}
uintptr_t Addr(uint64_t rva) {
    if (rva == companionai::abi::ModeRva) return reinterpret_cast<uintptr_t>(&FakeMode);
    if (rva == companionai::abi::RelocateRva) return reinterpret_cast<uintptr_t>(&FakeRelocate);
    if (rva == companionai::abi::RoomPointRva) return reinterpret_cast<uintptr_t>(&FakeRoomPoint);
    if (rva == companionai::abi::ServerMoveToCoordRva) return reinterpret_cast<uintptr_t>(&FakeServerMoveToCoord);
    if (rva == companionai::abi::ServerMoveUnitRva) return reinterpret_cast<uintptr_t>(&FakeOriginalServerMoveUnit);
    if (rva == companionai::abi::ServerStartWalkRva) return reinterpret_cast<uintptr_t>(&FakeOriginalServerStartWalk);
    if (rva == companionai::abi::ServerStartRunRva) return reinterpret_cast<uintptr_t>(&FakeOriginalServerStartRun);
    if (rva == companionai::abi::AttackRva) return reinterpret_cast<uintptr_t>(&FakeAttack);
    if (rva == companionai::abi::SkillLevelRva) return reinterpret_cast<uintptr_t>(&FakeOriginalSkillLevel);
    if (rva == companionai::abi::InventoryRva) return reinterpret_cast<uintptr_t>(&FakeInventory);
    if (rva == companionai::abi::EquippedRva) return reinterpret_cast<uintptr_t>(&FakeEquipped);
    if (rva == companionai::abi::GetStatRva) return reinterpret_cast<uintptr_t>(&FakeGetStat);
    if (rva == companionai::abi::StartRva) return reinterpret_cast<uintptr_t>(&FakeStartSkill);
    if (rva == companionai::abi::DispatchRva) return reinterpret_cast<uintptr_t>(&FakeDispatchSkill);
    if (rva == companionai::abi::SetStatRva) return reinterpret_cast<uintptr_t>(&FakeSetStat);
    if (rva == companionai::abi::SkillRva) return reinterpret_cast<uintptr_t>(&FakeOriginalSkillLookup);
    return 0;
}
bool Hook(uint64_t rva, const void*, uint32_t, void*, void** original) {
    if (!signatures) return false;
    if (rva == companionai::abi::ServerMoveUnitRva) {
        if (original) *original = reinterpret_cast<void*>(&FakeOriginalServerMoveUnit);
        return true;
    }
    if (rva == companionai::abi::ServerStartWalkRva) {
        if (original) *original = reinterpret_cast<void*>(&FakeOriginalServerStartWalk);
        return true;
    }
    if (rva == companionai::abi::ServerStartRunRva) {
        if (original) *original = reinterpret_cast<void*>(&FakeOriginalServerStartRun);
        return true;
    }
    if (rva == companionai::abi::SkillLevelRva) {
        if (original) *original = reinterpret_cast<void*>(&FakeOriginalSkillLevel);
        return true;
    }

    return false;
}
uint64_t Rva(uint64_t rva) { return rva; }
} // namespace d2rsig

namespace framevr::logging {
void Write(const char*){}
} // namespace framevr::logging

namespace enemysight {
int mockApplied = 100;
int AppliedPercent() {
    return mockApplied;
}
} // namespace enemysight

static int g_mockCorpseCount = 0;
static melee::Corpse g_mockCorpses[16]{};

namespace melee {
uintptr_t g_testServerUnit = 0;
bool LocalServer(uintptr_t& game, uintptr_t& player) {
    game = 0x20000;
    player = 0x30000;
    return true;
}
uintptr_t ServerUnit(uintptr_t game, int type, uint32_t id) {
    (void)game; (void)type; (void)id;
    return g_testServerUnit ? g_testServerUnit : 0x40000;
}
bool HirelingVitals(uint32_t hint, float& health, float& maxHealth, uint32_t& classId, uint32_t& unitId) {
    (void)hint;
    health = 100.0f;
    maxHealth = 100.0f;
    classId = 271;
    unitId = 12345;
    return true;
}
int CorpsesAround(Corpse* out, int max, float range, float centerX, float centerY, uintptr_t viewerUnit) {
    (void)range; (void)centerX; (void)centerY; (void)viewerUnit;
    const int count = std::min(max, g_mockCorpseCount);
    for (int i = 0; i < count; ++i) {
        if (out) out[i] = g_mockCorpses[i];
    }
    return count;
}

int HostilesAround(Hostile* out, int max, float range, float centerX, float centerY, uintptr_t viewerUnit) {
    (void)out; (void)max; (void)range; (void)centerX; (void)centerY; (void)viewerUnit;
    return 0;
}
int Hostiles(Hostile* out, int max, float range, float& heroX, float& heroY) {
    (void)out; (void)max; (void)range;
    heroX = 50.0f;
    heroY = 50.0f;
    return 0;
}
} // namespace melee

namespace bowvr {
int lastHeroSound = 0;
bool PlayHeroSound(int sound) {
    lastHeroSound = sound;
    return true;
}
} // namespace bowvr

int main() {
    using namespace framevr::companionai;

    // =========================================================================
    // 1. Detection Range Scaling Tests
    // =========================================================================
    assert(ComputeCompanionDetectRange(100) == 25);
    assert(ComputeCompanionDetectRange(120) == 30);
    assert(ComputeCompanionDetectRange(150) == 38);
    assert(ComputeCompanionDetectRange(190) == 48);
    assert(ComputeCompanionDetectRange(200) == 48); // Capped to MaxDetectRange (48)
    assert(ComputeCompanionDetectRange(300) == 48); // Capped
    assert(ComputeCompanionDetectRange(50) == 25);  // Clamped to MinDetectRange (25)

    // =========================================================================
    // 2. Elastic Combat Leash Tests
    // =========================================================================
    assert(ComputeCombatLeash(false) == DefaultLeashDistance);       // 24 sub-tiles out of combat
    assert(ComputeCombatLeash(true) == ElasticCombatLeashDistance);   // 40 sub-tiles in combat
    assert(!IsLeashExceeded(20.0f, false));
    assert(IsLeashExceeded(25.0f, false));
    assert(!IsLeashExceeded(25.0f, true));
    assert(!IsLeashExceeded(39.0f, true));
    assert(IsLeashExceeded(41.0f, true));

    // =========================================================================
    // 3. Unstuck Position Calculation Tests
    // =========================================================================
    {
        // Player at (100, 100), facing north along -Y (forwardX=0, forwardY=-1)
        const Vec2 playerPos{ 100.0f, 100.0f };
        const Vec2 unstuck = CalculateUnstuckPosition(playerPos, 0.0f, -1.0f);
        // Front flank vector: forward = (0, -1) * 2.5 = (0, -2.5)
        // Right normal: (-ny, nx) = (1, 0) * 2.2 = (2.2, 0)
        assert(std::fabs(unstuck.x - 102.2f) < 1e-4f);
        assert(std::fabs(unstuck.y - 97.5f) < 1e-4f);
    }
    {
        // Player at (50, 50), facing east along +X (forwardX=1, forwardY=0)
        const Vec2 playerPos{ 50.0f, 50.0f };
        const Vec2 unstuck = CalculateUnstuckPosition(playerPos, 1.0f, 0.0f);
        // Front flank vector: forward = (1, 0) * 2.5 = (2.5, 0)
        // Right normal: (-ny, nx) = (0, 1) * 2.2 = (0, 2.2)
        assert(std::fabs(unstuck.x - 52.5f) < 1e-4f);
        assert(std::fabs(unstuck.y - 52.2f) < 1e-4f);
    }
    {
        // Degenerate zero forward vector fallback: placed slightly ahead
        const Vec2 playerPos{ 20.0f, 30.0f };
        const Vec2 unstuck = CalculateUnstuckPosition(playerPos, 0.0f, 0.0f);
        assert(std::fabs(unstuck.x - 22.2f) < 1e-4f);
        assert(std::fabs(unstuck.y - 27.5f) < 1e-4f);
    }

    // =========================================================================
    // 4. Stuck Watchdog State Machine Tests
    // =========================================================================
    {
        StuckWatchdog dog;
        const Vec2 playerPos{ 100.0f, 100.0f };
        const Vec2 compPos{ 70.0f, 70.0f }; // 42.4 sub-tiles away (> 25.0)

        // Non-walk modes must never trigger stuck
        assert(!dog.Update(1, compPos, playerPos, 1000)); // MONMODE_NEUTRAL
        assert(!dog.Update(3, compPos, playerPos, 2000)); // MONMODE_GETHIT
        assert(!dog.Update(4, compPos, playerPos, 3000)); // MONMODE_ATTACK1
        assert(!dog.Update(12, compPos, playerPos, 5000)); // MONMODE_DEAD

        // If distance to player < StuckMinDistanceThreshold (25.0), never stuck!
        const Vec2 closePos{ 90.0f, 90.0f }; // ~14.1 sub-tiles away
        assert(!dog.Update(2, closePos, playerPos, 6000));
        assert(!dog.Update(2, closePos, playerPos, 20000));

        // Walking normally: displacement >= StuckMaxDisplacement (0.5) every 1000ms
        dog.Reset();
        Vec2 walkPos{ 70.0f, 70.0f };
        assert(!dog.Update(2, walkPos, playerPos, 0));
        walkPos = { 71.0f, 70.0f }; // disp = 1.0
        assert(!dog.Update(2, walkPos, playerPos, 1000));
        walkPos = { 72.0f, 70.0f }; // disp = 1.0
        assert(!dog.Update(2, walkPos, playerPos, 2000));
        walkPos = { 73.0f, 70.0f }; // disp = 1.0
        assert(!dog.Update(2, walkPos, playerPos, 4000));
        walkPos = { 74.0f, 70.0f }; // disp = 1.0
        assert(!dog.Update(2, walkPos, playerPos, 9000)); // Still moving, never triggers!

        // Stuck against collision: in WALK mode (2) and far away, disp < 0.5 for >= 8000ms
        dog.Reset();
        const Vec2 stuckPos{ 70.0f, 70.0f };
        assert(!dog.Update(2, stuckPos, playerPos, 1000)); // t=1000ms (first sample)
        assert(!dog.Update(2, { 70.05f, 70.05f }, playerPos, 2000)); // t=2000ms (disp ~0.07)
        assert(!dog.Update(2, { 70.08f, 70.04f }, playerPos, 3000)); // t=3000ms
        assert(!dog.Update(2, { 70.05f, 70.06f }, playerPos, 4000)); // t=4000ms
        assert(!dog.Update(2, { 70.06f, 70.05f }, playerPos, 5000)); // t=5000ms
        assert(!dog.Update(2, { 70.07f, 70.06f }, playerPos, 6000)); // t=6000ms
        assert(!dog.Update(2, { 70.05f, 70.05f }, playerPos, 7000)); // t=7000ms
        assert(!dog.Update(2, { 70.06f, 70.06f }, playerPos, 8000)); // t=8000ms
        // At t=9000ms: continuous stuck duration is 9000 - 1000 = 8000ms >= StuckTimeoutMs (8000)
        assert(dog.Update(2, { 70.06f, 70.05f }, playerPos, 9000)); // TRIGGERED!
        // Immediately after trigger, timer is reset:
        assert(!dog.Update(2, { 70.06f, 70.05f }, playerPos, 9500));
    }

    // =========================================================================
    // 5. MonStats Baseline Capture & Restoration Tests
    // =========================================================================
    {
        std::vector<uint8_t> table;
        PopulateCanonicalMonStats(table, 600);
        const auto original = table;

        std::vector<HirelingBaseline> baselines;
        assert(CaptureHirelingBaselines(table, 600, baselines));
        assert(baselines.size() == HirelingRowCount);

        // Write scaled detection range (e.g. 38 sub-tiles for 150%)
        assert(WriteHirelingAiDist(table, 600, 38));
        for (uint16_t r : HirelingRows) {
            const uint8_t* rowPtr = table.data() + size_t(r) * RowSize;
            assert(rowPtr[AiDistOffset + 0] == 38);
            assert(rowPtr[AiDistOffset + 1] == 38);
            assert(rowPtr[AiDistOffset + 2] == 38);
        }

        // Restore baselines
        assert(RestoreHirelingBaselines(table, 600, baselines));
        assert(table == original);
    }

    // =========================================================================
    // 6. Full Adapter Tests (companionai::Init, Configure, Tick, Shutdown)
    // =========================================================================
    {
        fakeThreads.runOnUiThread = &FakeRunOnUiThread;
        fakeThreads.runOnGameThread = &FakeRunOnGameThread;
        fakeTables.getTable = &FakeGetTable;

        D2RL::PluginApi api{};
        api.apiSize = sizeof(api);
        api.queryService = &FakeQueryService;
        D2RL::PluginContext ctx{};
        ctx.contextSize = sizeof(ctx);
        ctx.abiVersion = D2RL_PLUGIN_ABI_VERSION;
        ctx.api = &api;

        PopulateCanonicalMonStats(lodTable, 600);
        PopulateCanonicalMonStats(rotwTable, 600);
        const auto lodOriginal = lodTable;
        const auto rotwOriginal = rotwTable;

        companionai::Init(&ctx);
        companionai::Configure(true);
        assert(companionai::IsEnabled());

        signatures = true;
        companionai::Install();
        assert(companionai::g_installed.load());

        // 1. At 100% default, table stays at original baseline bytes
        enemysight::mockApplied = 100;
        clientMode = 0;
        companionai::Tick(0.0f, -1.0f);
        assert(lodTable == lodOriginal && rotwTable == rotwOriginal);

        // 2. When sight scales to 150%, companion detection range scales to 38
        enemysight::mockApplied = 150;
        companionai::Tick(0.0f, -1.0f);
        assert(lodTable[271 * RowSize + AiDistOffset] == 38);
        assert(rotwTable[560 * RowSize + AiDistOffset] == 38);

        // 3. Switch to desktop mode: verify restoration to baseline
        clientMode = 1;
        companionai::Tick(0.0f, -1.0f);
        assert(lodTable == lodOriginal && rotwTable == rotwOriginal);

        // 4. Back to offline VR: verify scaled to 38
        clientMode = 0;
        companionai::Tick(0.0f, -1.0f);
        assert(lodTable[271 * RowSize + AiDistOffset] == 38);

        // 5. Sight scaled to 200%: companion detection range scales to 48 (capped)
        enemysight::mockApplied = 200;
        companionai::Tick(0.0f, -1.0f);
        assert(lodTable[271 * RowSize + AiDistOffset] == 48);
        assert(rotwTable[560 * RowSize + AiDistOffset] == 48);

        // 6. Sight returns to 100%: restores baseline
        enemysight::mockApplied = 100;
        companionai::Tick(0.0f, -1.0f);
        assert(lodTable == lodOriginal && rotwTable == rotwOriginal);

        // 7. Re-scale to 200% and test Shutdown() restores baseline
        enemysight::mockApplied = 200;
        companionai::Tick(0.0f, -1.0f);
        assert(lodTable[271 * RowSize + AiDistOffset] == 48);
        companionai::Shutdown();
        assert(lodTable == lodOriginal && rotwTable == rotwOriginal);
        assert(!companionai::IsEnabled());
    }

    // =========================================================================
    // 7. Phase 2: Archetype Classification & Formation Anchors
    // =========================================================================
    {
        using namespace framevr::companionai;

        // Archetypes
        assert(GetHirelingArchetype(MonStatsIndex::Rogue) == Archetype::RangedOverwatch);
        assert(GetHirelingArchetype(MonStatsIndex::IronWolf) == Archetype::RangedOverwatch);
        assert(GetHirelingArchetype(MonStatsIndex::Desert) == Archetype::MeleeVanguard);
        assert(GetHirelingArchetype(MonStatsIndex::Barbarian1) == Archetype::MeleeVanguard);
        assert(GetHirelingArchetype(MonStatsIndex::Barbarian2) == Archetype::MeleeVanguard);

        // Vanguard Anchor (+30 deg, 5.5 sub-tiles ahead/flank)
        const Vec2 playerPos{ 100.0f, 100.0f };
        const Vec2 vanguardR = CalculateFormationAnchor(playerPos, 0.0f, -1.0f, Archetype::MeleeVanguard, true);
        const float distVR = std::hypot(vanguardR.x - playerPos.x, vanguardR.y - playerPos.y);
        assert(std::fabs(distVR - VanguardDistance) < 1e-3f);
        assert(vanguardR.x > playerPos.x); // Right flank
        assert(vanguardR.y < playerPos.y); // Ahead (facing -Y)

        const Vec2 vanguardL = CalculateFormationAnchor(playerPos, 0.0f, -1.0f, Archetype::MeleeVanguard, false);
        const float distVL = std::hypot(vanguardL.x - playerPos.x, vanguardL.y - playerPos.y);
        assert(std::fabs(distVL - VanguardDistance) < 1e-3f);
        assert(vanguardL.x < playerPos.x); // Left flank
        assert(vanguardL.y < playerPos.y); // Ahead

        // Overwatch Anchor (+45 deg, 5.5 sub-tiles ahead/flank in peripheral view)
        const Vec2 overwatchR = CalculateFormationAnchor(playerPos, 0.0f, -1.0f, Archetype::RangedOverwatch, true);
        const float distOR = std::hypot(overwatchR.x - playerPos.x, overwatchR.y - playerPos.y);
        assert(std::fabs(distOR - OverwatchDistance) < 1e-3f);
        assert(overwatchR.x > playerPos.x); // Right flank
        assert(overwatchR.y < playerPos.y); // Ahead in peripheral view (facing -Y)

        const Vec2 overwatchL = CalculateFormationAnchor(playerPos, 0.0f, -1.0f, Archetype::RangedOverwatch, false);
        const float distOL = std::hypot(overwatchL.x - playerPos.x, overwatchL.y - playerPos.y);
        assert(std::fabs(distOL - OverwatchDistance) < 1e-3f);
        assert(overwatchL.x < playerPos.x); // Left flank
        assert(overwatchL.y < playerPos.y); // Ahead in peripheral view
    }

    // =========================================================================
    // 8. Phase 2: Personal Space Bubble & Evasion Tests
    // =========================================================================
    {
        using namespace framevr::companionai;
        const Vec2 playerPos{ 100.0f, 100.0f };

        // No violation if companion >= PersonalSpaceRadius (3.75)
        assert(!CheckPersonalSpaceViolation(playerPos, { 100.0f, 96.0f })); // 4.0 sub-tiles away
        assert(!CheckPersonalSpaceViolation(playerPos, { 104.0f, 100.0f })); // 4.0 sub-tiles away

        // Violation if companion < PersonalSpaceRadius
        assert(CheckPersonalSpaceViolation(playerPos, { 100.0f, 98.0f })); // 2.0 sub-tiles
        assert(CheckPersonalSpaceViolation(playerPos, { 101.0f, 101.0f })); // ~1.4 sub-tiles

        // Evasion when companion directly in front: pushes sideways to clear sightline
        const Vec2 inFront{ 100.0f, 98.5f }; // 1.5 sub-tiles ahead
        const Vec2 evasion = CalculatePersonalSpaceEvasion(playerPos, inFront, 0.0f, -1.0f);
        const float evasionDist = std::hypot(evasion.x - playerPos.x, evasion.y - playerPos.y);
        assert(evasionDist >= PersonalSpaceRadius);
        assert(std::fabs(evasion.x - playerPos.x) >= PersonalSpaceRadius); // Sideways clearance!

        // Evasion when directly on top of player: pushes along right normal
        const Vec2 onTop{ 100.0f, 100.0f };
        const Vec2 evasionOnTop = CalculatePersonalSpaceEvasion(playerPos, onTop, 0.0f, -1.0f);
        assert(std::hypot(evasionOnTop.x - playerPos.x, evasionOnTop.y - playerPos.y) >= PersonalSpaceRadius);
    }

    // =========================================================================
    // 9. Phase 2: Doorway & Chokepoint Clearance Tests
    // =========================================================================
    {
        using namespace framevr::companionai;
        const Vec2 playerPos{ 100.0f, 100.0f };

        // Companion in doorway 2.5 sub-tiles directly ahead (within ChokepointDistance and DoorwayWidthThreshold)
        const Vec2 doorBlocker{ 100.2f, 97.5f };
        assert(IsDoorwayObstructed(playerPos, 0.0f, -1.0f, doorBlocker));

        // Companion far ahead or to the side: NOT obstructing doorway
        assert(!IsDoorwayObstructed(playerPos, 0.0f, -1.0f, { 100.0f, 92.0f })); // 8 sub-tiles away
        assert(!IsDoorwayObstructed(playerPos, 0.0f, -1.0f, { 103.5f, 97.5f })); // Far to the side (lat=3.5 > 1.8)

        // Clearance calculation: tucks laterally against the wall
        const Vec2 cleared = CalculateDoorwayClearance(playerPos, 0.0f, -1.0f, doorBlocker);
        assert(std::fabs(cleared.x - doorBlocker.x) >= 2.0f); // Lateral step executed!
    }

    // =========================================================================
    // 10. Phase 2: Tactical Formation Controller & Combat Freedom Tests
    // =========================================================================
    {
        using namespace framevr::companionai;
        TacticalFormationController controller;
        const Vec2 playerPos{ 100.0f, 100.0f };
        Vec2 target{};

        // 1. Personal space violation triggers immediately (Priority 1)
        const Vec2 tooClose{ 100.0f, 98.0f };
        assert(controller.Evaluate(1, tooClose, playerPos, 0.0f, -1.0f, MonStatsIndex::Desert, 1000, target));
        assert(std::hypot(target.x - playerPos.x, target.y - playerPos.y) >= PersonalSpaceRadius);

        // 2. Doorway obstruction triggers immediately (Priority 2)
        controller.Reset();
        const Vec2 inDoorway{ 100.0f, 97.0f };
        assert(controller.Evaluate(1, inDoorway, playerPos, 0.0f, -1.0f, MonStatsIndex::Desert, 2000, target));
        assert(std::fabs(target.x - inDoorway.x) >= 2.0f);

        // 3. Combat modes (4=Attack, 7=Cast) allow free combat (Priority 3)
        controller.Reset();
        const Vec2 combatPos{ 105.0f, 95.0f }; // Safe distance, away from anchor
        assert(!controller.Evaluate(4, combatPos, playerPos, 0.0f, -1.0f, MonStatsIndex::Desert, 3000, target)); // Attack mode
        assert(!controller.Evaluate(7, combatPos, playerPos, 0.0f, -1.0f, MonStatsIndex::IronWolf, 3000, target)); // Cast mode

        // 4. Following/idle: anchor repositioning when drift > FormationDeadband (2.5)
        controller.Reset();
        const Vec2 driftedPos{ 90.0f, 90.0f }; // 14.1 sub-tiles away
        assert(controller.Evaluate(1, driftedPos, playerPos, 0.0f, -1.0f, MonStatsIndex::Desert, 4000, target));
        assert(std::fabs(std::hypot(target.x - playerPos.x, target.y - playerPos.y) - VanguardDistance) < 1e-3f);

        // 5. Throttling: subsequent non-emergency check before 500ms must not re-dispatch
        Vec2 throttledTarget{};
        assert(!controller.Evaluate(1, driftedPos, playerPos, 0.0f, -1.0f, MonStatsIndex::Desert, 4200, throttledTarget));
    }

    // =========================================================================
    // 11. Phase 3: Critical Health & Recovery Evaluation Tests
    // =========================================================================
    {
        using namespace framevr::companionai;

        // Critical health triggers below 50% life (synchronized with MercLowHealth .5f)
        assert(IsCriticalHealth(49.0f, 100.0f));
        assert(IsCriticalHealth(1.0f, 100.0f));
        assert(!IsCriticalHealth(50.0f, 100.0f));
        assert(!IsCriticalHealth(70.0f, 100.0f));
        assert(!IsCriticalHealth(0.0f, 100.0f));  // Dead companion is not in tactical retreat
        assert(!IsCriticalHealth(-5.0f, 100.0f));
        assert(!IsCriticalHealth(50.0f, 0.0f));   // Invalid max life

        // Recovery triggers at or above 50% life (synchronized with MercLowHealth / red tint off)
        assert(IsHealthRecovered(50.0f, 100.0f));
        assert(IsHealthRecovered(75.0f, 100.0f));
        assert(IsHealthRecovered(100.0f, 100.0f));
        assert(!IsHealthRecovered(49.9f, 100.0f));
        assert(!IsHealthRecovered(25.0f, 100.0f));
    }

    // =========================================================================
    // 12. Phase 3: Receiving Stance Math Tests
    // =========================================================================
    {
        using namespace framevr::companionai;
        const Vec2 playerPos{ 100.0f, 100.0f };

        // Player facing -Y (0, -1)
        const Vec2 receivingR = CalculateReceivingStancePosition(playerPos, 0.0f, -1.0f, ReceivingStanceDistance, ReceivingStanceAngleDeg, true);
        const float distR = std::hypot(receivingR.x - playerPos.x, receivingR.y - playerPos.y);
        assert(std::fabs(distR - ReceivingStanceDistance) < 1e-3f);
        assert(receivingR.y < playerPos.y); // Ahead in front of player
        assert(receivingR.x > playerPos.x); // Right flank (+20 deg)

        const Vec2 receivingL = CalculateReceivingStancePosition(playerPos, 0.0f, -1.0f, ReceivingStanceDistance, ReceivingStanceAngleDeg, false);
        const float distL = std::hypot(receivingL.x - playerPos.x, receivingL.y - playerPos.y);
        assert(std::fabs(distL - ReceivingStanceDistance) < 1e-3f);
        assert(receivingL.y < playerPos.y); // Ahead in front of player
        assert(receivingL.x < playerPos.x); // Left flank (-20 deg)
    }

    // =========================================================================
    // 13. Phase 3: Tactical Retreat Controller & Distress Cue State Machine Tests
    // =========================================================================
    {
        using namespace framevr::companionai;
        TacticalRetreatController controller;

        // Healthy companion: no retreat, no distress cue
        assert(!controller.Update(100.0f, 100.0f, 1000));
        assert(!controller.IsInRetreat());

        // Companion takes heavy damage (45% HP < 50%): enters retreat, fires distress cue
        assert(controller.Update(45.0f, 100.0f, 2000));
        assert(controller.IsInRetreat());

        // Subsequent update while still critical (2 seconds later): no repeat cue (within 8s cooldown)
        assert(!controller.Update(45.0f, 100.0f, 4000));
        assert(controller.IsInRetreat());

        // 8.5 seconds after initial cue, still critical: fires repeat distress reminder
        assert(controller.Update(35.0f, 100.0f, 10500));
        assert(controller.IsInRetreat());

        // Health recovers to 51% (>= 50% recovery threshold synchronized with MercLowHealth): exits retreat!
        assert(!controller.Update(51.0f, 100.0f, 12000));
        assert(!controller.IsInRetreat());
        // While health is >= 50% (red tint off), distress cue never plays even after cooldown
        assert(!controller.Update(60.0f, 100.0f, 25000));
        assert(!controller.IsInRetreat());

        // Drop to 40% life: enters retreat again
        assert(controller.Update(40.0f, 100.0f, 15000));
        assert(controller.IsInRetreat());

        // Player throws potion: OnPotionConsumed immediately cancels retreat
        controller.OnPotionConsumed();
        assert(!controller.IsInRetreat());
    }

    // =========================================================================
    // 14. Phase 3: Tactical Formation Controller Integration with Retreat
    // =========================================================================
    {
        using namespace framevr::companionai;
        TacticalFormationController controller;
        const Vec2 playerPos{ 100.0f, 100.0f };
        Vec2 target{};

        // Companion in combat mode (4=Attack), but inRetreat=true:
        // Must OVERRIDE combat freedom and command movement toward receiving stance!
        const Vec2 combatPos{ 112.0f, 95.0f };
        assert(controller.Evaluate(4, combatPos, playerPos, 0.0f, -1.0f, MonStatsIndex::Desert, 1000, target, true));
        const float distToTarget = std::hypot(target.x - playerPos.x, target.y - playerPos.y);
        assert(std::fabs(distToTarget - ReceivingStanceDistance) < 1e-3f);
        assert(target.y < playerPos.y); // Ahead of player

        // When companion reaches receiving stance (within ReceivingStanceDeadband):
        // Controller holds stance steadily (no jitter) so player can throw potion easily
        controller.Reset();
        const Vec2 stancePos = CalculateReceivingStancePosition(playerPos, 0.0f, -1.0f);
        const Vec2 closeToStance{ stancePos.x + 0.5f, stancePos.y + 0.5f };
        assert(!controller.Evaluate(1, closeToStance, playerPos, 0.0f, -1.0f, MonStatsIndex::Desert, 2000, target, true));
    }

    // =========================================================================
    // 15. Phase 3: Stuck Watchdog in Retreat
    // =========================================================================
    {
        using namespace framevr::companionai;
        StuckWatchdog watchdog;
        const Vec2 playerPos{ 100.0f, 100.0f };
        const Vec2 stuckPos{ 118.0f, 100.0f }; // 18 sub-tiles away (> 15 retreat threshold)

        // Initial sample
        assert(!watchdog.Update(2, stuckPos, playerPos, 1000, true));
        // Samples every 1s with no displacement
        assert(!watchdog.Update(2, stuckPos, playerPos, 2000, true));
        assert(!watchdog.Update(2, stuckPos, playerPos, 3000, true));
        assert(!watchdog.Update(2, stuckPos, playerPos, 4000, true));
        // After 4.0s (RetreatStuckTimeoutMs), triggers urgent rescue!
        assert(watchdog.Update(2, stuckPos, playerPos, 5100, true));
        assert(watchdog.isStuck);
    }

    // =========================================================================
    // 16. Phase 3: Archetype Distress Sound Mapping Tests
    // =========================================================================
    {
        using namespace framevr::companionai;
        // Rogue Scout (271) -> Amazon "Help!" (2937)
        assert(GetArchetypeDistressSound(MonStatsIndex::Rogue) == 2937);
        // Desert Spearman (338) -> Paladin "Help!" (3171)
        assert(GetArchetypeDistressSound(MonStatsIndex::Desert) == 3171);
        // Iron Wolf (359) -> Necromancer "Help!" (3093)
        assert(GetArchetypeDistressSound(MonStatsIndex::IronWolf) == 3093);
        // Barbarian (560, 561) -> Barbarian "Help!" (3015)
        assert(GetArchetypeDistressSound(MonStatsIndex::Barbarian1) == 3015);
        assert(GetArchetypeDistressSound(MonStatsIndex::Barbarian2) == 3015);
        // Unknown/Fallback -> Paladin "Help!" (3171)
        assert(GetArchetypeDistressSound(9999) == 3171);
    }

    // =========================================================================
    // 17. Phase 4: Blind-Spot Threat Detection & Classification Tests
    // =========================================================================
    {
        using namespace framevr::companionai;
        const Vec2 playerPos{ 100.0f, 100.0f };
        const float forwardX = 0.0f;
        const float forwardY = -1.0f; // Looking North (-Y)

        // Monster 1: Directly ahead at distance 8 sub-tiles (North, 0 deg)
        const auto tAhead = EvaluateThreat(1, 10, { 100.0f, 92.0f }, playerPos, forwardX, forwardY, true, false);
        assert(!tAhead.isBlindSpot);
        assert(!tAhead.isRear);
        assert(tAhead.cosAngle > 0.99f);

        // Monster 2: Front-right flank at 45 deg (inside 110 deg FOV)
        const auto tFlankFront = EvaluateThreat(2, 10, { 105.6f, 94.4f }, playerPos, forwardX, forwardY, true, false);
        assert(!tFlankFront.isBlindSpot);
        assert(!tFlankFront.isRear);

        // Monster 3: Pure right flank at 90 deg (outside 110 deg FOV -> blind spot)
        const auto tSide = EvaluateThreat(3, 10, { 108.0f, 100.0f }, playerPos, forwardX, forwardY, true, false);
        assert(tSide.isBlindSpot);
        assert(!tSide.isRear); // cosAngle == 0.0f

        // Monster 4: Directly behind at distance 6 sub-tiles (South, 180 deg)
        const auto tBehind = EvaluateThreat(4, 10, { 100.0f, 106.0f }, playerPos, forwardX, forwardY, true, false);
        assert(tBehind.isBlindSpot);
        assert(tBehind.isRear); // cosAngle == -1.0f
        assert(tBehind.distance == 6.0f);
    }

    // =========================================================================
    // 18. Phase 4: Threat Scoring & Priority Tests
    // =========================================================================
    {
        using namespace framevr::companionai;
        const Vec2 playerPos{ 100.0f, 100.0f };
        const float forwardX = 0.0f;
        const float forwardY = -1.0f;

        // Frontal monster at distance 8: normal score (~40-60)
        const auto tFront = EvaluateThreat(1, 10, { 100.0f, 92.0f }, playerPos, forwardX, forwardY, true, false);
        // Blind-spot monster at distance 8: high score (> 400)
        const auto tBlind = EvaluateThreat(2, 10, { 108.0f, 100.0f }, playerPos, forwardX, forwardY, true, false);
        // Rear ambush monster at distance 6: very high score (> 500)
        const auto tRear = EvaluateThreat(3, 10, { 100.0f, 106.0f }, playerPos, forwardX, forwardY, true, false);
        // Emergency peel: player in hit recovery with attacker at distance 4 (> 900)
        const auto tEmergency = EvaluateThreat(4, 10, { 100.0f, 96.0f }, playerPos, forwardX, forwardY, true, true);

        assert(tBlind.score > tFront.score);
        assert(tRear.score > tBlind.score);
        assert(tEmergency.score > tRear.score);
        assert(tEmergency.score >= 900.0f);
    }

    // =========================================================================
    // 19. Phase 4: Interception Position Calculation Tests
    // =========================================================================
    {
        using namespace framevr::companionai;
        const Vec2 playerPos{ 100.0f, 100.0f };
        const Vec2 monsterPos{ 100.0f, 108.0f }; // 8 sub-tiles behind player

        // Melee Vanguard: positions screening between monster and player (~2.0 sub-tiles from monster)
        const Vec2 interceptMelee = CalculateInterceptPosition(playerPos, monsterPos, Archetype::MeleeVanguard);
        assert(interceptMelee.x == 100.0f);
        assert(interceptMelee.y > playerPos.y);  // Behind player toward monster
        assert(interceptMelee.y < monsterPos.y); // Between player and monster
        assert(std::fabs(monsterPos.y - interceptMelee.y - 2.0f) < 1e-3f);

        // Ranged Overwatch: maintains stand-off distance
        const Vec2 interceptRanged = CalculateInterceptPosition(playerPos, monsterPos, Archetype::RangedOverwatch);
        assert(interceptRanged.x == 100.0f);
        assert(interceptRanged.y > playerPos.y);
        assert(interceptRanged.y < monsterPos.y);
    }

    // =========================================================================
    // 20. Phase 4: Bodyguard Controller Selection & Cooldown Tests
    // =========================================================================
    {
        using namespace framevr::companionai;
        BodyguardController controller;
        const Vec2 playerPos{ 100.0f, 100.0f };
        const Vec2 compPos{ 100.0f, 97.0f }; // Companion is currently 3 sub-tiles ahead

        ThreatCandidate candidates[3];
        candidates[0] = EvaluateThreat(1, 10, { 100.0f, 85.0f }, playerPos, 0.0f, -1.0f, true, false); // Frontal (dist 15)
        candidates[1] = EvaluateThreat(2, 20, { 100.0f, 108.0f }, playerPos, 0.0f, -1.0f, true, false); // Rear ambush (dist 8)
        candidates[2] = EvaluateThreat(3, 30, { 110.0f, 100.0f }, playerPos, 0.0f, -1.0f, true, false); // Flanker (dist 10)

        ThreatCandidate best{};
        assert(controller.SelectPrimaryThreat(candidates, best));
        assert(best.unitId == 2); // Rear ambush must be prioritized!

        Vec2 targetMove{};
        // First evaluation: commands reposition toward intercept
        assert(controller.Evaluate(best, compPos, playerPos, Archetype::MeleeVanguard, 1000, targetMove));
        assert(controller.inPeelAction);
        assert(controller.activeTargetId == 2);

        // Immediate subsequent tick with same threat: cooldown prevents command spam
        assert(!controller.Evaluate(best, compPos, playerPos, Archetype::MeleeVanguard, 1200, targetMove));

        // After cooldown expires (1100ms > BodyguardPeelCooldownMs): allows update if still far
        assert(controller.Evaluate(best, compPos, playerPos, Archetype::MeleeVanguard, 2100, targetMove));

        // When companion reaches intercept position (within 2.5 sub-tiles): no further move commanded
        const Vec2 arrivedPos = CalculateInterceptPosition(playerPos, best.pos, Archetype::MeleeVanguard);
        assert(!controller.Evaluate(best, arrivedPos, playerPos, Archetype::MeleeVanguard, 2500, targetMove));
        assert(controller.inPeelAction);
    }

    // =========================================================================
    // 21. Phase 6: Archetype Tactical Voice Cue Mapping Tests
    // =========================================================================
    {
        using namespace framevr::companionai;
        // Rogue Scout (Amazon base: 2937, Dedicated move confirm: 4293/4297 "Sí / Vale")
        assert(GetArchetypeVoiceCue(MonStatsIndex::Rogue, TacticalVoiceCue::None) == 0);
        const int rogueDistress = GetArchetypeVoiceCue(MonStatsIndex::Rogue, TacticalVoiceCue::Distress);
        assert(rogueDistress == 2937 || rogueDistress == 2938);
        const int rogueMove = GetArchetypeVoiceCue(MonStatsIndex::Rogue, TacticalVoiceCue::MoveConfirm);
        assert(rogueMove == 4294 || rogueMove == 4291);
        const int rogueAtk = GetArchetypeVoiceCue(MonStatsIndex::Rogue, TacticalVoiceCue::AttackConfirm);
        assert(rogueAtk == 2937 + 12 || rogueAtk == 2937 + 13);
        const int rogueImp = GetArchetypeVoiceCue(MonStatsIndex::Rogue, TacticalVoiceCue::Impossible);
        assert(rogueImp == 2937 + 16 || rogueImp == 2937 + 17);
        const int rogueHold = GetArchetypeVoiceCue(MonStatsIndex::Rogue, TacticalVoiceCue::HoldConfirm);
        assert(rogueHold == 2937 + 6 || rogueHold == 2937 + 7);
        // RegroupLeash: 90% follow (base + 2/3), 10% farewell / "hasta luego" (base + 10/11)
        bool sawFollow = false;
        bool sawFarewell = false;
        for (int i = 0; i < 20; ++i) {
            const int reg = GetArchetypeVoiceCue(MonStatsIndex::Rogue, TacticalVoiceCue::RegroupLeash);
            if (reg == 2937 + 2 || reg == 2937 + 3) sawFollow = true;
            if (reg == 2937 + 10 || reg == 2937 + 11) sawFarewell = true;
        }
        assert(sawFollow && sawFarewell);

        // Desert Guard (Paladin base: 3171, Dedicated move confirm: 3952 "Sí / Vale")
        const int palDist = GetArchetypeVoiceCue(MonStatsIndex::Desert, TacticalVoiceCue::Distress);
        assert(palDist == 3171 || palDist == 3172);
        const int palMove = GetArchetypeVoiceCue(MonStatsIndex::Desert, TacticalVoiceCue::MoveConfirm);
        assert(palMove == 3952);
        const int palImp = GetArchetypeVoiceCue(MonStatsIndex::Desert, TacticalVoiceCue::Impossible);
        assert(palImp == 3171 + 16 || palImp == 3171 + 17);
        const int palHold = GetArchetypeVoiceCue(MonStatsIndex::Desert, TacticalVoiceCue::HoldConfirm);
        assert(palHold == 3171 + 6 || palHold == 3171 + 7);

        // Iron Wolf (Necromancer base: 3093, Dedicated move confirm: 4040/4041 "Sí / Vale")
        const int necDist = GetArchetypeVoiceCue(MonStatsIndex::IronWolf, TacticalVoiceCue::Distress);
        assert(necDist == 3093 || necDist == 3094);
        const int necMove = GetArchetypeVoiceCue(MonStatsIndex::IronWolf, TacticalVoiceCue::MoveConfirm);
        assert(necMove == 4040 || necMove == 4041);
        const int necImp = GetArchetypeVoiceCue(MonStatsIndex::IronWolf, TacticalVoiceCue::Impossible);
        assert(necImp == 3093 + 16 || necImp == 3093 + 17);

        // Barbarian (Barbarian base: 3015, Dedicated move confirm: 4565 "Sí / Vale")
        const int barbDist = GetArchetypeVoiceCue(MonStatsIndex::Barbarian1, TacticalVoiceCue::Distress);
        assert(barbDist == 3015 || barbDist == 3016);
        const int barbMove = GetArchetypeVoiceCue(MonStatsIndex::Barbarian1, TacticalVoiceCue::MoveConfirm);
        assert(barbMove == 4565);
        const int barbImp = GetArchetypeVoiceCue(MonStatsIndex::Barbarian2, TacticalVoiceCue::Impossible);
        assert(barbImp == 3015 + 16 || barbImp == 3015 + 17);
    }

    // =========================================================================
    // 22. Phase 6: Tactical Order Issuance & Rejection Validation Tests
    // =========================================================================
    {
        using namespace framevr::companionai;
        TacticalOrderController controller;
        const Vec2 playerPos{ 100.0f, 100.0f };
        const Vec2 compNear{ 100.0f, 96.0f }; // dist 4
        TacticalVoiceCue cue = TacticalVoiceCue::None;

        // 1. Valid MoveAndHold within leash (target at dist 15 < 40)
        TacticalOrder moveOrder{};
        moveOrder.type = TacticalOrderType::MoveAndHold;
        moveOrder.targetPos = { 100.0f, 85.0f };
        assert(controller.IssueOrder(moveOrder, 1000, compNear, playerPos, cue, false));
        assert(cue == TacticalVoiceCue::MoveConfirm);
        assert(controller.IsActive());
        assert(controller.GetOrderType() == TacticalOrderType::MoveAndHold);

        // 2. Reject order if companion is in retreat (< 50% HP)
        assert(!controller.IssueOrder(moveOrder, 1050, compNear, playerPos, cue, true));
        assert(cue == TacticalVoiceCue::Impossible);

        // 2b. Reject order if companion has critical health (< 50% HP)
        assert(!controller.IssueOrder(moveOrder, 1060, compNear, playerPos, cue, false, 40.0f, 100.0f));
        assert(cue == TacticalVoiceCue::Impossible);

        // 3. Reject order if target is beyond ElasticCombatLeashDistance (> 40 sub-tiles)
        TacticalOrder farTargetOrder{};
        farTargetOrder.type = TacticalOrderType::MoveAndHold;
        farTargetOrder.targetPos = { 100.0f, 150.0f }; // dist 50 > 40
        assert(!controller.IssueOrder(farTargetOrder, 1100, compNear, playerPos, cue, false));
        assert(cue == TacticalVoiceCue::Impossible);

        // 4. Reject order if companion itself is beyond leash (> 40 sub-tiles)
        const Vec2 compFar{ 100.0f, 150.0f }; // dist 50 > 40
        assert(!controller.IssueOrder(moveOrder, 1200, compFar, playerPos, cue, false));
        assert(cue == TacticalVoiceCue::Impossible);

        // 5. Valid DefensiveHold within leash
        TacticalOrder holdOrder{};
        holdOrder.type = TacticalOrderType::DefensiveHold;
        assert(controller.IssueOrder(holdOrder, 1300, compNear, playerPos, cue, false));
        assert(cue == TacticalVoiceCue::HoldConfirm);
        assert(controller.GetOrderType() == TacticalOrderType::DefensiveHold);

        // 6. Valid PrioritizeEnemy within leash
        TacticalOrder attackOrder{};
        attackOrder.type = TacticalOrderType::PrioritizeEnemy;
        attackOrder.targetUnitId = 42;
        attackOrder.targetPos = { 105.0f, 98.0f };
        assert(controller.IssueOrder(attackOrder, 1400, compNear, playerPos, cue, false));
        assert(cue == TacticalVoiceCue::AttackConfirm);
        assert(controller.GetOrderType() == TacticalOrderType::PrioritizeEnemy);
    }

    // =========================================================================
    // 23. Phase 6: Tactical Order Execution & Deadband Arrival Tests
    // =========================================================================
    {
        using namespace framevr::companionai;
        TacticalOrderController controller;
        const Vec2 playerPos{ 100.0f, 100.0f };
        const Vec2 compPos{ 100.0f, 96.0f };
        TacticalVoiceCue cue = TacticalVoiceCue::None;

        TacticalOrder moveOrder{};
        moveOrder.type = TacticalOrderType::MoveAndHold;
        moveOrder.targetPos = { 100.0f, 85.0f }; // dist 11 from compPos
        assert(controller.IssueOrder(moveOrder, 1000, compPos, playerPos, cue));

        Vec2 outTarget{};
        // En route: returns true to dispatch movement
        assert(controller.Evaluate(compPos, playerPos, 1100, false, outTarget, cue));
        assert(outTarget.x == 100.0f && outTarget.y == 85.0f);
        assert(cue == TacticalVoiceCue::None);

        // Arrived within deadband (<= 2.0 sub-tiles): returns false to hold ground stably
        const Vec2 compArrived{ 100.0f, 86.0f }; // dist 1.0 from target
        assert(!controller.Evaluate(compArrived, playerPos, 1200, false, outTarget, cue));
        assert(controller.IsActive()); // Still active holding position!

        // Defensive hold order: always returns false to hold current ground
        TacticalOrder holdOrder{};
        holdOrder.type = TacticalOrderType::DefensiveHold;
        assert(controller.IssueOrder(holdOrder, 1300, compPos, playerPos, cue));
        assert(!controller.Evaluate(compPos, playerPos, 1400, false, outTarget, cue));
        assert(controller.IsActive());

        // CancelOrder resets state
        controller.CancelOrder();
        assert(!controller.IsActive());
        assert(!controller.Evaluate(compPos, playerPos, 1500, false, outTarget, cue));
    }

    // =========================================================================
    // 24. Phase 6: Tactical Order Auto-Cancel by Retreat and Leash Break
    // =========================================================================
    {
        using namespace framevr::companionai;
        TacticalOrderController controller;
        const Vec2 playerPos{ 100.0f, 100.0f };
        const Vec2 compPos{ 100.0f, 96.0f };
        TacticalVoiceCue cue = TacticalVoiceCue::None;

        // Auto-cancel 1: Companion health drops below 50% (inRetreat = true)
        TacticalOrder moveOrder{};
        moveOrder.type = TacticalOrderType::MoveAndHold;
        moveOrder.targetPos = { 100.0f, 85.0f };
        assert(controller.IssueOrder(moveOrder, 1000, compPos, playerPos, cue));
        assert(controller.IsActive());

        Vec2 outTarget{};
        // Companion enters critical retreat: order must be auto-cancelled immediately!
        assert(!controller.Evaluate(compPos, playerPos, 1100, true, outTarget, cue));
        assert(cue == TacticalVoiceCue::Distress);
        assert(!controller.IsActive()); // Order aborted!

        // Auto-cancel 1b: Companion health drops below 50% during evaluation
        assert(controller.IssueOrder(moveOrder, 1150, compPos, playerPos, cue));
        assert(controller.IsActive());
        assert(!controller.Evaluate(compPos, playerPos, 1160, false, outTarget, cue, 30.0f, 100.0f));
        assert(cue == TacticalVoiceCue::Distress);
        assert(!controller.IsActive());

        // Auto-cancel 2: Player moves away and distance exceeds leash (> 40 sub-tiles)
        assert(controller.IssueOrder(moveOrder, 1200, compPos, playerPos, cue));
        assert(controller.IsActive());

        const Vec2 playerFar{ 100.0f, 150.0f }; // Player ran 50 sub-tiles away
        assert(!controller.Evaluate(compPos, playerFar, 1300, false, outTarget, cue));
        assert(cue == TacticalVoiceCue::RegroupLeash);
        assert(!controller.IsActive()); // Leash broke hold order!

        // Auto-cancel 3: Order duration timeout
        TacticalOrder timedOrder{};
        timedOrder.type = TacticalOrderType::DefensiveHold;
        timedOrder.holdDurationMs = 5000; // 5s duration
        assert(controller.IssueOrder(timedOrder, 2000, compPos, playerPos, cue));
        assert(controller.IsActive());

        // At 4s: still active
        assert(!controller.Evaluate(compPos, playerPos, 6000, false, outTarget, cue));
        assert(controller.IsActive());

        // At 7.1s (> 5s): timed out!
        assert(!controller.Evaluate(compPos, playerPos, 7100, false, outTarget, cue));
        assert(cue == TacticalVoiceCue::RegroupLeash);
        assert(!controller.IsActive());
    }

    // =========================================================================
    // 25. Phase 2: Engine Adapter & Tactical Commands Configuration Tests
    // =========================================================================
    {
        using namespace companionai;
        ConfigureTacticalCommands(true);
        assert(IsTacticalCommandsEnabled());

        ConfigureTacticalCommands(false);
        assert(!IsTacticalCommandsEnabled());

        ConfigureTacticalCommands(true);
        assert(IsTacticalCommandsEnabled());
        assert(TacticalCommandCount() == 0);

        CancelTacticalOrder();
        assert(GetActiveTacticalOrder().type == framevr::companionai::TacticalOrderType::None);
        assert(GetTacticalCommandState() == framevr::companionai::TacticalCommandState::Idle ||
               GetTacticalCommandState() == framevr::companionai::TacticalCommandState::Cancelled);
    }

    // =========================================================================
    // 26. Explicit Command State Machine: Lifecycle, Deduplication & No-Spam
    // =========================================================================
    {
        using namespace framevr::companionai;
        TacticalOrderController controller;
        const Vec2 playerPos{ 100.0f, 100.0f };
        const Vec2 compStart{ 100.0f, 96.0f };
        TacticalVoiceCue cue = TacticalVoiceCue::None;
        Vec2 outTarget{};

        assert(controller.GetState() == TacticalCommandState::Idle);
        assert(controller.GetCommandId() == 0);

        // Reject invalid (0, 0) target coordinates for MoveAndHold
        TacticalOrder zeroOrder{};
        zeroOrder.type = TacticalOrderType::MoveAndHold;
        zeroOrder.targetPos = { 0.0f, 0.0f };
        assert(!controller.IssueOrder(zeroOrder, 500, compStart, playerPos, cue));
        assert(cue == TacticalVoiceCue::Impossible);
        assert(controller.GetState() == TacticalCommandState::Idle);

        // 1. Issue valid MoveAndHold -> transitions Idle -> Requested with commandId = 1
        TacticalOrder moveOrder{};
        moveOrder.type = TacticalOrderType::MoveAndHold;
        moveOrder.targetPos = { 100.0f, 80.0f }; // dist 16 from compStart
        assert(controller.IssueOrder(moveOrder, 1000, compStart, playerPos, cue));
        assert(controller.GetState() == TacticalCommandState::Requested);
        assert(controller.GetCommandId() == 1);

        // 2. First Evaluate on game thread -> transitions Requested -> Moving and submits once
        assert(controller.Evaluate(compStart, playerPos, 1250, false, outTarget, cue));
        assert(controller.GetState() == TacticalCommandState::Moving);
        assert(outTarget.x == 100.0f && outTarget.y == 80.0f);

        // 3. Subsequent 250ms ticks while mercenary walks -> MUST NOT spam movement requests!
        assert(!controller.Evaluate({ 100.0f, 94.0f }, playerPos, 1500, false, outTarget, cue));
        assert(controller.GetState() == TacticalCommandState::Moving);
        assert(!controller.Evaluate({ 100.0f, 91.0f }, playerPos, 1750, false, outTarget, cue));
        assert(controller.GetState() == TacticalCommandState::Moving);
        assert(!controller.Evaluate({ 100.0f, 87.0f }, playerPos, 2000, false, outTarget, cue));
        assert(controller.GetState() == TacticalCommandState::Moving);

        // 4. Duplicate command within DuplicateTargetTolerance (0.5 sub-tiles) while Moving:
        //    Must NOT increment commandId, reset state to Requested, or trigger spam!
        TacticalOrder dupOrder = moveOrder;
        dupOrder.targetPos = { 100.2f, 80.1f };
        assert(controller.IssueOrder(dupOrder, 2100, { 100.0f, 87.0f }, playerPos, cue));
        assert(controller.GetCommandId() == 1);
        assert(controller.GetState() == TacticalCommandState::Moving);
        assert(!controller.Evaluate({ 100.0f, 85.0f }, playerPos, 2250, false, outTarget, cue));

        // 5. Arrival within ArrivalTolerance (<= 2.0 sub-tiles) -> transitions Moving -> Arrived
        assert(!controller.Evaluate({ 100.0f, 81.5f }, playerPos, 2500, false, outTarget, cue, 100.0f, 100.0f, Archetype::RangedOverwatch));
        assert(controller.GetState() == TacticalCommandState::Arrived);
        assert(controller.IsActive());

        // 6. Hysteresis deadband (HoldReleaseDeadband = 3.5f for Ranged/Caster):
        //    Minor pushback to dist 3.0 (> 2.0, <= 3.5) MUST stay Arrived without oscillating to Moving!
        assert(!controller.Evaluate({ 100.0f, 83.0f }, playerPos, 2750, false, outTarget, cue, 100.0f, 100.0f, Archetype::RangedOverwatch));
        assert(controller.GetState() == TacticalCommandState::Arrived);

        //    Large knockback beyond HoldReleaseDeadband (dist 5.0 > 3.5) transitions Arrived -> Moving once
        assert(controller.Evaluate({ 100.0f, 85.0f }, playerPos, 3000, false, outTarget, cue, 100.0f, 100.0f, Archetype::RangedOverwatch));
        assert(controller.GetState() == TacticalCommandState::Moving);
        assert(!controller.Evaluate({ 100.0f, 81.0f }, playerPos, 3250, false, outTarget, cue, 100.0f, 100.0f, Archetype::RangedOverwatch));
        assert(controller.GetState() == TacticalCommandState::Arrived);

        //    MeleeVanguard extended deadband (MeleeHoldEngageRadius + 1.5f = 7.5f):
        //    Dist 5.0 <= 7.5 remains Arrived so melee can strike hostiles within engage radius
        assert(!controller.Evaluate({ 100.0f, 85.0f }, playerPos, 3500, false, outTarget, cue, 100.0f, 100.0f, Archetype::MeleeVanguard));
        assert(controller.GetState() == TacticalCommandState::Arrived);
        //    Knockback beyond 7.5 (dist 8.0) transitions Arrived -> Moving
        assert(controller.Evaluate({ 100.0f, 88.0f }, playerPos, 3750, false, outTarget, cue, 100.0f, 100.0f, Archetype::MeleeVanguard));
        assert(controller.GetState() == TacticalCommandState::Moving);
    }

    // =========================================================================
    // 27. Explicit Command State Machine: Stall Detection, Retry Limits & Failure
    // =========================================================================
    {
        using namespace framevr::companionai;
        TacticalOrderController controller;
        const Vec2 playerPos{ 100.0f, 100.0f };
        const Vec2 stuckPos{ 100.0f, 95.0f };
        TacticalVoiceCue cue = TacticalVoiceCue::None;
        Vec2 outTarget{};

        TacticalOrder moveOrder{};
        moveOrder.type = TacticalOrderType::MoveAndHold;
        moveOrder.targetPos = { 100.0f, 80.0f };
        assert(controller.IssueOrder(moveOrder, 1000, stuckPos, playerPos, cue));
        assert(controller.GetState() == TacticalCommandState::Requested);

        // Initial submission at t=1000 -> Moving (retryCount = 0)
        assert(controller.Evaluate(stuckPos, playerPos, 1000, false, outTarget, cue));
        assert(controller.GetState() == TacticalCommandState::Moving);
        assert(controller.GetRetryCount() == 0);

        // No spam before StallTimeoutMs (2000ms)
        assert(!controller.Evaluate(stuckPos, playerPos, 1500, false, outTarget, cue));
        assert(!controller.Evaluate(stuckPos, playerPos, 2500, false, outTarget, cue));
        assert(controller.GetRetryCount() == 0);

        // Stall timeout #1 at t=3000 -> retry #1
        assert(controller.Evaluate(stuckPos, playerPos, 3000, false, outTarget, cue));
        assert(controller.GetState() == TacticalCommandState::Moving);
        assert(controller.GetRetryCount() == 1);

        // No spam between t=3000 and t=5000
        assert(!controller.Evaluate(stuckPos, playerPos, 4000, false, outTarget, cue));

        // Stall timeout #2 at t=5000 -> retry #2 (MaxMoveRetries = 2)
        assert(controller.Evaluate(stuckPos, playerPos, 5000, false, outTarget, cue));
        assert(controller.GetState() == TacticalCommandState::Moving);
        assert(controller.GetRetryCount() == 2);

        // Stall timeout #3 at t=7000 -> exceeds MaxMoveRetries, transitions Moving -> Failed!
        assert(!controller.Evaluate(stuckPos, playerPos, 7000, false, outTarget, cue));
        assert(controller.GetState() == TacticalCommandState::Failed);
        assert(controller.GetLastCancelReason() == TacticalCommandCancelReason::StalledNoProgress);
        assert(cue == TacticalVoiceCue::Impossible);
        assert(!controller.IsActive());

        // Subsequent ticks after Failed remain inert (no infinite retry loop)
        assert(!controller.Evaluate(stuckPos, playerPos, 7500, false, outTarget, cue));
        assert(controller.GetState() == TacticalCommandState::Failed);
    }

    // =========================================================================
    // 28. Explicit Command State Machine: Unreachable Room, Unit Death & Level Change
    // =========================================================================
    {
        using namespace framevr::companionai;
        TacticalOrderController controller;
        const Vec2 playerPos{ 100.0f, 100.0f };
        const Vec2 compPos{ 100.0f, 95.0f };
        TacticalVoiceCue cue = TacticalVoiceCue::None;

        // 1. Unreachable destination room -> Failed (DestinationUnreachable)
        TacticalOrder moveOrder{};
        moveOrder.type = TacticalOrderType::MoveAndHold;
        moveOrder.targetPos = { 100.0f, 80.0f };
        assert(controller.IssueOrder(moveOrder, 1000, compPos, playerPos, cue));
        controller.BindUnitAndArea(77, 0x20000);
        controller.ReportDestinationUnreachable(1100, cue);
        assert(controller.GetState() == TacticalCommandState::Failed);
        assert(controller.GetLastCancelReason() == TacticalCommandCancelReason::DestinationUnreachable);
        assert(cue == TacticalVoiceCue::Impossible);
        assert(!controller.IsActive());

        // 2. Mercenary death (animMode == 12 or health <= 0) -> Cancelled (UnitInvalidated)
        assert(controller.IssueOrder(moveOrder, 2000, compPos, playerPos, cue));
        controller.BindUnitAndArea(77, 0x20000);
        assert(controller.ValidateRuntimeContext(77, 0x20000, 2, 100.0f, 2100, cue));
        assert(!controller.ValidateRuntimeContext(77, 0x20000, 12, 0.0f, 2200, cue));
        assert(controller.GetState() == TacticalCommandState::Cancelled);
        assert(controller.GetLastCancelReason() == TacticalCommandCancelReason::UnitInvalidated);
        assert(!controller.IsActive());

        // 3. Mercenary unit ID change (resummoned/replaced) -> Cancelled (UnitInvalidated)
        assert(controller.IssueOrder(moveOrder, 3000, compPos, playerPos, cue));
        controller.BindUnitAndArea(77, 0x20000);
        assert(!controller.ValidateRuntimeContext(88, 0x20000, 1, 100.0f, 3100, cue));
        assert(controller.GetState() == TacticalCommandState::Cancelled);
        assert(controller.GetLastCancelReason() == TacticalCommandCancelReason::UnitInvalidated);

        // 4. Level/area transition (areaRoom becomes 0) -> Cancelled (LevelTransition)
        assert(controller.IssueOrder(moveOrder, 4000, compPos, playerPos, cue));
        controller.BindUnitAndArea(88, 0x20000);
        assert(!controller.ValidateRuntimeContext(88, 0, 1, 100.0f, 4100, cue));
        assert(controller.GetState() == TacticalCommandState::Cancelled);
        assert(controller.GetLastCancelReason() == TacticalCommandCancelReason::LevelTransition);

        // 5. PrioritizeEnemy target defeated -> Cancelled (TargetDefeated)
        TacticalOrder atkOrder{};
        atkOrder.type = TacticalOrderType::PrioritizeEnemy;
        atkOrder.targetUnitId = 555;
        atkOrder.targetPos = { 105.0f, 95.0f };
        assert(controller.IssueOrder(atkOrder, 5000, compPos, playerPos, cue));
        controller.ReportTargetDefeated(5500);
        assert(controller.GetState() == TacticalCommandState::Cancelled);
        assert(controller.GetLastCancelReason() == TacticalCommandCancelReason::TargetDefeated);
        assert(!controller.IsActive());
    }

    // =========================================================================
    // 29. Verified Server Walk Entry Point (0x4a77e0) & Native AI Detour (0x4a8a10)
    // =========================================================================
    {
        using namespace framevr::companionai;
        signatures = true;
        companionai::Configure(true);
        companionai::ConfigureTacticalCommands(true);
        companionai::Install();

        serverMoveCoordCalls = 0;
        origServerMoveUnitCalls = 0;

        // 1. Direct dispatch via 0x4a77e0 invokes ServerMoveToCoord with exact coordinates
        assert(companionai::DispatchServerMoveToCoord(0x20000, 0x40000, { 100.0f, 95.0f }, 100, 80));
        assert(serverMoveCoordCalls == 1);
        assert(lastServerMoveGame == 0x20000);
        assert(lastServerMoveUnit == 0x40000);
        assert(lastServerMoveX == 100);
        assert(lastServerMoveY == 80);

        // 2. Non-hireling units pass straight through DetourServerMoveUnit (0x4a8a10) untouched
        alignas(8) uint8_t otherUnit[0x80]{};
        alignas(8) uint16_t otherPath[32]{};
        const uint32_t otherHeader[4] = { 1, 10, 99999, 1 };
        std::memcpy(otherUnit, otherHeader, sizeof(otherHeader));
        const uintptr_t otherPathPtr = reinterpret_cast<uintptr_t>(otherPath);
        std::memcpy(otherUnit + 0x38, &otherPathPtr, sizeof(otherPathPtr));

        companionai::g_lastHirelingId.store(12345);
        assert(companionai::DetourServerMoveUnit(0x20000, reinterpret_cast<uintptr_t>(otherUnit), 0, 2, 110, 110, 1, 0) == 1);
        assert(origServerMoveUnitCalls == 1);
        assert(serverMoveCoordCalls == 1);

        // 3. Active hireling during MoveAndHold (Moving): native AI wander toward player is redirected to targetPos
        alignas(8) uint8_t mercUnit[0x80]{};
        alignas(8) uint16_t mercPath[32]{};
        mercPath[1] = 100; // posX = 100
        mercPath[3] = 95;  // posY = 95
        mercPath[10] = 100; // finalTargetX = 100 (player)
        mercPath[11] = 100; // finalTargetY = 100 (player)
        mercPath[26] = 0;   // dwPathPoints = 0
        const uint32_t mercHeader[4] = { 1, 271, 12345, 1 };
        std::memcpy(mercUnit, mercHeader, sizeof(mercHeader));
        const uintptr_t mercPathPtr = reinterpret_cast<uintptr_t>(mercPath);
        std::memcpy(mercUnit + 0x38, &mercPathPtr, sizeof(mercPathPtr));

        TacticalVoiceCue cue = TacticalVoiceCue::None;
        TacticalOrder moveOrder{};
        moveOrder.type = TacticalOrderType::MoveAndHold;
        moveOrder.targetPos = { 100.0f, 80.0f };
        assert(companionai::g_tacticalOrderController.IssueOrder(moveOrder, 1000, { 100.0f, 95.0f }, { 100.0f, 100.0f }, cue));
        Vec2 evalTarget{};
        assert(companionai::g_tacticalOrderController.Evaluate({ 100.0f, 95.0f }, { 100.0f, 100.0f }, 1000, false, evalTarget, cue));
        assert(companionai::g_tacticalOrderController.GetState() == TacticalCommandState::Moving);

        // Native AI attempts to move hireling to player (100, 102) -> redirected to (100, 80) via ServerMoveToCoord!
        assert(companionai::DetourServerMoveUnit(0x20000, reinterpret_cast<uintptr_t>(mercUnit), 0, 2, 100, 102, 1, 0) == 1);
        assert(origServerMoveUnitCalls == 1); // Not passed to original wander
        assert(serverMoveCoordCalls == 2);    // Redirected to (100, 80)
        assert(lastServerMoveX == 100);
        assert(lastServerMoveY == 80);

        // 4. Active hireling during MoveAndHold (Arrived): native AI wander is suppressed (returns 1 without moving)
        mercPath[1] = 100;
        mercPath[3] = 80; // Arrived at (100, 80)
        assert(!companionai::g_tacticalOrderController.Evaluate({ 100.0f, 80.0f }, { 100.0f, 100.0f }, 2000, false, evalTarget, cue));
        assert(companionai::g_tacticalOrderController.GetState() == TacticalCommandState::Arrived);

        assert(companionai::DetourServerMoveUnit(0x20000, reinterpret_cast<uintptr_t>(mercUnit), 0, 2, 100, 102, 1, 0) == 1);
        assert(origServerMoveUnitCalls == 1); // Suppressed!
        assert(serverMoveCoordCalls == 2);    // No extra move call while holding at destination!

        // 5. DetourServerStartWalk (0x445cc0) & DetourServerStartRun (0x445db0) block native 0x4a88fd->0x4475c0 follow-player walk
        alignas(8) uint8_t moveStruct[0x40]{};
        const uintptr_t mercUnitPtr = reinterpret_cast<uintptr_t>(mercUnit);
        std::memcpy(moveStruct + 0x08, &mercUnitPtr, sizeof(mercUnitPtr));
        const int wanderX = 100, wanderY = 102;
        std::memcpy(moveStruct + 0x18, &wanderX, sizeof(wanderX));
        std::memcpy(moveStruct + 0x1c, &wanderY, sizeof(wanderY));

        origServerStartWalkCalls = 0;
        origServerStartRunCalls = 0;
        assert(companionai::DetourServerStartWalk(0x20000, reinterpret_cast<uintptr_t>(moveStruct)) == 0);
        assert(companionai::DetourServerStartRun(0x20000, reinterpret_cast<uintptr_t>(moveStruct)) == 0);
        assert(origServerStartWalkCalls == 0);
        assert(origServerStartRunCalls == 0);

        // 6. Combat target arbitration during MoveAndHold:
        //    - While Moving (dist > ArrivalTolerance): TryOverrideCombatTarget suppresses combat targets (outTarget = 0)
        //    - Once Arrived: FilterNativeCombatTarget allows close targets and rejects distant targets beyond hold radius
        mercPath[1] = 100;
        mercPath[3] = 95; // Far from (100, 80)
        assert(companionai::g_tacticalOrderController.IssueOrder(moveOrder, 3000, { 100.0f, 95.0f }, { 100.0f, 100.0f }, cue));
        assert(companionai::g_tacticalOrderController.Evaluate({ 100.0f, 95.0f }, { 100.0f, 100.0f }, 3000, false, evalTarget, cue));
        uintptr_t overriddenTarget = 0x99999;
        assert(companionai::TryOverrideCombatTarget(0x20000, mercUnitPtr, overriddenTarget));
        assert(overriddenTarget == 0); // Suppressed while Moving to destination!

        // Arrive at (100, 80) with MeleeVanguard (Act 2 Desert Mercenary classId = 338)
        const uint32_t meleeMercHeader[4] = { 1, 338, 12345, 1 };
        std::memcpy(mercUnit, meleeMercHeader, sizeof(meleeMercHeader));
        mercPath[1] = 100;
        mercPath[3] = 80;
        assert(!companionai::g_tacticalOrderController.Evaluate({ 100.0f, 80.0f }, { 100.0f, 100.0f }, 4000, false, evalTarget, cue, 100.0f, 100.0f, Archetype::MeleeVanguard));
        assert(!companionai::TryOverrideCombatTarget(0x20000, mercUnitPtr, overriddenTarget));

        // Test hold sentry target acquisition: when g_activeHoldTargetId is populated,
        // TryOverrideCombatTarget overrides with that enemy!
        alignas(8) uint8_t enemyUnit[0x80]{};
        const uint32_t liveEnemyHeader[4] = { 1, 1, 888, 1 };
        std::memcpy(enemyUnit, liveEnemyHeader, sizeof(liveEnemyHeader));
        melee::g_testServerUnit = reinterpret_cast<uintptr_t>(enemyUnit);
        companionai::g_activeHoldTargetId.store(888);
        assert(companionai::TryOverrideCombatTarget(0x20000, mercUnitPtr, overriddenTarget));
        assert(overriddenTarget == reinterpret_cast<uintptr_t>(enemyUnit));
        companionai::g_activeHoldTargetId.store(0);
        melee::g_testServerUnit = 0;
        assert(!companionai::TryOverrideCombatTarget(0x20000, mercUnitPtr, overriddenTarget));

        alignas(8) uint16_t enemyPath[32]{};
        const uintptr_t enemyPathPtr = reinterpret_cast<uintptr_t>(enemyPath);
        std::memcpy(enemyUnit + 0x38, &enemyPathPtr, sizeof(enemyPathPtr));
        enemyPath[1] = 100;
        enemyPath[3] = 84; // 4 sub-tiles from anchor (<= 6.0 MeleeHoldEngageRadius)
        assert(companionai::FilterNativeCombatTarget(0x20000, mercUnitPtr, reinterpret_cast<uintptr_t>(enemyUnit)) == reinterpret_cast<uintptr_t>(enemyUnit));
        enemyPath[1] = 100;
        enemyPath[3] = 92; // 12 sub-tiles from anchor (> 6.0 MeleeHoldEngageRadius) -> rejected!
        assert(companionai::FilterNativeCombatTarget(0x20000, mercUnitPtr, reinterpret_cast<uintptr_t>(enemyUnit)) == 0);

        // 6b. Self-defense exception tests:
        //     - In FilterNativeCombatTarget: enemy at (100, 92) is 12 sub-tiles from anchor (100, 80) (> 6.0 MeleeHoldEngageRadius).
        //       If mercenary is at (100, 90), enemy is only 2 sub-tiles away (<= 3.5 self-defense radius) -> target allowed!
        mercPath[1] = 100;
        mercPath[3] = 90;
        assert(companionai::FilterNativeCombatTarget(0x20000, mercUnitPtr, reinterpret_cast<uintptr_t>(enemyUnit)) == reinterpret_cast<uintptr_t>(enemyUnit));

        //     - In TryOverrideCombatTarget: when Moving (distToAnchor > ArrivalTolerance), combat target is normally suppressed (outTarget = 0).
        //       If mercenary takes damage (animMode == 3 MONMODE_GETHIT), TryOverrideCombatTarget does NOT suppress (returns false), allowing self-defense!
        mercPath[3] = 95; // dist 15 to anchor
        uint32_t getHitMode = 3;
        std::memcpy(mercUnit + 0xc, &getHitMode, sizeof(getHitMode));
        uintptr_t hitTarget = 0x99999;
        assert(!companionai::TryOverrideCombatTarget(0x20000, mercUnitPtr, hitTarget));
        uint32_t walkMode = 1;
        std::memcpy(mercUnit + 0xc, &walkMode, sizeof(walkMode));

        // 7. Archetype-aware PrioritizeEnemy for RangedOverwatch (Act 1 Rogue classId = 271):
        //    Engages from stand-off distance (10..14 sub-tiles) without closing into < 4 sub-tile panic-flee range
        TacticalOrder rangedAtk{};
        rangedAtk.type = TacticalOrderType::PrioritizeEnemy;
        rangedAtk.targetUnitId = 777;
        rangedAtk.targetPos = { 100.0f, 80.0f };
        // Rogue at (100, 91) is 11 sub-tiles away -> already inside [5.0, 14.0] ranged firing window -> immediately Arrived!
        assert(companionai::g_tacticalOrderController.IssueOrder(rangedAtk, 5000, { 100.0f, 91.0f }, { 100.0f, 100.0f }, cue, false, 100.0f, 100.0f, Archetype::RangedOverwatch));
        assert(companionai::g_tacticalOrderController.GetState() == TacticalCommandState::Arrived);
        // Rogue at (100, 100) is 20 sub-tiles away (> 14.0) -> walks to stand-off point (100, 90) (10 sub-tiles from enemy), NOT (100, 80)!
        assert(companionai::g_tacticalOrderController.IssueOrder(rangedAtk, 6000, { 100.0f, 100.0f }, { 100.0f, 100.0f }, cue, false, 100.0f, 100.0f, Archetype::RangedOverwatch));
        assert(companionai::g_tacticalOrderController.Evaluate({ 100.0f, 100.0f }, { 100.0f, 100.0f }, 6000, false, evalTarget, cue, 100.0f, 100.0f, Archetype::RangedOverwatch));
        assert(std::fabs(evalTarget.x - 100.0f) < 1e-3f);
        assert(std::fabs(evalTarget.y - 90.0f) < 1e-3f);

        // Rogue arrives at (100, 90) -> Arrived
        assert(!companionai::g_tacticalOrderController.Evaluate({ 100.0f, 90.0f }, { 100.0f, 100.0f }, 6200, false, evalTarget, cue, 100.0f, 100.0f, Archetype::RangedOverwatch));
        assert(companionai::g_tacticalOrderController.GetState() == TacticalCommandState::Arrived);

        // While in optimal firing window, native AI walk/run towards the prioritized monster is suppressed
        const uint32_t rogueHeader[4] = { 1, 271, 12345, 1 };
        std::memcpy(mercUnit, rogueHeader, sizeof(rogueHeader));
        mercPath[1] = 100;
        mercPath[3] = 90;
        const int preWalkCalls = origServerMoveUnitCalls;
        assert(companionai::DetourServerMoveUnit(0x20000, reinterpret_cast<uintptr_t>(mercUnit), reinterpret_cast<uintptr_t>(enemyUnit), 2, 100, 80, 1, 0) == 1);
        assert(origServerMoveUnitCalls == preWalkCalls); // Suppressed: archer does NOT close into melee contact!

        // Kiting: Enemy moves into melee (dist < RangedMinStandOff = 5.0f, e.g. Rogue at (100, 83) is 3 sub-tiles from enemy at (100, 80))
        // Evaluate must transition back to Moving and compute kite destination keeping 10 sub-tiles stand-off!
        assert(companionai::g_tacticalOrderController.Evaluate({ 100.0f, 83.0f }, { 100.0f, 100.0f }, 6500, false, evalTarget, cue, 100.0f, 100.0f, Archetype::RangedOverwatch));
        assert(companionai::g_tacticalOrderController.GetState() == TacticalCommandState::Moving);
        assert(std::fabs(evalTarget.x - 100.0f) < 1e-3f);
        assert(std::fabs(evalTarget.y - 90.0f) < 1e-3f);

        // If issued when Rogue is already too close to a new target (e.g. at (100, 82), dist 2.0 < 5.0), must NOT mark Arrived!
        TacticalOrder closeEnemyAtk = rangedAtk;
        closeEnemyAtk.targetUnitId = 778;
        assert(companionai::g_tacticalOrderController.IssueOrder(closeEnemyAtk, 6600, { 100.0f, 82.0f }, { 100.0f, 100.0f }, cue, false, 100.0f, 100.0f, Archetype::RangedOverwatch));
        assert(companionai::g_tacticalOrderController.GetState() == TacticalCommandState::Requested);

        // 8. Near-obstacle arrival snap (<= 4.5 sub-tiles when A* stops at obstacle edge)
        TacticalOrder wallOrder{};
        wallOrder.type = TacticalOrderType::MoveAndHold;
        wallOrder.targetPos = { 100.0f, 80.0f };
        assert(companionai::g_tacticalOrderController.IssueOrder(wallOrder, 7000, { 100.0f, 95.0f }, { 100.0f, 100.0f }, cue));
        assert(companionai::g_tacticalOrderController.Evaluate({ 100.0f, 95.0f }, { 100.0f, 100.0f }, 7000, false, evalTarget, cue));
        assert(companionai::g_tacticalOrderController.GetState() == TacticalCommandState::Moving);
        assert(companionai::g_tacticalOrderController.SnapArrivedIfNearObstacle({ 100.0f, 83.7f }, 7500)); // dist = 3.7 <= 4.5
        assert(companionai::g_tacticalOrderController.GetState() == TacticalCommandState::Arrived);

        companionai::CancelTacticalOrder();
        assert(companionai::g_pendingRegroupMove.load());
        companionai::g_pendingRegroupMove.store(false);

        // 9. World Subtile Position Getters for VR raycasting and leash threshold
        float hx = 0.0f, hy = 0.0f;
        companionai::g_lastHirelingPosX.store(150.5f);
        companionai::g_lastHirelingPosY.store(250.25f);
        assert(companionai::GetHirelingWorldSubtilePos(hx, hy));
        assert(std::fabs(hx - 150.5f) < 1e-3f && std::fabs(hy - 250.25f) < 1e-3f);

        float px = 0.0f, py = 0.0f;
        companionai::g_lastPlayerPosX.store(140.0f);
        companionai::g_lastPlayerPosY.store(240.0f);
        assert(companionai::GetPlayerWorldSubtilePos(px, py));
        assert(std::fabs(px - 140.0f) < 1e-3f && std::fabs(py - 240.0f) < 1e-3f);

        // 10. Regression Coverage: MoveAndHold & DefensiveHold combat deadlock prevention
        //     Verifies enemy in/out of range before and after arrival, attack execution while holding,
        //     and resuming hold afterward for both RangedOverwatch and MeleeVanguard.
        {
            // Reset state
            companionai::CancelTacticalOrder();
            companionai::g_pendingRegroupMove.store(false);

            // A) RangedOverwatch (Act 1 Rogue, class 271) under MoveAndHold
            const uint32_t rogueHdr[4] = { 1, 271, 12345, 1 };
            std::memcpy(mercUnit, rogueHdr, sizeof(rogueHdr));
            mercPath[1] = 100;
            mercPath[3] = 100;

            TacticalOrder holdOrder{};
            holdOrder.type = TacticalOrderType::MoveAndHold;
            holdOrder.targetPos = { 100.0f, 80.0f };

            // Issue order: Requested -> Moving
            assert(companionai::g_tacticalOrderController.IssueOrder(holdOrder, 8000, { 100.0f, 100.0f }, { 100.0f, 100.0f }, cue));
            assert(companionai::g_tacticalOrderController.Evaluate({ 100.0f, 100.0f }, { 100.0f, 100.0f }, 8000, false, evalTarget, cue));
            assert(companionai::g_tacticalOrderController.GetState() == TacticalCommandState::Moving);

            // Before arrival (dist = 20.0 > ArrivalTolerance):
            // Enemy in range (anchor distance 15.0 <= 24.0)
            enemyPath[1] = 100;
            enemyPath[3] = 65;
            uintptr_t targetBefore = 0x99999;
            assert(companionai::TryOverrideCombatTarget(0x20000, mercUnitPtr, targetBefore));
            assert(targetBefore == 0); // Suppressed during transit so companion walks to destination uninterrupted

            // Mercenary walks to anchor: arrives at (100.0, 80.5), dist = 0.5 <= ArrivalTolerance
            mercPath[1] = 100;
            mercPath[3] = 80;
            assert(!companionai::g_tacticalOrderController.Evaluate({ 100.0f, 80.5f }, { 100.0f, 100.0f }, 8500, false, evalTarget, cue));
            assert(companionai::g_tacticalOrderController.GetState() == TacticalCommandState::Arrived);

            // After arrival:
            // 1) Enemy in range: enemy at (100, 65), anchor distance = 15.0 <= 24.0 (RangedHoldEngageRadius)
            int preCombatCalls = origServerMoveUnitCalls;
            assert(companionai::DetourServerMoveUnit(0x20000, mercUnitPtr, reinterpret_cast<uintptr_t>(enemyUnit), 4 /* MONMODE_ATTACK1 */, 100, 65, 1, 0) == 1);
            assert(origServerMoveUnitCalls == preCombatCalls + 1); // Allowed! Bow attack dispatched!

            // 2) Enemy out of range: enemy at (100, 50), anchor distance = 30.0 > 24.0 (RangedHoldEngageRadius)
            enemyPath[3] = 50;
            preCombatCalls = origServerMoveUnitCalls;
            assert(companionai::DetourServerMoveUnit(0x20000, mercUnitPtr, reinterpret_cast<uintptr_t>(enemyUnit), 4 /* MONMODE_ATTACK1 */, 100, 50, 1, 0) == 1);
            assert(origServerMoveUnitCalls == preCombatCalls); // Suppressed! Distant enemy outside engagement radius rejected!

            // 3) Companion remains in Arrived state after attack
            assert(companionai::g_tacticalOrderController.GetState() == TacticalCommandState::Arrived);
            assert(companionai::g_tacticalOrderController.IsActive());

            // B) MeleeVanguard (Act 2 Desert Mercenary, class 338) under DefensiveHold
            companionai::CancelTacticalOrder();
            companionai::g_pendingRegroupMove.store(false);

            const uint32_t meleeHdr[4] = { 1, 338, 12345, 1 };
            std::memcpy(mercUnit, meleeHdr, sizeof(meleeHdr));
            mercPath[1] = 100;
            mercPath[3] = 100;

            TacticalOrder defHold{};
            defHold.type = TacticalOrderType::DefensiveHold;
            defHold.targetPos = { 100.0f, 100.0f };

            assert(companionai::g_tacticalOrderController.IssueOrder(defHold, 9000, { 100.0f, 100.0f }, { 100.0f, 100.0f }, cue));
            assert(companionai::g_tacticalOrderController.GetState() == TacticalCommandState::Arrived);

            // 1) Enemy in melee engagement range: enemy at (100, 104), distance = 4.0 <= 6.0 (MeleeHoldEngageRadius)
            enemyPath[1] = 100;
            enemyPath[3] = 104;
            preCombatCalls = origServerMoveUnitCalls;
            assert(companionai::DetourServerMoveUnit(0x20000, mercUnitPtr, reinterpret_cast<uintptr_t>(enemyUnit), 4 /* MONMODE_ATTACK1 */, 100, 104, 1, 0) == 1);
            assert(origServerMoveUnitCalls == preCombatCalls + 1); // Allowed melee attack!

            // Stepping toward hostile in range is also allowed for MeleeVanguard
            preCombatCalls = origServerMoveUnitCalls;
            assert(companionai::DetourServerMoveUnit(0x20000, mercUnitPtr, reinterpret_cast<uintptr_t>(enemyUnit), 15 /* step walk */, 100, 104, 1, 0) == 1);
            assert(origServerMoveUnitCalls == preCombatCalls + 1); // Allowed step toward enemy!

            // 2) Enemy outside melee engagement range: enemy at (100, 110), distance = 10.0 > 6.0
            enemyPath[3] = 110;
            preCombatCalls = origServerMoveUnitCalls;
            assert(companionai::DetourServerMoveUnit(0x20000, mercUnitPtr, reinterpret_cast<uintptr_t>(enemyUnit), 4 /* MONMODE_ATTACK1 */, 100, 110, 1, 0) == 1);
            assert(origServerMoveUnitCalls == preCombatCalls); // Suppressed! Outside melee radius!

            // 3) Companion resumes holding at anchor afterward
            assert(companionai::g_tacticalOrderController.GetState() == TacticalCommandState::Arrived);
            assert(companionai::g_tacticalOrderController.IsActive());

            companionai::CancelTacticalOrder();
            companionai::g_pendingRegroupMove.store(false);
        }

        // ====================================================================
        // PHASE 1: COMPANION ITEM SKILLS & VIRTUAL CLASS INTERCEPTION TESTS
        // ====================================================================
        {
            using namespace framevr::companionai;

            // 1. Hook installation verification
            signatures = true;
            companionai::g_skillLevelHooked.store(false);
            companionai::originalSkillLevel = nullptr;
            companionai::ConfigureItemSkills(true);
            companionai::EnsureSkillLevelInstalled();
            assert(companionai::g_skillLevelHooked.load());
            assert(companionai::originalSkillLevel != nullptr);

            // Unit setups
            alignas(8) uint8_t mercUnit[0x80]{};
            const uintptr_t mercUnitPtr = reinterpret_cast<uintptr_t>(mercUnit);

            alignas(8) int16_t skillsRow[16]{};
            skillsRow[0] = 7;
            uintptr_t rowPtr = reinterpret_cast<uintptr_t>(skillsRow);
            alignas(8) uint8_t skillNode[0x60]{};
            std::memcpy(skillNode, &rowPtr, sizeof(rowPtr));
            const uintptr_t skillNodePtr = reinterpret_cast<uintptr_t>(skillNode);

            // 2. Rogue Scout (Act 1, class 271) using Fire Arrow (skill 7)
            const uint32_t rogueHeader[4] = { 1, 271, 10001, 1 }; // dwType=1, classId=271
            std::memcpy(mercUnit, rogueHeader, sizeof(rogueHeader));
            skillsRow[0] = 7; // skillId = 7
            fakeBaseSkillLevel = 10;

            // Gear: Helm (1) with +2 Amazon (stat 83, param 0), Bow (4) with +3 Bow (stat 188, param 0), Armor (3) with +1 Fire Arrow (stat 107, param 7)
            g_testEquippedSlots.clear();
            g_testEquippedSlots[1].stats = { { ItemStat::ClassSkills, 0, 2 } };
            g_testEquippedSlots[4].stats = { { ItemStat::TabSkills, 0, 3 } };
            g_testEquippedSlots[3].stats = { { ItemStat::SingleSkill, 7, 1 } };

            // With bonuses included: 10 + 2 (class) + 3 (tab) + 1 (single) = 16!
            assert(companionai::DetourSkillLevel(mercUnitPtr, skillNodePtr, 1, 0) == 16);

            // Without bonuses (bIncludeBonuses = 0): returns base level 10 untouched
            assert(companionai::DetourSkillLevel(mercUnitPtr, skillNodePtr, 0, 0) == 10);

            // 3. Iron Wolf (Act 3, class 359) using Glacial Spike (skill 55)
            const uint32_t wolfHeader[4] = { 1, 359, 10002, 1 };
            std::memcpy(mercUnit, wolfHeader, sizeof(wolfHeader));
            skillsRow[0] = 55; // skillId = 55
            fakeBaseSkillLevel = 12;

            g_testEquippedSlots.clear();
            g_testEquippedSlots[1].stats = { { ItemStat::ClassSkills, 1, 2 } }; // +2 Sorceress
            g_testEquippedSlots[4].stats = { { ItemStat::TabSkills, 5, 3 } };   // +3 Cold (tab 5)

            // 12 + 2 + 3 = 17!
            assert(companionai::DetourSkillLevel(mercUnitPtr, skillNodePtr, 1, 0) == 17);

            // 4. Cross-class rejection: Iron Wolf equips Barbarian helm (+2 Barbarian skills)
            g_testEquippedSlots.clear();
            g_testEquippedSlots[1].stats = { { ItemStat::ClassSkills, 4, 2 } }; // +2 Barbarian on Sorc
            assert(companionai::DetourSkillLevel(mercUnitPtr, skillNodePtr, 1, 0) == 12); // No bonus!

            // 5. Desert Guard (Act 2, class 338) with Might (98) and Jab (10)
            const uint32_t desertHeader[4] = { 1, 338, 10003, 1 };
            std::memcpy(mercUnit, desertHeader, sizeof(desertHeader));

            // Might (98, Paladin tab 10)
            skillsRow[0] = 98;
            fakeBaseSkillLevel = 8;
            g_testEquippedSlots.clear();
            g_testEquippedSlots[1].stats = { { ItemStat::ClassSkills, 3, 2 } }; // +2 Paladin
            g_testEquippedSlots[4].stats = { { ItemStat::TabSkills, 10, 3 } };  // +3 Offensive Auras
            assert(companionai::DetourSkillLevel(mercUnitPtr, skillNodePtr, 1, 0) == 13); // 8 + 2 + 3 = 13

            // Jab (10): Desert Guard special case with Amazon spear gear
            skillsRow[0] = 10;
            fakeBaseSkillLevel = 6;
            g_testEquippedSlots.clear();
            g_testEquippedSlots[4].stats = { { ItemStat::ClassSkills, 0, 2 }, { ItemStat::SingleSkill, 10, 3 } }; // +2 Amazon, +3 Jab
            assert(companionai::DetourSkillLevel(mercUnitPtr, skillNodePtr, 1, 0) == 11); // 6 + 2 (Ama class) + 3 (single) = 11

            // 6. Barbarian (Act 5, class 560) using Bash (126)
            const uint32_t barbHeader[4] = { 1, 560, 10004, 1 };
            std::memcpy(mercUnit, barbHeader, sizeof(barbHeader));
            skillsRow[0] = 126; // Bash
            fakeBaseSkillLevel = 15;
            g_testEquippedSlots.clear();
            g_testEquippedSlots[1].stats = { { ItemStat::ClassSkills, 4, 2 } }; // +2 Barbarian
            g_testEquippedSlots[4].stats = { { ItemStat::TabSkills, 12, 3 } };  // +3 Combat
            assert(companionai::DetourSkillLevel(mercUnitPtr, skillNodePtr, 1, 0) == 20); // 15 + 2 + 3 = 20

            // 7. Unlearned skills (baseLevel == 0):
            // Rogue with +2 Amazon helm, querying Strafe (skill 26, baseLevel = 0)
            std::memcpy(mercUnit, rogueHeader, sizeof(rogueHeader));
            skillsRow[0] = 26; // Strafe
            fakeBaseSkillLevel = 0;
            g_testEquippedSlots.clear();
            g_testEquippedSlots[1].stats = { { ItemStat::ClassSkills, 0, 2 } }; // +2 Amazon
            assert(companionai::DetourSkillLevel(mercUnitPtr, skillNodePtr, 1, 0) == 0); // Must NOT unlock unlearned skill!

            // But if gear has +1 Strafe directly (staffmod Stat 107), then it unlocks AND class bonus applies:
            g_testEquippedSlots[4].stats = { { ItemStat::SingleSkill, 26, 1 } }; // +1 Strafe bow
            assert(companionai::DetourSkillLevel(mercUnitPtr, skillNodePtr, 1, 0) == 3); // 0 + 1 (single) + 2 (class) = 3!

            // 8. Feature toggle: disabling companion item skills bypasses bonuses completely
            companionai::ConfigureItemSkills(false);
            assert(!companionai::IsItemSkillsEnabled());
            skillsRow[0] = 7; // Fire Arrow
            fakeBaseSkillLevel = 10;
            g_testEquippedSlots.clear();
            g_testEquippedSlots[1].stats = { { ItemStat::ClassSkills, 0, 5 } };
            // Returns baseLevel 10 directly
            assert(companionai::DetourSkillLevel(mercUnitPtr, skillNodePtr, 1, 0) == 10);

            // Re-enabling restores bonuses
            companionai::ConfigureItemSkills(true);
            assert(companionai::IsItemSkillsEnabled());
            assert(companionai::DetourSkillLevel(mercUnitPtr, skillNodePtr, 1, 0) == 15);

            // Developer charges parameter
            companionai::ConfigureConsumeItemCharges(false);
            assert(!companionai::IsConsumeItemChargesEnabled());
            companionai::ConfigureConsumeItemCharges(true);
            assert(companionai::IsConsumeItemChargesEnabled());
            companionai::ConfigureConsumeItemCharges(false);

            // Non-hireling units (e.g. player dwType == 0) untouched
            const uint32_t playerUnitHeader[4] = { 0, 0, 1, 0 };
            alignas(8) uint8_t playerUnit[0x80]{};
            std::memcpy(playerUnit, playerUnitHeader, sizeof(playerUnitHeader));
            assert(companionai::DetourSkillLevel(reinterpret_cast<uintptr_t>(playerUnit), skillNodePtr, 1, 0) == 10);


            // 9. Phase 3 & 4 Live Tactical Charged Cast & Accounting
            {
                // Equip Rogue with Teleport charges (skill 54, lvl 1, 20/20) on weapon slot 4
                std::memcpy(mercUnit, rogueHeader, sizeof(rogueHeader));
                g_testEquippedSlots.clear();
                const uint16_t tpParam = framevr::companionai::PackChargedSkillParam(54, 1);
                const int32_t tpVal = framevr::companionai::PackChargedSkillValue(20, 20);
                g_testEquippedSlots[4].stats = { { ItemStat::ChargedSkill, tpParam, tpVal } };

                companionai::ConfigureItemSkills(true);
                companionai::ConfigureConsumeItemCharges(false); // Zero-Wear Dev mode
                g_dispatchCount = 0;
                g_setStatCount = 0;

                const framevr::companionai::Vec2 compP{ 100.0f, 100.0f };
                const framevr::companionai::Vec2 plyrP{ 95.0f, 95.0f };
                uint64_t tickMs = 100000;

                // Companion health 30% (< 40% threshold)
                // When called, emergency retreat should select and cast Teleport to player pos
                // Note: melee::HostilesAround is mocked or 0; we set health low
                // Let's call ExecuteCompanionTacticalCharges:
                companionai::ExecuteCompanionTacticalCharges(
                    1, mercUnitPtr, 271, 30.0f, 100.0f,
                    compP, plyrP, tickMs, true, false);

                // Zero-Wear Dev mode: cast dispatched, but charges NOT decremented
                assert(g_dispatchCount == 1);
                assert(g_lastDispatchedSkill == 54);
                assert(g_lastDispatchedLevel == 1);
                assert(g_lastDispatchedDestX == 95);
                assert(g_lastDispatchedDestY == 95);
                assert(g_setStatCount == 0); // No wear in dev mode!

                // Realistic consumption mode:
                companionai::ConfigureConsumeItemCharges(true);
                tickMs += 15000; // Past 12s Teleport cooldown
                companionai::ExecuteCompanionTacticalCharges(
                    1, mercUnitPtr, 271, 30.0f, 100.0f,
                    compP, plyrP, tickMs, true, false);

                assert(g_dispatchCount == 2);
                assert(g_setStatCount == 1); // 1 charge consumed!
                assert(g_lastSetStatId == ItemStat::ChargedSkill);
                assert(g_lastSetStatParam == tpParam);
                assert((g_lastSetStatVal & 0xFF) == 19); // 20 -> 19 charges!
                assert(((g_lastSetStatVal >> 8) & 0xFF) == 20); // Max charges 20 preserved

                // Cooldown enforcement: immediate second tick without waiting should NOT cast
                tickMs += 100; // Only 100ms later
                companionai::ExecuteCompanionTacticalCharges(
                    1, mercUnitPtr, 271, 30.0f, 100.0f,
                    compP, plyrP, tickMs, true, false);
                assert(g_dispatchCount == 2); // Still 2!

                // Master toggle disabled:
                companionai::ConfigureItemSkills(false);
                tickMs += 15000; // Cooldown expired
                companionai::ExecuteCompanionTacticalCharges(
                    1, mercUnitPtr, 271, 30.0f, 100.0f,
                    compP, plyrP, tickMs, true, false);
                assert(g_dispatchCount == 2); // No new dispatch when disabled!

                // Re-enable for clean state
                companionai::ConfigureItemSkills(true);
                companionai::ConfigureConsumeItemCharges(false);
                g_testEquippedSlots.clear();
            }

            // Test 7: Corpse Skill (Poison Explosion 83) prerequisite and targeting validation
            {
                g_dispatchCount = 0;
                g_setStatCount = 0;
                g_mockCorpseCount = 0;
                g_testEquippedSlots.clear();

                const uint16_t peParam = framevr::companionai::PackChargedSkillParam(83, 6); // Poison Explosion lvl 6
                const int32_t peVal = framevr::companionai::PackChargedSkillValue(40, 40);   // 40 charges
                g_testEquippedSlots[5].header[2] = 205; // Distinct item ID
                g_testEquippedSlots[5].stats = { { static_cast<int>(ItemStat::ChargedSkill), peParam, peVal } };

                const framevr::companionai::Vec2 compP{ 100.0f, 100.0f };
                const framevr::companionai::Vec2 plyrP{ 100.0f, 100.0f };
                uint64_t tickMs = 200000;

                // Case A: In combat but NO corpses (g_mockCorpseCount == 0)
                // Poison Explosion must NOT cast because no corpses exist
                companionai::ExecuteCompanionTacticalCharges(
                    1, mercUnitPtr, 271, 100.0f, 100.0f,
                    compP, plyrP, tickMs, true, true);
                assert(g_dispatchCount == 0);

                // Case B: A corpse appears at (112, 115)
                g_mockCorpseCount = 1;
                g_mockCorpses[0] = { 777, 10, 112.0f, 115.0f, 0x60000 };
                tickMs += 500;

                companionai::ExecuteCompanionTacticalCharges(
                    1, mercUnitPtr, 271, 100.0f, 100.0f,
                    compP, plyrP, tickMs, true, true);

                // Poison Explosion should trigger and target corpse coordinates!
                assert(g_dispatchCount == 1);
                assert(g_lastDispatchedSkill == 83);
                assert(g_lastDispatchedLevel == 6);
                assert(g_lastDispatchedDestX == 112);
                assert(g_lastDispatchedDestY == 115);

                g_mockCorpseCount = 0;
                g_testEquippedSlots.clear();
            }

            // Test 8: Equipment Skill (Strafe 26) DetourSkill lookup & Revival Cache Invalidation
            {
                g_dispatchCount = 0;
                g_testEquippedSlots.clear();

                // Rogue mercenary class 271 with Strafe (skill 26, lvl 4) from item
                const uint16_t strafeParam = 26; // Skill ID
                const int32_t strafeLvl = 4;
                g_testEquippedSlots[4].header[2] = 301; // Bow slot
                g_testEquippedSlots[4].stats = { { static_cast<int>(ItemStat::SingleSkill), strafeParam, strafeLvl } };

                const framevr::companionai::Vec2 compP{ 100.0f, 100.0f };
                const framevr::companionai::Vec2 plyrP{ 100.0f, 100.0f };
                uint64_t tickMs = 300000;

                // Step 1: Scan and execute (Strafe lvl 4 fires min(4+4, 10) = 8 arrows in volley)
                companionai::ExecuteCompanionTacticalCharges(
                    1, mercUnitPtr, 271, 100.0f, 100.0f,
                    compP, plyrP, tickMs, true, true);

                assert(g_dispatchCount == 8);
                assert(g_lastDispatchedSkill == 26);
                assert(g_lastDispatchedLevel == 4);

                // Step 2: Verify synthetic node was populated with Strafe row, exact level at 0x40, monster targetType and owner -1
                assert(companionai::s_companionSyntheticNode.level == 4);
                assert(companionai::s_companionSyntheticNode.row != 0);
                assert(companionai::s_companionSyntheticNode.targetType == 1);
                assert(companionai::s_companionSyntheticNode.owner == -1);

                // Step 3: Simulate mercenary death / revival:
                // ClearCachedSkillBadges() is called when hireling disappears
                companionai::ClearCachedSkillBadges();

                // On the next tick with the same items on the revived merc,
                // cacheEmpty is true, forcing re-scan and execution
                tickMs += 10000;
                companionai::ExecuteCompanionTacticalCharges(
                    1, mercUnitPtr, 271, 100.0f, 100.0f,
                    compP, plyrP, tickMs, true, true);

                assert(g_dispatchCount == 16);
                assert(g_lastDispatchedSkill == 26);

                g_testEquippedSlots.clear();
            }

            g_testEquippedSlots.clear();
        }
    }

    return 0;
}
