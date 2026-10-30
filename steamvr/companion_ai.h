#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <span>
#include <vector>
#include "companion_item_skills_ui.h"

namespace framevr::companionai {

// Constants for companion AI modernizations
inline constexpr int DefaultDetectRange = 25;       // Vanilla baseline detection range (sub-tiles)
inline constexpr int MinDetectRange = 25;
inline constexpr int MaxDetectRange = 48;           // Max ceiling matching engine LOS limit (sub-tiles)
inline constexpr int DefaultLeashDistance = 24;     // Vanilla baseline idle leash (sub-tiles)
inline constexpr int ElasticCombatLeashDistance = 40; // Expanded leash during active combat (sub-tiles)

// Anti-stuck watchdog thresholds
// Anti-stuck watchdog thresholds (emergency catch-up for severe geometry traps only)
inline constexpr uint64_t StuckTimeoutMs = 8000;         // 8.0 seconds continuous stuck timer
inline constexpr float StuckMinDistanceThreshold = 25.0f; // > 25 sub-tiles (~10m) separation required
inline constexpr float StuckMaxDisplacement = 0.5f;      // Maximum displacement over sample interval
inline constexpr uint64_t StuckSampleIntervalMs = 1000;  // Interval between displacement checks (ms)

// Peripheral front-flank placement offsets (visible in VR peripheral FOV, ~41 deg flank)
inline constexpr float UnstuckForwardDistance = 2.5f;    // Distance ahead of player (sub-tiles, ~1m)
inline constexpr float UnstuckFlankDistance = 2.2f;      // Lateral flank offset (sub-tiles, ~0.9m)

// Phase 3: Survival, Self-Preservation & Potion Interaction Synergy
// CriticalHealthRatio is synchronized with MercLowHealth (0.50f / 50% life in interactions.h) for the red hurt tint
inline constexpr float CriticalHealthRatio = 0.50f;       // Trigger tactical retreat below 50% life (when merc turns red)
inline constexpr float RecoveryHealthRatio = 0.50f;       // Exit tactical retreat when healed to >= 50% life (synchronized with MercLowHealth / red tint off)
inline constexpr uint64_t DistressCueCooldownMs = 8000;   // Minimum interval between audible distress cues (8s)
inline constexpr float ReceivingStanceDistance = 6.5f;    // ~2.6m in front of player (sub-tiles)
inline constexpr float ReceivingStanceAngleDeg = 20.0f;   // +20 deg forward flank for clear throw trajectory
inline constexpr float ReceivingStanceDeadband = 2.0f;    // Deadband to avoid fidgeting in receiving stance
inline constexpr float RetreatStuckDistanceThreshold = 15.0f; // Distance threshold for stuck check in retreat
inline constexpr uint64_t RetreatStuckTimeoutMs = 4000;   // Faster rescue timeout when trapped while critical (4s)

// Native hireling class IDs (MonStats rows: roguehire, act2hire, act3hire, act5hire1, act5hire2)
inline constexpr uint16_t HirelingRows[] = { 271, 338, 359, 560, 561 };
inline constexpr size_t HirelingRowCount = sizeof(HirelingRows) / sizeof(HirelingRows[0]);

namespace MonStatsIndex {
    inline constexpr uint16_t Rogue = 271;       // Act 1 Hireling
    inline constexpr uint16_t Desert = 338;      // Act 2 Hireling
    inline constexpr uint16_t IronWolf = 359;    // Act 3 Hireling
    inline constexpr uint16_t Barbarian1 = 560;  // Act 5 Hireling
    inline constexpr uint16_t Barbarian2 = 561;  // Act 5 Hireling (alternative)
}

// Maps hireling class to native sounds.txt emote row for audible distress cries
inline int GetArchetypeDistressSound(uint16_t classId) {
    switch (classId) {
        case MonStatsIndex::Rogue:      // 271 (Act 1 Rogue Scout) -> Amazon "Help!" emote
            return 2937; // amazon_chat_a_1 (HD redirect: amazon_chat_a_hd1 10050)
        case MonStatsIndex::Desert:     // 338 (Act 2 Desert Spearman) -> Paladin "Help!" emote
            return 3171; // paladin_chat_a_1 (HD redirect: paladin_chat_a_hd1 10292)
        case MonStatsIndex::IronWolf:   // 359 (Act 3 Iron Wolf) -> Necromancer "Help!" emote
            return 3093; // necromancer_chat_a_1 (HD redirect: necromancer_chat_a_hd1 10259)
        case MonStatsIndex::Barbarian1: // 560
        case MonStatsIndex::Barbarian2: // 561 (Act 5 Barbarian) -> Barbarian "Help!" emote
            return 3015; // barbarian_chat_a_1 (HD redirect: barbarian_chat_a_hd1 10115)
        default:
            return 3171; // Default to Paladin help
    }
}

// Tactical voice cues for gesture-based commands
enum class TacticalVoiceCue : uint8_t {
    None = 0,
    Impossible,    // Command rejected / unreachable / out of leash / retreating
    MoveConfirm,   // Acknowledged MoveAndHold
    AttackConfirm, // Acknowledged PrioritizeEnemy
    HoldConfirm,   // Acknowledged DefensiveHold
    RegroupLeash,  // Auto-cancelled hold by leash break
    Distress       // Critical health < 50%
};

// Maps hireling class and tactical voice cue to native sounds.txt row
inline int GetArchetypeVoiceCue(uint16_t classId, TacticalVoiceCue cue) {
    if (cue == TacticalVoiceCue::None) return 0;

    static std::atomic<unsigned> g_voiceVariation{0};
    const unsigned var = g_voiceVariation.fetch_add(1, std::memory_order_relaxed) & 1;

    if (cue == TacticalVoiceCue::Distress) return GetArchetypeDistressSound(classId) + int(var);

    // sounds.txt row indices per character archetype:
    // Amazon base: 2937, Barbarian base: 3015, Necromancer base: 3093, Paladin base: 3171
    int base = 3171; // Default to Paladin
    switch (classId) {
        case MonStatsIndex::Rogue:      base = 2937; break; // Amazon
        case MonStatsIndex::Desert:     base = 3171; break; // Paladin
        case MonStatsIndex::IronWolf:   base = 3093; break; // Necromancer
        case MonStatsIndex::Barbarian1:
        case MonStatsIndex::Barbarian2: base = 3015; break; // Barbarian
        default:                        base = 3171; break;
    }

    switch (cue) {
        case TacticalVoiceCue::Impossible:
            return base + 16 + int(var); // impossible_1 (ama_impossible) / impossible_2 (ama_cant)
        case TacticalVoiceCue::MoveConfirm: {
            switch (classId) {
                case MonStatsIndex::Rogue:
                    return (var == 0) ? 4294 : 4291; // rogue_confirm_a_4 ("Sí") / rogue_confirm_a_1 ("Como desees") - both Voice Set A
                case MonStatsIndex::Desert:
                    return 3952;                      // greiz_greeting_inactive_2 ("Sí" / "Vale" / "Okay")
                case MonStatsIndex::IronWolf:
                    return (var == 0) ? 4040 : 4041; // ironwolf_yes_1 / ironwolf_yes_2 ("Sí" / "Vale" / "Okay")
                case MonStatsIndex::Barbarian1:
                case MonStatsIndex::Barbarian2:
                    return 4565;                      // qualkehk_greeting_inactive_3 ("Sí" / "Vale" / "Okay")
                default:
                    return 3952;
            }
        }
        case TacticalVoiceCue::AttackConfirm:
            return base + 12 + int(var); // chat_g_1 (ama_die) / chat_g_2 (ama_timetodie)
        case TacticalVoiceCue::HoldConfirm:
            return base + 6 + int(var);  // chat_d_1 (ama_thanks) / chat_d_2 (ama_thankyou)
        case TacticalVoiceCue::RegroupLeash: {
            // ~10% probability of saying farewell / "hasta luego" (chat_f_1 / chat_f_2, base + 10)
            // ~90% probability of follow / regroup call (chat_b_1 / chat_b_2, base + 2)
            static std::atomic<unsigned> s_leashCallCount{0};
            const bool isFarewell = ((s_leashCallCount.fetch_add(1, std::memory_order_relaxed) % 10) == 0);
            return base + (isFarewell ? 10 : 2) + int(var);
        }
        default:
            return 0;
    }
}

enum class Archetype {
    MeleeVanguard,   // Act 2 (Desert), Act 5 (Barbarians)
    RangedOverwatch  // Act 1 (Rogue), Act 3 (Iron Wolf)
};

inline Archetype GetHirelingArchetype(uint16_t classId) {
    if (classId == MonStatsIndex::Rogue || classId == MonStatsIndex::IronWolf) {
        return Archetype::RangedOverwatch;
    }
    return Archetype::MeleeVanguard;
}

// MonStats offsets
inline constexpr size_t RowSize = 508;
inline constexpr size_t AiDistOffset = 0x8d;        // uint8_t[3] Normal / Nightmare / Hell
inline constexpr size_t AiOffset = 0x52;            // uint16_t index into MonAi (61 for Hireable)
inline constexpr size_t AlignOffset = 0x87;         // 1 for friendly/ally

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
    bool operator==(const Vec2&) const = default;
};

