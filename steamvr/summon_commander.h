#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include "companion_ai.h"

namespace framevr::commander {

using Vec2 = framevr::companionai::Vec2;

constexpr size_t MaxArmyUnits = 64;
constexpr size_t MaxMovesPerTick = 32;

// Virtual Skill IDs for Commander of the Dead (Issue #399).
// Chosen inside [3901, 3907] (<= 4095) so they serialize natively in [glyphs] INI settings
// without colliding with native D2R skills (0..428) or GlyphDrawId (9000).
constexpr int SkillPhalanxDef  = 3901; // Defensive Stance + Phalanx Arc (130 deg front wall)
constexpr int SkillRingDef     = 3902; // Defensive Stance + Protective Ring (360 deg perimeter)
constexpr int SkillMarchBal    = 3903; // Balanced Stance + Marching Crescent (front sightline clear)
constexpr int SkillWedgeAgg    = 3904; // Aggressive Stance + Spearhead Wedge (seek & destroy)
constexpr int SkillHoldGround  = 3905; // Anchor / Hold Formation at aimed ground or current center
constexpr int SkillRegroup     = 3906; // Cancel anchors/focus and regroup army to player immediately
constexpr int SkillFallOut     = 3907; // Break ranks / restore standard vanilla AI without formation
constexpr int SkillRing360     = SkillRingDef;
constexpr int SkillArmyFocus   = SkillWedgeAgg;
constexpr int SkillMoveHold    = SkillHoldGround;

constexpr bool IsCommanderSkill(int id) noexcept {
    return id >= SkillPhalanxDef && id <= SkillFallOut;
}

inline const char* CommanderSkillName(int id) noexcept {
    switch (id) {
        case SkillPhalanxDef: return "Army: Phalanx Wall (Def)";
        case SkillRingDef:    return "Army: Protective Ring (Def)";
        case SkillMarchBal:   return "Army: Marching Escort (Bal)";
        case SkillWedgeAgg:   return "Army: Spearhead Assault (Agg)";
        case SkillHoldGround: return "Army: Hold / Anchor Here";
        case SkillRegroup:    return "Army: Regroup Now";
        case SkillFallOut:    return "Army: Fall Out";
        default:              return "";
    }
}

// Returns a representative native Necromancer summoning skill ID whose sprite can be
// used as an icon fallback in the VR wheel / skill picker.
constexpr int CommanderSkillIconId(int id) noexcept {
    switch (id) {
        case SkillPhalanxDef: return 70; // Raise Skeleton
        case SkillRingDef:    return 68; // Bone Armor
        case SkillMarchBal:   return 80; // Raise Skeletal Mage
        case SkillWedgeAgg:   return 95; // Revive
        case SkillHoldGround: return 75; // Clay Golem
        case SkillRegroup:    return 69; // Skeleton Mastery
        case SkillFallOut:    return 70; // Raise Skeleton
        default:              return -1;
    }
}

enum class ArmyStance : uint8_t {
    Defensive  = 0, // Tight wall/perimeter, short leash (7.5), mages rooted behind
    Balanced   = 1, // Marching escort, clear +-12 deg sightline, blind-spot peel (14.0)
    Aggressive = 2, // Wedge march, wide seek-and-destroy engagement radius (32.0) in combat
};

enum class ArmyFormation : uint8_t {
    PhalanxArc     = 0, // 130 deg frontal arc with +-12 deg center sightline corridor
    ProtectiveRing = 1, // 360 deg double ring (melee outer, mages inner)
    SpearheadWedge = 2, // Inverted-V assault spearhead led by Golem/Anchor
    CorridorColumn = 3, // Auto-compressed dual side-rails for narrow corridors/doors
};

enum class SummonRole : uint8_t {
    TacticalAnchor  = 0, // Golem (PetType 3), Grizzly Bear (15), Valkyrie (2), Shadow (16)
    FrontlineMelee  = 1, // Skeleton Warriors (PetType 4), Spirit/Dire Wolves (11, 12)
    RangedArtillery = 2, // Skeleton Mages (PetType 5)
    HeavyFlanker    = 3, // Revives (PetType 6)
};

inline const char* StanceName(ArmyStance stance) noexcept {
    switch (stance) {
        case ArmyStance::Defensive:  return "DEF";
        case ArmyStance::Balanced:   return "BAL";
        case ArmyStance::Aggressive: return "AGG";
        default:                     return "BAL";
    }
}

inline const char* FormationName(ArmyFormation formation) noexcept {
    switch (formation) {
        case ArmyFormation::PhalanxArc:     return "PHALANX";
        case ArmyFormation::ProtectiveRing: return "RING 360";
        case ArmyFormation::SpearheadWedge: return "WEDGE";
        case ArmyFormation::CorridorColumn: return "COLUMN";
        default:                            return "PHALANX";
    }
}

constexpr SummonRole ClassifySummonRole(uint8_t petType, uint16_t classId = 0) noexcept {
    (void)classId;
    switch (petType) {
        case 3:  // Golem
        case 15: // Druid Grizzly Bear
        case 2:  // Amazon Valkyrie
        case 16: // Assassin Shadow Warrior/Master
            return SummonRole::TacticalAnchor;
        case 4:  // Necromancer Skeleton Warrior
        case 11: // Druid Spirit Wolf
        case 12: // Druid Dire Wolf
            return SummonRole::FrontlineMelee;
        case 5:  // Necromancer Skeleton Mage
            return SummonRole::RangedArtillery;
        case 6:  // Necromancer Revive
        default:
            return SummonRole::HeavyFlanker;
    }
}

constexpr float GetStanceLeashRadius(ArmyStance stance, SummonRole role, bool inCombat = true) noexcept {
    if (role == SummonRole::RangedArtillery) {
        // Mages attack from range (up to 20 sub-tiles) but never chase into melee in Defensive/Balanced
        return (stance == ArmyStance::Aggressive && inCombat) ? 18.0f : 3.5f;
    }
    switch (stance) {
        case ArmyStance::Defensive:
            return 7.5f;
        case ArmyStance::Balanced:
            return 14.0f;
        case ArmyStance::Aggressive:
            return inCombat ? 32.0f : 14.0f;
        default:
            return 14.0f;
    }
}

// Stabilizes the player's forward heading so minor VR head turns (< 22.5 deg) while standing
// still do not cause 20 skeletons to constantly swap and recompute A* paths.
struct HeadingFilter {
    static constexpr float HysteresisCos = 0.9238795f; // cos(22.5 deg)
    static constexpr float WalkTriggerDist = 1.5f;     // sub-tiles

    Vec2 latchedForward{ 0.0f, -1.0f };
    Vec2 lastPlayerPos{ 0.0f, 0.0f };
    bool initialized = false;

