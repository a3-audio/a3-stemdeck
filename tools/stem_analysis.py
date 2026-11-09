# SPDX-FileCopyrightText: 2026 Raphael Eismann
# SPDX-License-Identifier: GPL-3.0-or-later
"""The four stems' loudness over time, decoded by the ffmpeg command line.

Per stem and per window of WINDOW_SECONDS: the level (RMS, dBFS) and the level
of the band above HIGH_BAND_HZ (a brightness and onset proxy), both whole dB.
ffmpeg does all the work (resample to a low rate, mono, one channel copied and
high-passed, astats per window); Python only reads its text output, so no
sample ever passes through a Python loop.

The result is kept beside the set as <name>.a3analysis.json, keyed by the set's
files (name, size, time), so a rerun does not decode again. A file with another
key, version or window, or one that does not read, counts as absent.

Decoding runs one ffmpeg at a time at the lowest CPU priority: decks are live
rigs."""

import json
import os
import shutil
import subprocess
from pathlib import Path

from passages import SILENCE_DB, Bars, power, to_db

ANALYSIS_VERSION = 1
ANALYSIS_SUFFIX = ".a3analysis.json"
WINDOW_SECONDS = 0.1
DECODE_RATE = 16000              # enough for a band above 4 kHz
HIGH_BAND_HZ = 4000
FLUX_FLOOR_DB = -60.0            # rises out of near silence count from here, not from -90
DECODE_TIMEOUT_SECONDS = 900
NICE_LEVEL = 19
NI_SUFFIX = ".stem.mp4"
STEMS = 4
LEVEL_KEY = "lavfi.astats.1.RMS_level="
BRIGHT_KEY = "lavfi.astats.2.RMS_level="


class AnalysisError(Exception):
    pass


# --- decoding ---------------------------------------------------------------

def decode_command(ffmpeg, file, slot):
    """The ffmpeg call for one stem: an NI file's slot n is its audio track
    n + 1 (track 0 is the master), a stem file has one track."""
    track = slot + 1 if Path(file).name.lower().endswith(NI_SUFFIX) else 0
    window = int(DECODE_RATE * WINDOW_SECONDS)
    chain = ",".join((
        "aformat=channel_layouts=mono",
        f"aresample={DECODE_RATE}",
        "pan=stereo|c0=c0|c1=c0",
        f"highpass=f={HIGH_BAND_HZ}:p=2:c=FR",
        f"highpass=f={HIGH_BAND_HZ}:p=2:c=FR",
        f"asetnsamples=n={window}:p=0",
        "astats=metadata=1:reset=1:measure_perchannel=RMS_level:measure_overall=none",
        "ametadata=print:file=-",
    ))
    return [ffmpeg, "-nostdin", "-hide_banner", "-v", "error", "-i", str(file),
            "-map", f"0:a:{track}", "-af", chain, "-f", "null", "-"]


def _whole_db(text):
    try:
        value = float(text)
    except ValueError:
        return int(SILENCE_DB)
    if value != value or value < SILENCE_DB:
        return int(SILENCE_DB)
    return round(value)


def parse_astats(text):
    """(level, bright) per window from ametadata's print-out."""
    level, bright = [], []
    for line in text.splitlines():
        if line.startswith(LEVEL_KEY):
            level.append(_whole_db(line[len(LEVEL_KEY):]))
        elif line.startswith(BRIGHT_KEY):
            bright.append(_whole_db(line[len(BRIGHT_KEY):]))
    count = min(len(level), len(bright))
    return level[:count], bright[:count]


def _lower_priority():
    os.nice(NICE_LEVEL)


def find_ffmpeg(name):
    found = shutil.which(name)
    if found is None:
        raise AnalysisError(f"ffmpeg not found ({name})")
    return found


def decode_stem(ffmpeg, file, slot):
    try:
        result = subprocess.run(decode_command(ffmpeg, file, slot), capture_output=True, text=True,
                                stdin=subprocess.DEVNULL, timeout=DECODE_TIMEOUT_SECONDS,
                                preexec_fn=_lower_priority)
    except (OSError, subprocess.TimeoutExpired) as error:
        raise AnalysisError(f"could not run ffmpeg on {Path(file).name}: {error}") from error
    if result.returncode != 0:
        reason = (result.stderr.strip().splitlines() or ["no message"])[-1]
        raise AnalysisError(f"ffmpeg could not decode {Path(file).name}: {reason}")
    level, bright = parse_astats(result.stdout)
    if not level:
        raise AnalysisError(f"ffmpeg found no audio in {Path(file).name}")
    return level, bright


