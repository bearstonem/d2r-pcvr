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
        case SkillPhalanxDef: return "Army: Phalanx Wall";
        case SkillRingDef:    return "Army: Protective Ring";
        case SkillMarchBal:   return "Army: Marching Escort";
        case SkillWedgeAgg:   return "Army: Spearhead Assault";
        case SkillHoldGround: return "Army: Hold Ground";
        case SkillRegroup:    return "Army: Regroup";
        case SkillFallOut:    return "Army: Fall Out";
        default:              return "";
    }
}

// Returns native Necromancer skill ID 69 (Skeleton Mastery) for all commander skills
// so they share a unified, thematic skill icon in the VR wheel and pickers.
constexpr int CommanderSkillIconId(int id) noexcept {
    if (IsCommanderSkill(id)) return 69; // Skeleton Mastery
    return -1;
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

// The game's monster modes in which a summon is in a fight: hit (3), attacks (4, 5), block (6), cast (7),
// skills (8..11), knock-back (13) and sequence attacks (14). Standing is 1, walking 2, running 15.
constexpr bool IsFightMode(uint8_t mode) noexcept {
    return (mode >= 3 && mode <= 11) || mode == 13 || mode == 14;
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
        const float forwardMagesBase = tight ? 4.8f : 5.4f;
        const uint8_t maxCols = 5;
        const float colSpacing = 2.10f;
        const float rowSpacing = 2.10f;

        const auto appendSlot = [&](SummonRole role, float forwardDist, float lateralDist) noexcept {
            if (cursor >= out.size()) return;
            const float deg = std::atan2(lateralDist, forwardDist) * (180.0f / 3.14159265358979323846f);
            out[cursor++] = { detail::LocalToWorld(anchorPos, heading, forwardDist, lateralDist), role, deg };
        };

        // 1. RangedArtillery (Skeleton Mages / Archers): Rear ranks of the central infantry block
        const uint8_t mageRows = (counts.ranged > 0) ? uint8_t((counts.ranged + maxCols - 1) / maxCols) : 0;
        for (uint8_t i = 0; i < counts.ranged; ++i) {
            const uint8_t row = i / maxCols;
            const uint8_t col = i % maxCols;
            const uint8_t numInRow = std::min<uint8_t>(uint8_t(counts.ranged - row * maxCols), maxCols);
            const float lateral = (float(col) - float(numInRow - 1) * 0.5f) * colSpacing;
            const float fwd = forwardMagesBase + float(row) * rowSpacing;
            appendSlot(SummonRole::RangedArtillery, fwd, lateral);
        }

        // 2. FrontlineMelee (Skeleton Warriors / Axe Warriors): Front ranks of the central infantry block
        const float forwardMagesMax = (mageRows > 0)
            ? (forwardMagesBase + float(mageRows - 1) * rowSpacing)
            : (forwardMagesBase - rowSpacing);
        const float forwardMeleeBase = forwardMagesMax + (tight ? 2.6f : 3.0f);

        const uint8_t meleeRows = (counts.melee > 0) ? uint8_t((counts.melee + maxCols - 1) / maxCols) : 0;
        for (uint8_t i = 0; i < counts.melee; ++i) {
            const uint8_t row = i / maxCols;
            const uint8_t col = i % maxCols;
            const uint8_t numInRow = std::min<uint8_t>(uint8_t(counts.melee - row * maxCols), maxCols);
            const float lateral = (float(col) - float(numInRow - 1) * 0.5f) * colSpacing;
            // Place row 0 furthest forward, subsequent rows behind it
            const float fwd = forwardMeleeBase + float(meleeRows - 1 - row) * rowSpacing;
            appendSlot(SummonRole::FrontlineMelee, fwd, lateral);
        }

        // 3. Flank Cavalry Wings (TacticalAnchor / Golem and HeavyFlanker / Revives):
        // Deployed in neat 2-wide columns on the left and right flanks of the central block!
        const uint8_t totalInfantryCols = (counts.ranged >= maxCols || counts.melee >= maxCols)
            ? maxCols
            : std::max<uint8_t>({ counts.ranged, counts.melee, uint8_t(4) });
        const float centerHalfWidth = (float(totalInfantryCols - 1) * 0.5f) * colSpacing;

        const uint8_t totalWings = counts.anchors + counts.flankers;
        if (totalWings > 0) {
            const float wingLateralBase = centerHalfWidth + 2.60f;
            const float wingColSpacing = 2.40f;
            const float forwardWingStart = forwardMagesBase + 0.6f;
            const float forwardWingStep = 2.60f;

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
        // Outer 360-degree bone wall: Anchors + Melee + Flankers encircling the player.
        // Dynamic radii ensure generous chord separation between adjacent summons and a wide
        // radial separation (>= 4.20 sub-tiles) between the inner mage artillery tier and the outer melee shield.
        const size_t outerTotal = size_t(counts.anchors) + size_t(counts.melee) + size_t(counts.flankers);
        const float outerSpan = (outerTotal <= 14) ? 330.0f : 360.0f;
        const float outerStart = (outerTotal <= 14) ? 15.0f : 0.0f;
        const float outerStepRad = (outerTotal > 1) ? detail::DegToRad(outerSpan / float(outerTotal)) : 3.14159f;
        const float minOuterR = (outerTotal > 1)
            ? (2.20f / (2.0f * std::sin(outerStepRad * 0.5f)))
            : 8.6f;
        const float baseOuter = tight ? 8.6f : 9.6f;
        float outerRadius = std::max(baseOuter, minOuterR);

        const float innerSpan = (counts.ranged <= 6) ? 320.0f : 360.0f;
        const float innerStart = (counts.ranged <= 6) ? 20.0f : 0.0f;
        const float innerStepRad = (counts.ranged > 1) ? detail::DegToRad(innerSpan / float(counts.ranged)) : 3.14159f;
        const float minInnerR = (counts.ranged > 1)
            ? (2.20f / (2.0f * std::sin(innerStepRad * 0.5f)))
            : 4.2f;
        const float baseInner = tight ? 4.2f : 4.8f;
        const float innerRadius = std::max(baseInner, minInnerR);

        // Enforce a wide, clear radial moat (>= 4.20 sub-tiles) between inner mages and outer warriors
        if (outerRadius < innerRadius + 4.20f) {
            outerRadius = innerRadius + 4.20f;
        }

        size_t outerIdx = 0;
        auto emitOuter = [&](SummonRole role, uint8_t roleCount) noexcept {
            for (uint8_t i = 0; i < roleCount && cursor < out.size(); ++i, ++outerIdx) {
                const float deg = (outerTotal <= 1)
                    ? outerStart
                    : (outerStart + (outerSpan * float(outerIdx)) / float(outerTotal));
                const float normDeg = (deg > 180.0f) ? (deg - 360.0f) : deg;
                out[cursor++] = { detail::PolarToWorld(anchorPos, heading, outerRadius, normDeg), role, normDeg };
            }
        };
        emitOuter(SummonRole::TacticalAnchor, counts.anchors);
        emitOuter(SummonRole::FrontlineMelee, counts.melee);
        emitOuter(SummonRole::HeavyFlanker, counts.flankers);

        // Inner 360-degree artillery ring: Skeleton Mages shielded inside the outer wall
        for (uint8_t i = 0; i < counts.ranged && cursor < out.size(); ++i) {
            const float deg = (counts.ranged == 1)
                ? 180.0f
                : (innerStart + (innerSpan * float(i)) / float(counts.ranged));
            const float normDeg = (deg > 180.0f) ? (deg - 360.0f) : deg;
            out[cursor++] = { detail::PolarToWorld(anchorPos, heading, innerRadius, normDeg), SummonRole::RangedArtillery, normDeg };
        }
        return cursor;
    }

    if (formation == ArmyFormation::SpearheadWedge) {
        // Spearhead Assault Wedge: True penetrating inverted-V battle formation.
        // Advanced forward so all ranks remain comfortably in front of the player (fwd >= 4.7f),
        // giving the player a clear rear view of all minions' backs.
        // Geometry:
        // 1. Apex (Tip):
        //    - Led by TacticalAnchor (Golem) if present at (apexFwd, 0.0).
        //    - If no anchor is summoned, the foremost FrontlineMelee unit takes the apex so
        //      the spearhead is never headless or split open!
        // 2. Outer Chevron (Arms of the V):
        //    - FrontlineMelee units flank the apex and step back in a sharp 45-degree angle.
        //      Spacing: 1.50f fwd and 1.50f lateral -> Euclidean distance = 2.121f >= 2.05f.
        // 3. Inner Chevron (Sheltered Hollow):
        //    - RangedArtillery (Skeleton Mages) form an inner V sheltered safely inside the hollow,
        //      stepping back with an open sightline corridor (|lateral| >= 1.30f) to allow player VR aiming.
        // 4. Rear Flank Escorts:
        //    - HeavyFlanker (Revives) form powerful flanking guards reinforcing the outer wings.
        const float apexFwd = tight ? 14.5f : 16.0f;
        const bool hasAnchor = (counts.anchors > 0);

        const auto appendSlot = [&](SummonRole role, float forwardDist, float lateralDist) noexcept {
            if (cursor >= out.size()) return;
            const float deg = std::atan2(lateralDist, forwardDist) * (180.0f / 3.14159265358979323846f);
            out[cursor++] = { detail::LocalToWorld(anchorPos, heading, forwardDist, lateralDist), role, deg };
        };

        // 1. TacticalAnchor at apex (Golem)
        for (uint8_t i = 0; i < counts.anchors && cursor < out.size(); ++i) {
            const float fwd = apexFwd - float(i) * 2.2f;
            appendSlot(SummonRole::TacticalAnchor, fwd, 0.0f);
        }

        // 2. FrontlineMelee (Skeleton Warriors)
        uint8_t meleeArmStart = 0;
        if (!hasAnchor && counts.melee > 0) {
            // Frontline warrior takes the apex point when no Golem is active!
            appendSlot(SummonRole::FrontlineMelee, apexFwd, 0.0f);
            meleeArmStart = 1;
        }

        const uint8_t meleeOnArms = (counts.melee > meleeArmStart) ? uint8_t(counts.melee - meleeArmStart) : 0;
        for (uint8_t i = 0; i < meleeOnArms && cursor < out.size(); ++i) {
            const int rank = int(i / 2) + 1;
            const float side = (i % 2 == 0) ? -1.0f : +1.0f;
            const float fwd = apexFwd - float(rank) * 1.50f;
            const float lateral = side * (float(rank) * 1.50f);
            appendSlot(SummonRole::FrontlineMelee, fwd, lateral);
        }

        // 3. RangedArtillery (Skeleton Mages) forming an inner chevron inside the hollow
        const float innerApexFwd = apexFwd - 3.4f;
        for (uint8_t i = 0; i < counts.ranged && cursor < out.size(); ++i) {
            const int rank = int(i / 2) + 1;
            const float side = (i % 2 == 0) ? -1.0f : +1.0f;
            const float fwd = innerApexFwd - float(rank) * 1.50f;
            const float lateral = side * (1.30f + float(rank - 1) * 1.50f);
            appendSlot(SummonRole::RangedArtillery, fwd, lateral);
        }

        // 4. HeavyFlanker (Revives) along the rear-outer flank wings outside the melee arms
        const float maxMeleeRank = float((meleeOnArms + 1) / 2);
        const float maxMeleeLat = maxMeleeRank * 1.50f;
        const float wingBaseLat = std::max(maxMeleeLat + 2.60f, 8.50f);
        for (uint8_t i = 0; i < counts.flankers && cursor < out.size(); ++i) {
            const int rank = int(i / 2) + 1;
            const float side = (i % 2 == 0) ? -1.0f : +1.0f;
            const float fwd = (apexFwd - 3.2f) - float(rank - 1) * 2.20f;
            const float lateral = side * (wingBaseLat + float(rank - 1) * 1.80f);
            appendSlot(SummonRole::HeavyFlanker, fwd, lateral);
        }

        return cursor;
    }

    // ArmyFormation::CorridorColumn: two side-rails at right = +-2.2 sub-tiles, keeping the
    // center lane open for the player and avoiding doorway congestion.
    // Both rails march entirely forward ahead of the player (fwd >= 3.8f) with ranged units
    // in the near section and melee/anchors leading at the head of the column.
    const uint8_t rearCount = counts.ranged + counts.flankers;
    const uint8_t rearRows = (rearCount + 1) / 2;
    const float rearBaseFwd = 3.8f;
    const float railSpacing = 2.30f;
    const float frontBaseFwd = rearBaseFwd + float(rearRows) * railSpacing + 0.6f;

    int rearRailIdx = 0;
    auto emitRearColumn = [&](SummonRole role, uint8_t count) noexcept {
        for (uint8_t i = 0; i < count && cursor < out.size(); ++i, ++rearRailIdx) {
            const float side = (rearRailIdx % 2 == 0) ? -2.2f : +2.2f;
            const float fwd = rearBaseFwd + float(rearRailIdx / 2) * railSpacing;
            const float deg = std::atan2(side, fwd) * (180.0f / 3.14159265358979323846f);
            out[cursor++] = { detail::LocalToWorld(anchorPos, heading, fwd, side), role, deg };
        }
    };
    emitRearColumn(SummonRole::RangedArtillery, counts.ranged);
    emitRearColumn(SummonRole::HeavyFlanker, counts.flankers);

    int frontRailIdx = 0;
    auto emitFrontColumn = [&](SummonRole role, uint8_t count) noexcept {
        for (uint8_t i = 0; i < count && cursor < out.size(); ++i, ++frontRailIdx) {
            const float side = (frontRailIdx % 2 == 0) ? -2.2f : +2.2f;
            const float fwd = frontBaseFwd + float(frontRailIdx / 2) * railSpacing;
            const float deg = std::atan2(side, fwd) * (180.0f / 3.14159265358979323846f);
            out[cursor++] = { detail::LocalToWorld(anchorPos, heading, fwd, side), role, deg };
        }
    };
    emitFrontColumn(SummonRole::FrontlineMelee, counts.melee);
    emitFrontColumn(SummonRole::TacticalAnchor, counts.anchors);

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
    static constexpr float ArrivalTolerance        = 1.10f; // sub-tiles: considered settled in slot
    static constexpr float ReleaseDeadbandBalanced = 1.80f; // sub-tiles: drift allowed before re-walking
    static constexpr float ReleaseDeadbandDef      = 1.40f; // tighter discipline in Defensive stance
    static constexpr float StickySlotMaxDist       = 3.50f; // preserve slot assignment within this radius
    static constexpr uint64_t MinRedispatchMs      = 300;   // per-unit cooldown between move dispatches
    static constexpr float CorridorEnterWidth      = 6.5f;  // compress to CorridorColumn below 6.5 sub-tiles
    static constexpr float CorridorExitWidth       = 8.5f;  // expand back above 8.5 sub-tiles
    static constexpr float RangedEngageRadius      = 20.0f; // Skeleton Mage firing radius
    static constexpr float RangedMinStandOff       = 5.5f;  // Mages hold firing stance inside [5.5, 18.0]
    static constexpr float SelfDefenseRadius       = 3.8f;  // Immediate melee self-defense radius
    static constexpr float UnstuckDistThreshold    = 28.0f; // sub-tiles before stuck timer starts
    static constexpr uint64_t UnstuckTimeoutMs     = 8000;  // 8s stuck behind wall -> relocate to slot
    static constexpr uint64_t CongestionTimeoutMs  = 3500;  // 3.5s unable to reach slot due to minion traffic -> settle to slot
    // Fighting in formation (owner 2026-11-12: in a formation the summons "seem to not be attacking any monsters").
    // Step used to walk every summon more than the deadband (1.4-1.8 sub-tiles) from its spot back to it every
    // 300 ms, fight or not, and the walk order replaces whatever it was doing, a swing included: the game's AI took
    // it a step toward a monster, the next tick took it back. Now a summon within its stance's fight range of its
    // spot (GetStanceLeashRadius, the same range the native-move and target gates allow) is left to the game's AI
    // while it fights, and for FightLingerMs after (the pauses between swings), and while it walks during a fight
    // (maybe at a monster). Idle summons still go back to their spots, and a new order sends everyone to their
    // spots first: until it arrives, or FormingMs after the order.
    static constexpr uint64_t FightLingerMs        = 1500;
    static constexpr uint64_t FormingMs            = 4000;

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
        uint64_t   stuckSinceMs       = 0;
        uint64_t   lastFightMs        = 0;     // last Step that saw it in a fight mode (IsFightMode)
        bool       formed             = false; // reached its spot since the last order
    };

    ArmyStance    activeStance            = ArmyStance::Balanced;
    ArmyFormation activeFormation         = ArmyFormation::PhalanxArc;
    bool          freeRoam                = false;
    bool          pendingFallOutDispersal = false;
    bool          corridorActive          = false;
    bool          anchorActive            = false;
    bool          inCombat                = false;
    bool          forceReformNow          = false;
    Vec2          anchorPos{};
    uint32_t      priorityTargetId        = 0;
    Vec2          priorityTargetPos{};
    HeadingFilter headingFilter{};

    std::array<TrackedUnit, MaxArmyUnits> tracked{};
    size_t trackedCount = 0;
    size_t rrCursor     = 0;
    uint64_t orderedAtMs        = 0;     // the Step after the last order: its FormingMs window starts here
    bool     orderStampPending  = false; // an order since the last Step
    size_t   leftToFightLastStep = 0;    // summons Step left to the game's AI to fight (for the log)

    void Reset() noexcept {
        anchorActive = false;
        corridorActive = false;
        inCombat = false;
        forceReformNow = false;
        pendingFallOutDispersal = false;
        anchorPos = {};
        priorityTargetId = 0;
        priorityTargetPos = {};
        headingFilter.Reset();
        trackedCount = 0;
        rrCursor = 0;
        orderedAtMs = 0;
        orderStampPending = false;
        leftToFightLastStep = 0;
    }

    // An order: every summon goes to its (new) spot first, then may fight from there.
    void MarkNewOrder() noexcept {
        orderStampPending = true;
        for (size_t i = 0; i < trackedCount; ++i) tracked[i].formed = false;
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
            tracked[i].stuckSinceMs = 0;
        }
        MarkNewOrder();
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
            tracked[i].stuckSinceMs = 0;
        }
        MarkNewOrder();
    }

    void ClearAnchor() noexcept {
        if (anchorActive) {
            anchorActive = false;
            forceReformNow = true;
            for (size_t i = 0; i < trackedCount; ++i) {
                tracked[i].inSlot = false;
                tracked[i].lastMoveDispatchMs = 0;
                tracked[i].stuckSinceMs = 0;
            }
            MarkNewOrder();
        }
    }

    // How far from its spot a summon may be while it fights: the stance's leash, as the gates below use it
    // (Hold Ground holds to the Defensive one; a focused monster gets the Aggressive one).
    float FightRange(SummonRole role) const noexcept {
        if (priorityTargetId != 0) return GetStanceLeashRadius(ArmyStance::Aggressive, role, true);
        return GetStanceLeashRadius(anchorActive ? ArmyStance::Defensive : activeStance, role, true);
    }

    static bool FoughtLately(const TrackedUnit& tu, uint64_t nowMs) noexcept {
        return tu.lastFightMs != 0 && nowMs >= tu.lastFightMs && nowMs - tu.lastFightMs < FightLingerMs;
    }

    bool Forming(const TrackedUnit& tu, uint64_t nowMs) const noexcept {
        return !tu.formed && orderedAtMs != 0 && nowMs >= orderedAtMs && nowMs - orderedAtMs < FormingMs;
    }

    // Whether Step leaves a summon that is off its spot to the game's AI instead of walking it back.
    bool LeftToFight(const TrackedUnit& tu, uint8_t animMode, float distToSlot, uint64_t nowMs) const noexcept {
        if (distToSlot > FightRange(tu.role)) return false;   // strayed past its leash: back to the spot
        if (Forming(tu, nowMs)) return false;                 // a new order: to the spot first
        if (FoughtLately(tu, nowMs)) return true;             // fighting, or between swings
        return (inCombat || priorityTargetId != 0) && animMode == 2;   // walking in a fight: maybe at a monster
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
                tracked[i].stuckSinceMs = 0;
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

    // Phase 4: Unstuck Watchdog for individual summons:
    // 1. Trapped behind walls > 28 sub-tiles for > 8s -> relocate to slot or player
    // 2. Blocked by minion traffic/crowding and unable to reach assigned slot for >= 3.5s (out of combat)
    bool CheckArmyUnstuck(uint32_t unitId, Vec2 unitPos, Vec2 playerPos, uint64_t nowMs, Vec2& outRelocatePos) noexcept {
        const Vec2 refCenter = anchorActive ? anchorPos : playerPos;
        const float distToRef = std::hypot(unitPos.x - refCenter.x, unitPos.y - refCenter.y);
        for (size_t i = 0; i < trackedCount; ++i) {
            if (tracked[i].unitId != unitId) continue;

            // 1. Far stuck check (> 28 sub-tiles away behind walls for > 8s)
            if (distToRef > UnstuckDistThreshold) {
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
            tracked[i].farSinceMs = 0;

            // 2. Traffic congestion check: unit has an assigned slot, is NOT in slot, and has been
            // blocked/stuck trying to reach it for >= CongestionTimeoutMs (3.5s) while out of combat
            const bool hasSlot = (tracked[i].slotPos.x != 0.0f || tracked[i].slotPos.y != 0.0f);
            // (not a summon in a fight: by an anchor away from the hero inCombat, which counts hostiles near the hero, can be false)
            if (hasSlot && !tracked[i].inSlot && !inCombat && !freeRoam && !FoughtLately(tracked[i], nowMs)) {
                if (tracked[i].stuckSinceMs == 0) {
                    tracked[i].stuckSinceMs = nowMs;
                    return false;
                }
                if (nowMs >= tracked[i].stuckSinceMs && (nowMs - tracked[i].stuckSinceMs) >= CongestionTimeoutMs) {
                    tracked[i].stuckSinceMs = nowMs;
                    outRelocatePos = tracked[i].slotPos;
                    return true;
                }
                return false;
            }
            tracked[i].stuckSinceMs = 0;
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
                    tracked[i].stuckSinceMs = 0;
                }
                MarkNewOrder();
                return true;
            case SkillFallOut:
                freeRoam = true;
                pendingFallOutDispersal = true;
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
        leftToFightLastStep = 0;
        if (units.empty() || freeRoam) {
            trackedCount = 0;
            rrCursor = 0;
            return 0;
        }
        if (orderStampPending) {
            orderStampPending = false;
            orderedAtMs = nowMs ? nowMs : 1;
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
                    nextTracked[i].stuckSinceMs = tracked[oldIdx].stuckSinceMs;
                    nextTracked[i].lastFightMs = tracked[oldIdx].lastFightMs;
                    nextTracked[i].formed = tracked[oldIdx].formed;
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
            if (IsFightMode(units[i].animMode)) nextTracked[i].lastFightMs = nowMs;
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
        std::array<Vec2, MaxArmyUnits> navTargets{};

        for (size_t i = 0; i < n; ++i) {
            if (!unitAssigned[i] || nextTracked[i].slotIndex >= slotCount) continue;
            const auto& slot = slots[nextTracked[i].slotIndex];
            const float dist = std::hypot(units[i].pos.x - slot.worldPos.x, units[i].pos.y - slot.worldPos.y);

            if (dist <= ArrivalTolerance) {
                nextTracked[i].inSlot = true;
                nextTracked[i].formed = true;
                nextTracked[i].stuckSinceMs = 0;
                continue;
            }
            if (nextTracked[i].inSlot && dist <= releaseDeadband) {
                continue;
            }
            nextTracked[i].inSlot = false;
            if (LeftToFight(nextTracked[i], units[i].animMode, dist, nowMs)) {
                ++leftToFightLastStep;
                continue;
            }

            // Determine desired facing direction for this slot:
            // Phalanx, Wedge, and Column face forward along heading (backs visible to player);
            // ProtectiveRing faces radially outwards from the center (360-degree perimeter watch).
            Vec2 faceDir = heading;
            if (effectiveFormation == ArmyFormation::ProtectiveRing) {
                const float dx = slot.worldPos.x - center.x;
                const float dy = slot.worldPos.y - center.y;
                const float rLen = std::hypot(dx, dy);
                if (rLen > 1e-4f) faceDir = { dx / rLen, dy / rLen };
            }

            // Check if unit is significantly ahead of its slot along the facing vector.
            // If coming from far ahead (e.g. corpses ahead in battle), route through an approach-from-rear
            // waypoint behind the slot so that its final arrival step moves forward into the slot.
            // When already in the immediate vicinity (<= 2.5 sub-tiles), head directly into the slot
            // to avoid any hunting or back-and-forth oscillation.
            Vec2 navTarget = slot.worldPos;
            const float relX = units[i].pos.x - slot.worldPos.x;
            const float relY = units[i].pos.y - slot.worldPos.y;
            const float dotAhead = relX * faceDir.x + relY * faceDir.y;
            const float distToSlot = std::hypot(relX, relY);

            if (dotAhead > 2.0f && distToSlot > 2.5f) {
                navTarget = {
                    slot.worldPos.x - faceDir.x * 1.5f,
                    slot.worldPos.y - faceDir.y * 1.5f
                };
            }
            navTargets[i] = navTarget;

            const int tx = int(std::round(navTarget.x));
            const int ty = int(std::round(navTarget.y));
            // Suppress duplicate move spam if unit is already walking (2) or running (15) toward the same sub-tile
            // and was dispatched recently.
            const bool sameDest = (tx == nextTracked[i].lastTargetX && ty == nextTracked[i].lastTargetY);
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

                const auto& target = navTargets[idx];
                const int tx = int(std::round(target.x));
                const int ty = int(std::round(target.y));
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
