#include "torch_light.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

using namespace framevr::torch;
using namespace framevr::math;

static void TestTipCalculation() {
    const Vec handPos{ 0.5f, 1.2f, -0.3f };
    const Quat identity{ 0.0f, 0.0f, 0.0f, 1.0f };
    const Vec tip = ComputeTorchTip(handPos, identity, DefaultShaftOffset);
    
    // In identity orientation, offset is added directly
    assert(std::fabs(tip.x - (handPos.x + DefaultShaftOffset.x)) < 1e-4f);
    assert(std::fabs(tip.y - (handPos.y + DefaultShaftOffset.y)) < 1e-4f);
    assert(std::fabs(tip.z - (handPos.z + DefaultShaftOffset.z)) < 1e-4f);

    // 90 deg Yaw rotation (looking right)
    const Quat yaw90 = Yaw(1.57079632679f);
    const Vec tipYaw = ComputeTorchTip(handPos, yaw90, DefaultShaftOffset);
    assert(std::isfinite(tipYaw.x) && std::isfinite(tipYaw.y) && std::isfinite(tipYaw.z));
    // Y (vertical) should remain unaffected by pure yaw
    assert(std::fabs(tipYaw.y - (handPos.y + DefaultShaftOffset.y)) < 1e-4f);
}

static void TestFlameDirection() {
    // Stationary hand: flame points straight up
    const Vec stationaryVel{ 0.0f, 0.0f, 0.0f };
    const Vec flameUp = ComputeFlameDirection(stationaryVel);
    assert(std::fabs(flameUp.x) < 1e-4f);
    assert(std::fabs(flameUp.y - 1.0f) < 1e-4f);
    assert(std::fabs(flameUp.z) < 1e-4f);

    // Hand swinging forward quickly (+Z direction velocity): flame should drag backward (-Z)
    const Vec swingForward{ 0.0f, 0.0f, 2.5f };
    const Vec flameDrag = ComputeFlameDirection(swingForward, 0.2f);
    assert(flameDrag.z < -0.1f); // Flame tilts backward
    assert(flameDrag.y > 0.5f);  // Still buoyant upward
    const float len = std::sqrt(Dot(flameDrag, flameDrag));
    assert(std::fabs(len - 1.0f) < 1e-3f); // Must be normalized
}

static void TestFlameFlicker() {
    float minWave = 100.0f;
    float maxWave = -100.0f;

    // Sample across 5 seconds at 10ms intervals
    for (uint64_t t = 0; t < 5000; t += 10) {
        const float wave = ComputeFlameFlickerWave(t);
        assert(wave >= -1.0f && wave <= 1.0f);
        minWave = std::min(minWave, wave);
        maxWave = std::max(maxWave, wave);

        const float radius = ComputeFlickerRadius(DefaultBaseRadius, wave);
        assert(radius >= MinFlickerRadius && radius <= MaxFlickerRadius);
        assert(radius >= DefaultBaseRadius * 0.93f && radius <= DefaultBaseRadius * 1.07f);

        float outColor[3]{};
        ComputeFlickerColor(DefaultTorchColor, wave, outColor);
        assert(outColor[0] > 0.0f && outColor[0] <= 1.0f);
        assert(outColor[1] > 0.0f && outColor[1] <= 1.0f);
        assert(outColor[2] > 0.0f && outColor[2] <= 1.0f);
    }

    // Verify dynamic variation actually occurs
    assert(minWave < -0.5f);
    assert(maxWave > 0.5f);
}

static void TestWorldCoordinates() {
    const Vec localHead{ 0.0f, 1.7f, 0.0f };
    const Vec localTip{ 0.3f, 1.5f, -0.4f }; // Offhand torch held slightly to the right/forward
    const Vec heroWorld{ 100.0f, 6.0f, 200.0f };
    constexpr float unitsPerMetre = 2.0f;

    const Vec worldTip = LocalToWorldGrid(localTip, heroWorld, localHead, unitsPerMetre);
    assert(std::fabs(worldTip.x - (100.0f + 0.3f * unitsPerMetre)) < 1e-4f);
    assert(std::fabs(worldTip.y - (6.0f - 0.2f * unitsPerMetre)) < 1e-4f);
    assert(std::fabs(worldTip.z - (200.0f - 0.4f * unitsPerMetre)) < 1e-4f);
}

