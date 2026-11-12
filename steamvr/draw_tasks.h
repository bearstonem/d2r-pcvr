// draw_tasks.h - the render tasks of an eye pass ([debug] draw_sampler, steamvr/PERFORMANCE.md).
//
// The game draws an eye pass as a graph of about two dozen render tasks. Each task that is ready runs as
// one job, on a worker thread or on the draw thread, and the draw thread meanwhile loops in the graph's
// "drain" - taking jobs when it may, otherwise spinning - until every task is done. A third of a pass went
// there in the first capture, so the pass is as long as the slowest chain of tasks. While a capture runs,
// the mod times each task's run, the drain and the pass (draw_sampler_capture.h, vr/render_task_hooks.cpp);
// this header is what is made of those records, with no Windows or game dependency so it is tested on
// made-up records (draw_sampler_test.cpp): which task ends last and how long the draw thread had been
// without a task of its own by then.
#pragma once
#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

namespace framevr::drawtasks {

enum class What : uint8_t { Task, Drain, Pass };

// One timed stretch: a task's run, a drain, or a whole eye pass. Times are ms on one clock.
struct Run {
    double start = 0, end = 0;
    uint32_t pass = 0;        // the eye pass it belongs to: the mod's count of passes
    uint32_t thread = 0;
    What what = What::Task;
    uint8_t eye = 0;          // 0 left, 1 right
    uint8_t index = 0;        // a task's place in the graph
    bool onDraw = false;      // run by the thread that draws the pass
    int name = -1;            // into the names; -1 = not known
};

struct TaskRow {
    std::string name;
    uint32_t runs[2] = {};
    double ms[2] = {}, longest[2] = {};          // run time: summed, the longest
    uint32_t onDraw[2] = {};                     // runs on the draw thread
    uint32_t inside[2] = {};                     // runs begun inside another task's run on the same thread (that task waited and helped)
    double startAt[2] = {}, endAt[2] = {};       // ms from the drain's start, summed
    uint32_t last[2] = {};                       // passes in which it was the last task to end
    double idleByThen[2] = {};                   // in those: how long the draw thread had run no task of its own when it ended, summed
    uint32_t Runs() const { return runs[0] + runs[1]; }
    uint32_t Last() const { return last[0] + last[1]; }
};

struct EyeTasks {
    uint32_t passes = 0;                 // passes with their window and at least one task
    uint32_t drained = 0;                // of those, with the drain timed
    uint32_t tasks = 0;                  // task runs
    double passMs = 0;
    double drainedPassMs = 0;                    // the time of the passes with the drain timed
    double drainMs = 0, drainLongest = 0;        // the drain's own time (passes with the drain timed)
    double lastEndMs = 0, lastEndLongest = 0;    // from the drain's start (the pass's, when not timed) to the last task's end
    double ownMs = 0;                            // of the drain: the draw thread running tasks itself (passes with the drain timed)
};

struct TaskReport {
    std::string note;                    // why nothing, or less, was recorded; empty when all was
    uint32_t dropped = 0;                // records lost to a full buffer
    uint32_t orphans = 0;                // task runs of passes whose window was not recorded (the capture's edges)
    EyeTasks eye[2];
    std::vector<TaskRow> rows;           // by passes ended last, then by where they end
    bool Any() const { return eye[0].passes + eye[1].passes != 0; }
    bool Drained() const { return eye[0].drained + eye[1].drained != 0; }
};

namespace detail {
inline std::string Text(const char* fmt, ...) {
    char b[512];
    va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    return b;
}
inline double Per(double sum, uint32_t n) { return n ? sum / n : 0.0; }
}  // namespace detail

// The tables from the records, in any order. A pass counts when its window and at least one task were
// recorded. Offsets are from the start of the pass's first drain; without a timed drain, from the pass's start.
inline TaskReport Summarise(const std::vector<Run>& runs, const std::vector<std::string>& names, std::string note = {}, uint32_t dropped = 0) {
    TaskReport r;
    r.note = std::move(note);
    r.dropped = dropped;
    struct Pass { const Run* window = nullptr; std::vector<const Run*> drains, tasks; };
    std::unordered_map<uint32_t, Pass> passes;
    std::vector<uint32_t> order;
    for (const Run& run : runs) {
        if (run.end < run.start) continue;
        auto [it, fresh] = passes.try_emplace(run.pass);
        if (fresh) order.push_back(run.pass);
        Pass& p = it->second;
        if (run.what == What::Pass) p.window = &run;
        else if (run.what == What::Drain) p.drains.push_back(&run);
        else p.tasks.push_back(&run);
    }
    std::unordered_map<std::string, size_t> at;
    const auto row = [&](const Run& t) -> TaskRow& {
        std::string name = t.name >= 0 && size_t(t.name) < names.size() && !names[size_t(t.name)].empty() ? names[size_t(t.name)]
                                                                                                         : detail::Text("task %u (no name read)", unsigned(t.index));
        auto [it, fresh] = at.try_emplace(name, r.rows.size());
        if (fresh) { r.rows.emplace_back(); r.rows.back().name = std::move(name); }
        return r.rows[it->second];
    };
    for (uint32_t id : order) {
        const Pass& p = passes[id];
        if (!p.window || p.tasks.empty()) { r.orphans += uint32_t(p.tasks.size()); continue; }
        const int e = p.window->eye & 1;
        EyeTasks& eye = r.eye[e];
        ++eye.passes;
        eye.passMs += p.window->end - p.window->start;
        eye.tasks += uint32_t(p.tasks.size());
        double from = p.window->start, drainEnd = 0;
        if (!p.drains.empty()) {
            ++eye.drained;
            eye.drainedPassMs += p.window->end - p.window->start;
            from = p.drains[0]->start;
            double sum = 0;
            for (const Run* d : p.drains) { from = std::min(from, d->start); drainEnd = std::max(drainEnd, d->end); sum += d->end - d->start; }
            eye.drainMs += sum;
            eye.drainLongest = std::max(eye.drainLongest, sum);
        }
        const Run* last = p.tasks[0];
        for (const Run* t : p.tasks) if (t->end > last->end) last = t;
        if (p.drains.empty()) drainEnd = last->end;
        const double lastEnd = last->end - from;
        eye.lastEndMs += lastEnd;
        eye.lastEndLongest = std::max(eye.lastEndLongest, lastEnd);
        // the draw thread's own tasks: a run inside another on the same thread is already counted with the outer one
        double ownEnd = from;
        for (const Run* t : p.tasks) {
            bool inside = false;
            for (const Run* o : p.tasks)
                if (o != t && o->thread == t->thread && o->start <= t->start && o->end >= t->end && (o->start < t->start || o->end > t->end || o < t)) { inside = true; break; }
            TaskRow& tr = row(*t);
            ++tr.runs[e];
            const double ms = t->end - t->start;
            tr.ms[e] += ms;
            tr.longest[e] = std::max(tr.longest[e], ms);
            tr.startAt[e] += t->start - from;
            tr.endAt[e] += t->end - from;
            if (t->onDraw) ++tr.onDraw[e];
            if (inside) ++tr.inside[e];
            if (t->onDraw && !inside) {
                if (!p.drains.empty()) eye.ownMs += std::max(0.0, std::min(t->end, drainEnd) - std::max(t->start, from));
                if (t->end <= last->end) ownEnd = std::max(ownEnd, t->end);
            }
        }
        TaskRow& lr = row(*last);
        ++lr.last[e];
        lr.idleByThen[e] += last->onDraw ? 0.0 : std::max(0.0, last->end - ownEnd);
    }
    std::stable_sort(r.rows.begin(), r.rows.end(), [](const TaskRow& a, const TaskRow& b) {
        if (a.Last() != b.Last()) return a.Last() > b.Last();
        return detail::Per(a.endAt[0] + a.endAt[1], a.Runs()) > detail::Per(b.endAt[0] + b.endAt[1], b.Runs());
    });
    return r;
}

// The lines for the frame log, without the prefix: the caller packs and prefixes them.
struct LogParts { std::vector<std::string> lines; std::string lastTitle; std::vector<std::string> lastItems; std::string longTitle; std::vector<std::string> longItems; };
inline LogParts LogText(const TaskReport& r) {
    using detail::Per; using detail::Text;
    LogParts out;
    if (!r.Any()) {
        out.lines.push_back("render tasks: none timed" + (r.note.empty() ? std::string(" (no eye pass with its tasks inside the capture)") : " - " + r.note));
        return out;
    }
    const uint32_t passes = r.eye[0].passes + r.eye[1].passes, drained = r.eye[0].drained + r.eye[1].drained;
    const double passMs = r.eye[0].passMs + r.eye[1].passMs, drainMs = r.eye[0].drainMs + r.eye[1].drainMs;
    const double drainedMs = r.eye[0].drainedPassMs + r.eye[1].drainedPassMs;
    const double lastEnd = r.eye[0].lastEndMs + r.eye[1].lastEndMs, own = r.eye[0].ownMs + r.eye[1].ownMs;
    if (drained) {
        out.lines.push_back(Text("render tasks: %.1f a pass in %u passes; the drain (the draw thread waiting for them) %.2f ms a pass (left %.2f, right %.2f; longest %.2f), "
                                 "%.1f%% of a pass", Per(r.eye[0].tasks + r.eye[1].tasks, passes), passes, Per(drainMs, drained),
                                 Per(r.eye[0].drainMs, r.eye[0].drained), Per(r.eye[1].drainMs, r.eye[1].drained),
                                 std::max(r.eye[0].drainLongest, r.eye[1].drainLongest), drainedMs > 0 ? 100.0 * drainMs / drainedMs : 0.0));
        out.lines.push_back(Text("render tasks: the last one ends %.2f ms into the drain; the draw thread ran tasks itself for %.2f ms of the drain and had none for %.2f ms",
                                 Per(lastEnd, passes), Per(own, drained), std::max(0.0, Per(drainMs, drained) - Per(own, drained))));
    } else
        out.lines.push_back(Text("render tasks: %.1f a pass in %u passes; the drain itself was not timed: the last task ends %.2f ms after the pass starts "
                                 "(left %.2f, right %.2f), a pass %.2f ms; offsets below are from the pass's start",
                                 Per(r.eye[0].tasks + r.eye[1].tasks, passes), passes, Per(lastEnd, passes), Per(r.eye[0].lastEndMs, r.eye[0].passes),
                                 Per(r.eye[1].lastEndMs, r.eye[1].passes), Per(passMs, passes)));
    if (!r.note.empty()) out.lines.push_back("render tasks: " + r.note);
    out.lastTitle = "render tasks ending last, which the drain waits for (% of passes; the draw thread's time without a task by then):";
    for (const TaskRow& t : r.rows) {
        if (!t.Last() || out.lastItems.size() == 5) continue;
        out.lastItems.push_back(Text("%s %.0f%% (ends %+.2f ms, runs %.2f ms, idle %.2f ms)", t.name.c_str(), 100.0 * t.Last() / passes,
                                     Per(t.endAt[0] + t.endAt[1], t.Runs()), Per(t.ms[0] + t.ms[1], t.Runs()), Per(t.idleByThen[0] + t.idleByThen[1], t.Last())));
    }
    std::vector<const TaskRow*> byTime;
    for (const TaskRow& t : r.rows) byTime.push_back(&t);
    std::stable_sort(byTime.begin(), byTime.end(), [](const TaskRow* a, const TaskRow* b) { return a->ms[0] + a->ms[1] > b->ms[0] + b->ms[1]; });
    out.longTitle = "render tasks by time (ms a pass, where from the drain's start, the thread):";
    for (size_t i = 0; i < byTime.size() && i < 6; ++i) {
        const TaskRow& t = *byTime[i];
        out.longItems.push_back(Text("%s %.2f ms (%+.2f to %+.2f, %s)", t.name.c_str(), Per(t.ms[0] + t.ms[1], passes), Per(t.startAt[0] + t.startAt[1], t.Runs()),
                                     Per(t.endAt[0] + t.endAt[1], t.Runs()),
                                     t.onDraw[0] + t.onDraw[1] == 0 ? "a worker" : t.onDraw[0] + t.onDraw[1] == t.Runs() ? "the draw thread" : "either"));
    }
    return out;
}

// The tables for the text file.
inline std::string FileText(const TaskReport& r) {
    using detail::Per; using detail::Text;
    std::string out = "\n7. Render tasks: what the draw thread's drain waits for\n";
    if (!r.Any()) {
        out += "  none timed" + (r.note.empty() ? std::string(": no eye pass with its tasks inside the capture") : ": " + r.note) + "\n";
        return out;
    }
    out += "  The game draws a pass as a graph of render tasks; each runs as one job on a worker thread or on the draw thread, when the tasks\n"
           "  before it are done. The draw thread meanwhile stays in the graph's drain: it takes a job when it may and otherwise spins, until\n"
           "  the last task ends. So the drain is as long as the slowest chain of tasks, and the task that ends last names that chain's end:\n"
           "  follow it back through the tasks that end where it starts.\n";
    if (!r.note.empty()) out += "  " + r.note + "\n";
    if (r.dropped || r.orphans)
        out += Text("  not counted: %u records lost to a full buffer, %u task runs of passes cut by the capture's start or end\n", r.dropped, r.orphans);
    for (int e = 0; e < 2; ++e) {
        const EyeTasks& eye = r.eye[e];
        out += Text("\n  %s eye: %u passes of %.3f ms, %.1f task runs a pass\n", e ? "right" : "left", eye.passes, Per(eye.passMs, eye.passes), Per(eye.tasks, eye.passes));
        if (!eye.passes) continue;
        if (eye.drained) {
            const double share = eye.drainedPassMs > 0 ? 100.0 * eye.drainMs / eye.drainedPassMs : 0.0;
            out += Text("    the drain: %.3f ms a pass (longest %.3f), %.1f%% of the pass; the last task ends %.3f ms after the drain starts (longest %.3f)\n",
                        Per(eye.drainMs, eye.drained), eye.drainLongest, share, Per(eye.lastEndMs, eye.passes), eye.lastEndLongest);
            out += Text("    of the drain: the draw thread ran tasks itself for %.3f ms; the other %.3f ms it had none (spinning, or helping with other jobs)\n",
                        Per(eye.ownMs, eye.drained), std::max(0.0, Per(eye.drainMs, eye.drained) - Per(eye.ownMs, eye.drained)));
            if (eye.drained != eye.passes) out += Text("    (the drain was timed in %u of the %u passes; in the others offsets are from the pass's start)\n", eye.drained, eye.passes);
        } else {
            out += Text("    the drain was not timed: offsets are from the pass's start; the last task ends %.3f ms after it (longest %.3f)\n",
                        Per(eye.lastEndMs, eye.passes), eye.lastEndLongest);
        }
        out += "    runs/pass  mean ms   max ms  draw thread  inside  start ms    end ms  last, % of passes  idle by then, ms  task\n";
        for (const TaskRow& t : r.rows) {
            if (!t.runs[e]) continue;
            out += Text("    %9.2f  %7.3f  %7.3f  %10.0f%%  %6u  %8.3f  %8.3f  %17.1f  %16.3f  %s\n", double(t.runs[e]) / eye.passes, Per(t.ms[e], t.runs[e]), t.longest[e],
                        100.0 * t.onDraw[e] / t.runs[e], t.inside[e], Per(t.startAt[e], t.runs[e]), Per(t.endAt[e], t.runs[e]), 100.0 * t.last[e] / eye.passes,
                        Per(t.idleByThen[e], t.last[e]), t.name.c_str());
        }
    }
    out += "\n  Columns: runs a pass; the run's mean and longest time; the share of runs on the draw thread (the rest on workers); runs begun inside\n"
           "  another task's run on the same thread (that task was waiting and helped: its time includes them); mean start and end from the\n"
           "  drain's start (a task started before the drain has a negative start); how often it was the last task to end; and, in those\n"
           "  passes, how long the draw thread had been without a task of its own when it ended - the time a shorter chain would give back.\n";
    return out;
}

}  // namespace framevr::drawtasks
