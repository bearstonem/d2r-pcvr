#include "companion_charges.h"
#include <cassert>
#include <cstdio>
#include <vector>

using namespace framevr::companionai;

namespace {

void TestStat204Packing() {
    // 1. Standard skill (< 256): Teleport (54, level 1, 20/20 charges)
    {
        const uint16_t param = PackChargedSkillParam(54, 1);
        const int32_t val = PackChargedSkillValue(20, 20);
        const auto skill = UnpackChargedSkill(101, 4, param, val);

        assert(skill.itemId == 101);
        assert(skill.bodyLocation == 4);
        assert(skill.skillId == 54);
        assert(skill.level == 1);
        assert(skill.currentCharges == 20);
        assert(skill.maxCharges == 20);
    }

    // 2. Curse: Life Tap (82, level 5, 90/90 charges)
    {
        const uint16_t param = PackChargedSkillParam(82, 5);
        const int32_t val = PackChargedSkillValue(90, 90);
        const auto skill = UnpackChargedSkill(102, 4, param, val);

        assert(skill.skillId == 82);
        assert(skill.level == 5);
        assert(skill.currentCharges == 90);
        assert(skill.maxCharges == 90);
    }

    // 3. High skill ID (> 255): Cloak of Shadows (264, level 3, 30/30 charges)
    {
        const uint16_t param = PackChargedSkillParam(264, 3);
        const int32_t val = PackChargedSkillValue(30, 30);
        const auto skill = UnpackChargedSkill(103, 1, param, val);

        assert(skill.skillId == 264);
        assert(skill.level == 3);
        assert(skill.currentCharges == 30);
        assert(skill.maxCharges == 30);
    }

    // 4. Extreme bounds: Skill 1023, Level 63, Charges 255/255
    {
        const uint16_t param = PackChargedSkillParam(1023, 63);
        const int32_t val = PackChargedSkillValue(255, 255);
        const auto skill = UnpackChargedSkill(104, 3, param, val);

        assert(skill.skillId == 1023);
        assert(skill.level == 63);
        assert(skill.currentCharges == 255);
        assert(skill.maxCharges == 255);
    }

    // 5. Depleted charges: 0 current, 45 max
    {
        const uint16_t param = PackChargedSkillParam(91, 3); // Lower Resist
        const int32_t val = PackChargedSkillValue(0, 45);
        const auto skill = UnpackChargedSkill(105, 4, param, val);

        assert(skill.skillId == 91);
        assert(skill.level == 3);
        assert(skill.currentCharges == 0);
        assert(skill.maxCharges == 45);
    }

    // 6. Real D2R Unique Item: Hexfire (Hydra skill 62, lvl 6, 36 max charges, 30 current)
    // In D2R runtime: param = 3974 (0x0F86), value = 9246 (0x0000241E)
    {
        assert(PackChargedSkillParam(62, 6) == 3974);
        assert(PackChargedSkillValue(30, 36) == 9246);
        const auto skill = UnpackChargedSkill(21, 4, 3974, 9246);
        assert(skill.skillId == 62); // Hydra! NOT barbarian 134!
        assert(skill.level == 6);
        assert(skill.currentCharges == 30);
        assert(skill.maxCharges == 36);
        assert(skill.kind == TacticalSkillKind::DamageSpell);
    }

    // 7. Real D2R Unique Item: Darksight Helm (Cloak of Shadows skill 264, lvl 5, 30 max charges, 26 current)
    // In D2R runtime: param = 16901 (0x4205), value = 7706 (0x00001E1A)
    {
        assert(PackChargedSkillParam(264, 5) == 16901);
        assert(PackChargedSkillValue(26, 30) == 7706);
        const auto skill = UnpackChargedSkill(20, 1, 16901, 7706);
        assert(skill.skillId == 264); // Cloak of Shadows! NOT trap 261!
        assert(skill.level == 5);
        assert(skill.currentCharges == 26);
        assert(skill.maxCharges == 30);
        assert(skill.kind == TacticalSkillKind::Suppression);
    }

    // 8. Real D2R Unique Item: Radament's Sphere (Poison Explosion skill 83, lvl 6, 40 max charges, 9 current)
    // In D2R runtime: param = 5318 (0x14C6), value = 10249 (0x00002809)
    {
        assert(PackChargedSkillParam(83, 6) == 5318);
        assert(PackChargedSkillValue(9, 40) == 10249);
        const auto skill = UnpackChargedSkill(22, 5, 5318, 10249);
        assert(skill.skillId == 83); // Poison Explosion! NOT 198!
        assert(skill.level == 6);
        assert(skill.currentCharges == 9);
        assert(skill.maxCharges == 40);
        assert(skill.kind == TacticalSkillKind::DamageSpell);
    }

    std::puts("TestStat204Packing: OK");
}

void TestTacticalSkillKindClassification() {
    assert(GetTacticalSkillKind(54) == TacticalSkillKind::Teleport);
    assert(GetTacticalSkillKind(82) == TacticalSkillKind::Curse);        // Life Tap
    assert(GetTacticalSkillKind(87) == TacticalSkillKind::Curse);        // Decrepify
    assert(GetTacticalSkillKind(91) == TacticalSkillKind::Curse);        // Lower Resist
    assert(GetTacticalSkillKind(9)  == TacticalSkillKind::Suppression);  // Slow Missiles
    assert(GetTacticalSkillKind(264) == TacticalSkillKind::Suppression); // Cloak of Shadows
    assert(GetTacticalSkillKind(44) == TacticalSkillKind::CrowdControl); // Frost Nova
    assert(GetTacticalSkillKind(55) == TacticalSkillKind::CrowdControl); // Glacial Spike
    assert(GetTacticalSkillKind(88) == TacticalSkillKind::CrowdControl); // Bone Prison
    assert(GetTacticalSkillKind(253) == TacticalSkillKind::CrowdControl); // Psychic Hammer
    assert(GetTacticalSkillKind(246) == TacticalSkillKind::Summon);      // Heart of Wolverine
    assert(GetTacticalSkillKind(226) == TacticalSkillKind::Summon);      // Oak Sage
    assert(GetTacticalSkillKind(47) == TacticalSkillKind::DamageSpell);  // Fire Ball
    assert(GetTacticalSkillKind(56) == TacticalSkillKind::DamageSpell);  // Meteor
    assert(GetTacticalSkillKind(83) == TacticalSkillKind::DamageSpell);  // Poison Explosion (Radament's Sphere)
    assert(GetTacticalSkillKind(31) == TacticalSkillKind::DamageSpell);  // Immolation Arrow (Hellrack)
    assert(GetTacticalSkillKind(261) == TacticalSkillKind::DamageSpell); // Charged Bolt Sentry (User's Helm charge)
    assert(GetTacticalSkillKind(251) == TacticalSkillKind::DamageSpell); // Fire Blast
    assert(GetTacticalSkillKind(271) == TacticalSkillKind::DamageSpell); // Lightning Sentry
    assert(GetTacticalSkillKind(41) == TacticalSkillKind::DamageSpell);  // Inferno
    assert(GetTacticalSkillKind(40) == TacticalSkillKind::Buff);         // Frozen Armor
    assert(GetTacticalSkillKind(52) == TacticalSkillKind::Buff);         // Enchant
    assert(GetTacticalSkillKind(278) == TacticalSkillKind::Buff);        // Venom
    assert(GetTacticalSkillKind(138) == TacticalSkillKind::Buff);        // Shout
    assert(GetTacticalSkillKind(0) == TacticalSkillKind::None);
    assert(GetTacticalSkillKind(218) == TacticalSkillKind::None);        // Identify
    assert(GetTacticalSkillKind(219) == TacticalSkillKind::None);        // Town Portal
    assert(GetTacticalSkillKind(999) == TacticalSkillKind::None);

    std::puts("TestTacticalSkillKindClassification: OK");
}

void TestCompanionSkillUsability() {
    // Bow skills only usable by Rogue (Amazon)
    for (uint16_t bowSkill : {6, 11, 12, 16, 21, 22, 26, 27, 31, 35}) {
        assert(IsSkillUsableByCompanion(VirtualPlayerClass::Amazon, bowSkill));
        assert(!IsSkillUsableByCompanion(VirtualPlayerClass::Paladin, bowSkill));
        assert(!IsSkillUsableByCompanion(VirtualPlayerClass::Sorceress, bowSkill));
        assert(!IsSkillUsableByCompanion(VirtualPlayerClass::Barbarian, bowSkill));
        assert(!IsSkillUsableByCompanion(VirtualPlayerClass::None, bowSkill));
    }

    // Incompatible player-locked animations rejected for ALL
    for (uint16_t bannedSkill : {97, 100, 106, 132, 144, 151, 255, 260, 270, 275}) {
        assert(!IsSkillUsableByCompanion(VirtualPlayerClass::Amazon, bannedSkill));
        assert(!IsSkillUsableByCompanion(VirtualPlayerClass::Paladin, bannedSkill));
        assert(!IsSkillUsableByCompanion(VirtualPlayerClass::Sorceress, bannedSkill));
        assert(!IsSkillUsableByCompanion(VirtualPlayerClass::Barbarian, bannedSkill));
    }

    // Standard spells/curses/summons usable by ALL
    for (uint16_t universalSkill : {54 /*Teleport*/, 82 /*Life Tap*/, 9 /*Slow Missiles*/, 44 /*Frost Nova*/, 246 /*Wolverine*/}) {
        assert(IsSkillUsableByCompanion(VirtualPlayerClass::Amazon, universalSkill));
        assert(IsSkillUsableByCompanion(VirtualPlayerClass::Paladin, universalSkill));
        assert(IsSkillUsableByCompanion(VirtualPlayerClass::Sorceress, universalSkill));
        assert(IsSkillUsableByCompanion(VirtualPlayerClass::Barbarian, universalSkill));
    }

    std::puts("TestCompanionSkillUsability: OK");
}

void TestEquipmentScanning() {
    struct MockItemStat {
        uint16_t statId;
        uint16_t param;
        int32_t value;
    };

    // Simulate equipped items:
    // Slot 1 (Head): Blackhorn's Face -> Slow Missiles (9, level 10, 20/20) + stat 72 durability
    // Slot 4 (Mainhand): Spellsteel -> Teleport (54, level 1, 20/20) + Decrepify (87, level 3, 30/30)
    // Slot 5 (Offhand): Shield with Bow skill (Multiple Shot 12, level 4, 15/15)
    std::vector<MockItemStat> headStats = {
        {72, 0, 20}, // Durability
        {ItemStat::ChargedSkill, PackChargedSkillParam(9, 10), PackChargedSkillValue(20, 20)}
    };

    std::vector<MockItemStat> weaponStats = {
        {ItemStat::ChargedSkill, PackChargedSkillParam(54, 1), PackChargedSkillValue(20, 20)},
        {ItemStat::ChargedSkill, PackChargedSkillParam(87, 3), PackChargedSkillValue(30, 30)},
        {ItemStat::AllSkills, 0, 1}
    };

    std::vector<MockItemStat> offhandStats = {
        {ItemStat::ChargedSkill, PackChargedSkillParam(12, 4), PackChargedSkillValue(15, 15)} // Multiple Shot
    };

    std::array<EquippedItemSlot, 3> slots = {{
        {1001, 1, 0x1000},
        {1002, 4, 0x2000},
        {1003, 5, 0x3000}
    }};

    auto extractor = [&](const EquippedItemSlot& slot, auto&& cb) {
        const std::vector<MockItemStat>* list = nullptr;
        if (slot.bodyLocation == 1) list = &headStats;
        else if (slot.bodyLocation == 4) list = &weaponStats;
        else if (slot.bodyLocation == 5) list = &offhandStats;

        if (list) {
            for (const auto& s : *list) {
                cb(s.statId, s.param, s.value);
            }
        }
    };

    // Paladin companion (Desert Guard): Multiple Shot must be filtered out!
    {
        auto charges = ScanCompanionChargedSkills(VirtualPlayerClass::Paladin, slots, extractor);
        assert(charges.size() == 3);
        assert(charges[0].skillId == 9);  // Slow Missiles
        assert(charges[1].skillId == 54); // Teleport
        assert(charges[2].skillId == 87); // Decrepify
    }

    // Amazon companion (Rogue): Multiple Shot is allowed!
    {
        auto charges = ScanCompanionChargedSkills(VirtualPlayerClass::Amazon, slots, extractor);
        assert(charges.size() == 4);
        assert(charges[0].skillId == 9);  // Slow Missiles
        assert(charges[1].skillId == 54); // Teleport
        assert(charges[2].skillId == 87); // Decrepify
        assert(charges[3].skillId == 12); // Multiple Shot
    }

    std::puts("TestEquipmentScanning: OK");
}

void TestTacticalPriorityArbitration() {
    std::vector<CompanionChargedSkill> skills;

    // Equip companion with:
    // 1. Teleport (54)
    // 2. Slow Missiles (9)
    // 3. Life Tap (82)
    // 4. Frost Nova (44)
    // 5. Heart of Wolverine (246)
    // 6. Fire Ball (47)
    skills.push_back({1001, 4, 54, 1, 20, 20, 0, TacticalSkillKind::Teleport});
    skills.push_back({1002, 1, 9, 10, 20, 20, 0, TacticalSkillKind::Suppression});
    skills.push_back({1003, 4, 82, 5, 30, 30, 0, TacticalSkillKind::Curse});
    skills.push_back({1004, 3, 44, 3, 25, 25, 0, TacticalSkillKind::CrowdControl});
    skills.push_back({1005, 1, 246, 5, 13, 13, 0, TacticalSkillKind::Summon});
    skills.push_back({1006, 4, 47, 8, 40, 40, 0, TacticalSkillKind::DamageSpell});

    TacticalCombatSensors sensors{};
    sensors.nowMs = 100000;

    // Test P1: Critical health & surrounded -> MUST select Teleport
    {
        sensors.companionHealthPct = 0.35f; // < 40%
        sensors.meleeAttackersCount = 3;    // surrounded
        sensors.nearbyHostilesCount = 4;
        sensors.hasActiveBoss = true;       // Boss present too!
        sensors.targetHasCurse = false;

        auto decision = SelectBestTacticalCharge(skills, sensors, true, true);
        assert(decision.shouldCast);
        assert(decision.kind == TacticalSkillKind::Teleport);
        assert(decision.skillId == 54);
    }

    // Test P2: Safe health, distant ranged shooters targeting squad -> MUST select Slow Missiles
    {
        sensors.companionHealthPct = 0.85f;
        sensors.meleeAttackersCount = 0;
        sensors.nearbyHostilesCount = 1;
        sensors.rangedShootersCount = 3; // Gloams/archers
        sensors.hasActiveBoss = false;

        auto decision = SelectBestTacticalCharge(skills, sensors, true, true);
        assert(decision.shouldCast);
        assert(decision.kind == TacticalSkillKind::Suppression);
        assert(decision.skillId == 9);
    }

    // Test P3: Boss present, no distant shooters, safe health -> MUST select Life Tap
    {
        sensors.companionHealthPct = 0.90f;
        sensors.meleeAttackersCount = 1;
        sensors.nearbyHostilesCount = 2;
        sensors.rangedShootersCount = 0;
        sensors.hasActiveBoss = true;
        sensors.targetHasCurse = false;

        auto decision = SelectBestTacticalCharge(skills, sensors, true, true);
        assert(decision.shouldCast);
        assert(decision.kind == TacticalSkillKind::Curse);
        assert(decision.skillId == 82);
    }

    // Test P3 Target already cursed -> should skip Curse and select Frost Nova (P4) or Wolverine (P5)
    {
        sensors.companionHealthPct = 0.90f;
        sensors.meleeAttackersCount = 1;
        sensors.nearbyHostilesCount = 3; // Dense pack
        sensors.rangedShootersCount = 0;
        sensors.hasActiveBoss = true;
        sensors.targetHasCurse = true; // ALREADY CURSED!
        sensors.isCombatActive = true;
        sensors.hasAlliedSummon = false;

        auto decision = SelectBestTacticalCharge(skills, sensors, true, true);
        assert(decision.shouldCast);
        assert(decision.kind == TacticalSkillKind::CrowdControl);
        assert(decision.skillId == 44);
    }

    // Test P5: In combat, target cursed, no dense pack, summon missing -> MUST select Wolverine
    {
        sensors.companionHealthPct = 0.90f;
        sensors.meleeAttackersCount = 1;
        sensors.nearbyHostilesCount = 1;
        sensors.rangedShootersCount = 0;
        sensors.hasActiveBoss = false;
        sensors.targetHasCurse = true;
        sensors.isCombatActive = true;
        sensors.hasAlliedSummon = false; // Missing summon

        auto decision = SelectBestTacticalCharge(skills, sensors, true, true);
        assert(decision.shouldCast);
        assert(decision.kind == TacticalSkillKind::Summon);
        assert(decision.skillId == 246);
    }

    // Test P6: In combat, target cursed, no dense pack, summon present -> MUST select Fire Ball (DamageSpell)
    {
        sensors.companionHealthPct = 0.90f;
        sensors.meleeAttackersCount = 1;
        sensors.nearbyHostilesCount = 1;
        sensors.rangedShootersCount = 0;
        sensors.hasActiveBoss = false;
        sensors.targetHasCurse = true;
        sensors.isCombatActive = true;
        sensors.hasAlliedSummon = true; // Summon present!

        auto decision = SelectBestTacticalCharge(skills, sensors, true, true);
        assert(decision.shouldCast);
        assert(decision.kind == TacticalSkillKind::DamageSpell);
        assert(decision.skillId == 47);
    }

    // Test Assassin Traps (e.g. Charged Bolt Sentry, ID 261):
    {
        std::vector<CompanionChargedSkill> trapSkills;
        trapSkills.push_back({1010, 1, 261, 2, 26, 30, 0, GetTacticalSkillKind(261)});
        TacticalCombatSensors trapSensors{};
        trapSensors.nowMs = 100000;
        trapSensors.hasActiveBoss = true;
        trapSensors.isCombatActive = true;

        auto decision = SelectBestTacticalCharge(trapSkills, trapSensors, true, true);
        assert(decision.shouldCast);
        assert(decision.skillId == 261);
        assert(decision.level == 2);
        assert(decision.kind == TacticalSkillKind::DamageSpell);
    }

    // Test Master Toggle Disabled: `isMasterEnabled == false`
    {
        auto decision = SelectBestTacticalCharge(skills, sensors, false, true);
        assert(!decision.shouldCast);
    }

    // Test Cooldown Gating
    {
        // Set lastCastMs to nowMs - 5000 (only 5s ago, while Teleport requires 12s)
        skills[0].lastCastMs = sensors.nowMs - 5000;
        sensors.companionHealthPct = 0.25f;
        sensors.meleeAttackersCount = 3;
        sensors.nearbyHostilesCount = 3;

        // Teleport is on cooldown! The next best trigger is evaluated
        auto decision = SelectBestTacticalCharge(skills, sensors, true, true);
        assert(decision.shouldCast);
        assert(decision.skillId != 54); // Cannot be Teleport!
    }

    std::puts("TestTacticalPriorityArbitration: OK");
}

void TestChargeAccounting() {
    CompanionChargedSkill skill{};
    skill.skillId = 54;
    skill.currentCharges = 2;
    skill.maxCharges = 20;

    // Zero-Wear Dev Mode: `consumeCharges == false`
    assert(DecrementChargedSkill(skill, false));
    assert(skill.currentCharges == 2); // Charges remain intact!

    // Realistic Mode: `consumeCharges == true`
    assert(DecrementChargedSkill(skill, true));
    assert(skill.currentCharges == 1);

    assert(DecrementChargedSkill(skill, true));
    assert(skill.currentCharges == 0);

    // Exhausted: Cannot decrement further!
    assert(!DecrementChargedSkill(skill, true));
    assert(skill.currentCharges == 0);

    std::puts("TestChargeAccounting: OK");
}

void TestCorpseSkillsIndexingAndTriggering() {
    // 1. Indexing check
    assert(SkillRequiresCorpse(74));  // Corpse Explosion
    assert(SkillRequiresCorpse(83));  // Poison Explosion
    assert(SkillRequiresCorpse(95));  // Revive
    assert(SkillRequiresCorpse(131)); // Find Potion
    assert(SkillRequiresCorpse(138)); // Find Item
    assert(SkillRequiresCorpse(150)); // Grim Ward

    assert(!SkillRequiresCorpse(54));  // Teleport
    assert(!SkillRequiresCorpse(62));  // Hydra
    assert(!SkillRequiresCorpse(264)); // Cloak of Shadows
    assert(!SkillRequiresCorpse(64));  // Frozen Orb

    // 2. HasCorpseSkills helper
    std::vector<CompanionChargedSkill> skills;
    skills.push_back({ 1, 4, 62, 6, 30, 36, 0, TacticalSkillKind::DamageSpell });
    assert(!HasCorpseSkills(skills));

    skills.push_back({ 2, 5, 83, 6, 9, 40, 0, TacticalSkillKind::DamageSpell });
    assert(HasCorpseSkills(skills));

    // 3. Evaluation gating: blocked when no corpses
    TacticalCombatSensors sensors{};
    sensors.isCombatActive = true;
    sensors.nearbyHostilesCount = 3;
    sensors.nearbyCorpsesCount = 0; // No corpses!

    const char* reason = "";
    assert(!EvaluateTacticalCharge(skills[1], sensors, false, reason));

    // 4. Evaluation trigger: permitted when corpses exist
    sensors.nearbyCorpsesCount = 2;
    sensors.closestCorpseX = 120.0f;
    sensors.closestCorpseY = 130.0f;
    assert(EvaluateTacticalCharge(skills[1], sensors, false, reason));
    assert(std::string(reason).find("Corpse Detonation") != std::string::npos);

    // 5. Arbitration: when Hydra is on cooldown, corpse skill triggers if corpses exist
    skills[0].lastCastMs = 1000;
    sensors.nowMs = 1500; // Hydra (4s cd) on cooldown
    sensors.nearbyCorpsesCount = 0;
    auto decision = SelectBestTacticalCharge(skills, sensors, true, false);
    assert(!decision.shouldCast); // Both blocked (Hydra on cd, Poison Exp lacks corpse)

    sensors.nearbyCorpsesCount = 1;
    decision = SelectBestTacticalCharge(skills, sensors, true, false);
    assert(decision.shouldCast);
    assert(decision.skillId == 83); // Poison Explosion selected!

    std::puts("TestCorpseSkillsIndexingAndTriggering: OK");
}
void TestEquipmentSkillsAndStrafe() {
    // 1. Passive skill check
    assert(IsSkillPassive(13));  // Dodge
    assert(IsSkillPassive(23));  // Penetrate
    assert(IsSkillPassive(33));  // Pierce
    assert(IsSkillPassive(37));  // Warmth
    assert(IsSkillPassive(61));  // Fire Mastery
    assert(IsSkillPassive(127)); // Sword Mastery
    assert(IsSkillPassive(258)); // Weapon Block
    assert(!IsSkillPassive(26)); // Strafe is NOT passive!
    assert(!IsSkillPassive(12)); // Multiple Shot is NOT passive!
    assert(!IsSkillPassive(62)); // Hydra is NOT passive!
    assert(!IsSkillPassive(74)); // Corpse Explosion is NOT passive!

    // 2. Usability check
    assert(IsSkillUsableByCompanion(VirtualPlayerClass::Amazon, 26)); // Rogue can use Strafe!
    assert(!IsSkillUsableByCompanion(VirtualPlayerClass::Paladin, 26)); // Desert guard cannot use Strafe
    assert(!IsSkillUsableByCompanion(VirtualPlayerClass::Amazon, 13)); // Dodge cannot be cast actively

    // 3. Scanning item with +3 Strafe (Stat 107 single skill / staffmod)
    struct MockItemStat {
        uint16_t statId;
        uint16_t param;
        int32_t value;
    };
    std::vector<MockItemStat> weaponStats = {
        { ItemStat::SingleSkill, 26, 3 } // +3 to Strafe
    };
    std::vector<MockItemStat> headStats = {
        { ItemStat::ChargedSkill, PackChargedSkillParam(54, 1), PackChargedSkillValue(20, 20) } // Teleport
    };

    std::array<EquippedItemSlot, 2> slots = {{
        { 2001, 4, 0x4000 },
        { 2002, 1, 0x1000 }
    }};

    auto extractor = [&](const EquippedItemSlot& slot, auto&& cb) {
        if (slot.bodyLocation == 4) {
            for (const auto& s : weaponStats) cb(s.statId, s.param, s.value);
        } else if (slot.bodyLocation == 1) {
            for (const auto& s : headStats) cb(s.statId, s.param, s.value);
        }
    };

    auto skills = ScanCompanionChargedSkills(VirtualPlayerClass::Amazon, slots, extractor);
    assert(skills.size() == 2);

    // Verify Strafe equipment skill properties
    const auto* strafe = (skills[0].skillId == 26) ? &skills[0] : &skills[1];
    assert(strafe != nullptr);
    assert(strafe->skillId == 26);
    assert(strafe->level == 3);
    assert(strafe->isEquipmentSkill == true);
    assert(strafe->maxCharges == 0);
    assert(strafe->currentCharges == 255);
    assert(strafe->kind == TacticalSkillKind::DamageSpell);

    // 4. Charge accounting: equipment skills never deplete
    CompanionChargedSkill strafeCopy = *strafe;
    assert(DecrementChargedSkill(strafeCopy, true) == true);
    assert(strafeCopy.currentCharges == 255);

    // 5. Tactical evaluation and selection in combat
    TacticalCombatSensors sensors{};
    sensors.nowMs = 50000;
    sensors.isCombatActive = true;
    sensors.nearbyHostilesCount = 3;

    const char* reason = "";
    assert(EvaluateTacticalCharge(*strafe, sensors, true, reason));
    assert(std::string(reason).find("Elemental Damage Burst") != std::string::npos);

    auto decision = SelectBestTacticalCharge(skills, sensors, true, true);
    assert(decision.shouldCast);
    assert(decision.skillId == 26);
    assert(decision.level == 3);

    std::puts("TestEquipmentSkillsAndStrafe: OK");
}


} // namespace

int main() {
    std::puts("Running companion_charges_test...");
    TestStat204Packing();
    TestTacticalSkillKindClassification();
    TestCompanionSkillUsability();
    TestEquipmentScanning();
    TestTacticalPriorityArbitration();
    TestChargeAccounting();
    TestCorpseSkillsIndexingAndTriggering();
    TestEquipmentSkillsAndStrafe();
    std::puts("All companion_charges_test checks passed successfully!");
    return 0;
}