    void Reset() noexcept {
        latchedForward = { 0.0f, -1.0f };
        lastPlayerPos = { 0.0f, 0.0f };
        initialized = false;
    }

    Vec2 Update(Vec2 playerPos, float rawFx, float rawFy) noexcept {
        const float len = std::hypot(rawFx, rawFy);
        const Vec2 normRaw = (len > 1e-4f) ? Vec2{ rawFx / len, rawFy / len } : latchedForward;

        if (!initialized) {
            latchedForward = normRaw;
            lastPlayerPos = playerPos;
            initialized = true;
            return latchedForward;
        }

        const float movedDist = std::hypot(playerPos.x - lastPlayerPos.x, playerPos.y - lastPlayerPos.y);
        const float dot = latchedForward.x * normRaw.x + latchedForward.y * normRaw.y;

        if (movedDist >= WalkTriggerDist || dot < HysteresisCos) {
            latchedForward = normRaw;
            lastPlayerPos = playerPos;
        }
        return latchedForward;
    }
};

struct RoleCounts {
    uint8_t anchors  = 0;
    uint8_t melee    = 0;
    uint8_t ranged   = 0;
    uint8_t flankers = 0;

    constexpr size_t Total() const noexcept {
        return size_t(anchors) + size_t(melee) + size_t(ranged) + size_t(flankers);
    }
};

struct FormationSlot {
    Vec2 worldPos{};
    SummonRole role = SummonRole::FrontlineMelee;
    float localAngleDeg = 0.0f; // Relative to formation heading (0 = straight ahead)
};

namespace detail {
constexpr float DegToRad(float deg) noexcept {
    return deg * (3.14159265358979323846f / 180.0f);
}

// Transforms local formation coordinates (forwardDist along heading, rightDist perpendicular)
// into isometric sub-tile world coordinates around anchorPos.
inline Vec2 LocalToWorld(Vec2 anchorPos, Vec2 heading, float forwardDist, float rightDist) noexcept {
    const float len = std::hypot(heading.x, heading.y);
    const float fx = (len > 1e-4f) ? (heading.x / len) : 0.0f;
    const float fy = (len > 1e-4f) ? (heading.y / len) : -1.0f;
    // Right perpendicular vector in 2D sub-tile plane
    const float rx = -fy;
    const float ry = fx;
    return {
        anchorPos.x + fx * forwardDist + rx * rightDist,
        anchorPos.y + fy * forwardDist + ry * rightDist
    };
}

inline Vec2 PolarToWorld(Vec2 anchorPos, Vec2 heading, float radius, float angleDeg) noexcept {
    const float rad = DegToRad(angleDeg);
    const float forwardDist = radius * std::cos(rad);
    const float rightDist = radius * std::sin(rad);
    return LocalToWorld(anchorPos, heading, forwardDist, rightDist);
}

// Distributes N slots evenly across two symmetric angular wings: [-maxDeg, -minDeg] and [+minDeg, +maxDeg].
// Preserves the central [-minDeg, +minDeg] line-of-sight corridor.
inline void AppendSplitArcSlots(
    Vec2 anchorPos,
    Vec2 heading,
    SummonRole role,
    uint8_t count,
    float radius,
    float minAbsDeg,
    float maxAbsDeg,
    std::span<FormationSlot> out,
    size_t& cursor) noexcept
{
    if (count == 0 || cursor >= out.size()) return;
    const uint8_t leftCount = uint8_t((count + 1) / 2);
    const uint8_t rightCount = uint8_t(count - leftCount);

    for (uint8_t i = 0; i < leftCount && cursor < out.size(); ++i) {
        const float t = (leftCount == 1) ? 0.25f : float(i) / float(leftCount - 1);
        const float deg = -(minAbsDeg + t * (maxAbsDeg - minAbsDeg));
        out[cursor++] = { PolarToWorld(anchorPos, heading, radius, deg), role, deg };
    }
    for (uint8_t i = 0; i < rightCount && cursor < out.size(); ++i) {
        const float t = (rightCount == 1) ? 0.25f : float(i) / float(rightCount - 1);
        const float deg = +(minAbsDeg + t * (maxAbsDeg - minAbsDeg));
        out[cursor++] = { PolarToWorld(anchorPos, heading, radius, deg), role, deg };
    }
}
} // namespace detail

// Generates deterministic role-stratified formation slots around anchorPos.
// Guarantees:
// 1. In PhalanxArc, the central [-12 deg, +12 deg] corridor directly ahead is kept clear so
//    skeletons do not block VR sightlines or Bone Spear / Curse aiming.
// 2. RangedArtillery (Skeleton Mages) are always positioned behind or inside the melee shield.
inline size_t GenerateFormationSlots(
    Vec2 anchorPos,
    Vec2 heading,
    ArmyFormation formation,
    ArmyStance stance,
    RoleCounts counts,
    std::span<FormationSlot> out) noexcept
{
    size_t cursor = 0;
    const bool tight = (stance == ArmyStance::Defensive);

    if (formation == ArmyFormation::PhalanxArc) {
        // Phalanx Wall: True multi-rank "Horse Wings" battle formation well in front of the player.
        // Geometry inspired by classic tactical wings (e.g. Celtic Kings Horse Wings):
        // 1. Commander (Player): Rear center anchor.
        // 2. Central Infantry Block:
        //    - Rear Ranks: Skeleton Mages (RangedArtillery / Archers) directly in front of the player.
        //      The player clearly sees their backs, and mages can fire over the frontlines.
        //    - Front Ranks: Skeleton Warriors (FrontlineMelee / Axe warriors) forming a tight shield wall.
        // 3. Flank Cavalry Wings (Alas de Caballería Laterales):
        //    - Left and Right Wings: Revives (HeavyFlanker) and Golem (TacticalAnchor) deployed in
        //      parallel columns on the lateral flanks of the infantry block, guarding the sides.
        const float forwardMagesBase = tight ? 4.4f : 5.0f;
        const uint8_t maxCols = 6;

        // Calculate infantry dimensions
        const uint8_t mageCols = std::min<uint8_t>(counts.ranged > 0 ? counts.ranged : 0, maxCols);
        const uint8_t meleeCols = std::min<uint8_t>(counts.melee > 0 ? counts.melee : 0, maxCols);
        const uint8_t infantryCols = std::max<uint8_t>({ mageCols, meleeCols, uint8_t(4) });
        const float centerHalfWidth = (float(infantryCols - 1) * 0.5f) * 1.25f;

        const float forwardMagesRow2 = (counts.ranged > maxCols) ? (forwardMagesBase + 1.20f) : forwardMagesBase;
        const float forwardMeleeBase = forwardMagesRow2 + (tight ? 2.2f : 2.6f);
        const float forwardMeleeRow2 = (counts.melee > maxCols) ? (forwardMeleeBase + 1.20f) : forwardMeleeBase;

        const auto appendSlot = [&](SummonRole role, float forwardDist, float lateralDist) noexcept {
            if (cursor >= out.size()) return;
            const float deg = std::atan2(lateralDist, forwardDist) * (180.0f / 3.14159265358979323846f);
            out[cursor++] = { detail::LocalToWorld(anchorPos, heading, forwardDist, lateralDist), role, deg };
        };

        // 1. RangedArtillery (Skeleton Mages / Archers): Rear ranks of the central infantry block
        if (counts.ranged > 0) {
            const uint8_t rank1Mages = std::min<uint8_t>(counts.ranged, maxCols);
            const uint8_t rank2Mages = uint8_t(counts.ranged - rank1Mages);
            for (uint8_t i = 0; i < rank1Mages; ++i) {
                const float lateral = (float(i) - float(rank1Mages - 1) * 0.5f) * 1.25f;
                appendSlot(SummonRole::RangedArtillery, forwardMagesBase, lateral);
            }
            for (uint8_t i = 0; i < rank2Mages; ++i) {
                const float lateral = (float(i) - float(rank2Mages - 1) * 0.5f) * 1.25f;
                appendSlot(SummonRole::RangedArtillery, forwardMagesBase + 1.20f, lateral);
            }
        }

        // 2. FrontlineMelee (Skeleton Warriors / Axe Warriors): Front ranks of the central infantry block
        if (counts.melee > 0) {
            const uint8_t rank1Melee = std::min<uint8_t>(counts.melee, maxCols);
            const uint8_t rank2Melee = uint8_t(counts.melee - rank1Melee);
            // Rank 1: Main front shield-wall
            for (uint8_t i = 0; i < rank1Melee; ++i) {
                const float lateral = (float(i) - float(rank1Melee - 1) * 0.5f) * 1.25f;
                appendSlot(SummonRole::FrontlineMelee, (rank2Melee > 0 ? forwardMeleeBase + 1.20f : forwardMeleeBase), lateral);
            }
            // Rank 2: Support melee rank behind Rank 1
            for (uint8_t i = 0; i < rank2Melee; ++i) {
                const float lateral = (float(i) - float(rank2Melee - 1) * 0.5f) * 1.25f;
                appendSlot(SummonRole::FrontlineMelee, forwardMeleeBase, lateral);
            }
        }

        // 3. Flank Cavalry Wings (TacticalAnchor / Golem and HeavyFlanker / Revives):
        // Deployed in neat 2-wide columns on the left and right flanks of the central block!
        const uint8_t totalWings = counts.anchors + counts.flankers;
        if (totalWings > 0) {
            const float wingLateralBase = centerHalfWidth + 1.8f;
            const float wingColSpacing = 1.35f;
            const float forwardWingStart = forwardMagesBase + 0.6f;
            const float forwardWingStep = 1.35f;

            const uint8_t leftWingTotal = (totalWings + 1) / 2;
            const uint8_t rightWingTotal = totalWings - leftWingTotal;

            // Distribute Anchors (Golem) and Flankers (Revives) into Left and Right wings
            uint8_t leftAnchors = (counts.anchors + 1) / 2;
            uint8_t rightAnchors = counts.anchors - leftAnchors;
            uint8_t leftFlankers = (leftWingTotal > leftAnchors) ? (leftWingTotal - leftAnchors) : 0;
            uint8_t rightFlankers = (rightWingTotal > rightAnchors) ? (rightWingTotal - rightAnchors) : 0;

            // Append Left Wing slots (lateral < 0)
            uint8_t leftSlotIdx = 0;
            for (uint8_t i = 0; i < leftAnchors; ++i, ++leftSlotIdx) {
                const uint8_t row = leftSlotIdx / 2;
                const uint8_t col = leftSlotIdx % 2;
                const float lateral = -(wingLateralBase + float(col) * wingColSpacing);
                const float fwd = forwardWingStart + float(row) * forwardWingStep;
                appendSlot(SummonRole::TacticalAnchor, fwd, lateral);
            }
            for (uint8_t i = 0; i < leftFlankers; ++i, ++leftSlotIdx) {
                const uint8_t row = leftSlotIdx / 2;
                const uint8_t col = leftSlotIdx % 2;
                const float lateral = -(wingLateralBase + float(col) * wingColSpacing);
                const float fwd = forwardWingStart + float(row) * forwardWingStep;
                appendSlot(SummonRole::HeavyFlanker, fwd, lateral);
            }

            // Append Right Wing slots (lateral > 0)
            uint8_t rightSlotIdx = 0;
            for (uint8_t i = 0; i < rightAnchors; ++i, ++rightSlotIdx) {
                const uint8_t row = rightSlotIdx / 2;
                const uint8_t col = rightSlotIdx % 2;
                const float lateral = +(wingLateralBase + float(col) * wingColSpacing);
                const float fwd = forwardWingStart + float(row) * forwardWingStep;
                appendSlot(SummonRole::TacticalAnchor, fwd, lateral);
            }
            for (uint8_t i = 0; i < rightFlankers; ++i, ++rightSlotIdx) {
                const uint8_t row = rightSlotIdx / 2;
                const uint8_t col = rightSlotIdx % 2;
                const float lateral = +(wingLateralBase + float(col) * wingColSpacing);
                const float fwd = forwardWingStart + float(row) * forwardWingStep;
                appendSlot(SummonRole::HeavyFlanker, fwd, lateral);
            }
        }

        return cursor;
    }

    if (formation == ArmyFormation::ProtectiveRing) {
        // Outer 360-degree bone wall: Anchors + Melee + Flankers
        const float outerRadius = tight ? 6.0f : 6.8f;
        const size_t outerTotal = size_t(counts.anchors) + size_t(counts.melee) + size_t(counts.flankers);
        size_t outerIdx = 0;

        auto emitOuter = [&](SummonRole role, uint8_t roleCount) noexcept {
            for (uint8_t i = 0; i < roleCount && cursor < out.size(); ++i, ++outerIdx) {
                // Start at +15 deg and sweep 330 deg so 0 deg (dead ahead) stays clear unless crowded
                const float span = (outerTotal <= 14) ? 330.0f : 360.0f;
                const float start = (outerTotal <= 14) ? 15.0f : 0.0f;
                const float deg = (outerTotal <= 1)
                    ? 15.0f
                    : (start + (span * float(outerIdx)) / float(outerTotal));
                const float normDeg = (deg > 180.0f) ? (deg - 360.0f) : deg;
                out[cursor++] = { detail::PolarToWorld(anchorPos, heading, outerRadius, normDeg), role, normDeg };
            }
        };
        emitOuter(SummonRole::TacticalAnchor, counts.anchors);
        emitOuter(SummonRole::FrontlineMelee, counts.melee);
        emitOuter(SummonRole::HeavyFlanker, counts.flankers);

        // Inner 360-degree artillery ring: Skeleton Mages shielded inside the outer wall
        const float innerRadius = tight ? 3.5f : 4.0f;
        for (uint8_t i = 0; i < counts.ranged && cursor < out.size(); ++i) {
            const float deg = (counts.ranged == 1)
                ? 180.0f
                : (20.0f + (320.0f * float(i)) / float(counts.ranged));
            const float normDeg = (deg > 180.0f) ? (deg - 360.0f) : deg;
            out[cursor++] = { detail::PolarToWorld(anchorPos, heading, innerRadius, normDeg), SummonRole::RangedArtillery, normDeg };
        }
        return cursor;
    }

    if (formation == ArmyFormation::SpearheadWedge) {
        // 1. TacticalAnchor at the tip of the spearhead (9.2 sub-tiles ahead)
        for (uint8_t i = 0; i < counts.anchors && cursor < out.size(); ++i) {
            const float rightOffset = (i == 0) ? 0.0f : ((i % 2 == 1) ? 1.6f : -1.6f) * float((i + 1) / 2);
            const float fwdOffset = 9.2f - float(i / 2) * 1.2f;
            const float deg = std::atan2(rightOffset, fwdOffset) * (180.0f / 3.14159265358979323846f);
            out[cursor++] = { detail::LocalToWorld(anchorPos, heading, fwdOffset, rightOffset), SummonRole::TacticalAnchor, deg };
        }

        // 2. FrontlineMelee along the two angled arms of the V (kept strictly ahead of the player)
        for (uint8_t i = 0; i < counts.melee && cursor < out.size(); ++i) {
            const int rank = int(i / 2) + 1;
            const float side = (i % 2 == 0) ? -1.0f : +1.0f;
            const float fwd = std::max(1.5f, 8.2f - float(rank) * 0.95f);
            const float right = side * (1.2f + float(rank) * 0.95f);
            const float deg = std::atan2(right, fwd) * (180.0f / 3.14159265358979323846f);
            out[cursor++] = { detail::LocalToWorld(anchorPos, heading, fwd, right), SummonRole::FrontlineMelee, deg };
        }

        // 3. HeavyFlanker extending the rear outer wings of the wedge
        const int baseRank = int((counts.melee + 1) / 2) + 1;
        for (uint8_t i = 0; i < counts.flankers && cursor < out.size(); ++i) {
            const int rank = baseRank + int(i / 2);
            const float side = (i % 2 == 0) ? -1.0f : +1.0f;
            const float fwd = std::max(1.0f, 8.2f - float(rank) * 0.95f);
            const float right = side * (1.5f + float(rank) * 1.0f);
            const float deg = std::atan2(right, fwd) * (180.0f / 3.14159265358979323846f);
            out[cursor++] = { detail::LocalToWorld(anchorPos, heading, fwd, right), SummonRole::HeavyFlanker, deg };
        }

        // 4. RangedArtillery forming a two-rank firing line sheltered inside the hollow of the V
        const uint8_t row1Count = std::min<uint8_t>(counts.ranged, 5);
        const uint8_t row2Count = uint8_t(counts.ranged - row1Count);
        for (uint8_t i = 0; i < row1Count && cursor < out.size(); ++i) {
            const float t = (row1Count == 1) ? 0.0f : (-1.0f + 2.0f * float(i) / float(row1Count - 1));
            const float right = t * 2.6f;
            const float fwd = 4.6f;
            const float deg = std::atan2(right, fwd) * (180.0f / 3.14159265358979323846f);
            out[cursor++] = { detail::LocalToWorld(anchorPos, heading, fwd, right), SummonRole::RangedArtillery, deg };
        }
        for (uint8_t i = 0; i < row2Count && cursor < out.size(); ++i) {
            const float t = (row2Count == 1) ? 0.0f : (-1.0f + 2.0f * float(i) / float(row2Count - 1));
            const float right = t * 3.2f;
            const float fwd = 2.8f;
            const float deg = std::atan2(right, fwd) * (180.0f / 3.14159265358979323846f);
            out[cursor++] = { detail::LocalToWorld(anchorPos, heading, fwd, right), SummonRole::RangedArtillery, deg };
        }
        return cursor;
    }

    // ArmyFormation::CorridorColumn: two side-rails at right = +-1.6 sub-tiles, keeping the
    // center lane open for the player and avoiding doorway congestion.
    int frontRailIdx = 0;
    auto emitFrontColumn = [&](SummonRole role, uint8_t count) noexcept {
        for (uint8_t i = 0; i < count && cursor < out.size(); ++i, ++frontRailIdx) {
            const float side = (frontRailIdx % 2 == 0) ? -1.6f : +1.6f;
            const float fwd = 3.2f + float(frontRailIdx / 2) * 1.6f;
            const float deg = std::atan2(side, fwd) * (180.0f / 3.14159265358979323846f);
            out[cursor++] = { detail::LocalToWorld(anchorPos, heading, fwd, side), role, deg };
        }
    };
    emitFrontColumn(SummonRole::TacticalAnchor, counts.anchors);
    emitFrontColumn(SummonRole::FrontlineMelee, counts.melee);

    int rearRailIdx = 0;
    auto emitRearColumn = [&](SummonRole role, uint8_t count) noexcept {
        for (uint8_t i = 0; i < count && cursor < out.size(); ++i, ++rearRailIdx) {
            const float side = (rearRailIdx % 2 == 0) ? -1.6f : +1.6f;
            const float fwd = -2.2f - float(rearRailIdx / 2) * 1.6f;
            const float deg = std::atan2(side, fwd) * (180.0f / 3.14159265358979323846f);
            out[cursor++] = { detail::LocalToWorld(anchorPos, heading, fwd, side), role, deg };
        }
    };
    emitRearColumn(SummonRole::RangedArtillery, counts.ranged);
    emitRearColumn(SummonRole::HeavyFlanker, counts.flankers);

    return cursor;
}

// Phase 3: Transverse corridor width sensor (probes perpendicular to heading at the player and 3 sub-tiles ahead).
// Returns effective corridor width in sub-tiles [0.0, 12.0].
template <class WalkablePred>
float MeasureCorridorWidth(Vec2 anchorPos, Vec2 heading, WalkablePred&& isWalkable) noexcept {
    auto measureAt = [&](float fwdOffset) noexcept -> float {
        const Vec2 origin = detail::LocalToWorld(anchorPos, heading, fwdOffset, 0.0f);
        float leftClear = 0.0f;
        for (int step = 1; step <= 6; ++step) {
            const Vec2 p = detail::LocalToWorld( origin, heading, 0.0f, -float(step) );
            if (!isWalkable(int(std::round(p.x)), int(std::round(p.y)))) break;
            leftClear = float(step);
        }
        float rightClear = 0.0f;
        for (int step = 1; step <= 6; ++step) {
            const Vec2 p = detail::LocalToWorld( origin, heading, 0.0f, +float(step) );
            if (!isWalkable(int(std::round(p.x)), int(std::round(p.y)))) break;
            rightClear = float(step);
        }
        return leftClear + rightClear;
    };
    const float w0 = measureAt(0.0f);
    const float wAhead = measureAt(3.0f);
    return std::min(w0, wAhead);
}

// Phase 3: Spiral walkable slot resolver. If (desiredX, desiredY) falls inside a wall or pillar,
// searches concentric rings (r = 1..3) and falls back toward anchorPos so no summon is ever ordered
// into unwalkable geometry.
template <class WalkablePred>
bool ResolveWalkableSlot(int desiredX, int desiredY, Vec2 anchorPos, int& outX, int& outY, WalkablePred&& isWalkable) noexcept {
    if (isWalkable(desiredX, desiredY)) {
        outX = desiredX;
        outY = desiredY;
        return true;
    }

    static constexpr int kRingOffsets[16][2] = {
        { -1,  0 }, {  1,  0 }, {  0, -1 }, {  0,  1 },
        { -1, -1 }, {  1, -1 }, { -1,  1 }, {  1,  1 },
        { -2,  0 }, {  2,  0 }, {  0, -2 }, {  0,  2 },
        { -2, -1 }, {  2, -1 }, { -2,  1 }, {  2,  1 },
    };

    float bestDistSq = 1e18f;
    int bestX = desiredX;
    int bestY = desiredY;
    bool found = false;

    for (size_t idx = 0; idx < 16; ++idx) {
        if (idx == 8 && found) break; // Prefer ring-1 neighbors before expanding to ring-2
        const int cx = desiredX + kRingOffsets[idx][0];
        const int cy = desiredY + kRingOffsets[idx][1];
        if (isWalkable(cx, cy)) {
            const float dx = float(cx) - anchorPos.x;
            const float dy = float(cy) - anchorPos.y;
            const float dSq = dx * dx + dy * dy;
            if (!found || dSq < bestDistSq) {
                bestDistSq = dSq;
                bestX = cx;
                bestY = cy;
                found = true;
            }
        }
    }
    if (found) {
        outX = bestX;
        outY = bestY;
        return true;
    }

    // Fallback: step halfway toward anchorPos, then directly to anchorPos
    const int midX = int(std::round((float(desiredX) + anchorPos.x) * 0.5f));
    const int midY = int(std::round((float(desiredY) + anchorPos.y) * 0.5f));
    if (isWalkable(midX, midY)) {
        outX = midX;
        outY = midY;
        return true;
    }
    const int ax = int(std::round(anchorPos.x));
    const int ay = int(std::round(anchorPos.y));
    if (isWalkable(ax, ay)) {
        outX = ax;
        outY = ay;
        return true;
    }
    return false;
}

struct ArmyUnitSample {
    uint32_t unitId   = 0;
    uint16_t classId  = 0;
    uint8_t  petType  = 0;
    uint8_t  animMode = 1; // 1 = MONMODE_NEUTRAL, 2 = MONMODE_WALK
    Vec2     pos{};
};

struct ArmyMoveOrder {
    uint32_t   unitId    = 0;
    uint8_t    petType   = 0;
    SummonRole role      = SummonRole::FrontlineMelee;
    uint8_t    slotIndex = 0;
    int        targetX   = 0;
    int        targetY   = 0;
};

// Coordinates slot generation, anti-swap persistent unit-to-slot assignment,
// arrival/release deadband hysteresis, corridor auto-compression, stance gatekeeping,
// and the staggered A* pathfinding budget.
struct SummonArmyCoordinator {
    static constexpr float ArrivalTolerance        = 1.25f; // sub-tiles: considered settled in slot
    static constexpr float ReleaseDeadbandBalanced = 2.40f; // sub-tiles: drift allowed before re-walking
    static constexpr float ReleaseDeadbandDef      = 1.85f; // tighter discipline in Defensive stance
    static constexpr float StickySlotMaxDist       = 14.0f; // preserve slot assignment within this radius
    static constexpr uint64_t MinRedispatchMs      = 300;   // per-unit cooldown between move dispatches
    static constexpr float CorridorEnterWidth      = 6.5f;  // compress to CorridorColumn below 6.5 sub-tiles
    static constexpr float CorridorExitWidth       = 8.5f;  // expand back above 8.5 sub-tiles
    static constexpr float RangedEngageRadius      = 20.0f; // Skeleton Mage firing radius
    static constexpr float RangedMinStandOff       = 5.5f;  // Mages hold firing stance inside [5.5, 18.0]
    static constexpr float SelfDefenseRadius       = 3.8f;  // Immediate melee self-defense radius
    static constexpr float UnstuckDistThreshold    = 28.0f; // sub-tiles before stuck timer starts
    static constexpr uint64_t UnstuckTimeoutMs     = 8000;  // 8s stuck behind wall -> relocate to slot

