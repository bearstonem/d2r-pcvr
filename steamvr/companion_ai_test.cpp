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

int pathMoveCalls = 0;
uintptr_t lastPathMovePath = 0;
uintptr_t lastPathMoveUnit = 0;
uintptr_t lastPathMoveRoom = 0;
int lastPathMoveX = 0;
int lastPathMoveY = 0;

int FakePathMove(uintptr_t path, uintptr_t unit, uintptr_t room, int x, int y) {
    ++pathMoveCalls;
    lastPathMovePath = path;
    lastPathMoveUnit = unit;
    lastPathMoveRoom = room;
    lastPathMoveX = x;
    lastPathMoveY = y;
    return 1;
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
    if (!out || tableId != D2RL::DataTables::TableId::MonStats) return D2RL::DataTables::Result::Unavailable;
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

namespace d2rsig {
bool Check(uint64_t rva, const void*, uint32_t) {
    if (rva == companionai::abi::ModeRva ||
        rva == companionai::abi::RelocateRva ||
        rva == companionai::abi::RoomPointRva ||
        rva == companionai::abi::PathMoveRva) {
        return signatures;
    }
    return false;
}
uintptr_t Addr(uint64_t rva) {
    if (rva == companionai::abi::ModeRva) return reinterpret_cast<uintptr_t>(&FakeMode);
    if (rva == companionai::abi::RelocateRva) return reinterpret_cast<uintptr_t>(&FakeRelocate);
    if (rva == companionai::abi::RoomPointRva) return reinterpret_cast<uintptr_t>(&FakeRoomPoint);
    if (rva == companionai::abi::PathMoveRva) return reinterpret_cast<uintptr_t>(&FakePathMove);
    return 0;
}
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

namespace melee {
bool LocalServer(uintptr_t& game, uintptr_t& player) {
    game = 0x20000;
    player = 0x30000;
    return true;
}
uintptr_t ServerUnit(uintptr_t game, int type, uint32_t id) {
    (void)game; (void)type; (void)id;
    return 0x40000;
}
bool HirelingVitals(uint32_t hint, float& health, float& maxHealth, uint32_t& classId, uint32_t& unitId) {
    (void)hint;
    health = 100.0f;
    maxHealth = 100.0f;
    classId = 271;
    unitId = 12345;
    return true;
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

    return 0;
}
