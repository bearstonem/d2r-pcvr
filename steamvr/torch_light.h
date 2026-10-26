#pragma once
#include "frame_math.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace framevr::torch {

using math::Vec;
using math::Quat;

// Default geometry for a one-handed club torch:
// OpenXR grip coordinates: +Y points up along handle/knuckles, -Z forward.
// The torch head is situated at the top of the handle (~56cm along shaft).
inline constexpr Vec DefaultShaftOffset{0.0f, 0.48f, -0.29f};

// Dedicated skill identifier for handheld torch flame rendering
inline constexpr int TorchSkill = 999;

// Lighting parameters aligned with D2R native torch reaches (18.0 - 20.0 units)
inline constexpr float DefaultBaseRadius = 18.0f;
inline constexpr float MinFlickerRadius = 14.0f;
inline constexpr float MaxFlickerRadius = 22.0f;

// Native torch warm color base (pale yellow-gold toward fire orange)
inline constexpr float DefaultTorchColor[3] = { 1.0f, 224.0f / 255.0f, 150.0f / 255.0f };

// Complete state of an active handheld torch emitter
struct TorchEmitter {
    bool active = false;
    bool isOffHand = true;              // Held in off-hand (left) or primary hand (right)
    Vec localTipPosition{};             // Position in XR play space (metres)
    Vec worldPosition{};                // Position in D2R world grid units (matching heroX/heroZ)
    Vec flameDirection{0.0f, 1.0f, 0.0f}; // Upright flame vector with velocity drag
    float radius = DefaultBaseRadius;   // Current reach in D2R world units
    float intensity = 1.0f;             // Brightness multiplier
    float rgb[3] = { DefaultTorchColor[0], DefaultTorchColor[1], DefaultTorchColor[2] };
    uint32_t qualificationFlags = 0;    // Bitmask of TorchQualification flags
};

// Computes the 3D tip position of the torch from a hand pose and shaft offset
inline Vec ComputeTorchTip(Vec handPos, Quat handRot, Vec shaftOffset = DefaultShaftOffset) {
    return handPos + math::Rotate(handRot, shaftOffset);
}

// Computes apparent flame tilt due to hand movement/wind drag
// Base flame points up {0, 1, 0}; velocity pulls the flame backward
inline Vec ComputeFlameDirection(Vec linearVelocity, float dragFactor = 0.12f) {
    const Vec buoyantUp{0.0f, 1.0f, 0.0f};
    const Vec drag = linearVelocity * -dragFactor;
    const Vec combined = buoyantUp + drag;
    const Vec normalized = math::Normalized(combined);
    return math::Dot(normalized, normalized) > 0.01f ? normalized : buoyantUp;
}

// Evaluates non-harmonic multi-frequency sinusoidal flame flicker [-1.0f, 1.0f]
// Uses prime-ratio wave components (3.1 Hz, 7.3 Hz, 13.7 Hz, 23.9 Hz) to ensure
// continuous, organic, non-repeating turbulent flickering without abrupt jumps.
inline float ComputeFlameFlickerWave(uint64_t timestampMs) {
    constexpr float TwoPi = 6.28318530718f;
    const float t = float(timestampMs % 10000000ULL) * 0.001f;
    const float s1 = std::sin(t * 3.1f * TwoPi);
    const float s2 = std::sin(t * 7.3f * TwoPi);
    const float s3 = std::sin(t * 13.7f * TwoPi);
    const float s4 = std::sin(t * 23.9f * TwoPi);
    // Weighted sum yields normalized range [-1.0f, 1.0f]
    return 0.45f * s1 + 0.30f * s2 + 0.15f * s3 + 0.10f * s4;
}

// Applies organic flicker modulation to light radius (default ±6% variation)
inline float ComputeFlickerRadius(float baseRadius, float flickerWave, float modulation = 0.06f) {
    const float modulated = baseRadius * (1.0f + modulation * std::clamp(flickerWave, -1.0f, 1.0f));
    return std::clamp(modulated, MinFlickerRadius, MaxFlickerRadius);
}

