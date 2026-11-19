#pragma once
// Ring skill menu (, review-r6 X1 / X6): the ring swapchain's life after an Off edge, as plain data. Pure and Windows-free
// (ring-wheel-test runs it with fake GPU sides and a real worker thread).
//
// X1: the ring's worker frees only the GPU side (Compose::Release waits for the GPU there, off the frame thread). The
// swapchain is destroyed by Reap on the frame thread, inside OnWheelLayer under transport's mutex, once that worker has
// exited, the way upstream destroys its own swapchains, so no API layer (VDXR, OpenXR bridges with frame generation) sees
// xrDestroySwapchain from a second thread while the frame thread is in xrWaitFrame / xrEndFrame.
//
// Perf: the Off edge's drain is the hook's last call before it goes dark, so a swapchain
// left for Reap would live (about 24 MB) until the next On or OnUiReset. Off() therefore destroys it in that drain call, on
// the frame thread, once the ring's last compose is done on the GPU (a wait of at most `waitMs`, normally 0: the last compose
// was a frame ago); only a GPU still busy past that keeps it for Reap (said in the log).
#include <cstddef>
#include <vector>

namespace framevr::ringwheel {
struct OffResult { bool destroyed = false; double ms = 0; };
// Audit 2026-11-10 bug 8: a swapchain's release is said once, wherever it happens. The destroy callbacks are idempotent (Reap
// destroys every side it frees, most of them long gone), so only a destroy that found a swapchain (`had`) speaks. `line()`
// builds the text (a std::string) and is called only then (perf nit: nothing is formatted for a side with nothing to say).
template <class Line, class Say> bool SayReleased(bool had, Line&& line, Say&& say) {
    if (had) { const auto text = line(); say(text.c_str()); }
    return had;
}
// The retired GPU sides. T is the ring's Gpu; the callbacks keep this file free of D3D12 and OpenXR:
//   exited(T*)  its worker has left (it never blocks);
//   destroy(T*) destroys its swapchain (the frame thread);
//   free(T*)    joins the exited worker and deletes it.
template <class T> class Retired {
    std::vector<T*> list;
public:
    void Add(T* g) { if (g) list.push_back(g); }
    // The Off edge (the frame thread, the drain call): `wait(g, ms)` blocks until its last compose is done on the GPU or `ms`
    // passed, whichever first, and says whether it is done (the ring: the compose fence's event, Compose::WaitIdle; one wait,
    // no polling, so the bound is `waitMs` and not a Sleep step past it: perf's X1 bound); `now()` ms; `destroy(g)` destroys
    // its swapchain (idempotent); `retire(g)` hands its GPU side to its worker. The swapchain goes here when the GPU is done
    // within `waitMs`; the side is then retired and kept for Reap.
    template <class Wait, class Now, class Destroy, class Retire>
    OffResult Off(T* g, unsigned waitMs, Wait wait, Now now, Destroy destroy, Retire retire) {
        OffResult r;
        if (!g) return r;
        const double t0 = now();
        if (wait(g, waitMs)) { destroy(g); r.destroyed = true; }
        r.ms = now() - t0;
        retire(g);
        Add(g);
        return r;
    }
    bool Empty() const { return list.empty(); }
    size_t Size() const { return list.size(); }
    // Review-r6 X6: a retired side whose swapchain `holds(g)` still exists (the GPU was busy at its Off edge, so Reap destroys
    // it once its worker exits). A new On edge makes no swapchain while one does: never two ring swapchains at once (frame
    // generation's runtime counts each against its limit, and a refusal keeps upstream's wheel for the session).
    template <class Holds> bool AnyHolds(Holds holds) const {
        for (T* g : list) if (holds(g)) return true;
        return false;
    }
    // The On edge's gate (Setup): none made yet and no retired swapchain still alive.
    template <class Holds> bool MayCreate(bool haveOne, Holds holds) const { return !haveOne && !AnyHolds(holds); }
    // Perf's X6 check: the frame thread, each frame before the gate: a retired swapchain whose GPU work is done (`gpuDone(g)`,
    // the fence mark taken at its Off edge; never blocks) is destroyed now, whether or not its worker has exited (the worker
    // never touches the swapchain; it can take 0.1 s to minutes to leave a build stage). The side itself waits for Reap.
    // Returns how many were destroyed.
    template <class Holds, class GpuDone, class Destroy>
    int DestroyDone(Holds holds, GpuDone gpuDone, Destroy destroy) {
        int n = 0;
        for (T* g : list) if (holds(g) && gpuDone(g)) { destroy(g); ++n; }
        return n;
    }
    // The frame thread: each one whose worker exited is destroyed here, then freed; a running one is skipped (no join, no wait).
    // `wait` (OnUiReset, teardown): every one, `join` first so its worker has exited before its swapchain goes. Returns how
    // many were freed.
    template <class Exited, class Join, class Destroy, class Free>
    int Reap(bool wait, Exited exited, Join join, Destroy destroy, Free free) {
        int n = 0;
        for (size_t i = 0; i < list.size();) {
            T* g = list[i];
            if (!wait && !exited(g)) { ++i; continue; }
            join(g);
            destroy(g);
            free(g);
            list[i] = list.back(); list.pop_back();
            ++n;
        }
        return n;
    }
};
}  // namespace framevr::ringwheel
