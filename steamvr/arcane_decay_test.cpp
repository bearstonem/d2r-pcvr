#include "arcane_decay.h"
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <thread>

using namespace framevr::arcane;

static int testsPassed = 0;

static void Expect(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
    ++testsPassed;
}

static void ExpectNear(float actual, float expected, float eps, const char* message) {
    if (std::fabs(actual - expected) > eps) {
        std::fprintf(stderr, "FAIL: %s (actual=%.5f, expected=%.5f, diff=%.5f)\n",
                     message, actual, expected, std::fabs(actual - expected));
        std::exit(1);
    }
    ++testsPassed;
}

int main() {
    // 1. Initial configuration & defaults
    {
        Config cfg;
        Expect(cfg.enabled == true, "Default enabled is true");
        ExpectNear(cfg.threshold, 50.0f, 0.001f, "Default threshold is 50%");
        ExpectNear(cfg.intensity, 1.0f, 0.001f, "Default intensity is 100%");
        ExpectNear(cfg.hysteresis, 3.0f, 0.001f, "Default hysteresis is 3 percentage points");
        Expect(cfg.fadeAnimation == true, "Default fadeAnimation is true");
    }

    // 2. Depletion and intensity calculation across the test table from spec:
    // Expected behavior at a 50% threshold:
    // - 60% mana: invisible (depletion = 0)
    // - 50% mana: invisible (depletion = 0)
    // - 40% mana: 20% intensity
    // - 35% mana: 30% intensity
    // - 25% mana: 50% intensity
    // - 10% mana: 80% intensity
    // - 0% mana: maximum intensity (100%)
    {
        Controller ctrl;
        Input in;
        in.time = 1.0;
        in.session = 1;
        in.vitalsKnown = true;
        in.maxMana = 100.0f;
        in.config.threshold = 50.0f;
        in.config.intensity = 1.0f;
        in.config.fadeAnimation = false; // Snap for exact formula test

        // 60% mana
        in.mana = 60.0f;
        auto out = ctrl.Step(in);
        Expect(!out.active, "60% mana: inactive");
        ExpectNear(out.depletion, 0.0f, 0.001f, "60% mana: depletion 0");
        ExpectNear(out.intensity, 0.0f, 0.001f, "60% mana: intensity 0");
        Expect(!out.visible, "60% mana: not visible");

        // 50% mana
        in.mana = 50.0f;
        out = ctrl.Step(in);
        Expect(!out.active, "50% mana: inactive (boundary condition)");
        ExpectNear(out.depletion, 0.0f, 0.001f, "50% mana: depletion 0");
        ExpectNear(out.intensity, 0.0f, 0.001f, "50% mana: intensity 0");

        // 40% mana -> 20% intensity
        in.mana = 40.0f;
        out = ctrl.Step(in);
        Expect(out.active, "40% mana: active");
        ExpectNear(out.depletion, 0.20f, 0.001f, "40% mana: depletion 0.20");
        ExpectNear(out.intensity, 0.20f, 0.001f, "40% mana: intensity 0.20 (20%)");
        Expect(out.visible, "40% mana: visible");

        // 35% mana -> 30% intensity
        in.mana = 35.0f;
        out = ctrl.Step(in);
        ExpectNear(out.depletion, 0.30f, 0.001f, "35% mana: depletion 0.30");
        ExpectNear(out.intensity, 0.30f, 0.001f, "35% mana: intensity 0.30 (30%)");

        // 25% mana -> 50% intensity
        in.mana = 25.0f;
        out = ctrl.Step(in);
        ExpectNear(out.depletion, 0.50f, 0.001f, "25% mana: depletion 0.50");
        ExpectNear(out.intensity, 0.50f, 0.001f, "25% mana: intensity 0.50 (50%)");

        // 10% mana -> 80% intensity
        in.mana = 10.0f;
        out = ctrl.Step(in);
        ExpectNear(out.depletion, 0.80f, 0.001f, "10% mana: depletion 0.80");
        ExpectNear(out.intensity, 0.80f, 0.001f, "10% mana: intensity 0.80 (80%)");

        // 0% mana -> 100% maximum intensity
        in.mana = 0.0f;
        out = ctrl.Step(in);
        ExpectNear(out.depletion, 1.00f, 0.001f, "0% mana: depletion 1.00");
        ExpectNear(out.intensity, 1.00f, 0.001f, "0% mana: intensity 1.00 (100%)");
    }

    // 3. Configured Intensity Scaling
    {
        Controller ctrl;
        Input in;
        in.time = 1.0;
        in.session = 1;
        in.vitalsKnown = true;
        in.maxMana = 100.0f;
        in.mana = 25.0f; // 50% depletion at 50% threshold
        in.config.threshold = 50.0f;
        in.config.intensity = 0.5f; // Configured to 50% intensity
        in.config.fadeAnimation = false;

        auto out = ctrl.Step(in);
        ExpectNear(out.depletion, 0.50f, 0.001f, "Depletion is 50%");
        ExpectNear(out.intensity, 0.25f, 0.001f, "Intensity scaled by 0.5 -> 25%");
    }

    // 4. Zero Configured Intensity
    {
        Controller ctrl;
        Input in;
        in.time = 1.0;
        in.session = 1;
        in.vitalsKnown = true;
        in.maxMana = 100.0f;
        in.mana = 10.0f;
        in.config.threshold = 50.0f;
        in.config.intensity = 0.0f; // 0%
        in.config.fadeAnimation = false;

        auto out = ctrl.Step(in);
        Expect(!out.active, "Zero configured intensity stays inactive");
        ExpectNear(out.intensity, 0.0f, 0.001f, "Intensity is 0");
        Expect(!out.visible, "Not visible when configured intensity is 0");
    }

    // 5. Disabled State
    {
        Controller ctrl;
        Input in;
        in.time = 1.0;
        in.session = 1;
        in.vitalsKnown = true;
        in.maxMana = 100.0f;
        in.mana = 10.0f;
        in.config.enabled = false;
        in.config.fadeAnimation = false;

        auto out = ctrl.Step(in);
        Expect(!out.active, "Disabled state is inactive");
        ExpectNear(out.intensity, 0.0f, 0.001f, "Disabled state produces 0 intensity");
        Expect(!out.visible, "Disabled state is not visible");
    }

    // 6. Hysteresis (Activate < 50%, Deactivate >= 53%)
    {
        Controller ctrl;
        Input in;
        in.time = 1.0;
        in.session = 1;
        in.vitalsKnown = true;
        in.maxMana = 100.0f;
        in.config.threshold = 50.0f;
        in.config.hysteresis = 3.0f;
        in.config.intensity = 1.0f;
        in.config.fadeAnimation = false;

        // Start at 55% -> inactive
        in.mana = 55.0f;
        auto out = ctrl.Step(in);
        Expect(!out.active, "Start at 55%: inactive");

        // Drop to 51% -> still >= threshold -> inactive
        in.mana = 51.0f;
        out = ctrl.Step(in);
        Expect(!out.active, "51%: still inactive");

        // Drop to 49% -> below threshold -> activates!
        in.mana = 49.0f;
        out = ctrl.Step(in);
        Expect(out.active, "49%: activated");
        ExpectNear(out.depletion, (50.0f - 49.0f) / 50.0f, 0.001f, "49%: correct depletion");

        // Rise to 50% -> between threshold and threshold + hysteresis (50..53) -> remains active!
        in.mana = 50.0f;
        out = ctrl.Step(in);
        Expect(out.active, "50%: remains active due to hysteresis");
        ExpectNear(out.depletion, 0.0f, 0.001f, "50%: depletion is 0.0f (no visual jump)");

        // Rise to 52% -> still < 53% -> remains active!
        in.mana = 52.0f;
        out = ctrl.Step(in);
        Expect(out.active, "52%: remains active due to hysteresis (< 53%)");
        ExpectNear(out.depletion, 0.0f, 0.001f, "52%: depletion is 0.0f");

        // Rise to 53% -> reaches threshold + hysteresis -> deactivates!
        in.mana = 53.0f;
        out = ctrl.Step(in);
        Expect(!out.active, "53%: deactivates at threshold + hysteresis");
        ExpectNear(out.intensity, 0.0f, 0.001f, "53%: intensity is 0");

        // Drop back to 52% -> was inactive, so remains inactive!
        in.mana = 52.0f;
        out = ctrl.Step(in);
        Expect(!out.active, "52%: remains inactive because did not fall below 50%");
    }

    // 7. Invalid Mana Values & Zero Maximum Mana
    {
        Controller ctrl;
        Input in;
        in.time = 1.0;
        in.session = 1;
        in.vitalsKnown = true;
        in.config.fadeAnimation = false;

        // Zero max mana
        in.mana = 0.0f;
        in.maxMana = 0.0f;
        auto out = ctrl.Step(in);
        Expect(!out.active, "Zero max mana: inactive");
        ExpectNear(out.intensity, 0.0f, 0.001f, "Zero max mana: intensity 0");

        // Negative max mana
        in.mana = 50.0f;
        in.maxMana = -100.0f;
        out = ctrl.Step(in);
        Expect(!out.active, "Negative max mana: inactive");

        // Negative current mana
        in.mana = -10.0f;
        in.maxMana = 100.0f;
        out = ctrl.Step(in);
        Expect(!out.active, "Negative current mana: safely inactive");

        // Non-finite (NaN / Inf)
        in.mana = std::numeric_limits<float>::quiet_NaN();
        in.maxMana = 100.0f;
        out = ctrl.Step(in);
        Expect(!out.active, "NaN mana: inactive");

        in.mana = 10.0f;
        in.maxMana = std::numeric_limits<float>::infinity();
        out = ctrl.Step(in);
        Expect(!out.active, "Infinity max mana: inactive");

        // vitalsKnown = false
        in.vitalsKnown = false;
        in.mana = 10.0f;
        in.maxMana = 100.0f;
        out = ctrl.Step(in);
        Expect(!out.active, "vitalsKnown=false: inactive");
        ExpectNear(out.intensity, 0.0f, 0.001f, "vitalsKnown=false: 0 intensity");
    }

    // 8. Delta-time exponential smoothing (Fade in / Fade out)
    {
        Controller ctrl;
        Input in;
        in.time = 1.0;
        in.session = 1;
        in.vitalsKnown = true;
        in.maxMana = 100.0f;
        in.mana = 100.0f;
        in.config.threshold = 50.0f;
        in.config.intensity = 1.0f;
        in.config.fadeAnimation = true;
        in.config.fadeInRate = 2.5f;
        in.config.fadeOutRate = 1.5f;

        // Initial step at full mana
        ctrl.Step(in);

        // Instant drop to 0% mana (targetIntensity = 1.0f)
        in.mana = 0.0f;
        in.time = 1.1; // dt = 0.1s
        auto out = ctrl.Step(in);

        // Expected intensity after dt = 0.1s with rate 2.5:
        // I = 0 + (1 - 0) * (1 - exp(-2.5 * 0.1)) = 1 - exp(-0.25) ~ 0.2212
        const float expectedFadeIn = 1.0f - std::exp(-2.5f * 0.1f);
        ExpectNear(out.intensity, expectedFadeIn, 0.01f, "Fade-in exponential step at dt=0.1s");

        // Advance 2.0s -> should be practically at 1.0f
        in.time = 3.1;
        out = ctrl.Step(in);
        ExpectNear(out.intensity, 1.0f, 0.01f, "Fade-in settles near 1.0f after 2 seconds");

        // Now mana potion! Mana recovered to 100% -> targetIntensity = 0.0f
        in.mana = 100.0f;
        in.time = 3.2; // dt = 0.1s
        out = ctrl.Step(in);

        // Fade out at rate 1.5:
        // I = 1.0 - (1.0 - 0) * (1 - exp(-1.5 * 0.1)) = exp(-0.15) ~ 0.8607
        const float expectedFadeOut = std::exp(-1.5f * 0.1f);
        ExpectNear(out.intensity, expectedFadeOut, 0.02f, "Fade-out exponential step at dt=0.1s");

        // Advance 3.0s -> should settle to 0.0f
        in.time = 6.2;
        out = ctrl.Step(in);
        ExpectNear(out.intensity, 0.0f, 0.005f, "Fade-out settles to 0.0f after recovery");
        Expect(!out.visible, "Invisible after fade-out settles");
    }

    // 9. Instant zero when disabled
    {
        Controller ctrl;
        Input in;
        in.time = 1.0;
        in.session = 1;
        in.vitalsKnown = true;
        in.maxMana = 100.0f;
        in.mana = 0.0f;
        in.config.threshold = 50.0f;
        in.config.intensity = 1.0f;
        in.config.fadeAnimation = false;

        auto out = ctrl.Step(in);
        ExpectNear(out.intensity, 1.0f, 0.001f, "Full intensity when enabled");

        // Disable immediately
        in.config.enabled = false;
        in.time = 1.05;
        out = ctrl.Step(in);
        ExpectNear(out.intensity, 0.0f, 0.001f, "Disabling immediately drops intensity to 0");
        Expect(!out.visible, "Disabling is immediately invisible");
    }

    // 10. Pixel generation & visual layers validation
    {
        // Invisible -> empty/zero buffer
        auto emptyPixels = Compose(0.0f, 0.0f, 0.0);
        bool allZero = true;
        for (uint8_t b : emptyPixels) {
            if (b != 0) { allZero = false; break; }
        }
        Expect(allZero, "Zero intensity produces completely transparent buffer");

        // Visible composition
        auto pixels = Compose(0.8f, 0.8f, 2.5);
        Expect(pixels.size() == size_t(Size) * Size * 4, "Buffer size matches 512x512 RGBA");

        // Verify central viewing area is completely clear (r <= 0.52)
        // Center pixel at (Size/2, Size/2)
        const int cx = Size / 2;
        const int cy = Size / 2;
        const size_t centerIdx = (static_cast<size_t>(cy) * Size + cx) * 4;
        Expect(pixels[centerIdx + 3] == 0, "Center pixel alpha is 0 (unobstructed central vision)");

        // Check a disk around the center (e.g. radius 50 pixels)
        bool centerDiscClear = true;
        for (int dy = -40; dy <= 40; ++dy) {
            for (int dx = -40; dx <= 40; ++dx) {
                if (dx * dx + dy * dy < 40 * 40) {
                    const size_t idx = (static_cast<size_t>(cy + dy) * Size + (cx + dx)) * 4;
                    if (pixels[idx + 3] != 0) {
                        centerDiscClear = false;
                        break;
                    }
                }
            }
        }
        Expect(centerDiscClear, "Central disk is completely clear");

        // Verify rim pixels have gothic blue/cyan color and alpha > 0
        // Near outer edge (e.g., x = 20, y = 20)
        const size_t rimIdx = (static_cast<size_t>(20) * Size + 20) * 4;
        const uint8_t r = pixels[rimIdx];
        const uint8_t g = pixels[rimIdx + 1];
        const uint8_t b = pixels[rimIdx + 2];
        const uint8_t a = pixels[rimIdx + 3];

        Expect(a > 0, "Rim pixel has visible alpha");
        Expect(b > r, "Gothic blue tone has blue dominant over red");
        Expect(b > g || g > r, "Gothic blue/cyan spectral tone");

        // PaintKey generation
        uint64_t k1 = PaintKey(0.8f, 0.5f, 1.0);
        uint64_t k2 = PaintKey(0.8f, 0.5f, 1.0);
        uint64_t k3 = PaintKey(0.8f, 0.5f, 1.5);
        uint64_t k4 = PaintKey(0.2f, 0.1f, 1.0);
        Expect(k1 != 0, "PaintKey is non-zero when visible");
        Expect(k1 == k2, "PaintKey is deterministic for same inputs");
        Expect(k1 != k3, "PaintKey changes over time for procedural drift");
        Expect(k1 != k4, "PaintKey changes for different intensity/depletion");
        Expect(PaintKey(0.0f, 0.0f, 1.0) == 0, "PaintKey is 0 when invisible");
    }

    // 12. Painter worker tests
    {
        Painter painter;
        Expect(painter.Start(), "Painter worker starts successfully");
        Expect(!painter.Busy(), "Painter is not busy initially");

        PaintJob job{0.8f, 0.5f, 1.0};
        Expect(painter.Submit(job), "Painter submits job successfully");
        Expect(painter.Busy(), "Painter is busy after submit");
        Expect(!painter.Submit(job), "Painter rejects duplicate submit while busy (prevents worker starvation)");

        PaintImage img;
        int retries = 0;
        while (!painter.Take(img) && retries++ < 200) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        Expect(!img.pixels.empty(), "Painter produces non-empty image");
        Expect(img.key != 0, "Painter produces non-zero paint key");
        Expect(!painter.Busy(), "Painter is not busy after Take");

        // Discard test
        Expect(painter.Submit(job), "Painter submits another job");
        painter.Discard();
        Expect(!painter.Busy(), "Painter is not busy after Discard");

        painter.Stop();
    }

    std::printf("arcane_decay_test: all %d checks passed.\n", testsPassed);
    return 0;
}

