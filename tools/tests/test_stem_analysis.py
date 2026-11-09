# SPDX-FileCopyrightText: 2026 Raphael Eismann
# SPDX-License-Identifier: GPL-3.0-or-later
"""The stems' loudness over time (tools/stem_analysis.py): parsing ffmpeg's
astats output, the analysis cache file, and windows mapped to bars."""

import json
import math
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
import wave
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "tools"))
import stem_analysis as sa  # noqa: E402

FFMPEG = shutil.which("ffmpeg")

ASTATS = """frame:0    pts:0       pts_time:0
lavfi.astats.1.RMS_level=-20.4
lavfi.astats.2.RMS_level=-41.6
frame:1    pts:1600    pts_time:0.1
lavfi.astats.1.RMS_level=-inf
lavfi.astats.2.RMS_level=-inf
"""


class Parsing(unittest.TestCase):
    def test_level_and_high_band_per_window_rounded_and_silence_floored(self):
        self.assertEqual(([-20, -90], [-42, -90]), sa.parse_astats(ASTATS))

    def test_no_windows_is_an_empty_analysis(self):
        self.assertEqual(([], []), sa.parse_astats(""))


class Command(unittest.TestCase):
    def test_a_stem_file_maps_its_only_audio_track(self):
        command = sa.decode_command("/usr/bin/ffmpeg", Path("/m/A - 1 - drums.flac"), 0)
        self.assertEqual("/usr/bin/ffmpeg", command[0])
        self.assertIn("0:a:0", command)
        self.assertIn("/m/A - 1 - drums.flac", command)

    def test_an_NI_file_skips_the_master_on_track_0(self):
        command = sa.decode_command("ffmpeg", Path("/m/Song.stem.mp4"), 2)
        self.assertIn("0:a:3", command)

    def test_the_window_and_the_high_band_are_in_the_filter(self):
        filters = " ".join(sa.decode_command("ffmpeg", Path("/m/a.wav"), 0))
        self.assertIn(f"asetnsamples=n={int(sa.DECODE_RATE * sa.WINDOW_SECONDS)}", filters)
        self.assertIn(f"highpass=f={sa.HIGH_BAND_HZ}", filters)


