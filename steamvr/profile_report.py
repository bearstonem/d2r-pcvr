#!/usr/bin/env python3
"""Summarize bounded live diagnostics; all timings are milliseconds.

GPU scene spans include idle queue time while the CPU records commands.
Sampled callback estimates are inclusive and must not be summed as exclusive
CPU categories. Frame scope records describe the main drawing thread; Frame
hook records additionally cover workers/input and overlap in wall time.
"""
import argparse
import csv
import math
from pathlib import Path
import statistics

COLUMNS = ('sequence frame gap begin left right end cpu_left cpu_right wait_frame '
           'locate input acquire_left acquire_right copy_submit_left copy_submit_right '
           'gpu_fence release submit gpu_scene_left gpu_scene_right gpu_effects_left '
           'gpu_effects_right gpu_copy_left gpu_copy_right gpu_valid').split()


def records(text):
    frames, scopes = [], {}
    for line in text.splitlines():
        if 'Frame profile,' in line:
            row = next(csv.reader([line.split('Frame profile,', 1)[1]]))
            if len(row) != len(COLUMNS):
                raise ValueError(f'Profile field count {len(row)}, expected {len(COLUMNS)}')
            frames.append(dict(zip(COLUMNS, map(float, row))))
        elif 'Frame scope,' in line:
            seq, count, eye, name, calls, samples, ms = line.split('Frame scope,', 1)[1].split(',')
            scopes[(int(seq), int(eye), name)] = (int(count), int(calls), int(samples), float(ms))
    return frames, scopes


def hook_records(text):
    hooks = {}
    for line in text.splitlines():
        if 'Frame hook,' not in line:
            continue
        seq, count, tid, name, calls, samples, ms, size = line.split('Frame hook,', 1)[1].split(',')
        hooks[(int(seq), int(tid), name)] = (int(count), int(calls), int(samples), float(ms), int(size))
    return hooks


def late_pairs(rows, budget):
    # A deadline overrun is not proof of a compositor frame drop/reprojection.
    return sum(sum(r[k] for k in ('gap', 'begin', 'left', 'right', 'end')) > budget for r in rows)


def timing_text(samples, ms):
    # A low-frequency hook may receive calls but no 1/64 timing sample.
    # Its encoded zero is missing timing data, not evidence of zero cost.
    return f'{ms:8.3f} ms' if samples else '     n/a ms'


