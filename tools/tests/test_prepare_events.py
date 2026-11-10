# SPDX-FileCopyrightText: 2026 Raphael Eismann
# SPDX-License-Identifier: GPL-3.0-or-later
"""stemdeck-prepare-events: event files for a whole library.

The schema check below mirrors Source/StemEventsFile.cpp (the loader): every
file the tool writes has to pass it, or the app would drop events."""

import contextlib
import io
import json
import math
import os
import random
import shutil
import struct
import sys
import tempfile
import unittest
import wave
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "tools"))
import passages as ps  # noqa: E402
import prepare_events as pe  # noqa: E402
import stem_analysis as sa  # noqa: E402

SHAPES = {"orbit", "ping", "rise", "scat"}
ORIGINS = {"hand", "prepared"}
BARS = {0.25, 0.5, 1, 2, 4, 8}
FIELDS = ("stem", "beat", "shape", "bars", "reversed", "seed", "active", "origin")
MAX_EVENTS = 512
SLOT_TOLERANCE = 1e-6


def check_event(item):
    """True when the app's loader would accept this event."""
    if not isinstance(item, dict) or any(name not in item for name in FIELDS):
        return False
    beat = item["beat"]
    if isinstance(beat, bool) or not isinstance(beat, (int, float)) or not math.isfinite(beat):
        return False
    seed = item["seed"]
    if not isinstance(seed, str) or not seed.isascii() or not seed.isdigit() or len(seed) > 20:
        return False
    if int(seed) >= 2 ** 64:
        return False
    stem = item["stem"]
    return (isinstance(stem, int) and not isinstance(stem, bool) and 0 <= stem < 4
            and item["shape"] in SHAPES and item["origin"] in ORIGINS
            and isinstance(item["bars"], (int, float)) and item["bars"] in BARS
            and isinstance(item["reversed"], bool) and isinstance(item["active"], bool))


def load_like_the_app(text):
    """(status, events); status is ok | damaged | newer, events only for ok.
    Events the loader would drop make the status damaged here: the tool must
    never write one."""
    try:
        root = json.loads(text)
    except ValueError:
        return "damaged", []
    if not isinstance(root, dict) or "version" not in root or "events" not in root:
        return "damaged", []
    version = root["version"]
    if isinstance(version, bool) or not isinstance(version, int):
        return "damaged", []
    if version > 1:
        return "newer", []
    if version < 1 or not isinstance(root["events"], list):
        return "damaged", []
    events = root["events"]
    if len(events) > MAX_EVENTS or not all(check_event(e) for e in events):
        return "damaged", []
    for i, a in enumerate(events):
        for b in events[:i]:
            if a["stem"] == b["stem"] and abs(a["beat"] - b["beat"]) <= SLOT_TOLERANCE:
                return "damaged", []
    return "ok", events


def wav_header(seconds, rate=8000, data_size=None):
    """A mono 8-bit PCM header claiming `seconds` of audio (or `data_size`
    bytes); no samples follow."""
    if data_size is None:
        data_size = int(seconds * rate)
    fmt = struct.pack("<HHIIHH", 1, 1, rate, rate, 1, 8)
    riff_size = min(36 + data_size, 0xFFFFFFFF)
    return (b"RIFF" + struct.pack("<I", riff_size) + b"WAVE"
            + b"fmt " + struct.pack("<I", 16) + fmt + b"data" + struct.pack("<I", data_size))


SMALL_RATE = 100   # samples a second in the test sets: the length is real, the files stay small


def wav_file(seconds, rate=SMALL_RATE):
    """A whole mono 8-bit PCM file: header and `seconds` of silence."""
    return wav_header(seconds, rate) + bytes([128]) * int(seconds * rate)


class LibraryCase(unittest.TestCase):
    BPM = 120.0
    FIRST_BEAT = 0.5
    SECONDS = 400.0   # 800 beats at 120 bpm: twelve phrases of 64 beats

    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.root = Path(tmp.name)
        self.library = self.root / "stems"
        self.library.mkdir()
        self.cache = self.root / "analysis.xml"
        self.keys = {}

    def make_set(self, name, grid=True, folder="Artist/Album"):
        directory = self.library / folder
        directory.mkdir(parents=True, exist_ok=True)
        files = []
        for slot, stem in enumerate(("drums", "bass", "other", "vocals")):
            path = directory / f"{name} - {slot + 1} - {stem}.wav"
            path.write_bytes(wav_file(self.SECONDS))
            files.append(path)
        if grid:
            self.keys[name] = pe.cache_key(files)
        return files

    def write_cache(self):
        sets = "".join(
            f'<SET key="{key}" name="{name}" bpm="{self.BPM}" firstBeat="{self.FIRST_BEAT}" version="2"/>'
            for name, key in self.keys.items())
        self.cache.write_text(f'<?xml version="1.0" encoding="UTF-8"?>\n<ANALYSIS>{sets}</ANALYSIS>\n')

    RULES = "v1"   # the file-safety tests are about files, not content: the bar-counting rules

    def run_tool(self, *args):
        self.write_cache()
        return self.run_tool_keeping_cache(*args)

    def run_tool_keeping_cache(self, *args):
        rules = ["--rules", self.RULES] if self.RULES and "--rules" not in args else []
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            code = pe.main([str(self.library), "--cache", str(self.cache), *rules, *args])
        return code, out.getvalue()

    def events_file(self, name, folder="Artist/Album"):
        return self.library / folder / f"{name} - 1 - drums.a3events.json"


class TheRules(LibraryCase):
    def prepared(self, name="Song"):
        files = self.make_set(name)
        return pe.prepare_set(files[0], pe.Grid(self.BPM, self.FIRST_BEAT), 800.0, pe.RULES_V1)

    def test_NeverMoreThanOneEventPerStemPerTwoBeats(self):
        events = self.prepared()
        self.assertGreater(len(events), 10)
        for stem in range(4):
            beats = sorted(e["beat"] for e in events if e["stem"] == stem)
            for earlier, later in zip(beats, beats[1:]):
                self.assertGreaterEqual(later - earlier, 2.0)

    def test_TheFloorStemNeverMoves(self):
        for e in self.prepared():
            self.assertNotEqual(0, e["stem"], e)

    def test_EveryOtherStemMoves(self):
        self.assertEqual({1, 2, 3}, {e["stem"] for e in self.prepared()})

    def test_RerunIsIdentical(self):
        self.make_set("Song")
        self.write_cache()
        argv = [str(self.library), "--cache", str(self.cache), "--rules", "v1"]
        with contextlib.redirect_stdout(io.StringIO()):
            pe.main(argv)
            first = self.events_file("Song").read_bytes()
            pe.main(argv)
        self.assertEqual(first, self.events_file("Song").read_bytes())
        self.assertEqual(self.prepared("Other"), self.prepared("Other"))

    def test_EveryEventIsPreparedAndInsideTheTrack(self):
        for e in self.prepared():
            self.assertEqual("prepared", e["origin"])
            self.assertTrue(e["active"])
            self.assertLessEqual(e["beat"] + 4 * e["bars"], 800.0)

    def test_SeedsDifferBetweenSetsAndEvents(self):
        a, b = self.prepared("A Song"), self.prepared("B Song")
        self.assertEqual(len(a), len({e["seed"] for e in a}))
        self.assertNotEqual({e["seed"] for e in a}, {e["seed"] for e in b})


FULL, QUIET, SILENT = -15.0, -28.0, -90.0
TRACK_BARS = 48


def per_bar(*parts):
    """[(value, bars), ...] -> a per-bar list."""
    result = []
    for value, count in parts:
        result.extend([value] * count)
    return result


def make_bars(drums, bass, other, vocal, flux=None):
    levels = [drums, bass, other, vocal]
    bright = [[value - 20.0 for value in stem] for stem in levels]
    return ps.Bars(levels, bright, flux or [1.0] * len(drums))


def steady(value=FULL, count=TRACK_BARS):
    return per_bar((value, count))


