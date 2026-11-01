#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

// Arcane Decay: Low Mana Visual Warning.
// A subtle, gothic, spectral visual warning that progressively appears around the
// edges of the player's vision when their mana falls below a configurable percentage.
//
// Purely visual: does not modify game mechanics, original game tables, mana
// calculations, spell behavior, or character attributes.
namespace framevr::arcane {

constexpr int Size = 512;
constexpr float DefaultThreshold = 50.0f; // percent (5..90)
constexpr float DefaultIntensity = 1.0f;  // 0..1 (from 0..100 percent)
constexpr float DefaultHysteresis = 3.0f; // percentage points
constexpr float FadeInRate = 2.5f;        // per second
constexpr float FadeOutRate = 1.5f;       // per second

struct Config {
    bool enabled = true;
    float threshold = DefaultThreshold;
    float intensity = DefaultIntensity;
    float hysteresis = DefaultHysteresis;
    bool fadeAnimation = true;
    float fadeInRate = FadeInRate;
    float fadeOutRate = FadeOutRate;
};

struct Input {
    double time = 0.0;
    uint64_t session = 0;
    float mana = 0.0f;
    float maxMana = 0.0f;
    bool vitalsKnown = false;
    Config config{};
};

struct Output {
    float manaPercent = 100.0f;
    float depletion = 0.0f;
    float intensity = 0.0f;
    float pulse = 0.0f;
    bool active = false;
    bool visible = false;
};

// Computes normalized depletion given mana percentage and threshold:
// depletion = clamp((threshold - manaPercent) / threshold, 0.0f, 1.0f)
inline float CalculateDepletion(float manaPercent, float threshold) {
    if (!std::isfinite(manaPercent) || !std::isfinite(threshold) || threshold <= 0.0f) {
        return 0.0f;
    }
    return std::clamp((threshold - manaPercent) / threshold, 0.0f, 1.0f);
}

// Arcane Decay Controller: manages activation state, hysteresis, and smoothed intensity.
class Controller {
    bool active_ = false;
    float manaPercent_ = 100.0f;
    float depletion_ = 0.0f;
    float targetIntensity_ = 0.0f;
    float currentIntensity_ = 0.0f;
    double lastTime_ = 0.0;
    uint64_t session_ = 0;

public:
    void Reset() {
        active_ = false;
        manaPercent_ = 100.0f;
        depletion_ = 0.0f;
        targetIntensity_ = 0.0f;
        currentIntensity_ = 0.0f;
        lastTime_ = 0.0;
        session_ = 0;
    }

