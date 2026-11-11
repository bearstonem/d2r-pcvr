# VR performance investigations

## Render task patches, November 10

**On for every install since the headset run below.** There is no setting for
them in VR Settings and no key in the shipped `d2r_pcvr.ini`. To tell a fault
from a patch, `[game_patches]` with `transparent_filter=0`,
`profiler_accounting=0` or `draw_debug_names=0` written into the file by hand
takes that one out between two frames.

**Measured, November 10** (SteamVR, game 3.4, Both eyes, an Act 2 field fight;
each patch switched on live, a 30 s capture after each; the scene changed
between captures, so the totals are approximate):

| an eye pass, ms | none | + transparent_filter | + profiler_accounting | + draw_debug_names |
| --- | --- | --- | --- | --- |
| pass | 9.27 | 7.96 | 6.72 | 6.34 |
| the drain | 5.35 | 4.14 | 3.22 | 2.53 |
| Depth / Forward Pass run | 2.6 | 2.7 | 1.8 | 1.2 |
| Late Post Processing run | 3.4 | 0.48 | 0.40 | 0.41 |

No crash as each went in; about six minutes of play with all three, the
picture as before (the owner). Not tried: interiors, other acts, By turns,
Virtual Desktop.

**What the third capture showed** (18:44, 30 s in the world, both eyes; the
first with table 8, the threads inside the render tasks): 2,231 eye passes of
9.41 ms, of which the drain is 5.56 ms (59%). The chain the drain waits for,
in ms from its start (left eye): `Collect Draw Calls` 1.19 (+0.12 to +1.31),
then `Depth Pass` 2.71 and `Forward Pass` 2.68 side by side (+1.35 to +4.06),
and `Late Post Processing` 3.49 (+2.06 to +5.55; right eye 3.71), which starts
when `Clutter + Height + Fluid + Friends` ends, not after the Depth or Forward
Pass, and whose end is the drain's end.

### transparent_filter: flags first in the transparent pass's Ghost filter

- **Found.** 88% of Late Post Processing (3.17 ms a run) is self time in one
  game function, `0xFDC510`, with no wait, no lock and almost no GPU driver
  time. It is the "Ghost" mode of the transparent render pass. For each
  transparency layer (three at most settings) it walks every 0x100-byte draw
  record of three buckets of draw calls and keeps the few that carry one of
  the flags `0x21800000`. But before it looks at a record's flags it loads the
  record's pointer to its per-model constants and reads one number through
  it, and those constants live in the buffer they are uploaded to the GPU
  from: mapped upload memory, which is slow to read back. Almost no record
  has the flags (the draw submit function is not among the task's top
  functions), so the time goes on reading a number for records that are then
  skipped. The read costing that much is an inference from the timing and from
  what the memory is; the sampler has no per-instruction table.
- **The patch.** The first instruction of the loop (`0xFDC740`, the 7-byte
  load of that pointer) becomes a jump to 25 bytes of new code in a page
  allocated near the game: test the record's flags; none set, go where the
  game's own test sends such a record (`0xFDCAAD`); otherwise do the load and
  go back to the next instruction, so a record with a flag runs exactly the
  game's code, its own flag test included. For a record without the flags the
  skipped instructions wrote only `rax`, `xmm0`, the low byte of `r15` and the
  flags register; at `0xFDCAAD` `r15` is reloaded from the stack and the others
  are written again before anything reads them. The number is not summed,
  compared across records or stored: for a record that has the flags it is
  handed to the game's per-layer test and chooses which of two sort keys the
  record gets, and that is all. Same records kept, same order, same picture.
- **Proven before writing** (`vr/game_patch_signatures.h`): 123 bytes in
  three stretches: the loop's set-up (the record pointer in `rsi`, the end, the
  fall into the loop), the loop's first twelve instructions up to its own
  flag test and jump, and the loop's end with its only way back to the first
  instruction. Then the whole function's bytes in the running game are
  searched for any jump that could land inside the replaced instruction. A
  difference anywhere: no patch, one log line.
- **Expected.** Late Post Processing 3.6 ms to about 0.45 ms (what is left of
  it in the capture besides that function). It then ends near +2.5 ms and the
  drain ends with the Depth and Forward Pass at about +4.06: about 1.5 ms off
  a 9.4 ms eye pass.

### profiler_accounting: no lock on every draw

- **Found.** In the Depth Pass and the Forward Pass (2.65 and 2.62 ms a run,
  one job each on a worker) 0.72 ms is inside one game function, `0xF13FC0`,
  and the system's lock calls under it: exclusive SRW lock acquire and release
  and the wait they park in. It is the game's built-in GPU profiler counting
  the draw: once per draw the command list's accounting (`0xF117E0`, its only
  caller) hands it the list's current profiler scope and the draw's
  primitives, and it takes the profiler's one lock, adds 1 and the primitives
  to two counters of that scope's record, and unlocks. Every thread recording
  draws takes that same lock for every draw, so the passes that run side by
  side wait for each other; Directional Shadows has 0.66 ms of it too.
- **Who reads the counts.** The profiler folds them into its result table at
  the next frame begin; the only lookups into that table found are in the
  game's developer windows (graphics / drawcall / world art profiling, a stats
  window) and a stats dump. The game has a dynamic resolution setting; nothing
  was found that feeds it from these counts, and the profiler's GPU times are
  not touched by this patch. That search was static and is not a proof.
- **The patch.** One byte: in `0xF117E0` the 2-byte "list not counted by the
  profiler -> skip" jump at `0xF11849` becomes an unconditional jump of the
  same distance, to the function's own end. The function called returns
  nothing, takes and releases its lock inside itself and writes nothing but
  the two counters; the caller's own counters (per list and a global table)
  are before the jump and stay. Scope begin and end, with their GPU
  timestamps, are other functions and are not touched.
- **Proven before writing:** 36 bytes from the test to the caller's last
  byte (the jump, both calls with their distances, the function's end the
  jump goes to) and the first 33 bytes of `0xF13FC0`.
- **Expected.** About 0.7 ms off each Depth Pass and Forward Pass run and off
  Directional Shadows. Those are not the end of the drain today: **alone it
  does not shorten the pass**. After `transparent_filter` they are, and the
  drain should end about 0.7 ms earlier.

### draw_debug_names: no formatted name per draw

- **Found.** In the Depth Pass 0.52 ms a run (19.7%) is under the game's
  debug event begin (`0xF113C0`), nearly all of it in string formatting. The
  draw submit function (`0x103DCD0`) asks for the game's debug event level and,
  when it is above 1, formats `Model: %s Mesh: %s Shader: %s` and puts a begin
  event with that name on the command list before the draw and an end event
  after it: names for a graphics debugger, for every draw. The level is one
  int in the game's data (RVA `0x2808A8C`) with one reader and one writer in
  the game's code; the writer is reached once, from start-up, with a launch
  option's value. In the captured session it is 2 or more; why is not known.
- **The patch.** No code is changed. If the level is above 1 (and at most
  16) it is set to 1. The draw's begin and end both test "above 1", so
  neither is issued; a pass's own begin and end test "above 0" and are issued
  as before, so every begin still has its end. The mod does not use the
  game's debug events (no code of it reads or matches them), and ReShade's
  add-on interface has no event for them. A level of 0 or 1 is left alone
  and said so.