class TheRulesV2(unittest.TestCase):
    LENGTH = TRACK_BARS * 4.0

    BPM = 120.0   # the cruise is 4 bars (8 s a lap), a build's lap 2 bars

    def plan(self, passages, bars):
        return pe.events_from_passages(passages, bars, self.LENGTH, self.BPM)

    def full_mix(self):
        return make_bars(steady(), steady(), steady(), steady())

    def test_a_vocal_solo_orbits_the_vocal_lap_after_lap_from_its_start(self):
        events = self.plan([ps.Passage("solo", 8, 16, 3)], self.full_mix())
        self.assertEqual([("orbit", 3, 32.0, 4, False), ("orbit", 3, 48.0, 4, True)],
                         [(e["shape"], e["stem"], e["beat"], e["bars"], e["reversed"]) for e in events])

    def test_a_long_solo_is_consecutive_laps_turning_each_time(self):
        events = self.plan([ps.Passage("solo", 8, 24, 2)], self.full_mix())
        self.assertEqual([("orbit", 2, 32.0, 4, False), ("orbit", 2, 48.0, 4, True),
                          ("orbit", 2, 64.0, 4, False), ("orbit", 2, 80.0, 4, True)],
                         [(e["shape"], e["stem"], e["beat"], e["bars"], e["reversed"]) for e in events])

    def test_a_solo_of_the_floor_stem_moves_nothing(self):
        self.assertEqual([], self.plan([ps.Passage("solo", 8, 16, 0)], self.full_mix()))

    def test_a_solo_of_the_tops_orbits_the_tops(self):
        events = self.plan([ps.Passage("solo", 8, 16, 1)], self.full_mix())
        self.assertEqual([("orbit", 1, 32.0, 4), ("orbit", 1, 48.0, 4)],
                         [(e["shape"], e["stem"], e["beat"], e["bars"]) for e in events])

    def test_a_break_orbits_the_strongest_mover_at_the_cruise(self):
        bars = make_bars(steady(), steady(FULL - 6), steady(FULL), steady(FULL - 6))
        events = self.plan([ps.Passage("break", 8, 24, None)], bars)
        self.assertEqual([("orbit", 2, beat, 4) for beat in (32.0, 48.0, 64.0, 80.0)],
                         [(e["shape"], e["stem"], e["beat"], e["bars"]) for e in events])

    def test_a_break_can_orbit_the_tops(self):
        bars = make_bars(steady(), steady(FULL), steady(FULL - 6), steady(FULL - 6))
        events = self.plan([ps.Passage("break", 8, 24, None)], bars)
        self.assertEqual({1}, {e["stem"] for e in events})
        self.assertEqual(32.0, events[0]["beat"])

    def test_a_break_where_the_movers_are_silent_moves_nothing(self):
        bars = make_bars(steady(), steady(SILENT), steady(SILENT), steady(SILENT))
        self.assertEqual([], self.plan([ps.Passage("break", 8, 24, None)], bars))

    def test_a_build_rises_the_rising_upper_stem_and_ends_with_it(self):
        other = per_bar((QUIET, 8)) + [QUIET + 2.0 * i for i in range(1, 7)] + per_bar((FULL, 34))
        bars = make_bars(steady(), steady(), other, steady(QUIET))
        events = self.plan([ps.Passage("build", 8, 14, 2)], bars)
        self.assertEqual([("rise", 2, 40.0, 4)], [(e["shape"], e["stem"], e["beat"], e["bars"])
                                                  for e in events if e["shape"] == "rise"])

    def test_a_build_inside_a_solo_of_the_same_stem_still_rises(self):
        other = per_bar((QUIET, 8)) + [QUIET + 2.0 * i for i in range(1, 9)] + per_bar((FULL, 32))
        bars = make_bars(steady(), steady(), other, steady(QUIET))
        events = self.plan([ps.Passage("solo", 9, 24, 2), ps.Passage("build", 9, 16, 2)], bars)
        self.assertIn(("rise", 2, 48.0, 4), [(e["shape"], e["stem"], e["beat"], e["bars"]) for e in events])

    def test_a_build_of_the_drums_alone_moves_nothing(self):
        drums = per_bar((QUIET, 8)) + [QUIET + 3.0 * i for i in range(1, 9)] + per_bar((FULL, 32))
        bars = make_bars(drums, steady(), steady(QUIET), steady(QUIET))
        self.assertEqual([], self.plan([ps.Passage("build", 8, 16, 0)], bars))

    def test_a_fill_pings_the_loudest_mover_on_beat_three(self):
        bars = make_bars(steady(), steady(FULL - 9), steady(FULL - 6), steady(FULL))
        events = self.plan([ps.Passage("fill", 15, 16, None)], bars)
        self.assertEqual([("ping", 3, 62.0, 1)], [(e["shape"], e["stem"], e["beat"], e["bars"]) for e in events])

    def test_a_fill_can_ping_the_tops(self):
        bars = make_bars(steady(), steady(FULL), steady(FULL - 6), steady(FULL - 9))
        events = self.plan([ps.Passage("fill", 15, 16, None)], bars)
        self.assertEqual([("ping", 1, 62.0)], [(e["shape"], e["stem"], e["beat"]) for e in events])

    def test_a_fill_pings_the_next_loudest_mover_when_the_loudest_is_busy(self):
        bars = make_bars(steady(), steady(FULL - 9), steady(FULL - 6), steady(FULL))
        events = self.plan([ps.Passage("solo", 12, 20, 3), ps.Passage("fill", 15, 16, None)], bars)
        self.assertIn(("ping", 2, 62.0), [(e["shape"], e["stem"], e["beat"]) for e in events])

    def test_the_return_puts_nothing_on_the_one_and_the_laps_end_before_it(self):
        events = self.plan([ps.Passage("break", 8, 24, None), ps.Passage("return", 14, 15, 0)], self.full_mix())
        self.assertEqual([("orbit", 1, 32.0, 4), ("orbit", 1, 48.0, 2)],    # 4 then x2 faster into the return
                         [(e["shape"], e["stem"], e["beat"], e["bars"]) for e in events])
        self.assertNotIn(56.0, [e["beat"] for e in events])

    def test_direction_alternates_lap_by_lap(self):
        events = self.plan([ps.Passage("solo", 8, 16, 3), ps.Passage("solo", 24, 32, 2),
                            ps.Passage("solo", 36, 44, 3)], self.full_mix())
        self.assertEqual([False, True] * 3, [e["reversed"] for e in events])

    def test_only_one_orbit_runs_at_a_time(self):
        events = self.plan([ps.Passage("break", 8, 24, None), ps.Passage("solo", 10, 18, 3)], self.full_mix())
        orbits = [e for e in events if e["shape"] == "orbit"]
        self.assertIn((3, 40.0), [(e["stem"], e["beat"]) for e in orbits])   # the solo wins its place
        for i, a in enumerate(orbits):
            for b in orbits[:i]:
                self.assertFalse(pe._overlap(a, b), (a, b))

    def test_a_build_laps_twice_as_fast_as_the_break_before_it(self):
        other = per_bar((QUIET, 16)) + [QUIET + 2.0 * i for i in range(1, 9)] + per_bar((FULL, 24))
        bars = make_bars(steady(), steady(FULL), other, steady(QUIET))
        events = self.plan([ps.Passage("break", 8, 24, None), ps.Passage("build", 16, 24, 2)], bars)
        orbits = [(e["stem"], e["beat"], e["bars"]) for e in events if e["shape"] == "orbit"]
        self.assertEqual([(1, 32.0, 4), (1, 48.0, 4), (1, 64.0, 2), (1, 72.0, 2), (1, 80.0, 2), (1, 88.0, 2)], orbits)
        self.assertEqual([False, True] * 3, [e["reversed"] for e in events if e["shape"] == "orbit"])

    def test_nothing_outside_the_track(self):
        events = self.plan([ps.Passage("solo", 44, 52, 3), ps.Passage("fill", 47, 48, None)], self.full_mix())
        for e in events:
            self.assertGreaterEqual(e["beat"], 0.0)
            self.assertLessEqual(e["beat"] + 4 * e["bars"], self.LENGTH)

    def test_events_are_valid_file_events_once_seeded(self):
        events = self.plan([ps.Passage("solo", 8, 24, 3), ps.Passage("fill", 31, 32, None),
                            ps.Passage("build", 34, 40, 2)], self.full_mix())
        for index, event in enumerate(events):
            event["seed"] = "1"
            self.assertTrue(check_event(event), event)


class TheFloorStemHighsOnly(unittest.TestCase):
    """The one exception: in a break, a floor stem playing only hats may orbit."""
    LENGTH = TRACK_BARS * 4.0

    def bars(self, floor_bars="hhhh"):
        """Floor full, then `floor_bars` from bar 8 ("h" hats, "k" kick), then full."""
        pattern = "k" * 8 + floor_bars + "k" * (TRACK_BARS - 8 - len(floor_bars))
        bars = make_bars(steady(), steady(FULL - 6), steady(FULL - 6), steady(FULL - 6))
        bars.level[0] = [-30.0 if c == "h" else FULL for c in pattern]
        bars.bright[0] = [-32.0 if c == "h" else FULL - 20 for c in pattern]
        return bars

    def plan(self, found, bars):
        return pe.events_from_passages(found, bars, self.LENGTH, 120.0)

    def test_a_highs_only_stretch_orbits_the_floor_stem(self):
        events = self.plan([ps.Passage("highs", 8, 12, 0)], self.bars())
        self.assertEqual([("orbit", 0, 32.0, 4)], [(e["shape"], e["stem"], e["beat"], e["bars"]) for e in events])

    def test_a_full_band_floor_stem_never_moves(self):
        self.assertEqual([], self.plan([ps.Passage("highs", 8, 12, 0)], self.bars("kkkk")))

    def test_never_over_a_kick_bar(self):
        self.assertEqual([], self.plan([ps.Passage("highs", 8, 12, 0)], self.bars("hhkh")))

    def test_found_in_the_content_end_to_end(self):
        events = pe.RULES_V2(self.LENGTH, self.bars("hhhhhh"), 120.0)
        self.assertIn(("orbit", 0, 32.0), [(e["shape"], e["stem"], e["beat"]) for e in events])

    def test_three_bars_of_highs_move_nothing_on_the_floor(self):
        events = pe.RULES_V2(self.LENGTH, self.bars("hhh"), 120.0)
        self.assertFalse([e for e in events if e["stem"] == 0])

    def test_never_a_ping_or_rise_on_the_floor_stem(self):
        bars = self.bars("hhhhhhhh")
        bars.level[0][15] = -10.0                    # the floor the loudest stem in a fill bar
        events = self.plan([ps.Passage("fill", 15, 16, None), ps.Passage("build", 9, 16, 0)], bars)
        self.assertFalse([e for e in events if e["stem"] == 0 and e["shape"] != "orbit"])

    def test_explained_by_name(self):
        self.assertIn("floor stem highs only", pe.explanation([ps.Passage("highs", 8, 12, 0)], [])[0])


class TheLaps(unittest.TestCase):
    """bars is the length of an orbit and its speed: one lap in `bars` bars."""

    def test_the_cruise_lap_follows_the_tempo(self):
        table = {60: 2, 80: 4, 98: 4, 120: 4, 128: 4, 140: 4, 170: 8, 174: 8}
        self.assertEqual(table, {bpm: pe.cruise_lap(bpm) for bpm in table})

    def test_a_build_laps_twice_as_fast_never_under_two_bars(self):
        self.assertEqual(2, pe.energetic_lap(120))
        self.assertEqual(4, pe.energetic_lap(170))
        self.assertEqual(2, pe.energetic_lap(60))

    def test_the_floor_highs_lap_takes_the_slow_end(self):
        for bpm in range(60, 181, 2):
            self.assertGreaterEqual(pe.slow_lap(bpm), pe.cruise_lap(bpm), bpm)
        self.assertEqual(8, pe.slow_lap(128))

    def test_a_passage_becomes_consecutive_laps_halving_into_its_end(self):
        self.assertEqual([4, 4, 4, 4], pe.lap_plan(16, 4))
        self.assertEqual([8, 4, 2], pe.lap_plan(14, 8))
        self.assertEqual([4, 2], pe.lap_plan(6, 4))
        self.assertEqual([2], pe.lap_plan(3, 4))
        self.assertEqual([], pe.lap_plan(1, 4))

    def test_laps_never_outlast_the_passage_and_change_only_by_two(self):
        for length in range(0, 40):
            for lap in (2, 4, 8):
                plan = pe.lap_plan(length, lap)
                self.assertLessEqual(sum(plan), length)
                self.assertTrue(all(bars in (2, 4, 8) for bars in plan), plan)
                for earlier, later in zip(plan, plan[1:]):
                    self.assertIn(earlier / later, (1, 2), plan)

    def test_seconds_per_lap_are_shown(self):
        orbit = {"stem": 2, "beat": 128.0, "shape": "orbit", "bars": 4, "reversed": False}
        self.assertEqual(["  -> orbit stem 2 beat 128 (bar 33) 4 bars = 9.8 s/lap"], pe.explanation(None, [orbit], 98.0))