// Modernized companion detection range: scales with enemy_sight_pct up to MaxDetectRange (48)
inline int ComputeCompanionDetectRange(int enemySightPct) {
    if (enemySightPct <= 100) return DefaultDetectRange;
    const float scaled = std::round(float(DefaultDetectRange) * float(enemySightPct) / 100.0f);
    return int(std::clamp(int(scaled), MinDetectRange, MaxDetectRange));
}

// Elastic combat leash: returns 40 sub-tiles if in combat, 24 sub-tiles if idle
inline int ComputeCombatLeash(bool inCombat, int baseMaxDistance = DefaultLeashDistance) {
    return inCombat ? ElasticCombatLeashDistance : baseMaxDistance;
}

// Leash exceeded check
inline bool IsLeashExceeded(float distanceToPlayer, bool inCombat, int baseMaxDistance = DefaultLeashDistance) {
    const float leash = float(ComputeCombatLeash(inCombat, baseMaxDistance));
    return distanceToPlayer > leash;
}

// Phase 3: Survival, Self-Preservation & Potion Interaction Synergy
inline bool IsCriticalHealth(float health, float maxHealth, float threshold = CriticalHealthRatio) {
    return maxHealth > 0.0f && health > 0.0f && (health / maxHealth) < threshold;
}

inline bool IsHealthRecovered(float health, float maxHealth, float threshold = RecoveryHealthRatio) {
    return maxHealth > 0.0f && (health / maxHealth) >= threshold;
}

// Computes receiving stance position in front of player for throwing potions
inline Vec2 CalculateReceivingStancePosition(Vec2 playerPos, float forwardX, float forwardY,
                                            float distance = ReceivingStanceDistance,
                                            float angleDeg = ReceivingStanceAngleDeg,
                                            bool rightFlank = true) {
    const float len = std::hypot(forwardX, forwardY);
    const float nx = (std::isfinite(len) && len >= 1e-4f) ? (forwardX / len) : 0.0f;
    const float ny = (std::isfinite(len) && len >= 1e-4f) ? (forwardY / len) : -1.0f;
    const float rx = -ny;
    const float ry = nx;

    const float rad = (rightFlank ? angleDeg : -angleDeg) * 0.0174532925f;
    const float cosA = std::cos(rad);
    const float sinA = std::sin(rad);
    const float dirX = nx * cosA + rx * sinA;
    const float dirY = ny * cosA + ry * sinA;

    return {
        playerPos.x + dirX * distance,
        playerPos.y + dirY * distance
    };
}

// Tactical retreat controller managing critical HP retreat, distress sound cues, and potion healing exit
struct TacticalRetreatController {
    bool inRetreat = false;
    uint64_t retreatStartMs = 0;
    uint64_t lastDistressCueMs = 0;
    bool potionReceived = false;

    // Evaluates health state and returns true if a distress sound cue should be played.
    bool Update(float health, float maxHealth, uint64_t nowMs, bool potionHealed = false) {
        if (potionHealed) {
            potionReceived = true;
        }

        if (inRetreat) {
            // Exit retreat if health recovered above 50% or potion was confirmed
            if (potionReceived || IsHealthRecovered(health, maxHealth)) {
                inRetreat = false;
                retreatStartMs = 0;
                potionReceived = false;
                return false;
            }

            // Periodic distress reminder while still in critical danger (must be red / < 50% life)
            if (IsCriticalHealth(health, maxHealth) && nowMs - lastDistressCueMs >= DistressCueCooldownMs) {
                lastDistressCueMs = nowMs;
                return true;
            }
            return false;
        }

        // Check entry into critical retreat (< 50% HP)
        if (IsCriticalHealth(health, maxHealth)) {
            inRetreat = true;
            retreatStartMs = nowMs;
            lastDistressCueMs = nowMs;
            potionReceived = false;
            return true; // Play distress cue immediately on entering retreat
        }

        return false;
    }

    void OnPotionConsumed() {
        potionReceived = true;
        inRetreat = false;
        retreatStartMs = 0;
    }

    void Reset() {
        inRetreat = false;
        retreatStartMs = 0;
        lastDistressCueMs = 0;
        potionReceived = false;
    }

    bool IsInRetreat() const { return inRetreat; }
};

// Computes safe catch-up position on the player's front flank (visible in peripheral FOV)
inline Vec2 CalculateUnstuckPosition(Vec2 playerPos, float forwardX, float forwardY,
                                    float fwdDist = UnstuckForwardDistance,
                                    float flankDist = UnstuckFlankDistance) {
    const float len = std::hypot(forwardX, forwardY);
    if (!std::isfinite(len) || len < 1e-4f) {
        // Fallback: place slightly ahead along -Y
        return { playerPos.x + flankDist, playerPos.y - fwdDist };
    }
    const float nx = forwardX / len;
    const float ny = forwardY / len;
    // Perpendicular flank vector (right normal: (-ny, nx))
    const float rx = -ny;
    const float ry = nx;
    return {
        playerPos.x + nx * fwdDist + rx * flankDist,
        playerPos.y + ny * fwdDist + ry * flankDist
    };
}

// Anti-stuck watchdog state machine
struct StuckWatchdog {
    uint64_t stuckStartMs = 0;
    Vec2 lastSamplePos{};
    uint64_t lastSampleMs = 0;
    bool isStuck = false;

