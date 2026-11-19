#pragma once
// Lab Ring wheel (T025, review-r6 X4): waiting for the reply to a ring bind, assign or glyph, as plain data. Pure and
// Windows-free (ring-wheel-test).
//
// Transport's request queue (interactionstate::Request) drops a request silently while another is queued or waiting, so a
// ring bind could get no reply at all: no log, no "Refused", no pulse. Two rules now:
//  - the Lab sends its next request only once the one before is answered (else transport would drop the new one);
//  - a request with no answer within upstream's own limit (interactions.h: a pending request is Refused after 1.5 s) is
//    refused, so every request the ring sends is answered exactly once.
#include <cstdint>
#include "interactions.h"

namespace framevr::ringwheel {
constexpr double ReplyTimeoutMs = 1500;   // upstream's engine: a pending request is Refused after 1.5 s
enum class ReplyEnd { None, Applied, Refused, TimedOut };
inline const char* ReplyEndName(ReplyEnd e) {
    switch (e) {
    case ReplyEnd::Applied: return "applied";
    case ReplyEnd::Refused: return "refused";
    case ReplyEnd::TimedOut: return "refused (no reply in 1.5 s)";
    default: return "";
    }
}
inline bool IsRefusal(ReplyEnd e) { return e == ReplyEnd::Refused || e == ReplyEnd::TimedOut; }
class ReplyWait {
    uint64_t seq = 0;
    double sentMs = 0;
public:
    void Sent(uint64_t sequence, double nowMs) { seq = sequence; sentMs = nowMs; }
    // This frame's reply (any request's): ours and final ends the wait; Applied or Dispatched = applied, the rest refused.
    ReplyEnd Reply(uint64_t sequence, interaction::Result r) {
        if (!seq || sequence != seq || r == interaction::Result::Pending || r == interaction::Result::None) return ReplyEnd::None;
        seq = 0;
        return r == interaction::Result::Applied || r == interaction::Result::Dispatched ? ReplyEnd::Applied : ReplyEnd::Refused;
    }
    // Once a frame, after Reply: no answer within the limit ends the wait as refused.
    ReplyEnd Step(double nowMs) {
        if (!seq || nowMs - sentMs < ReplyTimeoutMs) return ReplyEnd::None;
        seq = 0;
        return ReplyEnd::TimedOut;
    }
    void Clear() { seq = 0; }
    bool Waiting() const { return seq != 0; }
};
// The ring may send its next request (a bind or a glyph) only when none of its own waits for an answer.
inline bool CanSend(const ReplyWait& bind, const ReplyWait& glyph) { return !bind.Waiting() && !glyph.Waiting(); }
// Requests made but not sent yet (perf's X4 check: one queued slot let a third quick assign overwrite the second, unsent and
// unanswered). First in, first out, sent one at a time as CanSend allows; full = the new one is refused out loud by the
// caller (Push false), never one dropped in silence. Four is far more than a 1 s hold-to-assign can fill while one
// request waits (a reply comes within a few frames, 1.5 s at most).
constexpr int SendDepth = 4;
template <class T, int N = SendDepth> class SendQueue {
    T item[N]{};
    int head = 0, count = 0;
public:
    bool Push(const T& t) {
        if (count == N) return false;
        item[(head + count) % N] = t; ++count;
        return true;
    }
    bool Pop(T& out) {
        if (!count) return false;
        out = item[head]; head = (head + 1) % N; --count;
        return true;
    }
    void Clear() { head = count = 0; }
    int Size() const { return count; }
};
}  // namespace framevr::ringwheel
