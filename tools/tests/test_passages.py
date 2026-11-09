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

    def test_the_tops_alone_over_a_break_is_a_solo_of_the_tops(self):
        floor = stretch((FULL, 8), (SILENT, 8), (FULL, 8))
        tops = stretch((FULL, 24))
        found = ps.find_passages(bars_from([floor, tops, floor, floor]))
        self.assertEqual([ps.Passage("solo", 8, 16, 1)], kinds(found, "solo"))

    def test_no_solo_while_the_floor_plays(self):
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

    def test_the_floor_out_is_a_break_while_the_tops_play_on(self):
        floor = stretch((FULL, 8), (SILENT, 8), (FULL, 8))
        movers = stretch((FULL, 24))
        found = ps.find_passages(bars_from([floor, movers, movers, movers]))
        self.assertEqual([ps.Passage("break", 8, 16, None)], kinds(found, "break"))

    def test_the_floor_playing_is_no_break_when_the_movers_drop(self):
        floor = stretch((FULL, 24))
        movers = stretch((FULL, 8), (SILENT, 8), (FULL, 8))
        found = ps.find_passages(bars_from([floor, movers, movers, movers]))
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


class HighsOnly(unittest.TestCase):
    """The floor stem with its low end gone: hats and top percussion alone."""
    HATS, HATS_BRIGHT = -30.0, -32.0      # most of the stem's energy above 4 kHz
    KICK, KICK_BRIGHT = FULL, -35.0       # full band: almost nothing up there

    def track(self, floor_bars):
        """Floor full for 8 bars, then `floor_bars` ("h" hats, "k" kick), then full for 8."""
        level = stretch((self.KICK, 8)) + [self.HATS if c == "h" else self.KICK for c in floor_bars] + stretch((self.KICK, 8))
        bright = (stretch((self.KICK_BRIGHT, 8)) + [self.HATS_BRIGHT if c == "h" else self.KICK_BRIGHT for c in floor_bars]
                  + stretch((self.KICK_BRIGHT, 8)))
        movers = stretch((FULL, len(level)))
        return ps.find_passages(bars_from([level, movers, movers, movers],
                                          [bright, [v - 20 for v in movers], [v - 20 for v in movers], [v - 20 for v in movers]]))

    def test_four_bars_of_floor_highs_in_a_break_are_a_highs_only_passage(self):
        self.assertEqual([ps.Passage("highs", 8, 12, 0)], kinds(self.track("hhhh"), "highs"))

    def test_three_bars_are_not(self):
        self.assertEqual([], kinds(self.track("hhh"), "highs"))

    def test_a_kick_bar_ends_the_stretch(self):
        self.assertEqual([ps.Passage("highs", 8, 12, 0)], kinds(self.track("hhhhkhhh"), "highs"))

    def test_a_full_band_floor_is_never_highs_only(self):
        self.assertEqual([], kinds(self.track("kkkkkkkk"), "highs"))

    def test_the_floor_s_hats_alone_are_no_solo(self):
        floor = stretch((self.KICK, 8)) + stretch((self.HATS, 8)) + stretch((self.KICK, 8))
        bright = stretch((self.KICK_BRIGHT, 8)) + stretch((self.HATS_BRIGHT, 8)) + stretch((self.KICK_BRIGHT, 8))
        silent = stretch((SILENT, 8)) + stretch((SILENT, 8)) + stretch((FULL, 8))
        found = ps.find_passages(bars_from([floor, silent, silent, silent],
                                           [bright, [v - 20 for v in silent]] + [[v - 20 for v in silent]] * 2))
        self.assertEqual([], kinds(found, "solo"))
        self.assertEqual([ps.Passage("highs", 8, 16, 0)], kinds(found, "highs"))


class Nothing(unittest.TestCase):
    def test_a_steady_full_mix_has_no_passage(self):
        self.assertEqual([], ps.find_passages(bars_from([stretch((FULL, 64))] * 4)))

    def test_a_silent_track_has_no_passage_but_its_breaks(self):
        found = ps.find_passages(bars_from([stretch((SILENT, 32))] * 4))
        self.assertEqual({"break"}, {p.kind for p in found})

    def test_an_empty_track_has_no_passage(self):
        self.assertEqual([], ps.find_passages(ps.Bars([[], [], [], []], [[], [], [], []], [])))