    // animMode: D2 monster mode (2 = MONMODE_WALK)
    // companionPos: current sub-tile position of companion
    // playerPos: current sub-tile position of player
    // nowMs: current GetTickCount64() timestamp
    // inRetreat: whether companion is in critical health tactical retreat (< 25% HP)
    // Returns true ONCE when unstuck action should be executed.
    bool Update(uint32_t animMode, Vec2 companionPos, Vec2 playerPos, uint64_t nowMs, bool inRetreat = false) {
        constexpr uint32_t MONMODE_WALK = 2;
        const float distToPlayer = std::hypot(companionPos.x - playerPos.x, companionPos.y - playerPos.y);
        const float minDistance = inRetreat ? RetreatStuckDistanceThreshold : StuckMinDistanceThreshold;
        const uint64_t timeoutMs = inRetreat ? RetreatStuckTimeoutMs : StuckTimeoutMs;

        // Only consider stuck if companion is in WALK mode and not already near player
        if (animMode != MONMODE_WALK || distToPlayer < minDistance) {
            stuckStartMs = 0;
            lastSampleMs = nowMs;
            lastSamplePos = companionPos;
            isStuck = false;
            return false;
        }

        if (lastSampleMs == 0) {
            lastSampleMs = nowMs;
            lastSamplePos = companionPos;
            stuckStartMs = 0;
            return false;
        }

        const uint64_t sampleElapsed = nowMs - lastSampleMs;
        if (sampleElapsed >= StuckSampleIntervalMs) {
            const float displacement = std::hypot(companionPos.x - lastSamplePos.x, companionPos.y - lastSamplePos.y);
            lastSamplePos = companionPos;
            lastSampleMs = nowMs;

            if (displacement < StuckMaxDisplacement) {
                if (stuckStartMs == 0) {
                    stuckStartMs = nowMs - sampleElapsed;
                }
            } else {
                stuckStartMs = 0;
            }
        }

        if (stuckStartMs > 0 && (nowMs - stuckStartMs) >= timeoutMs) {
            stuckStartMs = 0; // reset after trigger
            lastSampleMs = nowMs;
            lastSamplePos = companionPos;
            isStuck = true;
            return true;
        }

        return false;
    }

    void Reset() {
        stuckStartMs = 0;
        lastSamplePos = {};
        lastSampleMs = 0;
        isStuck = false;
    }
};

// Phase 2: Dynamic Formations & VR Proxemics
inline constexpr float DegToRad = 0.0174532925f;

// Distances in sub-tiles (~0.4m per sub-tile)
inline constexpr float PersonalSpaceRadius = 3.75f;     // ~1.5m clearance bubble
inline constexpr float VanguardDistance = 4.5f;         // ~1.8m ahead
inline constexpr float VanguardAngleDeg = 35.0f;        // +35 deg peripheral front-flank
inline constexpr float OverwatchDistance = 5.5f;        // ~2.2m ahead
inline constexpr float OverwatchAngleDeg = 45.0f;       // +45 deg peripheral front-flank
inline constexpr float ChokepointDistance = 4.5f;       // ~1.8m ahead in doorway
inline constexpr float DoorwayWidthThreshold = 1.8f;    // ~0.7m lateral corridor
inline constexpr float FormationDeadband = 2.5f;        // Deadband to avoid micro-adjustments
inline constexpr uint64_t TacticalMoveIntervalMs = 500; // Throttle rate for non-emergency pathfinding

// Computes archetype formation anchor relative to player position and HMD forward vector
inline Vec2 CalculateFormationAnchor(Vec2 playerPos, float forwardX, float forwardY, Archetype archetype, bool rightFlank = true) {
    const float len = std::hypot(forwardX, forwardY);
    const float nx = (std::isfinite(len) && len >= 1e-4f) ? (forwardX / len) : 0.0f;
    const float ny = (std::isfinite(len) && len >= 1e-4f) ? (forwardY / len) : -1.0f;
    const float rx = -ny;
    const float ry = nx;

    float dist = VanguardDistance;
    float angleDeg = VanguardAngleDeg;
    if (archetype == Archetype::RangedOverwatch) {
        dist = OverwatchDistance;
        angleDeg = OverwatchAngleDeg;
    }

    if (!rightFlank) {
        angleDeg = -angleDeg;
    }

    const float rad = angleDeg * DegToRad;
    const float cosA = std::cos(rad);
    const float sinA = std::sin(rad);
    const float dirX = nx * cosA + rx * sinA;
    const float dirY = ny * cosA + ry * sinA;

    return {
        playerPos.x + dirX * dist,
        playerPos.y + dirY * dist
    };
}

// Checks if companion has intruded into the player's 1.5m personal space bubble
inline bool CheckPersonalSpaceViolation(Vec2 playerPos, Vec2 compPos, float radius = PersonalSpaceRadius) {
    const float dx = compPos.x - playerPos.x;
    const float dy = compPos.y - playerPos.y;
    return std::hypot(dx, dy) < radius;
}

// Calculates evasion target outside the personal space bubble, preferring lateral escape
inline Vec2 CalculatePersonalSpaceEvasion(Vec2 playerPos, Vec2 compPos, float forwardX, float forwardY, float minClearance = PersonalSpaceRadius) {
    const float len = std::hypot(forwardX, forwardY);
    const float nx = (std::isfinite(len) && len >= 1e-4f) ? (forwardX / len) : 0.0f;
    const float ny = (std::isfinite(len) && len >= 1e-4f) ? (forwardY / len) : -1.0f;
    const float rx = -ny;
    const float ry = nx;

    const float dx = compPos.x - playerPos.x;
    const float dy = compPos.y - playerPos.y;
    const float currentDist = std::hypot(dx, dy);

    if (currentDist < 1e-3f) {
        return { playerPos.x + rx * minClearance, playerPos.y + ry * minClearance };
    }

    const float fwd = dx * nx + dy * ny;
    const float lat = dx * rx + dy * ry;
    const float latSign = (lat >= 0.0f) ? 1.0f : -1.0f;

    if (fwd > 0.0f) {
        const float targetLat = latSign * std::max(minClearance, std::abs(lat) + 1.5f);
        return {
            playerPos.x + nx * std::max(fwd, 1.0f) + rx * targetLat,
            playerPos.y + ny * std::max(fwd, 1.0f) + ry * targetLat
        };
    } else {
        const float scale = minClearance / currentDist;
        return {
            playerPos.x + dx * scale,
            playerPos.y + dy * scale
        };
    }
}

// Detects if companion is directly ahead blocking a narrow doorway or corridor
inline bool IsDoorwayObstructed(Vec2 playerPos, float forwardX, float forwardY, Vec2 compPos,
                                float maxDist = ChokepointDistance, float maxWidth = DoorwayWidthThreshold) {
    const float len = std::hypot(forwardX, forwardY);
    const float nx = (std::isfinite(len) && len >= 1e-4f) ? (forwardX / len) : 0.0f;
    const float ny = (std::isfinite(len) && len >= 1e-4f) ? (forwardY / len) : -1.0f;
    const float rx = -ny;
    const float ry = nx;

    const float dx = compPos.x - playerPos.x;
    const float dy = compPos.y - playerPos.y;

    const float fwd = dx * nx + dy * ny;
    const float lat = dx * rx + dy * ry;

    return fwd > 0.5f && fwd <= maxDist && std::abs(lat) <= maxWidth;
}