    struct TrackedUnit {
        uint32_t   unitId             = 0;
        SummonRole role               = SummonRole::FrontlineMelee;
        uint8_t    slotIndex          = 0xFF;
        bool       inSlot             = false;
        uint64_t   lastMoveDispatchMs = 0;
        int        lastTargetX        = 0;
        int        lastTargetY        = 0;
        Vec2       slotPos{};
        uint64_t   farSinceMs         = 0;
    };

    ArmyStance    activeStance      = ArmyStance::Balanced;
    ArmyFormation activeFormation   = ArmyFormation::PhalanxArc;
    bool          freeRoam          = false;
    bool          corridorActive    = false;
    bool          anchorActive      = false;
    bool          inCombat          = false;
    bool          forceReformNow    = false;
    Vec2          anchorPos{};
    uint32_t      priorityTargetId  = 0;
    Vec2          priorityTargetPos{};
    HeadingFilter headingFilter{};

    std::array<TrackedUnit, MaxArmyUnits> tracked{};
    size_t trackedCount = 0;
    size_t rrCursor     = 0;

    void Reset() noexcept {
        anchorActive = false;
        corridorActive = false;
        inCombat = false;
        forceReformNow = false;
        anchorPos = {};
        priorityTargetId = 0;
        priorityTargetPos = {};
        headingFilter.Reset();
        trackedCount = 0;
        rrCursor = 0;
    }

