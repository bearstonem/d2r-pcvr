#pragma once
#include <cstdint>

namespace framevr::scenedepth::compare {
// One draw thread calls Pair after the right eye. Equal-length ABBA blocks
// balance a linear change in workload over each group of four.
struct Block {
    static constexpr unsigned Warmup = 4, Pairs = 32;
    uint64_t index = 0;
    unsigned pairs = 0;
    double cycleMs = 0, eyesMs = 0;
    bool Filtered() const { return index % 4 == 1 || index % 4 == 2; }
    void Pair(double cycle, double eyes) {
        if (++pairs > Warmup) { cycleMs += cycle; eyesMs += eyes; }
    }
    void Next() { *this = Block{index + 1}; }
};
void FinishPair(double cycleMs, double eyesMs);
}
