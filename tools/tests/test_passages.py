# SPDX-FileCopyrightText: 2026 Raphael Eismann
# SPDX-License-Identifier: GPL-3.0-or-later
"""Passage detection on per-bar numbers (tools/passages.py), on synthetic data."""

import sys
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "tools"))
import passages as ps  # noqa: E402

FULL = -15.0       # a stem playing
QUIET = -40.0      # a stem far below its usual level, still above the floor
SILENT = -90.0


def bars_from(levels, bright=None, flux=None):
    """`levels`: four lists of per-bar dB. Brightness follows the level 20 dB
    lower unless given; flux is flat unless given."""
    count = len(levels[0])
    if bright is None:
        bright = [[value - 20.0 for value in stem] for stem in levels]
    if flux is None:
        flux = [1.0] * count
    return ps.Bars(levels, bright, flux)


def stretch(*parts):
    """[(value, bars), ...] -> a per-bar list."""
    result = []
    for value, count in parts:
        result.extend([value] * count)
    return result


def kinds(found, kind):
    return [p for p in found if p.kind == kind]


class Solo(unittest.TestCase):
    def test_a_vocal_alone_over_a_break_is_a_solo_of_the_vocal(self):
        drums = stretch((FULL, 8), (SILENT, 8), (FULL, 8))
        vocal = stretch((FULL, 24))
        other = stretch((FULL, 8), (SILENT, 8), (FULL, 8))
        found = ps.find_passages(bars_from([drums, drums, other, vocal]))
        self.assertEqual([ps.Passage("solo", 8, 16, 3)], kinds(found, "solo"))

    def test_three_bars_alone_are_not_a_solo(self):
        drums = stretch((FULL, 8), (SILENT, 3), (FULL, 8))
        vocal = stretch((FULL, 19))
        found = ps.find_passages(bars_from([drums, drums, drums, vocal]))
        self.assertEqual([], kinds(found, "solo"))

    def test_no_solo_while_the_drums_play(self):
        drums = stretch((FULL, 16))
        vocal = stretch((FULL + 10, 16))
        silent = stretch((SILENT, 16))
        found = ps.find_passages(bars_from([drums, silent, silent, vocal]))
        self.assertEqual([], kinds(found, "solo"))

    def test_two_upper_stems_sharing_the_break_is_no_solo(self):
        drums = stretch((FULL, 8), (SILENT, 8), (FULL, 8))
        upper = stretch((FULL, 24))
        found = ps.find_passages(bars_from([drums, drums, upper, upper]))
        self.assertEqual([], kinds(found, "solo"))
        self.assertEqual([ps.Passage("break", 8, 16, None)], kinds(found, "break"))


class Break(unittest.TestCase):
    def test_drums_and_bass_far_down_for_four_bars_is_a_break(self):
        low = stretch((FULL, 8), (QUIET, 4), (FULL, 8))
        upper = stretch((FULL, 20))
        found = ps.find_passages(bars_from([low, low, upper, upper]))
        self.assertEqual([ps.Passage("break", 8, 12, None)], kinds(found, "break"))

    def test_bass_alone_keeps_the_floor(self):
        drums = stretch((FULL, 8), (SILENT, 8), (FULL, 8))
        bass = stretch((FULL, 24))
        found = ps.find_passages(bars_from([drums, bass, bass, bass]))
        self.assertEqual([], kinds(found, "break"))

    def test_a_short_dip_is_no_break(self):
        low = stretch((FULL, 8), (SILENT, 3), (FULL, 8))
        found = ps.find_passages(bars_from([low, low, stretch((FULL, 19)), stretch((FULL, 19))]))
        self.assertEqual([], kinds(found, "break"))


class Return(unittest.TestCase):
    def test_the_drums_coming_back_after_a_break_is_a_return_on_that_bar(self):
        low = stretch((FULL, 8), (SILENT, 8), (FULL, 8))
        found = ps.find_passages(bars_from([low, low, stretch((FULL, 24)), stretch((FULL, 24))]))
        self.assertEqual([ps.Passage("return", 16, 17, 0)], kinds(found, "return"))

    def test_no_return_without_a_break(self):
        steady = stretch((FULL, 24))
        found = ps.find_passages(bars_from([steady] * 4))
        self.assertEqual([], kinds(found, "return"))

    def test_a_break_running_to_the_end_has_no_return(self):
        low = stretch((FULL, 8), (SILENT, 8))
        found = ps.find_passages(bars_from([low, low, stretch((FULL, 16)), stretch((FULL, 16))]))
        self.assertEqual([], kinds(found, "return"))