// Calculates side-step clearance to tuck against wall and clear doorway
inline Vec2 CalculateDoorwayClearance(Vec2 playerPos, float forwardX, float forwardY, Vec2 compPos, bool yieldRight = true) {
    const float len = std::hypot(forwardX, forwardY);
    const float nx = (std::isfinite(len) && len >= 1e-4f) ? (forwardX / len) : 0.0f;
    const float ny = (std::isfinite(len) && len >= 1e-4f) ? (forwardY / len) : -1.0f;
    const float rx = -ny;
    const float ry = nx;

    const float dx = compPos.x - playerPos.x;
    const float dy = compPos.y - playerPos.y;
    const float lat = dx * rx + dy * ry;

    const float stepDir = (std::abs(lat) > 0.2f) ? ((lat >= 0.0f) ? 1.0f : -1.0f) : (yieldRight ? 1.0f : -1.0f);
    const float yieldStep = 2.5f;

    return {
        compPos.x + rx * stepDir * yieldStep,
        compPos.y + ry * stepDir * yieldStep
    };
}

// Phase 4: Active Bodyguard Instinct ("Peeling" & Blind-Spot Coverage)
inline constexpr float BlindSpotCosThreshold = 0.5735764f;   // cos(55 deg) for 110 deg FOV
inline constexpr float EmergencyPeelDistance = 10.0f;        // ~4m: urgent peeling if player is struck
inline constexpr float BlindSpotAmbushDistance = 18.0f;      // ~7.2m: blind-spot flank threat
inline constexpr float UrgentThreatScoreThreshold = 200.0f;  // Threshold to trigger bodyguard intervention
inline constexpr uint64_t BodyguardPeelCooldownMs = 1000;     // Rate limit for commanding peel moves

struct ThreatCandidate {
    uint32_t unitId = 0;
    uint32_t classId = 0;
    Vec2 pos{};           // Sub-tile coordinates
    float distance = 0.0f; // Distance to player (sub-tiles)
    float cosAngle = 0.0f; // Dot product with player forward
    bool isBlindSpot = false; // Outside player's ~110 deg FOV
    bool isRear = false;      // Behind player (cosAngle < 0)
    bool clearLine = false;   // Direct line of sight
    float score = 0.0f;       // Threat priority score
};

// Evaluates a threat candidate relative to player position and look heading
inline ThreatCandidate EvaluateThreat(uint32_t unitId, uint32_t classId, Vec2 monsterPos, Vec2 playerPos,
                                     float forwardX, float forwardY, bool clearLine, bool playerHitRecovery) {
    ThreatCandidate t{};
    t.unitId = unitId;
    t.classId = classId;
    t.pos = monsterPos;
    t.clearLine = clearLine;

    const float dx = monsterPos.x - playerPos.x;
    const float dy = monsterPos.y - playerPos.y;
    t.distance = std::hypot(dx, dy);

    const float fLen = std::hypot(forwardX, forwardY);
    const float nx = (std::isfinite(fLen) && fLen >= 1e-4f) ? (forwardX / fLen) : 0.0f;
    const float ny = (std::isfinite(fLen) && fLen >= 1e-4f) ? (forwardY / fLen) : -1.0f;

    if (t.distance >= 1e-3f) {
        const float vx = dx / t.distance;
        const float vy = dy / t.distance;
        t.cosAngle = std::clamp(nx * vx + ny * vy, -1.0f, 1.0f);
    } else {
        t.cosAngle = 1.0f;
    }

    t.isBlindSpot = t.cosAngle < BlindSpotCosThreshold;
    t.isRear = t.cosAngle < 0.0f;

    // Scoring:
    // Priority 1: Player in hit recovery / stun and enemy is within close range
    if (playerHitRecovery && t.distance <= EmergencyPeelDistance) {
        t.score = 1000.0f - t.distance * 10.0f;
        if (t.clearLine) t.score += 50.0f;
    }
    // Priority 2: Imminent blind-spot ambush (< 10 sub-tiles behind/flank)
    else if (t.isBlindSpot && t.distance <= 10.0f) {
        t.score = 500.0f - t.distance * 10.0f;
        if (t.isRear) t.score += 100.0f;
        if (t.clearLine) t.score += 50.0f;
    }
    // Priority 3: Approaching flanker (< 18 sub-tiles in blind spot)
    else if (t.isBlindSpot && t.distance <= BlindSpotAmbushDistance) {
        t.score = 250.0f - t.distance * 5.0f;
        if (t.isRear) t.score += 50.0f;
        if (t.clearLine) t.score += 25.0f;
    }
    // Priority 4: Standard frontal combatant
    else {
        t.score = 50.0f - t.distance;
        if (t.clearLine) t.score += 10.0f;
    }

    return t;
}

// Computes interception position for bodyguard peeling
inline Vec2 CalculateInterceptPosition(Vec2 playerPos, Vec2 monsterPos, Archetype archetype) {
    const float dx = monsterPos.x - playerPos.x;
    const float dy = monsterPos.y - playerPos.y;
    const float dist = std::hypot(dx, dy);

    if (dist < 1e-3f) {
        return playerPos;
    }

    const float nx = dx / dist;
    const float ny = dy / dist;

    if (archetype == Archetype::MeleeVanguard) {
        // Melee companion steps into the path between monster and player, ~2.0 sub-tiles from monster
        const float offsetFromMonster = std::min(2.0f, dist * 0.5f);
        return {
            monsterPos.x - nx * offsetFromMonster,
            monsterPos.y - ny * offsetFromMonster
        };
    } else {
        // Ranged companion maintains stand-off distance (~4.5 sub-tiles) with clear line of fire
        const float standoff = std::clamp(dist * 0.7f, 3.5f, 6.0f);
        return {
            playerPos.x + nx * (dist - standoff),
            playerPos.y + ny * (dist - standoff)
        };
    }
}

// Bodyguard controller managing blind-spot threat interception and emergency peeling
struct BodyguardController {
    uint32_t activeTargetId = 0;
    uint64_t lastPeelMs = 0;
    Vec2 lastInterceptPos{};
    bool inPeelAction = false;

    // Evaluates a set of threat candidates and selects the primary peel target
    bool SelectPrimaryThreat(std::span<const ThreatCandidate> candidates, ThreatCandidate& outBest) {
        float highestScore = -1.0f;
        int bestIdx = -1;

        for (size_t i = 0; i < candidates.size(); ++i) {
            if (candidates[i].score > highestScore) {
                highestScore = candidates[i].score;
                bestIdx = int(i);
            }
        }

        if (bestIdx >= 0 && highestScore >= UrgentThreatScoreThreshold) {
            outBest = candidates[bestIdx];
            return true;
        }

        return false;
    }

    // Evaluates whether an interception movement should be dispatched
    bool Evaluate(const ThreatCandidate& threat, Vec2 compPos, Vec2 playerPos,
                  Archetype archetype, uint64_t nowMs, Vec2& outTarget) {
        const Vec2 intercept = CalculateInterceptPosition(playerPos, threat.pos, archetype);
        const float distToIntercept = std::hypot(compPos.x - intercept.x, compPos.y - intercept.y);

        // If already within 2.5 sub-tiles of intercept position: companion is actively screening
        if (distToIntercept <= 2.5f) {
            activeTargetId = threat.unitId;
            inPeelAction = true;
            return false;
        }

        // Rate limit reposition commands unless target changed
        if (threat.unitId != activeTargetId || (nowMs - lastPeelMs >= BodyguardPeelCooldownMs)) {
            activeTargetId = threat.unitId;
            lastPeelMs = nowMs;
            lastInterceptPos = intercept;
            outTarget = intercept;
            inPeelAction = true;
            return true;
        }

        return false;
    }

    void Reset() {
        activeTargetId = 0;
        lastPeelMs = 0;
        lastInterceptPos = {};
        inPeelAction = false;
    }
};

