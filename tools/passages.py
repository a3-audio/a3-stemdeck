# SPDX-FileCopyrightText: 2026 Raphael Eismann
# SPDX-License-Identifier: GPL-3.0-or-later
"""Passages of a track, found in per-bar numbers of its four stems.

Pure functions, no audio: stem_analysis.py turns the stem files into `Bars`,
the preparation rules in prepare_events.py turn passages into events. Stems in
the set's order. Names do not matter, slots do: slot 0 is the floor (drums and
bass, the low end), slots 1-3 are the rest (tops, music, vocals or the like).

A passage is (kind, start bar, end bar exclusive, stem or None):
  solo    one stem carries most of the energy, the floor quiet
  break   the floor stem far below its usual level
  build   the mix gets louder and brighter, bar after bar; stem = the stem
          that rises most
  return  the bar where the floor comes back after a break (one bar long)
  fill    an onset spike in the last bar of a phrase
Bars count from the grid's first beat (the track's one), four beats a bar."""

from collections import namedtuple
from math import log10

# Thresholds: first guesses from reading about separated stems and a few
# synthetic cases, not from listening. They are meant to be tuned by ear on
# real tracks, and the tests pin behaviour around them, not their values.
SILENCE_DB = -90.0                # what a window of digital silence counts as
AUDIBLE_FLOOR_DB = -45.0          # a stem quieter than this is separation bleed, not playing
MASKED_BELOW_DB = 30.0            # ... and so is one this far under the loudest stem of the bar
REFERENCE_PERCENTILE = 0.75       # a stem's "usual level": survives a quarter of the track without it
QUIET_BELOW_REFERENCE_DB = 12.0   # the floor this far under its usual level is out
SOLO_SHARE = 0.7                  # of the bar's energy (the next stem at least ~4 dB down)
MIN_PASSAGE_BARS = 4              # solo, break and build: shorter is a moment, not a passage
BUILD_RISE_DB = 6.0               # the mix over the build, base bar to last bar
BUILD_BRIGHT_RISE_DB = 6.0        # the high band over the build
BUILD_DIP_DB = 1.0                # a bar may fall this much and the build goes on
BUILD_STEM_RISE_DB = 3.0          # a stem rising less than this over a build is not "the rising stem"
BUILD_MAX_BARS = 16
FILL_PHRASE_BARS = 8              # fills are looked for in the last bar of every 8
FILL_SPIKE_RATIO = 1.5            # onset proxy of the bar against the bars before and the track
FILL_LOOKBACK_BARS = 3

FLOOR = 0

Passage = namedtuple("Passage", "kind start end stem")


class Bars:
    """Per-bar numbers of one track: level[stem][bar] and bright[stem][bar] in
    dBFS (bright: the band above ~4 kHz), flux[bar] the onset proxy of the
    whole mix's high band (mean rise in dB from window to window)."""

    def __init__(self, level, bright, flux):
        self.level = level
        self.bright = bright
        self.flux = flux

    @property
    def count(self):
        return len(self.flux)

    @property
    def stems(self):
        return len(self.level)

    def total_level(self, bar):
        return power_sum_db(stem[bar] for stem in self.level)

    def total_bright(self, bar):
        return power_sum_db(stem[bar] for stem in self.bright)


def power(db):
    return 10.0 ** (db / 10.0)


def to_db(value):
    if value <= power(SILENCE_DB):
        return SILENCE_DB
    return 10.0 * log10(value)


def power_sum_db(levels):
    return to_db(sum(power(db) for db in levels))


def percentile(values, fraction):
    ordered = sorted(values)
    return ordered[int(round(fraction * (len(ordered) - 1)))]


def median(values):
    return percentile(values, 0.5)


def audible(bars, stem, bar):
    level = bars.level[stem][bar]
    loudest = max(bars.level[s][bar] for s in range(bars.stems))
    return level >= AUDIBLE_FLOOR_DB and level >= loudest - MASKED_BELOW_DB


def references(bars):
    return [percentile(stem, REFERENCE_PERCENTILE) for stem in bars.level]


def _quiet(bars, reference, stem, bar):
    return not audible(bars, stem, bar) or bars.level[stem][bar] < reference[stem] - QUIET_BELOW_REFERENCE_DB


def _runs(flags):
    """(start, end, value) of every run of equal, not-None values."""
    runs = []
    start = 0
    for bar in range(1, len(flags) + 1):
        if bar == len(flags) or flags[bar] != flags[start]:
            if flags[start] is not None:
                runs.append((start, bar, flags[start]))
            start = bar
    return runs


def _long_runs(flags):
    return [run for run in _runs(flags) if run[1] - run[0] >= MIN_PASSAGE_BARS]


