#include <windows.h>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <vector>
#include "companion_ai.h"
#include "../steamvr/companion_ai.h"
#include "../steamvr/companion_skills.h"
#include "../steamvr/companion_charges.h"
#include "../steamvr/companion_item_skills_ui.h"
#include "../steamvr/telegraph.h"
#include "../steamvr/callback_ticket.h"
#include "../steamvr/logging.h"
#include "bow.h"
#include "companion_ai_signatures.h"
#include "enemy_sight.h"
#include "melee.h"
#include "memory.h"
#include "sigscan.h"
#include "native_ui.h"

namespace companionai {
namespace {
using namespace D2RL;
using ModeFn = int(*)();
using RelocateFn = int(*)(uintptr_t game, uintptr_t unit, uintptr_t room, int x, int y, int unk1, int unk2);
using RoomPointFn = uintptr_t(*)(uintptr_t room, int x, int y);
using ServerMoveToCoordFn = int(*)(uintptr_t game, uintptr_t unit, int x, int y);
using ServerMoveUnitFn = int(*)(uintptr_t game, uintptr_t unit, uintptr_t targetUnit, int moveMode, int x, int y, uint8_t pathType, uint16_t flags);
using ServerStartMoveModeFn = int(*)(uintptr_t game, uintptr_t moveStruct);
using SkillLevelFn = int(*)(uintptr_t unit, uintptr_t skillNode, int bIncludeBonuses, int unk);
using InventoryFn = uintptr_t(*)(uintptr_t unit, const char* file, int line);
using EquippedFn = uintptr_t(*)(uintptr_t inventory, int bodyLocation);
using GetStatFn = int(*)(uintptr_t unit, int stat, int param);

struct BankState {
    const void* rows = nullptr;
    uint32_t rowCount = 0;
    int appliedRange = framevr::companionai::DefaultDetectRange;
    std::vector<framevr::companionai::HirelingBaseline> baseline;
};

const PluginContext* g_ctx = nullptr;
const DataTableService* g_tables = nullptr;
const ThreadService* g_threads = nullptr;
ModeFn mode = nullptr;
RelocateFn relocateFn = nullptr;
RoomPointFn roomPointFn = nullptr;
ServerMoveToCoordFn serverMoveToCoordFn = nullptr;
ServerMoveUnitFn originalServerMoveUnit = nullptr;
ServerStartMoveModeFn originalServerStartWalk = nullptr;
ServerStartMoveModeFn originalServerStartRun = nullptr;
std::atomic<bool> g_serverMoveHooked{false};
std::atomic<bool> g_serverStartWalkHooked{false};
std::atomic<bool> g_serverStartRunHooked{false};
SkillLevelFn originalSkillLevel = nullptr;
std::atomic<bool> g_skillLevelHooked{false};
std::atomic<bool> g_companionItemSkillsEnabled{true};
std::atomic<bool> g_companionConsumeItemCharges{false};
std::atomic<bool> g_nativeMoveBlockedWhileMoving{false};
static SRWLOCK s_badgesLock = SRWLOCK_INIT;
static std::vector<framevr::companionui::MercenarySkillBadge> s_cachedBadges;
static std::atomic<uint64_t> s_badgesRevision{1};

static std::vector<framevr::companionai::CompanionChargedSkill> s_cachedCharges;
static uint32_t s_lastHirelingUnitId = 0;
static std::array<uint32_t, 4> s_lastItemIds{};
static size_t s_lastSlotCount = 0;
static uint64_t s_lastTacticalCastMs = 0;

void ResetCompanionEquipmentCache() noexcept {
    s_lastHirelingUnitId = 0;
    s_lastSlotCount = 0;
    s_lastItemIds.fill(0);
    s_cachedCharges.clear();
}

void ClearCachedSkillBadges() {
    AcquireSRWLockExclusive(&s_badgesLock);
    if (!s_cachedBadges.empty()) {
        s_cachedBadges.clear();
        s_badgesRevision.fetch_add(1, std::memory_order_release);
    }
    ReleaseSRWLockExclusive(&s_badgesLock);
    ResetCompanionEquipmentCache();
}
std::atomic<bool> g_pendingRegroupMove{false};
thread_local bool g_inCommandedMove = false;
thread_local bool g_inCommandedAction = false;
thread_local uintptr_t g_commandedActionTargetUnit = 0;

std::atomic<bool> g_enabled{true};
std::atomic<bool> g_installed{false};
std::atomic<bool> g_anyModified{false};
std::atomic<int> g_lastAppliedRange{framevr::companionai::DefaultDetectRange};
std::atomic<unsigned> g_unstuckEvents{0};
std::atomic<unsigned> g_tacticalMoveEvents{0};
std::atomic<unsigned> g_peelEvents{0};
std::atomic<bool> g_peelEnabled{true};

// Cached server state for lock-free cross-thread queries (VR gesture thread -> game thread)
std::atomic<uint32_t> g_lastHirelingId{0};
std::atomic<uint32_t> g_lastHirelingClassId{0};
std::atomic<float> g_lastHirelingPosX{0.0f};
std::atomic<float> g_lastHirelingPosY{0.0f};
std::atomic<float> g_lastHirelingHealth{100.0f};
std::atomic<float> g_lastHirelingMaxHealth{100.0f};
std::atomic<float> g_lastPlayerPosX{0.0f};
std::atomic<float> g_lastPlayerPosY{0.0f};
std::atomic<uintptr_t> g_lastPlayerRoom{0};

std::atomic<uint32_t> g_activePeelTargetId{0};
std::atomic<uint32_t> g_activeHoldTargetId{0};
std::atomic<float> g_cachedForwardX{0.0f};
std::atomic<float> g_cachedForwardY{-1.0f};
std::atomic<bool> g_gameThreadPending{false};
std::atomic<uint64_t> g_lastDispatchMs{0};
std::atomic<int> g_distressSound{1}; // 1 = auto per-archetype emote voice
std::atomic<bool> g_potionReceived{false};
std::atomic<bool> g_inRetreatState{false};
std::atomic<bool> g_watchdogResetRequested{false};

std::mutex g_ticketMutex;
framevr::CallbackTicket g_syncTicket;
BankState g_banks[3]{}; // Classic, Lod, Rotw
framevr::companionai::StuckWatchdog g_watchdog;
framevr::companionai::TacticalFormationController g_formationController;
framevr::companionai::TacticalRetreatController g_retreatController;
framevr::companionai::BodyguardController g_bodyguardController;
std::atomic<bool> g_tacticalCommandsEnabled{true};
std::atomic<unsigned> g_tacticalCommandEvents{0};
std::atomic<int> g_lastDispatchedTargetX{0};
std::atomic<int> g_lastDispatchedTargetY{0};
framevr::companionai::TacticalOrderController g_tacticalOrderController;
SRWLOCK g_tacticalLock = SRWLOCK_INIT;

template <class T>
bool Read(uintptr_t p, T& out) {
    return p >= 0x10000 && p <= UINTPTR_MAX - sizeof(out) && framevr::ReadMemory(&out, reinterpret_cast<const void*>(p), sizeof(out));
}

bool ReadPosition(uintptr_t unit, float& x, float& y) {
    uintptr_t path = 0;
    uint16_t p[4]{};
    if (!Read(unit + 0x38, path) || !Read(path, p)) return false;
    x = p[1] + p[0] / 65536.0f;
    y = p[3] + p[2] / 65536.0f;
    return true;
}

int EffectiveDetectRange() {
    if (!g_enabled.load(std::memory_order_relaxed)) return framevr::companionai::DefaultDetectRange;
    if (!mode || mode() != 0) return framevr::companionai::DefaultDetectRange;
    if (g_inRetreatState.load(std::memory_order_relaxed)) {
        return framevr::companionai::DefaultDetectRange;
    }
    return framevr::companionai::ComputeCompanionDetectRange(enemysight::AppliedPercent());
}

void SyncBanks(const PluginContext* ctx) {
    using namespace framevr::companionai;
    if (!ctx || !g_tables || !g_tables->getTable) return;
    const int targetRange = EffectiveDetectRange();
    static constexpr DataTables::Bank kBanks[3] = {
        DataTables::Bank::Classic,
        DataTables::Bank::Lod,
        DataTables::Bank::Rotw,
    };
    bool modifiedNow = false;

    for (int b = 0; b < 3; ++b) {
        auto& state = g_banks[b];
        if (targetRange == DefaultDetectRange && state.baseline.empty()) continue;

        DataTables::TableView view{};
        view.structSize = DataTables::TableViewSize;
        if (g_tables->getTable(ctx, kBanks[b], DataTables::TableId::MonStats, &view) != DataTables::Result::Success ||
            !view.rows || view.rowSize != RowSize || view.rowCount < 562) {
            if (state.appliedRange != DefaultDetectRange) modifiedNow = true;
            continue;
        }

        if (state.rows != view.rows || state.rowCount != view.rowCount || state.baseline.empty()) {
            std::vector<uint8_t> snapshot(size_t(view.rowCount) * RowSize);
            if (!framevr::ReadMemory(snapshot.data(), view.rows, snapshot.size()) ||
                !CaptureHirelingBaselines(snapshot, view.rowCount, state.baseline)) {
                state.baseline.clear();
                continue;
            }
            state.rows = view.rows;
            state.rowCount = view.rowCount;
            state.appliedRange = DefaultDetectRange;
        }

        if (state.appliedRange == targetRange) {
            if (targetRange != DefaultDetectRange) modifiedNow = true;
            continue;
        }

        auto* liveData = static_cast<uint8_t*>(const_cast<void*>(view.rows));
        std::span<uint8_t> liveSpan(liveData, size_t(view.rowCount) * RowSize);
        bool success = false;
        if (targetRange == DefaultDetectRange) {
            success = RestoreHirelingBaselines(liveSpan, view.rowCount, state.baseline);
        } else {
            success = WriteHirelingAiDist(liveSpan, view.rowCount, uint8_t(targetRange));
        }

        if (success) {
            state.appliedRange = targetRange;
            if (targetRange != DefaultDetectRange) modifiedNow = true;
        }
    }

    g_anyModified.store(modifiedNow);
    g_lastAppliedRange.store(targetRange);
}

void __cdecl SyncBanksCallback(const PluginContext* ctx, void* token) noexcept {
    const auto ticketId = reinterpret_cast<uintptr_t>(token);
    {
        std::lock_guard lock(g_ticketMutex);
        if (!g_syncTicket.Claim(ticketId)) return;
    }
    SyncBanks(ctx);
}

void __cdecl DistressSoundCallback(const PluginContext*, void* user) noexcept {
    const int sound = int(reinterpret_cast<uintptr_t>(user));
    if (sound > 0) {
        const bool played = bowvr::PlayHeroSound(sound);
        char line[128];
        std::snprintf(line, sizeof line, "Frame companion AI: distress voice sound %d playback %s",
            sound, played ? "succeeded" : "FAILED");
        framevr::logging::Write(line);
    }
}

void __cdecl TacticalVoiceCallback(const PluginContext*, void* user) noexcept {
    const int sound = int(reinterpret_cast<uintptr_t>(user));
    if (sound > 0) {
        const bool played = bowvr::PlayHeroSound(sound);
        char line[128];
        std::snprintf(line, sizeof line, "Frame companion AI: tactical voice sound %d playback %s",
            sound, played ? "succeeded" : "FAILED");
        framevr::logging::Write(line);
    }
}

void PlayVoiceCue(uint16_t classId, framevr::companionai::TacticalVoiceCue cue) {
    if (cue == framevr::companionai::TacticalVoiceCue::None) return;
    const int sound = framevr::companionai::GetArchetypeVoiceCue(classId, cue);
    if (sound > 0 && g_threads && g_threads->runOnUiThread) {
        g_threads->runOnUiThread(g_ctx, &TacticalVoiceCallback, reinterpret_cast<void*>(uintptr_t(sound)));
    }
}

void LogTacticalTransition(
    uint64_t cmdId,
    framevr::companionai::TacticalOrderType orderType,
    framevr::companionai::TacticalCommandState prevState,
    framevr::companionai::TacticalCommandState newState,
    framevr::companionai::TacticalCommandCancelReason reason,
    uint8_t retries,
    uint32_t unitId,
    uint32_t animMode,
    framevr::companionai::Vec2 compPos,
    framevr::companionai::Vec2 targetPos,
    uintptr_t targetRoom,
    uintptr_t hireling) noexcept {
    uint16_t pathWords[32]{};
    uintptr_t compPath = 0;
    if (hireling && Read(hireling + 0x38, compPath) && compPath) {
        (void)Read(compPath, pathWords);
    }
    const float dist = std::hypot(compPos.x - targetPos.x, compPos.y - targetPos.y);
    char line[360];
    std::snprintf(line, sizeof line,
        "Frame companion AI: cmd#%llu type=%u state %s -> %s (reason=%s retry=%u unit=%u mode=%u pos=(%.1f,%.1f) target=(%.1f,%.1f) dist=%.2f room=0x%llx pathTarget=(%u,%u) finalTarget=(%u,%u) pts=%u)",
        static_cast<unsigned long long>(cmdId),
        static_cast<unsigned>(orderType),
        framevr::companionai::TacticalCommandStateName(prevState),
        framevr::companionai::TacticalCommandStateName(newState),
        framevr::companionai::TacticalCommandCancelReasonName(reason),
        static_cast<unsigned>(retries),
        unitId,
        animMode,
        compPos.x, compPos.y,
        targetPos.x, targetPos.y,
        dist,
        static_cast<unsigned long long>(targetRoom),
        static_cast<unsigned>(pathWords[8]),
        static_cast<unsigned>(pathWords[9]),
        static_cast<unsigned>(pathWords[10]),
        static_cast<unsigned>(pathWords[11]),
        static_cast<unsigned>(pathWords[26]));
    framevr::logging::Write(line);
}

void InvalidateTacticalContextOnMissingUnit(uintptr_t playerRoom) noexcept {
    ClearCachedSkillBadges();
    const auto nowMs = GetTickCount64();
    framevr::companionai::TacticalVoiceCue unusedCue = framevr::companionai::TacticalVoiceCue::None;
    framevr::companionai::TacticalCommandState prevState{};
    framevr::companionai::TacticalCommandState newState{};
    framevr::companionai::TacticalCommandCancelReason reason{};
    framevr::companionai::TacticalOrder prevOrder{};
    uint64_t cmdId = 0;
    uint8_t retries = 0;
    bool transitioned = false;
    {
        AcquireSRWLockExclusive(&g_tacticalLock);
        prevState = g_tacticalOrderController.GetState();
        prevOrder = g_tacticalOrderController.GetOrder();
        cmdId = g_tacticalOrderController.GetCommandId();
        retries = g_tacticalOrderController.GetRetryCount();
        if (g_tacticalOrderController.IsActive()) {
            (void)g_tacticalOrderController.ValidateRuntimeContext(0, playerRoom, 0, 0.0f, nowMs, unusedCue);
            newState = g_tacticalOrderController.GetState();
            reason = g_tacticalOrderController.GetLastCancelReason();
            transitioned = (prevState != newState);
        }
        ReleaseSRWLockExclusive(&g_tacticalLock);
    }
    g_activePeelTargetId.store(0, std::memory_order_relaxed);
    g_lastDispatchedTargetX.store(0, std::memory_order_relaxed);
    g_lastDispatchedTargetY.store(0, std::memory_order_relaxed);
    g_lastHirelingPosX.store(0.0f, std::memory_order_relaxed);
    g_lastHirelingPosY.store(0.0f, std::memory_order_relaxed);
    if (transitioned) {
        LogTacticalTransition(cmdId, prevOrder.type, prevState, newState, reason, retries, 0, 0, {}, prevOrder.targetPos, 0, 0);
    }
}

bool DispatchServerMoveToCoord(uintptr_t game, uintptr_t unit, framevr::companionai::Vec2 compPos, int targetX, int targetY) noexcept {
    if (!game || !unit) return false;
    if (!serverMoveToCoordFn && !originalServerMoveUnit) return false;

    struct CommandedMoveScope {
        CommandedMoveScope() noexcept { g_inCommandedMove = true; }
        ~CommandedMoveScope() noexcept { g_inCommandedMove = false; }
    } scope;

    const auto invokeMove = [&](int x, int y) noexcept -> int {
        if (serverMoveToCoordFn) {
            return serverMoveToCoordFn(game, unit, x, y);
        }
        return originalServerMoveUnit(game, unit, 0, 15, x, y, 1, 0);
    };

    if (invokeMove(targetX, targetY) != 0) {
        return true;
    }

    // If full-distance A* failed for a distant point (> 16 sub-tiles), step an intermediate 16-sub-tile waypoint
    const float dx = float(targetX) - compPos.x;
    const float dy = float(targetY) - compPos.y;
    const float dist = std::hypot(dx, dy);
    if (dist > 16.0f) {
        const int stepX = int(std::round(compPos.x + (dx / dist) * 16.0f));
        const int stepY = int(std::round(compPos.y + (dy / dist) * 16.0f));
        return invokeMove(stepX, stepY) != 0;
    }
    return false;
}

bool ShouldAllowNativeMoveStruct(uintptr_t game, uintptr_t moveStruct) noexcept {
    (void)game;
    if (g_inCommandedMove || g_inCommandedAction || !g_enabled.load(std::memory_order_relaxed) ||
        !g_tacticalCommandsEnabled.load(std::memory_order_relaxed) ||
        g_inRetreatState.load(std::memory_order_relaxed) || !moveStruct) {
        return true;
    }

    const uint32_t hirelingId = g_lastHirelingId.load(std::memory_order_relaxed);
    if (!hirelingId) return true;

    uintptr_t unit = 0;
    if (!Read(moveStruct + 0x08, unit) || !unit) return true;

    uint32_t header[4]{};
    if (!Read(unit, header) || header[0] != 1 || header[2] != hirelingId) {
        return true;
    }

    framevr::companionai::TacticalOrder order{};
    framevr::companionai::TacticalCommandState state = framevr::companionai::TacticalCommandState::Idle;
    bool active = false;
    float cx = 0.0f, cy = 0.0f;
    const bool hasPos = ReadPosition(unit, cx, cy);
    const auto arch = framevr::companionai::GetHirelingArchetype(uint16_t(header[1]));
    framevr::companionai::Vec2 effTarget{};
    {
        AcquireSRWLockShared(&g_tacticalLock);
        active = g_tacticalOrderController.IsActive();
        order = g_tacticalOrderController.GetOrder();
        state = g_tacticalOrderController.GetState();
        if (active) {
            effTarget = g_tacticalOrderController.ComputeEffectiveMoveTarget({cx, cy}, arch);
        }
        ReleaseSRWLockShared(&g_tacticalLock);
    }

    if (!active) return true;

    uintptr_t targetUnit = 0;
    int moveX = 0, moveY = 0;
    (void)Read(moveStruct + 0x10, targetUnit);
    (void)Read(moveStruct + 0x18, moveX);
    (void)Read(moveStruct + 0x1c, moveY);

    uint32_t targetHeader[4]{};
    const bool hasTargetUnit = (targetUnit != 0 && Read(targetUnit, targetHeader));
    const bool isTargetPlayer = (hasTargetUnit && targetHeader[0] == 0);

    float destX = float(moveX);
    float destY = float(moveY);
    if ((moveX <= 0 || moveY <= 0) && hasTargetUnit) {
        (void)ReadPosition(targetUnit, destX, destY);
    }
    if (destX <= 0.0f || destY <= 0.0f) {
        uintptr_t compPath = 0;
        uint16_t pathWords[32]{};
        if (Read(unit + 0x38, compPath) && compPath && Read(compPath, pathWords)) {
            if (pathWords[10] > 0 && pathWords[11] > 0) {
                destX = float(pathWords[10]);
                destY = float(pathWords[11]);
            }
        }
    }

    const float distToOrderTarget = hasPos ? std::hypot(cx - order.targetPos.x, cy - order.targetPos.y) : 0.0f;
    const float destDistToEffTarget = (destX > 0.0f && destY > 0.0f)
        ? std::hypot(destX - effTarget.x, destY - effTarget.y)
        : 9999.0f;

    float enemyX = 0.0f, enemyY = 0.0f;
    const bool hasEnemyPos = (hasTargetUnit && targetHeader[0] == 1 && ReadPosition(targetUnit, enemyX, enemyY));
    const float enemyDistToEff = hasEnemyPos ? std::hypot(enemyX - effTarget.x, enemyY - effTarget.y) : 9999.0f;
    const float compDistToEnemy = (hasPos && hasEnemyPos) ? std::hypot(cx - enemyX, cy - enemyY) : 9999.0f;
    const float destDistToEnemy = hasEnemyPos ? std::hypot(destX - enemyX, destY - enemyY) : 9999.0f;

    if (order.type == framevr::companionai::TacticalOrderType::DefensiveHold) {
        if (!hasPos || (order.targetPos.x == 0.0f && order.targetPos.y == 0.0f)) {
            return false;
        }
        if (distToOrderTarget <= framevr::companionai::TacticalOrderController::HoldReleaseDeadband) {
            if (isTargetPlayer) {
                static uint64_t s_lastMoveStructLogMs = 0;
                const uint64_t logNow = GetTickCount64();
                if (logNow - s_lastMoveStructLogMs >= 1000) {
                    s_lastMoveStructLogMs = logNow;
                    char logLine[180];
                    std::snprintf(logLine, sizeof logLine,
                        "Frame companion AI: ShouldAllowNativeMoveStruct blocked native return-to-player move");
                    framevr::logging::Write(logLine);
                }
                return false;
            }
            if (hasTargetUnit && targetHeader[0] == 1) {
                if (arch == framevr::companionai::Archetype::RangedOverwatch) {
                    if (enemyDistToEff <= framevr::companionai::TacticalOrderController::RangedHoldEngageRadius &&
                        destDistToEffTarget <= 3.5f) {
                        return true;
                    }
                    return false;
                }
                if (destDistToEffTarget <= framevr::companionai::TacticalOrderController::MeleeHoldEngageRadius) {
                    return true;
                }
            }
            return false;
        }
        if (destDistToEffTarget <= 3.0f) {
            return true;
        }
        g_nativeMoveBlockedWhileMoving.store(true, std::memory_order_relaxed);
        g_lastDispatchMs.store(0, std::memory_order_relaxed);
        return false;
    }

    if (order.type == framevr::companionai::TacticalOrderType::MoveAndHold) {
        if (state == framevr::companionai::TacticalCommandState::Arrived ||
            (hasPos && distToOrderTarget <= framevr::companionai::TacticalOrderController::ArrivalTolerance)) {
            if (!hasPos || distToOrderTarget <= framevr::companionai::TacticalOrderController::HoldReleaseDeadband) {
                if (isTargetPlayer) {
                    return false;
                }
                if (hasTargetUnit && targetHeader[0] == 1) {
                    if (arch == framevr::companionai::Archetype::RangedOverwatch) {
                        if (enemyDistToEff <= framevr::companionai::TacticalOrderController::RangedHoldEngageRadius &&
                            destDistToEffTarget <= 3.5f) {
                            return true;
                        }
                        return false;
                    }
                    if (destDistToEffTarget <= framevr::companionai::TacticalOrderController::MeleeHoldEngageRadius) {
                        return true;
                    }
                }
                return false;
            }
        }

        if (destDistToEffTarget <= 3.0f) {
            return true;
        }
        g_nativeMoveBlockedWhileMoving.store(true, std::memory_order_relaxed);
        g_lastDispatchMs.store(0, std::memory_order_relaxed);
        return false;
    }

    if (order.type == framevr::companionai::TacticalOrderType::PrioritizeEnemy) {
        if (isTargetPlayer) {
            if (state == framevr::companionai::TacticalCommandState::Moving) {
                g_nativeMoveBlockedWhileMoving.store(true, std::memory_order_relaxed);
                g_lastDispatchMs.store(0, std::memory_order_relaxed);
            }
            return false;
        }
        const float destDistToOrderEnemy = (destX > 0.0f && destY > 0.0f)
            ? std::hypot(destX - order.targetPos.x, destY - order.targetPos.y)
            : 9999.0f;
        if (arch == framevr::companionai::Archetype::RangedOverwatch && hasPos) {
            if (distToOrderTarget >= framevr::companionai::TacticalOrderController::RangedMinStandOff &&
                distToOrderTarget <= framevr::companionai::TacticalOrderController::RangedEngagementTolerance) {
                // Inside optimal firing window: hold stance! Suppress walking towards the enemy or follow-player
                return false;
            }
            if (destDistToEffTarget <= 4.0f ||
                (destDistToOrderEnemy >= framevr::companionai::TacticalOrderController::RangedMinStandOff &&
                 destDistToOrderEnemy <= framevr::companionai::TacticalOrderController::RangedEngagementTolerance)) {
                return true;
            }
            g_nativeMoveBlockedWhileMoving.store(true, std::memory_order_relaxed);
            g_lastDispatchMs.store(0, std::memory_order_relaxed);
            return false;
        }
        if (hasTargetUnit && targetHeader[0] == 1 && targetHeader[2] == order.targetUnitId) {
            return true;
        }
        if (destDistToEffTarget <= 4.0f || (hasPos && destDistToOrderEnemy < distToOrderTarget + 1.0f)) {
            return true;
        }
        g_nativeMoveBlockedWhileMoving.store(true, std::memory_order_relaxed);
        g_lastDispatchMs.store(0, std::memory_order_relaxed);
        return false;
    }

    return true;
}

int DetourServerStartWalk(uintptr_t game, uintptr_t moveStruct) {
    if (!ShouldAllowNativeMoveStruct(game, moveStruct)) {
        return 0;
    }
    return originalServerStartWalk ? originalServerStartWalk(game, moveStruct) : 0;
}

int DetourServerStartRun(uintptr_t game, uintptr_t moveStruct) {
    if (!ShouldAllowNativeMoveStruct(game, moveStruct)) {
        return 0;
    }
    return originalServerStartRun ? originalServerStartRun(game, moveStruct) : 0;
}

int DetourServerMoveUnit(uintptr_t game, uintptr_t unit, uintptr_t targetUnit, int moveMode, int x, int y, uint8_t pathType, uint16_t flags) {
    if (g_inCommandedMove || g_inCommandedAction || !g_enabled.load(std::memory_order_relaxed) ||
        !g_tacticalCommandsEnabled.load(std::memory_order_relaxed) ||
        g_inRetreatState.load(std::memory_order_relaxed)) {
        return originalServerMoveUnit(game, unit, targetUnit, moveMode, x, y, pathType, flags);
    }

    const uint32_t hirelingId = g_lastHirelingId.load(std::memory_order_relaxed);
    if (!hirelingId || !unit) {
        return originalServerMoveUnit(game, unit, targetUnit, moveMode, x, y, pathType, flags);
    }

    uint32_t header[4]{};
    if (!Read(unit, header) || header[0] != 1 || header[2] != hirelingId) {
        return originalServerMoveUnit(game, unit, targetUnit, moveMode, x, y, pathType, flags);
    }

    float cx = 0.0f, cy = 0.0f;
    const bool hasPos = ReadPosition(unit, cx, cy);
    const auto arch = framevr::companionai::GetHirelingArchetype(uint16_t(header[1]));

    framevr::companionai::TacticalOrder order{};
    static uint64_t s_lastMoveUnitLogMs = 0;
    const uint64_t logNow = GetTickCount64();
    if (logNow - s_lastMoveUnitLogMs >= 1000) {
        s_lastMoveUnitLogMs = logNow;
        char logLine[180];
        std::snprintf(logLine, sizeof logLine,
            "Frame companion AI: DetourServerMoveUnit hireling action (mode=%d targetUnit=%p pos=(%d,%d))",
            moveMode, reinterpret_cast<void*>(targetUnit), x, y);
        framevr::logging::Write(logLine);
    }
    framevr::companionai::TacticalCommandState state = framevr::companionai::TacticalCommandState::Idle;
    framevr::companionai::Vec2 effTarget{};
    bool active = false;
    {
        AcquireSRWLockShared(&g_tacticalLock);
        active = g_tacticalOrderController.IsActive();
        order = g_tacticalOrderController.GetOrder();
        state = g_tacticalOrderController.GetState();
        if (active) {
            effTarget = g_tacticalOrderController.ComputeEffectiveMoveTarget({cx, cy}, arch);
        }
        ReleaseSRWLockShared(&g_tacticalLock);
    }

    if (!active) {
        return originalServerMoveUnit(game, unit, targetUnit, moveMode, x, y, pathType, flags);
    }

    const int targetX = int(std::round(effTarget.x));
    const int targetY = int(std::round(effTarget.y));
    const float distToTarget = hasPos ? std::hypot(cx - order.targetPos.x, cy - order.targetPos.y) : 0.0f;

    const bool isCombatAction = (moveMode == 4 || moveMode == 5 || moveMode == 7 || (moveMode >= 8 && moveMode <= 11));

    // Helper: allow native AI to attack or cast skills on a hostile monster within the archetype engagement radius.
    const auto canAttackHostile = [&]() noexcept -> bool {
        if (!targetUnit || !isCombatAction) return false;
        uint32_t targetHeader[4]{};
        float tx = 0.0f, ty = 0.0f;
        if (!Read(targetUnit, targetHeader) || targetHeader[0] != 1 || !ReadPosition(targetUnit, tx, ty)) {
            return false;
        }
        const float targetDistToAnchor = std::hypot(tx - effTarget.x, ty - effTarget.y);
        const float maxRange = (arch == framevr::companionai::Archetype::RangedOverwatch)
            ? framevr::companionai::TacticalOrderController::RangedHoldEngageRadius
            : framevr::companionai::TacticalOrderController::MeleeHoldEngageRadius;
        return targetDistToAnchor <= maxRange;
    };

    // Helper: allow MeleeVanguard to step/walk toward a hostile monster inside MeleeHoldEngageRadius.
    // For RangedOverwatch, NEVER close into melee range - hold position at the anchor and engage from distance.
    const auto canStepTowardHostile = [&]() noexcept -> bool {
        if (!targetUnit || isCombatAction || arch != framevr::companionai::Archetype::MeleeVanguard) return false;
        uint32_t targetHeader[4]{};
        float tx = 0.0f, ty = 0.0f;
        if (!Read(targetUnit, targetHeader) || targetHeader[0] != 1 || !ReadPosition(targetUnit, tx, ty)) {
            return false;
        }
        const float targetDistToAnchor = std::hypot(tx - effTarget.x, ty - effTarget.y);
        return targetDistToAnchor <= framevr::companionai::TacticalOrderController::MeleeHoldEngageRadius;
    };

    if (order.type == framevr::companionai::TacticalOrderType::DefensiveHold) {
        if (!hasPos || (order.targetPos.x == 0.0f && order.targetPos.y == 0.0f) ||
            distToTarget <= framevr::companionai::TacticalOrderController::HoldReleaseDeadband) {
            if (canAttackHostile() || canStepTowardHostile()) {
                return originalServerMoveUnit(game, unit, targetUnit, moveMode, x, y, pathType, flags);
            }
            return 1;
        }
        return DispatchServerMoveToCoord(game, unit, {cx, cy}, targetX, targetY) ? 1 : 0;
    }

    if (order.type == framevr::companionai::TacticalOrderType::MoveAndHold) {
        if (state == framevr::companionai::TacticalCommandState::Arrived ||
            (hasPos && distToTarget <= framevr::companionai::TacticalOrderController::ArrivalTolerance)) {
            if (!hasPos || distToTarget <= framevr::companionai::TacticalOrderController::HoldReleaseDeadband) {
                if (canAttackHostile() || canStepTowardHostile()) {
                    return originalServerMoveUnit(game, unit, targetUnit, moveMode, x, y, pathType, flags);
                }
                return 1;
            }
        }

        // While Moving: if the mercenary is already actively walking toward (targetX, targetY), preserve the current path
        uintptr_t compPath = 0;
        uint16_t pathWords[32]{};
        if (header[3] == 2 && Read(unit + 0x38, compPath) && compPath && Read(compPath, pathWords)) {
            if (pathWords[26] > 0 &&
                std::abs(int(pathWords[10]) - targetX) <= 2 &&
                std::abs(int(pathWords[11]) - targetY) <= 2) {
                return 1;
            }
        }

        // Otherwise redirect native AI's move call toward the commanded destination
        return DispatchServerMoveToCoord(game, unit, {cx, cy}, targetX, targetY) ? 1 : 0;
    }

    if (order.type == framevr::companionai::TacticalOrderType::PrioritizeEnemy) {
        if (arch == framevr::companionai::Archetype::RangedOverwatch) {
            if (isCombatAction && targetUnit != 0) {
                uint32_t targetHeader[4]{};
                if (Read(targetUnit, targetHeader) && targetHeader[0] == 1) {
                    return originalServerMoveUnit(game, unit, targetUnit, moveMode, x, y, pathType, flags);
                }
            }
            if (hasPos &&
                distToTarget >= framevr::companionai::TacticalOrderController::RangedMinStandOff &&
                distToTarget <= framevr::companionai::TacticalOrderController::RangedEngagementTolerance) {
                // Inside optimal firing window: suppress walking towards the monster; hold firing stance!
                return 1;
            }
            if (targetX > 0 && targetY > 0) {
                return DispatchServerMoveToCoord(game, unit, {cx, cy}, targetX, targetY) ? 1 : 0;
            }
            return 1;
        }

        if (targetUnit != 0 && order.targetUnitId != 0) {
            uint32_t targetHeader[4]{};
            if (Read(targetUnit, targetHeader) && targetHeader[0] == 1 && targetHeader[2] == order.targetUnitId) {
                return originalServerMoveUnit(game, unit, targetUnit, moveMode, x, y, pathType, flags);
            }
        }
        if (order.targetUnitId != 0) {
            uintptr_t enemy = melee::ServerUnit(game, 1, order.targetUnitId);
            if (enemy) {
                return originalServerMoveUnit(game, unit, enemy, 15, 0, 0, 1, 0);
            }
        }
        if (targetX > 0 && targetY > 0) {
            return DispatchServerMoveToCoord(game, unit, {cx, cy}, targetX, targetY) ? 1 : 0;
        }
    }

    return originalServerMoveUnit(game, unit, targetUnit, moveMode, x, y, pathType, flags);
}

int DetourSkillLevel(uintptr_t unit, uintptr_t skillNode, int bIncludeBonuses, int unk) {
    if (!originalSkillLevel) return 0;
    const int baseLevel = originalSkillLevel(unit, skillNode, bIncludeBonuses, unk);

    if (!g_companionItemSkillsEnabled.load(std::memory_order_relaxed)) {
        return baseLevel;
    }
    if (!bIncludeBonuses || !unit || !skillNode) {
        return baseLevel;
    }
    if (mode && mode() != 0) {
        return baseLevel;
    }

    uint32_t header[3]{};
    if (!Read(unit, header) || header[0] != 1) { // dwType == 1 (UNIT_MONSTER)
        return baseLevel;
    }

    const uint16_t classId = static_cast<uint16_t>(header[1]);
    const auto vclass = framevr::companionai::GetVirtualPlayerClass(classId);
    if (vclass == framevr::companionai::VirtualPlayerClass::None) {
        return baseLevel;
    }

    uintptr_t row = 0;
    int16_t skillId = 0;
    if (!Read(skillNode, row) || !Read(row, skillId) || skillId <= 0) {
        return baseLevel;
    }

    const auto invFn = reinterpret_cast<InventoryFn>(d2rsig::Addr(abi::InventoryRva));
    const auto eqFn = reinterpret_cast<EquippedFn>(d2rsig::Addr(abi::EquippedRva));
    const auto statFn = reinterpret_cast<GetStatFn>(d2rsig::Addr(abi::GetStatRva));
    if (!invFn || !eqFn || !statFn) {
        return baseLevel;
    }

    const uintptr_t inv = invFn(unit, nullptr, 0);
    if (!inv) {
        return baseLevel;
    }

    std::array<uintptr_t, 4> items{};
    size_t count = 0;
    for (int loc : {1, 3, 4, 5}) {
        uintptr_t item = eqFn(inv, loc);
        uint32_t ih[3]{};
        if (item && Read(item, ih) && ih[0] == 4) { // dwType == 4 (UNIT_ITEM)
            items[count++] = item;
        }
    }

    if (count == 0) {
        return baseLevel;
    }

    return framevr::companionai::ComputeEffectiveCompanionSkillLevel(
        baseLevel,
        vclass,
        skillId,
        std::span<const uintptr_t>(items.data(), count),
        [&](uintptr_t itm, int st, int pr) {
            return statFn(itm, st, pr);
        });
}

void EnsureSkillLevelInstalled() noexcept {
    if (!g_companionItemSkillsEnabled.load(std::memory_order_relaxed)) return;
    if (g_skillLevelHooked.load(std::memory_order_relaxed)) return;

    if (d2rsig::Check(abi::SkillLevelRva, abi::SkillLevelSig, sizeof(abi::SkillLevelSig)) &&
        d2rsig::Check(abi::InventoryRva, abi::InventorySig, sizeof(abi::InventorySig)) &&
        d2rsig::Check(abi::EquippedRva, abi::EquippedSig, sizeof(abi::EquippedSig)) &&
        d2rsig::Check(abi::GetStatRva, abi::GetStatSig, sizeof(abi::GetStatSig))) {
        if (d2rsig::Hook(
                abi::SkillLevelRva,
                abi::SkillLevelSig,
                sizeof(abi::SkillLevelSig),
                reinterpret_cast<void*>(&DetourSkillLevel),
                reinterpret_cast<void**>(&originalSkillLevel))) {
            g_skillLevelHooked.store(true, std::memory_order_relaxed);
            framevr::logging::Write("Frame companion AI: verified and hooked native skill level (0x33d1e0) for companion class bonuses");
        }
    }
}

struct alignas(16) SyntheticCompanionSkillNode {
    uintptr_t row = 0;          // 0x00: pSkillsTxt
    uintptr_t next = 0;         // 0x08: pNext
    uint32_t flags = 1;         // 0x10: dwFlags
    uint32_t pad14 = 0;         // 0x14
    uint32_t pad18 = 0;         // 0x18
    uint32_t pad1c = 0;         // 0x1c
    uint32_t pad20 = 0;         // 0x20
    int32_t hitCount = 1;       // 0x24: NodeHitsOffset
    uint32_t pad28 = 0;         // 0x28
    int32_t targetType = 1;     // 0x2c: NodeTargetTypeOffset (1 = monster)
    int32_t targetId = 0;       // 0x30: NodeTargetIdOffset
    uint32_t pad34 = 0;         // 0x34
    uint64_t pad38 = 0;         // 0x38
    int32_t level = 1;          // 0x40: dwSkillLevel
    int32_t bonus = 0;          // 0x44: dwBonus
    int32_t base = 1;           // 0x48: dwBaseLevel
    int32_t owner = -1;         // 0x4c: dwOwner (-1 = universal/item)
    uint8_t extra[128]{};       // generous padding up to 256 bytes
};
static_assert(offsetof(SyntheticCompanionSkillNode, hitCount) == 0x24);
static_assert(offsetof(SyntheticCompanionSkillNode, targetType) == 0x2c);
static_assert(offsetof(SyntheticCompanionSkillNode, targetId) == 0x30);
static_assert(offsetof(SyntheticCompanionSkillNode, level) == 0x40);
static_assert(offsetof(SyntheticCompanionSkillNode, bonus) == 0x44);
static_assert(offsetof(SyntheticCompanionSkillNode, base) == 0x48);
static_assert(offsetof(SyntheticCompanionSkillNode, owner) == 0x4c);

static SyntheticCompanionSkillNode s_companionSyntheticNode{};
using SkillLookupFn = uintptr_t(*)(uintptr_t unit, int skillId, int owner);

void EnsureServerMoveInstalled() noexcept {
    if (!serverMoveToCoordFn && d2rsig::Check(abi::ServerMoveToCoordRva, abi::ServerMoveToCoordSig, sizeof(abi::ServerMoveToCoordSig))) {
        serverMoveToCoordFn = reinterpret_cast<ServerMoveToCoordFn>(d2rsig::Addr(abi::ServerMoveToCoordRva));
    }
    if (!g_serverMoveHooked.load(std::memory_order_relaxed) &&
        d2rsig::Check(abi::ServerMoveUnitRva, abi::ServerMoveUnitSig, sizeof(abi::ServerMoveUnitSig))) {
        if (d2rsig::Hook(
                abi::ServerMoveUnitRva,
                abi::ServerMoveUnitSig,
                sizeof(abi::ServerMoveUnitSig),
                reinterpret_cast<void*>(&DetourServerMoveUnit),
                reinterpret_cast<void**>(&originalServerMoveUnit))) {
            g_serverMoveHooked.store(true, std::memory_order_relaxed);
            framevr::logging::Write("Frame companion AI: verified and hooked native server move entry points (0x4a77e0, 0x4a8a10)");
        }
    }
    if (!g_serverStartWalkHooked.load(std::memory_order_relaxed) &&
        d2rsig::Check(abi::ServerStartWalkRva, abi::ServerStartWalkSig, sizeof(abi::ServerStartWalkSig))) {
        if (d2rsig::Hook(
                abi::ServerStartWalkRva,
                abi::ServerStartWalkSig,
                sizeof(abi::ServerStartWalkSig),
                reinterpret_cast<void*>(&DetourServerStartWalk),
                reinterpret_cast<void**>(&originalServerStartWalk))) {
            g_serverStartWalkHooked.store(true, std::memory_order_relaxed);
            framevr::logging::Write("Frame companion AI: verified and hooked native server start-walk gatekeeper (0x445cc0)");
        }
    }
    if (!g_serverStartRunHooked.load(std::memory_order_relaxed) &&
        d2rsig::Check(abi::ServerStartRunRva, abi::ServerStartRunSig, sizeof(abi::ServerStartRunSig))) {
        if (d2rsig::Hook(
                abi::ServerStartRunRva,
                abi::ServerStartRunSig,
                sizeof(abi::ServerStartRunSig),
                reinterpret_cast<void*>(&DetourServerStartRun),
                reinterpret_cast<void**>(&originalServerStartRun))) {
            g_serverStartRunHooked.store(true, std::memory_order_relaxed);
            framevr::logging::Write("Frame companion AI: verified and hooked native server start-run gatekeeper (0x445db0)");
        }
    }
}

void ExecuteCompanionTacticalChargesInternal(
    uintptr_t game,
    uintptr_t hireling,
    uint32_t classId,
    float health,
    float maxHealth,
    const framevr::companionai::Vec2& compPos,
    const framevr::companionai::Vec2& plyrPos,
    uint64_t nowMs,
    bool inRetreat,
    bool isTacticalActive)
{
    if (!g_companionItemSkillsEnabled.load(std::memory_order_relaxed)) {
        return;
    }

    uint32_t compHeader[4]{};
    if (!Read(hireling, compHeader) || compHeader[0] != 1 || health <= 0.0f) {
        return;
    }

    const auto vclass = framevr::companionai::GetVirtualPlayerClass(static_cast<uint16_t>(classId));
    if (vclass == framevr::companionai::VirtualPlayerClass::None) {
        return;
    }

    static uint64_t s_lastDiagScanLogMs = 0;

    const auto invFn = reinterpret_cast<InventoryFn>(d2rsig::Addr(abi::InventoryRva));
    const auto eqFn = reinterpret_cast<EquippedFn>(d2rsig::Addr(abi::EquippedRva));
    const auto statFn = reinterpret_cast<GetStatFn>(d2rsig::Addr(abi::GetStatRva));
    if (!invFn || !eqFn || !statFn) {
        return;
    }

    const uintptr_t inv = invFn(hireling, nullptr, 0);
    if (!inv) {
        return;
    }

    // Inspect current equipped item IDs (sub-microsecond memory read)
    std::array<framevr::companionai::EquippedItemSlot, 4> slots{};
    std::array<uint32_t, 4> currentItemIds{};
    size_t slotCount = 0;
    for (int loc : {1, 3, 4, 5}) {
        uintptr_t item = eqFn(inv, loc);
        uint32_t ih[3]{};
        if (item && Read(item, ih) && ih[0] == 4) {
            currentItemIds[slotCount] = ih[2];
            slots[slotCount++] = { ih[2], loc, item };
        }
    }

    const bool hirelingChanged = (s_lastHirelingUnitId != compHeader[2]);
    const bool itemsChanged = (slotCount != s_lastSlotCount || currentItemIds != s_lastItemIds);
    const bool cacheEmpty = s_cachedBadges.empty() && slotCount > 0;

    // ZERO OVERHEAD: Only perform probe if the mercenary or equipped item IDs actually changed,
    // or if the cached badges are empty after a revival/respawn!
    if (hirelingChanged || itemsChanged || cacheEmpty) {
        s_lastHirelingUnitId = compHeader[2];
        s_lastSlotCount = slotCount;
        s_lastItemIds = currentItemIds;

        auto prevCharges = std::move(s_cachedCharges);
        s_cachedCharges.clear();

        // Verified candidate charged skills in Diablo II (expanded for all classes, items, and assassin traps)
        static constexpr uint16_t CandidateSkills[] = {
            54,  // Teleport
            7, 9, 264, // Inner Sight, Slow Missiles, Cloak of Shadows
            66, 69, 71, 72, 76, 77, 81, 82, 86, 87, 91, 137, 146, // Curses
            43, 44, 45, 55, 59, 78, 88, 89, 116, 130, 139, 150, 154, 240, 253, 273, // Crowd Control
            17, 32, 75, 85, 90, 94, 95, 221, 222, 226, 227, 232, 236, 237, 242, 246, 247, 268, 279, // Summons
            6, 10, 11, 12, 14, 16, 19, 20, 21, 22, 24, 25, 26, 27, 29, 30, 31, 34, 35, // Amazon attacks & arrows
            36, 38, 39, 41, 42, 46, 47, 48, 49, 51, 53, 56, 62, 64, 67, 73, 74, 83, 84, 92, 93, // Sorc & Necro damage
            96, 101, 111, 112, 121, 126, 140, 152, // Paladin & Barbarian attacks
            223, 225, 229, 234, 244, 245, 249, 250, // Druid elemental
            251, 254, 256, 257, 259, 261, 262, 265, 266, 269, 271, 272, 274, 276, 280, // Assassin traps & martial arts
            40, 50, 52, 57, 58, 60, 68, 138, 149, 155, 235, 258, 267, 277, 278 // Buffs
        };

        for (size_t sIdx = 0; sIdx < slotCount; ++sIdx) {
            const auto& slot = slots[sIdx];

            auto processHit = [&](uint16_t p, int32_t statVal) {
                char hitBuf[256];
                std::snprintf(hitBuf, sizeof hitBuf,
                    "Frame companion AI: [STAT 204 ITEM HIT] loc=%d itemId=%u param=%u (0x%04X) val=%d (0x%08X)",
                    slot.bodyLocation, slot.itemId, p, p, statVal, statVal);
                framevr::logging::Write(hitBuf);

                const uint8_t cur = static_cast<uint8_t>(statVal & 0xFF);
                const uint8_t maxC = static_cast<uint8_t>((statVal >> 8) & 0xFF);

                // D2R Native stat 204 encoding: Bits 6..15 = Skill ID (10 bits), Bits 0..5 = Level (6 bits)
                const uint16_t s4 = static_cast<uint16_t>((p >> 6) & 0x3FF);
                const uint8_t l4 = static_cast<uint8_t>(p & 0x3F);

                uint16_t skillId = 0;
                uint8_t lvl = 1;
                if (s4 > 0 && s4 < 600 && l4 > 0 && l4 <= 60) {
                    skillId = s4;
                    lvl = l4;
                } else {
                    const uint16_t sA = static_cast<uint16_t>((p & 0xFF) | (((p >> 14) & 0x03) << 8));
                    const uint8_t lA = static_cast<uint8_t>((p >> 8) & 0x3F);
                    if (sA > 0 && sA < 600 && lA > 0 && lA <= 40) { skillId = sA; lvl = lA; }
                    else { skillId = s4 ? s4 : (p & 0xFF); lvl = l4 ? l4 : 1; }
                }

                if (skillId > 0 && maxC > 0 && framevr::companionai::IsSkillUsableByCompanion(vclass, skillId)) {
                    bool already = false;
                    for (const auto& cs : s_cachedCharges) {
                        if (cs.itemId == slot.itemId && cs.skillId == skillId) { already = true; break; }
                    }
                    if (!already) {
                        framevr::companionai::CompanionChargedSkill cSkillObj{};
                        cSkillObj.itemId = slot.itemId;
                        cSkillObj.bodyLocation = slot.bodyLocation;
                        cSkillObj.skillId = skillId;
                        cSkillObj.level = lvl;
                        cSkillObj.currentCharges = cur;
                        cSkillObj.maxCharges = maxC;
                        cSkillObj.kind = framevr::companionai::GetTacticalSkillKind(skillId);
                        for (const auto& prev : prevCharges) {
                            if (prev.itemId == cSkillObj.itemId && prev.skillId == cSkillObj.skillId) {
                                cSkillObj.lastCastMs = prev.lastCastMs;
                                break;
                            }
                        }
                        s_cachedCharges.push_back(cSkillObj);
                    }
                }
            };

            // Targeted candidate skill probe (Levels 1..25 and 30,33 with early exit per skill)
            for (uint16_t cSkill : CandidateSkills) {
                if (!framevr::companionai::IsSkillUsableByCompanion(vclass, cSkill)) {
                    continue;
                }

                // Direct skill
                int32_t v = statFn(slot.itemUnit, framevr::companionai::ItemStat::ChargedSkill, cSkill);
                if (v > 0) processHit(cSkill, v);

                bool foundLevel = false;
                for (uint8_t lvl = 1; lvl <= 25; ++lvl) {
                    const uint16_t pStd = framevr::companionai::PackChargedSkillParam(cSkill, lvl);
                    v = statFn(slot.itemUnit, framevr::companionai::ItemStat::ChargedSkill, pStd);
                    if (v > 0) {
                        processHit(pStd, v);
                        foundLevel = true;
                        break;
                    }
                }
                if (!foundLevel) {
                    for (uint8_t lvl : {30, 33}) {
                        const uint16_t pStd = framevr::companionai::PackChargedSkillParam(cSkill, lvl);
                        v = statFn(slot.itemUnit, framevr::companionai::ItemStat::ChargedSkill, pStd);
                        if (v > 0) {
                            processHit(pStd, v);
                            break;
                        }
                    }
                }
            }
        }

        // Update live mercenary UI badges (passing preDiscoveredCharges to avoid re-scanning)
        auto newBadges = framevr::companionui::CollectMercenarySkillBadges(
            vclass,
            std::span<const framevr::companionai::EquippedItemSlot>(slots.data(), slotCount),
            [&](uintptr_t itm, int st, int pr) {
                return statFn(itm, st, pr);
            },
            [](int id) {
                return nativeui::SkillName(id);
            },
            s_cachedCharges
        );

        // Integrate active non-passive equipment skills (VirtualClassSkill STAT 107 & Oskill STAT 97)
        // into tactical casting rotation with unlimited charges and natural tactical cooldowns
        for (const auto& b : newBadges) {
            if (b.type != framevr::companionui::SkillBadgeType::VirtualClassSkill &&
                b.type != framevr::companionui::SkillBadgeType::Oskill) {
                continue;
            }
            if (framevr::companionai::IsSkillPassive(b.skillId) ||
                !framevr::companionai::IsSkillUsableByCompanion(vclass, b.skillId)) {
                continue;
            }

            bool already = false;
            for (const auto& cs : s_cachedCharges) {
                if (cs.skillId == b.skillId) {
                    already = true;
                    break;
                }
            }
            if (!already) {
                framevr::companionai::CompanionChargedSkill eqSkill{};
                eqSkill.itemId = b.itemId;
                eqSkill.bodyLocation = b.bodyLocation;
                eqSkill.skillId = b.skillId;
                eqSkill.level = b.level > 0 ? b.level : 1;
                eqSkill.currentCharges = 255;
                eqSkill.maxCharges = 0;
                eqSkill.isEquipmentSkill = true;
                eqSkill.kind = framevr::companionai::GetTacticalSkillKind(b.skillId);

                // Preserve previous cast timestamp across scans
                for (const auto& prev : prevCharges) {
                    if (prev.itemId == eqSkill.itemId && prev.skillId == eqSkill.skillId) {
                        eqSkill.lastCastMs = prev.lastCastMs;
                        break;
                    }
                }
                s_cachedCharges.push_back(eqSkill);
            }
        }

        AcquireSRWLockExclusive(&s_badgesLock);
        s_cachedBadges = std::move(newBadges);
        s_badgesRevision.fetch_add(1, std::memory_order_release);
        ReleaseSRWLockExclusive(&s_badgesLock);

        static size_t s_lastLoggedChargeCount = 999;
        if (s_cachedCharges.size() != s_lastLoggedChargeCount || nowMs - s_lastDiagScanLogMs > 10000) {
            s_lastLoggedChargeCount = s_cachedCharges.size();
            s_lastDiagScanLogMs = nowMs;

            char scanBuf[256];
            std::snprintf(scanBuf, sizeof scanBuf,
                "Frame companion AI: scanned equipped items (%zu slots), detected %zu tactical skill(s)",
                slotCount, s_cachedCharges.size());
            framevr::logging::Write(scanBuf);

            for (const auto& cs : s_cachedCharges) {
                char skillBuf[256];
                std::snprintf(skillBuf, sizeof skillBuf,
                    "Frame companion AI:   skillId=%u lvl=%u (%s, %u/%u) kind=%u loc=%d",
                    cs.skillId, cs.level,
                    cs.isEquipmentSkill ? "unlimited equipment" : "item charges",
                    cs.currentCharges, cs.maxCharges, static_cast<unsigned>(cs.kind), cs.bodyLocation);
                framevr::logging::Write(skillBuf);
            }
        }
    }
    if (s_cachedCharges.empty()) {
        return;
    }

    // Gate actual skill execution behind animation readiness and casting interval
    const uint32_t hirelingMode = compHeader[3];
    const bool inActiveAttack = (hirelingMode == 4 || hirelingMode == 5 || hirelingMode == 7 || (hirelingMode >= 8 && hirelingMode <= 11));
    if (inActiveAttack || hirelingMode == 3 /* MONMODE_GETHIT */) {
        return;
    }

    if (nowMs - s_lastTacticalCastMs < 400) {
        return;
    }

    framevr::companionai::TacticalCombatSensors sensors{};
    sensors.companionHealthPct = (maxHealth > 0.0f) ? (health / maxHealth) : 1.0f;
    sensors.nowMs = nowMs;

    melee::Hostile hostiles[16]{};
    const int hCount = melee::HostilesAround(hostiles, 16, 24.0f, compPos.x, compPos.y, hireling);
    float primaryTargetX = compPos.x;
    float primaryTargetY = compPos.y;
    uint32_t primaryTargetId = 0;

    if (hCount > 0) {
        sensors.isCombatActive = true;
        float closestDist = 9999.0f;
        for (int i = 0; i < hCount; ++i) {
            const float d = std::hypot(hostiles[i].x - compPos.x, hostiles[i].y - compPos.y);
            if (d <= 2.5f) sensors.meleeAttackersCount++;
            if (d <= 18.0f) sensors.nearbyHostilesCount++;
            if (d > 12.0f) sensors.rangedShootersCount++;

            // Boss / Elite detection
            if (!sensors.hasActiveBoss) {
                const uint32_t clsId = hostiles[i].classId;
                if (clsId == 156 || clsId == 211 || clsId == 242 || clsId == 243 || clsId == 544) {
                    sensors.hasActiveBoss = true;
                } else if (game && hostiles[i].id) {
                    uintptr_t u = melee::ServerUnit(game, 1, hostiles[i].id);
                    uintptr_t mdata = 0;
                    if (u && Read(u + 0x10, mdata) && mdata) {
                        uint8_t spec = 0;
                        if (Read(mdata + 0x1a, spec) && (spec & (0x02 | 0x04 | 0x08))) {
                            sensors.hasActiveBoss = true;
                        }
                        uintptr_t mrow = 0; uint32_t mflags = 0;
                        if (Read(mdata, mrow) && mrow && Read(mrow + 0x3c, mflags) && (mflags & (1u << 7))) {
                            sensors.hasActiveBoss = true;
                        }
                    }
                }
            }

            if (hostiles[i].clear && d < closestDist) {
                closestDist = d;
                primaryTargetX = hostiles[i].x;
                primaryTargetY = hostiles[i].y;
                primaryTargetId = hostiles[i].id;
            }
        }
        if (closestDist >= 9998.0f && hCount > 0) {
            for (int i = 0; i < hCount; ++i) {
                const float d = std::hypot(hostiles[i].x - compPos.x, hostiles[i].y - compPos.y);
                if (d < closestDist) {
                    closestDist = d;
                    primaryTargetX = hostiles[i].x;
                    primaryTargetY = hostiles[i].y;
                    primaryTargetId = hostiles[i].id;
                }
            }
        }
    }

    const uint32_t holdTargetId = g_activeHoldTargetId.load(std::memory_order_relaxed);
    if (holdTargetId != 0) {
        uintptr_t enemy = melee::ServerUnit(game, 1, holdTargetId);
        float ex = 0.0f, ey = 0.0f;
        if (enemy && ReadPosition(enemy, ex, ey)) {
            primaryTargetX = ex;
            primaryTargetY = ey;
            primaryTargetId = holdTargetId;
            sensors.isCombatActive = true;
        }
    }

    if (inRetreat) {
        sensors.isCombatActive = true;
    }

    melee::Corpse corpses[16]{};
    int corpseCount = 0;
    if (framevr::companionai::HasCorpseSkills(s_cachedCharges)) {
        corpseCount = melee::CorpsesAround(corpses, 16, 24.0f, compPos.x, compPos.y, hireling);
    }
    if (corpseCount > 0) {
        sensors.nearbyCorpsesCount = corpseCount;
        int bestCIdx = 0;
        float bestCDist = 9999.0f;
        const float refX = (hCount > 0) ? primaryTargetX : compPos.x;
        const float refY = (hCount > 0) ? primaryTargetY : compPos.y;
        for (int c = 0; c < corpseCount; ++c) {
            const float d = std::hypot(corpses[c].x - refX, corpses[c].y - refY);
            if (d < bestCDist) {
                bestCDist = d;
                bestCIdx = c;
            }
        }
        sensors.closestCorpseX = corpses[bestCIdx].x;
        sensors.closestCorpseY = corpses[bestCIdx].y;
        sensors.closestCorpseId = corpses[bestCIdx].id;
        sensors.closestCorpseUnit = corpses[bestCIdx].unit;
    }

    const bool consumeCharges = g_companionConsumeItemCharges.load(std::memory_order_relaxed);
    const auto decision = framevr::companionai::SelectBestTacticalCharge(
        s_cachedCharges, sensors, true, consumeCharges);

    if (!decision.shouldCast) {
        static uint64_t s_lastNoCastLogMs = 0;
        if (sensors.isCombatActive && nowMs - s_lastNoCastLogMs > 5000) {
            s_lastNoCastLogMs = nowMs;
            char noCastBuf[256];
            std::snprintf(noCastBuf, sizeof noCastBuf,
                "Frame companion AI: tactical combat active (hostiles=%d boss=%d), %zu charge(s) available but gated by trigger/cooldown",
                sensors.nearbyHostilesCount, sensors.hasActiveBoss ? 1 : 0, s_cachedCharges.size());
            framevr::logging::Write(noCastBuf);
        }
        return;
    }

    int destX = int(std::round(primaryTargetX));
    int destY = int(std::round(primaryTargetY));

    if (decision.kind == framevr::companionai::TacticalSkillKind::Teleport) {
        destX = int(std::round(plyrPos.x));
        destY = int(std::round(plyrPos.y));
    } else if (framevr::companionai::SkillRequiresCorpse(decision.skillId)) {
        destX = int(std::round(sensors.closestCorpseX));
        destY = int(std::round(sensors.closestCorpseY));
    } else if (decision.kind == framevr::companionai::TacticalSkillKind::Suppression ||
               decision.kind == framevr::companionai::TacticalSkillKind::Summon ||
               decision.kind == framevr::companionai::TacticalSkillKind::Buff) {
        destX = int(std::round(compPos.x));
        destY = int(std::round(compPos.y));
    }

    const auto startFn = reinterpret_cast<int(__fastcall*)(uintptr_t, uintptr_t, int, int, int)>(d2rsig::Addr(abi::StartRva));
    const auto dispatchFn = reinterpret_cast<int(__fastcall*)(uintptr_t, uintptr_t, int, int, int, int, int)>(d2rsig::Addr(abi::DispatchRva));

    if (startFn && dispatchFn) {
        // Resolve skill node: check native hireling skill list first (charges / native skills)
        const auto lookupFn = reinterpret_cast<SkillLookupFn>(d2rsig::Addr(abi::SkillRva));
        uintptr_t activeSkillNode = lookupFn ? lookupFn(hireling, decision.skillId, -1) : 0;

        // If the skill is not in the hireling's native list (vanilla D2R ITEM_EquipItem skips
        // inserting STAT 107 single skills for non-player units), construct synthetic node
        // pointing to the compiled in-memory Skills.txt row.
        if (!activeSkillNode && decision.isEquipmentSkill) {
            const void* skillRow = nativeui::GetSkillRow(decision.skillId);
            if (skillRow) {
                s_companionSyntheticNode.row = reinterpret_cast<uintptr_t>(skillRow);
                s_companionSyntheticNode.next = 0;
                s_companionSyntheticNode.flags = 1;
                s_companionSyntheticNode.hitCount = 1;
                s_companionSyntheticNode.targetType = 1;
                s_companionSyntheticNode.targetId = primaryTargetId;
                s_companionSyntheticNode.level = decision.level > 0 ? decision.level : 1;
                s_companionSyntheticNode.bonus = 0;
                s_companionSyntheticNode.base = decision.level > 0 ? decision.level : 1;
                s_companionSyntheticNode.owner = -1;
                activeSkillNode = reinterpret_cast<uintptr_t>(&s_companionSyntheticNode);
            }
        }

        // Temporarily supply THIS skill node to the hireling's used skill slot (list + 0x18),
        // exactly matching how spells.cpp and melee.cpp prepare the caster for StartRva and DispatchRva
        struct SelectionScope {
            uintptr_t address = 0, previous = 0;
            bool changed = false;
            SelectionScope(uintptr_t unit, uintptr_t node) {
                uintptr_t list = 0;
                if (node && unit && Read(unit + 0x100, list) && list >= 0x10000 && Read(list + 0x18, previous)) {
                    address = list + 0x18;
                    changed = framevr::WriteMemory(reinterpret_cast<void*>(address), &node, sizeof(node));
                }
            }
            ~SelectionScope() {
                if (changed) framevr::WriteMemory(reinterpret_cast<void*>(address), &previous, sizeof(previous));
            }
        };
        SelectionScope selectionScope(hireling, activeSkillNode);

        uintptr_t targetUnit = 0;
        if (framevr::companionai::SkillRequiresCorpse(decision.skillId)) {
            targetUnit = sensors.closestCorpseUnit;
        } else if (primaryTargetId != 0 && game) {
            targetUnit = melee::ServerUnit(game, 1, primaryTargetId);
        }

        struct CommandedActionScope {
            CommandedActionScope(uintptr_t target = 0) noexcept { g_inCommandedAction = true; g_commandedActionTargetUnit = target; }
            ~CommandedActionScope() noexcept { g_inCommandedAction = false; g_commandedActionTargetUnit = 0; }
        };
        CommandedActionScope actionScope(targetUnit);

        const int arrowCount = (decision.skillId == 26)
            ? std::clamp(static_cast<int>(decision.level) + 4, 4, 10)
            : 1;

        const int startRes = startFn(game, hireling, decision.skillId, decision.level, 0);

        int castRes = 0;
        int successfulDispatches = 0;
        for (int a = 0; a < arrowCount; ++a) {
            uint32_t arrowTargetId = primaryTargetId;
            int arrowDestX = destX;
            int arrowDestY = destY;

            if (hCount > 0) {
                const auto& h = hostiles[a % hCount];
                arrowTargetId = h.id;
                arrowDestX = int(std::round(h.x));
                arrowDestY = int(std::round(h.y));
            }

            s_companionSyntheticNode.targetId = arrowTargetId;
            s_companionSyntheticNode.hitCount = arrowCount - a;

            const int res = dispatchFn(game, hireling, decision.skillId, decision.level, 1, arrowDestX, arrowDestY);
            if (res != 0) {
                successfulDispatches++;
                castRes = res;
            }
        }

        char logBuf[256];
        std::snprintf(logBuf, sizeof logBuf,
            "Frame companion AI: tactical cast skill %u (lvl %u, %s, %d arrows/hits) -> dest (%d,%d) startRes=%d castRes=%d",
            decision.skillId, decision.level, decision.reason, arrowCount, destX, destY, startRes, successfulDispatches);
        framevr::logging::Write(logBuf);

        if (castRes != 0 || startRes != 0) {
            s_lastTacticalCastMs = nowMs;
            for (auto& s : s_cachedCharges) {
                if (s.itemId == decision.itemId && s.skillId == decision.skillId) {
                    s.lastCastMs = nowMs;
                    if (consumeCharges && !s.isEquipmentSkill && s.maxCharges > 0) {
                        framevr::companionai::DecrementChargedSkill(s, true);
                        const auto setStatFn = reinterpret_cast<void(__fastcall*)(uintptr_t, int, int, int)>(d2rsig::Addr(abi::SetStatRva));
                        if (setStatFn) {
                            const uint16_t param = framevr::companionai::PackChargedSkillParam(s.skillId, s.level);
                            const int32_t val = framevr::companionai::PackChargedSkillValue(s.currentCharges, s.maxCharges);
                            uintptr_t itmUnit = eqFn(inv, decision.bodyLocation);
                            if (itmUnit) {
                                setStatFn(itmUnit, framevr::companionai::ItemStat::ChargedSkill, val, param);
                            }
                        }
                        AcquireSRWLockExclusive(&s_badgesLock);
                        for (auto& b : s_cachedBadges) {
                            if (b.itemId == s.itemId && b.skillId == s.skillId && b.type == framevr::companionui::SkillBadgeType::ChargedSkill) {
                                b.currentCharges = s.currentCharges;
                                break;
                            }
                        }
                        s_badgesRevision.fetch_add(1, std::memory_order_release);
                        ReleaseSRWLockExclusive(&s_badgesLock);
                    }
                    break;
                }
            }
        } else {
            // Failed cast backoff: apply cooldown penalty so the AI does not spam attempts every frame
            s_lastTacticalCastMs = nowMs;
            for (auto& s : s_cachedCharges) {
                if (s.itemId == decision.itemId && s.skillId == decision.skillId) {
                    s.lastCastMs = nowMs;
                    break;
                }
            }
        }
    }
}

void __cdecl CompanionGameThreadTick(const PluginContext*, void*) noexcept {
    g_gameThreadPending.store(false);
    if (g_watchdogResetRequested.exchange(false, std::memory_order_relaxed)) {
        g_watchdog.Reset();
    }

    EnsureServerMoveInstalled();
    EnsureSkillLevelInstalled();
    uintptr_t game = 0, player = 0;
    if (!melee::LocalServer(game, player)) {
        InvalidateTacticalContextOnMissingUnit(0);
        return;
    }

    // 1. Local player position and room
    float px = 0.0f, py = 0.0f;
    if (!ReadPosition(player, px, py)) return;
    g_lastPlayerPosX.store(px, std::memory_order_relaxed);
    g_lastPlayerPosY.store(py, std::memory_order_relaxed);

    uintptr_t playerPath = 0, playerRoom = 0;
    if (!Read(player + 0x38, playerPath) || !Read(playerPath + 0x20, playerRoom) || playerRoom < 0x10000) {
        g_lastPlayerRoom.store(0, std::memory_order_relaxed);
        InvalidateTacticalContextOnMissingUnit(0);
        return;
    }
    g_lastPlayerRoom.store(playerRoom, std::memory_order_relaxed);

    // 2. Query hireling vitals
    float health = 0.0f, maxHealth = 0.0f;
    uint32_t classId = 0, unitId = 0;
    if (!melee::HirelingVitals(g_lastHirelingId.load(), health, maxHealth, classId, unitId) || !unitId) {
        g_watchdog.Reset();
        InvalidateTacticalContextOnMissingUnit(playerRoom);
        return;
    }
    g_lastHirelingId.store(unitId, std::memory_order_relaxed);
    g_lastHirelingClassId.store(classId, std::memory_order_relaxed);
    g_lastHirelingHealth.store(health, std::memory_order_relaxed);
    g_lastHirelingMaxHealth.store(maxHealth, std::memory_order_relaxed);

    uintptr_t hireling = melee::ServerUnit(game, 1, unitId);
    if (!hireling) {
        InvalidateTacticalContextOnMissingUnit(playerRoom);
        return;
    }

    uint32_t header[4]{};
    if (!Read(hireling, header) || header[0] != 1 || header[2] != unitId) {
        InvalidateTacticalContextOnMissingUnit(playerRoom);
        return;
    }
    const uint32_t animMode = header[3]; // 2 = MONMODE_WALK

    float cx = 0.0f, cy = 0.0f;
    if (!ReadPosition(hireling, cx, cy)) return;
    g_lastHirelingPosX.store(cx, std::memory_order_relaxed);
    g_lastHirelingPosY.store(cy, std::memory_order_relaxed);

    const auto nowMs = GetTickCount64();
    const framevr::companionai::Vec2 compPos{ cx, cy };
    const framevr::companionai::Vec2 plyrPos{ px, py };
    const auto arch = framevr::companionai::GetHirelingArchetype(uint16_t(classId));

    // 3. Tactical retreat state & distress cue
    const bool wasInRetreat = g_retreatController.IsInRetreat();
    const bool potionHealed = g_potionReceived.exchange(false, std::memory_order_relaxed);
    if (potionHealed) {
        g_retreatController.OnPotionConsumed();
    }
    const bool playDistressCue = g_retreatController.Update(health, maxHealth, nowMs, potionHealed);
    const bool inRetreat = g_retreatController.IsInRetreat();
    g_inRetreatState.store(inRetreat, std::memory_order_relaxed);
    const bool enteredRetreat = !wasInRetreat && inRetreat;

    if (playDistressCue) {
        int sound = g_distressSound.load();
        if (sound <= 1) {
            sound = (sound == 0) ? 0 : framevr::companionai::GetArchetypeDistressSound(uint16_t(classId));
        }
        char line[128];
        std::snprintf(line, sizeof line, "Frame companion AI: requesting distress cue (class=%u sound=%d)",
            classId, sound);
        framevr::logging::Write(line);
        if (sound > 0 && g_threads && g_threads->runOnUiThread) {
            g_threads->runOnUiThread(g_ctx, &DistressSoundCallback, reinterpret_cast<void*>(uintptr_t(sound)));
        }
    }

    if (inRetreat != wasInRetreat) {
        char line[160];
        std::snprintf(line, sizeof line,
            "Frame companion AI: tactical retreat %s (class=%u life %.0f/%.0f = %.1f%%)",
            inRetreat ? "ENTERED" : "RECOVERED",
            classId, health, maxHealth, maxHealth > 0 ? (health * 100.0f / maxHealth) : 0.0f);
        framevr::logging::Write(line);
    }

    // 4. On entering retreat: auto-cancel any active tactical order first, then relocate to receiving stance
    if (enteredRetreat) {
        g_activePeelTargetId.store(0, std::memory_order_relaxed);
        g_activeHoldTargetId.store(0, std::memory_order_relaxed);
        {
            AcquireSRWLockExclusive(&g_tacticalLock);
            if (g_tacticalOrderController.IsActive()) {
                const auto prevState = g_tacticalOrderController.GetState();
                const auto prevOrder = g_tacticalOrderController.GetOrder();
                const auto cmdId = g_tacticalOrderController.GetCommandId();
                const auto retries = g_tacticalOrderController.GetRetryCount();
                framevr::companionai::Vec2 unusedTarget{};
                framevr::companionai::TacticalVoiceCue unusedCue = framevr::companionai::TacticalVoiceCue::None;
                if (g_tacticalOrderController.ValidateRuntimeContext(unitId, playerRoom, animMode, health, nowMs, unusedCue) &&
                    g_tacticalOrderController.IsActive()) {
                    (void)g_tacticalOrderController.Evaluate(
                        compPos, plyrPos, nowMs, true, unusedTarget, unusedCue, health, maxHealth, arch);
                }
                const auto newState = g_tacticalOrderController.GetState();
                const auto reason = g_tacticalOrderController.GetLastCancelReason();
                ReleaseSRWLockExclusive(&g_tacticalLock);
                LogTacticalTransition(
                    cmdId, prevOrder.type, prevState, newState,
                    reason, retries, unitId, animMode, compPos, prevOrder.targetPos, playerRoom, hireling);
            } else {
                ReleaseSRWLockExclusive(&g_tacticalLock);
            }
        }
        g_lastDispatchedTargetX.store(0, std::memory_order_relaxed);
        g_lastDispatchedTargetY.store(0, std::memory_order_relaxed);

        const float fx = g_cachedForwardX.load();
        const float fy = g_cachedForwardY.load();
        const auto stancePos = framevr::companionai::CalculateReceivingStancePosition(plyrPos, fx, fy);
        int targetX = int(std::round(stancePos.x));
        int targetY = int(std::round(stancePos.y));

        uintptr_t targetRoom = 0;
        if (roomPointFn) {
            targetRoom = roomPointFn(playerRoom, targetX, targetY);
        }
        if (!targetRoom) {
            targetRoom = playerRoom;
            targetX = int(std::round(px));
            targetY = int(std::round(py));
        }

        if (relocateFn && targetRoom) {
            relocateFn(game, hireling, targetRoom, targetX, targetY, 1, 0);
            g_tacticalMoveEvents.fetch_add(1, std::memory_order_relaxed);
            g_formationController.Reset();
            char line[160];
            std::snprintf(line, sizeof line,
                "Frame companion AI: tactical retreat disengage; companion relocated to receiving stance (%d, %d)",
                targetX, targetY);
            framevr::logging::Write(line);
            return;
        }
    }

    // 5. Watchdog update (Emergency catch-up for severe geometry traps only, suppressed during tactical orders)
    bool isOrderActiveForWatchdog = false;
    {
        AcquireSRWLockShared(&g_tacticalLock);
        isOrderActiveForWatchdog = g_tacticalOrderController.IsActive();
        ReleaseSRWLockShared(&g_tacticalLock);
    }
    if (!isOrderActiveForWatchdog && g_watchdog.Update(animMode, compPos, plyrPos, nowMs, inRetreat)) {
        const float fx = g_cachedForwardX.load();
        const float fy = g_cachedForwardY.load();
        const auto targetPos = inRetreat
            ? framevr::companionai::CalculateReceivingStancePosition(plyrPos, fx, fy)
            : framevr::companionai::CalculateUnstuckPosition(plyrPos, fx, fy);
        int targetX = int(std::round(targetPos.x));
        int targetY = int(std::round(targetPos.y));

        uintptr_t targetRoom = 0;
        if (roomPointFn) {
            targetRoom = roomPointFn(playerRoom, targetX, targetY);
        }
        if (!targetRoom) {
            targetRoom = playerRoom;
            targetX = int(std::round(px));
            targetY = int(std::round(py));
        }

        if (relocateFn && targetRoom) {
            relocateFn(game, hireling, targetRoom, targetX, targetY, 1, 0);
            g_unstuckEvents.fetch_add(1, std::memory_order_relaxed);
            g_formationController.Reset();
            char line[160];
            std::snprintf(line, sizeof line,
                "Frame companion AI: emergency catch-up triggered (dist=%.1f inRetreat=%d); companion relocated to (%d, %d)",
                std::hypot(compPos.x - plyrPos.x, compPos.y - plyrPos.y), inRetreat ? 1 : 0, targetX, targetY);
            framevr::logging::Write(line);
            return;
        }
    }

    // 5.5. Gesture-Based Tactical Commands (Explicit Command State Machine)
    bool isTacticalActive = false;
    if (g_tacticalCommandsEnabled.load()) {
        framevr::companionai::Vec2 tacticalTarget{};
        framevr::companionai::TacticalVoiceCue voiceCue = framevr::companionai::TacticalVoiceCue::None;
        framevr::companionai::TacticalOrder activeOrder{};
        framevr::companionai::TacticalCommandState prevState{};
        framevr::companionai::TacticalCommandState newState{};
        framevr::companionai::TacticalCommandCancelReason cancelReason{};
        uint64_t cmdId = 0;
        uint8_t retryCount = 0;
        uintptr_t resolvedTargetRoom = 0;
        bool submitMoveRequest = false;
        bool logTransition = false;
        static uint64_t s_lastLoggedCmdId = 0;
        static framevr::companionai::TacticalCommandState s_lastLoggedState = framevr::companionai::TacticalCommandState::Idle;
        static framevr::companionai::Vec2 s_lastDispatchedCompPos{};

        {
            AcquireSRWLockExclusive(&g_tacticalLock);
            const auto stateAtTickStart = g_tacticalOrderController.GetState();
            cmdId = g_tacticalOrderController.GetCommandId();
            prevState = (cmdId == s_lastLoggedCmdId) ? s_lastLoggedState : framevr::companionai::TacticalCommandState::Idle;
            const auto orderBefore = g_tacticalOrderController.GetOrder();

            if (g_tacticalOrderController.ValidateRuntimeContext(unitId, playerRoom, animMode, health, nowMs, voiceCue) &&
                g_tacticalOrderController.IsActive()) {
                // 1) For PrioritizeEnemy, verify enemy liveness and track live enemy coordinates
                if (orderBefore.type == framevr::companionai::TacticalOrderType::PrioritizeEnemy && orderBefore.targetUnitId) {
                    uintptr_t enemy = melee::ServerUnit(game, 1, orderBefore.targetUnitId);
                    uint32_t enemyHeader[4]{};
                    float ex = 0.0f, ey = 0.0f;
                    static uint32_t s_missingEnemyTicks = 0;
                    const bool enemyDead = enemy && Read(enemy, enemyHeader) && enemyHeader[0] == 1 &&
                        (enemyHeader[3] == 0 || enemyHeader[3] == 12);
                    const bool enemyValid = enemy && Read(enemy, enemyHeader) && enemyHeader[0] == 1 &&
                        enemyHeader[3] != 0 && enemyHeader[3] != 12 && ReadPosition(enemy, ex, ey);

                    if (enemyDead) {
                        s_missingEnemyTicks = 0;
                        g_tacticalOrderController.ReportTargetDefeated(nowMs);
                    } else if (!enemyValid) {
                        ++s_missingEnemyTicks;
                        if (s_missingEnemyTicks >= 4) { // ~400ms debounced missing/despawn confirmation
                            s_missingEnemyTicks = 0;
                            g_tacticalOrderController.ReportTargetDefeated(nowMs);
                        }
                    } else {
                        s_missingEnemyTicks = 0;
                        g_tacticalOrderController.activeOrder.targetPos = { ex, ey };
                    }
                }

                // 2) For MoveAndHold / PrioritizeEnemy, validate destination room reachability before dispatching
                if (g_tacticalOrderController.IsActive() &&
                    (g_tacticalOrderController.activeOrder.type == framevr::companionai::TacticalOrderType::MoveAndHold ||
                     g_tacticalOrderController.activeOrder.type == framevr::companionai::TacticalOrderType::PrioritizeEnemy)) {
                    const auto effDest = g_tacticalOrderController.ComputeEffectiveMoveTarget(compPos, arch);
                    const int tx = int(std::round(effDest.x));
                    const int ty = int(std::round(effDest.y));
                    resolvedTargetRoom = roomPointFn ? roomPointFn(playerRoom, tx, ty) : playerRoom;
                    if (!resolvedTargetRoom && roomPointFn) {
                        // Fall back to checking raw targetPos room
                        resolvedTargetRoom = roomPointFn(playerRoom, int(std::round(g_tacticalOrderController.activeOrder.targetPos.x)), int(std::round(g_tacticalOrderController.activeOrder.targetPos.y)));
                    }
                    g_tacticalOrderController.activeOrder.targetRoom = resolvedTargetRoom;
                    if (roomPointFn && !resolvedTargetRoom) {
                        g_tacticalOrderController.ReportDestinationUnreachable(nowMs, voiceCue);
                    }
                }

                // 3) Evaluate explicit command state machine (Requested -> Moving -> Arrived / Failed / Cancelled)
                if (g_tacticalOrderController.IsActive()) {
                    submitMoveRequest = g_tacticalOrderController.Evaluate(
                        compPos, plyrPos, nowMs, inRetreat, tacticalTarget, voiceCue, health, maxHealth, arch);
                    if (submitMoveRequest) {
                        g_tacticalCommandEvents.fetch_add(1, std::memory_order_relaxed);
                    } else {
                        tacticalTarget = g_tacticalOrderController.ComputeEffectiveMoveTarget(compPos, arch);
                    }
                }
            }

            isTacticalActive = g_tacticalOrderController.IsActive();
            activeOrder = isTacticalActive ? g_tacticalOrderController.GetOrder() : orderBefore;
            newState = g_tacticalOrderController.GetState();
            cancelReason = g_tacticalOrderController.GetLastCancelReason();
            retryCount = g_tacticalOrderController.GetRetryCount();
            if (cmdId != 0 && (cmdId != s_lastLoggedCmdId || newState != s_lastLoggedState || stateAtTickStart != newState)) {
                s_lastLoggedCmdId = cmdId;
                s_lastLoggedState = newState;
                logTransition = true;
            }
            ReleaseSRWLockExclusive(&g_tacticalLock);
        }

        if (!isTacticalActive && prevState != newState &&
            newState == framevr::companionai::TacticalCommandState::Cancelled &&
            (cancelReason == framevr::companionai::TacticalCommandCancelReason::ExplicitCancel ||
             cancelReason == framevr::companionai::TacticalCommandCancelReason::LeashExceeded ||
             cancelReason == framevr::companionai::TacticalCommandCancelReason::HoldExpired)) {
            g_pendingRegroupMove.store(true, std::memory_order_relaxed);
        }

        // 4) Dispatch verified server walk request (0x4a77e0 -> 0x4a8a10) when commanded
        if (isTacticalActive) {
            const int targetX = int(std::round(tacticalTarget.x));
            const int targetY = int(std::round(tacticalTarget.y));
            const bool blockedWhileMoving = g_nativeMoveBlockedWhileMoving.exchange(false, std::memory_order_relaxed);

            const auto dispatchOrderMove = [&]() noexcept -> bool {
                if (activeOrder.type == framevr::companionai::TacticalOrderType::PrioritizeEnemy &&
                    arch == framevr::companionai::Archetype::MeleeVanguard &&
                    activeOrder.targetUnitId != 0 && originalServerMoveUnit) {
                    uintptr_t enemy = melee::ServerUnit(game, 1, activeOrder.targetUnitId);
                    if (enemy) {
                        struct CommandedMoveScope {
                            CommandedMoveScope() noexcept { g_inCommandedMove = true; }
                            ~CommandedMoveScope() noexcept { g_inCommandedMove = false; }
                        } scope;
                        if (originalServerMoveUnit(game, hireling, enemy, 15, 0, 0, 1, 0) != 0) {
                            return true;
                        }
                    }
                }
                return DispatchServerMoveToCoord(game, hireling, compPos, targetX, targetY);
            };

            if (submitMoveRequest && targetX > 0 && targetY > 0) {
                const bool dispatched = dispatchOrderMove();
                s_lastDispatchedCompPos = compPos;
                if (!dispatched) {
                    AcquireSRWLockExclusive(&g_tacticalLock);
                    if (g_tacticalOrderController.SnapArrivedIfNearObstacle(compPos, nowMs, arch)) {
                        newState = g_tacticalOrderController.GetState();
                        activeOrder = g_tacticalOrderController.GetOrder();
                        s_lastLoggedState = newState;
                        logTransition = true;
                    }
                    ReleaseSRWLockExclusive(&g_tacticalLock);
                }
                char moveLine[200];
                std::snprintf(moveLine, sizeof moveLine,
                    "Frame companion AI: cmd#%llu dispatched server walk to (%d,%d) from (%.1f,%.1f) retry=%u result=%d",
                    static_cast<unsigned long long>(cmdId), targetX, targetY, compPos.x, compPos.y,
                    static_cast<unsigned>(retryCount), dispatched ? 1 : 0);
                framevr::logging::Write(moveLine);
            } else if (newState == framevr::companionai::TacticalCommandState::Moving && targetX > 0 && targetY > 0) {
                uintptr_t compPath = 0;
                uint16_t pathWords[32]{};
                if (Read(hireling + 0x38, compPath) && compPath && Read(compPath, pathWords)) {
                    const bool pathHijacked = (pathWords[26] > 0) &&
                        (std::abs(int(pathWords[10]) - targetX) > 2 || std::abs(int(pathWords[11]) - targetY) > 2);
                    const bool segmentFinishedAfterProgress = (animMode != 2 || pathWords[26] == 0) &&
                        (blockedWhileMoving || std::hypot(compPos.x - s_lastDispatchedCompPos.x, compPos.y - s_lastDispatchedCompPos.y) >= 2.0f);
                    if (pathHijacked || segmentFinishedAfterProgress) {
                        const bool advanced = dispatchOrderMove();
                        s_lastDispatchedCompPos = compPos;
                        if (!advanced) {
                            AcquireSRWLockExclusive(&g_tacticalLock);
                            if (g_tacticalOrderController.SnapArrivedIfNearObstacle(compPos, nowMs, arch)) {
                                newState = g_tacticalOrderController.GetState();
                                activeOrder = g_tacticalOrderController.GetOrder();
                                s_lastLoggedState = newState;
                                logTransition = true;
                            }
                            ReleaseSRWLockExclusive(&g_tacticalLock);
                        }
                    }
                }
            }
        } else {
            g_nativeMoveBlockedWhileMoving.store(false, std::memory_order_relaxed);
            if (g_pendingRegroupMove.exchange(false, std::memory_order_relaxed)) {
                const float fx = g_cachedForwardX.load();
                const float fy = g_cachedForwardY.load();
                const auto flankPos = framevr::companionai::CalculateUnstuckPosition(plyrPos, fx, fy);
                int rx = int(std::round(flankPos.x));
                int ry = int(std::round(flankPos.y));
                bool regrouped = DispatchServerMoveToCoord(game, hireling, compPos, rx, ry);
                if (!regrouped) {
                    rx = int(std::round(px));
                    ry = int(std::round(py));
                    regrouped = DispatchServerMoveToCoord(game, hireling, compPos, rx, ry);
                }
                if (!regrouped && relocateFn && std::hypot(compPos.x - plyrPos.x, compPos.y - plyrPos.y) > 28.0f) {
                    uintptr_t targetRoom = roomPointFn ? roomPointFn(playerRoom, rx, ry) : playerRoom;
                    if (!targetRoom) targetRoom = playerRoom;
                    relocateFn(game, hireling, targetRoom, rx, ry, 1, 0);
                }
                if (cancelReason == framevr::companionai::TacticalCommandCancelReason::ExplicitCancel) {
                    PlayVoiceCue(uint16_t(classId), framevr::companionai::TacticalVoiceCue::MoveConfirm);
                }
            }
        }

        if (logTransition) {
            LogTacticalTransition(
                cmdId, activeOrder.type, prevState, newState, cancelReason,
                retryCount, unitId, animMode, compPos, activeOrder.targetPos, resolvedTargetRoom, hireling);
        }

        if (voiceCue != framevr::companionai::TacticalVoiceCue::None) {
            PlayVoiceCue(uint16_t(classId), voiceCue);
        }

        static bool s_wasTacticalActive = false;
        const bool wasTacticalActive = s_wasTacticalActive;
        s_wasTacticalActive = isTacticalActive;

        if (wasTacticalActive && !isTacticalActive) {
            g_lastDispatchedTargetX.store(0, std::memory_order_relaxed);
            g_lastDispatchedTargetY.store(0, std::memory_order_relaxed);
        }

        if (isTacticalActive) {
            g_activePeelTargetId.store(0, std::memory_order_relaxed);

            // Visual telegraph placement while MoveAndHold is active
            if (activeOrder.type == framevr::companionai::TacticalOrderType::MoveAndHold) {
                framevr::telegraph::Current().Place(activeOrder.targetPos.x, activeOrder.targetPos.y, nowMs, 3000, 3, 1.8f);
                const int targetX = int(std::round(activeOrder.targetPos.x));
                const int targetY = int(std::round(activeOrder.targetPos.y));
                g_lastDispatchedTargetX.store(targetX, std::memory_order_relaxed);
                g_lastDispatchedTargetY.store(targetY, std::memory_order_relaxed);
            }

            // 6b. Active Hold Sentry Combat Targeting (MoveAndHold / DefensiveHold at Arrived state)
            // When holding ground, native AI will not acquire targets if far from the player.
            // We scan hostiles within the archetype's engagement radius of the hold position
            // and supply the best target via TryOverrideCombatTarget (0x48fe20).
            if (!inRetreat &&
                (activeOrder.type == framevr::companionai::TacticalOrderType::MoveAndHold ||
                 activeOrder.type == framevr::companionai::TacticalOrderType::DefensiveHold)) {
                
                const bool atHoldPosition = (newState == framevr::companionai::TacticalCommandState::Arrived) ||
                    (std::hypot(compPos.x - activeOrder.targetPos.x, compPos.y - activeOrder.targetPos.y) <= framevr::companionai::TacticalOrderController::HoldReleaseDeadband);

                if (atHoldPosition) {
                    const float anchorX = (activeOrder.targetPos.x != 0.0f || activeOrder.targetPos.y != 0.0f) ? activeOrder.targetPos.x : compPos.x;
                    const float anchorY = (activeOrder.targetPos.x != 0.0f || activeOrder.targetPos.y != 0.0f) ? activeOrder.targetPos.y : compPos.y;

                    // Archetype engagement radius in sub-tiles: Ranged = 24.0, Melee = 6.0
                    const float maxRange = (arch == framevr::companionai::Archetype::RangedOverwatch)
                        ? framevr::companionai::TacticalOrderController::RangedHoldEngageRadius
                        : framevr::companionai::TacticalOrderController::MeleeHoldEngageRadius;

                    melee::Hostile hostiles[16]{};
                    const int count = melee::HostilesAround(hostiles, 16, maxRange, anchorX, anchorY, hireling);

                    uint32_t chosenTargetId = 0;
                    if (count > 0) {
                        const uint32_t prevTargetId = g_activeHoldTargetId.load(std::memory_order_relaxed);
                        int prevIdx = -1;
                        if (prevTargetId != 0) {
                            for (int i = 0; i < count; ++i) {
                                if (hostiles[i].id == prevTargetId && hostiles[i].clear) {
                                    prevIdx = i;
                                    break;
                                }
                            }
                        }

                        // If previous target is still valid and has line of sight, keep it (sticky focus)!
                        if (prevIdx >= 0) {
                            chosenTargetId = prevTargetId;
                        } else {
                            float bestDist = 9999.0f;
                            int bestIdx = -1;
                            for (int i = 0; i < count; ++i) {
                                const float d = std::hypot(hostiles[i].x - anchorX, hostiles[i].y - anchorY);
                                if (hostiles[i].clear && d < bestDist) {
                                    bestDist = d;
                                    bestIdx = i;
                                }
                            }
                            if (bestIdx < 0) {
                                bestDist = 9999.0f;
                                for (int i = 0; i < count; ++i) {
                                    const float d = std::hypot(hostiles[i].x - anchorX, hostiles[i].y - anchorY);
                                    if (d < bestDist) {
                                        bestDist = d;
                                        bestIdx = i;
                                    }
                                }
                            }
                            if (bestIdx >= 0) {
                                chosenTargetId = hostiles[bestIdx].id;
                            }
                        }
                    }
                    static uint32_t s_lastLoggedHoldTarget = 0;
                    if (chosenTargetId != s_lastLoggedHoldTarget) {
                        s_lastLoggedHoldTarget = chosenTargetId;
                        if (chosenTargetId != 0) {
                            char line[128];
                            std::snprintf(line, sizeof line,
                                "Frame companion AI: hold sentry acquired target %u (found %d hostiles within %.1f)",
                                chosenTargetId, count, maxRange);
                            framevr::logging::Write(line);
                        }
                    }
                    g_activeHoldTargetId.store(chosenTargetId, std::memory_order_relaxed);

                    if (chosenTargetId != 0) {
                        uintptr_t enemy = melee::ServerUnit(game, 1, chosenTargetId);
                        if (!enemy) {
                            static uint32_t s_lastFailedResolveTarget = 0;
                            if (chosenTargetId != s_lastFailedResolveTarget) {
                                s_lastFailedResolveTarget = chosenTargetId;
                                char line[140];
                                std::snprintf(line, sizeof line,
                                    "Frame companion AI: hold sentry target %u failed resolution via ServerUnit", chosenTargetId);
                                framevr::logging::Write(line);
                            }
                            g_activeHoldTargetId.store(0, std::memory_order_relaxed);
                        } else {
                            uint32_t enemyHeader[4]{};
                            if (!Read(enemy, enemyHeader) || enemyHeader[0] != 1 || enemyHeader[3] == 0 || enemyHeader[3] == 12) {
                                static uint32_t s_lastFailedValTarget = 0;
                                if (chosenTargetId != s_lastFailedValTarget) {
                                    s_lastFailedValTarget = chosenTargetId;
                                    char line[140];
                                    std::snprintf(line, sizeof line,
                                        "Frame companion AI: hold sentry target %u validation failed (type=%u mode=%u)",
                                        chosenTargetId, enemyHeader[0], enemyHeader[3]);
                                    framevr::logging::Write(line);
                                }
                                g_activeHoldTargetId.store(0, std::memory_order_relaxed);
                            } else {
                                g_activeHoldTargetId.store(chosenTargetId, std::memory_order_relaxed);

                                float ex = 0.0f, ey = 0.0f;
                                if (ReadPosition(enemy, ex, ey)) {
                                    const float enemyDistToComp = std::hypot(compPos.x - ex, compPos.y - ey);
                                    const float enemyDistToAnchor = std::hypot(anchorX - ex, anchorY - ey);

                                    uint32_t compHeader[4]{};
                                    if (Read(hireling, compHeader)) {
                                        const uint32_t hirelingMode = compHeader[3];
                                        const bool inActiveAttack = (hirelingMode == 4 || hirelingMode == 5 || hirelingMode == 7 || (hirelingMode >= 8 && hirelingMode <= 11));

                                        static uint64_t s_lastHoldAttackMs = 0;

                                        // Only initiate attack when hireling is idle in MONMODE_NEUTRAL (1) and cadence interval has elapsed
                                        if (!inActiveAttack && hirelingMode != 3 /* MONMODE_GETHIT */ && (nowMs - s_lastHoldAttackMs >= 400)) {
                                            const int targetX = int(std::round(ex));
                                            const int targetY = int(std::round(ey));

                                            if (arch == framevr::companionai::Archetype::RangedOverwatch) {
                                                if (originalServerMoveUnit && enemyDistToAnchor <= maxRange) {
                                                    struct CommandedMoveScope {
                                                        CommandedMoveScope() noexcept { g_inCommandedMove = true; }
                                                        ~CommandedMoveScope() noexcept { g_inCommandedMove = false; }
                                                    } scope;
                                                    const int atkResult = originalServerMoveUnit(game, hireling, enemy, 4, targetX, targetY, 1, 0);
                                                    if (atkResult != 0) {
                                                        s_lastHoldAttackMs = nowMs;
                                                        char atkLine[180];
                                                        std::snprintf(atkLine, sizeof atkLine,
                                                            "Frame companion AI: sentry ranged attack dispatched against enemy %u (dist=%.1f anchorDist=%.1f result=%d)",
                                                            chosenTargetId, enemyDistToComp, enemyDistToAnchor, atkResult);
                                                        framevr::logging::Write(atkLine);
                                                    }
                                                }
                                            } else {
                                                // MeleeVanguard
                                                if (enemyDistToComp <= 3.2f) {
                                                    if (originalServerMoveUnit) {
                                                        struct CommandedMoveScope {
                                                            CommandedMoveScope() noexcept { g_inCommandedMove = true; }
                                                            ~CommandedMoveScope() noexcept { g_inCommandedMove = false; }
                                                        } scope;
                                                        const int atkResult = originalServerMoveUnit(game, hireling, enemy, 4, targetX, targetY, 1, 0);
                                                        if (atkResult != 0) {
                                                            s_lastHoldAttackMs = nowMs;
                                                            char atkLine[180];
                                                            std::snprintf(atkLine, sizeof atkLine,
                                                                "Frame companion AI: sentry melee attack dispatched against enemy %u (dist=%.1f result=%d)",
                                                                chosenTargetId, enemyDistToComp, atkResult);
                                                            framevr::logging::Write(atkLine);
                                                        }
                                                    }
                                                } else if (enemyDistToAnchor <= maxRange) {
                                                    if (originalServerMoveUnit) {
                                                        struct CommandedMoveScope {
                                                            CommandedMoveScope() noexcept { g_inCommandedMove = true; }
                                                            ~CommandedMoveScope() noexcept { g_inCommandedMove = false; }
                                                        } scope;
                                                        (void)originalServerMoveUnit(game, hireling, enemy, 15, targetX, targetY, 1, 0);
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    } else {
                        g_activeHoldTargetId.store(0, std::memory_order_relaxed);

                        if (arch == framevr::companionai::Archetype::MeleeVanguard) {
                            const float distFromAnchor = std::hypot(compPos.x - anchorX, compPos.y - anchorY);
                            if (distFromAnchor > 2.0f && originalServerMoveUnit) {
                                uint32_t compHeader[4]{};
                                if (Read(hireling, compHeader) && compHeader[3] == 1 /* MONMODE_NEUTRAL */) {
                                    (void)DispatchServerMoveToCoord(game, hireling, compPos, int(std::round(anchorX)), int(std::round(anchorY)));
                                }
                            }
                        }
                    }
                } else {
                    g_activeHoldTargetId.store(0, std::memory_order_relaxed);
                }
            } else {
                g_activeHoldTargetId.store(0, std::memory_order_relaxed);
            }

        } else {
            g_activeHoldTargetId.store(0, std::memory_order_relaxed);
        }
    }

    // 6. Active Bodyguard Instinct ("Peeling" & Blind-Spot Defense)
    // Routes companion combat target acquisition via TryOverrideCombatTarget (0x48fe20)
    // and dispatches server locomotion toward interceptPos when stationary/out of range.
    const bool recentlyCast = (nowMs - s_lastTacticalCastMs < 800);
    if (!inRetreat && !isTacticalActive && !recentlyCast && g_peelEnabled.load()) {
        const float fx = g_cachedForwardX.load();
        const float fy = g_cachedForwardY.load();

        uint32_t playerMode = 0;
        Read(player + 0xc, playerMode);
        const bool playerHitRecovery = (playerMode == 4 || playerMode == 18);

        melee::Hostile hostiles[16]{};
        float heroGridX = 0.0f, heroGridY = 0.0f;
        const int hostileCount = melee::Hostiles(hostiles, 16, 12.0f, heroGridX, heroGridY);

        if (hostileCount > 0) {
            framevr::companionai::ThreatCandidate candidates[16]{};
            size_t validCandidates = 0;

            for (int i = 0; i < hostileCount && validCandidates < 16; ++i) {
                // Hostiles coordinates from melee::Hostiles are in sub-tiles
                const framevr::companionai::Vec2 mPos{ hostiles[i].x, hostiles[i].y };
                candidates[validCandidates++] = framevr::companionai::EvaluateThreat(
                    hostiles[i].id, hostiles[i].classId, mPos, plyrPos, fx, fy,
                    hostiles[i].clear, playerHitRecovery);
            }

            framevr::companionai::ThreatCandidate primaryThreat{};
            if (g_bodyguardController.SelectPrimaryThreat(std::span(candidates, validCandidates), primaryThreat)) {
                framevr::companionai::Vec2 interceptPos{};

                if (g_bodyguardController.Evaluate(primaryThreat, compPos, plyrPos, arch, nowMs, interceptPos)) {
                    const int targetX = int(std::round(interceptPos.x));
                    const int targetY = int(std::round(interceptPos.y));
                    g_activePeelTargetId.store(primaryThreat.unitId, std::memory_order_relaxed);
                    g_peelEvents.fetch_add(1, std::memory_order_relaxed);
                    if (targetX > 0 && targetY > 0) {
                        (void)DispatchServerMoveToCoord(game, hireling, compPos, targetX, targetY);
                    }
                    char line[180];
                    std::snprintf(line, sizeof line,
                        "Frame companion AI: bodyguard peeling threat (id=%u class=%u dist=%.1f blindSpot=%d score=%.0f) -> intercept (%d, %d)",
                        primaryThreat.unitId, primaryThreat.classId, primaryThreat.distance,
                        primaryThreat.isBlindSpot ? 1 : 0, primaryThreat.score, targetX, targetY);
                    framevr::logging::Write(line);
                } else if (g_bodyguardController.inPeelAction) {
                    g_activePeelTargetId.store(g_bodyguardController.activeTargetId, std::memory_order_relaxed);
                }
            } else {
                g_bodyguardController.Reset();
                g_activePeelTargetId.store(0, std::memory_order_relaxed);
            }
        } else {
            g_bodyguardController.Reset();
            g_activePeelTargetId.store(0, std::memory_order_relaxed);
        }
    } else {
        g_activePeelTargetId.store(0, std::memory_order_relaxed);
    }

    // 7. Tactical Charged Skills & Spells Casting (Phase 3)
    ExecuteCompanionTacticalChargesInternal(
        game, hireling, classId, health, maxHealth,
        compPos, plyrPos, nowMs, inRetreat, isTacticalActive);
}
} // namespace

void Init(const PluginContext* ctx) {
    g_ctx = ctx;
    if (ctx) {
        (void)ctx->QueryService(&g_tables);
        (void)ctx->QueryService(&g_threads);
    }
}

void Configure(bool enabled) {
    g_enabled.store(enabled);
}

void Install() {
    if (g_installed.load()) return;

    if (!mode && d2rsig::Check(abi::ModeRva, abi::ModeSig, sizeof(abi::ModeSig))) {
        mode = reinterpret_cast<ModeFn>(d2rsig::Addr(abi::ModeRva));
    }
    if (!relocateFn && d2rsig::Check(abi::RelocateRva, abi::RelocateSig, sizeof(abi::RelocateSig))) {
        relocateFn = reinterpret_cast<RelocateFn>(d2rsig::Addr(abi::RelocateRva));
    }
    if (!roomPointFn && d2rsig::Check(abi::RoomPointRva, abi::RoomPointSig, sizeof(abi::RoomPointSig))) {
        roomPointFn = reinterpret_cast<RoomPointFn>(d2rsig::Addr(abi::RoomPointRva));
    }
    EnsureServerMoveInstalled();
    EnsureSkillLevelInstalled();

    if (mode && relocateFn && roomPointFn) {
        g_installed.store(true);
        framevr::logging::Write("Frame companion AI: verified native mode, relocate and room-point signatures; companion AI ready");
    }
}

void Tick(float forwardX, float forwardY) {
    g_cachedForwardX.store(forwardX);
    g_cachedForwardY.store(forwardY);

    if (!g_installed.load()) return;

    // 1. Synchronize MonStats table banks if target detection range changed
    const int targetRange = EffectiveDetectRange();
    if (targetRange != g_lastAppliedRange.load() || (!g_enabled.load() && g_anyModified.load())) {
        if (g_threads && g_threads->runOnUiThread) {
            uintptr_t token = 0;
            {
                std::lock_guard lock(g_ticketMutex);
                const auto now = GetTickCount64();
                if (g_syncTicket.Expired(now, 1000)) g_syncTicket.Cancel();
                token = g_syncTicket.Start(now);
            }
            if (token) {
                if (g_threads->runOnUiThread(g_ctx, &SyncBanksCallback, reinterpret_cast<void*>(token)) != Threads::Result::Success) {
                    std::lock_guard lock(g_ticketMutex);
                    g_syncTicket.Cancel();
                }
            }
        } else {
            SyncBanks(g_ctx);
        }
    }

    if (!g_enabled.load() || !mode || mode() != 0) {
        g_watchdogResetRequested.store(true, std::memory_order_relaxed);
        return;
    }

    // 2. Schedule game-thread watchdog check throttled to ~250ms
    const auto now = GetTickCount64();
    if (now - g_lastDispatchMs.load() >= 250 && !g_gameThreadPending.exchange(true)) {
        g_lastDispatchMs.store(now);
        if (!g_threads || !g_threads->runOnGameThread ||
            g_threads->runOnGameThread(g_ctx, &CompanionGameThreadTick, nullptr) != Threads::Result::Success) {
            g_gameThreadPending.store(false);
        }
    }
}

void Report() {
    static int reportedRange = framevr::companionai::DefaultDetectRange;
    const int applied = g_lastAppliedRange.load();
    const unsigned unstuck = g_unstuckEvents.exchange(0);
    const unsigned tactical = g_tacticalMoveEvents.exchange(0);
    const unsigned peels = g_peelEvents.exchange(0);

    if (applied != reportedRange || unstuck > 0 || tactical > 0 || peels > 0) {
        char line[180];
        std::snprintf(line, sizeof line,
            "Frame companion AI: detect_range=%d sub-tiles unstuck_relocations=%u tactical_moves=%u bodyguard_peels=%u",
            applied, unstuck, tactical, peels);
        framevr::logging::Write(line);
        reportedRange = applied;
    }
}

void Shutdown() {
    g_enabled.store(false);
    if (g_anyModified.load() && g_ctx && g_tables) {
        SyncBanks(g_ctx);
    }
    for (auto& b : g_banks) b = BankState{};
    g_anyModified.store(false);
    g_lastAppliedRange.store(framevr::companionai::DefaultDetectRange);
    g_watchdog.Reset();
    g_formationController.Reset();
    g_retreatController.Reset();
    g_bodyguardController.Reset();
    {
        AcquireSRWLockExclusive(&g_tacticalLock);
        g_tacticalOrderController.ResetToIdle();
        ReleaseSRWLockExclusive(&g_tacticalLock);
    }
    g_potionReceived.store(false);
    g_inRetreatState.store(false);
    g_watchdogResetRequested.store(false);
    g_activePeelTargetId.store(0);
    g_lastDispatchedTargetX.store(0);
    g_lastDispatchedTargetY.store(0);
    g_lastHirelingPosX.store(0.0f);
    g_lastHirelingPosY.store(0.0f);
    g_lastPlayerPosX.store(0.0f);
    g_lastPlayerPosY.store(0.0f);
    g_lastPlayerRoom.store(0);
    g_nativeMoveBlockedWhileMoving.store(false);
    std::lock_guard lock(g_ticketMutex);
    g_syncTicket.Cancel();
}

bool IsEnabled() {
    return g_enabled.load();
}

unsigned UnstuckCount() {
    return g_unstuckEvents.load();
}

unsigned TacticalMoveCount() {
    return g_tacticalMoveEvents.load();
}

unsigned PeelCount() {
    return g_peelEvents.load();
}

void ConfigureDistressSound(int soundId) {
    g_distressSound.store(soundId);
}

void ConfigurePeel(bool enabled) {
    g_peelEnabled.store(enabled);
}

void ConfigureTacticalCommands(bool enabled) {
    g_tacticalCommandsEnabled.store(enabled);
}

bool IsTacticalCommandsEnabled() {
    return g_tacticalCommandsEnabled.load();
}

unsigned TacticalCommandCount() {
    return g_tacticalCommandEvents.load(std::memory_order_relaxed);
}

bool IssueTacticalOrder(const framevr::companionai::TacticalOrder& order) {
    if (!g_tacticalCommandsEnabled.load()) return false;

    // Prefer lock-free cached state updated on the game thread to avoid cross-thread server pointer access.
    uint32_t unitId = g_lastHirelingId.load(std::memory_order_relaxed);
    uint32_t classId = g_lastHirelingClassId.load(std::memory_order_relaxed);
    float cx = g_lastHirelingPosX.load(std::memory_order_relaxed);
    float cy = g_lastHirelingPosY.load(std::memory_order_relaxed);
    float px = g_lastPlayerPosX.load(std::memory_order_relaxed);
    float py = g_lastPlayerPosY.load(std::memory_order_relaxed);
    float health = g_lastHirelingHealth.load(std::memory_order_relaxed);
    float maxHealth = g_lastHirelingMaxHealth.load(std::memory_order_relaxed);
    uintptr_t playerRoom = g_lastPlayerRoom.load(std::memory_order_relaxed);

    // Fallback when invoked directly on the game thread before the first tick caches values
    if (!unitId || (cx <= 0.0f && cy <= 0.0f)) {
        uintptr_t game = 0, player = 0;
        if (!melee::LocalServer(game, player)) return false;
        if (!melee::HirelingVitals(unitId, health, maxHealth, classId, unitId) || !unitId) return false;
        uintptr_t hireling = melee::ServerUnit(game, 1, unitId);
        if (!hireling || !ReadPosition(hireling, cx, cy) || !ReadPosition(player, px, py)) return false;
        uintptr_t playerPath = 0;
        (void)(Read(player + 0x38, playerPath) && Read(playerPath + 0x20, playerRoom));
        g_lastHirelingId.store(unitId, std::memory_order_relaxed);
        g_lastHirelingClassId.store(classId, std::memory_order_relaxed);
        g_lastHirelingPosX.store(cx, std::memory_order_relaxed);
        g_lastHirelingPosY.store(cy, std::memory_order_relaxed);
        g_lastHirelingHealth.store(health, std::memory_order_relaxed);
        g_lastHirelingMaxHealth.store(maxHealth, std::memory_order_relaxed);
        g_lastPlayerPosX.store(px, std::memory_order_relaxed);
        g_lastPlayerPosY.store(py, std::memory_order_relaxed);
        g_lastPlayerRoom.store(playerRoom, std::memory_order_relaxed);
    }

    const auto nowMs = GetTickCount64();
    const bool inRetreat = g_inRetreatState.load(std::memory_order_relaxed);
    const auto arch = framevr::companionai::GetHirelingArchetype(uint16_t(classId));
    framevr::companionai::TacticalVoiceCue cue = framevr::companionai::TacticalVoiceCue::None;
    bool accepted = false;

    {
        AcquireSRWLockExclusive(&g_tacticalLock);
        accepted = g_tacticalOrderController.IssueOrder(order, nowMs, {cx, cy}, {px, py}, cue, inRetreat, health, maxHealth, arch);
        if (accepted) {
            g_tacticalOrderController.BindUnitAndArea(unitId, playerRoom);
        }
        ReleaseSRWLockExclusive(&g_tacticalLock);
    }

    if (cue != framevr::companionai::TacticalVoiceCue::None) {
        PlayVoiceCue(uint16_t(classId), cue);
    }

    if (accepted) {
        g_lastDispatchedTargetX.store(0);
        g_lastDispatchedTargetY.store(0);
        g_lastDispatchMs.store(0); // Bypass 250ms throttle for immediate response
        if (order.type == framevr::companionai::TacticalOrderType::MoveAndHold) {
            framevr::telegraph::Current().Place(order.targetPos.x, order.targetPos.y, nowMs, 6000, 3, 1.8f);
        }
    }

    return accepted;
}

void CancelTacticalOrder() {
    const auto nowMs = GetTickCount64();
    AcquireSRWLockExclusive(&g_tacticalLock);
    g_tacticalOrderController.CancelOrder(framevr::companionai::TacticalCommandCancelReason::ExplicitCancel, nowMs);
    ReleaseSRWLockExclusive(&g_tacticalLock);

    g_pendingRegroupMove.store(true, std::memory_order_relaxed);
    g_lastDispatchMs.store(0, std::memory_order_relaxed);
    g_lastDispatchedTargetX.store(0);
    g_lastDispatchedTargetY.store(0);
}

bool TryOverrideCombatTarget(uintptr_t game, uintptr_t unit, uintptr_t& outTarget) {
    outTarget = 0;
    if (!g_enabled.load(std::memory_order_relaxed)) return false;
    const uint32_t hirelingId = g_lastHirelingId.load(std::memory_order_relaxed);
    if (!hirelingId || !unit) return false;

    uint32_t header[4]{};
    if (!Read(unit, header) || header[0] != 1 || header[2] != hirelingId) return false;

    static uint64_t s_lastTryLogMs = 0;
    const uint64_t tryNowMs = GetTickCount64();
    if (tryNowMs - s_lastTryLogMs >= 1000) {
        s_lastTryLogMs = tryNowMs;
        char tryLine[180];
        std::snprintf(tryLine, sizeof tryLine,
            "Frame companion AI: TryOverrideCombatTarget queried for hireling (caller=%p)",
            _ReturnAddress());
        framevr::logging::Write(tryLine);
    }

    // 0. Commanded Action Target Override (e.g. Corpse target for corpse skills)
    if (g_inCommandedAction && g_commandedActionTargetUnit) {
        outTarget = g_commandedActionTargetUnit;
        return true;
    }

    // 1. Tactical Retreat: disengage completely from combat targets
    if (g_inRetreatState.load(std::memory_order_relaxed)) {
        outTarget = 0;
        return true;
    }

    // 2. Explicit Tactical Orders
    if (g_tacticalCommandsEnabled.load(std::memory_order_relaxed)) {
        framevr::companionai::TacticalOrder order{};
        framevr::companionai::TacticalCommandState state = framevr::companionai::TacticalCommandState::Idle;
        bool active = false;
        {
            AcquireSRWLockShared(&g_tacticalLock);
            active = g_tacticalOrderController.IsActive();
            order = g_tacticalOrderController.GetOrder();
            state = g_tacticalOrderController.GetState();
            ReleaseSRWLockShared(&g_tacticalLock);
        }

        if (active) {
            if (order.type == framevr::companionai::TacticalOrderType::PrioritizeEnemy && order.targetUnitId) {
                uintptr_t enemy = melee::ServerUnit(game, 1, order.targetUnitId);
                if (enemy) {
                    uint32_t enemyHeader[4]{};
                    if (Read(enemy, enemyHeader) && enemyHeader[0] == 1 && enemyHeader[3] != 0 && enemyHeader[3] != 12) {
                        outTarget = enemy;
                        return true;
                    }
                }
            }
            if (order.type == framevr::companionai::TacticalOrderType::MoveAndHold ||
                order.type == framevr::companionai::TacticalOrderType::DefensiveHold) {
                float cx = 0.0f, cy = 0.0f;
                const bool hasPos = ReadPosition(unit, cx, cy);
                const float distToAnchor = hasPos ? std::hypot(cx - order.targetPos.x, cy - order.targetPos.y) : 9999.0f;
                // While walking to the commanded MoveAndHold position, suppress combat targets so locomotion is uninterrupted
                // UNLESS the companion is actively taking damage (animMode == 3)
                if (state != framevr::companionai::TacticalCommandState::Arrived &&
                    distToAnchor > framevr::companionai::TacticalOrderController::ArrivalTolerance) {
                    uint32_t compAnim = 0;
                    if (!Read(unit + 0xc, compAnim) || compAnim != 3) { // 3 = MONMODE_GETHIT
                        outTarget = 0;
                        return true;
                    }
                } else {
                    // Sentry at hold position: override with active hold target
                    const uint32_t holdTargetId = g_activeHoldTargetId.load(std::memory_order_relaxed);
                    if (holdTargetId != 0) {
                        uintptr_t enemy = melee::ServerUnit(game, 1, holdTargetId);
                        if (enemy) {
                            uint32_t enemyHeader[4]{};
                            if (Read(enemy, enemyHeader) && enemyHeader[0] == 1 && enemyHeader[3] != 0 && enemyHeader[3] != 12) {
                                outTarget = enemy;
                                return true;
                            }
                        }
                    }
                }
                return false;
            }
            return false;
        }
    }

    // 3. Active Bodyguard Peel Target (when no explicit tactical order is active)
    if (g_peelEnabled.load(std::memory_order_relaxed)) {
        const uint32_t peelTargetId = g_activePeelTargetId.load(std::memory_order_relaxed);
        if (peelTargetId != 0) {
            uintptr_t enemy = melee::ServerUnit(game, 1, peelTargetId);
            if (enemy) {
                uint32_t enemyHeader[4]{};
                if (Read(enemy, enemyHeader) && enemyHeader[0] == 1 && enemyHeader[3] != 0 && enemyHeader[3] != 12) {
                    outTarget = enemy;
                    return true;
                }
            }
        }
    }

    return false;
}

uintptr_t FilterNativeCombatTarget(uintptr_t game, uintptr_t unit, uintptr_t candidateTarget) {
    (void)game;
    if (!candidateTarget || !g_enabled.load(std::memory_order_relaxed) || !g_tacticalCommandsEnabled.load(std::memory_order_relaxed)) {
        return candidateTarget;
    }

    const uint32_t hirelingId = g_lastHirelingId.load(std::memory_order_relaxed);
    if (!hirelingId || !unit) return candidateTarget;

    uint32_t header[4]{};
    if (!Read(unit, header) || header[0] != 1 || header[2] != hirelingId) return candidateTarget;

    static uint64_t s_lastFilterLogMs = 0;
    const uint64_t filterNowMs = GetTickCount64();
    if (filterNowMs - s_lastFilterLogMs >= 1000) {
        s_lastFilterLogMs = filterNowMs;
        char filterLine[180];
        std::snprintf(filterLine, sizeof filterLine,
            "Frame companion AI: FilterNativeCombatTarget queried for hireling (caller=%p candidate=%p)",
            _ReturnAddress(), reinterpret_cast<void*>(candidateTarget));
        framevr::logging::Write(filterLine);
    }

    framevr::companionai::TacticalOrder order{};
    bool active = false;
    {
        AcquireSRWLockShared(&g_tacticalLock);
        active = g_tacticalOrderController.IsActive();
        order = g_tacticalOrderController.GetOrder();
        ReleaseSRWLockShared(&g_tacticalLock);
    }

    if (!active) return candidateTarget;

    if (order.type == framevr::companionai::TacticalOrderType::MoveAndHold ||
        order.type == framevr::companionai::TacticalOrderType::DefensiveHold) {
        float tx = 0.0f, ty = 0.0f;
        if (!ReadPosition(candidateTarget, tx, ty)) return 0;

        // Self-Defense Exception: if candidateTarget is attacking the companion or within immediate self-defense radius (<= 3.5 sub-tiles), always allow it!
        float cx = 0.0f, cy = 0.0f;
        if (ReadPosition(unit, cx, cy)) {
            const float distToComp = std::hypot(tx - cx, ty - cy);
            if (distToComp <= 3.5f) {
                return candidateTarget;
            }
        }

        const auto arch = framevr::companionai::GetHirelingArchetype(uint16_t(header[1]));
        const float maxEngageRadius = (arch == framevr::companionai::Archetype::RangedOverwatch)
            ? framevr::companionai::TacticalOrderController::RangedHoldEngageRadius
            : framevr::companionai::TacticalOrderController::MeleeHoldEngageRadius;

        float anchorX = order.targetPos.x;
        float anchorY = order.targetPos.y;
        if (anchorX == 0.0f && anchorY == 0.0f) {
            if (!ReadPosition(unit, anchorX, anchorY)) return candidateTarget;
        }

        const float distFromAnchor = std::hypot(tx - anchorX, ty - anchorY);
        if (distFromAnchor > maxEngageRadius) {
            return 0;
        }
    }

    return candidateTarget;
}

uintptr_t GetTacticalCombatTarget(uintptr_t game, uintptr_t unit) {
    uintptr_t target = 0;
    if (TryOverrideCombatTarget(game, unit, target)) {
        return target;
    }
    return 0;
}

bool GetHirelingWorldSubtilePos(float& outX, float& outY) {
    const float x = g_lastHirelingPosX.load(std::memory_order_relaxed);
    const float y = g_lastHirelingPosY.load(std::memory_order_relaxed);
    if (x <= 0.0f && y <= 0.0f) return false;
    outX = x;
    outY = y;
    return true;
}

bool GetPlayerWorldSubtilePos(float& outX, float& outY) {
    const float x = g_lastPlayerPosX.load(std::memory_order_relaxed);
    const float y = g_lastPlayerPosY.load(std::memory_order_relaxed);
    if (x <= 0.0f && y <= 0.0f) return false;
    outX = x;
    outY = y;
    return true;
}

framevr::companionai::TacticalOrder GetActiveTacticalOrder() {
    AcquireSRWLockShared(&g_tacticalLock);
    const auto order = g_tacticalOrderController.GetOrder();
    ReleaseSRWLockShared(&g_tacticalLock);
    return order;
}

framevr::companionai::TacticalCommandState GetTacticalCommandState() {
    AcquireSRWLockShared(&g_tacticalLock);
    const auto state = g_tacticalOrderController.GetState();
    ReleaseSRWLockShared(&g_tacticalLock);
    return state;
}

framevr::companionai::TacticalCommandCancelReason GetTacticalCommandCancelReason() {
    AcquireSRWLockShared(&g_tacticalLock);
    const auto reason = g_tacticalOrderController.GetLastCancelReason();
    ReleaseSRWLockShared(&g_tacticalLock);
    return reason;
}

uint64_t GetTacticalCommandId() {
    AcquireSRWLockShared(&g_tacticalLock);
    const auto id = g_tacticalOrderController.GetCommandId();
    ReleaseSRWLockShared(&g_tacticalLock);
    return id;
}

void OnPotionReceived() {
    g_potionReceived.store(true, std::memory_order_relaxed);
    g_inRetreatState.store(false, std::memory_order_relaxed);
    g_lastDispatchMs.store(0, std::memory_order_relaxed);
    framevr::logging::Write("Frame companion AI: potion received; companion exiting tactical retreat");
}

bool IsInTacticalRetreat() {
    return g_inRetreatState.load(std::memory_order_relaxed);
}

void ConfigureItemSkills(bool enabled) {
    g_companionItemSkillsEnabled.store(enabled);
}

bool IsItemSkillsEnabled() {
    return g_companionItemSkillsEnabled.load();
}

void ConfigureConsumeItemCharges(bool enabled) {
    g_companionConsumeItemCharges.store(enabled);
}

bool IsConsumeItemChargesEnabled() {
    return g_companionConsumeItemCharges.load();
}

void ExecuteCompanionTacticalCharges(
    uintptr_t game,
    uintptr_t hireling,
    uint32_t classId,
    float health,
    float maxHealth,
    framevr::companionai::Vec2 compPos,
    framevr::companionai::Vec2 plyrPos,
    uint64_t nowMs,
    bool inRetreat,
    bool isTacticalActive)
{
    ExecuteCompanionTacticalChargesInternal(
        game, hireling, classId, health, maxHealth,
        compPos, plyrPos, nowMs, inRetreat, isTacticalActive);
}

void GetLiveSkillBadges(std::vector<framevr::companionui::MercenarySkillBadge>& out, uint64_t& rev) {
    AcquireSRWLockShared(&s_badgesLock);
    rev = s_badgesRevision.load(std::memory_order_acquire);
    out = s_cachedBadges;
    ReleaseSRWLockShared(&s_badgesLock);
}

} // namespace companionai

namespace framevr::companionai {
void GetLiveMercenarySkillBadges(std::vector<companionui::MercenarySkillBadge>& out, uint64_t& rev) {
    ::companionai::GetLiveSkillBadges(out, rev);
}
}