- **Proven before writing:** the reader and the writer (7 bytes each; their
  distances give the int's place), the draw submit's two tests (14 and 27
  bytes) and the event begin's and end's own tests (9 bytes each).
- **Expected.** About 0.5 ms off each Depth Pass run and a like share of the
  other tasks that draw models (Forward Pass, shadows). As with
  `profiler_accounting`, **alone it does not shorten the pass**; after
  `transparent_filter` it does. All three: the drain about 5.56 ms to about
  2.9 ms of a 9.4 ms eye pass.

### How a patch goes in

- Only between frames: on the draw thread at the top of the mod's hook on the
  game's draw (`HookDrawGameScreen`), before the frame's first eye pass. The
  render graph's drain returned in the frame before only when every render
  task had ended, so no thread is in the code that is changed. Menus and
  loading screens do not run that hook: a patch goes in with the first frame
  drawn in the world, and a key changed in the INI takes effect at the next
  frame (1 puts it in, 0 puts the game's bytes back).
- The game's version is never asked. A place whose bytes differ is looked at
  again every 16th frame (code that has not run may not be there yet) and
  given up after 40 looks with `refused: bytes differ at RVA ...`.
- A code write is the loader's checked patch, which compares the bytes once
  more as it writes: the filter's jump is one aligned 8 bytes, the accounting
  jump 2 bytes of which one changes. The debug event level is a plain write
  of one int in the game's data.
- The log (`d2r-vr-frame.log`): `Frame game patches: [game_patches]
  transparent_filter=1 ...` when the settings are first read; per key
  `... applied: ...`, `... refused: ...` or `... off`; and `first frame: ...`
  with each key's state at the first frame drawn. Not in the Status tab's
  game-code table.
- `game-patch-plan-test` checks the fingerprint comparison, the jump search,
  the new code's bytes for given addresses, and that nothing is planned when
  the new code would be out of a relative jump's reach.

### Verifying with a capture

Stand in the same scene with the same settings, take a `[debug] draw_sampler`
capture with every key 0, set one key to 1, look for its `applied` line, take
another capture (another number), and compare:

- `transparent_filter`: table 7, `Late Post Processing` mean run 3.5-3.7 ms to
  about 0.45, and the drain's time a pass down by about 1.5 ms with the Depth
  or Forward Pass now the last long task; table 8, `Late Post Processing`:
  `D2RLoader.exe+0xFDC510` no longer near 88% of it. Samples in the new code
  itself show as code in no module.
- `profiler_accounting`: table 8, `Depth Pass` and `Forward Pass`:
  `D2RLoader.exe+0xF13FC0` gone from the inclusive table (d), ntdll's share
  (a) down from 28.6% to a few percent, no `NtWaitForAlertByThreadId` row by
  that function (b); table 7: both tasks' mean run about 0.7 ms shorter.
- `draw_debug_names`: table 8, `Depth Pass`: `D2RLoader.exe+0xF113C0` (0.52 ms
  inclusive, table d) and the formatting functions under it gone from the
  top functions (c); table 7: the run about 0.5 ms shorter.

**Risks, plainly.**

- The analysis was made on a dump of the game's code from 3.3.93787; the
  running game (3.4.95041) is only known to have the same function starts.
  The byte comparison at run time is what stands between a different build
  and a wrong write, and it covers the places named above, not every byte of
  the functions around them.
- "No thread is in the changed code between frames" rests on reading the
  drain's loop, not on an observation in the game. The loader's write is not
  promised to be atomic; a thread inside the filter's loop during the write
  could run a half-written jump. The accounting change is one byte and the
  level one int: those cannot be seen half-written.
- `transparent_filter`: the new code has no unwind row, so a crash report or a
  sampler walk from inside it stops there (it makes no call, so nothing else
  is ever above it on a stack). That the slow part is the read-back, and so
  the size of the saving, is inferred.
- `profiler_accounting`: the search for readers of the counts was static.
  Something not found that used a scope's draw or primitive count would see
  zeros.
- `draw_debug_names`: a graphics debugger attached to the game would no
  longer see per-draw names. If the game wrote the level again later (not
  found in its code) the names would come back until the key is toggled.
- Taking a patch out again (key back to 0 while playing) is built the same
  way but has not been exercised in the game.

## Draw-thread sampler, November 10

The game is limited by the one thread that draws the eyes: each eye pass keeps
it busy 6.5-11 ms, and it records only about 2,000 of a pass's 11,000 draws
itself. The sampler shows what that thread does with the rest of the time. It
is a diagnostic: off unless asked for, and not for normal play.

**Taking a capture.** In the world, in the headset, with the scene to be
measured in view, change `[debug] draw_sampler` in the installed `d2r_pcvr.ini`
to a new number other than 0 (the file is re-read when saved):

```ini
[debug]
draw_sampler=1
draw_sampler_seconds=30
draw_sampler_tasks=Late Post Processing, Forward Pass, Depth Pass, Collect Draw Calls, Directional Shadows, Shadow Directional Draw Call Collect
```

The capture starts with the first eye pass drawn and ends by itself after
`draw_sampler_seconds` (30; 1-300). `draw_sampler=0` ends a running capture
early; it still reports. Another capture needs another number (2, 3, ...). Put
it back to 0 afterwards: a number left there starts a capture at the next
launch, in the first 30 s in the world (it waits up to 10 minutes for the
first eye pass). Keep the same scene, view and graphics settings between
captures that are to be compared, and stand still for the first one.
`draw_sampler_tasks` names the render tasks whose threads are sampled too
(table 8): parts of the task names of table 7, any case, separated by commas.
The line above is the default, the chain the drain waits for; `all` is every
task, `none` or nothing the draw thread only.

**Where the report is.** `Frame draw sampler:` lines in `d2r-vr-frame.log`
(a start line, then about ten lines when it ends), and every table in
`d2r-vr-draw-sampler.txt` in the same `logs` folder, written anew by each
capture. The last log line names the file. The file ends with every loaded
module's base, size and PE link timestamp, which together name one build of a
binary: the game's `D2R.exe` row is what to check a disassembly against.

**How it works.** A thread of its own ticks about every millisecond, each tick
up to 25% early or late at random so the samples do not keep step with the
frame. The mod marks the stretch of `HookDrawGameScreen` and `DrawTurn` in
which the game draws one eye; outside it nothing is sampled. In it, the
sampler stops the draw thread, reads its registers and copies its stack from
the stack pointer up to the mod's own call of the eye pass (at most 256 KB),
lets it run again, and only then works out the callers from the copy: up to
32 frames are kept, and the walk goes on to the pass's call to know that it
got there. Menus, loading and the flat desktop draw no eye pass: a capture
started there waits. The same capture times the game's render tasks (table 7,
below) and samples the threads that run the watched ones (table 8, "Inside
the render tasks" below).

**Reading it.** Each sample is where the draw thread was at one instant of an
eye pass. A row's percentage is its samples over all samples; its
milliseconds are that share of the measured mean pass time, so the rows of a
table add up to the pass. With 30,000 samples one sample stands for about
0.3 us of a pass; a row of a few samples is noise. Addresses are
`module+0xRVA` of the **start of the function** the sample was in, from the
module's unwind table, so all samples inside one function count together and
the RVA can be looked up in a disassembler. `~` after an RVA marks code with no
unwind row (a leaf function at or after that address). A name after an address
is the module's export at exactly that address.

1. **Where the thread was**, by whose code the sampled instruction is in: the
   game, this mod, ReShade, the GPU driver's user-mode DLLs (NVIDIA, AMD,
   Intel, anything in the driver store), DLSS, D3D12, DXGI, ntdll,
   kernel32/KERNELBASE, win32u/user32/gdi32, the OpenXR runtime and layers,
   other modules, and code in no module (hook trampolines). Table 1b is the
   same by module.
2. **Waiting or running.** A sample stopped in one of the system's wait calls
   (`NtWaitForSingleObject`, `NtWaitForMultipleObjects`,
   `NtWaitForAlertByThreadId`, where SRW locks, critical sections and
   condition variables park; keyed events; `NtDelayExecution`; the win32u
   message and GPU waits; `NtYieldExecution`) is waiting. Table 2b lists the
   calls and 2c who waits: the first caller outside the system's own DLLs and,
   after `<`, the nearest game function behind it. Table 2d lists the other
   samples in ntdll and win32u: locks spinning, and system calls that are not
   waits (a D3DKMT call into the kernel driver counts as running).
3. **Top 25 functions** by the sampled instruction: self time.
4. **Top 15 game functions on the stack**: a sample counts once for every
   `D2R.exe` function it is inside, so these rows overlap and do not add up.
   This is which game subsystem owns the time, also when the instruction
   itself is in the driver or in ntdll.
5. **Graphics calls**: the samples in the driver, D3D12, DXGI, DLSS or
   ReShade, by the nearest `D2R.exe` function on the stack: the game code
   making the expensive calls.
6. **The two eyes** as two totals; the tables also carry a left and a right
   column. In a pair the right pass is drawn with the frame time held.
7. **Render tasks**: what the draw thread's drain waits for. See "Render
   tasks" below.
8. **Inside the render tasks**: for each watched task, where the threads that
   ran it were. See "Inside the render tasks" below.

**Check the walk first.** The head of the file and one log line say what
share of the samples' walks up the stack **reached the eye pass's own call**
in this mod (`HookDrawGameScreen` or `DrawTurn`). Every sample is taken inside
that call, so a sound capture is near 100%. Much less means the callers are
wrong or missing: then only self time (tables 1 to 3) stands, and tables 2c, 4
and 5 do not. Beside it: how many walks were cut short and why (the 32-frame
limit; a "return address" that is not in executable memory; one that no call
instruction ends at; the walk left the stack copy; unreadable memory), how
many frames were unwound with a module's own unwind table because the system
had none, and how many caller addresses were no code. An address outside a
module's executable sections is never shown as a function: as a caller it is
left out of every table and counted; as the sampled instruction it is a row
"(not code by the module's sections: the page)".

**Its cost.** Each sample stops a thread for about 20 millionths of a
second. `draw-sampler-test` on the owner's PC measured 19-21 us a sample, the
longest 30-73 us, 2.0% of the sampled thread's time at 1,000 samples a second.
That thread had a shallow stack; the game's is deeper and more of it is
copied, so expect somewhat more. The report states the capture's own figures:
the mean and longest stop, and their share of the passes' time. The stops are
inside the measured pass time, so a profiled pass is that much longer than an
unprofiled one; do not compare frame rates across a capture. A thread inside
a watched task is stopped as often and as briefly, so that task runs about 2%
longer too, and the sampler's own thread works that much more. When no capture
runs the cost is two reads of a flag per eye pass and one per render task.

**Why it cannot hang the game.** The stopped thread may hold any lock: the
heap's, the loader's, the log's, the one on the system's unwind tables. While
it is stopped the sampler makes three system calls on the thread's handle
(`SuspendThread`, `GetThreadContext`, `ResumeThread`) and reads plain memory
into buffers allocated before the capture: the flags the thread published
itself, the stack bounds in its TEB, the stack. It does not allocate, log,
call the loader, or unwind (`RtlLookupFunctionEntry` takes the unwind-table
lock, and dispatching an exception would too, so the stack is read only
within the TEB's bounds, without a guarded read). Unwinding runs after the
thread is running again, on the copy: the registers that point into the stack
are moved to point into the copy and `RtlVirtualUnwind` walks that. The
threads inside watched tasks are stopped by the same rule, one at a time, and
the sampler never holds a lock of its own nor stops its own thread.

**Limits.** Callers beyond 32 frames or 256 KB of stack are cut (the report
counts them); a sample cut before any `D2R.exe` frame shows as "no game
function within the frames kept". A sample taken inside a hook's trampoline,
or code without unwind rows between the sample and its caller, ends the walk
there: counted as cut short, no longer continued wrongly. The sampling of the
threads inside watched tasks has not run in the game yet: its figures are from
the test's own threads. The tables are `steamvr/draw_sampler.h` and
`steamvr/draw_tasks.h`, the table of which thread is in which task
`steamvr/task_slots.h`, the sampling `steamvr/draw_sampler_capture.cpp`, the
hooks `vr/render_task_hooks.cpp`, the test `draw-sampler-test` (made-up
samples and task records, the slot table, the watch list, then live captures
of a busy, a waiting and a lock-taking thread, with and without the system's
unwind table, and of two threads inside watched tasks).

### First capture, November 10, 17:16

30 s in the world, both eyes: 20,913 samples in 2,202 eye passes of 9.48 ms
(left 9.59, right 9.36). The sampler stopped the thread 21.5 us a sample
(longest 296 us), 2.2% of the passes' time; nothing froze.

- **Whose code** (self time, a pass): the game 70.2% (6.65 ms), the GPU driver
  10.4% (0.98), ntdll 10.0% (0.94), win32u 3.6% (0.34, mostly the D3DKMT
  submit calls), D3D12 2.0%, ReShade 1.3% (0.12), this mod 1.2% (0.11).
- **Waiting** only 1.8% (0.17 ms): by the system's account the thread runs.
- **One cluster is 28% of the pass**: four game functions at `+0x9E9D40`
  (11.2%), `+0x9E9EC0` (7.5%), `+0x9E8E70` (5.5%) and `+0x9E9770` (4.0%), 2.67
  ms a pass. Read in the game's code they are the job system's "help while
  waiting" path: get the job system, make one attempt to take a job
  (`0x9E9EC0`), which looks through the queue's priority lists under its spin
  lock (`0x9E9D40`) and, when it finds one it may take, runs and frees it
  (`0x9E9770`). The loop around them that matters here is the render graph's
  drain (`0xF1BD50`, 4.2% self, 0.40 ms): after the pass's render tasks are
  started it loops, without sleeping, until all of them are done.
- So about **3 ms of a 9.5 ms pass the draw thread is not computing: it spins
  while worker threads finish the pass's render tasks**. Each pass registers
  23 of them (`Renderer::PrepareGraph`, `0xE25970`): Initial Setup, UI Pass,
  Collect Draw Calls, the light buffer, the shadow collects and passes,
  terrain, Popcorn (particles), Depth Pass, Forward Pass, the post processing
  and transparent passes and others. A task that is ready runs as one job.
  What lengthens the slowest chain of tasks lengthens the pass, the mod's own
  work inside a task included (the per-draw callbacks, the gather hook, the
  skeleton hook).
- `+0xF13FC0`, where the thread does wait (0.135 ms a pass): the game's GPU
  profiler counts every draw call under one lock, taken by every thread that
  records draws.
- The two eyes cost the same: nothing is cheaper in the right pass today.

**What was wrong with its caller tables.** Table 4 could not be right: a
function whose only callee had 11.2% self time showed 4.1% inclusive, no game
function was on nearly every stack, and its top row, 28%, was
`D2RLoader.exe+0x1CAF505~` - no function, but the label for anything past the
last row of the unwind table. The cause: to walk up a stack the unwinder needs
each function's unwind row, and asks the system for it
(`RtlLookupFunctionEntry`). The system keeps the table a module had when it
was loaded. The game's executable is loaded packed: on disk its exception
directory is a 3-row table for the unpacking stub, and its real table of
150,572 rows appears in memory later, with the header changed to point at it.
The system never looks again (checked on this Windows: after a module's header
is edited in memory the lookup still answers from the table captured at
load). So for every address in the game the system answered "no row". The
sampler took that for a leaf function and read the top of the stack as the
return address, then the next slot, and so on: the "callers" were whatever
the stack held, among them pointers into the game's data (the job system's
object, vtables), all labelled `+0x1CAF505~`. Frames in system DLLs, the
driver and this mod unwound correctly, which is why the wait tables looked
sound. From that capture: **self time, the system-call tables and "who waits"
stand; tables 4 and 5 and every `<` caller through game code do not** (the
9.1% "inclusive" of the game's allocator free is one such ghost).

**The walk now.** For an address the system has no row for, the sampler looks
in that module's own unwind table as it is in memory (the one it already read
function starts from) and hands that row and the module's base to
`RtlVirtualUnwind`, which follows chained rows itself. Only an address with no
row in either table is a leaf. Every unwound return address must lie in
executable memory (the module's executable sections, or memory executable
now) and directly after a call instruction; if not, the walk stops and the
sample counts as cut short. The walk ends at the pass's own call. Nothing
changed in what happens while the thread is stopped, except one more plain
read (the mark of the pass's call) so that less stack is copied.
`draw-sampler-test` proves it on a live thread: with the system's lookup
withheld for the executable, as in the game, all walks reach the pass's call
through the module's own table (and through ntdll's and KERNELBASE's when
withheld for every module); with the table withheld too, as the first sampler
walked, none do and the report says so.

### Render tasks

While a capture runs the mod also times every render task, the drain and the
pass. Two hooks go in when the first capture of a session is asked for, never
before: on the game's task runner (`0xF18BC0`, which runs one task as one job;
its only caller is the job's functor) and on the drain (`0xF1BD50`). Each is
put in only where the game's bytes are the known ones: the runner's first 39
bytes, which are also the proof of what the hook reads (the task's index and
the graph, a task slot of 0x2A0 bytes), two places in the task registration
for the name pointer at slot + 0x238, and the drain's first 23 bytes. On any
difference - another game build, code not yet run - that hook is left out,
the frame log says which (`Frame draw sampler: ...`), and the report says
what is missing: without the runner no task times, without the name proof
tasks are numbered instead of named, without the drain offsets count from the
pass's start. The fingerprints are `vr/render_task_signatures.h`; they are
not in the game-code table of the Status tab, so a build that lacks them
shows nothing there.

With no capture running a hooked call costs one flag test. In a capture,
inside an eye pass, it reads the task's index and name pointer (two guarded
reads, before the clock starts), reads the clock before and after the game's
function, and writes one record into a buffer made before the capture: no
lock, no allocation, no log, on whichever of the game's threads runs the job.
The arguments are passed on untouched and the result is returned as it was.

