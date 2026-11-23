import unittest
import contextlib
import io
from pathlib import Path
import sys
import tempfile
from unittest.mock import patch
from profile_report import COLUMNS, records, hook_records, late_pairs, timing_text, skeleton_report, main


class ReportTest(unittest.TestCase):
    def test_missing_samples_are_not_reported_as_free_work(self):
        self.assertIn('n/a', timing_text(0, 0))
        self.assertIn('0.000', timing_text(1, 0))

    def test_older_logs_still_parse(self):
        row = [1, 1] + [0] * (len(COLUMNS) - 2)
        frames, scopes = records('prefix Frame profile,' + ','.join(map(str, row)))
        self.assertEqual(len(frames), 1)
        self.assertEqual(scopes, {})

    def test_workers_and_sequences_remain_separate(self):
        text = ('Frame hook,1,120,42,memory-write,1200,18,0.100,14400\n'
                'Frame hook,1,360,42,memory-write,3600,56,0.080,43200\n'
                'Frame hook,1,360,43,memory-write,7200,112,0.130,86400\n'
                'Frame hook,2,120,42,memory-write,2400,36,0.110,28800\n')
        hooks = hook_records(text)
        self.assertEqual(len(hooks), 3)
        self.assertEqual(hooks[(1, 42, 'memory-write')], (360, 3600, 56, .080, 43200))

    def test_budget_counts_include_idle_wait(self):
        rows = [dict(gap=1, begin=2, left=7, right=7, end=3),
                dict(gap=1, begin=4, left=7, right=7, end=4)]
        self.assertEqual(late_pairs(rows, 22.222), 1)
        self.assertEqual(late_pairs(rows, 18.519), 2)

    def test_skeleton_draw_and_other_threads_use_one_complete_sequence(self):
        text = 'Frame profile,7,360,0\nFrame profile: complete\n'
        scopes = {(7, 0, 'skeleton-mod'): (360, 360, 360, .1),
                  (7, 1, 'skeleton-mod'): (360, 720, 720, .2)}
        hooks = {(7, 11, 'view'): (360, 1, 1, .1, 0),
                 (7, 11, 'skeleton-mod'): (360, 1440, 1440, .4, 0),
                 (7, 22, 'skeleton-mod'): (360, 3600, 3600, .7, 0),
                 (7, 33, 'skeleton-mod'): (360, 3600, 3600, .5, 0),
                 (8, 33, 'skeleton-mod'): (360, 10, 10, 90., 0)}
        def report(log=text):
            return skeleton_report(log, 7, 11, scopes, hooks)
        self.assertIn('0.400 ms/pair, 4.0 calls/pair', report())
        self.assertIn('0.300 ms/pair, 3.0 calls/pair', report())
        self.assertIn('1.200 ms/pair, 20.0 calls/pair', report())
        self.assertIn('unavailable', report('Frame profile,7,360,0'))
        self.assertIn('unavailable', skeleton_report(text, 7, 99, scopes, hooks))
        self.assertIn('unavailable', report(text.replace('7,360', '7,361')))
        self.assertIn('unavailable', report(text.replace('Frame profile: complete',
                                            'Frame profile: all-thread registry full\nFrame profile: complete')))
        hooks[(7, 22, 'skeleton-mod')] = (360, 1, 0, 0, 0)
        self.assertIn('timing unavailable', report())
        scopes[(7, 0, 'skeleton-mod')] = (120, 120, 120, .1)
        self.assertIn('snapshots do not match', report())

    def report(self, *options):
        row = dict.fromkeys(COLUMNS, 0)
        row.update(sequence=1, frame=1, left=6, right=7)  # 13 ms misses full 90 Hz.
        with tempfile.TemporaryDirectory() as temporary:
            log = Path(temporary) / 'fixture.log'
            log.write_text('Frame profile,' + ','.join(str(row[k]) for k in COLUMNS))
            with patch.object(sys, 'argv', ['profile_report.py', str(log), *options]), \
                    contextlib.redirect_stdout(io.StringIO()) as output:
                main()
            return output.getvalue()

    def test_full_refresh_is_default_for_one_stereo_pair_per_tick(self):
        self.assertIn('11.111 ms (90 Hz, full rate): 1/1', self.report())
        self.assertIn('10.417 ms (96 Hz, full rate): 1/1', self.report('--refresh-hz', '96'))

    def test_half_rate_requires_explicit_comparison(self):
        self.assertIn('22.222 ms (90 Hz, half rate): 0/1', self.report('--pair-refreshes', '2'))

    def test_invalid_refresh_rates_cannot_hide_deadline_overruns(self):
        for value in ('0', '-90', 'nan', 'inf'):
            with self.subTest(value=value), self.assertRaisesRegex(SystemExit, 'finite and positive'):
                self.report('--refresh-hz', value)


if __name__ == '__main__':
    unittest.main()
