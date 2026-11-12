// task_slots.h - which thread is inside which render task right now ([debug] draw_sampler, steamvr/PERFORMANCE.md).
//
// The game runs a pass's render tasks on some seventeen threads. To sample the thread that runs a chosen
// task, the sampler must know which thread that is at this instant. The hook on the game's task runner
// (vr/render_task_hooks.cpp) says so here, around each run, on the thread that runs it: a fixed table with
// one slot per thread that ever ran a task. A thread finds its slot by its id (a hash, then the next slots)
// and claims an empty one with one atomic compare-exchange; a slot is never given back. Nothing here
// allocates, locks or calls anything: it is loads and stores of atomics in memory that is there from the
// start, so it may run in the hook, and be read while the slot's thread is stopped.
//
// A task that waits helps with other jobs on its own thread, another render task among them: the slot keeps
// a small stack of runs, the innermost on top, and the outer one is the thread's task again when the inner
// ends. Only the slot's own thread writes it. It makes the sequence number odd while it writes, so a reader
// that sees an odd or a changed number knows what it read is not one state. No Windows or game dependency:
// tested on its own (draw_sampler_test.cpp).
#pragma once
#include <atomic>
#include <cstdint>

namespace framevr::taskslots {

constexpr int MaxSlots = 64;   // threads that ever ran a render task; a power of two
constexpr int MaxDepth = 4;    // task runs open on one thread, one inside the other

// One task run open on a thread.
struct Level {
    std::atomic<uint64_t> name{0};   // the address of the task's name in the game; 0 = not known
    std::atomic<uint64_t> root{0};   // an address in the hook's stack frame for this run: the task's own frames are all below it
    std::atomic<uint32_t> run{0};    // counts this thread's runs: names one run
    std::atomic<uint8_t> index{0};   // the task's place in the graph
    std::atomic<uint8_t> eye{0};
};
struct Slot {
    std::atomic<uint32_t> thread{0};   // the thread it belongs to; 0 = free
    std::atomic<uint32_t> seq{0};      // odd while its thread writes it
    std::atomic<uint32_t> depth{0};    // runs open; beyond MaxDepth they are counted, not kept
    std::atomic<uint32_t> runs{0};     // runs begun by this thread
    std::atomic<uint64_t> teb{0};      // the thread's TEB (its stack's bounds), as it said itself
    Level levels[MaxDepth];
};
struct Table {
    Slot slots[MaxSlots];
    std::atomic<uint32_t> overflow{0};   // runs not published: every slot belongs to another thread
};

inline unsigned First(uint32_t thread) noexcept { return (thread * 0x9E3779B1u) >> 26; }   // 0..63

// The calling thread's slot, claimed when it has none; nullptr when the table is full (counted).
inline Slot* Claim(Table& t, uint32_t thread) noexcept {
    if (!thread) return nullptr;
    const unsigned first = First(thread);
    for (unsigned i = 0; i < unsigned(MaxSlots); ++i) {
        Slot& s = t.slots[(first + i) & unsigned(MaxSlots - 1)];
        uint32_t owner = s.thread.load(std::memory_order_relaxed);
        if (owner == thread) return &s;
        if (owner) continue;
        if (s.thread.compare_exchange_strong(owner, thread) || owner == thread) return &s;
    }
    t.overflow.fetch_add(1, std::memory_order_relaxed);
    return nullptr;
}
// A thread's slot, or nullptr when it never ran a task. Slots are never freed, so the first empty one ends the search.
inline const Slot* Find(const Table& t, uint32_t thread) noexcept {
    if (!thread) return nullptr;
    const unsigned first = First(thread);
    for (unsigned i = 0; i < unsigned(MaxSlots); ++i) {
        const Slot& s = t.slots[(first + i) & unsigned(MaxSlots - 1)];
        const uint32_t owner = s.thread.load(std::memory_order_relaxed);
        if (owner == thread) return &s;
        if (!owner) return nullptr;
    }
    return nullptr;
}

// The slot's own thread, as a task run begins. `root` is an address in the caller's frame; stacks grow
// down, so a run inside another has a lower one. A level at or below `root` was never left (its frame was
// unwound past, by an exception): it is dropped first.
inline void Enter(Slot& s, uint64_t teb, uint64_t name, uint64_t root, uint8_t index, uint8_t eye) noexcept {
    const uint32_t seq = s.seq.load(std::memory_order_relaxed);
    s.seq.store(seq + 1);
    uint32_t depth = s.depth.load(std::memory_order_relaxed);
    while (depth && depth <= uint32_t(MaxDepth) && s.levels[depth - 1].root.load(std::memory_order_relaxed) <= root) --depth;
    const uint32_t run = s.runs.load(std::memory_order_relaxed) + 1;
    s.runs.store(run, std::memory_order_relaxed);
    if (depth < uint32_t(MaxDepth)) {
        Level& l = s.levels[depth];
        l.name.store(name, std::memory_order_relaxed);
        l.root.store(root, std::memory_order_relaxed);
        l.run.store(run, std::memory_order_relaxed);
        l.index.store(index, std::memory_order_relaxed);
        l.eye.store(eye, std::memory_order_relaxed);
    }
    s.teb.store(teb, std::memory_order_relaxed);
    s.depth.store(depth + 1, std::memory_order_relaxed);
    s.seq.store(seq + 2);
}
// The slot's own thread, as that run ends: the run it was inside is its task again.
inline void Leave(Slot& s) noexcept {
    const uint32_t seq = s.seq.load(std::memory_order_relaxed);
    s.seq.store(seq + 1);
    const uint32_t depth = s.depth.load(std::memory_order_relaxed);
    if (depth) s.depth.store(depth - 1, std::memory_order_relaxed);
    s.seq.store(seq + 2);
}

// What a slot says: the innermost run open on its thread.
struct View {
    uint32_t thread = 0, depth = 0, run = 0;
    uint64_t teb = 0, name = 0, root = 0;
    uint8_t index = 0, eye = 0;
};
enum class Seen : uint8_t {
    Task,      // inside a task run: the view is one state
    Idle,      // in no task
    Writing,   // its thread was writing it: nothing to go by
    Deep,      // more runs open than are kept: the innermost is not known
};
// Any thread. While the slot's thread is stopped, what this returns stays true until it runs again.
inline Seen Read(const Slot& s, View& v) noexcept {
    const uint32_t seq = s.seq.load();
    if (seq & 1) return Seen::Writing;
    v.thread = s.thread.load(std::memory_order_relaxed);
    v.depth = s.depth.load(std::memory_order_relaxed);
    v.teb = s.teb.load(std::memory_order_relaxed);
    const bool kept = v.depth && v.depth <= uint32_t(MaxDepth);
    if (kept) {
        const Level& l = s.levels[v.depth - 1];
        v.name = l.name.load(std::memory_order_relaxed);
        v.root = l.root.load(std::memory_order_relaxed);
        v.run = l.run.load(std::memory_order_relaxed);
        v.index = l.index.load(std::memory_order_relaxed);
        v.eye = l.eye.load(std::memory_order_relaxed);
    }
    if (s.seq.load() != seq) return Seen::Writing;
    return kept ? Seen::Task : v.depth ? Seen::Deep : Seen::Idle;
}

// Which threads to stop at this tick: at most `most` of the `count` slots that are eligible, from the
// cursor on and round, the cursor then moved past the last one taken - so when more threads are in watched
// tasks than may be stopped at a tick, each gets its turn. Returns how many; `out` holds their indices.
inline int Pick(const bool* eligible, int count, int most, int& cursor, int* out) noexcept {
    int n = 0;
    if (count <= 0) return 0;
    if (cursor < 0 || cursor >= count) cursor = 0;
    const int from = cursor;
    for (int i = 0; i < count && n < most; ++i) {
        const int at = (from + i) % count;
        if (!eligible[at]) continue;
        out[n++] = at;
        cursor = (at + 1) % count;
    }
    return n;
}

}  // namespace framevr::taskslots