**Reading table 7** (per eye) and the `render tasks` log lines:

- The head: passes counted, **the drain's time a pass** and its share of the
  pass, when the last task ends, and how the drain divides into the draw
  thread running tasks itself and having none (spinning, or helping with
  jobs that are not render tasks).
- A row per task name: **runs a pass**; its **mean and longest run**; the
  share of runs **on the draw thread** (the rest on workers); runs begun
  **inside** another task's run on the same thread (that task was waiting and
  helped, so its own time includes them); mean **start and end**, in ms from
  the drain's start (a task started before the drain has a negative start);
  **last**: in what share of passes it was the last task to end; and **idle
  by then**: in those passes, how long the draw thread had been without a task
  of its own when that task ended.
- The task that ends last is the end of the critical chain; the drain cannot
  end before it. Follow the chain back through the rows: the task before it
  is the one that ends where it starts. "Idle by then" is what shortening
  that chain could give back to the pass, at most.
- A long task on a worker that is never last is not the limit. A task on the
  draw thread delays the pass only by its own time.

**Limits.** A task belongs to the pass in which it starts. Passes cut by the
capture's start or end are left out and counted. A capture set in the INI
before the game starts asks for the hooks before the game has run that code:
it gets samples and no task times; start captures in the world. The hooks stay
in until the game exits. They ran in the game in the second capture, below.

### Second capture, November 10, 18:10

The same scene with the fixed walk and the render task hooks, both run in the
game for the first time: 21,657 samples in 2,092 eye passes of 10.34 ms; the
thread stopped 20.4 us a sample, 2.0% of the passes' time. 93.4% of the walks
reached the eye pass's own call (1,428 ended at an address that is not code,
23 at the frame limit), and 198,895 of 258,993 frames were unwound by the
game's own table: the caller tables stand now. Every task was named; the drain
was timed in every pass.

- **The drain is 6.2 ms of the 10.3 ms pass** (60%): the draw thread ran tasks
  itself for 2.5 ms of it and had none for 3.7 ms. On the stacks, the drain
  (`0xF1BD50`) is on 59.6% of the samples and the job system's help attempt
  (`0x9E9EC0`) on 49.2%.
- **The chain the drain waits for**, mostly on worker threads, in ms from the
  drain's start (left eye; the right is the same within 0.2 ms):
  1. `Collect Draw Calls`, 1.2 ms (+0.13 to +1.33; longest 2.8-4.5), one
     serial job, 99% on a worker. Beside it `Shadow Directional Draw Call
     Collect`, 1.1 ms.
  2. `Depth Pass` 2.85 ms and `Forward Pass` 2.83 ms side by side (+1.38 to
     +4.2), one job each, always on workers.
  3. `Late Post Processing`, 3.8-3.9 ms (+2.44 to +6.2), 93% on a worker: it
     starts before the Forward Pass ends and its end is the drain's end
     (`Release Popcorn Draw Calls`, 5 us, follows it and is the last task in
     99.7% of passes).
- Off the chain, but long: `Directional Shadows` 2.1 ms (62% on the draw
  thread), `Clutter + Height + Fluid + Friends` 1.1 ms, `Transparent Pass`
  1.0 ms.
- Waiting is still only 1.7% of the draw thread's samples: it spins.

So the pass is as long as Collect Draw Calls, then the Depth or Forward Pass,
then Late Post Processing, and what happens inside those links decides it.
That capture could not say: it sampled the draw thread only, and those tasks
ran on workers. Table 8 is for that.

### Inside the render tasks

During a capture the sampler also stops **whichever thread is running a
watched render task** and takes the same kind of sample of it.