// Applies organic color warmth shift: slightly deeper orange on dips, brighter golden on surges
inline void ComputeFlickerColor(const float baseRgb[3], float flickerWave, float outRgb[3], float colorMod = 0.05f) {
    const float wave = std::clamp(flickerWave, -1.0f, 1.0f);
    // Red stays saturated at ~1.0; Green and Blue modulate slightly
    outRgb[0] = std::clamp(baseRgb[0] * (1.0f + 0.02f * wave), 0.0f, 1.0f);
    outRgb[1] = std::clamp(baseRgb[1] * (1.0f + colorMod * wave), 0.0f, 1.0f);
    outRgb[2] = std::clamp(baseRgb[2] * (1.0f + colorMod * 1.5f * wave), 0.0f, 1.0f);
}

// Scales torch reach radius against the ceiling shader's baseline reach (baseline: 20.0 units with standard torch 19.0)
inline float ComputeShaderLightRadius(float torchRadius, float ceilTorchRadius = 20.0f) {
    return (ceilTorchRadius > 0.001f) ? (torchRadius * 19.0f / ceilTorchRadius) : 19.0f;
}

// Converts XR play space position (metres relative to head) to D2R engine world units
inline Vec LocalToWorldGrid(Vec localTip, Vec heroWorldPos, Vec localHeadPos, float unitsPerMetre = 2.0f) {
    const Vec delta = localTip - localHeadPos;
    return {
        heroWorldPos.x + delta.x * unitsPerMetre,
        heroWorldPos.y + delta.y * unitsPerMetre,
        heroWorldPos.z + delta.z * unitsPerMetre
    };
}

// Checks whether an item code corresponds to a club capable of acting as a torch
inline bool IsTorchClubCode(std::string_view code) {
    if (code.size() < 3) return false;
    return code.substr(0, 3) == "clb" || code.substr(0, 3) == "spc";
}

// Conditions that qualify a one-handed club as a dynamic burning torch
enum class TorchQualification : uint32_t {
    None = 0,
    BaseClub = 1 << 0,         // Base item is 1H Club ("clb") or Spiked Club ("spc")
    FireDamage = 1 << 1,       // Weapon deals fire damage (> 0)
    EnchantBuff = 1 << 2,      // Player is affected by Sorceress Enchant spell (state 16)
    DevOverride = 1 << 3       // Explicit dev-mode override / test preset
};

inline constexpr uint32_t TorchFireMask = 
    static_cast<uint32_t>(TorchQualification::FireDamage) |
    static_cast<uint32_t>(TorchQualification::EnchantBuff) |
    static_cast<uint32_t>(TorchQualification::DevOverride);

// Evaluates whether a set of qualification flags satisfies the torch requirements
inline bool IsTorchQualified(uint32_t flags) {
    const bool isClub = (flags & static_cast<uint32_t>(TorchQualification::BaseClub)) != 0;
    const bool hasFire = (flags & TorchFireMask) != 0;
    return isClub && hasFire;
}

// Evaluates an equipped weapon's properties to produce the qualification flags.
// Pure fire-damage rule:
// A club qualifies as a torch IF AND ONLY IF it has actual fire damage (> 0),
// Sorceress Enchant buff is active, or dev mode override is enabled.
// No checks by sockets, socketed count, affixes, or Felloak ID remain.
inline TorchQualification EvaluateQualification(std::string_view code, uint32_t fireDamage, bool enchantActive, bool devMode = false) {
    if (!IsTorchClubCode(code)) return TorchQualification::None;
    
    uint32_t flags = static_cast<uint32_t>(TorchQualification::BaseClub);
    if (devMode) {
        flags |= static_cast<uint32_t>(TorchQualification::DevOverride);
    }
    if (enchantActive) {
        flags |= static_cast<uint32_t>(TorchQualification::EnchantBuff);
    }
    if (fireDamage > 0) {
        flags |= static_cast<uint32_t>(TorchQualification::FireDamage);
    }
    return static_cast<TorchQualification>(flags);
}

} // namespace framevr::torch
