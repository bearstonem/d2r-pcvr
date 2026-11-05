#include "companions.h"
#include "summon_commander.h"
#include "interactions.h"
#include "glyph_trail.h"
#include "skill_usage.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace framevr;
using namespace framevr::commander;

namespace {

void TestCollectAlivePets() {
    constexpr uintptr_t base = 0x10000, player = base, data = base + 0x100, pets = base + 0x200, lists = base + 0x300;
    std::vector<uint8_t> memory(0x6000, 0);
    auto put = [&](uintptr_t p, const auto& value) {
        assert(p >= base && p - base + sizeof(value) <= memory.size());
        std::memcpy(memory.data() + p - base, &value, sizeof(value));
    };
    auto read = [&](uintptr_t p, auto& value) {
        if (p < base || p - base > memory.size() || sizeof(value) > memory.size() - (p - base)) return false;
        std::memcpy(&value, memory.data() + p - base, sizeof(value));
        return true;
    };

    uint32_t header[4] = { 0, 2, 42, 1 }; // Necromancer player
    put(player, header);
    put(player + 0x10, data);
    put(data + 0x98, pets);
    put(pets, lists);

    auto setList = [&](int type, uintptr_t head, int current, int maximum) {
        put(lists + type * 32, head);
        put(lists + type * 32 + 8, int32_t(current));
        put(lists + type * 32 + 12, int32_t(maximum));
    };
    auto setNode = [&](uintptr_t p, uint32_t id, uintptr_t next, uint32_t flags = 0) {
        put(p, flags);
        put(p + 4, id);
        put(p + 24, next);
    };

    const uintptr_t nGolem = base + 0x1000;
    const uintptr_t nSkel1 = nGolem + 32;
    const uintptr_t nSkel2 = nSkel1 + 32;
    const uintptr_t nSkelDead = nSkel2 + 32;
    const uintptr_t nMage1 = nSkelDead + 32;
    const uintptr_t nRevive1 = nMage1 + 32;

    setNode(nGolem, 501, 0, 0);
    setList(3, nGolem, 1, 1);

    setNode(nSkel1, 601, nSkel2, 0);
    setNode(nSkel2, 602, nSkelDead, 0);
    setNode(nSkelDead, 603, 0, 1); // dead flag bit 0 set -> should be skipped
    setList(4, nSkel1, 3, 8);

    setNode(nMage1, 701, 0, 0);
    setList(5, nMage1, 1, 8);

    setNode(nRevive1, 801, 0, 0);
    setList(6, nRevive1, 1, 10);

    std::array<companions::AlivePet, companions::MaxAlivePets> alive{};
    const size_t n = companions::CollectAlivePets(player, 22, companions::ArmyTypeMask, alive, read);
    assert(n == 5);
    assert(alive[0] == (companions::AlivePet{ 501, 3 }));
    assert(alive[1] == (companions::AlivePet{ 601, 4 }));
    assert(alive[2] == (companions::AlivePet{ 602, 4 }));
    assert(alive[3] == (companions::AlivePet{ 701, 5 }));
    assert(alive[4] == (companions::AlivePet{ 801, 6 }));

    // Cycle detection must return 0 safely
    setNode(nSkel2, 602, nSkel1, 0);
    assert(companions::CollectAlivePets(player, 22, companions::ArmyTypeMask, alive, read) == 0);
}

void TestRoleClassificationAndVirtualSkills() {
    assert(ClassifySummonRole(3) == SummonRole::TacticalAnchor);
    assert(ClassifySummonRole(15) == SummonRole::TacticalAnchor);
    assert(ClassifySummonRole(4) == SummonRole::FrontlineMelee);
    assert(ClassifySummonRole(11) == SummonRole::FrontlineMelee);
    assert(ClassifySummonRole(5) == SummonRole::RangedArtillery);
    assert(ClassifySummonRole(6) == SummonRole::HeavyFlanker);

    for (int id = SkillPhalanxDef; id <= SkillFallOut; ++id) {
        assert(IsCommanderSkill(id));
        assert(std::strlen(CommanderSkillName(id)) > 0);
        assert(CommanderSkillIconId(id) == 69); // All commander skills use Skeleton Mastery icon
    }
    assert(!IsCommanderSkill(70));
    assert(!IsCommanderSkill(9000));
}

void TestHeadingHysteresis() {
    HeadingFilter filter;
    const Vec2 origin{ 100.0f, 100.0f };
    Vec2 h = filter.Update(origin, 0.0f, -1.0f);
    assert(std::fabs(h.x - 0.0f) < 1e-4f && std::fabs(h.y - (-1.0f)) < 1e-4f);

    // 10-degree head turn while stationary must NOT change latched heading
    const float rad10 = 10.0f * (3.14159265f / 180.0f);
    h = filter.Update(origin, std::sin(rad10), -std::cos(rad10));
    assert(std::fabs(h.x - 0.0f) < 1e-4f && std::fabs(h.y - (-1.0f)) < 1e-4f);

    // 30-degree head turn (> 22.5 deg) while stationary MUST update latched heading
    const float rad30 = 30.0f * (3.14159265f / 180.0f);
    h = filter.Update(origin, std::sin(rad30), -std::cos(rad30));
    assert(std::fabs(h.x - std::sin(rad30)) < 1e-4f && std::fabs(h.y - (-std::cos(rad30))) < 1e-4f);

    // Walking >= 1.5 sub-tiles updates heading even for small angle changes
    const Vec2 walked{ 100.0f, 98.0f };
    h = filter.Update(walked, 0.0f, -1.0f);
    assert(std::fabs(h.x - 0.0f) < 1e-4f && std::fabs(h.y - (-1.0f)) < 1e-4f);
}

void TestPhalanxVisualCorridorAndStratification() {
    const Vec2 anchor{ 200.0f, 200.0f };
    const Vec2 heading{ 0.0f, -1.0f }; // North
    const RoleCounts counts{ 1, 10, 6, 4 }; // 1 Golem, 10 Warriors, 6 Mages, 4 Revives = 21 units

    std::array<FormationSlot, MaxArmyUnits> slots{};
    const size_t n = GenerateFormationSlots(anchor, heading, ArmyFormation::PhalanxArc, ArmyStance::Balanced, counts, slots);
    assert(n == counts.Total());

    float minMageDist = 999.0f, maxMageDist = 0.0f;
    float minMeleeDist = 999.0f, maxMeleeDist = 0.0f;
    size_t leftWingCount = 0, rightWingCount = 0;

    for (size_t i = 0; i < n; ++i) {
        // Every unit in Phalanx Wall must be well in front of the player (North = -Y)
        assert(slots[i].worldPos.y < anchor.y);
        const float forwardDist = anchor.y - slots[i].worldPos.y;
        const float lateralDist = slots[i].worldPos.x - anchor.x;

        if (slots[i].role == SummonRole::RangedArtillery) {
            // Central block rear rank: mages right in front of player
            assert(std::fabs(lateralDist) <= 4.5f);
            minMageDist = std::min(minMageDist, forwardDist);
            maxMageDist = std::max(maxMageDist, forwardDist);
        } else if (slots[i].role == SummonRole::FrontlineMelee) {
            // Central block front rank: warriors in front of mages
            assert(std::fabs(lateralDist) <= 4.5f);
            minMeleeDist = std::min(minMeleeDist, forwardDist);
            maxMeleeDist = std::max(maxMeleeDist, forwardDist);
        } else if (slots[i].role == SummonRole::HeavyFlanker || slots[i].role == SummonRole::TacticalAnchor) {
            // Flank Cavalry Wings (Alas de Caballería): Revives and Golem on lateral flanks!
            assert(std::fabs(lateralDist) >= 6.5f);
            if (lateralDist < 0.0f) {
                ++leftWingCount;
            } else {
                ++rightWingCount;
            }
        }
    }

    // Central block rank stratification:
    // Player -> [Mages (rear, closest)] -> [Warriors (frontline shield wall)]
    assert(minMageDist >= 5.0f && maxMageDist <= 8.0f);
    assert(minMeleeDist > maxMageDist + 2.0f);

    // Flank wings: 1 Golem + 4 Revives = 5 units distributed across left and right wings
    assert(leftWingCount + rightWingCount == 5);
    assert(leftWingCount == 3 && rightWingCount == 2);
}

void TestProtectiveRingWedgeAndColumn() {
    const Vec2 anchor{ 200.0f, 200.0f };
    const Vec2 heading{ 1.0f, 0.0f }; // East
    const RoleCounts counts{ 1, 8, 4, 2 };

    std::array<FormationSlot, MaxArmyUnits> slots{};

    // 1. ProtectiveRing: Encircles the anchor (player) with wide separation between inner mages and outer warriors.
    // Mages must have strictly smaller radius (< 5.0f) and outer ring must have radius > 8.0f (radial gap >= 4.20f).
    size_t n = GenerateFormationSlots(anchor, heading, ArmyFormation::ProtectiveRing, ArmyStance::Defensive, counts, slots);
    assert(n == counts.Total());
    for (size_t i = 0; i < n; ++i) {
        const float r = std::hypot(slots[i].worldPos.x - anchor.x, slots[i].worldPos.y - anchor.y);
        if (slots[i].role == SummonRole::RangedArtillery) {
            assert(r < 5.0f);
        } else {
            assert(r > 8.0f);
        }
    }

    // 2. SpearheadWedge: TacticalAnchor is furthest forward along +X heading
    n = GenerateFormationSlots(anchor, heading, ArmyFormation::SpearheadWedge, ArmyStance::Aggressive, counts, slots);
    assert(n == counts.Total());
    float anchorX = 0.0f;
    for (size_t i = 0; i < n; ++i) {
        if (slots[i].role == SummonRole::TacticalAnchor) anchorX = slots[i].worldPos.x;
    }
    for (size_t i = 0; i < n; ++i) {
        assert(slots[i].worldPos.x >= anchor.x + 4.5f); // All wedge slots well ahead of player
        if (slots[i].role != SummonRole::TacticalAnchor) {
            assert(slots[i].worldPos.x < anchorX);
        }
    }

    // 2b. SpearheadWedge without anchor: FrontlineMelee takes the apex at y == anchor.y
    RoleCounts countsNoAnchor{ 0, 8, 4, 2 };
    n = GenerateFormationSlots(anchor, heading, ArmyFormation::SpearheadWedge, ArmyStance::Aggressive, countsNoAnchor, slots);
    assert(n == countsNoAnchor.Total());
    float maxMeleeX = 0.0f;
    size_t apexIdx = 0;
    for (size_t i = 0; i < n; ++i) {
        if (slots[i].worldPos.x > maxMeleeX) {
            maxMeleeX = slots[i].worldPos.x;
            apexIdx = i;
        }
    }
    assert(slots[apexIdx].role == SummonRole::FrontlineMelee);
    assert(std::fabs(slots[apexIdx].worldPos.y - anchor.y) < 1e-3f);

    // 3. CorridorColumn: Center walking lane (|y - anchor.y| < 1.8) must be completely clear
    n = GenerateFormationSlots(anchor, heading, ArmyFormation::CorridorColumn, ArmyStance::Balanced, counts, slots);
    assert(n == counts.Total());
    for (size_t i = 0; i < n; ++i) {
        assert(slots[i].worldPos.x >= anchor.x + 3.5f); // Both rails march entirely ahead of player
        const float lateralDist = std::fabs(slots[i].worldPos.y - anchor.y);
        assert(lateralDist >= 2.0f && lateralDist <= 2.4f);
    }
}

void TestCoordinatorAntiSwapAndStaggeredBudget() {
    SummonArmyCoordinator coord;
    const Vec2 playerPos{ 500.0f, 500.0f };

    // Create 1 Golem + 8 Skeletons + 4 Mages far from their slots
    std::vector<ArmyUnitSample> army;
    army.push_back({ 100, 289, 3, 1, { 485.0f, 510.0f } }); // Golem
    for (uint32_t i = 0; i < 8; ++i) {
        army.push_back({ 200 + i, 363, 4, 1, { 490.0f + float(i), 512.0f } }); // Warriors
    }
    for (uint32_t i = 0; i < 4; ++i) {
        army.push_back({ 300 + i, 364, 5, 1, { 492.0f + float(i), 515.0f } }); // Mages
    }

    std::array<ArmyMoveOrder, MaxMovesPerTick> moves{};
    std::array<FormationSlot, MaxArmyUnits> slots{};

    // Tick 1 (t = 1000ms): Emits move orders for the whole squad (13 <= MaxMovesPerTick 32), starting with the Golem (TacticalAnchor)
    size_t emitted = coord.Step(army, playerPos, 0.0f, -1.0f, 1000, moves, slots);
    assert(emitted == army.size());
    assert(moves[0].unitId == 100 && moves[0].role == SummonRole::TacticalAnchor);

    // Record initial slot assignments
    std::array<uint8_t, MaxArmyUnits> initialSlots{};
    for (size_t i = 0; i < army.size(); ++i) {
        initialSlots[i] = coord.tracked[i].slotIndex;
        assert(initialSlots[i] < army.size());
    }

    // Move all units directly onto their assigned slots
    for (size_t i = 0; i < army.size(); ++i) {
        army[i].pos = slots[initialSlots[i]].worldPos;
    }

    // Tick 2 (t = 2000ms): All units have arrived within ArrivalTolerance (0.0 <= 0.85) -> 0 move orders!
    emitted = coord.Step(army, playerPos, 0.0f, -1.0f, 2000, moves, slots);
    assert(emitted == 0);
    for (size_t i = 0; i < army.size(); ++i) {
        assert(coord.tracked[i].inSlot);
        assert(coord.tracked[i].slotIndex == initialSlots[i]); // Zero slot swapping!
    }

    // Drift units by 1.2 sub-tiles (< ReleaseDeadbandBalanced 1.60) -> still 0 move orders!
    for (size_t i = 0; i < army.size(); ++i) {
        army[i].pos.x += 1.2f;
    }
    emitted = coord.Step(army, playerPos, 0.0f, -1.0f, 2500, moves, slots);
    assert(emitted == 0);

    // Virtual Skill 3901 (Army: Phalanx Wall Def) tightens stance to Defensive (ReleaseDeadbandDef = 1.30)
    assert(coord.ApplyCommanderSkill(SkillPhalanxDef, playerPos));
    assert(coord.activeStance == ArmyStance::Defensive);
    assert(coord.activeFormation == ArmyFormation::PhalanxArc);

    // Virtual Skill 3905 (Army: Hold / Anchor Here) anchors formation at target spot
    const Vec2 holdSpot{ 512.0f, 488.0f };
    assert(coord.ApplyCommanderSkill(SkillHoldGround, holdSpot));
    assert(coord.anchorActive);
    assert(std::fabs(coord.anchorPos.x - holdSpot.x) < 1e-4f);

    // Virtual Skill 3902 (Army: Protective Ring Def) clears anchor and forms ring around player
    assert(coord.ApplyCommanderSkill(SkillRingDef, playerPos));
    assert(!coord.anchorActive);
    assert(coord.activeFormation == ArmyFormation::ProtectiveRing);

    // Re-anchor then test Virtual Skill 3906 (Army: Regroup Now) clears anchor
    assert(coord.ApplyCommanderSkill(SkillHoldGround, holdSpot));
    assert(coord.anchorActive);
    assert(coord.ApplyCommanderSkill(SkillRegroup, playerPos));
    assert(!coord.anchorActive);

    // Virtual Skill 3907 (Army: Fall Out) breaks ranks and restores free roam (0 formation moves, native AI allowed)
    assert(coord.ApplyCommanderSkill(SkillFallOut, playerPos));
    assert(coord.freeRoam);
    assert(coord.Step(army, playerPos, 0.0f, -1.0f, 3500, moves, slots) == 0);
    assert(coord.ShouldAllowSummonNativeMove(200, army[1].pos, { 999.0f, 999.0f }, false, false, 0, {}));
    assert(coord.ShouldAllowSummonCombatTarget(200, army[1].pos, { 999.0f, 999.0f }));

    // Re-activating any formation skill (e.g., 3903 Army: Marching Escort) resumes formation control
    assert(coord.ApplyCommanderSkill(SkillMarchBal, playerPos));
    assert(!coord.freeRoam);
}

void TestCorridorCompressionAndWalkableSpiral() {
    SummonArmyCoordinator coord;
    const Vec2 center{ 100.0f, 100.0f };
    const Vec2 heading{ 0.0f, -1.0f }; // North (-Y), so lateral right is +X

    // 1. Narrow Maggot Lair corridor: only |x - 100| <= 2 is walkable (width = 4.0 < 6.5)
    auto narrowWalkable = [](int x, int y) {
        (void)y;
        return std::abs(x - 100) <= 2;
    };
    assert(coord.UpdateCorridorState(center, heading, narrowWalkable));
    assert(coord.corridorActive);

    // 2. Medium room (|x - 100| <= 3 -> width = 6.0 < 8.5): hysteresis keeps corridorActive true
    auto mediumWalkable = [](int x, int y) {
        (void)y;
        return std::abs(x - 100) <= 3;
    };
    assert(coord.UpdateCorridorState(center, heading, mediumWalkable));

    // 3. Open room (|x - 100| <= 6 -> width = 12.0 >= 8.5): exits corridor column mode
    auto openWalkable = [](int x, int y) {
        (void)x; (void)y;
        return true;
    };
    assert(!coord.UpdateCorridorState(center, heading, openWalkable));
    assert(!coord.corridorActive);

    // 4. ResolveWalkableSlot: if (105, 95) is inside a pillar, spiral finds nearest valid neighbor
    int rx = 0, ry = 0;
    const bool resolved = ResolveWalkableSlot(105, 95, center, rx, ry, [](int x, int y) {
        return !(x == 105 && y == 95);
    });
    assert(resolved);
    assert(!(rx == 105 && ry == 95));
    assert(std::abs(rx - 105) <= 1 && std::abs(ry - 95) <= 1);
}

void TestStanceGatekeeperAndArmyWatchdog() {
    SummonArmyCoordinator coord;
    coord.SetStanceAndFormation(ArmyStance::Defensive, ArmyFormation::PhalanxArc);

    const Vec2 playerPos{ 300.0f, 300.0f };
    std::vector<ArmyUnitSample> army = {
        { 101, 289, 3, 1, { 300.0f, 294.0f } }, // Golem (Anchor)
        { 201, 363, 4, 1, { 297.0f, 295.0f } }, // Skeleton Warrior (Melee)
        { 301, 364, 5, 1, { 297.0f, 302.0f } }, // Skeleton Mage (Ranged)
    };
    std::array<ArmyMoveOrder, MaxMovesPerTick> moves{};
    std::array<FormationSlot, MaxArmyUnits> slots{};
    (void)coord.Step(army, playerPos, 0.0f, -1.0f, 1000, moves, slots);

    // Snap units to their assigned slots
    for (size_t i = 0; i < army.size(); ++i) {
        army[i].pos = coord.tracked[i].slotPos;
    }
    (void)coord.Step(army, playerPos, 0.0f, -1.0f, 1500, moves, slots);

    const Vec2 warriorSlot = coord.tracked[1].slotPos;
    const Vec2 mageSlot = coord.tracked[2].slotPos;

    // 1. Skeleton Mage in Defensive/Balanced stance must NOT walk toward an enemy within 20 sub-tiles
    const Vec2 enemyNear{ mageSlot.x, mageSlot.y - 10.0f };
    assert(!coord.ShouldAllowSummonNativeMove(301, mageSlot, enemyNear, false, true, 999, enemyNear));
    // But Mage IS allowed to attack that enemy with ranged bolts!
    assert(coord.ShouldAllowSummonCombatTarget(301, mageSlot, enemyNear));

    // 2. Skeleton Warrior in Defensive stance:
    //    - Allowed to step and attack an enemy inside the 7.5 sub-tile leash
    const Vec2 closeEnemy{ warriorSlot.x, warriorSlot.y - 5.0f };
    assert(coord.ShouldAllowSummonNativeMove(201, warriorSlot, closeEnemy, false, true, 998, closeEnemy));
    assert(coord.ShouldAllowSummonCombatTarget(201, warriorSlot, closeEnemy));

    //    - Blocked from chasing a distant enemy 15 sub-tiles away (outside 7.5 Defensive leash)
    const Vec2 farEnemy{ warriorSlot.x, warriorSlot.y - 15.0f };
    assert(!coord.ShouldAllowSummonNativeMove(201, warriorSlot, farEnemy, false, true, 997, farEnemy));
    assert(!coord.ShouldAllowSummonCombatTarget(201, warriorSlot, farEnemy));

    // 3. Unstuck Watchdog: if Warrior is trapped > 28 sub-tiles away for >= 8000ms, triggers relocation
    const Vec2 trappedPos{ 340.0f, 340.0f };
    Vec2 relocPos{};
    assert(!coord.CheckArmyUnstuck(201, trappedPos, playerPos, 2000, relocPos)); // starts timer
    assert(!coord.CheckArmyUnstuck(201, trappedPos, playerPos, 9000, relocPos)); // 7000ms < 8000ms
    assert(coord.CheckArmyUnstuck(201, trappedPos, playerPos, 10000, relocPos)); // 8000ms reached!
    assert(std::fabs(relocPos.x - warriorSlot.x) < 1e-4f && std::fabs(relocPos.y - warriorSlot.y) < 1e-4f);

    // 4. Congestion Watchdog: if Warrior is near slot but body-blocked (!inSlot) for >= 3500ms out of combat, triggers relocation
    coord.tracked[1].inSlot = false;
    const Vec2 nearPos{ warriorSlot.x + 2.0f, warriorSlot.y }; // dist to player is small (~10 < 28)
    assert(!coord.CheckArmyUnstuck(201, nearPos, playerPos, 11000, relocPos)); // starts congestion timer
    assert(!coord.CheckArmyUnstuck(201, nearPos, playerPos, 13000, relocPos)); // 2000ms < 3500ms
    assert(coord.CheckArmyUnstuck(201, nearPos, playerPos, 14600, relocPos));  // 3600ms >= 3500ms reached!
    assert(std::fabs(relocPos.x - warriorSlot.x) < 1e-4f && std::fabs(relocPos.y - warriorSlot.y) < 1e-4f);
}

void TestCommanderUiAndGlyphIntegration() {
    using namespace framevr;
    // 1. Category routing: Summoning tab for Necromancer (3) and Druid (1), General (4) for others
    for (int id = SkillPhalanxDef; id <= SkillFallOut; ++id) {
        assert(skillcategories::Category(2, id) == 3);
        assert(skillcategories::Category(5, id) == 1);
        assert(skillcategories::Category(3, id) == skillcategories::General);
        assert(glyphtrail::ElementLook(id) != glyphtrail::Look::Drawing);
        assert(skillusage::Line(id, 0)[0] != '\0');
        assert(!skillusage::Details(id, 0).empty());
    }

    // 2. Snapshot with Raise Skeleton (70) exposes all 7 Virtual Commander Skills in button & glyph pickers
    interaction::Snapshot s{};
    s.session = 1;
    s.at = 100;
    s.skillClass = 2; // Necromancer
    s.bindingsEditable = true;
    s.bindingBank = 0;
    s.catalog[0].id = 70; // Raise Skeleton
    s.catalog[0].known = true;
    s.catalog[0].available = true;
    std::snprintf(s.catalog[0].name, sizeof s.catalog[0].name, "Raise Skeleton");
    s.skillCount = 1;
    assert(interaction::HasSummonArmy(s));

    for (int id = SkillPhalanxDef; id <= SkillFallOut; ++id) {
        const auto gs = interaction::GlyphSkill(s, id);
        assert(gs.id == id && gs.known && gs.available);
        interaction::Request req{};
        req.action = interaction::Action::BindSkill;
        req.session = 1;
        req.at = 100;
        req.index = 0;
        req.bank = 0;
        req.skill = id;
        req.skillOwner = -1;
        req.previousSkill = -1;
        req.previousOwner = -1;
        assert(interaction::CanBind(s, req, 100));
    }
}

void TestApproachFromRearOrientationWaypoint() {
    SummonArmyCoordinator coord;
    const Vec2 playerPos{ 500.0f, 500.0f };
    const Vec2 heading{ 0.0f, -1.0f }; // North (-Y)

    // Slot 0 for warrior will be North of player (Y < 500.0f)
    // Place a warrior far ahead (North) of its formation slot (e.g. spawned from corpse ahead)
    std::vector<ArmyUnitSample> army = {
        { 201, 363, 4, 1, { 500.0f, 480.0f } } // Y = 480 is ahead of slot (~490) along North heading
    };

    std::array<ArmyMoveOrder, MaxMovesPerTick> moves{};
    std::array<FormationSlot, MaxArmyUnits> slots{};
    size_t emitted = coord.Step(army, playerPos, heading.x, heading.y, 1000, moves, slots);
    assert(emitted == 1);

    const auto& slot = slots[moves[0].slotIndex];
    // Since warrior was ahead of slot along heading, the move order target must be BEHIND the slot
    // (i.e. target.y > slot.worldPos.y since heading is North = -Y)
    const float expectedRearY = slot.worldPos.y - heading.y * 1.5f; // slot.y + 1.5f
    assert(moves[0].targetY == int(std::round(expectedRearY)));
    assert(moves[0].targetX == int(std::round(slot.worldPos.x)));

    // Now place warrior behind the slot (e.g. it has reached the rear waypoint)
    army[0].pos = { slot.worldPos.x, slot.worldPos.y + 1.5f };
    emitted = coord.Step(army, playerPos, heading.x, heading.y, 2000, moves, slots);
    assert(emitted == 1);
    // Target is now directly the formation slot! Final arrival step will move forward into slot!
    assert(moves[0].targetX == int(std::round(slot.worldPos.x)));
    assert(moves[0].targetY == int(std::round(slot.worldPos.y)));
}

void TestFormationMinimumSeparation() {
    const Vec2 anchor{ 100.0f, 100.0f };
    const Vec2 heading{ 0.0f, -1.0f };
    const ArmyFormation formations[] = {
        ArmyFormation::PhalanxArc,
        ArmyFormation::ProtectiveRing,
        ArmyFormation::SpearheadWedge,
        ArmyFormation::CorridorColumn
    };
    const ArmyStance stances[] = {
        ArmyStance::Defensive,
        ArmyStance::Balanced,
        ArmyStance::Aggressive
    };
    const RoleCounts compositions[] = {
        { 1, 10, 6, 4 }, // Full endgame army: 21 minions
        { 0, 8, 4, 0 },  // No golem, no revives
        { 1, 4, 2, 1 },  // Small midgame army
        { 1, 12, 8, 6 }, // Huge army: 27 minions
        { 1, 16, 12, 8 } // Massive army: 37 minions
    };

    std::array<FormationSlot, MaxArmyUnits> slots{};
    for (auto formation : formations) {
        for (auto stance : stances) {
            for (const auto& counts : compositions) {
                const size_t n = GenerateFormationSlots(anchor, heading, formation, stance, counts, slots);
                assert(n == counts.Total());
                for (size_t i = 0; i < n; ++i) {
                    for (size_t j = i + 1; j < n; ++j) {
                        const float dist = std::hypot(slots[i].worldPos.x - slots[j].worldPos.x,
                                                      slots[i].worldPos.y - slots[j].worldPos.y);
                        assert(dist >= 2.05f);
                    }
                }
            }
        }
    }
}

} // namespace

int main() {
    TestCollectAlivePets();
    TestRoleClassificationAndVirtualSkills();
    TestHeadingHysteresis();
    TestPhalanxVisualCorridorAndStratification();
    TestProtectiveRingWedgeAndColumn();
    TestCoordinatorAntiSwapAndStaggeredBudget();
    TestCorridorCompressionAndWalkableSpiral();
    TestStanceGatekeeperAndArmyWatchdog();
    TestCommanderUiAndGlyphIntegration();
    TestApproachFromRearOrientationWaypoint();
    TestFormationMinimumSeparation();
    std::puts("Summon commander: pet extraction, role stratification, +-12 deg sightline, anti-swap slots, corridor compression, stance gatekeeper and virtual skills passed");
    return 0;
}