**Which thread is in which task.** The hook on the task runner already
brackets every task's run on the thread that runs it. Around the run it now
also writes that into the thread's own slot of a fixed table
(`steamvr/task_slots.h`): 64 slots, one per thread that ever ran a task. A
thread finds its slot by its id and claims an empty one with one atomic
compare-exchange; slots are never given back. A slot holds the thread's TEB
and a stack of up to four open runs (task name, index, eye, a run number, and
an address in the hook's stack frame), because a task that waits helps with
other jobs, another render task among them: the inner run is then the
thread's task, and the outer one again when it ends. Only the slot's own
thread writes it, with its sequence number odd while it does, so a reader
never takes two states for one. It is stores into memory that is there from
the start: no allocation, no lock, no log, and only while a capture watches
tasks.

**The sampling.** At each tick, before the draw thread's own sample, the
sampler reads the 64 slots, keeps the threads whose innermost run is a watched
task, and stops at most three of them, one after the other, starting after
the last one stopped at the tick before - so when more than three threads are
in watched tasks they take turns. A thread's handle is opened once and kept;
the system is asked once where that thread's TEB is, and the slot's word must
agree. Stopped, the thread's registers are read, then its slot: only if it
still names the run the thread was picked for is the sample kept (the slot
cannot change while its thread is stopped), otherwise it is dropped and
counted as "moved on". Then the stack is copied from the stack pointer to the
hook's frame, within the bounds in the TEB, and the thread runs again. The
draw thread is not stopped a second time: its own sample notes the task its
slot names, and the frames up to that task's call are that task's sample too.
The walk up a task's stack ends at the hook's own frame in this mod.

**Reading table 8.** After the head (what was watched, samples, how many ticks
had more threads than could be stopped, the stops' cost, what was lost) each
task that got samples has:

- a head line: **samples**, how many from the draw thread and from other
  threads, the **achieved rate** in samples per second of the task's run time
  (about 1,000 when every tick could take it; less when tasks took turns),
  its runs and mean run time from table 7, and the share of walks that
  **reached the task's own call** - near 100%, or only self time stands for
  that task;
- **a.** whose code the sampled instruction was in, as % of the task and ms of
  a run;
- **b.** waiting or running, the wait calls, and who waits (the first caller
  outside the system's DLLs, then the nearest game function);
- **c.** the top 15 functions by self time (module + RVA of the function's
  start);
- **d.** the top 15 game functions on the stack, inclusive;
- **e.** the samples in the GPU driver, D3D12, DXGI, DLSS, ReShade or this
  mod, as `whose code < the nearest game function`: which game code the
  outside work is done for;
- **f.** the samples in this mod and ReShade by function. The RVA is the
  function's start in the DLL: look it up in the mod's link map to name the
  callback. A name after it is the DLL's export at that address.

A row's ms is its share of the task's samples times the task's mean run. The
eyes are shown together; when a run's time, or what a tenth or more of it
goes to, differs between them by more than 10%, the tables carry a left and a
right column instead. The log has four or five `task <name>:` lines per task:
the head, whose code, waiting, the top functions, and this mod's and
ReShade's.

**Limits.** Samples inside a run nested in another count for the inner task
only, while the outer task's run time still includes it. A thread stopped
while it holds one of the game's spin locks keeps the others spinning for the
20 us of the stop, as the draw thread's stops already do. With `all`, more
threads can be in tasks at once than are stopped at a tick, and each task
gets fewer samples: its head line says what it got. Not yet run in the game.
## Draw thread: the bodies' physics on its own thread, November 10

**Gain not yet measured in a headset.** Built and tested natively only.

Evidence, the owner's headset log of 2026-11-10 16:32-16:39: the `input` part
of `Frame pacing: XR begin, ms a pair` was 0.26-0.30 ms a pair with no ragdoll
alive, 0.37-0.39 with 1-2, 0.53 with 3 and 0.58-0.84 with 5-6 (`Frame bodies:
ten seconds: ... ragdolls most=N`). So the bodies that give way cost the draw
thread roughly 0.3-0.5 ms a pair in a big fight. This is inferred from those
timings: the step itself was not timed.

The bodies' Box3D world now lives on one worker thread (`limb_worker.h`), which
makes it, steps it and destroys it. Each frame the draw thread hands over that
frame's time, player position, weapon capsules and posed bodies, and picks up
what finished since the frame before. Only the newest frame waits: one handed
over while the worker is still stepping replaces it, and the fixed 1/120 s
steps are counted from the frames' own times, so nothing queues and no
simulated time is lost. The worker gives each batch's answers straight to the
board the skeleton hook reads, the moment its steps end, so a pose made later
in the same frame is bent by that frame's steps as before; a pose made in the
half millisecond before the batch ends uses the frame before's answer. The
hand's tick and the counters are picked up at the next frame's input, one
frame later than before. The worker runs at normal priority. Making and
destroying a Box3D world takes one lock (`physics_worlds.h`): Box3D keeps all
worlds in one table, and the bottles' world is still made on the draw thread.

The bottle and prop world stays on the draw thread: the same frame reads it
back many times (a let-go bottle, the thrown potion's pose, props' matrices,
traces for the throw sigil), and it already skips its step at rest.

**What to read in the next log.** `Frame bodies: worker: ten seconds: N step
batches, mean X ms, longest Y ms; N frames handed over, N replaced before they
were stepped` is the cost that left the draw thread. `input` in `Frame pacing`
with 5-6 ragdolls should be near its no-ragdoll value (0.26-0.30 ms a pair).
Replaced frames mean the worker did not finish within a frame; a few are
harmless, many would show as bodies reacting late.

## Draw thread: panel uploads and prop physics, November 10

**Not yet measured in a headset.** The changes below passed the native build
and its tests; none has run in a headset, so the gains are expected, not
measured. A headset log from the owner's PC showed the game's draw thread as
the limit: in fights a stereo pair ran 1-2 ms over its slot. Three pieces of
work on that thread were cut.

**The wrist atlas uploads only what changed.** Any life or mana change
re-uploaded the whole 1024 x 1024 interaction atlas: 0.80 ms on the draw thread
each (0.61 ms of it the copy into the upload buffer), 15-34 times in 10 s. The
painter's thread now compares each atlas with the one it painted before
(`atlas_dirty.h`) and hands over the 64-texel tiles that differ; the draw thread
copies those tiles' rectangles alone, and uploads nothing when the picture is
the same. A swapchain hands out several images in turn, so each image also gets
what it missed while the others were written: the image shown is always whole.
The swapchain's image list is read once instead of twice per upload.

**The head-locked overlays do less per repaint.** Blood, Arcane Decay, guard
strain and the glyph trail get the same changed-tile uploads into the shared
master, and their pictures arrive in the atlas's channel order, so the draw
thread never exchanges channels pixel by pixel. An upload no longer has a
command list of its own: the frame's one list at its end puts the staged
pictures into the master and copies on to the swapchain image only what that
image is behind by, where it copied the whole 2048 x 2056 master (16 MB) for any
picture. Their looks and repaint rates are unchanged: Arcane Decay's picture
really differs at each 25 Hz repaint, so its 1 MB copy remains.

**Prop physics does not step at rest.** With level props catalogued the Box3D
world stepped at 120 Hz on the draw thread although every body in it was
static. Without a bottle, a thrown bottle or a moving prop no step is taken; the
weapons' bodies are placed where the steps would have left them and the step
clock keeps its phase, so a prop a weapon reaches is set going and stepped on
the same frame as before (`dropped_bottles_test.cpp` compares the two). The
weapon sweep also skips props outside its bounds before the closer test.

**What to read in the next log.** `Frame UI refresh:` every 10 s now gives the
share of the atlas copied and how many refreshes were unchanged; its `copy`
should fall well below 0.61 ms. A new `Frame pacing: XR begin, ms a pair:` line
splits the frame's begin into the runtime's wait, locating the head and eyes,
and the input step, which holds the panel uploads; compare `input` with and
without Arcane Decay active (begin rose from 0.31-0.36 to 0.75-1.2 ms a pair
when it came on).

## Render thread waits, October 20

**By turns' passes built in the background.** The passes that turn and move
the eye not drawn were built on the render thread at the first turned eye of
every OpenXR session: 9 shader compiles at optimisation level 3 and 7
pipelines, under the transport lock. Timed on the owner's PC (RTX 5090,
Ryzen 9 5950X), that took about 215 ms in a new process (107 ms compiling, the
rest the driver building the pipelines for the first time) and 112 ms at every
later session. A session starts at each game entry, resolution change and
graphics Apply. The shaders now compile once a process, and a background worker
builds the passes once when a session is set up. The render thread's own build
then takes about 3 ms.

**The transport lock costs no frame time.** The render thread holds the
transport lock through the OpenXR frame wait, begin and end. The earlier notes
below suggested that other threads stall on it. A private counting build
measured 8 minutes of By turns play (town, melee, bow, spells, VR Settings).
Other threads waited 1.5-4.1 s per 10 s in total, mostly during the frame wait,
and all of that waiting was on the mod's own 1 kHz update thread: melee
sampling 107 s, the bow's world epoch 19 s, shield, bow and spell shots under
1 s. The game's threads waited twice (about 0 ms), and the render thread about
0.4 ms per 10 s. The update thread waits for the very poses it then reads,
which the frame publishes when it releases the lock, so nothing arrives later.
The frame loop was left as it is.

## Replay removed, October 20

Replay left Stereo rendering (now VR Settings > Graphics > Performance > Eyes drawn per frame). Walks on the owner's PC
(RTX 5090, Ryzen 9 5950X, 3072 square per eye, 90 Hz headset):

- The game set its biome at every line of its 80-unit map grid, so the
  240-pair settle restarted every 1-5 s while walking and Replay ran on no
  pair. Within one level, the private test builds let it run through those
  lines. Those builds waited for the last replay on every free the game made,
  and never followed a held left present with a second pass, which stalled
  the game for 1 s. With that, Replay ran on about 9 pairs in 10 while
  walking.
- It halved the game thread's CPU a pair (about 18 to 10 ms; fpsVR's CPU
  frame time 17.6 to 12.8 ms), but not the frame time. The right eye's GPU
  work can start only after the whole left pass is recorded, so a frame
  spanned about 20 ms, the GPU busy about 13 of it (60% in fpsVR). SteamVR
  paced it at 45 Hz, as it did Both eyes. By turns ran 74-90 frames/s in that
  day's sessions.

A profile saved with Replay goes by turns. Its code is kept but no longer
registered; the test builds are on a local branch.

## Draw distance and the frame, October 20

Lowering the VR resolution from 3072 to 2560 and then 1536 square changed
nothing: each eye's pass took 8-11 ms of CPU at every size. The game's drawing
thread was busy 46-74% of a core, and the frame waited 2.5-8.5 ms for the
headset because it had missed the 11.1 ms frame. The owner then stepped through
the settings in Rogue Encampment, standing at one spot facing one way, and
watched the readout on VR Settings > Graphics > Performance. The same timings are in the log's 10-second
lines. The run used By turns, a 90 Hz headset and an RTX 5090 with a Ryzen 9
5950X:

| Setting | Drawing a frame | Frames/s |
| --- | --- | --- |
| Rings 3, objects 360, game Shadow Quality 4 (1536 square) | 10.8-11.6 ms | 45 |
| Shadow Quality 4, then 2, then 1 | 9.8-11.4 ms | 45-50 |
| Rings 2, objects 260 | 7.7-9.1 ms | 55-80 |
| Objects 210 | 6.7-7.4 ms | 88-90 |
| The same, ReShade's Generic Depth add-on off (restart) | 7.1-7.3 ms | 89-90 |
| The same, at 3072 square | 7.0-7.3 ms | 89-90 |
| Objects 160, 2560 square | 6.4-6.5 ms | 89-90 |

The draw distances did it. Going from rings 3 / objects 360 to rings 2 /
objects 210 halved the draws counted per frame, from about 14,000 to 6,500,
and with that the drawing time. Once the frame fit, resolution made no
difference. Shadows took off about half a millisecond, and Generic Depth
nothing measurable. The default object distance became 210, just past the
fog's end at 200, where further objects cost frames and hardly show. The
shipped default had been 300, and 260 still missed the frame in the camp.
Profiles that set the key keep their value.

About 10 s after each VR resolution change the game stops drawing the world
for about 3 s ("no world camera" in the log) while it rebuilds at the new size.
That is the game's own work, not a gameplay stall.

## Replay, October 19

**Replay** ports upstream's right-eye command-list replay (see the Issue 20
notes below, where it was first identified as the main architectural
difference). One CPU pass draws the left eye; its lists run again on the GPU
with the right eye's camera constants. Upstream reported 127-159 stereo pairs/s
with it against 108 with two passes on the author's PC, and about 5-6 ms of CPU
per eye pass as the limit. On the owner's PC a pair took about 16.7 ms of work
(left 6.86, right 6.95 ms of it), so a CPU-limited session should gain most of
the right pass; a GPU-limited one gains little, as the GPU still draws both
eyes.