// Checks if animMode indicates active attack or skill cast
inline bool IsCombatMode(uint32_t animMode) {
    return animMode >= 4 && animMode <= 8;
}

// Controller coordinating formation, proxemics and doorway clearance
struct TacticalFormationController {
    uint64_t lastMoveMs = 0;
    Vec2 lastTargetPos{};
    bool inTacticalMove = false;

    bool Evaluate(uint32_t animMode, Vec2 compPos, Vec2 playerPos, float forwardX, float forwardY,
                  uint16_t classId, uint64_t nowMs, Vec2& outTarget, bool inRetreat = false) {
        // Priority 1: Emergency personal space bubble (< 1.5m)
        if (CheckPersonalSpaceViolation(playerPos, compPos)) {
            outTarget = CalculatePersonalSpaceEvasion(playerPos, compPos, forwardX, forwardY);
            lastTargetPos = outTarget;
            lastMoveMs = nowMs;
            inTacticalMove = true;
            return true;
        }

        // Priority 2: Doorway/chokepoint clearance
        if (IsDoorwayObstructed(playerPos, forwardX, forwardY, compPos)) {
            outTarget = CalculateDoorwayClearance(playerPos, forwardX, forwardY, compPos);
            lastTargetPos = outTarget;
            lastMoveMs = nowMs;
            inTacticalMove = true;
            return true;
        }

        // Priority 3: Tactical retreat receiving stance (< 25% critical HP)
        // Companion prioritizes falling back to player's forward arc and holding receiving stance for potions
        if (inRetreat) {
            const Vec2 receivingPos = CalculateReceivingStancePosition(playerPos, forwardX, forwardY);
            const float distToStance = std::hypot(compPos.x - receivingPos.x, compPos.y - receivingPos.y);

            if (distToStance > ReceivingStanceDeadband) {
                outTarget = receivingPos;
                lastTargetPos = outTarget;
                lastMoveMs = nowMs;
                inTacticalMove = true;
                return true;
            }
            // Within deadband: hold stance stably, no unnecessary fidgeting
            inTacticalMove = false;
            return false;
        }

        // Priority 4: When companion is in combat mode, allow free combat
        if (IsCombatMode(animMode)) {
            inTacticalMove = false;
            return false;
        }

        // Priority 5: Formation anchor repositioning
        if (nowMs - lastMoveMs >= TacticalMoveIntervalMs) {
            const Archetype arch = GetHirelingArchetype(classId);
            const Vec2 anchor = CalculateFormationAnchor(playerPos, forwardX, forwardY, arch);
            const float distToAnchor = std::hypot(compPos.x - anchor.x, compPos.y - anchor.y);

            if (distToAnchor > FormationDeadband) {
                outTarget = anchor;
                lastTargetPos = outTarget;
                lastMoveMs = nowMs;
                inTacticalMove = true;
                return true;
            }
        }

        return false;
    }

    void Reset() {
        lastMoveMs = 0;
        lastTargetPos = {};
        inTacticalMove = false;
    }
};

// Phase 6: Gesture-Based Tactical Commands (Point & Hold)
enum class TacticalOrderType : uint8_t {
    None = 0,         // Autonomous mode: standard formations and peeling
    MoveAndHold,      // March to target ground coordinate and hold defensive ground
    PrioritizeEnemy,  // Focus attack exclusively on a specific monster entity
    DefensiveHold     // Cease movement and lock current ground position
};

enum class TacticalCommandState : uint8_t {
    Idle = 0,         // No active custom movement command; native AI operates normally
    Requested,        // Valid destination accepted, awaiting game-thread submission
    Moving,           // Movement request active; monitoring progress and stall state
    Arrived,          // Within arrival tolerance and holding position
    Failed,           // Destination rejected/unreachable or stalled past retry limit
    Cancelled         // Explicitly released, expired, leash broken, or unit/level invalidated
};

enum class TacticalCommandCancelReason : uint8_t {
    None = 0,
    ExplicitCancel,
    RetreatOrCriticalHealth,
    LeashExceeded,
    HoldExpired,
    TargetDefeated,
    DestinationUnreachable,
    StalledNoProgress,
    UnitInvalidated,
    LevelTransition
};

inline constexpr const char* TacticalCommandStateName(TacticalCommandState s) noexcept {
    switch (s) {
        case TacticalCommandState::Idle:      return "Idle";
        case TacticalCommandState::Requested: return "Requested";
        case TacticalCommandState::Moving:    return "Moving";
        case TacticalCommandState::Arrived:   return "Arrived";
        case TacticalCommandState::Failed:    return "Failed";
        case TacticalCommandState::Cancelled: return "Cancelled";
        default:                              return "Unknown";
    }
}

inline constexpr const char* TacticalCommandCancelReasonName(TacticalCommandCancelReason r) noexcept {
    switch (r) {
        case TacticalCommandCancelReason::None:                    return "None";
        case TacticalCommandCancelReason::ExplicitCancel:          return "ExplicitCancel";
        case TacticalCommandCancelReason::RetreatOrCriticalHealth: return "RetreatOrCriticalHealth";
        case TacticalCommandCancelReason::LeashExceeded:           return "LeashExceeded";
        case TacticalCommandCancelReason::HoldExpired:             return "HoldExpired";
        case TacticalCommandCancelReason::TargetDefeated:          return "TargetDefeated";
        case TacticalCommandCancelReason::DestinationUnreachable:  return "DestinationUnreachable";
        case TacticalCommandCancelReason::StalledNoProgress:       return "StalledNoProgress";
        case TacticalCommandCancelReason::UnitInvalidated:         return "UnitInvalidated";
        case TacticalCommandCancelReason::LevelTransition:         return "LevelTransition";
        default:                                                   return "Unknown";
    }
}

struct TacticalOrder {
    TacticalOrderType type = TacticalOrderType::None;
    uint32_t targetUnitId = 0;       // Unit ID if type == PrioritizeEnemy
    Vec2 targetPos{};                // Sub-tile world coordinates
    uintptr_t targetRoom = 0;        // D2RoomStrc pointer
    uint64_t issuedMs = 0;           // Timestamp of command issuance
    uint64_t holdDurationMs = 0;     // 0 = indefinite until leash break or cancel
    bool operator==(const TacticalOrder&) const = default;
};

struct TacticalOrderController {
    static constexpr float ArrivalTolerance = 2.0f;
    static constexpr float NearObstacleArrivalTolerance = 4.5f;
    static constexpr float HoldReleaseDeadband = 3.5f;
    static constexpr float MeleeSentryTetherRadius = 5.5f;
    static constexpr float MeleeHoldEngageRadius = 6.0f;
    static constexpr float RangedHoldEngageRadius = 24.0f;
    static constexpr float EngagementTolerance = 3.5f;
    static constexpr float EngagementReleaseDeadband = 5.0f;
    static constexpr float RangedMinStandOff = 5.0f;
    static constexpr float RangedOptimalStandOff = 10.0f;
    static constexpr float RangedEngagementTolerance = 14.0f;
    static constexpr float RangedEngagementReleaseDeadband = 16.5f;
    static constexpr float DuplicateTargetTolerance = 0.5f;
    static constexpr float ProgressThreshold = 0.25f;
    static constexpr uint64_t StallTimeoutMs = 2000;
    static constexpr uint8_t MaxMoveRetries = 2;

    TacticalOrder activeOrder{};
    uint64_t lastOrderMs = 0;
    bool inTacticalOrder = false;