    Output Step(const Input& in) {
        Output out;
        if (in.session != session_) {
            Reset();
            session_ = in.session;
        }

        const double dtRaw = (lastTime_ > 0.0 && in.time >= lastTime_) ? (in.time - lastTime_) : 0.0;
        const float dt = (dtRaw > 0.0 && dtRaw <= 0.5) ? static_cast<float>(dtRaw) : 0.0f;
        lastTime_ = in.time;

        const auto& cfg = in.config;

        // Mana data validation: safe handling for zero maximum mana, negative or non-finite values
        const bool validMana = in.vitalsKnown &&
                               std::isfinite(in.mana) &&
                               std::isfinite(in.maxMana) &&
                               in.maxMana > 0.0f &&
                               in.mana >= 0.0f;

        if (!cfg.enabled || cfg.intensity <= 0.0f || !validMana) {
            targetIntensity_ = 0.0f;
            depletion_ = 0.0f;
            active_ = false;
            if (validMana) {
                const float clamped = std::clamp(in.mana, 0.0f, in.maxMana);
                manaPercent_ = 100.0f * (clamped / in.maxMana);
            } else {
                manaPercent_ = 100.0f;
            }
        } else {
            const float clampedMana = std::clamp(in.mana, 0.0f, in.maxMana);
            manaPercent_ = 100.0f * (clampedMana / in.maxMana);

            // Hysteresis logic:
            // - Activate when mana falls strictly below threshold
            // - Deactivate when mana rises to or above threshold + hysteresis
            constexpr float Epsilon = 1e-4f;
            if (manaPercent_ < cfg.threshold) {
                active_ = true;
            } else if (manaPercent_ >= (cfg.threshold + cfg.hysteresis - Epsilon)) {
                active_ = false;
            }

            if (active_) {
                depletion_ = CalculateDepletion(manaPercent_, cfg.threshold);
                const float scale = std::clamp(cfg.intensity, 0.0f, 1.0f);
                targetIntensity_ = depletion_ * scale;
            } else {
                depletion_ = 0.0f;
                targetIntensity_ = 0.0f;
            }
        }

        // Transitions: delta-time-based exponential smoothing
        if (!cfg.enabled) {
            // Disabling immediately targets zero intensity
            currentIntensity_ = 0.0f;
        } else if (!cfg.fadeAnimation || dt <= 0.0f) {
            currentIntensity_ = targetIntensity_;
        } else {
            const float rate = (targetIntensity_ > currentIntensity_) ? cfg.fadeInRate : cfg.fadeOutRate;
            currentIntensity_ += (targetIntensity_ - currentIntensity_) * (1.0f - std::exp(-rate * dt));
            if (std::fabs(currentIntensity_ - targetIntensity_) < 0.001f) {
                currentIntensity_ = targetIntensity_;
            }
        }
        currentIntensity_ = std::clamp(currentIntensity_, 0.0f, 1.0f);

        // Atmospheric pulse: subtle, slow sinusoidal modulation (~0.6 Hz)
        constexpr double PulseFrequency = 0.60; // Hz
        const double phase = in.time * (2.0 * 3.14159265358979323846 * PulseFrequency);
        const float pulseVal = static_cast<float>(0.5 + 0.5 * std::sin(phase));

        out.manaPercent = manaPercent_;
        out.depletion = depletion_;
        out.intensity = currentIntensity_;
        out.pulse = pulseVal;
        out.active = active_;
        out.visible = (currentIntensity_ > 0.004f);

        return out;
    }
};

// Generates 512x512 straight-RGBA overlay image containing the 4 visual layers:
// 1. Spectral Vignette (dark sapphire blue shading)
// 2. Arcane Mist (translucent blue/cyan drift)
// 3. Spectral Filaments (thin glowing strands)
// 4. Arcane Pulse (subtle atmospheric pulse that deepens with depletion)
inline std::vector<uint8_t> Compose(float intensity, float depletion, double time) {
    std::vector<uint8_t> rgba(static_cast<size_t>(Size) * Size * 4, 0);
    if (!std::isfinite(intensity) || !std::isfinite(depletion) || intensity <= 0.004f) {
        return rgba;
    }

    intensity = std::clamp(intensity, 0.0f, 1.0f);
    depletion = std::clamp(depletion, 0.0f, 1.0f);

    // Subtle atmospheric pulse factor: at critically low mana, breathing amplitude increases
    constexpr double PulseFreq = 0.60;
    const float pulseCycle = static_cast<float>(0.5 + 0.5 * std::sin(time * (2.0 * 3.14159265358979323846 * PulseFreq)));
    const float pulseMod = 1.0f + 0.18f * depletion * pulseCycle;

    const float t = static_cast<float>(time);

    for (int y = 0; y < Size; ++y) {
        const float v = ((y + 0.5f) / static_cast<float>(Size) * 2.0f - 1.0f);
        for (int x = 0; x < Size; ++x) {
            const float u = ((x + 0.5f) / static_cast<float>(Size) * 2.0f - 1.0f);

            // Elliptical radius matching the visor aspect ratio: keeps the central viewing area unobstructed
            const float r = std::sqrt(u * u * 0.85f + v * v);
            if (r <= 0.52f) {
                continue; // Clear center
            }

            const float theta = std::atan2(v, u * 0.92f);

            // Layer 1: Spectral Vignette (dark sapphire blue)
            const float vigT = std::clamp((r - 0.55f) / 0.45f, 0.0f, 1.0f);
            const float vigCurve = vigT * vigT * (3.0f - 2.0f * vigT);
            const float alpha1 = 0.50f * vigCurve * intensity;

            // Layer 2: Arcane Mist (translucent blue/cyan drift around peripheral rim)
            const float mistBand = std::clamp((r - 0.60f) / 0.38f, 0.0f, 1.0f);
            const float drift1 = std::sin(theta * 3.0f + t * 0.35f);
            const float drift2 = std::cos(theta * 5.0f - t * 0.22f + r * 2.5f);
            const float mistNoise = 0.5f + 0.25f * drift1 + 0.25f * drift2;
            const float alpha2 = 0.35f * mistBand * mistNoise * intensity * pulseMod;

            // Layer 3: Spectral Filaments (occasional thin glowing strands within mist)
            const float filBand = std::clamp((r - 0.68f) / 0.30f, 0.0f, 1.0f);
            const float strandOsc = std::sin(theta * 7.0f + t * 0.18f) * std::cos(theta * 11.0f - t * 0.14f + r * 3.0f);
            const float strandSharp = std::pow(std::max(0.0f, strandOsc), 6.0f);
            const float alpha3 = 0.30f * filBand * strandSharp * intensity * (1.0f + 0.25f * depletion * pulseCycle);

            // Layer 4: Combine layers into straight RGBA with Gothic Diablo I/II tones
            const float totalAlpha = std::clamp(alpha1 + alpha2 + alpha3, 0.0f, 1.0f);
            if (totalAlpha <= 0.002f) {
                continue;
            }

            // Weighted color blend between:
            // Vignette: (8, 22, 54)
            // Mist:     (18, 68, 108)
            // Filament: (45, 125, 195)
            const float sum = std::max(0.001f, alpha1 + alpha2 + alpha3);
            const float cr = (8.0f * alpha1 + 18.0f * alpha2 + 45.0f * alpha3) / sum;
            const float cg = (22.0f * alpha1 + 68.0f * alpha2 + 125.0f * alpha3) / sum;
            const float cb = (54.0f * alpha1 + 108.0f * alpha2 + 195.0f * alpha3) / sum;

            auto* p = &rgba[(static_cast<size_t>(y) * Size + x) * 4];
            p[0] = static_cast<uint8_t>(std::clamp(std::lround(cr), 0L, 255L));
            p[1] = static_cast<uint8_t>(std::clamp(std::lround(cg), 0L, 255L));
            p[2] = static_cast<uint8_t>(std::clamp(std::lround(cb), 0L, 255L));
            p[3] = static_cast<uint8_t>(std::clamp(std::lround(255.0f * totalAlpha), 0L, 255L));
        }
    }
    return rgba;
}

// Generates repaint key: only triggers a worker repaint when intensity, depletion,
// or procedural animation phase has visibly changed.
inline uint64_t PaintKey(float intensity, float depletion, double time) {
    if (!std::isfinite(intensity) || !std::isfinite(depletion) || intensity <= 0.004f) {
        return 0;
    }
    const uint64_t iKey = static_cast<uint64_t>(std::lround(std::clamp(intensity, 0.0f, 1.0f) * 128.0f));
    const uint64_t dKey = static_cast<uint64_t>(std::lround(std::clamp(depletion, 0.0f, 1.0f) * 64.0f));
    // Time quantized to ~25 Hz for smooth, efficient procedural animation
    const uint64_t tKey = static_cast<uint64_t>(std::lround(time * 25.0));
    return (iKey << 40) | (dKey << 32) | (tKey & 0xFFFFFFFFull) | 1ull;
}

} // namespace framevr::arcane