First working run (Act 1 camp, 3072 square per eye, DLSS off, the game's
temporal anti-aliasing on): 446 of 446 pairs from one pass, the game's 35
lists in its 5 calls (0.08 ms to submit), about 94 camera records folded a
pair in 46 slots, none overflowed. The right eye's CPU time fell from about
6.2 to 0.8 ms, but the rate stayed at 44.6 pairs/s: SteamVR paced the game at
half of 90 Hz, and the end-of-frame wait for the GPU rose from about 4.9 to
8.9 ms - the GPU, drawing both eyes in full, became the limit.

The limit was the end-of-frame wait, not the GPU's speed: with the
conservative wait the CPU and GPU take turns, so a frame costs the CPU's time
plus the GPU's. With `defer_gpu_wait=1`, the game's FXAA and Vertical Sync
off (3128 square per eye, Headset; Act 1 camp, standing), Replay drew 889 of
889 pairs from one pass: 88.9 pairs/s with the runtime predicting 88.9 Hz,
against 44-45 before. Leaving the camp, a biome set paused it and it took
about a minute to settle again, at 40-48 pairs/s in two passes meanwhile; the
10-second line now counts what starts the settle again. Replay briefly became
the default stereo rendering, and the installer now sets `defer_gpu_wait=1`.

A later session (3128 square, the deferred wait on, switching modes with
fpsVR running; the owner found By turns best) measured:

| Stereo rendering | Headset frames/s | CPU per frame |
| --- | ---: | --- |
| By turns | 86-88 (each eye drawn 43-44) | game pass 5.7-7.4 ms |
| Replay, replaying | 57-67 | left 7.9-8.3 + right 0.75 ms |
| Replay, paused | about 45 | two passes |
| Both eyes | 44.7 | 6.7 + 6.4 ms |

Outside the camp the game set the same biome 1-13 times in 10 seconds, each
restarting Replay's 240-pair settle, so it drew two passes much of the time.
Noting the left pass's lists costs about 1.5 ms, which left a replayed pair
just over the 11.1 ms budget. By turns needs the deferred wait as much as
Replay: an earlier run at 3072 square without it went 43-46 frames/s, the
frame's end waiting 5.4-6.3 ms for the GPU (1.2-1.4 ms deferred). By turns
became the default, and the Video page offers the deferred wait in every mode.
At 2048 square every mode's pass took 12-14 ms, against 6-7 ms at 3128, for
reasons not yet known; the interaction atlas upload took 5.4-7 ms on the
frame thread, up to once a second (1.2 ms on October 10).

Differences from upstream: the right eye's camera math is a pure header with
tests (the fold's V*P_R matches a true right-eye camera's clip coordinates);
camera positions inside a found record's matrices are not taken for stand-alone
ones; the native command-list hooks and the upload-buffer mappings are made
only once Replay is first wanted; the left eye reaches the headset through the
transport's normal capture at the held present, and the right eye's pose is
the left one moved along its x, so the layer matches what was drawn.

## Eyes by turns, October 19

On the owner's Windows PC a stereo pair took about 16.7 ms of work: game 0.90,
left draw 6.86, right draw 6.95 and OpenXR submission 1.95 ms. That is over the
11.1 ms budget at 90 Hz, so the runtime paced the game at about 44 pairs/s.
These are one session's 10-second averages, not a controlled benchmark.

VR Settings > Graphics > Performance > **Eyes drawn per frame: One by turns** (`[stereo] rendering` 1)
draws one eye per game frame and resubmits the other eye's last image at its
own pose, following upstream's "Native OpenXR - eyes by turns" mode. It removes
the second CPU draw from every headset frame without replaying command lists,
so culling, labels, effects, foveation and DLSS stay per eye. Each eye still
updates at half the headset rate, and the eyes are one game frame apart. The
right-eye replay described below would keep both eyes from one frame but
was then unported (it is now Replay, above). Both eyes each frame was then the
default.

The first headset test (SteamVR, Steam Frame, 3072x3072 eyes; DLSS off - the
game creates a DLSS instance at start but never evaluates it - and the game's
temporal anti-aliasing, `Anti Aliasing` 3, on) looked
right with the head still but jittered and lost convergence in every head
turn. Then the other eye went to the runtime at its own older pose, which
SteamVR should reproject per eye. The candidate now turns that eye itself:
each eye's image as shown is kept (`KeepTurn`), and on the frames that draw
the other eye a full-screen GPU pass rotates it to this frame's view
(`TurnEye`, `turn_warp.h`). Both views then carry one frame's poses, as from
a pair. In that session the GPU, not the CPU, limited the rate: both eyes ran
at 25-28 pairs/s, by turns at 42 frames/s (21 per eye), and one eye's pass
took 11-14 ms against about 7 ms per eye in the measurement above.

The second test: head turns were right, but walking left one eye lagging. The
turned eye still showed the world from where the hero stood a frame earlier,
10 cm or more at a run, which a rotation cannot correct; that needs the scene's
depth (a positional reprojection). That candidate became **Hybrid** (since
removed; By turns keeps its rule until both eyes have depth): a pair
from the hero point's first step to 0.3 s after its last and on a snap turn,
by turns only in first person, stick and mouse turns part of the GPU turn.

**By turns** then added the positional reprojection. Each eye's scene depth is
kept with its image (scenedepth's per-present copy, kept even when no effect
wants it), and where the eye stood in the world (View) and its near plane. The
eye not drawn is a grid of 4-pixel cells over the old image, each vertex put
at near / depth along its ray, carried through the world to the current eye
(TurnMove) and rasterised with a reversed depth test, over the rotation-only
turn for the edges. GPU tests check a wall, the sky, occlusion by a nearer post
and DLSS's drawn corner. Not yet measured in the headset: its GPU cost (a 3072
square eye is about 3.5 million grid vertices) and how the stretched
disocclusion edges look.

## Issue 20 investigation, October 12