    TacticalCommandState state = TacticalCommandState::Idle;
    TacticalCommandCancelReason lastCancelReason = TacticalCommandCancelReason::None;
    uint64_t commandId = 0;
    uint64_t stateEnteredMs = 0;
    uint64_t lastProgressMs = 0;
    float bestDistToTarget = 0.0f;
    uint8_t retryCount = 0;
    bool pendingMoveSubmission = false;
    uint32_t boundUnitId = 0;
    uintptr_t boundAreaRoom = 0;

    // Computes archetype-aware arrival and release tolerances for the active order
    static void GetOrderTolerances(TacticalOrderType type, Archetype arch, float& outArrivalTol, float& outReleaseDeadband) noexcept {
        if (type == TacticalOrderType::MoveAndHold || type == TacticalOrderType::DefensiveHold) {
            outArrivalTol = ArrivalTolerance;
            outReleaseDeadband = (arch == Archetype::MeleeVanguard) ? (MeleeHoldEngageRadius + 1.5f) : HoldReleaseDeadband;
            return;
        }
        if (type == TacticalOrderType::PrioritizeEnemy) {
            if (arch == Archetype::RangedOverwatch) {
                outArrivalTol = RangedEngagementTolerance;
                outReleaseDeadband = RangedEngagementReleaseDeadband;
            } else {
                outArrivalTol = EngagementTolerance;
                outReleaseDeadband = EngagementReleaseDeadband;
            }
            return;
        }
        outArrivalTol = ArrivalTolerance;
        outReleaseDeadband = HoldReleaseDeadband;
    }

    // Computes the effective destination coordinate for a movement submission, respecting ranged stand-off
    Vec2 ComputeEffectiveMoveTarget(Vec2 compPos, Archetype arch = Archetype::MeleeVanguard, Vec2 playerPos = {}) const noexcept {
        if (activeOrder.type == TacticalOrderType::PrioritizeEnemy && arch == Archetype::RangedOverwatch) {
            float dx = compPos.x - activeOrder.targetPos.x;
            float dy = compPos.y - activeOrder.targetPos.y;
            float dist = std::hypot(dx, dy);
            if (dist < 1e-3f) {
                dx = (playerPos.x != 0.0f || playerPos.y != 0.0f) ? (playerPos.x - activeOrder.targetPos.x) : 1.0f;
                dy = (playerPos.x != 0.0f || playerPos.y != 0.0f) ? (playerPos.y - activeOrder.targetPos.y) : 0.0f;
                dist = std::hypot(dx, dy);
                if (dist < 1e-3f) { dx = 1.0f; dy = 0.0f; dist = 1.0f; }
            }
            if (dist > RangedEngagementTolerance || dist < RangedMinStandOff) {
                // Walk or kite to optimal stand-off firing distance (~10 sub-tiles)
                return {
                    activeOrder.targetPos.x + (dx / dist) * RangedOptimalStandOff,
                    activeOrder.targetPos.y + (dy / dist) * RangedOptimalStandOff
                };
            }
            return compPos;
        }
        return activeOrder.targetPos;
    }

    static void AssignConfirmationCue(TacticalOrderType type, TacticalVoiceCue& outCue) noexcept {
        switch (type) {
            case TacticalOrderType::MoveAndHold:
                outCue = TacticalVoiceCue::MoveConfirm;
                break;
            case TacticalOrderType::PrioritizeEnemy:
                outCue = TacticalVoiceCue::AttackConfirm;
                break;
            case TacticalOrderType::DefensiveHold:
                outCue = TacticalVoiceCue::HoldConfirm;
                break;
            default:
                outCue = TacticalVoiceCue::None;
                break;
        }
    }

    // Issues an order. Validates finite coordinates, health/retreat, and leash distance.
    // Deduplicates identical active orders so repeated inputs do not reset progress or spam requests.
    // Returns true if accepted (outCue set to confirmation cue),
    // or false if rejected (outCue set to TacticalVoiceCue::Impossible).
    bool IssueOrder(const TacticalOrder& order, uint64_t nowMs, Vec2 compPos, Vec2 playerPos, TacticalVoiceCue& outCue, bool inRetreat = false, float health = 0.0f, float maxHealth = 0.0f, Archetype arch = Archetype::MeleeVanguard) {
        if (order.type == TacticalOrderType::None) {
            outCue = TacticalVoiceCue::None;
            return false;
        }

        if (!std::isfinite(compPos.x) || !std::isfinite(compPos.y) ||
            !std::isfinite(playerPos.x) || !std::isfinite(playerPos.y)) {
            outCue = TacticalVoiceCue::Impossible;
            return false;
        }

        if (inRetreat || IsCriticalHealth(health, maxHealth)) {
            outCue = TacticalVoiceCue::Impossible;
            return false;
        }

        const float compDistToPlayer = std::hypot(compPos.x - playerPos.x, compPos.y - playerPos.y);
        if (compDistToPlayer > float(ElasticCombatLeashDistance)) {
            outCue = TacticalVoiceCue::Impossible;
            return false;
        }

        if (order.type == TacticalOrderType::MoveAndHold || order.type == TacticalOrderType::PrioritizeEnemy) {
            if (!std::isfinite(order.targetPos.x) || !std::isfinite(order.targetPos.y) ||
                (order.targetPos.x <= 0.0f && order.targetPos.y <= 0.0f)) {
                outCue = TacticalVoiceCue::Impossible;
                return false;
            }
            const float targetDistToPlayer = std::hypot(order.targetPos.x - playerPos.x, order.targetPos.y - playerPos.y);
            if (targetDistToPlayer > float(ElasticCombatLeashDistance)) {
                outCue = TacticalVoiceCue::Impossible;
                return false;
            }
        }

        // Deduplicate identical active commands so repeated clicks do not reset progress or spam dispatches
        if (IsActive() && activeOrder.type == order.type) {
            bool isDuplicate = false;
            if (order.type == TacticalOrderType::MoveAndHold) {
                const float delta = std::hypot(activeOrder.targetPos.x - order.targetPos.x, activeOrder.targetPos.y - order.targetPos.y);
                isDuplicate = (delta <= DuplicateTargetTolerance);
            } else if (order.type == TacticalOrderType::PrioritizeEnemy) {
                isDuplicate = (order.targetUnitId != 0 && activeOrder.targetUnitId == order.targetUnitId);
            } else if (order.type == TacticalOrderType::DefensiveHold) {
                isDuplicate = true;
            }

            if (isDuplicate) {
                activeOrder.issuedMs = nowMs;
                activeOrder.holdDurationMs = order.holdDurationMs;
                if (order.type == TacticalOrderType::PrioritizeEnemy) {
                    activeOrder.targetPos = order.targetPos;
                }
                lastOrderMs = nowMs;
                AssignConfirmationCue(order.type, outCue);
                return true;
            }
        }

        ++commandId;
        activeOrder = order;
        activeOrder.issuedMs = nowMs;
        lastOrderMs = nowMs;
        inTacticalOrder = true;
        lastCancelReason = TacticalCommandCancelReason::None;
        retryCount = 0;
        stateEnteredMs = nowMs;
        lastProgressMs = nowMs;

        if (order.type == TacticalOrderType::DefensiveHold) {
            if (activeOrder.targetPos.x == 0.0f && activeOrder.targetPos.y == 0.0f) {
                activeOrder.targetPos = compPos;
            }
            bestDistToTarget = 0.0f;
            state = TacticalCommandState::Arrived;
            pendingMoveSubmission = false;
        } else {
            const float distToTarget = std::hypot(compPos.x - order.targetPos.x, compPos.y - order.targetPos.y);
            bestDistToTarget = distToTarget;
            float arrivalTol = ArrivalTolerance;
            float releaseDeadband = HoldReleaseDeadband;
            GetOrderTolerances(order.type, arch, arrivalTol, releaseDeadband);
            if (order.type == TacticalOrderType::PrioritizeEnemy && arch == Archetype::RangedOverwatch) {
                if (distToTarget >= RangedMinStandOff && distToTarget <= RangedEngagementTolerance) {
                    state = TacticalCommandState::Arrived;
                    pendingMoveSubmission = false;
                } else {
                    state = TacticalCommandState::Requested;
                    pendingMoveSubmission = true;
                }
            } else if (distToTarget <= arrivalTol) {
                state = TacticalCommandState::Arrived;
                pendingMoveSubmission = false;
            } else {
                state = TacticalCommandState::Requested;
                pendingMoveSubmission = true;
            }
        }

        AssignConfirmationCue(order.type, outCue);
        return true;
    }

