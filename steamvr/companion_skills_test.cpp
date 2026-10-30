#include "companion_skills.h"
#include <cassert>
#include <cmath>
#include <map>
#include <tuple>
#include <vector>

namespace {
using namespace framevr::companionai;

struct MockItem {
    uintptr_t id = 0;
    // Map of (statId, param) -> value
    std::map<std::pair<int, int>, int> stats;

    void SetStat(int statId, int param, int value) {
        stats[{statId, param}] = value;
    }

    int GetStat(int statId, int param) const {
        auto it = stats.find({statId, param});
        return it != stats.end() ? it->second : 0;
    }
};

} // namespace

int main() {
    // 1. Virtual Class Mapping Tests
    assert(GetVirtualPlayerClass(MonStatsHireling::Rogue) == VirtualPlayerClass::Amazon);
    assert(GetVirtualPlayerClass(MonStatsHireling::Desert) == VirtualPlayerClass::Paladin);
    assert(GetVirtualPlayerClass(MonStatsHireling::IronWolf) == VirtualPlayerClass::Sorceress);
    assert(GetVirtualPlayerClass(MonStatsHireling::Barbarian1) == VirtualPlayerClass::Barbarian);
    assert(GetVirtualPlayerClass(MonStatsHireling::Barbarian2) == VirtualPlayerClass::Barbarian);
    assert(GetVirtualPlayerClass(1) == VirtualPlayerClass::None);    // Fallen
    assert(GetVirtualPlayerClass(267) == VirtualPlayerClass::None);  // Blood Raven
    assert(GetVirtualPlayerClass(544) == VirtualPlayerClass::None);  // Baal

    // 2. Skill Class and Tab Identification Tests
    // Amazon: Class 0
    assert(GetSkillClass(6) == 0);  // Magic Arrow
    assert(GetSkillClass(7) == 0);  // Fire Arrow
    assert(GetSkillClass(9) == 0);  // Critical Strike
    assert(GetSkillClass(10) == 0); // Jab
    assert(GetSkillClass(22) == 0); // Guided Arrow
    assert(GetSkillClass(26) == 0); // Strafe
    assert(GetSkillClass(33) == 0); // Pierce

    assert(GetSkillTab(6) == 0);  // Magic Arrow -> Bow (tab 0)
    assert(GetSkillTab(7) == 0);  // Fire Arrow -> Bow (tab 0)
    assert(GetSkillTab(12) == 0); // Multiple Shot -> Bow (tab 0)
    assert(GetSkillTab(22) == 0); // Guided Arrow -> Bow (tab 0)
    assert(GetSkillTab(26) == 0); // Strafe -> Bow (tab 0)

    assert(GetSkillTab(8) == 1);  // Inner Sight -> Passive (tab 1)
    assert(GetSkillTab(9) == 1);  // Critical Strike -> Passive (tab 1)
    assert(GetSkillTab(23) == 1); // Penetrate -> Passive (tab 1)
    assert(GetSkillTab(33) == 1); // Pierce -> Passive (tab 1)

    assert(GetSkillTab(10) == 2); // Jab -> Javelin & Spear (tab 2)

    // Sorceress: Class 1
    assert(GetSkillClass(36) == 1); // Fire Bolt
    assert(GetSkillClass(37) == 1); // Warmth
    assert(GetSkillClass(47) == 1); // Fire Ball
    assert(GetSkillClass(55) == 1); // Glacial Spike
    assert(GetSkillClass(59) == 1); // Blizzard
    assert(GetSkillClass(61) == 1); // Fire Mastery
    assert(GetSkillClass(65) == 1); // Cold Mastery

    assert(GetSkillTab(36) == 3); // Fire Bolt -> Fire (tab 3)
    assert(GetSkillTab(37) == 3); // Warmth -> Fire (tab 3)
    assert(GetSkillTab(47) == 3); // Fire Ball -> Fire (tab 3)
    assert(GetSkillTab(61) == 3); // Fire Mastery -> Fire (tab 3)

    assert(GetSkillTab(38) == 4); // Charged Bolt -> Lightning (tab 4)
    assert(GetSkillTab(48) == 4); // Nova -> Lightning (tab 4)
    assert(GetSkillTab(49) == 4); // Lightning -> Lightning (tab 4)
    assert(GetSkillTab(53) == 4); // Chain Lightning -> Lightning (tab 4)
    assert(GetSkillTab(54) == 4); // Teleport -> Lightning (tab 4)

    assert(GetSkillTab(40) == 5); // Frozen Armor -> Cold (tab 5)
    assert(GetSkillTab(45) == 5); // Ice Blast -> Cold (tab 5)
    assert(GetSkillTab(55) == 5); // Glacial Spike -> Cold (tab 5)
    assert(GetSkillTab(59) == 5); // Blizzard -> Cold (tab 5)
    assert(GetSkillTab(65) == 5); // Cold Mastery -> Cold (tab 5)

    // Paladin: Class 3
    assert(GetSkillClass(98) == 3);  // Might
    assert(GetSkillClass(99) == 3);  // Prayer
    assert(GetSkillClass(104) == 3); // Defiance
    assert(GetSkillTab(98) == 10);   // Might -> Offensive Auras (tab 10)
    assert(GetSkillTab(99) == 11);   // Prayer -> Defensive Auras (tab 11)
    assert(GetSkillTab(104) == 11);  // Defiance -> Defensive Auras (tab 11)

    // Barbarian: Class 4
    assert(GetSkillClass(126) == 4); // Bash
    assert(GetSkillClass(138) == 4); // Shout
    assert(GetSkillClass(145) == 4); // Iron Skin
    assert(GetSkillClass(147) == 4); // Frenzy
    assert(GetSkillTab(126) == 12);  // Bash -> Combat (tab 12)
    assert(GetSkillTab(138) == 14);  // Shout -> Warcries (tab 14)
    assert(GetSkillTab(145) == 13);  // Iron Skin -> Masteries (tab 13)
    assert(GetSkillTab(147) == 12);  // Frenzy -> Combat (tab 12)

    // 3. Skill Matching Virtual Class
    assert(IsSkillMatchingVirtualClass(VirtualPlayerClass::Amazon, 7));   // Fire Arrow
    assert(IsSkillMatchingVirtualClass(VirtualPlayerClass::Amazon, 9));   // Critical Strike
    assert(!IsSkillMatchingVirtualClass(VirtualPlayerClass::Amazon, 47)); // Fire Ball
    assert(!IsSkillMatchingVirtualClass(VirtualPlayerClass::Amazon, 126));// Bash

    assert(IsSkillMatchingVirtualClass(VirtualPlayerClass::Sorceress, 47)); // Fire Ball
    assert(IsSkillMatchingVirtualClass(VirtualPlayerClass::Sorceress, 55)); // Glacial Spike
    assert(IsSkillMatchingVirtualClass(VirtualPlayerClass::Sorceress, 37)); // Warmth
    assert(!IsSkillMatchingVirtualClass(VirtualPlayerClass::Sorceress, 7)); // Fire Arrow

    assert(IsSkillMatchingVirtualClass(VirtualPlayerClass::Paladin, 98));  // Might
    assert(IsSkillMatchingVirtualClass(VirtualPlayerClass::Paladin, 10));  // Jab (special case for Act 2 merc)
    assert(!IsSkillMatchingVirtualClass(VirtualPlayerClass::Paladin, 47)); // Fire Ball

    assert(IsSkillMatchingVirtualClass(VirtualPlayerClass::Barbarian, 126)); // Bash
    assert(IsSkillMatchingVirtualClass(VirtualPlayerClass::Barbarian, 147)); // Frenzy
    assert(IsSkillMatchingVirtualClass(VirtualPlayerClass::Barbarian, 138)); // Iron Skin
    assert(!IsSkillMatchingVirtualClass(VirtualPlayerClass::Barbarian, 98));  // Might

    // 4. Equipment Skill Bonus Calculation Tests
    std::vector<MockItem> mockItems(4);
    for (size_t i = 0; i < mockItems.size(); ++i) {
        mockItems[i].id = 0x1000 + i;
    }

    auto getStatLambda = [&](uintptr_t itemPtr, int statId, int param) -> int {
        for (const auto& item : mockItems) {
            if (item.id == itemPtr) {
                return item.GetStat(statId, param);
            }
        }
        return 0;
    };

    uintptr_t itemPtrs[4] = { mockItems[0].id, mockItems[1].id, mockItems[2].id, mockItems[3].id };
    std::span<const uintptr_t> equipSpan(itemPtrs, 4);

    // Scenario A: Rogue (Amazon) with Amazon Gear
    // Item 0 (Bow): +2 to Amazon Skills (Stat 83, param 0), +3 to Bow & Crossbow (Stat 188, param 0), +1 to Fire Arrow (Stat 107, param 7)
    mockItems[0].SetStat(ItemStat::ClassSkills, 0, 2);
    mockItems[0].SetStat(ItemStat::TabSkills, 0, 3);
    mockItems[0].SetStat(ItemStat::SingleSkill, 7, 1);

    // Item 1 (Circlet): +1 to Amazon Skills (Stat 83, param 0)
    mockItems[1].SetStat(ItemStat::ClassSkills, 0, 1);

    // Test Fire Arrow (ID 7, base level 15):
    // Total bonus should be: Class (+2 from bow +1 from circlet = 3) + Tab (+3 from bow) + Single (+1 from bow) = +7!
    const int rogueFireBonus = ComputeCompanionEquipmentSkillBonus(VirtualPlayerClass::Amazon, 7, equipSpan, getStatLambda);
    assert(rogueFireBonus == 7);
    assert(ComputeEffectiveCompanionSkillLevel(15, VirtualPlayerClass::Amazon, 7, equipSpan, getStatLambda, true) == 22);

    // Test Critical Strike (ID 9, Passive tab 1, base level 4):
    // Bow has tab 0 (Bow & Crossbow), so tab bonus does NOT apply. Single bonus is for skill 7, so it does NOT apply.
    // Class bonus (+3 from bow and circlet) DOES apply!
    const int rogueCritBonus = ComputeCompanionEquipmentSkillBonus(VirtualPlayerClass::Amazon, 9, equipSpan, getStatLambda);
    assert(rogueCritBonus == 3);
    assert(ComputeEffectiveCompanionSkillLevel(4, VirtualPlayerClass::Amazon, 9, equipSpan, getStatLambda, true) == 7);

    // Scenario B: Feature Disabled Check
    // When feature is disabled, effective level must equal base level strictly
    assert(ComputeEffectiveCompanionSkillLevel(15, VirtualPlayerClass::Amazon, 7, equipSpan, getStatLambda, false) == 15);
    assert(ComputeEffectiveCompanionSkillLevel(4, VirtualPlayerClass::Amazon, 9, equipSpan, getStatLambda, false) == 4);

    // Scenario C: Iron Wolf (Sorceress) with Sorceress Gear
    for (auto& item : mockItems) item.stats.clear();

    // Item 0 (Sword): +2 to All Sorceress Skills (Stat 83, param 1)
    mockItems[0].SetStat(ItemStat::ClassSkills, 1, 2);
    // Item 1 (Shield): +1 to Fire Skills (Stat 188, param 3)
    mockItems[1].SetStat(ItemStat::TabSkills, 3, 1);
    // Item 2 (Armor): Ormus' Robes with +3 to Fire Ball (Stat 107, param 47)
    mockItems[2].SetStat(ItemStat::SingleSkill, 47, 3);
    // Item 3 (Helm): +1 to Cold Skills (Stat 188, param 5)
    mockItems[3].SetStat(ItemStat::TabSkills, 5, 1);

    // Fire Ball (47, Fire tab 3):
    // Bonus = +2 (Sorc class) + 1 (Fire tab) + 3 (Fire Ball staffmod) = +6!
    const int wolfFireBonus = ComputeCompanionEquipmentSkillBonus(VirtualPlayerClass::Sorceress, 47, equipSpan, getStatLambda);
    assert(wolfFireBonus == 6);
    assert(ComputeEffectiveCompanionSkillLevel(12, VirtualPlayerClass::Sorceress, 47, equipSpan, getStatLambda, true) == 18);

    // Glacial Spike (55, Cold tab 5):
    // Bonus = +2 (Sorc class) + 1 (Cold tab from helm) + 0 (Fire tab does not apply, Fire Ball staffmod does not apply) = +3!
    const int wolfColdBonus = ComputeCompanionEquipmentSkillBonus(VirtualPlayerClass::Sorceress, 55, equipSpan, getStatLambda);
    assert(wolfColdBonus == 3);
    assert(ComputeEffectiveCompanionSkillLevel(8, VirtualPlayerClass::Sorceress, 55, equipSpan, getStatLambda, true) == 11);

    // Warmth (37, Fire tab 3, passive):
    // Bonus = +2 (Sorc class) + 1 (Fire tab) = +3!
    const int wolfWarmthBonus = ComputeCompanionEquipmentSkillBonus(VirtualPlayerClass::Sorceress, 37, equipSpan, getStatLambda);
    assert(wolfWarmthBonus == 3);

    // Scenario D: Cross-class gear mismatch
    // If Iron Wolf equips Rogue's Amazon bow (+2 Amazon, +3 Bow, +1 Fire Arrow),
    // bonuses to Sorceress skills (Fire Ball, Glacial Spike) must be 0!
    for (auto& item : mockItems) item.stats.clear();
    mockItems[0].SetStat(ItemStat::ClassSkills, 0, 2);
    mockItems[0].SetStat(ItemStat::TabSkills, 0, 3);
    mockItems[0].SetStat(ItemStat::SingleSkill, 7, 1);

    assert(ComputeCompanionEquipmentSkillBonus(VirtualPlayerClass::Sorceress, 47, equipSpan, getStatLambda) == 0);
    assert(ComputeCompanionEquipmentSkillBonus(VirtualPlayerClass::Sorceress, 55, equipSpan, getStatLambda) == 0);
    assert(ComputeEffectiveCompanionSkillLevel(12, VirtualPlayerClass::Sorceress, 47, equipSpan, getStatLambda, true) == 12);

    // Scenario E: Desert Guard (Paladin) using Jab (skill 10) with Amazon and Paladin gear
    for (auto& item : mockItems) item.stats.clear();
    mockItems[0].SetStat(ItemStat::ClassSkills, 3, 2);  // +2 Paladin Skills
    mockItems[0].SetStat(ItemStat::TabSkills, 10, 3);   // +3 Offensive Auras (Might tab 10)
    mockItems[1].SetStat(ItemStat::SingleSkill, 10, 2); // +2 to Jab
    mockItems[2].SetStat(ItemStat::ClassSkills, 0, 1);  // +1 to Amazon Skills

    // Might (98, Paladin tab 10):
    // Bonus = +2 (Paladin class) + 3 (Offensive Auras tab) = +5!
    assert(ComputeCompanionEquipmentSkillBonus(VirtualPlayerClass::Paladin, 98, equipSpan, getStatLambda) == 5);
    assert(ComputeEffectiveCompanionSkillLevel(10, VirtualPlayerClass::Paladin, 98, equipSpan, getStatLambda, true) == 15);

    // Jab (10):
    // Bonus = +2 (single skill) + 1 (Amazon class gear on Desert Guard special case) = +3!
    assert(ComputeCompanionEquipmentSkillBonus(VirtualPlayerClass::Paladin, 10, equipSpan, getStatLambda) == 3);
    assert(ComputeEffectiveCompanionSkillLevel(8, VirtualPlayerClass::Paladin, 10, equipSpan, getStatLambda, true) == 11);

    return 0;
}