class Build(unittest.TestCase):
    def rising(self, steps, step_db=2.0, bright_step_db=2.0):
        """Drums out for 4 bars, then `steps` bars of the other stem rising,
        then the drums return."""
        drums = stretch((FULL, 4), (SILENT, 4 + steps), (FULL, 4))
        other = stretch((FULL - 12, 8)) + [FULL - 12 + step_db * (i + 1) for i in range(steps)] + stretch((FULL, 4))
        vocal = stretch((FULL - 12, 8 + steps + 4))
        levels = [drums, drums, other, vocal]
        bright = [[value - 20.0 for value in stem] for stem in levels]
        bright[2] = stretch((FULL - 32, 8)) + [FULL - 32 + bright_step_db * (i + 1) for i in range(steps)] + stretch((FULL - 20, 4))
        return ps.find_passages(bars_from(levels, bright))

    def test_energy_and_brightness_rising_over_six_bars_is_a_build_of_the_rising_stem(self):
        builds = kinds(self.rising(6), "build")
        self.assertEqual(1, len(builds))
        build = builds[0]
        self.assertEqual(2, build.stem)
        self.assertEqual(14, build.end)                  # it ends where the drums return
        self.assertGreaterEqual(build.end - build.start, 4)
        self.assertLessEqual(build.start, 8)

    def test_a_riser_under_a_steady_louder_vocal_is_a_build_from_its_first_bar(self):
        low = stretch((FULL, 4), (SILENT, 12), (FULL, 4))
        riser = stretch((SILENT, 8)) + [-38.0 + 4.0 * i for i in range(8)] + stretch((FULL, 4))
        vocal = stretch((FULL, 20))
        bright = [[value - 20.0 for value in low], [value - 20.0 for value in low],
                  [value - 3.0 for value in riser], stretch((-60.0, 20))]
        builds = kinds(ps.find_passages(bars_from([low, low, riser, vocal], bright)), "build")
        self.assertEqual([ps.Passage("build", 8, 16, 2)], builds)

    def test_a_rise_of_three_bars_is_no_build(self):
        self.assertEqual([], kinds(self.rising(3, step_db=4.0, bright_step_db=4.0), "build"))

    def test_louder_but_not_brighter_is_no_build(self):
        self.assertEqual([], kinds(self.rising(6, bright_step_db=0.0), "build"))

    def test_a_steady_track_has_no_build(self):
        self.assertEqual([], kinds(ps.find_passages(bars_from([stretch((FULL, 32))] * 4)), "build"))


class Fill(unittest.TestCase):
    def test_an_onset_spike_in_the_last_bar_of_a_phrase_is_a_fill(self):
        flux = [1.0] * 24
        flux[15] = 3.0
        found = ps.find_passages(bars_from([stretch((FULL, 24))] * 4, flux=flux))
        self.assertEqual([ps.Passage("fill", 15, 16, None)], kinds(found, "fill"))

    def test_a_spike_inside_a_phrase_is_no_fill(self):
        flux = [1.0] * 24
        flux[12] = 3.0
        found = ps.find_passages(bars_from([stretch((FULL, 24))] * 4, flux=flux))
        self.assertEqual([], kinds(found, "fill"))

    def test_a_small_rise_is_no_fill(self):
        flux = [1.0] * 24
        flux[15] = 1.2
        found = ps.find_passages(bars_from([stretch((FULL, 24))] * 4, flux=flux))
        self.assertEqual([], kinds(found, "fill"))


class Nothing(unittest.TestCase):
    def test_a_steady_full_mix_has_no_passage(self):
        self.assertEqual([], ps.find_passages(bars_from([stretch((FULL, 64))] * 4)))

    def test_a_silent_track_has_no_passage_but_its_breaks(self):
        found = ps.find_passages(bars_from([stretch((SILENT, 32))] * 4))
        self.assertEqual({"break"}, {p.kind for p in found})

    def test_an_empty_track_has_no_passage(self):
        self.assertEqual([], ps.find_passages(ps.Bars([[], [], [], []], [[], [], [], []], [])))


class Audible(unittest.TestCase):
    def test_a_stem_under_the_floor_or_masked_is_not_audible(self):
        bars = bars_from([[-5.0], [FULL], [-50.0], [-36.0]])
        self.assertTrue(ps.audible(bars, 0, 0))
        self.assertFalse(ps.audible(bars, 2, 0))   # under the floor
        self.assertFalse(ps.audible(bars, 3, 0))   # 31 dB under the loudest


if __name__ == "__main__":
    unittest.main()