    void CancelOrder(TacticalCommandCancelReason reason = TacticalCommandCancelReason::ExplicitCancel, uint64_t nowMs = 0) {
        if (inTacticalOrder || state == TacticalCommandState::Requested || state == TacticalCommandState::Moving || state == TacticalCommandState::Arrived) {
            state = TacticalCommandState::Cancelled;
            lastCancelReason = reason;
            stateEnteredMs = nowMs;
        } else if (state != TacticalCommandState::Failed && state != TacticalCommandState::Cancelled) {
            state = TacticalCommandState::Idle;
        }
        activeOrder = {};
        inTacticalOrder = false;
        pendingMoveSubmission = false;
        retryCount = 0;
    }

    void ResetToIdle() {
        activeOrder = {};
        inTacticalOrder = false;
        state = TacticalCommandState::Idle;
        lastCancelReason = TacticalCommandCancelReason::None;
        pendingMoveSubmission = false;
        retryCount = 0;
        boundUnitId = 0;
        boundAreaRoom = 0;
    }

    void BindUnitAndArea(uint32_t unitId, uintptr_t areaRoom) {
        boundUnitId = unitId;
        boundAreaRoom = areaRoom;
    }

    // Validates that the mercenary unit and active level context remain valid.
    // Returns false if the active command was invalidated and cancelled.
    bool ValidateRuntimeContext(uint32_t currentUnitId, uintptr_t currentAreaRoom, uint32_t animMode, float health, uint64_t nowMs, TacticalVoiceCue& outCue) {
        outCue = TacticalVoiceCue::None;
        const bool unitDeadOrMissing = (currentUnitId == 0 || health <= 0.0f || animMode == 0 || animMode == 12);
        const bool unitChanged = (boundUnitId != 0 && currentUnitId != 0 && currentUnitId != boundUnitId);
        const bool levelInvalid = (boundAreaRoom != 0 && currentAreaRoom == 0);

        if (unitDeadOrMissing || unitChanged) {
            boundUnitId = currentUnitId;
            boundAreaRoom = currentAreaRoom;
            if (IsActive()) {
                CancelOrder(TacticalCommandCancelReason::UnitInvalidated, nowMs);
            }
            return false;
        }

        if (levelInvalid) {
            boundAreaRoom = currentAreaRoom;
            if (IsActive()) {
                CancelOrder(TacticalCommandCancelReason::LevelTransition, nowMs);
            }
            return false;
        }

        boundUnitId = currentUnitId;
        boundAreaRoom = currentAreaRoom;
        return true;
    }

    // Marks the active order as Failed because the destination room/coordinates are unreachable.
    void ReportDestinationUnreachable(uint64_t nowMs, TacticalVoiceCue& outCue) {
        state = TacticalCommandState::Failed;
        lastCancelReason = TacticalCommandCancelReason::DestinationUnreachable;
        stateEnteredMs = nowMs;
        activeOrder = {};
        inTacticalOrder = false;
        pendingMoveSubmission = false;
        retryCount = 0;
        outCue = TacticalVoiceCue::Impossible;
    }

    // Marks a PrioritizeEnemy order as Cancelled when the target enemy dies or despawns.
    void ReportTargetDefeated(uint64_t nowMs) {
        CancelOrder(TacticalCommandCancelReason::TargetDefeated, nowMs);
    }

    // If MoveAndHold walked up to an obstacle edge within NearObstacleArrivalTolerance (4.5 sub-tiles)
    // and native A* cannot step any closer, snap anchor to compPos and mark Arrived rather than failing.
    bool SnapArrivedIfNearObstacle(Vec2 compPos, uint64_t nowMs, Archetype arch = Archetype::MeleeVanguard) noexcept {
        if (!inTacticalOrder || state != TacticalCommandState::Moving) {
            return false;
        }
        const float dist = std::hypot(compPos.x - activeOrder.targetPos.x, compPos.y - activeOrder.targetPos.y);
        if (activeOrder.type == TacticalOrderType::MoveAndHold) {
            if (dist <= NearObstacleArrivalTolerance) {
                activeOrder.targetPos = compPos;
                state = TacticalCommandState::Arrived;
                stateEnteredMs = nowMs;
                retryCount = 0;
                pendingMoveSubmission = false;
                return true;
            }
        } else if (activeOrder.type == TacticalOrderType::PrioritizeEnemy && arch == Archetype::RangedOverwatch) {
            if (dist <= RangedEngagementTolerance && dist >= RangedMinStandOff) {
                state = TacticalCommandState::Arrived;
                stateEnteredMs = nowMs;
                retryCount = 0;
                pendingMoveSubmission = false;
                return true;
            }
        }
        return false;
    }

