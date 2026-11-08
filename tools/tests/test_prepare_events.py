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


def wav_header(seconds, rate=8000):
    """A mono 8-bit PCM header claiming `seconds` of audio; no samples follow,
    the tool reads the length from the header as the app does."""
    data_size = int(seconds * rate)
    fmt = struct.pack("<HHIIHH", 1, 1, rate, rate, 1, 8)
    return (b"RIFF" + struct.pack("<I", 36 + data_size) + b"WAVE"
            + b"fmt " + struct.pack("<I", 16) + fmt + b"data" + struct.pack("<I", data_size))


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
            path.write_bytes(wav_header(self.SECONDS))
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

    def test_NeverOverwritesAFileWithHandEvents(self):
        self.make_set("Song")
        hand = {"stem": 1, "beat": 64.0, "shape": "scat", "bars": 2, "reversed": True,
                "seed": "42", "active": True, "origin": "hand"}
        stale = {"stem": 3, "beat": 3.0, "shape": "ping", "bars": 1, "reversed": False,
                 "seed": "7", "active": True, "origin": "prepared"}
        # A hand event on the very slot the rules want for an orbit (stem 0, beat 64).
        collide = dict(hand, stem=0, beat=64.0, seed="43")
        self.events_file("Song").write_text(json.dumps({"version": 1, "events": [hand, stale, collide]}))
        self.run_tool()
        status, events = load_like_the_app(self.events_file("Song").read_text())
        self.assertEqual("ok", status)
        self.assertEqual([collide, hand], [e for e in events if e["origin"] == "hand"])   # file order: beat, then stem
        self.assertNotIn(stale, events)
        self.assertEqual(1, sum(1 for e in events if e["stem"] == 0 and e["beat"] == 64.0))
        self.assertGreater(sum(1 for e in events if e["origin"] == "prepared"), 0)

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


class TheFormats(unittest.TestCase):
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
