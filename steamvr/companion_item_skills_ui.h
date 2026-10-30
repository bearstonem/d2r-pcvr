#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>
#include "companion_skills.h"
#include "companion_charges.h"

namespace framevr::companionui {

enum class SkillBadgeType : uint8_t {
    ChargedSkill = 0,       // Active charged skill (displays charges, e.g. 20)
    VirtualClassSkill = 1,  // Equipment skill matching companion's virtual class (displays +level)
    Oskill = 2,             // Non-class skill granted to any companion (displays +level)
    Aura = 3                // Aura granted by equipment (displays aura level, e.g. L12)
};

struct MercenarySkillBadge {
    uint16_t skillId = 0;
    SkillBadgeType type = SkillBadgeType::ChargedSkill;
    uint8_t level = 1;
    uint8_t currentCharges = 0;
    uint8_t maxCharges = 0;
    uint32_t itemId = 0;
    int bodyLocation = 0;
    char skillName[64]{};
};

// Geometry constants for the overlay strip
constexpr int OverlayWidth = 512;
constexpr int OverlayHeight = 48;
constexpr int MaxBadges = 8;
constexpr int BadgeCellSize = 48;   // 48x48 px cell (1:1 square)
constexpr int BadgeIconSize = 42;   // 42x42 px icon (centered inside cell)
constexpr int BadgeGap = 6;         // 6 px spacing between badges

// Geometry constants for the skill tooltip card (compact native D2R inspection box)
constexpr int TooltipWidth = 320;
constexpr int TooltipHeight = 100;

// UI placement constants:
// The D2R 16:9 menu band spans height 'band = min(height, width * 9/16)'.
// The menu art ends at 0.860 * band from the top.
// The Mercenary sheet is on the left half of the menu (centered at -0.25f of width).
constexpr float MercenaryPanelCenterOffsetX = -0.25f;
constexpr float MenuArtBottomInBand = 0.860f;
constexpr float MenuArtDockGap = 0.003f; // 3 mm gap below menu bottom border

struct Spot {
    float x = 0.0f;       // Metres from surface centre (X right)
    float y = 0.0f;       // Metres from surface centre (Y up)
    float width = 0.0f;   // Metres wide
    float height = 0.0f;  // Metres high
    float u = 0.0f;       // Normalized U on 2D canvas (0..1)
    float v = 0.0f;       // Normalized V on 2D canvas (0..1)
};

struct PixelRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

// Places the badges overlay docked directly below the Mercenary panel at the bottom of the D2R menu
inline Spot PlaceOverlay(float canvasWidth, float canvasHeight, int badgeCount = 1) {
    if (canvasWidth <= 0.0f || canvasHeight <= 0.0f || badgeCount <= 0) {
        return {};
    }
    const float band = std::min(canvasHeight, canvasWidth * 9.f / 16.f);
    const int count = std::clamp(badgeCount, 1, MaxBadges);
    const int totalW = count * BadgeCellSize + (count - 1) * BadgeGap;
    const float mpp = band / 1080.0f;
    const float spotW = static_cast<float>(totalW) * mpp;
    const float spotH = static_cast<float>(BadgeCellSize) * mpp;

    // Dock right below the bottom stone frame of the Mercenary panel
    const float centerY = (MenuArtBottomInBand + MenuArtDockGap) * band + spotH * 0.5f;
    const float spotY = canvasHeight * 0.5f - centerY;

    // Centered under the Mercenary panel (left half of the menu)
    const float spotX = canvasWidth * MercenaryPanelCenterOffsetX;

    const float u = 0.5f + spotX / canvasWidth;
    const float v = 0.5f - spotY / canvasHeight;

    return { spotX, spotY, spotW, spotH, u, v };
}

template<typename RegionT>
inline PixelRect SubImageRegion(const RegionT& region, int badgeCount = 1) {
    const int count = std::clamp(badgeCount, 1, MaxBadges);
    const int totalW = count * BadgeCellSize + (count - 1) * BadgeGap;
    const int pixelW = std::min(region.w, totalW);
    const int startX = std::max(0, (region.w - pixelW) / 2);
    return { region.x + startX, region.y, pixelW, BadgeCellSize };
}

// Tests whether the VR laser pointing at (hitU, hitV) hovers over one of the skill badges.
inline int HitTestBadge(float canvasWidth, float canvasHeight, int badgeCount, float hitU, float hitV) {
    if (canvasWidth <= 0.0f || canvasHeight <= 0.0f || badgeCount <= 0) return -1;
    const auto spot = PlaceOverlay(canvasWidth, canvasHeight, badgeCount);
    if (spot.width <= 0.0f || spot.height <= 0.0f) return -1;

    // Convert raycast hit (u, v) into metric surface coordinates from center
    const float hitX = (hitU - 0.5f) * canvasWidth;
    const float hitY = (0.5f - hitV) * canvasHeight;

    if (std::fabs(hitX - spot.x) > spot.width * 0.5f || std::fabs(hitY - spot.y) > spot.height * 0.5f) {
        return -1;
    }

    const int count = std::clamp(badgeCount, 1, MaxBadges);
    const float left = spot.x - spot.width * 0.5f;
    const float relX = (hitX - left) / spot.width; // 0..1 across the strip
    const int idx = static_cast<int>(relX * count);
    return std::clamp(idx, 0, count - 1);
}

// Calculates metric placement for the floating skill tooltip card above the badge strip.
inline Spot PlaceTooltip(float canvasWidth, float canvasHeight, const Spot& badgesSpot) {
    if (canvasWidth <= 0.0f || canvasHeight <= 0.0f || badgesSpot.width <= 0.0f) return {};
    const float band = std::min(canvasHeight, canvasWidth * 9.f / 16.f);
    const float mpp = band / 1080.0f;
    const float w = static_cast<float>(TooltipWidth) * mpp;
    const float h = static_cast<float>(TooltipHeight) * mpp;

    const float x = badgesSpot.x;
    // Float directly ABOVE the badge strip (towards the menu)
    const float y = badgesSpot.y + badgesSpot.height * 0.5f + h * 0.5f + 0.010f;
    const float u = 0.5f + x / canvasWidth;
    const float v = 0.5f - y / canvasHeight;

    return { x, y, w, h, u, v };
}

template<typename RegionT>
inline PixelRect TooltipSubImageRegion(const RegionT& region) {
    const int pixelW = std::min(region.w, TooltipWidth);
    const int pixelH = std::min(region.h, TooltipHeight);
    const int startX = std::max(0, (region.w - pixelW) / 2);
    const int startY = std::max(0, (region.h - pixelH) / 2);
    return { region.x + startX, region.y + startY, pixelW, pixelH };
}

// Checks if the mercenary inventory panel is open in the active UI modal names
template<class Names>
inline bool HirelingPanelOpen(const Names& open) {
    for (const auto& name : open) {
        const std::string_view n(name);
        if (n == "HirelingInventoryPanel" || n == "HirelingInventory" ||
            n == "HireMenuPanel" || n.starts_with("HirelingInventory")) {
            return true;
        }
    }
    return false;
}

// Traverses equipped items to extract all charged skills, virtual class skills, oskills, and auras.
template<typename GetStatFn, typename GetNameFn = std::nullptr_t>
inline std::vector<MercenarySkillBadge> CollectMercenarySkillBadges(
    companionai::VirtualPlayerClass vclass,
    std::span<const companionai::EquippedItemSlot> slots,
    GetStatFn&& getStat,
    GetNameFn&& getName = nullptr,
    std::span<const companionai::CompanionChargedSkill> preDiscoveredCharges = {})
{
    std::vector<MercenarySkillBadge> badges;
    if (vclass == companionai::VirtualPlayerClass::None || slots.empty()) {
        return badges;
    }

    auto applyName = [&](MercenarySkillBadge& b) {
        if constexpr (!std::is_same_v<std::decay_t<GetNameFn>, std::nullptr_t>) {
            std::string n = getName(static_cast<int>(b.skillId));
            if (!n.empty()) {
                std::snprintf(b.skillName, sizeof(b.skillName), "%s", n.c_str());
            }
        }
        if (b.skillName[0] == '\0') {
            std::snprintf(b.skillName, sizeof(b.skillName), "Skill %u", b.skillId);
        }
    };

    // 1. Collect Charged Skills (STAT 204)
    if (!preDiscoveredCharges.empty()) {
        // Fast path: Reuse pre-discovered charges (zero redundant native queries)
        for (const auto& c : preDiscoveredCharges) {
            bool exists = false;
            for (auto& b : badges) {
                if (b.skillId == c.skillId && b.type == SkillBadgeType::ChargedSkill) {
                    exists = true;
                    break;
                }
            }
            if (!exists && badges.size() < MaxBadges) {
                badges.push_back({ c.skillId, SkillBadgeType::ChargedSkill, c.level, c.currentCharges, c.maxCharges, c.itemId, c.bodyLocation });
                applyName(badges.back());
            }
        }
    } else {
        // Fallback for standalone invocations
        static constexpr uint16_t ChargeCandidateSkills[] = {
            54,  // Teleport
            7, 9, 264, // Inner Sight, Slow Missiles, Cloak of Shadows
            66, 69, 71, 72, 76, 77, 81, 82, 86, 87, 91, 137, 146, // Curses
            43, 44, 45, 55, 59, 78, 88, 89, 116, 130, 139, 150, 154, 240, 253, 273, // Crowd Control
            17, 32, 75, 85, 90, 94, 95, 221, 222, 226, 227, 232, 236, 237, 242, 246, 247, 268, 279, // Summons
            6, 10, 11, 12, 14, 16, 19, 20, 21, 22, 24, 25, 26, 27, 29, 30, 31, 34, 35, // Amazon attacks
            36, 38, 39, 41, 42, 46, 47, 48, 49, 51, 53, 56, 62, 64, 67, 73, 74, 83, 84, 92, 93, // Sorc & Necro
            96, 101, 111, 112, 121, 126, 140, 152, // Paladin & Barbarian
            223, 225, 229, 234, 244, 245, 249, 250, // Druid
            251, 254, 256, 257, 259, 261, 262, 265, 266, 269, 271, 272, 274, 276, 280, // Assassin
            40, 50, 52, 57, 58, 60, 68, 138, 149, 155, 235, 258, 267, 277, 278 // Buffs
        };

        for (const auto& slot : slots) {
            for (uint16_t sId : ChargeCandidateSkills) {
                if (!companionai::IsSkillUsableByCompanion(vclass, sId)) {
                    continue;
                }
                for (uint8_t lvl : {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 18, 20}) {
                    const uint16_t p = companionai::PackChargedSkillParam(sId, lvl);
                    const int32_t val = getStat(slot.itemUnit, 204, static_cast<int>(p));
                    if (val > 0) {
                        const uint8_t cur = static_cast<uint8_t>(val & 0xFF);
                        const uint8_t maxC = static_cast<uint8_t>((val >> 8) & 0xFF);
                        if (maxC > 0 && companionai::IsSkillUsableByCompanion(vclass, sId)) {
                            bool exists = false;
                            for (auto& b : badges) {
                                if (b.skillId == sId && b.type == SkillBadgeType::ChargedSkill) {
                                    exists = true;
                                    break;
                                }
                            }
                            if (!exists && badges.size() < MaxBadges) {
                                badges.push_back({ sId, SkillBadgeType::ChargedSkill, lvl, cur, maxC, slot.itemId, slot.bodyLocation });
                                applyName(badges.back());
                            }
                            break;
                        }
                    }
                }
            }
        }
    }

    // 2. Collect Granted Auras (STAT 151)
    static constexpr uint16_t AuraSkillCandidates[] = {
        98, 99, 100, 102, 103, 104, 105, 108, 109, 110, 113, 114, 115, 117, 118, 119, 120, 122, 123, 124, 125
    };
    for (const auto& slot : slots) {
        for (uint16_t auraId : AuraSkillCandidates) {
            const int32_t auraLvl = getStat(slot.itemUnit, 151, static_cast<int>(auraId));
            if (auraLvl > 0) {
                bool exists = false;
                for (auto& b : badges) {
                    if (b.skillId == auraId && b.type == SkillBadgeType::Aura) {
                        exists = true;
                        break;
                    }
                }
                if (!exists && badges.size() < MaxBadges) {
                    badges.push_back({ auraId, SkillBadgeType::Aura, static_cast<uint8_t>(auraLvl), 0, 0, slot.itemId, slot.bodyLocation });
                    applyName(badges.back());
                }
            }
        }
    }

    // 3. Collect Virtual Class Skills (STAT 107) & Oskills (STAT 97)
    for (const auto& slot : slots) {
        for (uint16_t sId = 1; sId <= 300; ++sId) {
            // STAT 107 (Single Skill Bonus): only query if skill matches companion's virtual class
            if (companionai::IsSkillMatchingVirtualClass(vclass, static_cast<int>(sId))) {
                const int32_t singleLvl = getStat(slot.itemUnit, 107, static_cast<int>(sId));
                if (singleLvl > 0) {
                    bool exists = false;
                    for (auto& b : badges) {
                        if (b.skillId == sId && b.type == SkillBadgeType::VirtualClassSkill) {
                            b.level = static_cast<uint8_t>(b.level + singleLvl);
                            exists = true;
                            break;
                        }
                    }
                    if (!exists && badges.size() < MaxBadges) {
                        badges.push_back({ sId, SkillBadgeType::VirtualClassSkill, static_cast<uint8_t>(singleLvl), 0, 0, slot.itemId, slot.bodyLocation });
                        applyName(badges.back());
                    }
                }
            }

            // STAT 97 (Non-Class Oskill)
            const int32_t oskillLvl = getStat(slot.itemUnit, 97, static_cast<int>(sId));
            if (oskillLvl > 0) {
                bool exists = false;
                for (auto& b : badges) {
                    if (b.skillId == sId && b.type == SkillBadgeType::Oskill) {
                        b.level = static_cast<uint8_t>(b.level + oskillLvl);
                        exists = true;
                        break;
                    }
                }
                if (!exists && badges.size() < MaxBadges) {
                    badges.push_back({ sId, SkillBadgeType::Oskill, static_cast<uint8_t>(oskillLvl), 0, 0, slot.itemId, slot.bodyLocation });
                    applyName(badges.back());
                }
            }
        }
    }

    // Compute effective skill level for virtual class skills including class (Stat 83) and tab (Stat 188) bonuses
    std::array<uintptr_t, 4> itemUnits{};
    size_t uCount = 0;
    for (const auto& s : slots) {
        if (s.itemUnit && uCount < itemUnits.size()) itemUnits[uCount++] = s.itemUnit;
    }
    for (auto& b : badges) {
        if (b.type == SkillBadgeType::VirtualClassSkill) {
            const int totalLvl = companionai::ComputeCompanionEquipmentSkillBonus(
                vclass, b.skillId, std::span<const uintptr_t>(itemUnits.data(), uCount), getStat);
            if (totalLvl > b.level) {
                b.level = static_cast<uint8_t>(std::min(60, totalLvl));
            }
        }
    }

    return badges;
}

// Paints the RGBA buffer containing rendered mini skill badges with optional hover highlight
std::vector<uint8_t> PaintSkillsOverlay(
    const std::vector<MercenarySkillBadge>& badges,
    int width = OverlayWidth,
    int height = OverlayHeight,
    int hoverIndex = -1);

// Paints the native-styled compact Diablo II skill tooltip card for the hovered badge
std::vector<uint8_t> PaintSkillTooltipCard(
    const MercenarySkillBadge& badge,
    int width = TooltipWidth,
    int height = TooltipHeight);

} // namespace framevr::companionui
