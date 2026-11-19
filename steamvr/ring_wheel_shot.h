#pragma once
// Lab Ring wheel (T025, round 2026-11-08 item 4): a screenshot's picture of the ring's layer, as data. The compose pass writes
// premultiplied color into an 8-bit target; a layer PNG (layer_shot.h, upstream's) is sRGB with straight alpha. Pure and
// Windows-free. Runs only on a screenshot's frame.
// Round 2026-11-12 Job B (audit bug 4: the save's 42-58 ms sat on the frame): the frame thread only hands the filled
// readback buffer over (Queue); a worker copies its rows (Raw), makes the picture, writes the file and says the log line
// (SaveQueue, Finish).
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <functional>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#include <vector>
#include "layer_shot.h"

namespace framevr::ringwheel::shot {
inline float SrgbToLinear(uint8_t v) {
    static const std::array<float, 256> table = [] {
        std::array<float, 256> t{};
        for (int i = 0; i < 256; ++i) { const float c = float(i) / 255.f; t[size_t(i)] = c <= .04045f ? c / 12.92f : std::pow((c + .055f) / 1.055f, 2.4f); }
        return t;
    }();
    return table[v];
}
// `rows` (rowPitch bytes apart), w x h texels. bgra: B8G8R8A8 order. srgb: an _SRGB target (blended in linear light, so its
// bytes are the sRGB encoding of premultiplied linear color: decode, divide, encode); else the bytes are premultiplied as they
// are. A clear texel stays all zero; color past its alpha (the effects' additive light) is capped at white.
inline std::vector<uint8_t> ToStraightRgba8(const uint8_t* rows, size_t rowPitch, int w, int h, bool bgra, bool srgb) {
    std::vector<uint8_t> out(size_t(w) * size_t(h) * 4);
    const int r = bgra ? 2 : 0, b = bgra ? 0 : 2;
    for (int y = 0; y < h; ++y) {
        const uint8_t* in = rows + size_t(y) * rowPitch;
        uint8_t* o = &out[size_t(y) * size_t(w) * 4];
        for (int x = 0; x < w; ++x, in += 4, o += 4) {
            const uint8_t a = in[3];
            if (!a) continue;
            o[3] = a;
            const uint8_t c[3] = {in[r], in[1], in[b]};
            for (int k = 0; k < 3; ++k) {
                if (a == 255) o[k] = c[k];
                else if (srgb) o[k] = layershot::LinearToSrgb8(SrgbToLinear(c[k]) * 255.f / float(a));
                else o[k] = uint8_t(std::min(255, (int(c[k]) * 255 + a / 2) / int(a)));
            }
        }
    }
    return out;
}

// ---- Job B: the save off the frame ----
// The readback's rows as the GPU wrote them: `h` rows `rowPitch` bytes apart (the last one only `w * 4` long),
// premultiplied, in the target's byte order. The worker copies them out of the buffer (Compose's ShotRows::Read).
struct Raw {
    std::vector<uint8_t> rows;
    size_t rowPitch = 0;
    int w = 0, h = 0;
    bool bgra = false, srgb = false;
    static size_t Bytes(size_t rowPitch, int w, int h) { return w > 0 && h > 0 ? rowPitch * size_t(h - 1) + size_t(w) * 4 : 0; }
    bool Ok() const { return w > 0 && h > 0 && rowPitch >= size_t(w) * 4 && rows.size() >= Bytes(rowPitch, w, h); }
};
// The layer PNG's pixels (RGBA8, sRGB, straight alpha, tight rows); empty when `r` is not whole.
inline std::vector<uint8_t> Picture(const Raw& r) { return r.Ok() ? ToStraightRgba8(r.rows.data(), r.rowPitch, r.w, r.h, r.bgra, r.srgb) : std::vector<uint8_t>{}; }
inline double NowMs() { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

// The saves waiting to be written, and the one thread that writes them in order. No thread exists unless a save waits: Push
// starts it, and it ends when nothing is left, so nothing here runs or sleeps between screenshots. Push, Full and Drain are
// the frame thread's (one caller at a time: transport's mutex covers the screenshot hook and Shutdown).
class SaveQueue {
public:
    static constexpr int MaxWaiting = 4;   // the one being written and three behind it (about 7 MB each at 150%); a fifth is refused, never queued
    struct Ticket { double at = 0; int behind = 0; double begun = 0; };   // when Push took it (NowMs), the saves not finished then, when the worker began it
    using Job = std::function<void(const Ticket&)>;
    SaveQueue() = default;
    // Never a blocking join here (as AtlasWorker, review R5 K3: it may run at DLL unload, under the loader lock): the Lab
    // holds its queue on the heap and never destroys it; a test calls Drain first.
    ~SaveQueue() { if (thread.joinable()) { if (Idle()) thread.join(); else thread.detach(); } }
    SaveQueue(const SaveQueue&) = delete;
    SaveQueue& operator=(const SaveQueue&) = delete;
    bool Full() const { std::lock_guard lock(mutex); return waiting >= MaxWaiting; }
    int Waiting() const { std::lock_guard lock(mutex); return waiting; }
    bool Idle() const { std::lock_guard lock(mutex); return !running; }
    unsigned Threads() const { return threads; }   // threads started so far (tests: none without a save)
    // Never waits for a save. False: full, or no thread could be started; the job is not kept.
    bool Push(Job job) {
        std::lock_guard lock(mutex);
        if (waiting >= MaxWaiting) return false;
        if (!running) {
            // The last thread left its loop under this mutex and takes it no more: joined at once.
            if (thread.joinable()) thread.join();
            try { thread = std::thread([this] { Run(); }); } catch (...) { return false; }
            running = true; ++threads;
        }
        jobs.push_back({std::move(job), {NowMs(), waiting, 0}});   // stamped last: the caller's time up to here is the frame's share
        ++waiting;
        return true;
    }
    // Shutdown: every save taken is written before this returns (the thread joined). Nothing waiting: returns at once.
    void Drain() { if (thread.joinable()) thread.join(); }
private:
    struct Item { Job job; Ticket ticket; };
    void Run() {
        std::unique_lock lock(mutex);
        while (!jobs.empty()) {
            Item item = std::move(jobs.front());
            jobs.pop_front();
            lock.unlock();
            item.ticket.begun = NowMs();
            try { item.job(item.ticket); } catch (...) {}
            item.job = nullptr;   // its pixels freed here, not under the mutex
            lock.lock();
            --waiting;
        }
        running = false;
    }
    mutable std::mutex mutex;
    std::deque<Item> jobs;
    std::thread thread;
    int waiting = 0;        // queued or being written
    bool running = false;   // the thread is in its loop
    unsigned threads = 0;
};

// The worker's half of a screenshot, after it copied the rows out: the picture from them, the file, then the one log line,
// with the frame thread's ms (from `frameT0`, a NowMs the hook took first, to the Push) and the worker's (from the
// ticket's `begun`, so the rows' copy counts). `write(file, rgba, w, h)`: true when the file is written (png::Save);
// `say(const char*)`: the log. Rows that are not whole (the copy failed): no file, and the line says so.
template <class Write, class Say>
void Finish(const Raw& raw, const std::wstring& file, double frameT0, const SaveQueue::Ticket& t, Write&& write, Say&& say) {
    bool saved = false;
    try {
        const auto rgba = Picture(raw);
        saved = !rgba.empty() && write(file.c_str(), rgba, unsigned(raw.w), unsigned(raw.h));
    } catch (...) {}
    const double frameMs = t.at - frameT0, workerMs = NowMs() - t.begun;
    char line[820], tail[96] = "";
    if (t.behind > 0) std::snprintf(tail, sizeof tail, ", after %.1f ms behind %d other save%s", t.begun - t.at, t.behind, t.behind == 1 ? "" : "s");
    if (saved) std::snprintf(line, sizeof line, "Frame capture: ring wheel saved %ls (%dx%d, %.1f ms on the frame, %.1f ms on the worker%s)", file.c_str(), raw.w, raw.h, frameMs, workerMs, tail);
    else std::snprintf(line, sizeof line, "Frame capture: ring wheel not saved (%s %ls; %.1f ms on the frame, %.1f ms on the worker%s)", raw.Ok() ? "cannot write the file" : "the picture could not be read back for", file.c_str(), frameMs, workerMs, tail);
    say(line);
}

// The frame thread's half of a screenshot (the Lab's OnLayerShot hook). With MaxWaiting saves unwritten it refuses at
// once, before any GPU work. Else `take(why)` does the GPU's part (Compose::Shot) and returns how the worker reads the
// rows, or sets `why`; then the save is queued (the queue is made at the first one). Null: queued, and the worker says
// the saved / not saved line. Else why not, for the caller to say. On the worker, per save: `begin()` (its thread's
// priority), the rows read, Finish. Nothing thrown reaches the frame thread (out of memory: a reason, as the rest).
using Reader = std::function<bool(Raw&)>;
template <class Take, class Write, class Say, class Begin>
const char* Queue(SaveQueue*& queue, double frameT0, const wchar_t* file, Take&& take, Write write, Say say, Begin begin) {
    if (queue && queue->Full()) return "the last screenshots are still being written";
    try {
        const char* why = nullptr;
        Reader read = take(why);
        if (why) return why;
        if (!read) return "no picture taken";
        if (!queue) queue = new (std::nothrow) SaveQueue;
        if (!queue || !queue->Push([read = std::move(read), name = std::wstring(file ? file : L""), frameT0, write, say, begin](const SaveQueue::Ticket& t) {
                begin();
                Raw raw;
                read(raw);   // a failed read leaves no rows, and Finish says so
                Finish(raw, name, frameT0, t, write, say);
            })) return "no worker to write it";
    } catch (...) { return "no memory to queue it"; }
    return nullptr;
}
}  // namespace framevr::ringwheel::shot