    void SetStanceAndFormation(ArmyStance stance, ArmyFormation formation) noexcept {
        freeRoam = false;
        activeStance = stance;
        activeFormation = formation;
        forceReformNow = true;
        for (size_t i = 0; i < trackedCount; ++i) {
            tracked[i].slotIndex = 0xFF;
            tracked[i].inSlot = false;
            tracked[i].lastMoveDispatchMs = 0;
        }
    }

    void AnchorAt(Vec2 pos) noexcept {
        freeRoam = false;
        anchorActive = true;
        anchorPos = pos;
        priorityTargetId = 0;
        forceReformNow = true;
        for (size_t i = 0; i < trackedCount; ++i) {
            tracked[i].inSlot = false;
            tracked[i].lastMoveDispatchMs = 0;
        }
    }

    void ClearAnchor() noexcept {
        if (anchorActive) {
            anchorActive = false;
            forceReformNow = true;
            for (size_t i = 0; i < trackedCount; ++i) {
                tracked[i].inSlot = false;
                tracked[i].lastMoveDispatchMs = 0;
            }
        }
    }

    void SetPriorityTarget(uint32_t targetId, Vec2 targetPos) noexcept {
        priorityTargetId = targetId;
        priorityTargetPos = targetPos;
    }

    void ClearPriorityTarget() noexcept {
        priorityTargetId = 0;
        priorityTargetPos = {};
    }