# --- the cache file ---------------------------------------------------------

def _int_list(value):
    return isinstance(value, list) and all(isinstance(v, int) and not isinstance(v, bool) for v in value)


def _valid_stem(item):
    return (isinstance(item, dict) and _int_list(item.get("level")) and _int_list(item.get("bright"))
            and len(item["level"]) == len(item["bright"]))


def read_analysis(path, key):
    """The windows stored under `key`, or None when the file is missing, from
    another version, for other files, or damaged."""
    try:
        root = json.loads(Path(path).read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None
    if not isinstance(root, dict):
        return None
    version = root.get("version")
    if isinstance(version, bool) or version != ANALYSIS_VERSION:
        return None
    if root.get("key") != key or root.get("window") != WINDOW_SECONDS:
        return None
    stems = root.get("stems")
    if not isinstance(stems, list) or len(stems) != STEMS or not all(_valid_stem(s) for s in stems):
        return None
    return [(s["level"], s["bright"]) for s in stems]


def write_analysis(path, key, windows):
    path = Path(path)
    root = {"version": ANALYSIS_VERSION, "key": key, "window": WINDOW_SECONDS,
            "stems": [{"level": level, "bright": bright} for level, bright in windows]}
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text(json.dumps(root, separators=(",", ":")) + "\n", encoding="utf-8")
    os.replace(temporary, path)


def analyse(files, path, key, ffmpeg, write):
    """(windows, decoded): from the cache file if it holds this key, else
    decoded stem by stem (and stored when `write`). An empty slot is silence.
    Raises AnalysisError when ffmpeg is missing or a file does not decode."""
    cached = read_analysis(path, key)
    if cached is not None:
        return cached, False
    executable = find_ffmpeg(ffmpeg)
    windows = [decode_stem(executable, file, slot) if file is not None else ([], [])
               for slot, file in enumerate(files)]
    if write:
        write_analysis(path, key, windows)
    return windows, True


# --- windows to bars ----------------------------------------------------------

BEATS_PER_BAR = 4


def _beat_of_window(index, bpm, first_beat):
    centre = (index + 0.5) * WINDOW_SECONDS
    return int(((centre - first_beat) * bpm / 60.0) // 1)


def _per_bar_db(values, bar_of, bar_count):
    """Mean by power; a bar without a window is silence."""
    return [to_db(mean) for mean in _per_bar_mean([power(db) for db in values], bar_of, bar_count)]


def _high_band_flux(windows, length):
    """Per window: how far the whole mix's high band rose from the window
    before, in dB (falls count as 0)."""
    def at(values, index):
        return values[index] if index < len(values) else SILENCE_DB
    totals = [max(FLUX_FLOOR_DB, to_db(sum(power(at(bright, i)) for _, bright in windows)))
              for i in range(length)]
    return [0.0] + [max(0.0, totals[i] - totals[i - 1]) for i in range(1, length)]


def _per_bar_mean(values, bar_of, bar_count):
    sums = [0.0] * bar_count
    counts = [0] * bar_count
    for index, value in enumerate(values):
        bar = bar_of[index]
        if 0 <= bar < bar_count:
            sums[bar] += value
            counts[bar] += 1
    return [sums[b] / counts[b] if counts[b] else 0.0 for b in range(bar_count)]


def bars_from_windows(windows, bpm, first_beat, bar_count):
    """passages.Bars on the deck's grid: a window belongs to the bar its centre
    falls in; levels average by power. The level is kept per beat too."""
    length = max((len(level) for level, _ in windows), default=0)
    beat_of = [_beat_of_window(i, bpm, first_beat) for i in range(length)]
    bar_of = [beat // BEATS_PER_BAR for beat in beat_of]
    level = [_per_bar_db(stem_level, bar_of, bar_count) for stem_level, _ in windows]
    bright = [_per_bar_db(stem_bright, bar_of, bar_count) for _, stem_bright in windows]
    flux = _per_bar_mean(_high_band_flux(windows, length), bar_of, bar_count)
    beat_level = [_per_bar_db(stem_level, beat_of, bar_count * BEATS_PER_BAR) for stem_level, _ in windows]
    return Bars(level, bright, flux, beat_level)
