// The left pass's command lists as the game submitted them, and the
// submissions the stereo replay makes of them. Adapted from
// natpoh/diablo2-vr-port 8bd1d3b vr/replay_groups.h (MIT, Copyright (c) 2026
// BodyWalkVR). Pure: steamvr/replay_groups_test.cpp.
//
// The replay runs the left pass's lists again for the right eye. Upstream
// found one ExecuteCommandLists for a queue's whole run of lists ran the
// direct queue into a list that never ended; one call a list with a signal
// after each never hung. The lists were recorded for the game's own calls (a
// resource promoted out of COMMON decays when an ExecuteCommandLists ends,
// and the driver flushes there), so the replay can also submit them exactly
// as the game did: the same lists in the same calls.
//
// ReShade fires execute_command_list once for each list of a call, in order,
// then makes the call; the call itself is seen by a hook on the queue's
// ExecuteCommandLists (vr/ecl_hook.h), which numbers each captured list (Tag).
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace replaygroups {

// [stereo] replay_marks
enum Mode {
    kBatched = 0,        // a queue's whole run in ONE call, no signals (upstream: hangs)
    kPerListSignal = 1,  // a call a list, a signal after each
    kPerList = 2,        // a call a list, no signals
    kGame = 3,           // the game's calls, no signals of ours between them
    kGameSignal = 4,     // the game's calls, a signal after each
};

inline bool SignalAfterEach(int mode) { return mode == kPerListSignal || mode == kGameSignal; }

// The game made call `id` (non-zero) with lists[0..n) on queue `q`: the entries
// of `subs` captured for them get the number. Their events came just before the
// call, so they are near the end (other threads' may be between). Entries not
// found are not an error; ones already numbered are left. Returns how many were
// numbered. Sub needs: q, cl, call.
template <class Sub>
inline unsigned Tag(std::vector<Sub>& subs, const void* q, const void* const* lists, unsigned n, uint32_t id, size_t window = 128) {
    unsigned tagged = 0;
    const size_t floor = subs.size() > window ? subs.size() - window : 0;
    for (unsigned k = n; k-- > 0;) {
        for (size_t i = subs.size(); i-- > floor;) {
            if ((const void*)subs[i].cl == lists[k] && (const void*)subs[i].q == q && subs[i].call == 0) { subs[i].call = id; ++tagged; break; }
        }
    }
    return tagged;
}

struct Batch { size_t first; size_t count; };   // subs[first .. first + count): one ExecuteCommandLists

// One queue's run of lists, subs[from, to), as submissions. A list without a
// call number (its call was not seen) is a call of its own.
template <class Sub>
inline std::vector<Batch> PlanRun(const std::vector<Sub>& subs, size_t from, size_t to, int mode) {
    std::vector<Batch> out;
    if (to > subs.size()) to = subs.size();
    if (from >= to) return out;
    if (mode == kBatched) { out.push_back({from, to - from}); return out; }
    if (mode == kGame || mode == kGameSignal) {
        size_t i = from;
        while (i < to) {
            size_t j = i + 1;
            if (subs[i].call != 0) while (j < to && subs[j].call == subs[i].call) ++j;
            out.push_back({i, j - i});
            i = j;
        }
        return out;
    }
    for (size_t i = from; i < to; ++i) out.push_back({i, 1});
    return out;
}

}  // namespace replaygroups
