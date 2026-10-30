#pragma once
#include <cstdint>
#include <span>
#include <vector>
#include <array>
#include "companion_skills.h"

namespace framevr::companionai {

// High-level tactical classification of skills for companion AI decision making.
enum class TacticalSkillKind : uint8_t {
    None = 0,
    Teleport,       // Emergency mobility & repositions
    Suppression,    // Defensive anti-missile / blinding (Slow Missiles, Cloak of Shadows)
    Curse,          // Life Tap, Lower Resist, Decrepify, Weaken, Dim Vision...
    CrowdControl,   // Frost Nova, Glacial Spike, Blizzard, Bone Prison...
    Summon,         // Heart of Wolverine, Oak Sage, Spirit Wolf, Golems...
    DamageSpell,    // Fireball, Meteor, Chain Lightning, Nova, Bone Spear...
    Buff            // Enchant, Cyclone Armor, Bone Armor...
};

// Represents a verified charged skill discovered on companion equipment.
struct CompanionChargedSkill {
    uint32_t itemId = 0;
    int bodyLocation = 0;          // 1 = Head, 3 = Torso, 4 = Mainhand, 5 = Offhand
    uint16_t skillId = 0;
    uint8_t level = 0;
    uint8_t currentCharges = 0;
    uint8_t maxCharges = 0;
    uint64_t lastCastMs = 0;
    TacticalSkillKind kind = TacticalSkillKind::None;
    bool isEquipmentSkill = false; // true for skills granted directly by equipment (STAT 107/97)
};

// Tactical sensors provided by Layer 1 & Layer 2 to evaluate casting eligibility.
struct TacticalCombatSensors {
    float companionHealthPct = 1.0f; // 0.0f to 1.0f
    int nearbyHostilesCount = 0;     // Hostiles within 8 sub-tiles
    int meleeAttackersCount = 0;     // Hostiles within 2.5 sub-tiles
    int rangedShootersCount = 0;     // Missile shooters (gloams/archers) > 12 sub-tiles
    int nearbyCorpsesCount = 0;      // Dead monster bodies in tactical reach
    float closestCorpseX = 0.0f;     // Subtile X of prioritized corpse
    float closestCorpseY = 0.0f;     // Subtile Y of prioritized corpse
    uint32_t closestCorpseId = 0;    // Unit ID of prioritized corpse
    uintptr_t closestCorpseUnit = 0; // Server unit pointer of prioritized corpse
    bool hasActiveBoss = false;       // Target is Boss/Champion/Unique
    bool targetHasCurse = false;     // Target already afflicted by curse
    bool hasAlliedSummon = false;    // Squad already has active Oak Sage / Wolverine
    bool isCombatActive = false;     // True if currently engaged in combat
    uint64_t nowMs = 0;              // Current timestamp in milliseconds
};

// Decision output from the tactical charge evaluation.
struct TacticalCastDecision {
    bool shouldCast = false;
    uint16_t skillId = 0;
    uint8_t level = 0;
    uint32_t itemId = 0;
    int bodyLocation = 0;
    TacticalSkillKind kind = TacticalSkillKind::None;
    const char* reason = "";
    bool isEquipmentSkill = false;
};

// Slot metadata for inventory item traversal.
struct EquippedItemSlot {
    uint32_t itemId = 0;
    int bodyLocation = 0;
    uintptr_t itemUnit = 0;
};

// Forward declaration
inline TacticalSkillKind GetTacticalSkillKind(uint16_t skillId) noexcept;

// Stat 204 (item_charged_skill) packing and unpacking helpers.
// In Diablo II: Resurrected / D2R:
// Parameter (16 bits):
//   - bits 0..5 (6 bits): Skill Level (0..63)
//   - bits 6..15 (10 bits): Skill ID (0..1023)
//   param = ((skillId & 0x3FF) << 6) | (level & 0x3F)
// Value (32 bits):
//   - bits 0..7 (8 bits): Current Charges (0..255)
//   - bits 8..15 (8 bits): Max Charges (0..255)
//   value = (MaxCharges << 8) | CurrentCharges

inline constexpr uint16_t PackChargedSkillParam(uint16_t skillId, uint8_t level) noexcept {
    return static_cast<uint16_t>(
        ((static_cast<uint16_t>(skillId) & 0x3FF) << 6) |
        (static_cast<uint16_t>(level) & 0x3F)
    );
}

inline constexpr int32_t PackChargedSkillValue(uint8_t currentCharges, uint8_t maxCharges) noexcept {
    return static_cast<int32_t>(
        (static_cast<uint32_t>(maxCharges) << 8) | static_cast<uint32_t>(currentCharges)
    );
}

inline CompanionChargedSkill UnpackChargedSkill(
    uint32_t itemId,
    int bodyLocation,
    uint16_t param,
    int32_t value) noexcept {
    CompanionChargedSkill skill{};
    skill.itemId = itemId;
    skill.bodyLocation = bodyLocation;
    skill.skillId = static_cast<uint16_t>((param >> 6) & 0x3FF);
    skill.level = static_cast<uint8_t>(param & 0x3F);
    skill.currentCharges = static_cast<uint8_t>(value & 0xFF);
    skill.maxCharges = static_cast<uint8_t>((value >> 8) & 0xFF);
    skill.kind = GetTacticalSkillKind(skill.skillId);
    return skill;
}

// Maps a skill ID to its tactical combat role.
inline TacticalSkillKind GetTacticalSkillKind(uint16_t skillId) noexcept {
    switch (skillId) {
        // Mobility / Emergency Escape
        case 54: // Teleport
            return TacticalSkillKind::Teleport;

        // Projectile Suppression & Vision
        case 8:   // Inner Sight
        case 9:   // Slow Missiles
        case 17:  // Slow Missiles (variant)
        case 264: // Cloak of Shadows
            return TacticalSkillKind::Suppression;

        // Curses
        case 66:  // Amplify Damage
        case 71:  // Dim Vision
        case 72:  // Weaken
        case 76:  // Iron Maiden
        case 77:  // Terror
        case 81:  // Confuse
        case 82:  // Life Tap
        case 86:  // Attract
        case 87:  // Decrepify
        case 91:  // Lower Resist
        case 137: // Taunt
        case 146: // Battle Cry
            return TacticalSkillKind::Curse;

        // Crowd Control
        case 43:  // Telekinesis
        case 44:  // Frost Nova
        case 45:  // Ice Blast
        case 55:  // Glacial Spike
        case 59:  // Blizzard
        case 78:  // Bone Wall
        case 88:  // Bone Prison
        case 89:  // Bone Prison (variant ID)
        case 116: // Conversion
        case 130: // Howl
        case 139: // Stun
        case 150: // Grim Ward
        case 154: // War Cry
        case 240: // Twister
        case 253: // Psychic Hammer
        case 273: // Mind Blast
            return TacticalSkillKind::CrowdControl;

        // Summons
        case 28:  // Decoy (Dopplezon)
        case 32:  // Valkyrie
        case 75:  // Clay Golem
        case 85:  // Blood Golem
        case 90:  // Iron Golem
        case 94:  // Fire Golem
        case 95:  // Revive
        case 221: // Raven
        case 222: // Poison Creeper
        case 226: // Oak Sage
        case 227: // Spirit Wolf
        case 232: // Carrion Vine
        case 236: // Spirit of Barbs
        case 237: // Dire Wolf
        case 242: // Solar Creeper
        case 246: // Heart of Wolverine
        case 247: // Grizzly
        case 268: // Shadow Warrior
        case 279: // Shadow Master
            return TacticalSkillKind::Summon;

        // Targeted Damage Spells & AoE
        case 6:   // Magic Arrow
        case 7:   // Fire Arrow
        case 10:  // Jab
        case 11:  // Cold Arrow
        case 12:  // Multiple Shot
        case 14:  // Power Strike
        case 15:  // Poison Javelin
        case 16:  // Exploding Arrow
        case 19:  // Impale
        case 20:  // Lightning Bolt
        case 21:  // Ice Arrow
        case 22:  // Guided Arrow
        case 24:  // Charged Strike
        case 25:  // Plague Javelin
        case 26:  // Strafe
        case 27:  // Immolation Arrow
        case 30:  // Fend
        case 31:  // Freezing Arrow (Hellrack)
        case 34:  // Lightning Strike
        case 35:  // Lightning Fury
        case 36:  // Fire Bolt
        case 38:  // Charged Bolt
        case 39:  // Ice Bolt
        case 41:  // Inferno
        case 42:  // Static Field
        case 46:  // Blaze
        case 47:  // Fire Ball
        case 48:  // Nova
        case 49:  // Lightning
        case 51:  // Fire Wall
        case 53:  // Chain Lightning
        case 56:  // Meteor
        case 62:  // Hydra
        case 64:  // Frozen Orb
        case 67:  // Teeth
        case 73:  // Poison Dagger
        case 74:  // Corpse Explosion
        case 83:  // Poison Explosion (Radament's Sphere)
        case 84:  // Bone Spear
        case 92:  // Poison Nova
        case 93:  // Bone Spirit
        case 96:  // Sacrifice
        case 101: // Holy Bolt
        case 111: // Vengeance
        case 112: // Blessed Hammer
        case 121: // Fist of the Heavens
        case 126: // Bash
        case 140: // Double Swing
        case 152: // Berserk
        case 223: // Arctic Blast
        case 225: // Firestorm
        case 229: // Molten Boulder
        case 234: // Fissure
        case 244: // Volcano
        case 245: // Tornado
        case 249: // Armageddon
        case 250: // Hurricane
        case 251: // Fire Blast
        case 254: // Tiger Strike
        case 256: // Shock Web
        case 257: // Blade Sentinel
        case 259: // Fists of Fire
        case 261: // Charged Bolt Sentry (Traps)
        case 262: // Wake of Fire
        case 265: // Cobra Strike
        case 266: // Blade Fury
        case 269: // Claws of Thunder
        case 271: // Lightning Sentry
        case 272: // Wake of Inferno
        case 274: // Blades of Ice
        case 276: // Death Sentry
        case 280: // Phoenix Strike
            return TacticalSkillKind::DamageSpell;

        // Buffs
        case 40:  // Frozen Armor
        case 50:  // Shiver Armor
        case 52:  // Enchant
        case 57:  // Thunder Storm
        case 58:  // Energy Shield
        case 60:  // Chilling Armor
        case 68:  // Bone Armor
        case 138: // Shout
        case 149: // Battle Orders
        case 155: // Battle Command
        case 235: // Cyclone Armor
        case 258: // Burst of Speed
        case 267: // Fade
        case 277: // Blade Shield
        case 278: // Venom (Andariel's Visage)
            return TacticalSkillKind::Buff;

        // Non-combat utility explicitly excluded
        case 0:
        case 218: // Identify
        case 219: // Town Portal
            return TacticalSkillKind::None;

        default:
            // Safe fallback: Any unrecognized valid class skill (1..360) is treated as DamageSpell
            if (skillId > 0 && skillId < 360) {
                return TacticalSkillKind::DamageSpell;
            }
            return TacticalSkillKind::None;
    }
}

// Minimum cooldown in milliseconds required between tactical casts of each kind.
inline uint64_t GetTacticalCooldownMs(TacticalSkillKind kind) noexcept {
    switch (kind) {
        case TacticalSkillKind::Teleport:     return 12000; // 12.0s
        case TacticalSkillKind::Suppression:  return 15000; // 15.0s
        case TacticalSkillKind::Curse:        return 20000; // 20.0s
        case TacticalSkillKind::CrowdControl: return  8000; //  8.0s
        case TacticalSkillKind::Summon:       return 45000; // 45.0s
        case TacticalSkillKind::DamageSpell:  return  4000; //  4.0s
        case TacticalSkillKind::Buff:         return 60000; // 60.0s
        default:                              return  4000;
    }
}

// Identifies passive skills and masteries across all character classes that cannot be cast actively.
inline constexpr bool IsSkillPassive(uint16_t skillId) noexcept {
    switch (skillId) {
        // Amazon passives
        case 13:  // Dodge
        case 18:  // Avoid
        case 23:  // Penetrate
        case 29:  // Evade
        case 33:  // Pierce
        // Sorceress passives
        case 37:  // Warmth
        case 61:  // Fire Mastery
        case 63:  // Lightning Mastery
        case 65:  // Cold Mastery
        // Necromancer passives
        case 69:  // Skeleton Mastery
        case 79:  // Golem Mastery
        case 89:  // Summon Resist
        // Barbarian combat masteries & passives
        case 127: // Sword Mastery
        case 128: // Axe Mastery
        case 129: // Mace Mastery
        case 134: // Polearm Mastery
        case 135: // Throwing Mastery
        case 136: // Spear Mastery
        case 141: // Increased Stamina
        case 145: // Iron Skin
        case 148: // Increased Speed
        case 153: // Natural Resistance
        // Druid passives
        case 224: // Lycanthropy
        // Assassin passives
        case 252: // Claw Mastery
        case 258: // Weapon Block
            return true;
        default:
            return false;
    }
}

// Verifies that the companion model and animation system can execute the skill safely
// without missing animation tokens or skeletal glitches.
inline bool IsSkillUsableByCompanion(VirtualPlayerClass vclass, uint16_t skillId) noexcept {
    if (vclass == VirtualPlayerClass::None) {
        return false;
    }

    // Passive skills and weapon masteries cannot be cast actively as spells
    if (IsSkillPassive(skillId)) {
        return false;
    }

    // Hardcoded player-locked animations and complex state machines that hireling skeletons lack:
    switch (skillId) {
        case 97:  // Smite
        case 100: // Charge
        case 106: // Zeal
        case 132: // Leap
        case 144: // Leap Attack
        case 151: // Whirlwind
        case 255: // Dragon Talon
        case 260: // Dragon Claw
        case 270: // Dragon Tail
        case 275: // Dragon Flight
            return false;
    }

    // Amazon bow/crossbow exclusive skills:
    switch (skillId) {
        case 6:  // Magic Arrow
        case 7:  // Fire Arrow
        case 11: // Cold Arrow
        case 12: // Multiple Shot
        case 16: // Exploding Arrow
        case 21: // Ice Arrow
        case 22: // Guided Arrow
        case 26: // Strafe
        case 27: // Immolation Arrow
        case 31: // Freezing Arrow
        case 35: // Lightning Fury / bow token
            // Only Rogue archers (Amazon virtual class) have bow casting tokens
            return vclass == VirtualPlayerClass::Amazon;
    }

    // All standard spells, curses, summons, buffs, and warcries are supported across
    // all hireling models (they use special cast SC or standard attack A1/A2).
    return true;
}

// Returns true if the skill requires a dead monster corpse as its execution target.
inline constexpr bool SkillRequiresCorpse(uint16_t skillId) noexcept {
    switch (skillId) {
        case 74:  // Corpse Explosion (Necromancer)
        case 83:  // Poison Explosion (Necromancer)
        case 95:  // Revive (Necromancer)
        case 131: // Find Potion (Barbarian)
        case 138: // Find Item (Barbarian)
        case 150: // Grim Ward (Barbarian)
            return true;
        default:
            return false;
    }
}

// Helper checking if a collection of charged skills contains any corpse-requiring skills.
template <typename Range>
inline bool HasCorpseSkills(const Range& skills) noexcept {
    for (const auto& s : skills) {
        if (SkillRequiresCorpse(s.skillId)) return true;
    }
    return false;
}

// Scans equipped items and extracts usable charged skills matching the companion archetype.
template <typename StatExtractor>
std::vector<CompanionChargedSkill> ScanCompanionChargedSkills(
    VirtualPlayerClass vclass,
    std::span<const EquippedItemSlot> items,
    StatExtractor&& extractStats) {
    std::vector<CompanionChargedSkill> result;

    for (const auto& item : items) {
        if (!item.itemUnit) continue;

        extractStats(item, [&](uint16_t statId, uint16_t param, int32_t value) {
            if (statId == ItemStat::ChargedSkill) {
                CompanionChargedSkill skill = UnpackChargedSkill(item.itemId, item.bodyLocation, param, value);
                if (skill.skillId == 0 || skill.maxCharges == 0) {
                    return;
                }

                // Usability validation by virtual class
                if (!IsSkillUsableByCompanion(vclass, skill.skillId)) {
                    return;
                }

                skill.kind = GetTacticalSkillKind(skill.skillId);
                result.push_back(skill);
                return;
            }

            if (statId == ItemStat::SingleSkill) {
                const uint16_t skillId = param;
                if (skillId > 0 && IsSkillMatchingVirtualClass(vclass, skillId) &&
                    !IsSkillPassive(skillId) && IsSkillUsableByCompanion(vclass, skillId)) {
                    CompanionChargedSkill skill{};
                    skill.itemId = item.itemId;
                    skill.bodyLocation = item.bodyLocation;
                    skill.skillId = skillId;
                    skill.level = static_cast<uint8_t>(value > 0 ? value : 1);
                    skill.currentCharges = 255;
                    skill.maxCharges = 0;
                    skill.isEquipmentSkill = true;
                    skill.kind = GetTacticalSkillKind(skillId);
                    result.push_back(skill);
                }
                return;
            }

            if (statId == ItemStat::NonClassSkill) {
                const uint16_t skillId = param;
                if (skillId > 0 && !IsSkillPassive(skillId) && IsSkillUsableByCompanion(vclass, skillId)) {
                    CompanionChargedSkill skill{};
                    skill.itemId = item.itemId;
                    skill.bodyLocation = item.bodyLocation;
                    skill.skillId = skillId;
                    skill.level = static_cast<uint8_t>(value > 0 ? value : 1);
                    skill.currentCharges = 255;
                    skill.maxCharges = 0;
                    skill.isEquipmentSkill = true;
                    skill.kind = GetTacticalSkillKind(skillId);
                    result.push_back(skill);
                }
                return;
            }
        });
    }

    return result;
}

// Accounting helper: decrements 1 charge if realistic consumption is active.
inline bool DecrementChargedSkill(
    CompanionChargedSkill& skill,
    bool consumeCharges) noexcept {
    if (!consumeCharges || skill.isEquipmentSkill || skill.maxCharges == 0) {
        // Zero-Wear Dev Mode or equipment-granted skills: charges remain intact
        return true;
    }

    if (skill.currentCharges > 0) {
        skill.currentCharges--;
        return true;
    }

    return false; // Charges exhausted
}

// Evaluates a single candidate charged skill against tactical triggers and cooldowns.
inline bool EvaluateTacticalCharge(
    const CompanionChargedSkill& skill,
    const TacticalCombatSensors& sensors,
    bool consumeCharges,
    const char*& outReason) noexcept {
    outReason = "";

    // Charge check (only applies to finite item charges, never to equipment skills)
    if (!skill.isEquipmentSkill && skill.maxCharges > 0 && consumeCharges && skill.currentCharges == 0) {
        return false;
    }

    // Cooldown check
    const uint64_t cooldown = GetTacticalCooldownMs(skill.kind);
    if (skill.lastCastMs > 0 && sensors.nowMs < skill.lastCastMs + cooldown) {
        return false;
    }

    // Corpse skill prerequisite check: require at least one valid corpse nearby
    if (SkillRequiresCorpse(skill.skillId)) {
        if (sensors.nearbyCorpsesCount == 0) {
            return false;
        }
        if (sensors.hasActiveBoss || sensors.nearbyHostilesCount >= 1 || sensors.isCombatActive) {
            outReason = "P6 Corpse Detonation (Poison / Corpse Explosion)";
            return true;
        }
        return false;
    }

    // Tactical trigger evaluation by priority category
    switch (skill.kind) {
        case TacticalSkillKind::Teleport:
            // Emergency escape: Health < 40% (critical retreat) or (Health < 60% and surrounded by melee attackers >= 2)
            if (sensors.companionHealthPct < 0.40f ||
                (sensors.companionHealthPct < 0.60f && sensors.meleeAttackersCount >= 2)) {
                outReason = "P1 Emergency Teleport retreat";
                return true;
            }
            return false;

        case TacticalSkillKind::Suppression:
            // Threat suppression: Ranged shooters threatening squad
            if (sensors.rangedShootersCount >= 1) {
                outReason = "P2 Projectile suppression (Slow Missiles / Cloak)";
                return true;
            }
            return false;

        case TacticalSkillKind::Curse:
            // High-value debuff: Target Elite/Boss or active combat lacking active curse
            if ((sensors.hasActiveBoss || sensors.nearbyHostilesCount >= 1) && !sensors.targetHasCurse) {
                outReason = "P3 Assistance Curse (Life Tap / Lower Resist / Decrepify)";
                return true;
            }
            return false;

        case TacticalSkillKind::CrowdControl:
            // Area crowd control: Dense pack or boss within range
            if (sensors.nearbyHostilesCount >= 2 || sensors.hasActiveBoss) {
                outReason = "P4 Area Crowd Control (Frost Nova / Glacial Spike)";
                return true;
            }
            return false;

        case TacticalSkillKind::Summon:
            // Squad enhancement: Active combat and allied summon is missing
            if (sensors.isCombatActive && !sensors.hasAlliedSummon) {
                outReason = "P5 Supportive Summon (Heart of Wolverine / Oak Sage)";
                return true;
            }
            return false;

        case TacticalSkillKind::DamageSpell:
            // Tactical offense: Engaging Boss or hostile target
            if (sensors.hasActiveBoss || sensors.nearbyHostilesCount >= 1 || sensors.isCombatActive) {
                outReason = "P6 Elemental Damage Burst";
                return true;
            }
            return false;

        case TacticalSkillKind::Buff:
            // Squad buff: In combat without active buff
            if (sensors.isCombatActive) {
                outReason = "P7 Squad Buff (Enchant / Armor)";
                return true;
            }
            return false;

        case TacticalSkillKind::None:
            // Fallback for uncategorized active skills: trigger during active combat
            if (sensors.hasActiveBoss || sensors.nearbyHostilesCount >= 1 || sensors.isCombatActive) {
                outReason = "P8 Uncategorized Combat Action";
                return true;
            }
            return false;

        default:
            return false;
    }
}

// Selects the highest-priority tactical charged skill ready for execution.
inline TacticalCastDecision SelectBestTacticalCharge(
    std::span<CompanionChargedSkill> skills,
    const TacticalCombatSensors& sensors,
    bool isMasterEnabled,
    bool consumeCharges) noexcept {
    TacticalCastDecision decision{};

    // Master feature toggle check
    if (!isMasterEnabled || skills.empty()) {
        return decision;
    }

    // Evaluation in strict priority hierarchy (P1 to P8)
    constexpr std::array<TacticalSkillKind, 8> PriorityOrder = {
        TacticalSkillKind::Teleport,
        TacticalSkillKind::Suppression,
        TacticalSkillKind::Curse,
        TacticalSkillKind::CrowdControl,
        TacticalSkillKind::Summon,
        TacticalSkillKind::DamageSpell,
        TacticalSkillKind::Buff,
        TacticalSkillKind::None
    };

    for (TacticalSkillKind kind : PriorityOrder) {
        for (auto& skill : skills) {
            if (skill.kind != kind) continue;

            const char* reason = "";
            if (EvaluateTacticalCharge(skill, sensors, consumeCharges, reason)) {
                decision.shouldCast = true;
                decision.skillId = skill.skillId;
                decision.level = skill.level;
                decision.itemId = skill.itemId;
                decision.bodyLocation = skill.bodyLocation;
                decision.kind = skill.kind;
                decision.reason = reason;
                decision.isEquipmentSkill = skill.isEquipmentSkill;
                return decision;
            }
        }
    }

    return decision;
}

} // namespace framevr::companionai