    // Evaluates the active order on tick.
    // Returns true ONLY when a single movement request should be submitted (on Requested -> Moving
    // or a bounded stall retry), never spamming on every tick.
    // If order is cancelled or fails, transitions state and sets outCue appropriately.
    bool Evaluate(Vec2 compPos, Vec2 playerPos, uint64_t nowMs, bool inRetreat, Vec2& outTarget, TacticalVoiceCue& outCue, float health = 0.0f, float maxHealth = 0.0f, Archetype arch = Archetype::MeleeVanguard) {
        outCue = TacticalVoiceCue::None;
        if (!inTacticalOrder || activeOrder.type == TacticalOrderType::None) {
            return false;
        }

        // Priority 1: Critical health retreat aborts tactical orders immediately
        if (inRetreat || IsCriticalHealth(health, maxHealth)) {
            CancelOrder(TacticalCommandCancelReason::RetreatOrCriticalHealth, nowMs);
            outCue = TacticalVoiceCue::Distress;
            return false;
        }

        // Priority 2: Elastic leash auto-cancel (> 40 sub-tiles from player)
        const float compDistToPlayer = std::hypot(compPos.x - playerPos.x, compPos.y - playerPos.y);
        if (compDistToPlayer > float(ElasticCombatLeashDistance)) {
            CancelOrder(TacticalCommandCancelReason::LeashExceeded, nowMs);
            outCue = TacticalVoiceCue::RegroupLeash;
            return false;
        }

        if (activeOrder.type == TacticalOrderType::MoveAndHold || activeOrder.type == TacticalOrderType::PrioritizeEnemy) {
            const float targetDistToPlayer = std::hypot(activeOrder.targetPos.x - playerPos.x, activeOrder.targetPos.y - playerPos.y);
            if (targetDistToPlayer > float(ElasticCombatLeashDistance)) {
                CancelOrder(TacticalCommandCancelReason::LeashExceeded, nowMs);
                outCue = TacticalVoiceCue::RegroupLeash;
                return false;
            }
        }

        // Priority 3: Optional timeout duration
        if (activeOrder.holdDurationMs > 0 && (nowMs - activeOrder.issuedMs) >= activeOrder.holdDurationMs) {
            CancelOrder(TacticalCommandCancelReason::HoldExpired, nowMs);
            outCue = TacticalVoiceCue::RegroupLeash;
            return false;
        }

        // Priority 4: State-machine evaluation by order type
        if (activeOrder.type == TacticalOrderType::DefensiveHold) {
            const float distToHold = std::hypot(compPos.x - activeOrder.targetPos.x, compPos.y - activeOrder.targetPos.y);
            if (distToHold > HoldReleaseDeadband) {
                outTarget = activeOrder.targetPos;
                return true;
            }
            state = TacticalCommandState::Arrived;
            pendingMoveSubmission = false;
            return false; // Hold current position
        }

        if (activeOrder.type == TacticalOrderType::MoveAndHold || activeOrder.type == TacticalOrderType::PrioritizeEnemy) {
            float arrivalTol = ArrivalTolerance;
            float releaseDeadband = HoldReleaseDeadband;
            GetOrderTolerances(activeOrder.type, arch, arrivalTol, releaseDeadband);
            const float distToTarget = std::hypot(compPos.x - activeOrder.targetPos.x, compPos.y - activeOrder.targetPos.y);
            const Vec2 effectiveMoveTarget = ComputeEffectiveMoveTarget(compPos, arch, playerPos);

            if (state == TacticalCommandState::Arrived) {
                // For RangedOverwatch under PrioritizeEnemy: if the enemy approaches closer than RangedMinStandOff (5.0 sub-tiles),
                // the archer must kite away rather than letting the enemy get glued to her!
                if (activeOrder.type == TacticalOrderType::PrioritizeEnemy &&
                    arch == Archetype::RangedOverwatch &&
                    distToTarget < RangedMinStandOff) {
                    state = TacticalCommandState::Moving;
                    stateEnteredMs = nowMs;
                    lastProgressMs = nowMs;
                    bestDistToTarget = distToTarget;
                    retryCount = 0;
                    pendingMoveSubmission = false;
                    outTarget = effectiveMoveTarget;
                    return true;
                }

                // Hysteresis deadband prevents oscillation between Arrived and Moving on minor collision pushback
                if (distToTarget <= releaseDeadband) {
                    pendingMoveSubmission = false;
                    return false;
                }
                // Pushed significantly outside hold deadband: transition back to Moving with a single re-submission
                state = TacticalCommandState::Moving;
                stateEnteredMs = nowMs;
                lastProgressMs = nowMs;
                bestDistToTarget = distToTarget;
                retryCount = 0;
                pendingMoveSubmission = false;
                outTarget = effectiveMoveTarget;
                return true;
            }

            // Check arrival within tolerance
            if (distToTarget <= arrivalTol) {
                state = TacticalCommandState::Arrived;
                stateEnteredMs = nowMs;
                retryCount = 0;
                pendingMoveSubmission = false;
                return false; // Arrived; hold position without thrashing path calls
            }

            // Initial submission: transition Requested -> Moving once
            if (state == TacticalCommandState::Requested || pendingMoveSubmission) {
                state = TacticalCommandState::Moving;
                stateEnteredMs = nowMs;
                lastProgressMs = nowMs;
                bestDistToTarget = distToTarget;
                pendingMoveSubmission = false;
                outTarget = effectiveMoveTarget;
                return true;
            }

            // Currently Moving: track progress toward target and enforce stall/retry bounds
            if (state == TacticalCommandState::Moving) {
                if (distToTarget + ProgressThreshold < bestDistToTarget) {
                    bestDistToTarget = distToTarget;
                    lastProgressMs = nowMs;
                    return false; // Making progress; do not spam movement entry point
                }

                if (nowMs >= lastProgressMs && (nowMs - lastProgressMs) >= StallTimeoutMs) {
                    if (retryCount < MaxMoveRetries) {
                        ++retryCount;
                        lastProgressMs = nowMs;
                        outTarget = effectiveMoveTarget;
                        return true; // Bounded retry after stall timeout
                    }
                    // Stalled beyond retry limit: transition Moving -> Failed and stop retrying
                    state = TacticalCommandState::Failed;
                    lastCancelReason = TacticalCommandCancelReason::StalledNoProgress;
                    stateEnteredMs = nowMs;
                    activeOrder = {};
                    inTacticalOrder = false;
                    pendingMoveSubmission = false;
                    outCue = TacticalVoiceCue::Impossible;
                    return false;
                }

                return false; // Waiting for movement progress within stall window
            }
        }

        return false;
    }

    bool IsActive() const { return inTacticalOrder && activeOrder.type != TacticalOrderType::None; }
    const TacticalOrder& GetOrder() const { return activeOrder; }
    TacticalOrderType GetOrderType() const { return activeOrder.type; }
    TacticalCommandState GetState() const { return state; }
    TacticalCommandCancelReason GetLastCancelReason() const { return lastCancelReason; }
    uint64_t GetCommandId() const { return commandId; }
    uint8_t GetRetryCount() const { return retryCount; }
    float GetBestDistanceToTarget() const { return bestDistToTarget; }
};

struct HirelingBaseline {
    uint16_t row = 0;
    uint8_t aidist[3]{};
};

inline bool CaptureHirelingBaselines(std::span<const uint8_t> table, size_t rowCount, std::vector<HirelingBaseline>& out) {
    if (rowCount < 562 || table.size() != rowCount * RowSize) return false;
    out.clear();
    out.reserve(HirelingRowCount);
    for (uint16_t r : HirelingRows) {
        if (size_t(r) >= rowCount) return false;
        const uint8_t* rowPtr = table.data() + size_t(r) * RowSize;
        HirelingBaseline b{};
        b.row = r;
        std::memcpy(b.aidist, rowPtr + AiDistOffset, 3);
        out.push_back(b);
    }
    return true;
}

inline bool WriteHirelingAiDist(std::span<uint8_t> table, size_t rowCount, uint8_t detectRange) {
    if (rowCount < 562 || table.size() != rowCount * RowSize) return false;
    const uint8_t distBytes[3] = { detectRange, detectRange, detectRange };
    for (uint16_t r : HirelingRows) {
        if (size_t(r) >= rowCount) return false;
        uint8_t* rowPtr = table.data() + size_t(r) * RowSize;
        std::memcpy(rowPtr + AiDistOffset, distBytes, 3);
    }
    return true;
}

inline bool RestoreHirelingBaselines(std::span<uint8_t> table, size_t rowCount, const std::vector<HirelingBaseline>& baseline) {
    if (rowCount < 562 || table.size() != rowCount * RowSize || baseline.size() != HirelingRowCount) return false;
    for (const auto& b : baseline) {
        if (size_t(b.row) >= rowCount) return false;
        uint8_t* rowPtr = table.data() + size_t(b.row) * RowSize;
        std::memcpy(rowPtr + AiDistOffset, b.aidist, 3);
    }
    return true;
}

void GetLiveMercenarySkillBadges(std::vector<companionui::MercenarySkillBadge>& out, uint64_t& rev);

} // namespace framevr::companionai

