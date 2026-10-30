#pragma once
#include <cstdint>
#include <cstddef>
#include <span>
#include <vector>
#include "skill_categories.h"

namespace framevr::companionai {

// Virtual Player Classes for mercenaries (reflecting lore, archetype, and animations)
enum class VirtualPlayerClass : int8_t {
    None = -1,
    Amazon = 0,     // Act 1 Rogue Scout (MonStats 271)
    Sorceress = 1,  // Act 3 Iron Wolf (MonStats 359)
    Paladin = 3,    // Act 2 Desert Guard (MonStats 338)
    Barbarian = 4   // Act 5 Barbarian (MonStats 560, 561)
};

// MonStats rows for native hirelings
namespace MonStatsHireling {
    inline constexpr uint16_t Rogue = 271;
    inline constexpr uint16_t Desert = 338;
    inline constexpr uint16_t IronWolf = 359;
    inline constexpr uint16_t Barbarian1 = 560;
    inline constexpr uint16_t Barbarian2 = 561;
}

// Maps native MonStats classId to virtual player character class
inline constexpr VirtualPlayerClass GetVirtualPlayerClass(uint16_t monStatsClassId) {
    switch (monStatsClassId) {
        case MonStatsHireling::Rogue:      return VirtualPlayerClass::Amazon;
        case MonStatsHireling::Desert:     return VirtualPlayerClass::Paladin;
        case MonStatsHireling::IronWolf:   return VirtualPlayerClass::Sorceress;
        case MonStatsHireling::Barbarian1:
        case MonStatsHireling::Barbarian2: return VirtualPlayerClass::Barbarian;
        default:                           return VirtualPlayerClass::None;
    }
}

// Diablo II standard ItemStatCost property IDs
namespace ItemStat {
    inline constexpr int ClassSkills = 83;      // item_addclassskills (param = ClassId 0..6)
    inline constexpr int NonClassSkill = 97;    // item_nonclassskill (oskills, param = SkillId)
    inline constexpr int SingleSkill = 107;     // item_singleskill (staffmods, param = SkillId)
    inline constexpr int AllSkills = 127;       // item_allskills
    inline constexpr int TabSkills = 188;       // item_addskill_tab (param = TabId 0..20)
    inline constexpr int ChargedSkill = 204;    // item_charged_skill (param = SkillId)
}

// Resolves character class (0..6) to which a skill ID belongs (-1 if not a standard class skill)
inline constexpr int GetSkillClass(int skillId) {
    if (skillId >= 6 && skillId <= 35) return 0;   // Amazon (6..35)
    if (skillId >= 36 && skillId <= 65) return 1;  // Sorceress (36..65)
    if (skillId >= 66 && skillId <= 95) return 2;  // Necromancer (66..95)
    if (skillId >= 96 && skillId <= 125) return 3; // Paladin (96..125)
    if (skillId >= 126 && skillId <= 155) return 4;// Barbarian (126..155)
    if (skillId >= 221 && skillId <= 250) return 5;// Druid (221..250)
    if (skillId >= 251 && skillId <= 280) return 6;// Assassin (251..280)
    return -1;
}

// Resolves global tab index (0..20) for item_addskill_tab (Stat 188)
inline int GetSkillTab(int skillId) {
    const int cls = GetSkillClass(skillId);
    if (cls < 0 || !skillcategories::ValidClass(cls)) return -1;

    const int cat = skillcategories::Category(cls, skillId);
    if (cat < 1 || cat > 3) return -1;

    const int tabOffset = cat - 1; // 0, 1, or 2
    return (cls * 3) + tabOffset;
}

// Determines if a skill matches a companion's virtual class
inline bool IsSkillMatchingVirtualClass(VirtualPlayerClass vclass, int skillId) {
    if (vclass == VirtualPlayerClass::None || skillId <= 0) return false;
    const int skillCls = GetSkillClass(skillId);
    if (skillCls == int(vclass)) return true;

    // Special case: Act 2 Desert Guard uses Jab (skill 10) in baseline hireling skills
    if (vclass == VirtualPlayerClass::Paladin && skillId == 10) return true;

    return false;
}

// Computes the total matching equipment skill bonus for a companion's skill
template<class GetStatFn>
inline int ComputeCompanionEquipmentSkillBonus(
    VirtualPlayerClass vclass,
    int skillId,
    std::span<const uintptr_t> equippedItems,
    GetStatFn getStat)
{
    if (vclass == VirtualPlayerClass::None || skillId <= 0) return 0;

    const int skillCls = GetSkillClass(skillId);
    const int tabId = GetSkillTab(skillId);
    const int vclsInt = int(vclass);

    int totalBonus = 0;

    for (uintptr_t item : equippedItems) {
        if (!item) continue;

        // 1. Single skill bonus (Staffmod, Stat 107) and Oskills (Stat 97)
        const int singleBonus = getStat(item, ItemStat::SingleSkill, skillId);
        if (singleBonus > 0) totalBonus += singleBonus;

        const int nonClassBonus = getStat(item, ItemStat::NonClassSkill, skillId);
        if (nonClassBonus > 0) totalBonus += nonClassBonus;

        // 2. Class skill bonus (Stat 83: item_addclassskills)
        if (skillCls == vclsInt) {
            const int classBonus = getStat(item, ItemStat::ClassSkills, vclsInt);
            if (classBonus > 0) totalBonus += classBonus;
        } else if (vclass == VirtualPlayerClass::Paladin && skillId == 10) {
            // Act 2 Guard Jab with Amazon gear
            const int amaBonus = getStat(item, ItemStat::ClassSkills, 0);
            if (amaBonus > 0) totalBonus += amaBonus;
        }

        // 3. Tab skill bonus (Stat 188: item_addskill_tab)
        if (tabId >= 0) {
            const int tabBonus = getStat(item, ItemStat::TabSkills, tabId);
            if (tabBonus > 0) totalBonus += tabBonus;
        }
    }

    return totalBonus;
}

// Computes effective skill level including virtual class gear bonuses
template<class GetStatFn>
inline int ComputeEffectiveCompanionSkillLevel(
    int baseLevel,
    VirtualPlayerClass vclass,
    int skillId,
    std::span<const uintptr_t> equippedItems,
    GetStatFn getStat,
    bool featureEnabled = true)
{
    if (!featureEnabled || vclass == VirtualPlayerClass::None || skillId <= 0) {
        return baseLevel;
    }

    // Direct grants (Stat 107 single skill / staffmod, Stat 97 non-class skill / oskill)
    int directGrant = 0;
    for (uintptr_t item : equippedItems) {
        if (!item) continue;
        const int s = getStat(item, ItemStat::SingleSkill, skillId);
        if (s > 0) directGrant += s;
        const int nc = getStat(item, ItemStat::NonClassSkill, skillId);
        if (nc > 0) directGrant += nc;
    }

    // In Diablo II, class skills (+X to Class) and tab skills (+X to Tree) never unlock
    // a skill if the unit's baseLevel is 0 and the skill is not granted directly by an item.
    if (baseLevel <= 0 && directGrant <= 0) {
        return 0;
    }

    const int bonus = ComputeCompanionEquipmentSkillBonus(vclass, skillId, equippedItems, getStat);
    return baseLevel + bonus;
}

} // namespace framevr::companionai