    template <class WalkablePred>
    bool UpdateCorridorState(Vec2 centerPos, Vec2 heading, WalkablePred&& isWalkable) noexcept {
        const float width = MeasureCorridorWidth(centerPos, heading, isWalkable);
        const bool prev = corridorActive;
        if (!corridorActive && width < CorridorEnterWidth) {
            corridorActive = true;
        } else if (corridorActive && width >= CorridorExitWidth) {
            corridorActive = false;
        }
        if (corridorActive != prev) {
            for (size_t i = 0; i < trackedCount; ++i) {
                tracked[i].slotIndex = 0xFF;
                tracked[i].inSlot = false;
                tracked[i].lastMoveDispatchMs = 0;
            }
        }
        return corridorActive;
    }

    bool FindTrackedUnit(uint32_t unitId, TrackedUnit& outUnit) const noexcept {
        if (!unitId) return false;
        for (size_t i = 0; i < trackedCount; ++i) {
            if (tracked[i].unitId == unitId) {
                outUnit = tracked[i];
                return true;
            }
        }
        return false;
    }

    bool IsTrackedUnit(uint32_t unitId) const noexcept {
        TrackedUnit unused{};
        return FindTrackedUnit(unitId, unused);
    }

    // Phase 4: Gatekeeper for native ServerStartWalk (0x445cc0), ServerStartRun (0x445db0),
    // and ServerMoveUnit (0x4a8a10) for managed summon army units.
    // Note: Only invoked when g_inCommandedMove is false (i.e. native MONAI random wander or chase).
    bool ShouldAllowSummonNativeMove(
        uint32_t unitId,
        Vec2 unitPos,
        Vec2 destPos,
        bool isTargetPlayer,
        bool hasEnemyTarget,
        uint32_t targetUnitId,
        Vec2 enemyPos) const noexcept
    {
        if (freeRoam) return true;

        TrackedUnit tu{};
        if (!FindTrackedUnit(unitId, tu)) return true;

        // 1. Priority Target focus active
        if (priorityTargetId != 0) {
            if (isTargetPlayer) return false;
            const float distToPriority = std::hypot(unitPos.x - priorityTargetPos.x, unitPos.y - priorityTargetPos.y);
            if (tu.role == SummonRole::RangedArtillery) {
                // Mages hold firing stance inside [RangedMinStandOff, 18.0] of the priority target
                if (distToPriority >= RangedMinStandOff && distToPriority <= 18.0f) {
                    return false;
                }
            }
            if (targetUnitId == priorityTargetId) return true;
            const float destDistToPriority = (destPos.x > 0.0f && destPos.y > 0.0f)
                ? std::hypot(destPos.x - priorityTargetPos.x, destPos.y - priorityTargetPos.y)
                : 9999.0f;
            return destDistToPriority <= distToPriority + 1.5f;
        }

        const bool hasSlot = (tu.slotPos.x != 0.0f || tu.slotPos.y != 0.0f);
        if (!hasSlot) return true;

        const float distUnitToSlot = std::hypot(unitPos.x - tu.slotPos.x, unitPos.y - tu.slotPos.y);
        const float distDestToSlot = (destPos.x > 0.0f && destPos.y > 0.0f)
            ? std::hypot(destPos.x - tu.slotPos.x, destPos.y - tu.slotPos.y)
            : 9999.0f;

        // 2. When NO hostile enemy is targeted on this native move call:
        // Allow ongoing path steps that head directly into the assigned formation slot,
        // while blocking all vanilla idle random wander or follow-player scatter away from the slot!
        if (!hasEnemyTarget) {
            if (!isTargetPlayer && distDestToSlot <= 1.8f) {
                return true;
            }
            if (!isTargetPlayer && !tu.inSlot && distDestToSlot + 1.0f < distUnitToSlot) {
                return true;
            }
            return (distUnitToSlot > 20.0f && distDestToSlot + 2.0f < distUnitToSlot);
        }

        // 3. RangedArtillery (Skeleton Mages): in Defensive or Balanced stance (or when anchored),
        // forbid walking into melee when an enemy is within firing range and mage is near its slot!
        if (tu.role == SummonRole::RangedArtillery &&
            (activeStance != ArmyStance::Aggressive || anchorActive)) {
            const float distToEnemy = std::hypot(unitPos.x - enemyPos.x, unitPos.y - enemyPos.y);
            if (distToEnemy <= RangedEngageRadius && distUnitToSlot <= 4.5f) {
                return false; // Hold slot and cast ranged bolts instead of walking toward monster!
            }
        }

        // 4. Defensive Stance or Anchored Formation: enforce strict formation leash
        if (activeStance == ArmyStance::Defensive || anchorActive) {
            const float leash = GetStanceLeashRadius(ArmyStance::Defensive, tu.role, true);
            const float enemyDistToSlot = std::hypot(enemyPos.x - tu.slotPos.x, enemyPos.y - tu.slotPos.y);
            return (tu.role != SummonRole::RangedArtillery && enemyDistToSlot <= leash);
        }

        // 5. Balanced Stance: allow melee engagement within the Balanced combat leash (14.0 sub-tiles)
        if (activeStance == ArmyStance::Balanced) {
            const float leash = GetStanceLeashRadius(ArmyStance::Balanced, tu.role, true);
            const float enemyDistToSlot = std::hypot(enemyPos.x - tu.slotPos.x, enemyPos.y - tu.slotPos.y);
            return enemyDistToSlot <= leash;
        }

        return true;
    }

