#include "companion_item_skills_ui.h"
#include "interaction_icons.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace framevr::companionui;
using namespace framevr::companionai;

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::printf("Running companion_item_skills_ui_test...\n");

    // TEST 1: HirelingPanelOpen detection
    {
        const std::vector<std::string> openWithMerc = { "HUDPanel", "HirelingInventoryPanel", "MiniMenuPanel" };
        assert(HirelingPanelOpen(openWithMerc));

        const std::vector<std::string> openWithAlt = { "HirelingInventory" };
        assert(HirelingPanelOpen(openWithAlt));

        const std::vector<std::string> openWithHireMenu = { "HireMenuPanel" };
        assert(HirelingPanelOpen(openWithHireMenu));

        const std::vector<std::string> openOther = { "HUDPanel", "PlayerInventoryExpansionLayout", "SkillsTreePanel", "CharacterStatsPanel" };
        assert(!HirelingPanelOpen(openOther));

        const std::vector<std::string> openEmpty = {};
        assert(!HirelingPanelOpen(openEmpty));
        std::printf("  [PASS] Test 1: HirelingPanelOpen\n");
    }

    // TEST 2: PlaceOverlay, SubImageRegion & TooltipSubImageRegion geometry
    {
        const float canvasW = 1.9f;
        const float canvasH = 1.06875f;

        // Valid spot
        const auto spot1 = PlaceOverlay(canvasW, canvasH, 1);
        assert(spot1.width > 0.0f);
        assert(spot1.height > 0.0f);
        assert(spot1.x < 0.0f); // Placed on left half of the canvas
        assert(spot1.y < 0.0f); // Placed in lower half (skills region)
        assert(spot1.u < 0.5f);
        assert(spot1.v > 0.5f);

        // Multiple badges increase width up to OverlayWidth
        const auto spot4 = PlaceOverlay(canvasW, canvasH, 4);
        assert(spot4.width > spot1.width);
        assert(spot4.height == spot1.height);

        // Max badges clamp width
        const auto spotMax = PlaceOverlay(canvasW, canvasH, 12);
        const auto spotLimit = PlaceOverlay(canvasW, canvasH, 8);
        assert(spotMax.width == spotLimit.width);

        // Invalid inputs
        const auto spotZero = PlaceOverlay(0.0f, canvasH, 2);
        assert(spotZero.width == 0.0f && spotZero.height == 0.0f);
        const auto spotNoBadges = PlaceOverlay(canvasW, canvasH, 0);
        assert(spotNoBadges.width == 0.0f && spotNoBadges.height == 0.0f);

        // SubImageRegion test
        struct MockRegion { int x, y, w, h; };
        const MockRegion region{ 1288, 0, 512, 64 };
        const auto sub1 = SubImageRegion(region, 1);
        assert(sub1.width > 0 && sub1.height == BadgeCellSize);
        assert(sub1.x >= region.x && sub1.x + sub1.width <= region.x + region.w);

        const auto sub4 = SubImageRegion(region, 4);
        assert(sub4.width > sub1.width);
        assert(sub4.height == BadgeCellSize);
        assert(sub4.x >= region.x && sub4.x + sub4.width <= region.x + region.w);

        const auto subMax = SubImageRegion(region, 8);
        const auto subOver = SubImageRegion(region, 16);
        assert(subMax.width == subOver.width);

        // TooltipSubImageRegion test
        const MockRegion tRegion{ 1288, 70, 440, 220 };
        const auto tSub = TooltipSubImageRegion(tRegion);
        assert(tSub.width == TooltipWidth);
        assert(tSub.height == TooltipHeight);
        assert(tSub.x == tRegion.x + (tRegion.w - TooltipWidth) / 2);
        assert(tSub.y == tRegion.y + (tRegion.h - TooltipHeight) / 2);

        std::printf("  [PASS] Test 2: PlaceOverlay, SubImageRegion, TooltipSubImageRegion\n");
    }

    // TEST 3: HitTestBadge and PlaceTooltip
    {
        const float canvasW = 1.9f;
        const float canvasH = 1.06875f;
        const auto spot = PlaceOverlay(canvasW, canvasH, 4);

        // Laser pointing directly at the center of the first badge (left side of strip)
        const float halfW = (spot.width / canvasW) * 0.5f;
        const float hitU_first = spot.u - halfW * 0.7f;
        const float hitV_center = spot.v;
        const int hitBadge0 = HitTestBadge(canvasW, canvasH, 4, hitU_first, hitV_center);
        assert(hitBadge0 >= 0 && hitBadge0 < 4);

        // Laser pointing outside (right of the strip)
        const int hitOutside = HitTestBadge(canvasW, canvasH, 4, spot.u + halfW + 0.1f, hitV_center);
        assert(hitOutside == -1);

        // Tooltip placement floats above the badge strip
        const auto tooltipSpot = PlaceTooltip(canvasW, canvasH, spot);
        assert(tooltipSpot.width > 0.0f && tooltipSpot.height > 0.0f);
        assert(tooltipSpot.x == spot.x);
        assert(tooltipSpot.y > spot.y); // Floating above
        std::printf("  [PASS] Test 3: HitTestBadge and PlaceTooltip\n");
    }

    // TEST 4: CollectMercenarySkillBadges from mock equipped items with dynamic names
    {
        std::map<std::tuple<uintptr_t, int, int>, int32_t> mockStats;

        constexpr uintptr_t itemHelm = 0x1001;
        constexpr uintptr_t itemArmor = 0x1002;
        constexpr uintptr_t itemWeapon = 0x1003;
        constexpr uintptr_t itemShield = 0x1004;

        // Slot 1 (Helm): Item with Level 12 Holy Freeze Aura (Stat 151, Skill 114)
        mockStats[{itemHelm, 151, 114}] = 12;

        // Slot 3 (Armor): Item with +3 to Paladin skill (Might, Skill 98, Stat 107)
        mockStats[{itemArmor, 107, 98}] = 3;

        // Slot 4 (Weapon): Item with Level 5 Hydra charges (20/20 charges)
        const uint16_t paramHydra = PackChargedSkillParam(221, 5);
        const int32_t valHydra = PackChargedSkillValue(20, 20);
        mockStats[{itemWeapon, 204, static_cast<int>(paramHydra)}] = valHydra;

        // Slot 5 (Shield): Item with +1 Battle Orders (Skill 149, non-class oskill, Stat 97)
        mockStats[{itemShield, 97, 149}] = 1;

        const std::array<EquippedItemSlot, 4> slots = {{
            { 101, 4, itemWeapon },
            { 102, 1, itemHelm },
            { 103, 3, itemArmor },
            { 104, 5, itemShield }
        }};

        auto getStat = [&](uintptr_t unit, int stat, int param) -> int32_t {
            auto it = mockStats.find({unit, stat, param});
            return (it != mockStats.end()) ? it->second : 0;
        };

        auto getName = [](int id) -> std::string {
            if (id == 221) return "Hidra";
            if (id == 114) return "Frio Sagrado";
            if (id == 98) return "Poderio";
            if (id == 149) return "Ordenes de Batalla";
            return "";
        };

        // Case A: Paladin virtual class (Act 2 Desert Guard) with custom name callback
        const auto badges = CollectMercenarySkillBadges(VirtualPlayerClass::Paladin, slots, getStat, getName);
        assert(badges.size() == 4);

        bool foundHydra = false, foundHolyFreeze = false, foundMight = false, foundBO = false;
        for (const auto& b : badges) {
            if (b.skillId == 221) {
                assert(b.type == SkillBadgeType::ChargedSkill);
                assert(b.level == 5);
                assert(b.currentCharges == 20);
                assert(b.maxCharges == 20);
                assert(b.itemId == 101);
                assert(std::strcmp(b.skillName, "Hidra") == 0);
                foundHydra = true;
            } else if (b.skillId == 114) {
                assert(b.type == SkillBadgeType::Aura);
                assert(b.level == 12);
                assert(b.itemId == 102);
                assert(std::strcmp(b.skillName, "Frio Sagrado") == 0);
                foundHolyFreeze = true;
            } else if (b.skillId == 98) {
                assert(b.type == SkillBadgeType::VirtualClassSkill);
                assert(b.level == 3);
                assert(b.itemId == 103);
                assert(std::strcmp(b.skillName, "Poderio") == 0);
                foundMight = true;
            } else if (b.skillId == 149) {
                assert(b.type == SkillBadgeType::Oskill);
                assert(b.level == 1);
                assert(b.itemId == 104);
                assert(std::strcmp(b.skillName, "Ordenes de Batalla") == 0);
                foundBO = true;
            }
        }
        assert(foundHydra);
        assert(foundHolyFreeze);
        assert(foundMight);
        assert(foundBO);

        // Case B: Default fallback name when getName is omitted
        const auto badgesDefault = CollectMercenarySkillBadges(VirtualPlayerClass::Paladin, slots, getStat);
        assert(badgesDefault.size() == 4);
        assert(std::strcmp(badgesDefault[0].skillName, "Skill 221") == 0);

        // Case C: Amazon virtual class (Act 1 Rogue) -> Might (Paladin skill) should NOT match as virtual class skill
        const auto badgesZon = CollectMercenarySkillBadges(VirtualPlayerClass::Amazon, slots, getStat);
        bool zonHasMight = false;
        for (const auto& b : badgesZon) {
            if (b.skillId == 98 && b.type == SkillBadgeType::VirtualClassSkill) {
                zonHasMight = true;
            }
        }
        assert(!zonHasMight);

        // Case D: VirtualPlayerClass::None -> returns empty badges
        const auto badgesNone = CollectMercenarySkillBadges(VirtualPlayerClass::None, slots, getStat);
        assert(badgesNone.empty());

        // Case E: Passing preDiscoveredCharges reuses pre-scanned charged skills
        std::vector<CompanionChargedSkill> preCharges;
        CompanionChargedSkill hydraCharge{};
        hydraCharge.itemId = 101;
        hydraCharge.bodyLocation = 4;
        hydraCharge.skillId = 221;
        hydraCharge.level = 5;
        hydraCharge.currentCharges = 18;
        hydraCharge.maxCharges = 20;
        preCharges.push_back(hydraCharge);

        const auto badgesPre = CollectMercenarySkillBadges(VirtualPlayerClass::Paladin, slots, getStat, getName, preCharges);
        assert(badgesPre.size() == 4);
        bool foundPreHydra = false;
        for (const auto& b : badgesPre) {
            if (b.skillId == 221) {
                assert(b.type == SkillBadgeType::ChargedSkill);
                assert(b.currentCharges == 18);
                foundPreHydra = true;
            }
        }
        assert(foundPreHydra);

        std::printf("  [PASS] Test 4: CollectMercenarySkillBadges (with dynamic localized names and pre-scanned charges)\n");
    }

    // TEST 5: PaintSkillsOverlay output with hover
    {
        // Empty badges -> transparent buffer
        const auto emptyPixels = PaintSkillsOverlay({}, 512, 64);
        assert(emptyPixels.size() == 512 * 64 * 4);
        bool allZero = true;
        for (uint8_t p : emptyPixels) {
            if (p != 0) { allZero = false; break; }
        }
        assert(allZero);

        // Populated badges -> non-empty rendered pixels
        std::vector<MercenarySkillBadge> testBadges = {
            { 221, SkillBadgeType::ChargedSkill, 5, 20, 20, 101, 4, "Hidra" },
            { 114, SkillBadgeType::Aura, 12, 0, 0, 102, 1, "Frio Sagrado" },
            { 98, SkillBadgeType::VirtualClassSkill, 3, 0, 0, 103, 3, "Poderio" }
        };

        const auto renderedPixels = PaintSkillsOverlay(testBadges, 512, 64, 0); // Hovering badge 0
        assert(renderedPixels.size() == 512 * 64 * 4);

        int nonZeroCount = 0;
        for (uint8_t p : renderedPixels) {
            if (p > 0) nonZeroCount++;
        }
        assert(nonZeroCount > 100);
        std::printf("  [PASS] Test 5: PaintSkillsOverlay (with hover highlight)\n");
    }

    // TEST 6: PaintSkillTooltipCard output (compact native style)
    {
        MercenarySkillBadge hydraBadge{ 221, SkillBadgeType::ChargedSkill, 5, 20, 20, 101, 4, "Hidra" };
        const auto tooltipPixels = PaintSkillTooltipCard(hydraBadge, 440, 220);
        assert(tooltipPixels.size() == 440 * 220 * 4);

        int nonZeroCount = 0;
        for (uint8_t p : tooltipPixels) {
            if (p > 0) nonZeroCount++;
        }
        assert(nonZeroCount > 500); // Title, subtitle, description drawn
        std::printf("  [PASS] Test 6: PaintSkillTooltipCard\n");
    }

    std::printf("All companion_item_skills_ui tests passed successfully!\n");
    framevr::interaction::icons::Shutdown();
    return 0;
}
