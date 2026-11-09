# SPDX-FileCopyrightText: 2026 Raphael Eismann
# SPDX-License-Identifier: GPL-3.0-or-later
"""compare_events.py: two event files side by side, on one grid."""

import contextlib
import io
import json
import sys
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "tools"))
import compare_events as ce  # noqa: E402


def event(stem, beat, shape="scat", bars=1):
    return {"stem": stem, "beat": float(beat), "shape": shape, "bars": bars, "reversed": False,
            "seed": "1", "active": True, "origin": "prepared"}


class TheOverlap(unittest.TestCase):
    def test_the_same_stem_and_shape_within_four_bars_matches(self):
        a = [event(2, 0, "orbit", 8)]
        self.assertEqual(1, ce.matched(a, [event(2, 16, "orbit", 8)]))
        self.assertEqual(0, ce.matched(a, [event(2, 17, "orbit", 8)]))
        self.assertEqual(0, ce.matched(a, [event(1, 0, "orbit", 8)]))

    def test_a_ping_and_a_scat_are_both_accents(self):
        a = [event(1, 0, "scat")]
        self.assertEqual(1, ce.matched(a, [event(1, 4, "ping")]))
        self.assertEqual(0, ce.matched(a, [event(1, 4, "ping")], strict=True))
        self.assertEqual(0, ce.matched(a, [event(1, 4, "rise")]))


class TheStats(unittest.TestCase):
    GRID = ce.Grid(120.0, 0.5)    # a bar is 2 s

    def test_rates_and_shares(self):
        events = [event(1, 0), event(2, 6, "orbit", 4), event(3, 32, "rise", 4), event(1, 40)]
        stats = ce.stats(events, self.GRID, minutes=1.0, breaks=[(0, 2)])
        self.assertEqual(4.0, stats["events/min"])
        self.assertEqual(0.75, stats["on a bar line"])
        self.assertEqual(0.5, stats["on a phrase start (8 bars)"])
        self.assertEqual(0.5, stats["in a break"])
        self.assertEqual(2, stats["stem 1 scat"])
        self.assertEqual(1, stats["stem 2 orbit"])

    def test_the_peak_counts_starts_in_any_thirty_seconds(self):
        events = [event(1, 0), event(2, 8), event(3, 56), event(1, 59), event(2, 62)]   # 2 s a bar: 60 beats = 30 s
        self.assertEqual(4, ce.stats(events, self.GRID, minutes=1.0)["peak in 30 s"])

    def test_overruns_are_counted_with_the_content(self):
        levels = [[-10.0] * 16, [-10.0] * 16, [-10.0] * 3 + [-90.0] * 13, [-10.0] * 16]
        bars = ce.passages.Bars(levels, [[-30.0] * 16] * 4, [1.0] * 16)
        events = [event(2, 0, "orbit", 8), event(2, 0, "scat", 2)]
        self.assertEqual(1, ce.stats(events, self.GRID, minutes=1.0, bars=bars)["overrun their sound"])
        self.assertNotIn("overrun their sound", ce.stats(events, self.GRID, minutes=1.0))

    def test_only_events_before_the_last_bar_count(self):
        events = [event(1, 0), event(1, 36)]
        self.assertEqual([events[0]], ce.until(events, 9))


class TheTool(unittest.TestCase):
    def test_prints_both_files_side_by_side(self):
        with tempfile.TemporaryDirectory() as folder:
            a, b = Path(folder) / "a.a3events.json", Path(folder) / "b.a3events.json"
            a.write_text(json.dumps({"version": 1, "events": [event(1, 0), event(2, 32, "orbit", 8)]}))
            b.write_text(json.dumps({"version": 1, "events": [event(1, 4)]}))
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                code = ce.main([str(a), str(b), "--bpm", "120", "--first-beat", "0.5", "--length-seconds", "60"])
        self.assertEqual(0, code)
        text = out.getvalue()
        self.assertIn("events/min", text)
        self.assertIn("a.a3events.json", text)
        self.assertRegex(text, r"of A matched in B \(loose\)\s+1/2")


if __name__ == "__main__":
    unittest.main()
