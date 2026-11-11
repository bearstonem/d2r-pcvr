#pragma once
#include <mutex>

// Box3D keeps every world in one table and b3CreateWorld looks for a free row
// in it without a lock. The bottles' world is made on the frame thread and the
// bodies' on their own worker (limb_worker.h), so making and destroying a
// world takes this. Stepping two worlds at once needs nothing: each world's
// state is its own.
namespace framevr::physics {
inline std::mutex& WorldTable() {static std::mutex table;return table;}
}