def find_breaks(bars, reference):
    flags = [True if _quiet(bars, reference, FLOOR, b) else None for b in range(bars.count)]
    return [Passage("break", start, end, None) for start, end, _ in _long_runs(flags)]


def _solo_stem(bars, reference, bar):
    if not _quiet(bars, reference, FLOOR, bar):
        return None
    total = sum(power(stem[bar]) for stem in bars.level)
    for stem in range(bars.stems):
        if audible(bars, stem, bar) and power(bars.level[stem][bar]) >= SOLO_SHARE * total:
            return stem
    return None


def find_solos(bars, reference):
    flags = [_solo_stem(bars, reference, b) for b in range(bars.count)]
    return [Passage("solo", start, end, stem) for start, end, stem in _long_runs(flags)]


def find_returns(bars, reference, breaks):
    returns = []
    for passage in breaks:
        bar = passage.end
        if bar < bars.count and not _quiet(bars, reference, FLOOR, bar):
            returns.append(Passage("return", bar, bar + 1, FLOOR))
    return returns


def _rising_runs(bars, return_bars):
    """Maximal runs of bars in which neither the mix nor its high band falls
    by more than BUILD_DIP_DB; a return bar ends a run."""
    runs = []
    start = 0
    for bar in range(1, bars.count + 1):
        going_on = (bar < bars.count and bar not in return_bars
                    and bars.total_level(bar) >= bars.total_level(bar - 1) - BUILD_DIP_DB
                    and bars.total_bright(bar) >= bars.total_bright(bar - 1) - BUILD_DIP_DB)
        if not going_on:
            runs.append((start, bar))
            start = bar
    return runs


def _build_in(bars, start, end):
    """The build inside a rising run, or None: it ends where the mix first
    reaches its peak, and starts on the bar after the last one still at the
    run's bottom (that bar is the base the rise is measured from). At the
    bottom means level and high band both: a riser under a louder steady
    vocal hardly moves the level at first, but the high band shows it."""
    totals = [bars.total_level(b) for b in range(start, end)]
    peak = max(totals)
    end = start + next(i for i, value in enumerate(totals) if value >= peak - BUILD_DIP_DB) + 1
    totals = totals[:end - start]
    brights = [bars.total_bright(b) for b in range(start, end)]
    low_level = [value <= min(totals) + BUILD_DIP_DB for value in totals]
    low_bright = [value <= min(brights) + BUILD_DIP_DB for value in brights]
    at_bottom = [i for i in range(len(totals)) if low_level[i] and low_bright[i]]
    base = start + max(at_bottom or [i for i in range(len(totals)) if low_level[i]])
    base = max(base, end - 1 - BUILD_MAX_BARS)
    if end - (base + 1) < MIN_PASSAGE_BARS:
        return None
    last = end - 1
    if bars.total_level(last) - bars.total_level(base) < BUILD_RISE_DB:
        return None
    if bars.total_bright(last) - bars.total_bright(base) < BUILD_BRIGHT_RISE_DB:
        return None
    rises = [bars.level[s][last] - bars.level[s][base] for s in range(bars.stems)]
    stem = max(range(bars.stems), key=lambda s: (rises[s], -s))
    return Passage("build", base + 1, end, stem)


def stem_rise(bars, build, stem):
    """How much `stem` rose over a build: from its base bar (the one before
    the build) to its last bar."""
    return bars.level[stem][build.end - 1] - bars.level[stem][build.start - 1]


def find_builds(bars, return_bars):
    builds = []
    for start, end in _rising_runs(bars, return_bars):
        if end - start > MIN_PASSAGE_BARS:
            build = _build_in(bars, start, end)
            if build is not None:
                builds.append(build)
    return builds


def find_fills(bars):
    if bars.count == 0:
        return []
    typical = median(bars.flux)
    fills = []
    for bar in range(FILL_PHRASE_BARS - 1, bars.count, FILL_PHRASE_BARS):
        before = bars.flux[max(0, bar - FILL_LOOKBACK_BARS):bar]
        if not before:
            continue
        if bars.flux[bar] > 0 and bars.flux[bar] >= FILL_SPIKE_RATIO * max(median(before), typical):
            fills.append(Passage("fill", bar, bar + 1, None))
    return fills


def find_passages(bars):
    """Every passage of the track, by start bar (then kind)."""
    if bars.count == 0:
        return []
    reference = references(bars)
    breaks = find_breaks(bars, reference)
    returns = find_returns(bars, reference, breaks)
    found = (find_solos(bars, reference) + breaks + returns
             + find_builds(bars, {p.start for p in returns}) + find_fills(bars))
    order = ("break", "solo", "build", "fill", "return")
    return sorted(found, key=lambda p: (p.start, order.index(p.kind)))
