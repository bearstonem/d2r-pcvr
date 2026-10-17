#pragma once
#include <array>
namespace framevr::dev {
// Curated from the last published release, including Windows previews.
// Update this summary when publishing; never populate it with unreleased PRs.
inline constexpr const char* ReleaseTitle="v0.3.17 / Last published release";
inline constexpr const char* ReleaseTag="v0.3.17-windows-preview";
inline constexpr const char* ReleaseUrl="https://github.com/bearstonem/d2r-pcvr/releases/tag/v0.3.17-windows-preview";
inline constexpr std::array<const char*,14> ReleaseLines{{
    "D2R PCVR is now named D2R VR Unleashed.",
    "Bow quiver: select an arrow skill with its assigned face button.",
    "Skill arrows use native mana costs, damage and effects.",
    "The nocked arrow shows the selected skill's model and particles.",
    "Arrow impact bursts follow the contact point on low shots.",
    "Toss health, rejuvenation, antidote or thawing potions to your mercenary.",
    "Mercenaries turn red below 50% life; the tint clears on recovery.",
    "Building roofs remain visible when entering in first person.",
    "Hanging fixtures advance physics once per stereo pair.",
    "Larger renderer and entity pools target Act III allocation crashes.",
    "Unleashed installs separately under mods/D2R-PCVR.",
    "Own plugin identity: d2rl-pcvr.dll and d2r_pcvr.ini.",
    "Update / repair preserves settings, calibration and normal saves.",
    "Setup explains Windows Security blocks and checks for enabled HDR."
}};
}
