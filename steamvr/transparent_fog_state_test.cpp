#include "transparent_fog_state.h"
#include <cassert>
#include <cstdio>
int main() {
    framevr::transparentfog::EyeCapture eye;
    assert(!eye.Fresh());
    const auto left=eye.Record();
    assert(!eye.Fresh()); // Recording without executing cannot expose a mask.
    eye.Submit(left,false);assert(!eye.Fresh()); // Another queue has no ordering guarantee.
    eye.Submit(left,true);assert(eye.Fresh());
    eye.Invalidate();assert(!eye.Fresh()); // Right eye must not sample the left mask.
    eye.Submit(left,true);assert(!eye.Fresh()); // Resubmitting an old list is also stale.
    const auto right=eye.Record();eye.Submit(right,true);assert(eye.Fresh());
    eye.Invalidate();assert(!eye.Fresh()); // Next frame with no transparent draw.
    auto pending=eye.Record();eye.Invalidate(); // Resize/reload/disable before submission.
    eye.Submit(pending,true);assert(!eye.Fresh());
    eye.Submit(0,true);assert(!eye.Fresh()); // Reset or unrelated command list.
    eye.Submit(eye.Record(),true);assert(eye.Fresh()); // Normal recovery.
    std::puts("Transparent fog: submission, queue, eye, frame and invalidation isolation passed");
}