def skeleton_report(text, seq, draw_thread, scopes, hooks):
    """Use the same cumulative snapshot for both eyes and all thread totals."""
    sequence, frame = None, 0
    completed = 0
    overflow = False
    for line in text.splitlines():
        if 'Frame profile: start ' in line:
            sequence = None
        elif 'Frame profile,' in line:
            fields = line.split('Frame profile,', 1)[1].split(',', 2)
            sequence, frame = int(fields[0]), int(fields[1])
        elif 'Frame profile: all-thread registry full' in line and sequence == seq:
            overflow = True
        elif 'Frame profile: complete' in line:
            if sequence == seq:
                completed = frame
            sequence = None
    rows = {tid: row for (s, tid, name), row in hooks.items() if s == seq and name == 'skeleton-mod'}
    latest = max((row[0] for (s, _, _), row in hooks.items() if s == seq), default=0)
    has_draw = any(s == seq and tid == draw_thread and row[0] == latest for (s, tid, _), row in hooks.items())
    if not completed or completed != latest or overflow or not has_draw:
        return 'Skeleton attribution unavailable (draw thread missing or all-thread capture incomplete).'
    eyes = [scopes.get((seq, eye, 'skeleton-mod')) for eye in (0, 1)]
    if any(row is None or row[0] != latest for row in eyes) or any(row[0] != latest for row in rows.values()):
        return 'Skeleton attribution unavailable (scope and thread snapshots do not match).'

    def describe(group):
        calls = sum(r[1] for r in group)
        if any(r[1] and not r[2] for r in group):
            return f'timing unavailable, {calls/latest:.1f} calls/pair'
        return f'{sum(r[3] for r in group):.3f} ms/pair, {calls/latest:.1f} calls/pair'

    direct = [rows[draw_thread]] if draw_thread in rows else []
    others = [row for tid, row in rows.items() if tid != draw_thread]
    return (f'Skeleton mod through {latest} pairs (inclusive elapsed time)\n'
            f'  Draw thread {draw_thread}, whole capture   {describe(direct)}\n'
            f'  Draw thread inside the two eyes       {describe(eyes)}\n'
            f'  Other threads, summed across threads  {describe(others)}\n'
            '  Worker time can overlap the draw thread. The capture does not measure time waiting for poses.')


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('log', type=Path)
    ap.add_argument('--sequence', type=int)
    ap.add_argument('--draw-thread', type=int, metavar='TID',
                    help='summarize skeleton work using the thread marked (draw) in the capture')
    ap.add_argument('--csv', type=Path)
    ap.add_argument('--refresh-hz', type=float, default=90, help='panel refresh; default target is one stereo pair per refresh')
    ap.add_argument('--pair-refreshes', type=int, choices=(1, 2), default=1,
                    help='display intervals per stereo pair (1: full rate, 2: explicit half-rate comparison)')
    args = ap.parse_args()
    text = args.log.read_text(errors='replace')
    frames, scopes = records(text)
    if not frames:
        raise SystemExit('No profile frames recorded yet')
    seq = args.sequence if args.sequence is not None else int(frames[-1]['sequence'])
    rows = [r for r in frames if r['sequence'] == seq]
    if not rows:
        raise SystemExit(f'No frames for sequence {seq}')
    print(f'Sequence {seq}: {len(rows)} pairs; {sum(r["gpu_valid"] != 0 for r in rows)} valid GPU samples')
    if not math.isfinite(args.refresh_hz) or args.refresh_hz <= 0:
        raise SystemExit('Refresh rate must be finite and positive')
    budget = 1000 * args.pair_refreshes / args.refresh_hz
    late = late_pairs(rows, budget)
    rate = 'full rate' if args.pair_refreshes == 1 else 'half rate'
    print(f'Pairs over {budget:.3f} ms ({args.refresh_hz:g} Hz, {rate}): {late}/{len(rows)} ({100*late/len(rows):.1f}%). This alone does not measure compositor drops.')
    print(f'{"stage":24s} {"mean":>9s} {"median":>9s} {"p95":>9s} {"max":>9s}')
    for key in ['cycle'] + COLUMNS[2:-1]:
        values = [(sum(r[k] for k in ('gap', 'begin', 'left', 'right', 'end')) if key == 'cycle' else r[key])
                  for r in rows if not key.startswith(('gpu_scene', 'gpu_effects', 'gpu_copy')) or r['gpu_valid']]
        if not values:
            continue
        ordered = sorted(values)
        print(f'{key:24s} {statistics.mean(values):9.3f} {statistics.median(values):9.3f} '
              f'{ordered[min(len(ordered)-1, int(.95*len(ordered)))]:9.3f} {max(values):9.3f}')
    print('Scopes: estimated inclusive ms/eye; calls/pair; samples (main render thread only)')
    for (scope_seq, eye, name), (count, calls, samples, ms) in sorted(scopes.items()):
        if scope_seq == seq:
            print(f'  eye {eye} {name:18s} {timing_text(samples, ms)} {calls/count:9.1f} calls {samples:8d} samples')
    hooks = hook_records(text)
    if args.draw_thread is not None:
        print(skeleton_report(text, seq, args.draw_thread, scopes, hooks))
    if hooks:
        print('All-thread hooks: estimated inclusive ms/pair by thread; concurrent/nested times must not be summed as frame cost')
        for (hook_seq, tid, name), (count, calls, samples, ms, size) in sorted(hooks.items()):
            if hook_seq == seq:
                print(f'  thread {tid:6d} {name:18s} {timing_text(samples, ms)} {calls/count:9.1f} calls/pair {samples:8d} samples {size/count:10.0f} bytes/pair')
    if args.csv:
        with args.csv.open('w', newline='') as out:
            w = csv.DictWriter(out, fieldnames=COLUMNS); w.writeheader(); w.writerows(rows)


if __name__ == '__main__':
    main()
