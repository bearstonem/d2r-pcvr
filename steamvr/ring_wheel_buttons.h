#pragma once
// One owner per hand button among the Ring skill menu's own buttons: the Glyphs and Gestures menus (stored by the ring,
// ring_wheel_config.h) and Quick cast (the radial's button, steamvr/quick_radial.h). Each keeps a button as the slot + 1 (0 none),
// set by the ring's hold to assign. Binding one takes the button from the others (the newest bind wins, said in the log), and the
// hold names what it replaces. The ring calls Claim from its own Applied. A skill put on a button by the ring's own bind makes
// every menu on it let go (LetGo), so a later clear of that skill never brings a menu back on that button without a word.
#include <cstdio>
#include "quick_radial.h"
#include "ring_wheel_config.h"

namespace framevr::ringwheel::buttons {
enum class Owner { Glyphs, Gestures, QuickCast, None };
inline constexpr const char* OwnerName[3] = {"Glyphs menu", "Gestures menu", "Quick cast"};
inline int SlotOfKey(float v) { const int s = int(v + .5f) - 1; return s >= 0 && s < 10 ? s : -1; }
inline int SlotOf(Owner o) {
    switch (o) {
    case Owner::Glyphs: return SlotOfKey(cfg::Value(cfg::GlyphsButton));
    case Owner::Gestures: return SlotOfKey(cfg::Value(cfg::GesturesButton));
    case Owner::QuickCast: return quickradial::ButtonSlot();
    default: return -1;
    }
}
inline void Let(Owner o) {   // that owner's button goes to none
    switch (o) {
    case Owner::Glyphs: cfg::Set(cfg::GlyphsButton, 0); break;
    case Owner::Gestures: cfg::Set(cfg::GesturesButton, 0); break;
    case Owner::QuickCast: quickradial::SetButton(0); break;
    default: break;
    }
}
inline constexpr Owner All[3] = {Owner::Glyphs, Owner::Gestures, Owner::QuickCast};
// The menu button on `slot` (other than `mine`), or nullptr: the hold's "Replaces <name>".
inline const char* OwnerOf(int slot, Owner mine = Owner::None) {
    if (slot < 0) return nullptr;
    for (const Owner o : All) if (o != mine && SlotOf(o) == slot) return OwnerName[int(o)];
    return nullptr;
}
// `mine` has just been put on `slot`: every other menu button on it lets go, said once each.
inline int Claim(int slot, Owner mine, void (*say)(const char*)) {
    int n = 0;
    if (slot < 0 || mine == Owner::None) return 0;
    for (const Owner o : All) {
        if (o == mine || SlotOf(o) != slot) continue;
        Let(o); ++n;
        if (say) { char line[200]; std::snprintf(line, sizeof line, "Frame ring wheel: button slot %d is %s's now; %s let go of it", slot, OwnerName[int(mine)], OwnerName[int(o)]); say(line); }
    }
    return n;
}
// A skill was put on `slot` by the ring's own bind: every menu button on it lets go, said once each.
inline int LetGo(int slot, void (*say)(const char*)) {
    int n = 0;
    if (slot < 0) return 0;
    for (const Owner o : All) {
        if (SlotOf(o) != slot) continue;
        Let(o); ++n;
        if (say) { char line[200]; std::snprintf(line, sizeof line, "Frame ring wheel: %s let go of button slot %d (a skill was put there)", OwnerName[int(o)], slot); say(line); }
    }
    return n;
}
}  // namespace framevr::ringwheel::buttons