[#20](https://github.com/bearstonem/d2r-pcvr/issues/20) reports roughly 60 FPS
and less than 50% GPU utilization on an RTX 5080, compared with 90 FPS and
better clarity in another mod. The CPU, headset/runtime, refresh rate, exact
mod versions, frame metric and comparable settings are still unknown. No
reporter capture has arrived. This candidate addresses costs found in our
code; it does **not** establish that the RTX 5080 report is fixed.

The saved October 10 native Windows log contains a ten-second window with
left/right draw means of 6.77/6.12 ms, including Present means of 0.50/0.38 ms.
The two draws alone exceed the 11.11 ms full-90-Hz budget. A separate 109.5 ms
hitch spent 89.1 ms in XR begin, showing why a long frame cannot always be
attributed to drawing. These are local historical captures, not the reporter's
hardware, matched A/B measurements, or evidence of a specific FPS cap.

Current upstream at
[`4b6284c0766c089139693a504333b9a19201cdd2`](https://github.com/natpoh/diablo2-vr-port/tree/4b6284c0766c089139693a504333b9a19201cdd2)
has a significant architectural difference: its
[`replay_right=1` default](https://github.com/natpoh/diablo2-vr-port/blob/4b6284c0766c089139693a504333b9a19201cdd2/d2r_vr.ini#L91)
records the engine's CPU draw once, then replays GPU commands with right-eye
camera constants. Our build still invokes the engine draw twice. If the
reporter's comparison version uses this path, it is a plausible larger source
of the difference. Its identity/version must be confirmed.

A replay port needs per-eye view/projection/history buffers, direct/compute/copy
queue ordering and resource lifetimes, correct right-eye depth for effects,
and one desktop Present per pair. Our native OpenXR capture, VRS masks,
physical controls and spatial UI also need integration. The upstream path has
fallbacks and recent area-transition/depth fixes; copying it wholesale would
not validate those behaviors here. No replay code is included in this candidate.

Implemented changes:

- HUD bind/scissor/reset callbacks reject command lists outside the UI pass
  before taking the shared HUD mutex. A bounded atomic membership filter allows
  false positives only; collisions still consult the locked authoritative map.
  UI-list reset/destruction removes membership. UI capture, hiding and map
  redirection retain their existing critical sections.
- Per-thread resource-description caches avoid repeated native metadata calls
  in VRS, depth and HUD bindings. Resource creation/destruction and device
  destruction invalidate them. RTV/DSV-to-resource resolution is deliberately
  uncached: D3D12 descriptor copies can replace those mappings without an
  `init_resource_view` event. A HUD exit reuses its resolved metadata locally.
- Missing `[render] rings` and `model_radius` keys use the shipped defaults
  (2/300), rather than 4/1000. Camera and settings UI share those defaults;
  explicit INI values are preserved. Object-distance adjustments use steps of
  50 instead of jumping directly from 300 to 50.
- Routine ten-second logs add `Frame pacing`: predicted runtime application rate,
  XR begin/wait, end/submit and total GPU fence averages alongside the existing draw
  timings. Bounded hook captures include resource lookups and HUD lock waits.
- The report defaults to `1000 / refresh_hz`, one complete stereo pair per
  refresh. `--pair-refreshes 2` retains an explicit half-rate comparison. Older
  CSV/log formats still parse. Historical late-pair counts below used the old
  half-rate budget and must not be read as full-refresh success rates.

Validation on Windows, Ryzen 9 5950X (local machine, not the reporter's CPU):

- `powershell -NoProfile -ExecutionPolicy Bypass -File steamvr/build_windows.ps1 -Test`:
  native DLL builds, **42/42 CTest cases pass**. New cases cover description
  lifetime/device invalidation, descriptor rebinding, worker isolation,
  membership collisions, removal/reuse and concurrent membership. Existing
  settings fixtures cover partial INIs and retention of explicit distances.
- `.\deps\python\python.exe -m unittest discover -s steamvr -p profile_report_test.py -v`:
  **7 tests pass**, including a synthetic 13 ms pair that misses full 90 Hz but
  passes an explicitly requested half-rate comparison.
- `resource-descriptions-test.exe --benchmark`: native D3D12 metadata query
  versus cache hit, 30.10 versus 2.58 ns/query with one thread. Absolute cost
  is small and cannot explain the reported FPS gap by itself.
- `active-keys-test.exe --benchmark`: isolated empty HUD registry, 5.70 versus
  0.71 aggregate wall ns/query with one thread; 38.91 versus 0.20 with eight
  workers. These are synthetic contention measurements, not game frame-time
  savings. Real UI work can still wait on the HUD lock.
- Candidate runtime commit `f014e6d` was installed into the local Steam and
  Battle.net copies after backing up the previous DLLs, settings and saves.
  Installation preserved their settings and player data. The Steam edition
  (game 3.3.93787) was launched and measured in offline Paladin gameplay on a
  Ryzen 9 5950X / RTX 5090, using SteamVR OpenXR and Steam Link to Steam Frame
  at 90 Hz. Battle.net gameplay and the reporter's RTX 5080 remain untested.

Local headset measurements, October 12 evening:

| Capture | Scene scale / terrain rings | Mean pair ms | Throughput FPS | Pair p95 ms | Left + right draw ms |
| --- | --- | ---: | ---: | ---: | ---: |
| Moving, sequence 10121839 | 133% / 4 | 25.887 | 38.6 | 32.665 | 16.395 |
| Standing, sequence 10121844 | 100% / 2 | 24.779 | 40.4 | 32.451 | 15.898 |

Each capture contains 360 stereo pairs, with `profile_gpu=0` and
`profile_hooks=0`. GPU timestamp queries and all-thread hook attribution were
disabled; main-eye scopes remained active. Routine ten-second windows ranged
from 41.8-44.1 FPS early in gameplay and 33.2-39.0 FPS during movement. GPU
utilization spot samples were 29-39% on the RTX 5090, including 34% after the
second capture. These are application stereo-pair rates, not headset delivery
or compositor reprojection measurements.

The user changed both scene scale and terrain distance before the planned
terrain-only comparison could start. Position/view and motion also differ
between the captures; this table does **not** isolate either setting's effect
or establish a gain from the candidate DLL. There is no matched previous-DLL
capture. The bounded captures cannot establish the reporter's bottleneck.

Eye images remain 3840 x 2160. Both runs used fixed foveation and symmetric
projection, object radius 300, and `defer_gpu_wait=0`; shadows, AO and
atmospherics were 2, with DLSS and dynamic resolution disabled. In the second
capture, average XR begin/end were 2.708/4.721 ms, with 3.216 ms of GPU fence
waiting included in XR end. Coarse per-thread CPU totals averaged 14.714 ms
across the two eye draws, consistent with CPU draw work and synchronization
limiting throughput rather than sustained GPU saturation. They are not
exclusive high-resolution CPU profiles. Periodic roughly 80-90 ms hitches
included an eye draw around 62 ms without pipeline creation; their cause is
unresolved. Neither capture reaches the full 90 Hz budget of 11.111 ms.

Temporary profiling keys were removed after each capture, with the INI verified
byte-identical to its pre-capture contents. The user's final 100% scale and
two-ring setting were retained. Raw logs, CSVs, DLLs and backups remain local
under ignored `build-frame/` paths; only aggregate results are recorded here.
Deferred GPU waiting, scene resolution and projection source defaults were
not changed by this candidate.

Remaining validation needs the same offline character/scene, runtime and
settings before/after a DLL swap, with routine logs plus separate 360-pair
stationary and walking captures (`profile_gpu=0`, `profile_hooks=0`). An
attribution capture can then enable hooks. Test the map tab, labels and spatial
NPC/inventory menus as well; those UI regressions have not been checked here.
Record actual eye dimensions, scene scale, symmetric projection and VRS when
comparing clarity. Fresh/full installs use 100% scene scale from v0.3.10;
the previous 80% default rendered only 64% as many pixels. Update / repair
preserves existing video settings. Lower resolution does not remove CPU draw/submission work.
Keep #20 open until the reporter's bottleneck and improvement are measured.

### Follow-up: periodic diagnostic stalls

The ten-second hitches in the `f014e6d` session line up with
`OnReshadePresent` calling `BusyThreads` synchronously. Its Toolhelp snapshot
enumerates system threads before querying this process's threads. The native
standalone scanner benchmark measured 52.078-53.467 ms (median 52.850 ms),
consistent with the roughly 54 ms added to an otherwise 8 ms eye draw.

Native builds now start a bounded diagnostics worker during plugin load.
Present submits a value-only request and polls for the completed report;
snapshot creation, thread queries, allocation and sorting run on the worker.
Addon callbacks are unregistered before the worker is joined at unload. The
worker reads OS thread counters only and never follows game pointers or uses
XR/render resources. Percentages use elapsed thread CPU time, retaining thread
creation identities to reject recycled IDs, instead of a cycle/CPU-time ratio
dependent on clock speed. Worker startup failure disables the scan.

`steamvr/build_windows.ps1 -Test` passes **43/43** native checks. The new fixture
covers CPU percentages, reused IDs, absent/invalid counters, restart and
render-side polling while the scanner is deliberately blocked. In its native
benchmark the background scan took 53.182 ms while request submission took
0.300 microseconds.

Runtime commit `918bd5e` was installed and tested with the same local Windows
5950X / RTX 5090 / Steam Frame setup, Steam game 3.3.93787 and offline Paladin.
Three 360-pair captures with hooks/GPU queries disabled contained diagnostics
scans of 57.63, 62.61 and 60.14 ms on the worker. Their maximum complete pair
times were 34.944, 28.648 and 34.026 ms respectively: the scan no longer
appears as a roughly 62 ms eye draw. The session's remaining logged >80 ms
hitch at entry spent 417 ms in the game gap while loading. No average FPS gain
from the diagnostics change is established by a matched previous-DLL run.

A separate native Windows trial changed only the existing GPU wait option,
keeping 100% scale, two rings, radius 300, fixed foveation and symmetric
projection. Each row is 360 pairs with hooks and GPU timestamps disabled:

| Sequence | `defer_gpu_wait` | Mean pair ms | Application FPS | Pair p95 ms | Eye draws ms | GPU fence ms |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| 1791856478 | 0 | 25.302 | 39.5 | 32.594 | 15.750 | 3.337 |
| 1791856539 | 1 | 22.378 | 44.7 | 25.436 | 15.930 | 0.000 |
| 1791856550 | 0, restored | 24.005 | 41.7 | 31.543 | 16.018 | 3.219 |

The character stayed at one position, but head yaw changed within and between
rows (124-153, 111-124 and 124-131 degrees). These are not exact view matches.
Eye draw times remain similar; with overlap enabled XR end fell to 1.234 ms,
while XR begin rose to 3.936 ms. This supports a local pacing benefit, not
eliminated rendering work or a universal percentage gain. Copy allocator
reuse and shutdown still wait for GPU completion. The INI was restored
byte-for-byte after this trial; source defaults remain conservative because
earlier Linux deferred-wait testing did not establish stable streaming.

A subsequent one-minute walking/turning trial held 44.7-45.0 FPS in complete
ten-second windows with GPU overlap enabled. These moving views are not
matched before/after benchmarks. The owner confirmed the image, tracking and
controls looked normal. After the automatic rollback, `defer_gpu_wait=1` was
enabled as a local preference on this PC; 100% scale, two rings and fixed
foveation were retained. The shipped default remains 0. Longer sessions,
area transitions and the broader UI regression checks remain outstanding.

Attribution sequence 1791856607 (`profile_hooks=1`, GPU queries off) covered
18 threads. Approximately 22,565 pipeline binds/pair reached callbacks;
sampled VRS pipeline/draw and UI world-bind costs on the main draw thread were
0.130/0.131/0.120 ms per pair, while its two eye draws totalled 16.359 ms.
Skeleton game/mod estimates peaked at 0.088/0.034 ms per worker per pair.
These inclusive concurrent estimates cannot be added into a frame cost.
The input-tick estimate includes transport-lock waiting and is not exclusive
CPU work. Sparse resource/HUD-lock timing rounded to zero and must not be
treated as proof of zero cost. The capture's 36.2 FPS includes attribution
overhead and changing head direction; it is not a comparable throughput run.
Profiling keys were restored byte-for-byte afterward. The larger
two-engine-draw cost remains the primary architectural follow-up.

## Native Windows continuation, October 10 afternoon

Native SteamVR streaming to Steam Frame now launches the mod and an offline
character successfully. The owner reports the native performance candidate runs
"superbly". This is subjective feedback; no bounded native baseline/candidate
comparison has completed. Windows uses 80% resolution scale and shadows/AO/atmospherics 2,
fixed foveation on, gaze off, wait 0 and radius 300. Actual game eye images are
3840×2160; the standalone probe's 2160×2160 recommendation is a separate value.
The scene/settings differ from Linux; no cross-OS performance gain is inferred.

Build `e27084ed…`, installed at 17:39 PDT, adds the owner's requested body
calibration and shoulder swap controls (32 native tests pass). The settings-menu
calibration saved eye height 1.65854 m and controller span 1.50859 m live.
That changes world scale and arm proportions: any later baseline/candidate
comparison must hold this calibration and the corrected camera INI constant.
Do not include calibration/INI writes or coordinate probes in timed captures.
The new shoulder gesture uses the existing confirmed native equipment request;
hip overlay anchors are removed. Cave swing mismatch diagnostics are expanded,
but collision/warp guards are retained and the cave issue remains open.

The 17:52 cave reproduction supplied paired native/render positions confirming
terrain elevation shear on X/Z. Candidate `f559b3e5...` corrects melee's player
comparison and enemy capsules without changing the 0.65 m warp limit. All 33
native tests pass; installed at 17:55 after normal exit. The owner reports "it all
works" after the offline retest. Logs confirm accepted cave melee contacts with
no position mismatches, return outdoors, and normal exit at 18:02. This validates
the gameplay correction, not a performance improvement.
The bounded read-only coordinate capture is diagnostic evidence, not a timed run.

Before a bounded baseline capture began, Akara dialogue froze desktop and
headset. Dump analysis points to synchronous `SetForegroundWindow` inside the
mod's render/input callback, re-entering a game focus path waiting on an SRW
lock. The performance candidate now defers activation through the window queue.
25 native C++ tests pass, including the focus regression. Candidate SHA256:
`9b3ce9df5ba307222c5d1d04848ed74ab2aeec83a6f1e4325155268a55f075f7`.
Its log records Akara dialogue opening/closing twice, followed by gameplay and
normal Save & Exit. Original baseline and saves remain backed up.

The owner now requests fixes for missing torso/belt potions and loss of wrist
vitals/free-swing damage after a dungeon transition. A follow-up DLL
`996cc430ca276d65a1aaa877de960b2c95a8dd4ea2c5ae2d1062cdabcfbe134d`
is installed, preserving the performance changes and current graphics/INI.
SteamVR's native probe confirms requested formats 29/10 use resource formats
27/9 (typeless). Potion and UI conversion GPU tests failed with typeless
textures before the fix and pass with explicit typed views afterward. All 29
native tests pass, plus potion stereo/readback tests including SteamVR's sRGB
format. Callback tickets recover discarded game/UI work without executing late
callbacks against newer requests. Headset validation and torso diagnosis remain
pending; the callback cause of the reported transition issue is not yet proven.
Diagnostic stereo captures must be excluded from performance measurements.
Owner testing confirms belt models render with `996cc430…`; torso and cave
swings still fail. Cave diagnostics show fresh UI snapshots but expired native
local-player identity, ruling out a stalled UI queue for that wrist failure.
Next candidate `5847a204…` is installed with UI-thread identity refresh, closer
belt configuration support and stable empty-set detection; 30 native tests pass.
Normal game closure and backups preceded installation at 17:14 PDT. Graphics
are unchanged; belt_forward is now 0.06 m at the owner's request. Headset cave
validation confirms wrist HP/mana survives the cave transition. Cave swings
still fail: callbacks run, but the server/render player-position guard rejects
the requests. Normal-posture captures confirmed torso/shield clipping. The
installer had emitted a malformed first camera section, so Win32 ignored the
intended near=0.2 and used the default 1.5. Correcting the installed INI live
fixed torso/arms/shield clipping (owner-confirmed); installer parsing and its
Win32 API regression test now pass. The actual camera settings changed to the
intended values at 17:24:50 PDT. Future controlled comparisons must use the same
corrected configuration for both DLLs. No performance measurement is inferred.
See the current [Windows handoff](../WINDOWS_HANDOFF.md) for local evidence.

## Afternoon retest and Windows handoff

The owner returned and tested the restored baseline: **"Still stalls and loses
image quality"**. After a full PC reboot, fresh SteamVR and Frame connection,
the same baseline still failed: **"Still happening"**. The fresh driver log
recorded **119 stream reset requests before D2R launched**, from SteamVR startup
at 15:40:34 to the launch at 15:41:22 PDT. At least one failure exists outside
the mod; separate native game-update stalls remain unresolved.

| Restored-baseline capture | Pairs | Mean ms | p95 ms | Max ms |
| --- | ---: | ---: | ---: | ---: |
| Before reboot / 10201 | 360 | 23.759 | 31.631 | 37.521 |
| After reboot / 10202 | 360 | 22.932 | 30.004 | 35.161 |

Views/routes differ, and these short windows miss larger intermittent stalls
visible elsewhere in the logs. They do not demonstrate a reboot performance
gain or refute the owner's feedback. Graphics/DLL settings were unchanged.

A manual 50 Mbps streaming test was started but **not evaluated**: the owner
chose a clean Windows installation before providing a comparison. SteamVR Link
was restored to automatic bandwidth, target 200 Mbps, with OpenVR readback
verified. No conclusion can be drawn about the 50 Mbps setting.

Current next step is [WINDOWS_HANDOFF.md](../WINDOWS_HANDOFF.md), baseline first;
native Windows is untested. Portable numeric evidence, DLL hashes, validation
and limitations are in [evidence/2026-10-10-handoff.json](evidence/2026-10-10-handoff.json).
Raw logs are retained locally, excluded from GitHub. The handoff release keeps
the baseline distinct from the current-source experimental performance DLL.

## Current disposition: rollback after severe stalls

After the captures below, the owner reported random stalls during movement,
combat and menus, stuck/catching-up frames and severe temporary image-quality
loss. He requested further investigation without headset use and went to bed.
The game was closed; the pre-performance DLL `16e24f55…` was restored with
`defer_gpu_wait=0`, `model_radius=300` and all profiling off. Other settings,
game graphics, saves and SteamVR/network configuration were preserved. No game
launch or headset test was performed during this follow-up. The rollback is a
comparison baseline, not a demonstrated cure. Both candidates and all source
changes are preserved; `build-frame/d2rl-vrcam.dll` now contains the later,
**staged-only** `bc51fd37…` candidate, not the installed DLL.

Records: `build-frame/latest-perf-regression.json` and
`build-frame/latest-perf-offline-stage.json`. Original logs, ten Link histories,
configuration and replaced DLL are preserved under
`build-frame/backups/perf-regression-20261010-023124`.

### Streaming was also failing

Saved SteamVR Link histories contain 10,711 unique driver rows from
02:20:54.816 to 02:24:53.801 PDT, after merging 18,993 overlapping input rows.
The Link log records **147 video stream reset requests** in that interval,
including bursts of retransmission requests for messages no longer retained.

| Driver stage | Median ms | p95 ms | Maximum ms |
| --- | ---: | ---: | ---: |
| Gate release → app submit | 17.799 | 23.226 | 126.808 |
| Encode start → end | 7.576 | 8.866 | 12.371 |
| Send start → end | 0.528 | 0.795 | 1.564 |
| Send start → reported receive | 7.772 | 80.810 | 734.133 |
| Receive → decode start | 0.059 | 0.189 | 146.378 |
| Decode start → end | 5.560 | 7.129 | 26.534 |

These stages have different valid sample counts. Missing decode/receive fields
are unknown, not zero work; some frames never acquired those timestamps. The
driver clocks wrapped during this interval and must be handled as 32-bit
microseconds. Do not subtract remote-clock timestamps from host timestamps.
The displayed receive values are the driver's already-aligned fields, not a
direct measurement of pure radio latency.

For example, the frame released at 02:23:54.746 had gate→app submission of
16.401 ms and encode time of 7.493 ms, but send→reported receive of 674.954 ms.
At 02:23:55.546, the same measurements were 16.240, 6.319 and 734.133 ms;
that row had no decode completion. Game rendering alone does not explain those
delivery delays. The evidence implicates transport/receiver handling, but
does not distinguish wireless problems, driver queuing/retransmission or client
handling. Actual bitrate/resolution is absent, so the reported picture collapse
cannot be quantified from these logs.

Reset requests also occurred at 01:52–01:54 before installing the first CPU
candidate, and before enabling deferred wait at 02:01:10. Thus the streaming
problem cannot be attributed solely to the new DLL or wait setting. The last
session's compositor summary records 14,376 presents and 733 drops, but includes
startup/menus and uses different accounting from stereo-pair profiles; it is
not a like-for-like FPS benchmark against the earlier sessions.

Current adapter inspection found Wi-Fi power saving already off. Kernel logs
covering 01:45–02:26 showed no GPU error or USB reset; their connection-time WMM
parameter warnings do not establish a stall cause. No network tuning or global
driver changes were made. D2R settings remain resolution scale 100, FXAA,
DLSS off and dynamic resolution off. The owner briefly tried room rings 4 and 1
in the VR settings during the last session and returned to 2; final INI changes
from baseline were only wait/radius and disabled hook profiling. The later
menu/settings intervals should not be treated as identical gameplay scenes.

`steamvr/stream_report.py` reproduces these results offline. It merges repeated
histories, retains repeated present IDs at distinct times, prefers completed
records, excludes absent/invalid timings, handles timestamp rollover and scopes
reset counts to the selected interval. Five tests pass; saved full report is
`build-frame/perf-stream-report.json`. These auto-dumps are **fault-triggered
histories**, not an unbiased whole-session FPS or packet-loss survey.

### Additional code finding, staged only

Every INI reload previously marked render distance dirty, even when changing
only a profiler switch, foveation option or unrelated control. Applying model
radius forces the game's visibility sort every tick for 500 ms. The new guard
marks it dirty only for actual ring/radius changes, retaining initial and failed
application retries. This also removes a confound at the start/end of future
profiling captures. It does not fix the independent Link delivery failures or
establish a cure for large native simulation ticks. The candidate cross-build
and 14 report/launcher tests pass; this guard is not runtime/headset verified.

Next session should first establish the restored baseline's delivery behavior,
collecting app timings and Link telemetry together. If resets persist, compare
a fresh streaming session before adding DLL optimizations; then isolate stream
quality/bandwidth if needed. Avoid changing rendering, wait mode, radio settings
and refresh rate together. No further headset testing requested tonight.

## Earlier comparison and its limitations

Linux, Proton Experimental, SteamVR, Steam Frame at 90 Hz; offline Nekro in
town. Owner confirmed the installed candidate's image looked normal, then
reported definite hitches while running. No game graphics settings or
saves were modified. Resolution stayed 100%, shadows/AO 4, DLSS off, fixed and
eye-tracked foveation on, fog end 200.

Each row contains 360 stereo pairs with `profile_gpu=0`. `profile_hooks=0`
except the attribution row. Total includes game update, eye rendering and XR
pacing/waiting; reciprocal throughput is not a compositor drop/reprojection
measurement. GPU profiling was deliberately disabled because it forces the
completion fence being compared.

| Capture / sequence | Wait deferred | Model radius | Mean pair ms | Eye passes ms | Pair p95 ms |
| --- | --- | --- | ---: | ---: | ---: |
| Previous DLL / 10101 | No | 300 | 31.38 | 18.03 | 35.40 |
| Candidate / 10102 | No | 300 | 31.69 | 17.57 | 34.96 |
| Candidate + thread counters / 10103 | No | 300 | 31.72 | 17.42 | 35.28 |
| Deferred wait / 10104 | Yes | 300 | 24.60 | 17.74 | 32.31 |
| Shorter radius / 10105 | Yes | 210 | 22.70 | 15.33 | 27.41 |
| Radius repeat / 10106 | Yes | 300 | 22.53 | 15.38 | 26.41 |
| Radius repeat / 10107 | Yes | 210 | 22.46 | 14.75 | 25.87 |

The code-only comparison did **not** demonstrate increased frame rate. It also
was not an exact scene match: baseline hero position was (8791.1,11261.1),
candidate (8794.6,11255.8), and head direction changed. Launcher polling changes
were already used by both runs, so this does not measure that improvement.

Deferred waiting produced the clearest mean-cycle reduction in this session,
but was subsequently rolled back after the owner's severe-stall report. Candidate and
deferred-wait views were close (125–127 and 128 degrees). The explicit fence
fell from 4.29 ms to zero; `xrEndFrame` grew from 0.68 to 2.68 ms and
`xrWaitFrame` fell from 7.19 to 2.00 ms. This is improved overall pacing, not
7 ms of eliminated rendering work. Image copies still wait before allocator
reuse and shutdown.

The first radius comparison kept nearly the same view (128–129 degrees) and
reduced eye-pass time by 2.40 ms. During the repeats head direction changed to
154–163, then 118–155 degrees. Those repeats cannot establish a clean percentage
gain from radius alone. Fog coverage does not prove all distant models/shadows
are invisible; walk/turn and check silhouettes before calling this verified.

## What the worker counters found

Sequence 10103 covered 18 threads including input. Roughly 19,291 pipeline
binds/pair reached 17 render threads, but only 277 VRS pipeline-map lookups/pair
remained after the existing same-pipeline fast path and new generation cache.
It counted 1,368 guarded memory reads/pair. Only two memory writes/pair occurred
in this scene (~23.8 KB); none of those worker buckets reached a timing sample.
The reporter displays missing samples as `n/a`, never as evidence of free work.

The input tick's sampled inclusive wall time was 9.81 ms/pair, including waiting
and work. The transport lock still spans blocking XR/fence calls. This is a
useful follow-up target, but the number is not exclusive CPU time or a measured
saving available from lock removal. Worker/hook scopes overlap and nest; do
not sum their estimates into a frame cost.

## Candidate changes and validation (now rolled back)

- Generation-aware thread-local pipeline caches and native SRW callback locks.
- Guarded local copies instead of writes routed through wineserver; no page
  permission changes. Invalid, freed, read-only and crossing-page buffers fail
  without entering application exception handlers in the Proton test.
- Bounded per-instance portal classification reused within particle batches,
  retaining mixed portal/nonportal lanes and the verified return-portal math.
- Sparse routine CPU-time sampling; idle mouse cursor queries skipped.
- Optional UI tracing decided at startup, with no live callback-list mutation.
- Full launcher process scans every five seconds during play; known-game PID
  identity/exit checks still run every half second.
- Bounded per-thread hook/memory diagnostics with backward-compatible reports.

The DLL cross-build, host/Proton counter/cache/lock tests, Proton memory tests,
portal tests, five launcher tests and four report tests passed. An isolated
64-byte copy microbenchmark improved from 15.2 to 0.14 microseconds on one
thread. This does not imply an equivalent game frame-time saving.

Candidate SHA256:
`a3bc1aeacb1859144d160c3169347b4f8c6286234b518b6bc08885c7cfdd414e`.
Previous DLL:
`16e24f5564a822531682629a5753c0b5ef248ef48dc33f0fbe358e1e30ed15fc`.
Local ignored evidence: `build-frame/perf-*.log/.json/.csv`,
`build-frame/perf-comparison.json`, and
`build-frame/latest-perf-headless-deployment.json`. The deployment's ready and
backup directories retain both DLLs and settings. Replace DLLs only after
Save & Exit. Settings-only rollback is `defer_gpu_wait=0`, `model_radius=300`.

Shadow quality 2, transport lock separation, a dedicated XR frame thread,
shared inter-eye work, different eye image dimensions and 96/108 Hz remain
untested. Source defaults were not changed by the owner's live settings trial.

## Movement follow-up

The owner ran for three further 360-pair captures with the original candidate,
deferred waiting on and radius 210. These are separate moving scenes, not a
matched static A/B.

| Sequence | Mean pair ms | p95 ms | Maximum ms | Input stage over 4 ms |
| --- | ---: | ---: | ---: | ---: |
| 10108 | 22.94 | 24.88 | 197.56 | 4 |
| 10109 | 22.82 | 28.22 | 35.88 | 8 |
| 10110 | 23.45 | 32.22 | 36.54 | 8 |

The 20 input-stage spikes were typically 12–13 ms. CPU/GDI painting of the
1024-square interaction atlas still ran synchronously in that stage. A local
Proton test measured 10.427 ms per repaint with actual artwork and icons.
The new candidate paints on a bounded worker and uploads completed pixels on
the frame thread, with a fast RGBA row copy. No game pointers, D3D12 objects or
XR calls are passed to the worker. Changed skill/character/page contents cannot
be clicked using an obsolete image. Vitals and hover refreshes retain the wheel.

Host/Proton tests cover latest-only work, failures, shutdown/restart and stale
UI contents. Twelve worker-painted images exactly matched the synchronous
images with varying pages, hover and vitals. Installed after Save & Exit:
`d44762d4cc4a9b2cefad350a6ada3b6779b3c9bf83211be835e7b1430b28442f`.
Record: `build-frame/latest-perf-ui-deployment.json`. Subsequent moving captures
10111–10113 had **zero input stages over 4 ms in 1,080 pairs** (maximum 1.811 ms).
Two HUD refreshes measured background paint 10.12/10.28 ms and frame-thread
upload 1.27/1.16 ms. Mean pair times were 23.21, 23.62 and 22.46 ms; the routes
and views differ from the earlier runs, so no overall FPS gain is inferred.
Evidence: `build-frame/perf-running-ui-{1,2,3}.log/.json` and
`build-frame/perf-ui-comparison.json`. Headset wheel/wrist usability feedback
is pending. The owner subsequently reported severe global stalls/quality loss,
and this candidate was rolled back; the narrow UI result does not establish
overall smoothness.

The 198 ms pair included a 173 ms LevelTranslationLayer simulation tick;
other traversal ticks in the live log reached 208–753 ms. Their recorded
pipeline creations, measured room discovery and extra VR tiling were zero.
The remaining game-update cost is unresolved; native room/model activation
or loading is a follow-up hypothesis, not a demonstrated diagnosis. The
asynchronous atlas change does not address these larger stalls.

## Skeleton hook cost in the log

The pose hook (`vr/skeletons.cpp`) runs for every skeleton, every pose, on the
game's worker threads. Its cost can be read from a headset log without a
profiler:

- `Frame skeleton hook: ten seconds:` poses a second, ms a second across every
  helper, guarded reads a second and per pose (each read is a system call),
  the longest single pose, and the identity look-ups (model path read, rig
  learned, entity searched for) with their total time.
- `Frame skeleton hook: forgot N of M remembered skeletons at once` when a
  cache sweep drops 256 or more entries, or any to make room; one second later
  `in the 1.0 s after forgetting ...` gives the look-ups and the longest pose
  that followed. Compare their times with the `longest` field of
  `vrcam: a pair, ms:`.
- `Frame look-at:` and `Frame bodies:` keep their own `ms a second`; both are
  included in the hook total.

The remembered skeletons are aged (`steamvr/aged_cache.h`), a registry storage
is read as one block (`steamvr/ecs_storage.h`), and look-at and bodies share
one identity, buffer and placement read per pose call. These were built from a
code review; the gain has not been measured in a headset. The 2026-11-09 16:27
session before them logged look-at 6-14.6 and bodies 5-8.4 ms a second in
fights, with a 38-53 ms longest pair in every ten-second window.

### 2026-11-10: fewer guarded reads and less waiting on locks

Built from a count of the reads in the code; **the gain has not been measured in
a headset**. Compare the `Frame skeleton hook: ten seconds:` line (ms a second,
guarded reads a pose) of a session before and after, in town and in a fight.
Every read is still a guarded one; what is posed, when and how is unchanged.

- **A monster or townsperson near you** (head turned, body within reach): 11
  guarded calls a pose before (13 when the body was also bent), 8 now. The
  bodies take the pose look-at has read and turned instead of reading the buffer
  again, and a bend's write is the pose's one write (look-at writes only when
  nothing was bent). The entity's transform is found at the index it had last
  time, checked against the storage's packed list (`DenseHint` in
  `steamvr/ecs_storage.h`: 3 reads for 5).
- **What a skeleton is** is remembered once for look-at and bodies (they kept
  one cache each), read with their settings under one shared lock a pose. The
  model's path is read again only when the instance's name object, model or
  skeleton pointer changed, not every 250 / 500 ms; the entity is still checked
  that often.
- **The hero's pose** asked for the hero's transform 2 to 4 times (13 reads
  each, under the registry's exclusive lock) and read its matrix each time. It
  is now taken from the look-up the pose makes anyway.
- **The registry's lock** is taken shared by everything that only reads it
  (the hero's transform, the ceiling's entities, the shield and crossbow
  checks); exclusive only to find the storages or an entity again. A body's
  placement read no longer waits on them.
- **A bow drawn, a crossbow held**: every skeleton's pose read 3 to 4 fields of
  its own and took the bow rigs' lock exclusively. Now 1 read, and no lock for a
  skeleton this thread noted as not the bow in the last second.
- **`Frame room parts`** looked every skeleton's entity up for 300 ms after
  each `Frame room body` line (10 reads a pose, under the exclusive lock). That
  sampling is in developer builds only; the two lines remain.
## Draw and bind callbacks, November 10

The add-on's ReShade callbacks run for every draw and every pipeline bind on
the game's recording threads, about 11,000 of each per eye pass. Three changes
take work out of them; none changes what is drawn in VR:

- New full installs ship ReShade's Generic Depth add-on off (`[ADDON]
  DisabledAddons=Generic Depth` in the installed ReShade.ini). It watched every
  draw call for a depth buffer the VR picture does not read. Updates keep the
  player's ReShade.ini; `[render] reshade_depth=1` turns it back on.
- With Lower detail at edges off, the foveation draw, bind, target and viewport
  callbacks return after one load (`steamvr/foveation_switch.h`). The log then
  says `Frame foveation: off; draws not counted` instead of counting draws.
  Switched on while playing, a command list is followed from its next reset.
- The light-bounds and menu-world bind callbacks compare the bound pipeline
  with the few they replace (`steamvr/pipeline_set.h`) before the device
  checks, the cache and any lock.

The gain is an estimate from reading the code, about 0.1 ms a pass on the draw
thread; it has not been measured, in a headset or otherwise.