class TheCacheFile(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.path = Path(tmp.name) / "Song.a3analysis.json"
        self.windows = [([-20, -21], [-40, -41]), ([-30, -30], [-60, -61]), ([], []), ([-10, -90], [-30, -90])]

    def test_what_is_written_reads_back_under_the_same_key(self):
        sa.write_analysis(self.path, "k1", self.windows)
        self.assertEqual(self.windows, sa.read_analysis(self.path, "k1"))

    def test_another_key_is_another_track(self):
        sa.write_analysis(self.path, "k1", self.windows)
        self.assertIsNone(sa.read_analysis(self.path, "k2"))

    def test_a_missing_file_is_absent(self):
        self.assertIsNone(sa.read_analysis(self.path, "k1"))

    def test_an_older_or_newer_version_is_absent(self):
        for version in (sa.ANALYSIS_VERSION - 1, sa.ANALYSIS_VERSION + 1, "1", True):
            sa.write_analysis(self.path, "k1", self.windows)
            root = json.loads(self.path.read_text())
            root["version"] = version
            self.path.write_text(json.dumps(root))
            self.assertIsNone(sa.read_analysis(self.path, "k1"), version)

    def test_a_damaged_file_is_absent(self):
        for text in ("{not json", "[]", '{"version": 1}', json.dumps(
                {"version": sa.ANALYSIS_VERSION, "key": "k1", "window": sa.WINDOW_SECONDS,
                 "stems": [{"level": [1, "x"], "bright": [1, 2]}] * 4}),
                json.dumps({"version": sa.ANALYSIS_VERSION, "key": "k1", "window": sa.WINDOW_SECONDS,
                            "stems": [{"level": [1, 2], "bright": [1]}] * 4}),
                json.dumps({"version": sa.ANALYSIS_VERSION, "key": "k1", "window": sa.WINDOW_SECONDS,
                            "stems": [{"level": [], "bright": []}] * 3})):
            self.path.write_text(text)
            self.assertIsNone(sa.read_analysis(self.path, "k1"), text)

    def test_another_window_length_is_absent(self):
        sa.write_analysis(self.path, "k1", self.windows)
        root = json.loads(self.path.read_text())
        root["window"] = 0.05
        self.path.write_text(json.dumps(root))
        self.assertIsNone(sa.read_analysis(self.path, "k1"))


class ToBars(unittest.TestCase):
    # 120 bpm: a beat is 0.5 s, a bar 2 s, 20 windows of 0.1 s.
    def test_windows_average_by_power_into_bars_from_the_first_beat(self):
        lead = [-90] * 5                                   # 0.5 s before the one
        drums = lead + [-20] * 20 + [-90] * 20
        windows = [(drums, drums), ([], []), ([], []), ([], [])]
        bars = sa.bars_from_windows(windows, bpm=120.0, first_beat=0.5, bar_count=2)
        self.assertEqual(2, bars.count)
        self.assertAlmostEqual(-20.0, bars.level[0][0], places=6)
        self.assertEqual(-90.0, bars.level[0][1])
        self.assertEqual(-90.0, bars.level[1][0])         # an empty slot is silence

    def test_half_a_bar_of_sound_is_three_dB_down(self):
        windows = [([-20] * 10 + [-90] * 10, [-90] * 20), ([], []), ([], []), ([], [])]
        bars = sa.bars_from_windows(windows, bpm=120.0, first_beat=0.0, bar_count=1)
        self.assertAlmostEqual(-20.0 - 10 * math.log10(2), bars.level[0][0], places=2)

    def test_flux_counts_the_rises_of_the_high_band(self):
        steady = [-30] * 20
        pulsing = [-30, -50] * 10
        windows = [([-20] * 40, steady + pulsing), ([], []), ([], []), ([], [])]
        bars = sa.bars_from_windows(windows, bpm=120.0, first_beat=0.0, bar_count=2)
        self.assertAlmostEqual(0.0, bars.flux[0])
        self.assertGreater(bars.flux[1], 5.0)

    def test_bars_after_the_audio_ends_are_silence(self):
        windows = [([-20] * 20, [-40] * 20), ([], []), ([], []), ([], [])]
        bars = sa.bars_from_windows(windows, bpm=120.0, first_beat=0.0, bar_count=3)
        self.assertEqual(-90.0, bars.level[0][2])

    def test_the_levels_are_also_kept_per_beat(self):
        lead = [-90] * 5                                   # 0.5 s before the one
        drums = lead + [-20] * 5 + [-90] * 5 + [-20] * 10   # beat 2 of the first bar silent
        windows = [(drums, drums), ([], []), ([], []), ([], [])]
        bars = sa.bars_from_windows(windows, bpm=120.0, first_beat=0.5, bar_count=2)
        self.assertEqual(8, len(bars.beat_level[0]))
        self.assertAlmostEqual(-20.0, bars.beat_level[0][0], places=6)
        self.assertEqual(-90.0, bars.beat_level[0][1])
        self.assertAlmostEqual(-20.0, bars.beat_level[0][2], places=6)
        self.assertEqual(-90.0, bars.beat_level[0][4])      # after the audio: silence
        self.assertEqual(-90.0, bars.beat_level[1][0])      # an empty slot is silence


def write_tone(path, seconds, amplitude, rate=16000, frequency=500):
    period = rate // frequency
    cycle = b"".join(struct.pack("<h", int(32767 * amplitude * math.sin(2 * math.pi * i / period)))
                     for i in range(period))
    with wave.open(str(path), "wb") as out:
        out.setnchannels(1)
        out.setsampwidth(2)
        out.setframerate(rate)
        out.writeframes(cycle * int(seconds * rate / period))


@unittest.skipIf(FFMPEG is None, "ffmpeg is not installed: the decoding tests need it")
class Decoding(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.root = Path(tmp.name)

    def test_a_tone_decodes_to_its_level_window_by_window(self):
        path = self.root / "tone.wav"
        write_tone(path, 2.0, 0.5)                         # sine at 0.5: RMS -9 dBFS
        level, bright = sa.decode_stem(FFMPEG, path, 0)
        self.assertEqual(20, len(level))
        self.assertTrue(all(abs(value + 9) <= 1 for value in level), level)
        self.assertTrue(all(value < -40 for value in bright), bright)   # 500 Hz has no high band

    def test_an_NI_file_decodes_each_stem_from_its_own_track(self):
        # Master on track 0 (loudest), then the four stems at levels in no order.
        path = self.root / "Song.stem.mp4"
        command = [FFMPEG, "-v", "error", "-y"]
        for volume in (0.9, 0.05, 0.4, 0.1, 0.2):
            command += ["-f", "lavfi", "-i", f"sine=f=500:d=1:sample_rate=44100,volume={volume}"]
        for index in range(5):
            command += ["-map", f"{index}:a"]
        subprocess.run(command + ["-c:a", "aac", "-f", "mp4", str(path)], check=True)
        means = [sum(sa.decode_stem(FFMPEG, path, slot)[0][2:8]) / 6 for slot in range(4)]
        self.assertEqual([1, 3, 2, 0], sorted(range(4), key=lambda slot: -means[slot]))
        self.assertGreater(means[1] - means[0], 15)                       # 0.4 against 0.05: 18 dB

    def test_a_file_ffmpeg_cannot_read_is_an_analysis_error(self):
        path = self.root / "broken.wav"
        path.write_bytes(b"RIFF nothing here")
        with self.assertRaises(sa.AnalysisError):
            sa.decode_stem(FFMPEG, path, 0)

    def test_a_missing_ffmpeg_is_an_analysis_error(self):
        with self.assertRaises(sa.AnalysisError):
            sa.find_ffmpeg(str(self.root / "no-ffmpeg-here"))

    def test_analyse_decodes_once_and_then_reads_the_cache(self):
        files = []
        for slot in range(4):
            files.append(self.root / f"S - {slot + 1} - x.wav")
            write_tone(files[-1], 1.0, 0.25)
        cache = self.root / "S.a3analysis.json"
        windows, decoded = sa.analyse(files, cache, "k", FFMPEG, write=True)
        self.assertTrue(decoded)
        self.assertTrue(cache.exists())
        again, decoded = sa.analyse(files, cache, "k", str(self.root / "no-ffmpeg-here"), write=True)
        self.assertFalse(decoded)
        self.assertEqual(windows, again)

    def test_analyse_without_write_leaves_no_file(self):
        path = self.root / "S.wav"
        write_tone(path, 1.0, 0.25)
        cache = self.root / "S.a3analysis.json"
        sa.analyse([path, None, None, None], cache, "k", FFMPEG, write=False)
        self.assertFalse(cache.exists())


if __name__ == "__main__":
    unittest.main()