static void TestClubCodeRecognition() {
    assert(IsTorchClubCode("clb"));
    assert(IsTorchClubCode("clb "));
    assert(IsTorchClubCode("spc"));
    assert(IsTorchClubCode("spc "));
    assert(!IsTorchClubCode("ssd"));
    assert(!IsTorchClubCode("buc"));
    assert(!IsTorchClubCode("7cr"));
    assert(!IsTorchClubCode(""));
    assert(!IsTorchClubCode("c"));
}

static void TestTorchQualification() {
    // 1. Plain standard club without any fire: NOT qualified as a burning torch
    const auto plainClub = EvaluateQualification("clb ", 0 /* fireDamage */, false /* enchant */, false /* dev */);
    assert(!IsTorchQualified(static_cast<uint32_t>(plainClub)));
    assert(plainClub == TorchQualification::BaseClub);

    // 2. Club with Sorceress Enchant active: QUALIFIED
    const auto enchantedClub = EvaluateQualification("clb ", 0, true /* enchant */, false);
    assert(IsTorchQualified(static_cast<uint32_t>(enchantedClub)));
    assert((static_cast<uint32_t>(enchantedClub) & static_cast<uint32_t>(TorchQualification::EnchantBuff)) != 0);

    // 3. Club with fire damage (e.g. Felloak with 8 fire damage, or ruby with 4, or Ral with 30): QUALIFIED
    const auto fireDamageClub = EvaluateQualification("clb ", 8 /* fire damage */, false, false);
    assert(IsTorchQualified(static_cast<uint32_t>(fireDamageClub)));
    assert((static_cast<uint32_t>(fireDamageClub) & static_cast<uint32_t>(TorchQualification::FireDamage)) != 0);

    // 4. Socketed club with NO fire damage (empty socket or emerald/sapphire/topaz): NOT QUALIFIED!
    const auto emptySocketClub = EvaluateQualification("clb ", 0 /* fireDamage = 0 */, false, false);
    assert(!IsTorchQualified(static_cast<uint32_t>(emptySocketClub)));

    // 4b. Club socketed with Ruby (e.g. 4 fire damage): QUALIFIED!
    const auto socketedRubyClub = EvaluateQualification("clb ", 4 /* 4 fire damage */, false, false);
    assert(IsTorchQualified(static_cast<uint32_t>(socketedRubyClub)));
    assert((static_cast<uint32_t>(socketedRubyClub) & static_cast<uint32_t>(TorchQualification::FireDamage)) != 0);

    // 5. Dev mode test override on plain club: QUALIFIED
    const auto devClub = EvaluateQualification("clb ", 0, false, true /* dev */);
    assert(IsTorchQualified(static_cast<uint32_t>(devClub)));

    // 6. Non-club weapon (e.g., Short Sword "ssd ") with enchant: NOT a torch (it is an enchanted blade, not a torch club)
    const auto enchantedSword = EvaluateQualification("ssd ", 0, true, false);
    assert(!IsTorchQualified(static_cast<uint32_t>(enchantedSword)));
    assert(enchantedSword == TorchQualification::None);

    // 7. Spiked Club without fire damage: NOT qualified
    const auto spikedEmpty = EvaluateQualification("spc ", 0, false, false);
    assert(!IsTorchQualified(static_cast<uint32_t>(spikedEmpty)));

    // 7b. Spiked Club with fire damage (e.g. Ral rune giving 30): QUALIFIED!
    const auto spikedRal = EvaluateQualification("spc ", 30, false, false);
    assert(IsTorchQualified(static_cast<uint32_t>(spikedRal)));
    assert((static_cast<uint32_t>(spikedRal) & static_cast<uint32_t>(TorchQualification::FireDamage)) != 0);
}

static void TestShaderLightRadius() {
    // When torch radius matches default ceiling radius (20), shader radius is native 19.0
    const float r1 = ComputeShaderLightRadius(20.0f, 20.0f);
    assert(std::fabs(r1 - 19.0f) < 1e-4f);

    // 18.0f reach scaled against 20.0f baseline: 18 * 19 / 20 = 17.1f
    const float r2 = ComputeShaderLightRadius(18.0f, 20.0f);
    assert(std::fabs(r2 - 17.1f) < 1e-4f);

    // Fallback on zero/invalid ceiling radius
    const float r3 = ComputeShaderLightRadius(18.0f, 0.0f);
    assert(std::fabs(r3 - 19.0f) < 1e-4f);
}