class TheRulesV2OnContent(unittest.TestCase):
    """The hard rules, over every synthetic track built here, detection included."""

    def tracks(self):
        drop = per_bar((FULL, 8), (SILENT, 8), (FULL, 8), (SILENT, 16), (FULL, 8))
        rising = per_bar((QUIET, 24)) + [QUIET + 2.0 * i for i in range(1, 9)] + per_bar((FULL, 16))
        fills = [1.0] * TRACK_BARS
        for bar in (7, 23, 31, 47):
            fills[bar] = 4.0
        yield "steady", make_bars(steady(), steady(), steady(), steady())
        yield "vocal over breaks", make_bars(drop, drop, drop, steady())
        yield "both over breaks", make_bars(drop, drop, steady(), steady(FULL - 3))
        yield "silent upper", make_bars(drop, drop, steady(SILENT), steady(SILENT))
        yield "build", make_bars(drop, drop, rising, steady(QUIET))
        yield "fills", make_bars(steady(), steady(), steady(), steady(FULL - 3), flux=fills)
        yield "everything", make_bars(drop, drop, rising, steady(), flux=fills)

    def each(self):
        for name, bars in self.tracks():
            yield name, bars, pe.RULES_V2(TRACK_BARS * 4.0, bars, 120.0)

    def test_the_floor_stem_never_moves(self):
        for name, _, events in self.each():
            self.assertFalse([e for e in events if e["stem"] == 0], name)

    def test_only_audible_stems_move(self):
        for name, bars, events in self.each():
            for e in events:
                start = int(e["beat"] // 4)
                covered = range(start, min(bars.count, start + max(1, int(e["bars"]))))
                heard = sum(1 for b in covered if ps.audible(bars, e["stem"], b))
                self.assertGreaterEqual(2 * heard, len(covered), (name, e))

    def test_two_beats_at_least_between_events_on_a_stem(self):
        for name, _, events in self.each():
            for stem in range(4):
                beats = sorted(e["beat"] for e in events if e["stem"] == stem)
                for earlier, later in zip(beats, beats[1:]):
                    self.assertGreaterEqual(later - earlier, 2.0, name)

    def test_all_inside_the_track(self):
        for name, _, events in self.each():
            for e in events:
                self.assertGreaterEqual(e["beat"], 0.0, name)
                self.assertLessEqual(e["beat"] + 4 * e["bars"], TRACK_BARS * 4.0, name)

    def test_a_track_without_passages_gets_no_events(self):
        _, bars = next(self.tracks())
        self.assertEqual([], ps.find_passages(bars))
        self.assertEqual([], pe.RULES_V2(TRACK_BARS * 4.0, bars, 120.0))

    def test_the_content_tracks_do_get_events(self):
        for name, _, events in self.each():
            if name not in ("steady", "silent upper"):
                self.assertTrue(events, name)

    def test_every_orbit_is_a_lap_of_two_four_or_eight_bars(self):
        for name, _, events in self.each():
            for e in events:
                if e["shape"] == "orbit":
                    self.assertIn(e["bars"], (2, 4, 8), (name, e))

    def test_the_same_content_gives_the_same_events(self):
        for (name, _, first), (_, _, second) in zip(self.each(), self.each()):
            self.assertEqual(first, second, name)

    def test_the_rules_never_place_a_quarter_bar(self):
        # The DJ can play a quarter bar (since 2026-11-10); the rules keep to what they chose before.
        for name, _, events in self.each():
            for e in events:
                self.assertGreaterEqual(e["bars"], 0.5, (name, e))


V3_BARS = 64


def full(count=V3_BARS, value=FULL):
    return per_bar((value, count))


def summary(events, *fields):
    return [tuple(e[f] for f in fields) for e in events]


def orbits_at(events, beat):
    return sum(1 for e in events if e["shape"] == "orbit" and pe._span(e)[0] <= beat < pe._span(e)[1])


class TheRulesV3(unittest.TestCase):
    """Roles per slot: tops accent, music orbits slowly, vocals rise or orbit
    when they come in; breaks move two or three stems together."""
    LENGTH = V3_BARS * 4.0
    BPM = 120.0   # cruise 4 bars, slow lap 8 bars, a build's lap 2 bars

    def plan(self, found, bars):
        return pe.events_from_roles(found, bars, self.LENGTH, self.BPM)

    def groove(self, start=0, end=V3_BARS):
        return ps.Passage("groove", start, end, None)

    def full_mix(self):
        return make_bars(full(), full(), full(), full())

    def test_music_orbits_slowly_under_the_groove_lap_after_lap(self):
        events = self.plan([self.groove()], self.full_mix())
        self.assertEqual([(32.0, 8), (64.0, 8), (96.0, 8), (128.0, 8), (160.0, 8), (192.0, 8), (224.0, 8)],
                         summary([e for e in events if e["stem"] == 2], "beat", "bars"))
        self.assertEqual({"orbit"}, {e["shape"] for e in events if e["stem"] == 2})

    def test_music_does_not_orbit_where_it_is_not_heard(self):
        music = per_bar((FULL, 16), (SILENT, 24), (FULL, 24))
        events = self.plan([self.groove()], make_bars(full(), full(), music, full()))
        self.assertEqual([32.0, 160.0, 192.0, 224.0], [e["beat"] for e in events if e["stem"] == 2])

    def test_the_tops_accent_the_turn_of_every_fourth_phrase_with_a_one_bar_scat(self):
        # his edits deleted 148 of 181 phrase-end scats (11 of 14 sets mostly)
        self.assertEqual(4, pe.TOPS_TURN_EVERY_PHRASES)
        events = self.plan([self.groove()], self.full_mix())
        self.assertEqual([(124.0, 1), (252.0, 1)],
                         summary([e for e in events if e["stem"] == 1 and e["shape"] == "scat"], "beat", "bars"))

    def test_phrases_count_from_the_grid_s_one_even_after_an_odd_return(self):
        events = self.plan([self.groove(), ps.Passage("return", 17, 18, 0)], self.full_mix())
        self.assertEqual([124.0, 252.0], [e["beat"] for e in events if e["stem"] == 1 and e["shape"] == "scat"])

    def test_no_cross_mid_phrase(self):
        # his edits deleted 94 of 102 mid-phrase CROSS (7 of 13 sets all of them)
        for tops, vocals in ((FULL, FULL), (SILENT, FULL)):
            events = self.plan([self.groove()], make_bars(full(), full(value=tops), full(), full(value=vocals)))
            self.assertEqual([], [e for e in events if e["shape"] == "ping"], (tops, vocals))

    def test_v3_judges_by_the_bridged_sound_rule(self):
        music = per_bar((FULL, 10), (SILENT, 2), (FULL, 52))
        events = self.plan([self.groove()], make_bars(full(), full(), music, full()))
        self.assertIn((32.0, 8), summary([e for e in events if e["stem"] == 2], "beat", "bars"))
        self.assertEqual(0, pe.overruns(events, make_bars(full(), full(), music, full()), pe.sounds_through_bridged))

    def test_a_break_s_laps_run_on_over_a_short_rest(self):
        floor = per_bar((FULL, 8), (SILENT, 16), (FULL, 40))
        music = per_bar((FULL, 12), (SILENT, 2), (FULL, 50))
        events = self.plan([ps.Passage("break", 8, 24, None)], make_bars(floor, full(value=FULL - 6), music,
                                                                        full(value=FULL - 6)))
        self.assertEqual([(32.0, 8), (64.0, 8)],
                         summary([e for e in events if e["stem"] == 2 and e["shape"] == "orbit"], "beat", "bars"))

    def test_a_dropout_is_a_tops_accent_and_a_vocal_ping(self):
        events = self.plan([self.groove(), ps.Passage("dropout", 21, 22, None)], self.full_mix())
        self.assertIn((1, "scat", 84.0, 1), summary(events, "stem", "shape", "beat", "bars"))
        self.assertIn((3, "ping", 86.0, 1), summary(events, "stem", "shape", "beat", "bars"))

    def test_pings_are_few(self):
        dropouts = [ps.Passage("dropout", bar, bar + 1, None) for bar in (5, 13, 21, 29, 37, 45, 53)]
        events = self.plan([self.groove()] + dropouts, self.full_mix())
        pings = sorted(e["beat"] for e in events if e["shape"] == "ping")
        self.assertTrue(pings)
        for earlier, later in zip(pings, pings[1:]):
            self.assertGreaterEqual(later - earlier, 4 * pe.PING_SPACING_BARS)

    def test_a_vocal_coming_in_over_the_groove_rises_for_four_bars(self):
        vocal = per_bar((SILENT, 16), (FULL, 48))
        events = self.plan([self.groove(), ps.Passage("entry", 16, 64, 3)], make_bars(full(), full(), full(), vocal))
        self.assertIn((3, "rise", 64.0, 4), summary(events, "stem", "shape", "beat", "bars"))

    def test_a_vocal_coming_in_in_a_break_orbits_at_the_cruise(self):
        floor = per_bar((FULL, 8), (SILENT, 16), (FULL, 40))
        vocal = per_bar((SILENT, 12), (FULL - 6, 52))
        found = [ps.Passage("break", 8, 24, None), ps.Passage("entry", 12, 64, 3)]
        events = self.plan(found, make_bars(floor, full(), full(), vocal))
        self.assertIn((3, "orbit", 48.0, 4), summary(events, "stem", "shape", "beat", "bars"))

    def test_a_short_vocal_shot_pings_on_beat_three_instead_of_rising(self):
        vocal = per_bar((SILENT, 16), (FULL, 2), (SILENT, 46))
        events = self.plan([self.groove(), ps.Passage("entry", 16, 18, 3)], make_bars(full(), full(), full(), vocal))
        self.assertEqual([("ping", 66.0, 1)], summary([e for e in events if e["stem"] == 3], "shape", "beat", "bars"))

    def test_a_turn_without_a_vocal_pings_the_tops_in_place_of_their_scat(self):
        found = [self.groove(), ps.Passage("dropout", 13, 14, None)]
        events = self.plan(found, make_bars(full(), full(), full(), full(value=SILENT)))
        self.assertIn((1, "ping", 54.0), summary(events, "stem", "shape", "beat"))
        self.assertNotIn((1, "scat", 52.0), summary(events, "stem", "shape", "beat"))

    def test_a_shot_too_close_to_a_ping_scats(self):
        vocal = per_bar((SILENT, 16), (FULL, 2), (SILENT, 2), (FULL, 2), (SILENT, 42))
        found = [self.groove(), ps.Passage("entry", 16, 18, 3), ps.Passage("entry", 20, 22, 3)]
        events = self.plan(found, make_bars(full(), full(), full(), vocal))
        self.assertEqual([("ping", 66.0, 1), ("scat", 80.0, 1)],
                         summary([e for e in events if e["stem"] == 3], "shape", "beat", "bars"))

    def test_a_vocal_coming_in_where_the_floor_drops_out_orbits(self):
        vocal = per_bar((SILENT, 20), (FULL, 44))
        found = [self.groove(), ps.Passage("dropout", 20, 22, None), ps.Passage("entry", 20, 64, 3)]
        events = self.plan(found, make_bars(full(), full(), full(), vocal))
        self.assertIn((3, "orbit", 80.0, 4), summary(events, "stem", "shape", "beat", "bars"))

    def test_a_vocal_scats_now_and_then_while_it_sings(self):
        vocal = per_bar((SILENT, 8), (FULL, 56))
        events = self.plan([self.groove(), ps.Passage("entry", 8, 64, 3)], make_bars(full(), full(), full(), vocal))
        self.assertEqual([(32.0, "rise", 4)] + [(beat, "scat", 1) for beat in range(64, 256, 32)],
                         summary([e for e in events if e["stem"] == 3], "beat", "shape", "bars"))

    def test_a_break_moves_two_or_three_stems_together_in_different_shapes(self):
        floor = per_bar((FULL, 8), (SILENT, 16), (FULL, 40))
        bars = make_bars(floor, full(value=FULL - 3), full(), full(value=FULL - 6))
        events = self.plan([ps.Passage("break", 8, 24, None)], bars)
        at_start = [e for e in events if e["beat"] == 32.0]
        self.assertGreaterEqual(len(at_start), 2)
        self.assertEqual(len(at_start), len({e["shape"] for e in at_start}))
        self.assertIn((2, "orbit", 8), summary(at_start, "stem", "shape", "bars"))   # the loudest, music: the slow lap

    def test_in_a_break_only_the_vocals_scatter(self):
        # his edits deleted 55 of 74 break scats on the tops and the music
        floor = per_bar((FULL, 8), (SILENT, 16), (FULL, 40))
        bars = make_bars(floor, full(value=FULL - 3), full(), full(value=FULL - 6))
        events = self.plan([ps.Passage("break", 8, 24, None)], bars)
        scats = [e for e in events if e["shape"] == "scat" and 32 <= e["beat"] < 96]
        self.assertTrue(scats)
        self.assertEqual({3}, {e["stem"] for e in scats})

    def test_three_stems_may_orbit_at_once(self):
        floor = per_bar((FULL, 8), (-30.0, 16), (FULL, 40))
        bars = make_bars(floor, full(), full(), full())
        bars.bright[0] = [-32.0 if 8 <= b < 24 else FULL - 20 for b in range(V3_BARS)]
        found = [ps.Passage("break", 8, 24, None), ps.Passage("highs", 8, 24, 0), ps.Passage("entry", 8, 24, 3)]
        events = self.plan(found, bars)
        self.assertEqual(3, pe.MAX_ORBITS_AT_ONCE)
        self.assertEqual(3, max(orbits_at(events, beat) for beat in range(int(self.LENGTH))))

    def test_never_more_than_three_stems_orbit_at_once(self):
        floor = per_bar((FULL, 8), (-30.0, 16), (FULL, 40))
        bars = make_bars(floor, full(), full(), full())
        bars.bright[0] = [-32.0 if 8 <= b < 24 else FULL - 20 for b in range(V3_BARS)]
        found = [ps.Passage("break", 8, 24, None), ps.Passage("highs", 8, 24, 0), ps.Passage("entry", 8, 24, 3),
                 ps.Passage("solo", 8, 24, 1)]
        events = self.plan(found, bars)
        self.assertTrue([e for e in events if e["stem"] == 0], "the floor's hats still orbit")
        for beat in range(0, int(self.LENGTH)):
            self.assertLessEqual(orbits_at(events, beat), pe.MAX_ORBITS_AT_ONCE, beat)

    def test_nothing_starts_on_the_one_of_a_return(self):
        floor = per_bar((FULL, 8), (SILENT, 8), (FULL, 48))
        vocal = per_bar((SILENT, 16), (FULL, 48))
        found = [ps.Passage("break", 8, 16, None), ps.Passage("return", 16, 17, 0), self.groove(16, 64),
                 ps.Passage("dropout", 15, 16, None), ps.Passage("entry", 16, 64, 3)]
        events = self.plan(found, make_bars(floor, full(), full(), vocal))
        self.assertNotIn(64.0, [e["beat"] for e in events])

    def test_a_build_rises_the_rising_mover_as_in_v2(self):
        other = per_bar((QUIET, 8)) + [QUIET + 2.0 * i for i in range(1, 7)] + per_bar((FULL, 50))
        bars = make_bars(full(), full(), other, full(value=QUIET))
        events = self.plan([ps.Passage("build", 8, 14, 2)], bars)
        self.assertIn(("rise", 2, 40.0, 4), summary(events, "shape", "stem", "beat", "bars"))

    def test_most_orbits_turn_forward_the_second_of_every_four_turns_back(self):
        events = self.plan([self.groove()], self.full_mix())
        self.assertEqual([False, True, False, False, False, True, False],
                         [e["reversed"] for e in events if e["shape"] == "orbit"])

    def test_the_density_stays_under_the_cap(self):
        dropouts = [ps.Passage("dropout", bar, bar + 1, None) for bar in range(3, 64, 4)]
        vocal = per_bar((SILENT, 4), (FULL, 60))
        found = [self.groove(), ps.Passage("entry", 4, 64, 3)] + dropouts
        events = self.plan(found, make_bars(full(), full(), full(), vocal))
        window = pe.DENSITY_WINDOW_SECONDS * self.BPM / 60.0
        beats = sorted(e["beat"] for e in events)
        for index, beat in enumerate(beats):
            inside = [b for b in beats[index:] if b < beat + window]
            self.assertLessEqual(len(inside), pe.DENSITY_MAX_PER_WINDOW, beat)


def with_beats(bars, silent_beats=None):
    """Per-beat levels from the per-bar ones; `silent_beats`: {stem: beats}."""
    bars.beat_level = [[value for value in stem for _ in range(4)] for stem in bars.level]
    for stem, beats in (silent_beats or {}).items():
        for beat in beats:
            bars.beat_level[stem][beat] = SILENT
    return bars


class TheDrumGapsV3(unittest.TestCase):
    """A pause in the drums of 1-4 beats: the tops accent it, the music keeps
    (or starts) its slow orbit, the vocals answer a beat later."""
    LENGTH = V3_BARS * 4.0
    BPM = 120.0

    def plan(self, gaps, bars=None, found=None):
        bars = bars or with_beats(make_bars(full(), full(), full(), full()))
        found = found if found is not None else [ps.Passage("groove", 0, V3_BARS, None)]
        return pe.events_from_roles(found, bars, self.LENGTH, self.BPM, [ps.Gap(*g) for g in gaps])

    def at(self, events, stem, start, end):
        return [e for e in events if e["stem"] == stem and start <= e["beat"] < end]

    def test_the_tops_accent_a_short_gap_from_its_first_beat_half_a_bar(self):
        events = self.at(self.plan([(41, 43)]), 1, 41, 42)
        self.assertEqual([(41.0, 0.5)], summary(events, "beat", "bars"))
        self.assertIn(events[0]["shape"], ("scat", "ping"))

    def test_a_gap_of_three_or_four_beats_gets_a_one_bar_accent(self):
        for gap in ((41, 44), (41, 45)):
            self.assertEqual([(41.0, 1)], summary(self.at(self.plan([gap]), 1, 41, 42), "beat", "bars"), gap)

    def test_the_vocals_answer_on_the_gap_s_second_beat(self):
        events = self.at(self.plan([(41, 43)]), 3, 41, 44)
        self.assertEqual([(42.0, 0.5)], summary(events, "beat", "bars"))
        self.assertIn(events[0]["shape"], ("scat", "ping"))

    def test_no_event_on_a_stem_that_is_silent_in_the_gap(self):
        bars = with_beats(make_bars(full(), full(), full(), full()), {1: range(40, 44), 3: range(40, 44)})
        events = self.plan([(41, 43)], bars)
        self.assertEqual([], self.at(events, 1, 40, 44) + self.at(events, 3, 40, 44))

    def test_the_music_orbit_runs_on_across_a_gap(self):
        without = self.plan([])
        events = self.plan([(41, 43)])
        music = [e for e in events if e["stem"] == 2]
        self.assertEqual([e for e in without if e["stem"] == 2], music)
        self.assertIn((32.0, "orbit", 8), summary(music, "beat", "shape", "bars"))   # the lap over beat 41

    def test_the_music_starts_a_slow_orbit_in_a_gap_where_none_runs(self):
        events = self.plan([(73, 75)], found=[])              # no groove: no orbit of the music yet
        self.assertIn((2, "orbit", 73.0, 8), summary(events, "stem", "shape", "beat", "bars"))

    def test_one_gap_group_in_four_bars(self):
        events = self.plan([(41, 43), (49, 51), (61, 63)])
        self.assertTrue(self.at(events, 1, 41, 42))
        self.assertEqual([], self.at(events, 1, 49, 50) + self.at(events, 3, 49, 51))
        self.assertTrue(self.at(events, 1, 61, 62))

    def test_a_drum_fill_of_gaps_does_not_machine_gun(self):
        gaps = [(beat, beat + 1) for beat in range(161, 192, 2)]
        events = self.plan(gaps)
        for stem in (1, 3):
            beats = sorted(e["beat"] for e in events if e["stem"] == stem)
            for earlier, later in zip(beats, beats[1:]):
                self.assertGreaterEqual(later - earlier, pe.MIN_GAP_BEATS)
        groups = sorted({g for g in gaps if self.at(events, 1, g[0], g[1]) or self.at(events, 3, g[0], g[0] + 2)})
        for earlier, later in zip(groups, groups[1:]):
            self.assertGreaterEqual(later[0] - earlier[0], 4 * pe.GAP_GROUP_EVERY_BARS)

    def test_the_floor_never_moves_in_a_gap(self):
        self.assertEqual([], [e for e in self.plan([(41, 43), (101, 105)]) if e["stem"] == 0])

    def test_gaps_go_before_the_phrase_end_scats(self):
        events = self.plan([(61, 63)])                        # bar 16, where the tops' phrase scat sits
        self.assertIn((1, 61.0, 0.5), summary(events, "stem", "beat", "bars"))
        self.assertNotIn((1, 60.0), summary(events, "stem", "beat"))


class TheLongHatsV3(unittest.TestCase):
    """Hats alone for 8 bars or more are carried through by slow laps back
    and forth, from lap to lap without a gap, the last ending with them."""
    LENGTH = V3_BARS * 4.0

    def floor_hats(self, start, end, hats=True):
        """The floor plays hats alone (or, hats=False, full band) in [start, end)."""
        bars = make_bars(full(), full(value=FULL - 6), full(value=FULL - 6), full(value=FULL - 6))
        inside_bright = -32.0 if hats else -55.0
        bars.level[0] = [-30.0 if start <= b < end else FULL for b in range(V3_BARS)]
        bars.bright[0] = [inside_bright if start <= b < end else FULL - 20 for b in range(V3_BARS)]
        return bars

    def laps(self, events, stem):
        return summary([e for e in events if e["stem"] == stem and e["shape"] == "orbit"], "beat", "bars", "reversed")

    def test_a_long_floor_highs_passage_is_covered_by_slow_laps_back_and_forth(self):
        events = pe.events_from_roles([ps.Passage("highs", 8, 20, 0)], self.floor_hats(8, 20), self.LENGTH, 120.0)
        self.assertEqual([(32.0, 8, False), (64.0, 4, True)], self.laps(events, 0))

    def test_the_last_lap_stops_where_the_passage_ends(self):
        events = pe.events_from_roles([ps.Passage("highs", 8, 23, 0)], self.floor_hats(8, 23), self.LENGTH, 120.0)
        laps = self.laps(events, 0)
        self.assertEqual([(36.0, 8, False), (68.0, 4, True), (84.0, 2, False)], laps)
        self.assertEqual(23 * 4.0, laps[-1][0] + 4 * laps[-1][1])

    def test_a_full_band_floor_never_moves_even_in_a_long_highs_passage(self):
        bars = self.floor_hats(8, 23, hats=False)
        events = pe.events_from_roles([ps.Passage("highs", 8, 23, 0)], bars, self.LENGTH, 120.0)
        self.assertEqual([], [e for e in events if e["stem"] == 0])

    def test_a_short_floor_highs_passage_stays_as_before(self):
        events = pe.events_from_roles([ps.Passage("highs", 8, 15, 0)], self.floor_hats(8, 15), self.LENGTH, 120.0)
        self.assertEqual([32.0, 48.0], [beat for beat, _, _ in self.laps(events, 0)])

    def test_hats_in_the_tops_lap_the_tops_back_and_forth_the_floor_stays(self):
        floor = per_bar((SILENT, 15), (FULL, V3_BARS - 15))
        bars = make_bars(floor, full(value=-31.0), full(value=-22.0), full(value=-38.0))
        found = ps.find_all_passages(bars)
        self.assertIn(ps.Passage("hats", 0, 15, 1), found)
        events = pe.events_from_roles(found, bars, self.LENGTH, 138.0)
        self.assertEqual([(4.0, 8, False), (36.0, 4, True), (52.0, 2, False)], self.laps(events, 1))
        self.assertEqual([], [e for e in events if e["stem"] == 0])

    def test_one_other_orbit_at_most_beside_the_laps(self):
        floor = per_bar((SILENT, 15), (FULL, V3_BARS - 15))
        bars = make_bars(floor, full(value=-31.0), full(value=-22.0), full(value=-24.0))
        events = pe.events_from_roles(ps.find_all_passages(bars), bars, self.LENGTH, 138.0)
        for beat in range(0, 60):
            self.assertLessEqual(orbits_at(events, beat), pe.MAX_ORBITS_AT_ONCE, beat)


def music_for(*parts):
    """The music stem per bar from [(value, bars)], the rest full."""
    return make_bars(full(), full(), per_bar(*parts), full())


def ev(stem, beat, shape, bars):
    return pe._event(stem, beat, shape, bars, False)


class TheSoundRule(unittest.TestCase):
    """An event never outlasts its stem's sound: shortened to an allowed
    length that fits, else dropped; nothing late in the track unless the
    stem sounds to the end."""

    def test_an_orbit_over_three_bars_of_sound_becomes_two_bars(self):
        bars = music_for((FULL, 11), (SILENT, 53))
        self.assertEqual(2, pe.fit_to_sound(ev(2, 32, "orbit", 8), bars)["bars"])

    def test_a_scat_is_shortened_to_the_longest_length_that_fits(self):
        bars = music_for((FULL, 11), (SILENT, 53))
        self.assertEqual(2, pe.fit_to_sound(ev(2, 32, "scat", 4), bars)["bars"])

    def test_an_event_that_fits_is_left_as_it_is(self):
        event = ev(2, 32, "orbit", 8)
        self.assertIs(event, pe.fit_to_sound(event, music_for((FULL, 64))))

    def test_shorter_than_the_shortest_length_is_dropped_not_moved(self):
        bars = with_beats(music_for((FULL, 64)), {2: [b for b in range(256) if b != 32]})
        self.assertIsNone(pe.fit_to_sound(ev(2, 32, "scat", 1), bars))       # one beat of sound

    def test_half_a_bar_of_sound_keeps_a_half_bar_accent(self):
        bars = with_beats(music_for((FULL, 64)), {2: [b for b in range(256) if b not in (32, 33)]})
        self.assertEqual(0.5, pe.fit_to_sound(ev(2, 32, "scat", 1), bars)["bars"])

    def test_an_orbit_never_shrinks_under_a_lap_of_two_bars(self):
        self.assertIsNone(pe.fit_to_sound(ev(2, 40, "orbit", 4), music_for((FULL, 11), (SILENT, 53))))

    def test_a_vocal_tail_cuts_a_rise_short(self):
        bars = make_bars(full(), full(), full(), per_bar((FULL, 20), (SILENT, 44)))
        self.assertEqual(2, pe.fit_to_sound(ev(3, 72, "rise", 4), bars)["bars"])

    def test_the_last_bar_must_sound(self):
        bars = music_for((FULL, 15), (SILENT, 1), (FULL, 48))
        self.assertEqual(4, pe.fit_to_sound(ev(2, 32, "orbit", 8), bars)["bars"])

    def test_a_few_silent_beats_inside_a_long_orbit_are_tolerated(self):
        bars = with_beats(music_for((FULL, 64)), {2: (40, 41, 42)})
        event = ev(2, 32, "orbit", 8)
        self.assertIs(event, pe.fit_to_sound(event, bars))

    def test_nothing_starts_late_unless_the_stem_sounds_to_the_end(self):
        fading = music_for((FULL, 63), (SILENT, 1))
        self.assertIsNone(pe.fit_to_sound(ev(2, 62 * 4, "scat", 0.5), fading))
        self.assertIsNotNone(pe.fit_to_sound(ev(2, 62 * 4, "scat", 0.5), music_for((FULL, 64))))

    def test_overruns_are_counted(self):
        bars = music_for((FULL, 11), (SILENT, 53))
        self.assertEqual(1, pe.overruns([ev(2, 32, "orbit", 8), ev(2, 32, "scat", 2)], bars))

    def test_v3_s_laps_follow_the_stem_not_the_passage(self):
        floor = per_bar((SILENT, 15), (FULL, V3_BARS - 15))
        tops = per_bar((-31.0, 11), (SILENT, V3_BARS - 11))
        bars = make_bars(floor, tops, full(value=-22.0), full(value=-38.0))
        events = pe.events_from_roles([ps.Passage("hats", 0, 15, 1)], bars, V3_BARS * 4.0, 138.0)
        tops_orbits = [e for e in events if e["stem"] == 1 and e["shape"] == "orbit"]
        self.assertTrue(tops_orbits)
        self.assertEqual(44.0, max(pe._span(e)[1] for e in tops_orbits))
        self.assertEqual(0, pe.overruns(events, bars))

    def test_v2_keeps_the_rule_too(self):
        bars = make_bars(steady(), steady(), steady(), per_bar((FULL, 11), (SILENT, 37)))
        events = pe.events_from_passages([ps.Passage("solo", 8, 16, 3)], bars, TRACK_BARS * 4.0, 120.0)
        self.assertEqual([(32.0, 2)], summary(events, "beat", "bars"))


def bridged(event, bars):
    return pe.fit_to_sound(event, bars, pe.sounds_through_bridged)


class TheBridgedSoundRule(unittest.TestCase):
    """v3's sound rule, as the DJ decided (2026-11-10): a rest of up to
    SOUND_BRIDGE_BARS inside a stem's stretch does not end its sound, and the
    ENTRY_LEAD_BEATS before its first sound do not count; real silence after
    the sound still shortens or drops the event."""

    def test_the_constants_are_the_decided_ones(self):
        self.assertEqual(2, pe.SOUND_BRIDGE_BARS)
        self.assertEqual(4, pe.ENTRY_LEAD_BEATS)

    def test_a_rest_of_two_bars_inside_a_stretch_does_not_end_the_sound(self):
        bars = music_for((FULL, 12), (SILENT, 2), (FULL, 50))
        event = ev(2, 32, "orbit", 8)
        self.assertIsNot(event, pe.fit_to_sound(event, bars))     # the strict rule (v2) cuts it short
        self.assertIs(event, bridged(event, bars))

    def test_the_event_may_end_inside_a_bridged_rest(self):
        bars = music_for((FULL, 15), (SILENT, 2), (FULL, 47))
        event = ev(2, 32, "orbit", 8)
        self.assertIs(event, bridged(event, bars))

    def test_a_rest_of_three_bars_ends_the_sound(self):
        bars = music_for((FULL, 12), (SILENT, 3), (FULL, 49))
        self.assertEqual(4, bridged(ev(2, 32, "orbit", 8), bars)["bars"])

    def test_the_stem_gone_for_good_still_shortens(self):
        bars = music_for((FULL, 11), (SILENT, 53))
        self.assertEqual(2, bridged(ev(2, 32, "orbit", 8), bars)["bars"])

    def test_silence_at_the_track_end_is_not_bridged(self):
        bars = music_for((FULL, 62), (SILENT, 2))
        self.assertEqual(4, bridged(ev(2, 56 * 4, "orbit", 8), bars)["bars"])   # bars 56-59, not into 62-63

    def test_the_lead_before_the_first_sound_does_not_count(self):
        bars = music_for((SILENT, 16), (FULL, 48))
        event = ev(2, 15 * 4, "orbit", 8)
        self.assertIs(event, bridged(event, bars))

    def test_more_than_the_lead_before_the_first_sound_does(self):
        bars = music_for((SILENT, 16), (FULL, 48))
        self.assertIsNone(bridged(ev(2, 14 * 4, "orbit", 2), bars))

    def test_the_lead_on_the_beat_grid(self):
        bars = with_beats(music_for((FULL, 64)), {2: range(0, 61)})
        event = ev(2, 57, "orbit", 8)               # the first sound on beat 61: a lead of 4 beats
        self.assertIs(event, bridged(event, bars))
        self.assertIsNone(bridged(ev(2, 56, "orbit", 2), bars))   # 5 beats ahead is too early

    def test_a_beat_grid_rest_of_nine_beats_ends_the_sound(self):
        bars = with_beats(music_for((FULL, 64)), {2: range(80, 89)})
        self.assertEqual(4, bridged(ev(2, 64, "orbit", 8), bars)["bars"])

    def test_a_beat_grid_rest_of_eight_beats_does_not(self):
        bars = with_beats(music_for((FULL, 64)), {2: range(80, 88)})
        event = ev(2, 64, "orbit", 8)
        self.assertIs(event, bridged(event, bars))

    def test_nothing_starts_late_unless_the_stem_sounds_to_the_end(self):
        self.assertIsNone(bridged(ev(2, 62 * 4, "scat", 0.5), music_for((FULL, 63), (SILENT, 1))))
        self.assertIsNotNone(bridged(ev(2, 62 * 4, "scat", 0.5), music_for((FULL, 64))))

    def test_overruns_count_by_either_rule(self):
        bars = music_for((FULL, 12), (SILENT, 2), (FULL, 50))
        events = [ev(2, 32, "orbit", 8)]
        self.assertEqual(1, pe.overruns(events, bars))
        self.assertEqual(0, pe.overruns(events, bars, pe.sounds_through_bridged))


class TheRulesV3OnContent(TheRulesV2OnContent):
    """v2's hard rules hold for v3, over the same synthetic tracks."""

    def each(self):
        for name, bars in self.tracks():
            yield name, bars, pe.RULES_V3(TRACK_BARS * 4.0, bars, 120.0)

    def test_a_track_without_passages_gets_no_events(self):
        """A steady full mix is a groove in v3: the music orbits, the tops accent."""
        _, bars = next(self.tracks())
        events = pe.RULES_V3(TRACK_BARS * 4.0, bars, 120.0)
        self.assertEqual({(1, "scat"), (2, "orbit")}, {(e["stem"], e["shape"]) for e in events})

    def test_the_content_tracks_do_get_events(self):
        for name, _, events in self.each():
            if name != "silent upper":
                self.assertTrue(events, name)

    def test_never_more_than_three_orbits_at_once(self):
        for name, _, events in self.each():
            for beat in range(TRACK_BARS * 4):
                self.assertLessEqual(orbits_at(events, beat), 3, name)

    def test_no_event_outlasts_its_sound_by_the_bridged_rule(self):
        for name, bars, events in self.each():
            self.assertEqual(0, pe.overruns(events, bars, pe.sounds_through_bridged), name)

    def test_the_rules_write_no_speed(self):
        for name, _, events in self.each():
            self.assertFalse([e for e in events if "speed" in e], name)


class TheFiles(LibraryCase):
    def test_OutputParsesInTheAppFormat(self):
        self.make_set("Song")
        self.make_set("Other", folder="Artist")
        code, _ = self.run_tool()
        self.assertEqual(0, code)
        for name, folder in (("Song", "Artist/Album"), ("Other", "Artist")):
            status, events = load_like_the_app(self.events_file(name, folder).read_text())
            self.assertEqual("ok", status)
            self.assertGreater(len(events), 0)

    def test_ReplaceKeepsHandEventsAndRedoesThePreparedOnes(self):
        self.make_set("Song")
        hand = {"stem": 1, "beat": 64.0, "shape": "scat", "bars": 2, "reversed": True,
                "seed": "42", "active": True, "origin": "hand"}
        stale = {"stem": 3, "beat": 3.0, "shape": "ping", "bars": 1, "reversed": False,
                 "seed": "7", "active": True, "origin": "prepared"}
        # A hand event on the floor stem, which the rules never touch.
        collide = dict(hand, stem=0, beat=64.0, seed="43")
        self.events_file("Song").write_text(json.dumps({"version": 1, "events": [hand, stale, collide]}))
        self.run_tool("--replace")
        status, events = load_like_the_app(self.events_file("Song").read_text())
        self.assertEqual("ok", status)
        self.assertEqual([collide, hand], [e for e in events if e["origin"] == "hand"])   # file order: beat, then stem
        self.assertNotIn(stale, events)
        # The rules want an orbit on stem 1 at beat 64, where the hand event sits.
        self.assertEqual([hand], [e for e in events if e["stem"] == 1 and e["beat"] == 64.0])
        self.assertGreater(sum(1 for e in events if e["origin"] == "prepared"), 0)

    def test_AQuarterBarHandEventReadsAsValid(self):
        self.make_set("Song")
        hand = {"stem": 2, "beat": 16.0, "shape": "scat", "bars": 0.25, "reversed": False,
                "seed": "9", "active": True, "origin": "hand"}
        self.events_file("Song").write_text(json.dumps({"version": 1, "events": [hand]}))
        self.assertEqual(("ok", [hand]), pe.read_events(self.events_file("Song")))

    def test_ReplaceKeepsAQuarterBarHandEvent(self):
        self.make_set("Song")
        hand = {"stem": 2, "beat": 16.0, "shape": "ping", "bars": 0.25, "reversed": False,
                "seed": "9", "active": True, "origin": "hand"}
        self.events_file("Song").write_text(json.dumps({"version": 1, "events": [hand]}))
        _, out = self.run_tool("--replace")
        self.assertNotIn("damaged", out)
        status, events = load_like_the_app(self.events_file("Song").read_text())
        self.assertEqual("ok", status)
        self.assertEqual([hand], [e for e in events if e["origin"] == "hand"])
        self.assertGreater(sum(1 for e in events if e["origin"] == "prepared"), 0)

    # The optional "speed" (2026-11-10): the tempo step of a hand event; missing is x1.
    def test_EverySpeedStepOfAHandEventReadsAsValid(self):
        self.make_set("Song")
        hands = [{"stem": stem, "beat": 16.0, "shape": "ping", "bars": 1, "reversed": False, "speed": speed,
                  "seed": "9", "active": True, "origin": "hand"}
                 for stem, speed in enumerate((0.25, 0.5, 2, 4))]
        hands.append(dict(hands[0], beat=32.0, speed=1.0))
        self.events_file("Song").write_text(json.dumps({"version": 1, "events": hands}))
        self.assertEqual(("ok", hands), pe.read_events(self.events_file("Song")))

    def test_ABadSpeedLeavesTheFileAlone(self):
        self.make_set("Song")
        for bad in (3, 0, -1, "x2", True, None, 8):
            hand = {"stem": 2, "beat": 16.0, "shape": "scat", "bars": 1, "reversed": False, "speed": bad,
                    "seed": "9", "active": True, "origin": "hand"}
            text = json.dumps({"version": 1, "events": [hand]})
            self.events_file("Song").write_text(text)
            self.assertEqual("damaged", pe.read_events(self.events_file("Song"))[0], bad)
            _, out = self.run_tool("--replace")
            self.assertIn("damaged", out)
            self.assertEqual(text, self.events_file("Song").read_text(), bad)

    def test_ReplaceKeepsTheSpeedOfAHandEventAndThePreparedOnesHaveNone(self):
        self.make_set("Song")
        hand = {"stem": 2, "beat": 16.0, "shape": "ping", "bars": 1, "reversed": True, "speed": 0.5,
                "seed": "9", "active": True, "origin": "hand"}
        plain = dict(hand, stem=0, beat=8.0, seed="10")
        del plain["speed"]
        self.events_file("Song").write_text(json.dumps({"version": 1, "events": [hand, plain]}))
        self.run_tool("--replace")
        status, events = load_like_the_app(self.events_file("Song").read_text())
        self.assertEqual("ok", status)
        self.assertEqual([plain, hand], [e for e in events if e["origin"] == "hand"])
        prepared = [e for e in events if e["origin"] == "prepared"]
        self.assertGreater(len(prepared), 0)
        self.assertFalse(any("speed" in e for e in prepared), "the rules choose no speed yet")

    def test_AFileWithAnyEventsIsKeptByDefault(self):
        self.make_set("Song")
        self.make_set("Mine")
        prepared = {"stem": 3, "beat": 3.0, "shape": "ping", "bars": 1, "reversed": False,
                    "seed": "7", "active": False, "origin": "prepared"}
        hand = dict(prepared, origin="hand", active=True)
        texts = {"Song": json.dumps({"version": 1, "events": [prepared]}),
                 "Mine": json.dumps({"version": 1, "events": [hand]})}
        for name, text in texts.items():
            self.events_file(name).write_text(text)
        code, out = self.run_tool()
        self.assertEqual(0, code)
        for name, text in texts.items():
            self.assertEqual(text, self.events_file(name).read_text(), name)   # byte-identical
            self.assertIn(f"kept: {name} already has events; use --replace to redo the prepared ones", out)

    def test_ReplaceRedoesAFileOfPreparedEvents(self):
        self.make_set("Song")
        stale = {"stem": 3, "beat": 3.0, "shape": "ping", "bars": 1, "reversed": False,
                 "seed": "7", "active": True, "origin": "prepared"}
        self.events_file("Song").write_text(json.dumps({"version": 1, "events": [stale]}))
        _, out = self.run_tool("--replace")
        status, events = load_like_the_app(self.events_file("Song").read_text())
        self.assertEqual("ok", status)
        self.assertNotIn(stale, events)
        self.assertGreater(len(events), 1)
        self.assertIn("wrote", out)

    def test_DryRunReportsBothWaysAndWritesNothing(self):
        self.make_set("Song")
        stale = {"stem": 3, "beat": 3.0, "shape": "ping", "bars": 1, "reversed": False,
                 "seed": "7", "active": True, "origin": "prepared"}
        text = json.dumps({"version": 1, "events": [stale]})
        self.events_file("Song").write_text(text)
        _, kept = self.run_tool("--dry-run")
        self.assertIn("kept: Song already has events", kept)
        _, replaced = self.run_tool("--dry-run", "--replace")
        self.assertIn("would write", replaced)
        self.assertEqual(text, self.events_file("Song").read_text())

    def test_NewerVersionFileIsSkippedAndReported(self):
        self.make_set("Song")
        text = json.dumps({"version": 2, "events": [], "future": True})
        self.events_file("Song").write_text(text)
        code, out = self.run_tool()
        self.assertEqual(0, code)
        self.assertEqual(text, self.events_file("Song").read_text())
        self.assertIn("newer", out)

    def test_ADamagedFileIsSkippedAndReported(self):
        self.make_set("Song")
        self.events_file("Song").write_text("{not json")
        _, out = self.run_tool()
        self.assertEqual("{not json", self.events_file("Song").read_text())
        self.assertIn("damaged", out)

    def test_SetWithoutAGridIsSkippedAndReported(self):
        self.make_set("Gridless", grid=False)
        self.make_set("Gridded")
        _, out = self.run_tool()
        self.assertFalse(self.events_file("Gridless").exists())
        self.assertTrue(self.events_file("Gridded").exists())
        self.assertIn("no grid", out)
        self.assertIn("Gridless", out)

    def test_DryRunWritesNothing(self):
        self.make_set("Song")
        _, out = self.run_tool("--dry-run")
        self.assertFalse(self.events_file("Song").exists())
        self.assertIn("Song", out)

    def test_OnlyTouchesTheNamedSet(self):
        self.make_set("Song")
        self.make_set("Other")
        self.run_tool("--only", "Other")
        self.assertFalse(self.events_file("Song").exists())
        self.assertTrue(self.events_file("Other").exists())

    def test_AnOriginalsFolderIsNotASet(self):
        self.make_set("Song", folder="Artist/originals")
        _, out = self.run_tool()
        self.assertFalse(self.events_file("Song", "Artist/originals").exists())

    def test_AnUnknownRuleSetIsAUsageError(self):
        with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
            pe.main([str(self.library), "--rules", "v9"])


class TheToolOnContent(LibraryCase):
    """v2 from a stored analysis: no ffmpeg needed (and none allowed)."""
    RULES = "v2"
    NO_FFMPEG = "/nonexistent/ffmpeg"

    def analysis_file(self, name, folder="Artist/Album"):
        return self.library / folder / f"{name} - 1 - drums.a3analysis.json"

    def store_analysis(self, name, files, level_of_bar):
        """`level_of_bar(stem, bar)` in dB, laid on the test grid in 0.1 s windows."""
        windows = []
        for stem in range(4):
            level = []
            for index in range(int(self.SECONDS / sa.WINDOW_SECONDS)):
                beat = ((index + 0.5) * sa.WINDOW_SECONDS - self.FIRST_BEAT) * self.BPM / 60.0
                level.append(int(level_of_bar(stem, int(beat // 4))) if beat >= 0 else -90)
            windows.append((level, [value - 20 for value in level]))
        sa.write_analysis(self.analysis_file(name), pe.cache_key(files), windows)

    @staticmethod
    def vocal_alone_in_bars_9_to_16(stem, bar):
        return -15 if stem == 3 or not 8 <= bar < 16 else -90

    def run_content(self, *args):
        return self.run_tool("--ffmpeg", self.NO_FFMPEG, *args)

    def test_v2_moves_what_the_content_says(self):
        self.store_analysis("Song", self.make_set("Song"), self.vocal_alone_in_bars_9_to_16)
        code, out = self.run_content()
        self.assertEqual(0, code, out)
        status, events = load_like_the_app(self.events_file("Song").read_text())
        self.assertEqual("ok", status)
        self.assertEqual([(3, 32.0, "orbit", 4), (3, 48.0, "orbit", 4)],
                         [(e["stem"], e["beat"], e["shape"], e["bars"]) for e in events])

    def test_a_dry_run_shows_the_passages_and_the_events_and_writes_nothing(self):
        self.store_analysis("Song", self.make_set("Song"), self.vocal_alone_in_bars_9_to_16)
        before = sorted(self.library.rglob("*"))
        _, out = self.run_content("--dry-run")
        self.assertEqual(before, sorted(self.library.rglob("*")))
        self.assertIn("  solo    bars 9-16 stem 3", out)
        self.assertIn("  break   bars 9-16", out)
        self.assertIn("  return  bar 17", out)
        self.assertIn("  -> orbit stem 3 beat 32 (bar 9) 4 bars = 8.0 s/lap", out)

    def test_explain_shows_the_same_in_a_real_run(self):
        self.store_analysis("Song", self.make_set("Song"), self.vocal_alone_in_bars_9_to_16)
        _, out = self.run_content("--explain")
        self.assertTrue(self.events_file("Song").exists())
        self.assertIn("  solo    bars 9-16 stem 3", out)
        self.assertIn("  -> orbit stem 3 beat 32 (bar 9) 4 bars", out)

    def test_a_real_run_without_explain_says_one_line_a_set(self):
        self.store_analysis("Song", self.make_set("Song"), self.vocal_alone_in_bars_9_to_16)
        _, out = self.run_content()
        self.assertEqual(1, len(out.splitlines()), out)

    def test_without_ffmpeg_a_set_with_no_analysis_is_skipped_and_the_run_goes_on(self):
        self.make_set("Bare")
        self.store_analysis("Song", self.make_set("Song"), self.vocal_alone_in_bars_9_to_16)
        code, out = self.run_content()
        self.assertEqual(0, code)
        self.assertIn("skipped Bare: ffmpeg not found", out)
        self.assertFalse(self.events_file("Bare").exists())
        self.assertFalse(self.analysis_file("Bare").exists())
        self.assertTrue(self.events_file("Song").exists())

    def test_a_damaged_or_newer_analysis_counts_as_absent(self):
        self.make_set("Song")
        for text in ("{not json", json.dumps({"version": sa.ANALYSIS_VERSION + 1})):
            self.analysis_file("Song").write_text(text)
            code, out = self.run_content()
            self.assertEqual(0, code)
            self.assertIn("skipped Song: ffmpeg not found", out)
            self.assertEqual(text, self.analysis_file("Song").read_text())
            self.assertFalse(self.events_file("Song").exists())

    def test_an_analysis_of_other_files_counts_as_absent(self):
        files = self.make_set("Song")
        self.store_analysis("Song", files, self.vocal_alone_in_bars_9_to_16)
        files[3].write_bytes(wav_file(self.SECONDS) + b"changed")
        self.keys["Song"] = pe.cache_key(files)
        _, out = self.run_content()
        self.assertIn("skipped Song: ffmpeg not found", out)

    def test_a_set_with_events_is_kept_without_decoding(self):
        self.make_set("Song")
        text = json.dumps({"version": 1, "events": [{"stem": 3, "beat": 3.0, "shape": "ping", "bars": 1,
                                                      "reversed": False, "seed": "7", "active": True,
                                                      "origin": "hand"}]})
        self.events_file("Song").write_text(text)
        _, out = self.run_content()
        self.assertIn("kept: Song already has events", out)
        self.assertEqual(text, self.events_file("Song").read_text())

    def test_the_bar_counting_rules_need_no_ffmpeg(self):
        self.make_set("Song")
        self.run_content("--rules", "v1")
        self.assertTrue(self.events_file("Song").exists())

    def test_a_track_without_passages_gets_no_events_file(self):
        self.store_analysis("Song", self.make_set("Song"), lambda stem, bar: -15)
        _, out = self.run_content()
        self.assertIn("Song: nothing to prepare", out)
        self.assertFalse(self.events_file("Song").exists())


class TheToolOnContentV3(LibraryCase):
    """v3, the default, from a stored analysis."""
    RULES = None
    NO_FFMPEG = TheToolOnContent.NO_FFMPEG
    analysis_file = TheToolOnContent.analysis_file
    store_analysis = TheToolOnContent.store_analysis
    run_content = TheToolOnContent.run_content

    @staticmethod
    def vocal_comes_in_over_the_groove(stem, bar):
        return -15 if stem != 3 or bar >= 16 else -90

    def test_v3_is_the_default_and_gives_each_slot_its_role(self):
        self.store_analysis("Song", self.make_set("Song"), self.vocal_comes_in_over_the_groove)
        code, out = self.run_content()
        self.assertEqual(0, code, out)
        status, events = load_like_the_app(self.events_file("Song").read_text())
        self.assertEqual("ok", status)
        roles = {(e["stem"], e["shape"]) for e in events}
        self.assertIn((1, "scat"), roles)
        self.assertIn((2, "orbit"), roles)
        self.assertIn((3, "rise", 64.0), [(e["stem"], e["shape"], e["beat"]) for e in events])
        self.assertFalse([e for e in events if e["stem"] == 0])

    def test_a_dry_run_shows_the_grooves_and_entries(self):
        self.store_analysis("Song", self.make_set("Song"), self.vocal_comes_in_over_the_groove)
        _, out = self.run_content("--dry-run")
        self.assertIn("  groove  bars 1-", out)
        self.assertIn("  entry   bars 17-", out)
        self.assertIn("  -> rise stem 3 beat 64 (bar 17) 4 bars", out)

    def store_analysis_by_beat(self, name, files, level_of_beat):
        """`level_of_beat(stem, beat)` in dB, laid on the test grid in 0.1 s windows."""
        windows = []
        for stem in range(4):
            level = []
            for index in range(int(self.SECONDS / sa.WINDOW_SECONDS)):
                beat = ((index + 0.5) * sa.WINDOW_SECONDS - self.FIRST_BEAT) * self.BPM / 60.0
                level.append(int(level_of_beat(stem, int(beat // 1))) if beat >= 0 else -90)
            windows.append((level, [value - 20 for value in level]))
        sa.write_analysis(self.analysis_file(name), pe.cache_key(files), windows)

    @staticmethod
    def a_drum_gap_and_hats_alone(stem, beat):
        """Bars 1-12 (beats 0-47): the floor out, the tops on (hats alone);
        then all four, the floor out on beats 101-102 (bar 26 beats 2-3)."""
        if stem == 0:
            return -90 if beat < 48 or beat in (101, 102) else -15
        return -15

    def test_a_dry_run_names_the_gaps_and_the_hats_and_what_answers_them(self):
        self.store_analysis_by_beat("Song", self.make_set("Song"), self.a_drum_gap_and_hats_alone)
        _, out = self.run_content("--dry-run")
        self.assertIn("  hats    bars 1-12 stem 1 (the floor out, hats alone in the tops)", out)
        self.assertIn("  gap     bar 26 beat 2, 2 beats", out)
        self.assertIn("  -> orbit stem 1 beat 0 (bar 1) 8 bars", out)
        self.assertIn("stem 1 beat 101 (bar 26 beat 2) 0.5 bars", out)
        self.assertIn("stem 3 beat 102 (bar 26 beat 3) 0.5 bars", out)

    def test_the_help_names_v3_the_default(self):
        out = io.StringIO()
        with contextlib.redirect_stdout(out), self.assertRaises(SystemExit):
            pe.main(["--help"])
        self.assertIn("v3 (default)", out.getvalue())
        self.assertIn("default: v3", out.getvalue())
        self.assertIn("GAP", out.getvalue())
        self.assertIn("HATS", out.getvalue())


E2E_RATE = 16000
E2E_BAR_SAMPLES = E2E_RATE * 2          # 120 bpm: a bar is 2 s


def pcm(values):
    return struct.pack(f"<{len(values)}h", *values)


def sine_bar(frequency, amplitude):
    period = E2E_RATE // frequency
    cycle = pcm([int(32767 * amplitude * math.sin(2 * math.pi * i / period)) for i in range(period)])
    return cycle * (E2E_BAR_SAMPLES // period)


def noise(count, amplitude, rng):
    return pcm([int(32767 * amplitude * (2 * rng.random() - 1)) for _ in range(count)])


def silence(count):
    return bytes(2 * count)


def drum_bar(hits, hit_seconds, rng):
    spacing = E2E_BAR_SAMPLES // hits
    hit = int(hit_seconds * E2E_RATE)
    return b"".join(noise(hit, 0.5, rng) + silence(spacing - hit) for _ in range(hits))


def write_stem(path, lead_samples, bars):
    with wave.open(str(path), "wb") as out:
        out.setnchannels(1)
        out.setsampwidth(2)
        out.setframerate(E2E_RATE)
        out.writeframes(silence(lead_samples) + b"".join(bars))


@unittest.skipIf(shutil.which("ffmpeg") is None, "ffmpeg is not installed: the end-to-end test decodes real files")
class EndToEnd(LibraryCase):
    """Real stems, real ffmpeg, the whole tool. 40 bars at 120 bpm, the one at
    0.5 s. Slot 0 is the floor (drums and bass), slots 1-3 the movers (tops,
    other, vocal). Bars counted from 0 here:
      0-7   all four play; bar 7 ends on a drum fill (eighths instead of quarters)
      8-15  the vocal alone
      16-23 the vocal, and a noise riser in "other" growing bar by bar
      24-39 all four again: the floor returns on bar 24"""
    RULES = "v2"
    BARS = 40

    def make_content_set(self, name="Song"):
        rng = random.Random(7)
        directory = self.library / "Artist/Album"
        directory.mkdir(parents=True, exist_ok=True)
        playing = lambda bar: bar < 8 or bar >= 24   # noqa: E731
        quiet = silence(E2E_BAR_SAMPLES)
        drums = [drum_bar(8 if bar == 7 else 4, 0.06 if bar == 7 else 0.1, rng) if playing(bar) else quiet
                 for bar in range(self.BARS)]
        tops = [sine_bar(2000, 0.15) if playing(bar) else quiet for bar in range(self.BARS)]
        other = [sine_bar(500, 0.3) if playing(bar)
                 else noise(E2E_BAR_SAMPLES, 0.02 * 1.6 ** (bar - 16), rng) if 16 <= bar < 24 else quiet
                 for bar in range(self.BARS)]
        vocal = [sine_bar(1000, 0.25) for _ in range(self.BARS)]
        files = []
        for slot, (stem, bars) in enumerate((("low", drums), ("tops", tops), ("other", other), ("vocals", vocal))):
            files.append(directory / f"{name} - {slot + 1} - {stem}.wav")
            write_stem(files[-1], int(self.FIRST_BEAT * E2E_RATE), bars)
        self.keys[name] = pe.cache_key(files)
        return files

    def analysis_file(self, name="Song"):
        return self.library / "Artist/Album" / f"{name} - 1 - low.a3analysis.json"

    def events_file(self, name, folder="Artist/Album"):
        return self.library / folder / f"{name} - 1 - low.a3events.json"

    def test_the_events_land_where_the_content_says(self):
        self.make_content_set()
        code, out = self.run_tool("--explain")
        self.assertEqual(0, code, out)
        status, events = load_like_the_app(self.events_file("Song").read_text())
        self.assertEqual("ok", status, out)
        found = [(e["shape"], e["stem"], e["beat"]) for e in events]
        self.assertIn(("orbit", 3, 32.0), found, out)          # the vocal alone, from its first bar
        rises = [e for e in events if e["shape"] == "rise"]
        self.assertEqual([(2, 96.0)], [(e["stem"], e["beat"] + 4 * e["bars"]) for e in rises], out)   # into the return
        self.assertIn(("ping", 2, 30.0), found, out)           # the fill, beat 3 of bar 7, on the loudest free mover
        self.assertFalse([e for e in events if e["stem"] == 0], out)
        self.assertNotIn(96.0, [e["beat"] for e in events], out)   # nothing on the one of the return
        self.assertTrue(self.analysis_file().exists())

    def test_a_dry_run_decodes_but_writes_nothing(self):
        self.make_content_set()
        before = sorted(self.library.rglob("*"))
        _, out = self.run_tool("--dry-run")
        self.assertEqual(before, sorted(self.library.rglob("*")))
        self.assertIn("would write", out)
        self.assertIn("  solo    bars 9-", out)

    def test_a_rerun_reads_the_analysis_and_writes_the_same_bytes(self):
        self.make_content_set()
        self.run_tool()
        first = self.events_file("Song").read_bytes()
        _, out = self.run_tool("--replace", "--ffmpeg", "/nonexistent/ffmpeg")
        self.assertIn("unchanged", out)
        self.assertEqual(first, self.events_file("Song").read_bytes())


@unittest.skipIf(shutil.which("ffmpeg") is None, "ffmpeg is not installed: the end-to-end test decodes real files")
class EndToEndDrumGap(LibraryCase):
    """Real stems, real ffmpeg, v3: 32 bars at 120 bpm, all four playing, the
    drums out on beats 41-42 (bar 11, beats 2-3, counted from 1)."""
    RULES = "v3"
    BARS = 32
    GAP_BEATS = (41, 42)
    analysis_file = EndToEnd.analysis_file
    events_file = EndToEnd.events_file

    def drums(self, rng):
        beat_samples = E2E_BAR_SAMPLES // 4
        hit = int(0.1 * E2E_RATE)
        beats = [silence(beat_samples) if beat in self.GAP_BEATS
                 else noise(hit, 0.5, rng) + silence(beat_samples - hit) for beat in range(4 * self.BARS)]
        return [b"".join(beats[bar * 4:bar * 4 + 4]) for bar in range(self.BARS)]

    def make_content_set(self, name="Song"):
        rng = random.Random(7)
        directory = self.library / "Artist/Album"
        directory.mkdir(parents=True, exist_ok=True)
        stems = (("low", self.drums(rng)), ("tops", [sine_bar(2000, 0.15)] * self.BARS),
                 ("other", [sine_bar(500, 0.3)] * self.BARS), ("vocals", [sine_bar(1000, 0.25)] * self.BARS))
        files = []
        for slot, (stem, bars) in enumerate(stems):
            files.append(directory / f"{name} - {slot + 1} - {stem}.wav")
            write_stem(files[-1], int(self.FIRST_BEAT * E2E_RATE), bars)
        self.keys[name] = pe.cache_key(files)

    def test_the_gap_is_found_and_answered_by_the_tops_and_the_vocals(self):
        self.make_content_set()
        code, out = self.run_tool("--explain")
        self.assertEqual(0, code, out)
        self.assertIn("  gap     bar 11 beat 2, 2 beats", out)
        status, events = load_like_the_app(self.events_file("Song").read_text())
        self.assertEqual("ok", status, out)
        found = [(e["stem"], e["beat"], e["bars"]) for e in events]
        self.assertIn((1, 41.0, 0.5), found, out)
        self.assertIn((3, 42.0, 0.5), found, out)
        self.assertFalse([e for e in events if e["stem"] == 0], out)
        self.assertFalse([e for e in events if e["stem"] == 2 and 36 <= e["beat"] <= 44], out)


class TheCacheKey(unittest.TestCase):
    def test_hash_matches_JUCE_hashCode64(self):
        # 101 * 97 + 98 = 9895 = 0x26a7; JUCE's toHexString is lower case.
        self.assertEqual("26a7", pe.juce_hex_hash("ab"))
        self.assertEqual("0", pe.juce_hex_hash(""))

    def test_hash_wraps_at_64_bits(self):
        value = pe.juce_hex_hash("x" * 40)
        self.assertLessEqual(len(value), 16)


class TheCacheLookup(LibraryCase):
    def test_the_key_matches_a_value_worked_out_by_hand(self):
        # AnalysisCache::keyFor builds "name|size|mtime_ms;" per slot, an empty
        # slot being "|0|0;", and takes String::hashCode64 (h = 101 * h + code
        # point, from 0, wrapping at 2^64) as lower-case hex. Worked out as the
        # polynomial sum(c_i * 101^(n-1-i)) mod 2^64 over the text
        # "A - 1 - drums.wav|1234|1700000000000;|0|0;|0|0;|0|0;":
        self.assertEqual("810ba1598a6f7161",
                         pe.juce_hex_hash("A - 1 - drums.wav|1234|1700000000000;|0|0;|0|0;|0|0;"))
        # Code points, not UTF-8 bytes: "Ä.flac|7|1000;".
        self.assertEqual("c60b8b18607fef37", pe.juce_hex_hash("\u00c4.flac|7|1000;"))

    def test_cache_key_reads_name_size_and_time_in_milliseconds(self):
        file = self.root / "A - 1 - drums.wav"
        file.write_bytes(b"x" * 1234)
        os.utime(file, (1700000000, 1700000000))
        self.assertEqual("810ba1598a6f7161", pe.cache_key([file, None, None, None]))

    def write_entries(self, *entries):
        body = "".join(f'<SET key="{k}" name="{n}" bpm="{b}" firstBeat="{f}" {extra}/>'
                       for k, n, b, f, extra in entries)
        self.cache.write_text(f"<ANALYSIS>{body}</ANALYSIS>")

    def test_corrected_values_win_over_the_analysed_ones(self):
        self.write_entries(("k", "S", 100.0, 1.0, 'correctedBpm="128" correctedFirstBeat="0.25"'))
        self.assertEqual(pe.Grid(128.0, 0.25), pe.AnalysisCache(self.cache).grid_of("k"))

    def test_the_analysed_values_serve_without_a_correction(self):
        self.write_entries(("k", "S", 100.0, 1.0, ""))
        self.assertEqual(pe.Grid(100.0, 1.0), pe.AnalysisCache(self.cache).grid_of("k"))

    def test_a_set_with_an_empty_slot_is_found_by_its_key(self):
        files = self.make_set("Trio")
        files[3].unlink()
        slots = [files[0], files[1], files[2], None]
        self.keys["Trio"] = pe.cache_key(slots)
        self.write_cache()
        self.assertEqual(pe.Grid(self.BPM, self.FIRST_BEAT), pe.AnalysisCache(self.cache).grid_of(pe.cache_key(slots)))
        self.run_tool_keeping_cache()
        self.assertTrue(self.events_file("Trio").exists())

    def test_two_entries_with_one_name_never_cross_apply(self):
        self.write_entries(("k1", "Same", 100.0, 1.0, ""), ("k2", "Same", 140.0, 2.0, ""))
        cache = pe.AnalysisCache(self.cache)
        self.assertEqual(pe.Grid(100.0, 1.0), cache.grid_of("k1"))
        self.assertEqual(pe.Grid(140.0, 2.0), cache.grid_of("k2"))
        self.assertIsNone(cache.grid_of("k3"))

    def test_a_changed_file_with_the_same_name_has_no_grid(self):
        files = self.make_set("Song")
        self.write_cache()
        files[0].write_bytes(wav_file(self.SECONDS) + b"more")   # new size, new key
        _, out = self.run_tool_keeping_cache()
        self.assertFalse(self.events_file("Song").exists())
        self.assertIn("no grid", out)


class TheFormats(unittest.TestCase):
    def write(self, data):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        path = Path(tmp.name) / "a.wav"
        path.write_bytes(data)
        return path

    def test_a_streamed_WAV_takes_its_length_from_the_file_size(self):
        # A recorder that never went back to fill in the size writes 0xFFFFFFFF.
        path = self.write(wav_header(0, rate=100, data_size=0xFFFFFFFF) + bytes([128]) * 300)
        self.assertAlmostEqual(3.0, pe.track_seconds(path))

    def test_a_WAV_cut_short_takes_its_length_from_the_file_size(self):
        path = self.write(wav_header(3600, rate=100) + bytes([128]) * 200)
        self.assertAlmostEqual(2.0, pe.track_seconds(path))

    def test_a_whole_WAV_takes_its_length_from_the_header(self):
        self.assertAlmostEqual(5.0, pe.track_seconds(self.write(wav_file(5.0))))

    def test_a_FLAC_streaminfo_gives_the_length(self):
        packed = (44100 << 44) | (1 << 41) | (15 << 36) | (44100 * 10)
        data = b"fLaC" + bytes([0x80, 0, 0, 34]) + bytes(10) + packed.to_bytes(8, "big") + bytes(16)
        self.assertAlmostEqual(10.0, pe._flac_seconds(data))

    def test_an_NI_file_is_named_without_its_whole_extension(self):
        self.assertEqual(Path("/m/Song.a3events.json"), pe.events_file_for("/m/Song.stem.mp4"))
        self.assertEqual(Path("/m/Song - 1 - drums.a3events.json"), pe.events_file_for("/m/Song - 1 - drums.flac"))

    def test_a_numbered_stem_creator_set_is_one_set(self):
        names = [f"T - {n} - {s}" for n, s in enumerate(("drums", "bass", "other", "vocals"), 1)]
        self.assertEqual([("T", [0, 1, 2, 3])], pe.group_stem_files(names))


if __name__ == "__main__":
    unittest.main()