class Groove(unittest.TestCase):
    """The floor playing (drums and bass in), between the breaks."""

    def test_the_stretches_between_breaks_are_grooves(self):
        floor = stretch((FULL, 16), (SILENT, 8), (FULL, 16))
        found = ps.find_all_passages(bars_from([floor, stretch((FULL, 40)), stretch((FULL, 40)), stretch((FULL, 40))]))
        self.assertEqual([ps.Passage("groove", 0, 16, None), ps.Passage("groove", 24, 40, None)],
                         kinds(found, "groove"))

    def test_a_dropout_of_a_bar_does_not_split_the_groove(self):
        floor = stretch((FULL, 15), (SILENT, 1), (FULL, 16))
        found = ps.find_all_passages(bars_from([floor] + [stretch((FULL, 32))] * 3))
        self.assertEqual([ps.Passage("groove", 0, 32, None)], kinds(found, "groove"))

    def test_a_short_stretch_of_floor_is_no_groove(self):
        floor = stretch((SILENT, 8), (FULL, ps.MIN_GROOVE_BARS - 1), (SILENT, 8))
        found = ps.find_all_passages(bars_from([floor] + [stretch((FULL, len(floor)))] * 3))
        self.assertEqual([], kinds(found, "groove"))

    def test_v2_s_passages_have_no_grooves(self):
        self.assertEqual([], ps.find_passages(bars_from([stretch((FULL, 64))] * 4)))


class Dropout(unittest.TestCase):
    """The floor out for one to three bars inside a groove: a turn, not a break."""

    def found(self, floor):
        return ps.find_all_passages(bars_from([floor] + [stretch((FULL, len(floor)))] * 3))

    def test_one_bar_without_the_floor_is_a_dropout(self):
        floor = stretch((FULL, 15), (SILENT, 1), (FULL, 16))
        self.assertEqual([ps.Passage("dropout", 15, 16, None)], kinds(self.found(floor), "dropout"))

    def test_three_bars_are_a_dropout_four_a_break(self):
        three = stretch((FULL, 13), (SILENT, 3), (FULL, 16))
        four = stretch((FULL, 12), (SILENT, 4), (FULL, 16))
        self.assertEqual([ps.Passage("dropout", 13, 16, None)], kinds(self.found(three), "dropout"))
        self.assertEqual([], kinds(self.found(four), "dropout"))

    def test_the_quiet_start_of_a_track_is_no_dropout(self):
        floor = stretch((SILENT, 2), (FULL, 30))
        self.assertEqual([], kinds(self.found(floor), "dropout"))


class Entry(unittest.TestCase):
    """A mover coming in after a rest: the passage runs while it stays."""

    def found(self, vocal):
        full = stretch((FULL, len(vocal)))
        return kinds(ps.find_all_passages(bars_from([full, full, full, vocal])), "entry")

    def test_a_vocal_coming_in_is_an_entry_lasting_while_it_sings(self):
        vocal = stretch((SILENT, 8), (FULL, 12), (SILENT, 4), (FULL, 16))
        self.assertEqual([ps.Passage("entry", 8, 20, 3), ps.Passage("entry", 24, 40, 3)], self.found(vocal))

    def test_a_single_bar_is_no_entry(self):
        vocal = stretch((SILENT, 8), (FULL, 1), (SILENT, 23))
        self.assertEqual([], self.found(vocal))

    def test_a_one_bar_gap_is_no_new_entry(self):
        vocal = stretch((SILENT, 8), (FULL, 8), (SILENT, 1), (FULL, 15))
        self.assertEqual([ps.Passage("entry", 8, 32, 3)], self.found(vocal))

    def test_a_stem_playing_from_the_start_never_enters(self):
        self.assertEqual([], self.found(stretch((FULL, 32))))

    def test_every_mover_can_enter_the_floor_does_not(self):
        late = stretch((SILENT, 8), (FULL, 24))
        found = ps.find_all_passages(bars_from([late, late, late, late]))
        self.assertEqual({1, 2, 3}, {p.stem for p in kinds(found, "entry")})


