#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <span>
#include <vector>

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

} // namespace framevr::companionai