    // Phase 4: Combat Target Arbiter for melee::Target (0x48fe20)
    bool ShouldAllowSummonCombatTarget(uint32_t unitId, Vec2 unitPos, Vec2 candidateEnemyPos) const noexcept {
        if (freeRoam) return true;

        TrackedUnit tu{};
        if (!FindTrackedUnit(unitId, tu)) return true;

        // Always allow immediate self-defense
        const float distToUnit = std::hypot(candidateEnemyPos.x - unitPos.x, candidateEnemyPos.y - unitPos.y);
        if (distToUnit <= SelfDefenseRadius) return true;

        const bool hasSlot = (tu.slotPos.x != 0.0f || tu.slotPos.y != 0.0f);
        if (!hasSlot) return true;

        const float distFromSlot = std::hypot(candidateEnemyPos.x - tu.slotPos.x, candidateEnemyPos.y - tu.slotPos.y);
        if (tu.role == SummonRole::RangedArtillery) {
            return distFromSlot <= RangedEngageRadius;
        }
        if (activeStance == ArmyStance::Defensive || anchorActive) {
            return distFromSlot <= GetStanceLeashRadius(ArmyStance::Defensive, tu.role, true);
        }
        if (activeStance == ArmyStance::Balanced) {
            return distFromSlot <= GetStanceLeashRadius(ArmyStance::Balanced, tu.role, true);
        }
        return true;
    }