class PhraseOffset(unittest.TestCase):
    def test_the_returns_set_the_phrase_grid(self):
        returns = [ps.Passage("return", bar, bar + 1, 0) for bar in (9, 49, 105)]
        self.assertEqual(1, ps.phrase_offset(returns, 8))

    def test_without_a_return_the_grid_starts_on_the_one(self):
        self.assertEqual(0, ps.phrase_offset([ps.Passage("break", 3, 9, None)], 8))

    def test_a_tie_goes_to_the_earliest_return(self):
        returns = [ps.Passage("return", bar, bar + 1, 0) for bar in (19, 34)]
        self.assertEqual(3, ps.phrase_offset(returns, 8))


def bars_with_beats(floor_beats, mover_beats=None):
    """Per-beat levels: the floor's from `floor_beats`, each mover's from
    `mover_beats` (one list per mover) or FULL; the per-bar numbers follow
    by power."""
    count = len(floor_beats)
    movers = mover_beats or [[FULL] * count] * 3
    beat_level = [list(floor_beats)] + [list(m) for m in movers]

    def per_bar(beats):
        return [ps.to_db(sum(ps.power(v) for v in beats[b:b + 4]) / 4) for b in range(0, count, 4)]
    level = [per_bar(stem) for stem in beat_level]
    bars = bars_from(level)
    bars.beat_level = beat_level
    return bars


def floor_with_quiet_beats(quiet, beats=128):
    return [SILENT if b in quiet else FULL for b in range(beats)]


class Gap(unittest.TestCase):
    """The floor out for one to four beats, with the floor playing on both sides."""

    def test_one_to_four_beats_of_floor_out_are_a_gap(self):
        for length in (1, 2, 3, 4):
            bars = bars_with_beats(floor_with_quiet_beats(range(41, 41 + length)))
            self.assertEqual([ps.Gap(41, 41 + length)], ps.find_gaps(bars), length)

    def test_five_beats_are_no_gap(self):
        self.assertEqual([], ps.find_gaps(bars_with_beats(floor_with_quiet_beats(range(41, 46)))))

    def test_the_floor_at_its_usual_level_has_no_gap(self):
        self.assertEqual([], ps.find_gaps(bars_with_beats([FULL] * 128)))

    def test_a_dip_not_far_enough_under_the_usual_level_is_no_gap(self):
        floor = [FULL - ps.GAP_BELOW_REFERENCE_DB + 2 if b == 41 else FULL for b in range(128)]
        self.assertEqual([], ps.find_gaps(bars_with_beats(floor)))

    def test_the_quiet_start_and_end_of_a_track_are_no_gaps(self):
        self.assertEqual([], ps.find_gaps(bars_with_beats(floor_with_quiet_beats({0, 1, 126, 127}))))

    def test_a_gap_repeating_every_four_bars_is_the_groove_s_pattern(self):
        quiet = {b for b in range(6, 128, 16)} | {75, 76}
        self.assertEqual([ps.Gap(75, 77)], ps.find_gaps(bars_with_beats(floor_with_quiet_beats(quiet))))

    def test_no_beat_levels_no_gaps(self):
        self.assertEqual([], ps.find_gaps(bars_from([stretch((FULL, 32))] * 4)))

    def test_who_sounds_in_a_gap(self):
        movers = [[FULL] * 128, [SILENT if 40 <= b < 44 else FULL for b in range(128)], [-50.0] * 128]
        bars = bars_with_beats(floor_with_quiet_beats(range(41, 43)), movers)
        self.assertTrue(ps.audible_during(bars, 1, 41, 43))
        self.assertFalse(ps.audible_during(bars, 2, 41, 43))      # silent there
        self.assertFalse(ps.audible_during(bars, 3, 41, 43))      # under the bleed floor


class Audible(unittest.TestCase):
    def test_a_stem_under_the_floor_or_masked_is_not_audible(self):
        bars = bars_from([[-5.0], [FULL], [-50.0], [-36.0]])
        self.assertTrue(ps.audible(bars, 0, 0))
        self.assertFalse(ps.audible(bars, 2, 0))   # under the floor
        self.assertFalse(ps.audible(bars, 3, 0))   # 31 dB under the loudest


if __name__ == "__main__":
    unittest.main()
