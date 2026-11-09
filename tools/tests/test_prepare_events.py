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
import struct
import sys
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "tools"))
import passages as ps  # noqa: E402
import prepare_events as pe  # noqa: E402

SHAPES = {"orbit", "ping", "rise", "scat"}
ORIGINS = {"hand", "prepared"}
BARS = {0.5, 1, 2, 4, 8}
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

    def run_tool(self, *args):
        self.write_cache()
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            code = pe.main([str(self.library), "--cache", str(self.cache), *args])
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

    def test_DrumsAndBassNeverMove(self):
        for e in self.prepared():
            self.assertNotIn(e["stem"], (0, 1), e)

    def test_BothUpperStemsMove(self):
        self.assertEqual({2, 3}, {e["stem"] for e in self.prepared()})

    def test_RerunIsIdentical(self):
        self.make_set("Song")
        self.write_cache()
        argv = [str(self.library), "--cache", str(self.cache)]
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

    def plan(self, passages, bars):
        return pe.events_from_passages(passages, bars, self.LENGTH)

    def full_mix(self):
        return make_bars(steady(), steady(), steady(), steady())

    def test_a_vocal_solo_orbits_the_vocal_for_four_bars_from_its_start(self):
        events = self.plan([ps.Passage("solo", 8, 16, 3)], self.full_mix())
        self.assertEqual([("orbit", 3, 32.0, 4, False)],
                         [(e["shape"], e["stem"], e["beat"], e["bars"], e["reversed"]) for e in events])

    def test_a_long_solo_orbits_eight_bars_then_turns_back(self):
        events = self.plan([ps.Passage("solo", 8, 24, 2)], self.full_mix())
        self.assertEqual([("orbit", 2, 32.0, 8, False), ("orbit", 2, 64.0, 8, True)],
                         [(e["shape"], e["stem"], e["beat"], e["bars"], e["reversed"]) for e in events])

    def test_a_solo_of_the_drums_or_bass_moves_nothing(self):
        self.assertEqual([], self.plan([ps.Passage("solo", 8, 16, 0), ps.Passage("solo", 20, 28, 1)],
                                       self.full_mix()))

    def test_a_break_orbits_slowly_the_stronger_upper_stem(self):
        bars = make_bars(steady(), steady(), steady(FULL), steady(FULL - 6))
        events = self.plan([ps.Passage("break", 8, 24, None)], bars)
        self.assertEqual([("orbit", 2, 32.0, 8)], [(e["shape"], e["stem"], e["beat"], e["bars"]) for e in events])

    def test_a_break_where_the_upper_stems_are_silent_moves_nothing(self):
        bars = make_bars(steady(), steady(), steady(SILENT), steady(SILENT))
        self.assertEqual([], self.plan([ps.Passage("break", 8, 24, None)], bars))

    def test_a_build_rises_the_rising_upper_stem_and_ends_with_it(self):
        other = per_bar((QUIET, 8)) + [QUIET + 2.0 * i for i in range(1, 7)] + per_bar((FULL, 34))
        bars = make_bars(steady(), steady(), other, steady(QUIET))
        events = self.plan([ps.Passage("build", 8, 14, 2)], bars)
        self.assertEqual([("rise", 2, 40.0, 4)], [(e["shape"], e["stem"], e["beat"], e["bars"]) for e in events])

    def test_a_build_inside_a_solo_of_the_same_stem_still_rises(self):
        other = per_bar((QUIET, 8)) + [QUIET + 2.0 * i for i in range(1, 9)] + per_bar((FULL, 32))
        bars = make_bars(steady(), steady(), other, steady(QUIET))
        events = self.plan([ps.Passage("solo", 9, 24, 2), ps.Passage("build", 9, 16, 2)], bars)
        self.assertIn(("rise", 2, 48.0, 4), [(e["shape"], e["stem"], e["beat"], e["bars"]) for e in events])

    def test_a_build_of_the_drums_alone_moves_nothing(self):
        drums = per_bar((QUIET, 8)) + [QUIET + 3.0 * i for i in range(1, 9)] + per_bar((FULL, 32))
        bars = make_bars(drums, steady(), steady(QUIET), steady(QUIET))
        self.assertEqual([], self.plan([ps.Passage("build", 8, 16, 0)], bars))

    def test_a_fill_pings_the_louder_upper_stem_on_beat_three(self):
        bars = make_bars(steady(), steady(), steady(FULL - 6), steady(FULL))
        events = self.plan([ps.Passage("fill", 15, 16, None)], bars)
        self.assertEqual([("ping", 3, 62.0, 1)], [(e["shape"], e["stem"], e["beat"], e["bars"]) for e in events])

    def test_a_fill_pings_the_other_upper_stem_when_the_louder_is_busy(self):
        bars = make_bars(steady(), steady(), steady(FULL - 6), steady(FULL))
        events = self.plan([ps.Passage("solo", 12, 20, 3), ps.Passage("fill", 15, 16, None)], bars)
        self.assertIn(("ping", 2, 62.0), [(e["shape"], e["stem"], e["beat"]) for e in events])

    def test_the_return_puts_nothing_on_the_one_and_ends_a_running_orbit(self):
        events = self.plan([ps.Passage("break", 8, 24, None), ps.Passage("return", 14, 15, 0)], self.full_mix())
        self.assertEqual([("orbit", 2, 32.0, 4)], [(e["shape"], e["stem"], e["beat"], e["bars"]) for e in events])
        self.assertNotIn(56.0, [e["beat"] for e in events])

    def test_direction_alternates_per_passage(self):
        events = self.plan([ps.Passage("solo", 8, 16, 3), ps.Passage("solo", 24, 32, 2),
                            ps.Passage("solo", 36, 44, 3)], self.full_mix())
        self.assertEqual([False, True, False], [e["reversed"] for e in events])

    def test_only_one_orbit_runs_at_a_time(self):
        events = self.plan([ps.Passage("break", 8, 24, None), ps.Passage("solo", 10, 18, 3)], self.full_mix())
        orbits = [e for e in events if e["shape"] == "orbit"]
        self.assertEqual([(3, 40.0)], [(e["stem"], e["beat"]) for e in orbits])   # the solo wins

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
            yield name, bars, pe.RULES_V2(TRACK_BARS * 4.0, bars)

    def test_drums_and_bass_never_move(self):
        for name, _, events in self.each():
            self.assertFalse([e for e in events if e["stem"] in (0, 1)], name)

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
        self.assertEqual([], pe.RULES_V2(TRACK_BARS * 4.0, bars))

    def test_the_content_tracks_do_get_events(self):
        for name, _, events in self.each():
            if name not in ("steady", "silent upper"):
                self.assertTrue(events, name)

    def test_the_same_content_gives_the_same_events(self):
        for (name, _, first), (_, _, second) in zip(self.each(), self.each()):
            self.assertEqual(first, second, name)


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
        # A hand event on the very slot the rules want for an orbit (stem 0, beat 64).
        collide = dict(hand, stem=0, beat=64.0, seed="43")
        self.events_file("Song").write_text(json.dumps({"version": 1, "events": [hand, stale, collide]}))
        self.run_tool("--replace")
        status, events = load_like_the_app(self.events_file("Song").read_text())
        self.assertEqual("ok", status)
        self.assertEqual([collide, hand], [e for e in events if e["origin"] == "hand"])   # file order: beat, then stem
        self.assertNotIn(stale, events)
        self.assertEqual(1, sum(1 for e in events if e["stem"] == 0 and e["beat"] == 64.0))
        self.assertGreater(sum(1 for e in events if e["origin"] == "prepared"), 0)

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

    def run_tool_keeping_cache(self):
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            code = pe.main([str(self.library), "--cache", str(self.cache)])
        return code, out.getvalue()


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