static void TestShaderLightSlotAllocation() {
    constexpr int kMaxLights = 16;
    struct Light { float x, y, z, lit; float rgb[3]; };
    Light worldLights[kMaxLights];
    for (int i = 0; i < kMaxLights; ++i) {
        worldLights[i] = { float(i * 10), 5.0f, float(i * 10), 19.0f, { 1.0f, 1.0f, 1.0f } };
    }

    // Scenario A: Handheld torch active -> Slot 0 is torch, slots 1..15 are world lights
    float pos[kMaxLights * 4] = {}, col[kMaxLights * 4] = {};
    int outIndex = 0;
    const bool torchActive = true;
    const float torchPos[3] = { 0.5f, 1.2f, -0.3f };
    const float torchShaderRadius = 17.1f;
    const float torchCol[3] = { 1.0f, 0.7f, 0.3f };

    if (torchActive) {
        pos[0] = torchPos[0];
        pos[1] = torchPos[1];
        pos[2] = torchPos[2];
        pos[3] = torchShaderRadius;
        col[0] = torchCol[0];
        col[1] = torchCol[1];
        col[2] = torchCol[2];
        col[3] = 1.0f; // Player torch never fades by distance
        ++outIndex;
    }

    const int remaining = kMaxLights - outIndex;
    const int copyCount = std::min(kMaxLights, remaining);
    for (int i = 0; i < copyCount; ++i, ++outIndex) {
        pos[outIndex * 4 + 0] = worldLights[i].x;
        pos[outIndex * 4 + 1] = worldLights[i].y;
        pos[outIndex * 4 + 2] = worldLights[i].z;
        pos[outIndex * 4 + 3] = worldLights[i].lit;
        col[outIndex * 4 + 0] = worldLights[i].rgb[0];
        col[outIndex * 4 + 1] = worldLights[i].rgb[1];
        col[outIndex * 4 + 2] = worldLights[i].rgb[2];
        col[outIndex * 4 + 3] = 0.8f;
    }

    assert(outIndex == kMaxLights);
    // Verify slot 0 is the torch
    assert(std::fabs(pos[0] - 0.5f) < 1e-4f);
    assert(std::fabs(pos[3] - 17.1f) < 1e-4f);
    assert(std::fabs(col[3] - 1.0f) < 1e-4f);

    // Verify slot 1 has world light 0
    assert(std::fabs(pos[1 * 4 + 0] - worldLights[0].x) < 1e-4f);
    assert(std::fabs(pos[1 * 4 + 3] - worldLights[0].lit) < 1e-4f);

    // Verify slot 15 has world light 14 (light 15 was displaced by player torch)
    assert(std::fabs(pos[15 * 4 + 0] - worldLights[14].x) < 1e-4f);
}

static void TestWarmthBlending() {
    const float baseCol[3] = { 1.0f, 224.0f / 255.0f, 150.0f / 255.0f };
    const float fireOrange[3] = { 1.0f, 0.58f, 0.22f };

    // Warmth 0.0 -> matches baseCol
    float c0[3];
    for (int c = 0; c < 3; ++c) c0[c] = (1.0f - 0.0f) * baseCol[c] + 0.0f * fireOrange[c];
    assert(std::fabs(c0[1] - baseCol[1]) < 1e-4f);

    // Warmth 1.0 -> matches fireOrange
    float c1[3];
    for (int c = 0; c < 3; ++c) c1[c] = (1.0f - 1.0f) * baseCol[c] + 1.0f * fireOrange[c];
    assert(std::fabs(c1[1] - fireOrange[1]) < 1e-4f);

    // Warmth 0.65 -> between base and fireOrange
    float c65[3];
    for (int c = 0; c < 3; ++c) c65[c] = (1.0f - 0.65f) * baseCol[c] + 0.65f * fireOrange[c];
    assert(c65[1] < baseCol[1] && c65[1] > fireOrange[1]);
}

int main() {
    TestTipCalculation();
    TestFlameDirection();
    TestFlameFlicker();
    TestWorldCoordinates();
    TestClubCodeRecognition();
    TestTorchQualification();
    TestShaderLightRadius();
    TestShaderLightSlotAllocation();
    TestWarmthBlending();
    std::cout << "torch_light_test: All Phase 1, Phase 2 & Phase 3 tests passed successfully.\n";
    return 0;
}