    // Phase 4: Unstuck Watchdog for individual summons trapped behind walls > 28 sub-tiles for > 8s
    bool CheckArmyUnstuck(uint32_t unitId, Vec2 unitPos, Vec2 playerPos, uint64_t nowMs, Vec2& outRelocatePos) noexcept {
        const Vec2 refCenter = anchorActive ? anchorPos : playerPos;
        const float dist = std::hypot(unitPos.x - refCenter.x, unitPos.y - refCenter.y);
        for (size_t i = 0; i < trackedCount; ++i) {
            if (tracked[i].unitId != unitId) continue;
            if (dist <= UnstuckDistThreshold) {
                tracked[i].farSinceMs = 0;
                return false;
            }
            if (tracked[i].farSinceMs == 0) {
                tracked[i].farSinceMs = nowMs;
                return false;
            }
            if (nowMs >= tracked[i].farSinceMs && (nowMs - tracked[i].farSinceMs) >= UnstuckTimeoutMs) {
                tracked[i].farSinceMs = nowMs;
                outRelocatePos = (tracked[i].slotPos.x != 0.0f || tracked[i].slotPos.y != 0.0f)
                    ? tracked[i].slotPos
                    : refCenter;
                return true;
            }
            return false;
        }
        return false;
    }

    // Apply one of the 7 Virtual Commander Skills (3901..3907).
    bool ApplyCommanderSkill(int skillId, Vec2 fallbackAnchorPos) noexcept {
        switch (skillId) {
            case SkillPhalanxDef:
                ClearAnchor();
                ClearPriorityTarget();
                SetStanceAndFormation(ArmyStance::Defensive, ArmyFormation::PhalanxArc);
                return true;
            case SkillRingDef:
                ClearAnchor();
                ClearPriorityTarget();
                SetStanceAndFormation(ArmyStance::Defensive, ArmyFormation::ProtectiveRing);
                return true;
            case SkillMarchBal:
                ClearAnchor();
                ClearPriorityTarget();
                SetStanceAndFormation(ArmyStance::Balanced, ArmyFormation::PhalanxArc);
                return true;
            case SkillWedgeAgg:
                ClearAnchor();
                ClearPriorityTarget();
                SetStanceAndFormation(ArmyStance::Aggressive, ArmyFormation::SpearheadWedge);
                return true;
            case SkillHoldGround:
                AnchorAt(fallbackAnchorPos);
                return true;
            case SkillRegroup:
                freeRoam = false;
                ClearAnchor();
                ClearPriorityTarget();
                forceReformNow = true;
                for (size_t i = 0; i < trackedCount; ++i) {
                    tracked[i].inSlot = false;
                    tracked[i].lastMoveDispatchMs = 0;
                }
                return true;
            case SkillFallOut:
                freeRoam = true;
                ClearAnchor();
                ClearPriorityTarget();
                forceReformNow = false;
                trackedCount = 0;
                rrCursor = 0;
                return true;
            default:
                return false;
        }
    }

