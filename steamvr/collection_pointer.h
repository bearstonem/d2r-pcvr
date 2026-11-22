#pragma once
// Collection panel pointing: the laser's place on the panel, the trigger and the
// clock become hover and clicks. Pure logic on a millisecond clock, so the tests
// drive it with made-up times (collection_pointer_test.cpp) and transport only
// feeds it the laser.
//
// The rules are the ones the VR Settings menu already uses, because a hand does
// the same things at either panel:
// - Pre-trigger lock: a press goes to what was under the laser LockMs before the
//   trigger went down (the hand dips as it pulls).
// - Hysteresis: hover stays on a slot until the laser is HysteresisPx outside it.
// - Click on release, unless the laser is then further than ReleaseSlopPx away.
// - A trigger already held when the panel opens must be released first: opening
//   the panel never presses.
#include <cstdint>

#include "collection_panel.h"

namespace framevr::collection::pointer {

using panel::Hit;
using panel::HitKind;
using panel::Layout;
using panel::Pick;
using panel::Rect;

namespace detail {

inline bool Same(const Hit& a, const Hit& b) { return a.kind == b.kind && a.index == b.index; }

inline const Rect* RectOf(const Layout& layout, const Hit& hit) {
    switch (hit.kind) {
    case HitKind::Tab:      return hit.index >= 0 && hit.index < layout.tabCount ? &layout.tabs[hit.index] : nullptr;
    case HitKind::Page:     return &layout.track;
    case HitKind::Sort:     return hit.index >= 0 && hit.index < int(panel::Sort::Count) ? &layout.sortOptions[hit.index] : nullptr;
    case HitKind::SortBox:  return &layout.sortBox;
    case HitKind::ShowBox:  return &layout.showBox;
    case HitKind::Group:    return hit.index >= 0 && hit.index < panel::GroupCount ? &layout.groups[hit.index] : nullptr;
    case HitKind::Band:     return hit.index >= 0 && hit.index < panel::BandCount ? &layout.bands[hit.index] : nullptr;
    case HitKind::Previous: return &layout.previous;
    case HitKind::Next:     return &layout.next;
    case HitKind::Size:     return hit.index >= 0 && hit.index < int(panel::Size::Count) ? &layout.sizeSteps[hit.index] : nullptr;
    case HitKind::Show:     return hit.index >= 0 && hit.index < int(panel::Show::Count) ? &layout.showOptions[hit.index] : nullptr;
    case HitKind::Slot:     return hit.index >= 0 && hit.index < layout.slotCount ? &layout.slots[hit.index] : nullptr;
    default:                return nullptr;
    }
}

// Whether (x, y) is inside the hit's rect, grown by slop on every side.
inline bool Near(const Layout& layout, const Hit& hit, float x, float y, float slop) {
    const Rect* rect = RectOf(layout, hit);
    if (rect == nullptr) return false;
    return x >= rect->x - slop && y >= rect->y - slop && x < rect->x + rect->width + slop && y < rect->y + rect->height + slop;
}

}   // namespace detail

class Filter {
public:
    static constexpr uint64_t LockMs         = 90;
    static constexpr float    HysteresisPx   = 10.0f;
    static constexpr float    ReleaseSlopPx  = 24.0f;
    static constexpr int      HistorySamples = 12;

    // Forgets hover, presses and the sample history. A trigger still down is
    // remembered, so it must be released before it can press again.
    void Reset(bool trigger = false) {
        hover_ = {};
        locked_ = {};
        pressed_ = {};
        for (Sample& sample : history_) sample = {};
        next_    = 0;
        held_    = false;
        blocked_ = trigger;
    }

    // One frame: where the laser is on the panel (canvas pixels), the layout it
    // is over, the trigger and the clock. Returns the click, else a Hit of None.
    Hit Step(float x, float y, const Layout& layout, bool trigger, uint64_t now) {
        const Hit raw = Pick(layout, x, y);
        history_[next_] = {raw, now, true};
        next_ = (next_ + 1) % HistorySamples;

        // Hover: another element under the laser takes it; leaving the hovered
        // one only counts past the hysteresis ring, so hand tremor at an edge
        // does not flicker.
        if (raw.kind != HitKind::None) {
            if (!detail::Same(raw, hover_)) hover_ = raw;
        } else if (hover_.kind != HitKind::None && !detail::Near(layout, hover_, x, y, HysteresisPx)) {
            hover_ = {};
        }

        if (blocked_) {
            if (!trigger) blocked_ = false;
            return {};
        }

        Hit click {};
        if (trigger && !held_) {
            held_    = true;
            locked_  = SampleAt(now - LockMs);
            pressed_ = locked_;
        } else if (!trigger && held_) {
            held_ = false;
            if (detail::Near(layout, pressed_, x, y, ReleaseSlopPx)) click = pressed_;
            pressed_ = {};
        }
        return click;
    }

    const Hit& Hover() const { return hover_; }
    bool       Held() const { return held_; }

private:
    struct Sample {
        Hit      hit {};
        uint64_t at   = 0;
        bool     used = false;
    };

    // What the laser was over LockMs before the press: the newest sample at or
    // before that time, else the oldest one kept.
    Hit SampleAt(uint64_t target) const {
        const Sample* atOrBefore = nullptr;
        const Sample* earliest   = nullptr;
        for (const Sample& sample : history_) {
            if (!sample.used) continue;
            if (earliest == nullptr || sample.at < earliest->at) earliest = &sample;
            if (sample.at <= target && (atOrBefore == nullptr || sample.at > atOrBefore->at)) atOrBefore = &sample;
        }
        const Sample* chosen = atOrBefore != nullptr ? atOrBefore : earliest;
        return chosen != nullptr ? chosen->hit : Hit {};
    }

    Hit    hover_ {};
    Hit    locked_ {};
    Hit    pressed_ {};
    Sample history_[HistorySamples] {};
    int    next_    = 0;
    bool   held_    = false;
    bool   blocked_ = false;   // a trigger was already down when the panel opened
};

}   // namespace framevr::collection::pointer
