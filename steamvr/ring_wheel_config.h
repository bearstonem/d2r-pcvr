#pragma once
// The Ring skill menu's numbers. The player's own choice is one VR Settings row (Menus > Ring skill menu, On or Off); everything
// else is fixed here at the values tuned in the headset. A value is read by
// Value / On and can be changed in memory by Set (the Effects preset keeps its rows in step; the Reset place press clears itself);
// only the three sub-menu buttons persist: each is the hand button (slot + 1, 0 none) its sub-menu is bound to, kept in
// d2r_pcvr.ini [ring_wheel] by ring_wheel.cpp through the Persist hook below (pure here: the tests install their own).
#include <array>
#include <cmath>

namespace framevr::ringwheel::cfg {
enum Key {
    Distance,      // cm ahead of the eyes the wheel opens
    Width,         // cm, the skills' circle
    Below,         // degrees below eye level
    WorldLock,     // 1: stays put in the room while open
    FooterFull,    // 0: footer tabs under the wheel
    RenderPct,     // pixels drawn per wheel pixel, percent
    HoldMs,        // hold to assign, ms
    TapMs,         // a press released before this is a tap, ms
    GrowMs,        // center grow delay, ms
    BlurbMs,       // help text delay, ms
    Anim,          // Effects preset: 0 Off, 1 Floating icons, 2 Effect on hover, 3 All effects, 4 Custom
    Float,         // icons drift
    Glow,          // element glow: 0 Off, 1 pointed only, 2 everywhere
    HoverFx,       // effect on the pointed skill
    FaceFx,        // the pointed icon's art moves
    Burst,         // Tree orbs fly-out
    CrawlS,        // lightning crawl at rest, seconds
    TreeLayout,    // 0 Trees shown, 1 Tree orbs
    OtherTrees,    // how visible the other trees stay while one is pointed, percent
    BadgeAngle,    // bound-button badge: -15 Auto, else degrees clockwise from 12
    BadgeDepth,    // 0 outside, 1 on outline, 2 inside, 3 center
    BadgeSize,     // px
    PointingHand,  // 0: either hand may point
    Grab,          // grip to move the wheel
    GrabKeep,      // remember the moved place
    GrabReset,     // a press puts it back (turns itself Off)
    Icon,          // skill circle layers: picture
    ColorRing,     // ... its color ring
    ElementFrame,  // ... the element frame
    FxLayer,       // ... the animation layer
    ThirdGrow,     // percent the pointed center third grows
    ThirdDim,      // percent opacity of the other thirds
    MenuEntries,   // the sub-menus on the General shelf
    GlyphsButton, GesturesButton, QuickButton,   // each sub-menu's hand button, slot + 1 (0 none); persisted
    TreeIcon,      // 0 Drawn, 1 Painted circle, 2 Painted square
    TreeIconSize,  // percent of the drawn mark
    GlyphIcon,     // 0 Drawn, 1 Painted circle, 2 Painted square
    GlyphIconSize, // percent
    Count };

inline std::array<float, Count>& Values() {
    static std::array<float, Count> v = [] {
        std::array<float, Count> a{};
        a[Distance] = 90; a[Width] = 70; a[Below] = 8; a[WorldLock] = 1; a[FooterFull] = 0; a[RenderPct] = 125;
        a[HoldMs] = 1000; a[TapMs] = 350; a[GrowMs] = 150; a[BlurbMs] = 100;
        a[Anim] = 1; a[Float] = 1; a[Glow] = 0; a[HoverFx] = 0; a[FaceFx] = 0; a[Burst] = 1; a[CrawlS] = 0;
        a[TreeLayout] = 0; a[OtherTrees] = 80;
        a[BadgeAngle] = -15; a[BadgeDepth] = 1; a[BadgeSize] = 28; a[PointingHand] = 0;
        a[Grab] = 1; a[GrabKeep] = 1; a[GrabReset] = 0;
        a[Icon] = 1; a[ColorRing] = 1; a[ElementFrame] = 0; a[FxLayer] = 0;
        a[ThirdGrow] = 11; a[ThirdDim] = 70; a[MenuEntries] = 1;
        a[GlyphsButton] = 0; a[GesturesButton] = 0; a[QuickButton] = 0;
        a[TreeIcon] = 1; a[TreeIconSize] = 150; a[GlyphIcon] = 1; a[GlyphIconSize] = 100;
        return a;
    }();
    return v;
}
inline bool Persisted(Key k) { return k == GlyphsButton || k == GesturesButton || k == QuickButton; }
// ring_wheel.cpp installs the writer; null (the tests): nothing is written.
inline void (*&Persist())(Key, float) { static void (*f)(Key, float) = nullptr; return f; }
inline float Value(Key k) { return k >= 0 && k < Count ? Values()[size_t(k)] : 0.f; }
inline bool On(Key k) { return Value(k) > .5f; }
inline void Set(Key k, float v) {
    if (k < 0 || k >= Count || !std::isfinite(v)) return;
    Values()[size_t(k)] = v;
    if (Persisted(k) && Persist()) Persist()(k, v);
}
inline void Load(Key k, float v) { if (k >= 0 && k < Count && std::isfinite(v)) Values()[size_t(k)] = v; }   // from the ini, not written back
}  // namespace framevr::ringwheel::cfg