    // Steps the formation engine for the live army roster and emits up to MaxMovesPerTick
    // move orders for out-of-position units. Also writes the full resolved slot table into
    // outSlots (up to units.size()) for inspection/testing.
    size_t Step(
        std::span<const ArmyUnitSample> units,
        Vec2 playerPos,
        float rawForwardX,
        float rawForwardY,
        uint64_t nowMs,
        std::span<ArmyMoveOrder> outMoves,
        std::span<FormationSlot> outSlots = {}) noexcept
    {
        if (units.empty() || freeRoam) {
            trackedCount = 0;
            rrCursor = 0;
            return 0;
        }

        const size_t n = std::min(units.size(), MaxArmyUnits);
        const Vec2 heading = headingFilter.Update(playerPos, rawForwardX, rawForwardY);
        const Vec2 center = anchorActive ? anchorPos : playerPos;
        const ArmyFormation effectiveFormation = corridorActive ? ArmyFormation::CorridorColumn : activeFormation;

        // 1. Count roles in current live sample
        RoleCounts counts{};
        std::array<SummonRole, MaxArmyUnits> unitRoles{};
        for (size_t i = 0; i < n; ++i) {
            unitRoles[i] = ClassifySummonRole(units[i].petType, units[i].classId);
            switch (unitRoles[i]) {
                case SummonRole::TacticalAnchor:  ++counts.anchors;  break;
                case SummonRole::FrontlineMelee:  ++counts.melee;    break;
                case SummonRole::RangedArtillery: ++counts.ranged;   break;
                case SummonRole::HeavyFlanker:    ++counts.flankers; break;
            }
        }

        // 2. Generate formation slots
        std::array<FormationSlot, MaxArmyUnits> slots{};
        const size_t slotCount = GenerateFormationSlots(center, heading, effectiveFormation, activeStance, counts, std::span(slots.data(), n));
        if (!outSlots.empty()) {
            const size_t copyCount = std::min(outSlots.size(), slotCount);
            for (size_t i = 0; i < copyCount; ++i) outSlots[i] = slots[i];
        }

        // 3. Reconcile tracked state & preserve persistent sticky slots where valid
        std::array<TrackedUnit, MaxArmyUnits> nextTracked{};
        std::array<bool, MaxArmyUnits> slotTaken{};
        std::array<bool, MaxArmyUnits> unitAssigned{};

        for (size_t i = 0; i < n; ++i) {
            nextTracked[i].unitId = units[i].unitId;
            nextTracked[i].role = unitRoles[i];

            for (size_t oldIdx = 0; oldIdx < trackedCount; ++oldIdx) {
                if (tracked[oldIdx].unitId == units[i].unitId && tracked[oldIdx].role == unitRoles[i]) {
                    nextTracked[i].inSlot = tracked[oldIdx].inSlot;
                    nextTracked[i].lastMoveDispatchMs = tracked[oldIdx].lastMoveDispatchMs;
                    nextTracked[i].lastTargetX = tracked[oldIdx].lastTargetX;
                    nextTracked[i].lastTargetY = tracked[oldIdx].lastTargetY;
                    nextTracked[i].farSinceMs = tracked[oldIdx].farSinceMs;
                    const uint8_t prevSlot = tracked[oldIdx].slotIndex;
                    if (prevSlot < slotCount && !slotTaken[prevSlot] && slots[prevSlot].role == unitRoles[i]) {
                        const float dist = std::hypot(units[i].pos.x - slots[prevSlot].worldPos.x,
                                                      units[i].pos.y - slots[prevSlot].worldPos.y);
                        if (dist <= StickySlotMaxDist) {
                            nextTracked[i].slotIndex = prevSlot;
                            nextTracked[i].slotPos = slots[prevSlot].worldPos;
                            slotTaken[prevSlot] = true;
                            unitAssigned[i] = true;
                        }
                    }
                    break;
                }
            }
        }

        // 4. Greedily assign remaining unassigned units to nearest free slot of matching role
        for (size_t pass = 0; pass < n; ++pass) {
            float bestDistSq = 1e18f;
            size_t bestUnit = MaxArmyUnits;
            size_t bestSlot = MaxArmyUnits;

            for (size_t i = 0; i < n; ++i) {
                if (unitAssigned[i]) continue;
                for (size_t s = 0; s < slotCount; ++s) {
                    if (slotTaken[s] || slots[s].role != unitRoles[i]) continue;
                    const float dx = units[i].pos.x - slots[s].worldPos.x;
                    const float dy = units[i].pos.y - slots[s].worldPos.y;
                    const float dSq = dx * dx + dy * dy;
                    if (dSq < bestDistSq) {
                        bestDistSq = dSq;
                        bestUnit = i;
                        bestSlot = s;
                    }
                }
            }

            if (bestUnit < n && bestSlot < slotCount) {
                nextTracked[bestUnit].slotIndex = uint8_t(bestSlot);
                nextTracked[bestUnit].slotPos = slots[bestSlot].worldPos;
                slotTaken[bestSlot] = true;
                unitAssigned[bestUnit] = true;
            } else {
                break;
            }
        }

        // 5. Evaluate deadband hysteresis and select up to MaxMovesPerTick (4) move commands
        const float releaseDeadband = (activeStance == ArmyStance::Defensive)
            ? ReleaseDeadbandDef
            : ReleaseDeadbandBalanced;

        std::array<bool, MaxArmyUnits> needsMove{};
        for (size_t i = 0; i < n; ++i) {
            if (!unitAssigned[i] || nextTracked[i].slotIndex >= slotCount) continue;
            const auto& slot = slots[nextTracked[i].slotIndex];
            const float dist = std::hypot(units[i].pos.x - slot.worldPos.x, units[i].pos.y - slot.worldPos.y);

            if (dist <= ArrivalTolerance) {
                nextTracked[i].inSlot = true;
                continue;
            }
            if (nextTracked[i].inSlot && dist <= releaseDeadband) {
                continue;
            }
            nextTracked[i].inSlot = false;

            const int tx = int(std::round(slot.worldPos.x));
            const int ty = int(std::round(slot.worldPos.y));
            // Suppress duplicate move spam if unit is already walking (2) or running (15) toward the same sub-tile
            // and was dispatched recently.
            const bool sameDest = ( std::abs(tx - nextTracked[i].lastTargetX) <= 1 &&
                                    std::abs(ty - nextTracked[i].lastTargetY) <= 1 );
            if ((units[i].animMode == 2 || units[i].animMode == 15) && sameDest && nextTracked[i].lastMoveDispatchMs != 0 &&
                nowMs >= nextTracked[i].lastMoveDispatchMs &&
                (nowMs - nextTracked[i].lastMoveDispatchMs) < 1200) {
                continue;
            }
            if (nowMs >= nextTracked[i].lastMoveDispatchMs &&
                (nowMs - nextTracked[i].lastMoveDispatchMs) < MinRedispatchMs &&
                nextTracked[i].lastMoveDispatchMs != 0) {
                continue;
            }
            needsMove[i] = true;
        }

        const size_t maxEmit = std::min(outMoves.size(), MaxMovesPerTick);
        size_t emitted = 0;

        // Pass A: TacticalAnchor first, then round-robin across remaining roles
        if (rrCursor >= n) rrCursor = 0;
        for (int rolePriority = 0; rolePriority < 2 && emitted < maxEmit; ++rolePriority) {
            for (size_t step = 0; step < n && emitted < maxEmit; ++step) {
                const size_t idx = (rrCursor + step) % n;
                if (!needsMove[idx]) continue;
                const bool isAnchor = (unitRoles[idx] == SummonRole::TacticalAnchor);
                if ((rolePriority == 0) != isAnchor) continue;

                const auto& slot = slots[nextTracked[idx].slotIndex];
                const int tx = int(std::round(slot.worldPos.x));
                const int ty = int(std::round(slot.worldPos.y));
                outMoves[emitted++] = {
                    units[idx].unitId,
                    units[idx].petType,
                    unitRoles[idx],
                    nextTracked[idx].slotIndex,
                    tx,
                    ty
                };
                nextTracked[idx].lastMoveDispatchMs = nowMs;
                nextTracked[idx].lastTargetX = tx;
                nextTracked[idx].lastTargetY = ty;
                needsMove[idx] = false;
            }
        }

        rrCursor = (rrCursor + std::max<size_t>(1, emitted)) % n;
        tracked = nextTracked;
        trackedCount = n;
        return emitted;
    }
};

} // namespace framevr::commander
