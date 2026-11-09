# SPDX-FileCopyrightText: 2026 Raphael Eismann
# SPDX-License-Identifier: GPL-3.0-or-later
"""Event files for a whole library: StemDeck's prepared events, v1 format.

Writes <first stem>.a3events.json beside each set (Source/StemEventsFile.cpp
is the format, and the loader this has to satisfy). A set is found the way
Source/StemSet.cpp and Source/StemNames.cpp find it, minimally; its tempo and
first beat are read from the app's analysis cache (~/.config/StemDeck/
analysis.xml), so a set the app has never loaded has no grid and is skipped
and reported. The track's length is read from the first file's header.

A file that already holds any event is kept as it is: the DJ may have nudged,
switched off or deleted prepared events (an edit in the app makes an event
"hand"). With --replace only the "prepared" events are redone; events the DJ
made by hand (origin "hand") are kept as they are, and no prepared event lands
on a slot a hand event holds. A file that is damaged or written by a newer
StemDeck is never touched.

The content rules are content-motivated, without any model: the stems are
decoded by ffmpeg into loudness and high-band level per 0.1 s
(stem_analysis.py, stored beside the set as <name>.a3analysis.json), laid on
the deck's grid as per-bar numbers, searched for solo, break, build, return
and fill passages, and for grooves, dropouts and entries (passages.py, where
the detection thresholds are). RULES_V3 (the default) gives each moving slot
a role and places events from all of these; RULES_V2 only from the passages,
almost nothing outside the breaks. v1 still counts bars only. `--help` says it
for the user; `--dry-run` and `--explain` print the passages and events per
set."""

import argparse
import hashlib
import json
import math
import os
import re
import struct
import sys
import xml.etree.ElementTree as ElementTree
from collections import namedtuple
from pathlib import Path

import passages
import stem_analysis

Grid = namedtuple("Grid", "bpm first_beat")

FILE_VERSION = 1
MAX_EVENTS = 512
SLOT_TOLERANCE = 1e-6
BEATS_PER_BAR = 4
MIN_GAP_BEATS = 2.0           # per stem, from the pro-DJ density rule
STEMS = 4
FIELD_ORDER = ("stem", "beat", "shape", "bars", "reversed", "seed", "active", "origin")
SHAPES = ("orbit", "ping", "rise", "scat")   # "ping" is the file's name for the pad CROSS
BAR_LENGTHS = (0.5, 1, 2, 4, 8)
EVENTS_SUFFIX = ".a3events.json"
NI_SUFFIX = ".stem.mp4"
AUDIO_EXTENSIONS = (".wav", ".aiff", ".aif", ".flac", ".ogg", ".mp3", ".m4a", ".opus")
SEPARATORS = " -_"
STREAMED_WAV_SIZE = 0xFFFFFFFF   # a recorder that never went back to fill in the data size
DEFAULT_CACHE = Path.home() / ".config/StemDeck/analysis.xml"


# --- the rule set (a placeholder for the pro-DJ to tune) --------------------

PHRASE_BARS = 16
ORBIT_BARS = 4
PING_BARS = 1
FLOOR_STEM = 0                # the first slot: drums and bass (low), never moves
MOVING_STEMS = (1, 2, 3)      # tops, music, vocals (or other); names do not matter, slots do
PING_BEAT_IN_BAR = 2          # beat 3 of the bar, counted from 0


def _event(stem, beat, shape, bars, reversed_):
    return {"stem": stem, "beat": float(beat), "shape": shape, "bars": bars,
            "reversed": reversed_, "active": True, "origin": "prepared"}


def RULES_V1(length_beats, bars=None, bpm=None):
    """Candidate events (no seed yet) for a track of `length_beats` beats.

    The first slot (drums and bass, the low end) holds the floor and never
    moves (pro-DJ: nothing under ~250 Hz travels); the other three do. From
    the second phrase on, an ORBIT of 4 bars on each phrase start, the movers
    taking turns and
    the direction alternating per two phrases. On beat 3 of the last bar of
    every phrase a PING on the next mover, the one that is not about to orbit.
    Sparse on purpose: two events a phrase. Counts bars only: `bars` (the
    content) and `bpm` are not looked at."""
    phrase_beats = PHRASE_BARS * BEATS_PER_BAR
    events = []
    phrase = 0
    while (phrase + 1) * phrase_beats <= length_beats:
        orbiting_next = MOVING_STEMS[phrase % len(MOVING_STEMS)]
        resting = MOVING_STEMS[(phrase + 1) % len(MOVING_STEMS)]
        ping_beat = (phrase + 1) * phrase_beats - BEATS_PER_BAR + PING_BEAT_IN_BAR
        events.append(_event(resting, ping_beat, "ping", PING_BARS, False))
        next_start = (phrase + 1) * phrase_beats
        if next_start + ORBIT_BARS * BEATS_PER_BAR <= length_beats:
            events.append(_event(orbiting_next, next_start, "orbit", ORBIT_BARS, (phrase // 2) % 2 == 1))
        phrase += 1
    return events


# --- v2: events from the passages the stems show ----------------------------

# An orbit's `bars` is its length and its speed: one lap in `bars` bars. The
# pro-DJ speed table (a3-pro-dj spatial/playing-in-3d.md, "How fast"): one lap
# per 4 bars is the cruise, per 2 bars energetic (the cruise below ~110 BPM),
# per bar the ceiling, 8 bars or slower a drift; laps are powers of two and a
# speed change must be x2 to be heard. So the lap is chosen by tempo, in
# seconds, and only ever doubled or halved.
ORBIT_LAPS = (2, 4, 8)        # 0.5 and 1 bar are accents (PING), never an orbit here
LAP_SECONDS_TARGET = 9.0      # a calm lap: the middle of a readable ~6-12 s (98 BPM, 4 bars: 9.8 s)
SLOW_LAP_SECONDS = 12.0       # the slow end of that range, for the floor stem's hats
RISE_MAX_BARS = 8
# Which passage wins a stem (or the one orbit) when two want it. A build
# before a solo: a riser that is also loudest is still a riser.
PRIORITY = {"build": 0, "solo": 1, "highs": 2, "break": 3, "fill": 4}

Candidate = namedtuple("Candidate", "priority passage alternatives event")
Plan = namedtuple("Plan", "found bars bpm")


def lap_seconds(lap_bars, bpm):
    return lap_bars * BEATS_PER_BAR * 60.0 / bpm


def _nearest_lap(bpm, seconds):
    """The lap whose duration is nearest `seconds`, as a ratio (a lap twice as
    long is as far as one half as long); a tie goes to the calmer lap."""
    return min(ORBIT_LAPS, key=lambda bars: (abs(math.log(lap_seconds(bars, bpm) / seconds)), -bars))


def cruise_lap(bpm):
    """The calm lap for a break or a solo at this tempo."""
    return _nearest_lap(bpm, LAP_SECONDS_TARGET)


def energetic_lap(bpm):
    """A build laps twice as fast as the cruise, never faster than 2 bars."""
    return max(ORBIT_LAPS[0], cruise_lap(bpm) // 2)


def slow_lap(bpm):
    """The floor stem's hats take the slow end, never faster than the cruise."""
    return max(cruise_lap(bpm), _nearest_lap(bpm, SLOW_LAP_SECONDS))


def lap_plan(passage_bars, lap):
    """Consecutive laps filling a passage: as many of `lap` as fit, then twice
    as fast for what is left, never under the shortest lap. A lap never
    outlasts the passage; the speed changes only by x2."""
    while lap > passage_bars and lap >= ORBIT_LAPS[0]:
        lap //= 2                      # the first lap: the passage is shorter than the wanted one
    laps = []
    left = passage_bars
    while lap >= ORBIT_LAPS[0] and left >= lap:
        laps.append(lap)
        left -= lap
        if left < lap:
            lap //= 2                  # into the end: one step faster, and only if that fits
    return laps


def _largest_length(limit, shortest):
    fitting = [bars for bars in BAR_LENGTHS if shortest <= bars <= limit]
    return max(fitting) if fitting else None


def _span(event):
    return event["beat"], event["beat"] + BEATS_PER_BAR * event["bars"]


def _overlap(a, b):
    (a_start, a_end), (b_start, b_end) = _span(a), _span(b)
    return a_start < b_end and b_start < a_end


def _covered_bars(bars, event):
    start = int(event["beat"] // BEATS_PER_BAR)
    return range(start, min(bars.count, start + max(1, int(event["bars"]))))


def _heard(bars, event):
    """Audible in at least half of the bars the event covers."""
    covered = _covered_bars(bars, event)
    heard = sum(1 for bar in covered if passages.audible(bars, event["stem"], bar))
    return len(covered) > 0 and 2 * heard >= len(covered)


def _mean_level(bars, stem, start, end):
    return passages.power_sum_db(bars.level[stem][start:end]) - 10 * math.log10(max(1, end - start))


def _calm_end(passage, found):
    """A calm passage's laps stop where a build or a return begins inside it:
    the build takes over at its own, faster lap, the return gets nothing."""
    inside = [p.start for p in found if p.kind in ("build", "return") and passage.start < p.start < passage.end]
    return min(inside + [passage.end])


def _laps(stem, start_bar, end_bar, lap, priority, index):
    candidates = []
    bar = start_bar
    for bars in lap_plan(end_bar - start_bar, lap):
        candidates.append(Candidate(priority, index, 1, _event(stem, bar * BEATS_PER_BAR, "orbit", bars, False)))
        bar += bars
    return candidates


def _solo_candidates(index, passage, plan):
    if passage.stem not in MOVING_STEMS:
        return []
    return _laps(passage.stem, passage.start, _calm_end(passage, plan.found), cruise_lap(plan.bpm),
                 PRIORITY["solo"], index)


def _highs_candidates(index, passage, plan):
    """The floor stem's one exception: hats alone in a break orbit, slowly."""
    return _laps(FLOOR_STEM, passage.start, _calm_end(passage, plan.found), slow_lap(plan.bpm),
                 PRIORITY["highs"], index)


def _loudest_mover(bars, start, end, among=MOVING_STEMS):
    return max(among, key=lambda s: (_mean_level(bars, s, start, end), -s))


def _break_candidates(index, passage, plan):
    end = _calm_end(passage, plan.found)
    stem = _loudest_mover(plan.bars, passage.start, end)
    return _laps(stem, passage.start, end, cruise_lap(plan.bpm), PRIORITY["break"], index)


def _build_candidates(index, passage, plan):
    """A RISE of the rising mover into the build's end, and the loudest other
    mover lapping twice as fast as the cruise through it."""
    bars = plan.bars
    rises = {s: passages.stem_rise(bars, passage, s) for s in MOVING_STEMS}
    stem = max(MOVING_STEMS, key=lambda s: (rises[s], -s))
    if rises[stem] < passages.BUILD_STEM_RISE_DB:
        return []
    length = _largest_length(min(RISE_MAX_BARS, passage.end - passage.start), 1)
    start = (passage.end - length) * BEATS_PER_BAR
    rise = Candidate(PRIORITY["build"], index, 1, _event(stem, start, "rise", length, False))
    others = tuple(s for s in MOVING_STEMS if s != stem)
    lapping = _loudest_mover(bars, passage.start, passage.end, others)
    return [rise] + _laps(lapping, passage.start, passage.end, energetic_lap(plan.bpm), PRIORITY["build"], index)


def _fill_candidates(index, passage, plan):
    """A PING on the loudest mover, or the next one when that one is busy."""
    by_loudness = sorted(MOVING_STEMS, key=lambda s: (-plan.bars.level[s][passage.start], s))
    beat = passage.start * BEATS_PER_BAR + PING_BEAT_IN_BAR
    return [Candidate(PRIORITY["fill"], index, len(by_loudness), _event(stem, beat, "ping", PING_BARS, False))
            for stem in by_loudness]


CANDIDATE_MAKERS = {"solo": _solo_candidates, "highs": _highs_candidates, "break": _break_candidates,
                    "build": _build_candidates, "fill": _fill_candidates}


def _candidates(plan):
    result = []
    for index, passage in enumerate(plan.found):
        maker = CANDIDATE_MAKERS.get(passage.kind)
        if maker is not None:
            result.extend(maker(index, passage, plan))
    return result


def _stem_may_move(event, kind, bars):
    """Movers always; the floor stem only as an orbit of a highs-only passage,
    and only over bars where it really is highs only (no kick, no bass)."""
    if event["stem"] in MOVING_STEMS:
        return True
    return (event["stem"] == FLOOR_STEM and event["shape"] == "orbit" and kind == "highs"
            and all(passages.highs_only(bars, bar) for bar in _covered_bars(bars, event)))


def _may_move(candidate, found, bars):
    return _stem_may_move(candidate.event, found[candidate.passage].kind, bars)


def _fits_its_stem(event, accepted, bars, length_beats):
    """Inside the track, heard, and clear of the stem's other events."""
    start, end = _span(event)
    if start < 0 or end > length_beats or not _heard(bars, event):
        return False
    return not any(other["stem"] == event["stem"]
                   and (_overlap(other, event) or abs(other["beat"] - event["beat"]) < MIN_GAP_BEATS)
                   for other in accepted)


def _fits(event, accepted, bars, length_beats):
    if not _fits_its_stem(event, accepted, bars, length_beats):
        return False
    return not (event["shape"] == "orbit"
                and any(other["shape"] == "orbit" and _overlap(other, event) for other in accepted))


def _alternate_directions(events):
    """Every lap turns the other way from the one before it."""
    orbits = sorted((e for e in events if e["shape"] == "orbit"), key=lambda e: e["beat"])
    for number, event in enumerate(orbits):
        event["reversed"] = number % 2 == 1


def events_from_passages(found, bars, length_beats, bpm):
    """RULES_V2's decision: candidate events per passage, then the hard rules
    (the floor stem moves only as hats alone, only audible stems, one orbit
    at a time, no two events on a stem closer than MIN_GAP_BEATS or
    overlapping, all inside the track), the stronger passage kind first."""
    plan = Plan(found, bars, bpm)
    candidates = [c for c in _candidates(plan) if _may_move(c, found, bars)]
    chosen = []
    served = set()
    for candidate in sorted(candidates, key=lambda c: (c.priority, c.event["beat"], c.passage)):
        if candidate.alternatives > 1 and candidate.passage in served:
            continue
        if _fits(candidate.event, [c.event for c in chosen], bars, length_beats):
            chosen.append(candidate)
            served.add(candidate.passage)
    events = sorted((c.event for c in chosen), key=lambda e: (e["beat"], e["stem"]))
    _alternate_directions(events)
    return events


def RULES_V2(length_beats, bars, bpm):
    """Content-motivated events from the passages of the track (see
    passages.py): a solo orbits its stem, a break orbits the loudest mover,
    both at the tempo's cruise lap; a build rises the rising mover into its
    end while another mover laps twice as fast; the floor stem's hats alone
    orbit slowly; a fill pings a mover on beat 3; a return gets nothing.
    No passage, no event."""
    return events_from_passages(passages.find_passages(bars), bars, length_beats, bpm)


# --- v3: a role per slot, driven by the passages -----------------------------

# Each moving slot has a role (names do not matter, slots do): the tops
# accent, the music drifts, the vocals mark where they come in. Values are
# first guesses from the pro-DJ's density rule and one hand-made reference
# track, by role and rate only; the tests pin the behaviour, not the numbers.
TOPS, MUSIC, VOCALS = 1, 2, 3
ROLE_PHRASE_BARS = 8              # the phrase the roles count in (fills are looked for per 8 too)
ACCENT_BARS = 1                   # a SCAT accent: one bar, from the bar line
TOPS_TURN_EVERY_PHRASES = 2       # under the groove: the tops accent the last bar of every second phrase
MUSIC_REST_LAPS = 1               # under the groove: the music laps once slowly, then rests as long
VOCAL_ENTRY_BARS = 4              # a vocal coming in rises (over the groove) or orbits (the floor out);
                                  # a shorter shot gets a ping, or a one-bar scat
VOCAL_SCAT_EVERY_PHRASES = 2      # while it sings on: a one-bar scat every second phrase
BREAK_SCAT_BARS = 4               # in a break the second mover scatters this long, each phrase
PING_SPACING_BARS = 16            # pings are few: at most one in this many bars
MAX_ORBITS_AT_ONCE = 2
DENSITY_WINDOW_SECONDS = 30.0     # no more than DENSITY_MAX_PER_WINDOW events start in any
DENSITY_MAX_PER_WINDOW = 6        # such window: 12 a minute at a break's peak, far fewer in the groove
REVERSE_EVERY_ORBITS = 4          # the second orbit of every four turns back, the rest go forward
ACCENT_STEMS = (TOPS, VOCALS)     # who takes a turn's scat, in this order
PING_STEMS = (VOCALS, TOPS)       # who takes a turn's ping, in this order: a tops ping takes the
                                  # place of the tops' scat
# A drum gap (passages.find_gaps, 1-4 beats): accents, not new orbits on everything.
GAP_ACCENT_LENGTHS = (0.5, 1)     # bars; the longest not outrunning the gap by more than ...
GAP_OVERRUN_BEATS = 1             # ... this many beats: 1-2 beats get half a bar, 3-4 beats a bar
GAP_VOCAL_DELAY_BEATS = 1         # the vocals answer on the gap's second beat (or the beat after it)
GAP_VOCAL_BARS = 0.5
GAP_GROUP_EVERY_BARS = 4          # at most one gap's group of events in this many bars: a fill of
                                  # gaps gets one answer, not one per gap
# The stronger reason wins a place first. A vocal coming in is one moment and
# goes before the break's group, which has the whole break to move in. A gap's
# accents go before the phrase-end scats; the music starts an orbit in a gap
# only when nothing else wants it.
ROLE_PRIORITY = {"build": 0, "highs": 1, "entry": 2, "break": 3, "turn": 4, "groove": 5, "gap": 6,
                 "phrase": 7, "present": 8, "gap-orbit": 9}
GAP_KINDS = ("gap", "gap-orbit")

# One wish per event; of the wishes sharing a group, only the first that fits
# is taken. Wishes of one priority are tried bar by bar, in the order made.
Wish = namedtuple("Wish", "priority kind group event")
RolePlan = namedtuple("RolePlan", "found bars bpm offset")


def _phrase_starts(plan, start, end):
    """Bars in [start, end) where a phrase starts."""
    first = start + (plan.offset - start) % ROLE_PHRASE_BARS
    return list(range(first, end, ROLE_PHRASE_BARS))


def _wishes_of(candidates, kind):
    return [Wish(ROLE_PRIORITY[kind], kind, None, c.event) for c in candidates]


def _floor_out(plan, bar):
    return any(p.kind in ("break", "dropout") and p.start <= bar < p.end for p in plan.found)


def _ping(stem, bar):
    return _event(stem, bar * BEATS_PER_BAR + PING_BEAT_IN_BAR, "ping", PING_BARS, False)


def _scat(stem, bar, bars=ACCENT_BARS):
    return _event(stem, bar * BEATS_PER_BAR, "scat", bars, False)


def _turn_wishes(index, bar, kind):
    """A turn (a dropout or a fill): a ping on beat 3 when the pings allow,
    and a one-bar scat from its bar line."""
    priority = ROLE_PRIORITY["turn"]
    pings = [Wish(priority, kind, (index, "ping"), _ping(s, bar)) for s in PING_STEMS]
    return pings + [Wish(priority, kind, (index, "scat"), _scat(s, bar)) for s in ACCENT_STEMS]


def _break_wishes(index, passage, plan):
    """The loudest mover laps (the music at its slow lap, any other at the
    cruise); the next one scatters each phrase; on the break's last bar a
    mover that is not lapping takes a one-bar accent."""
    end = _calm_end(passage, plan.found)
    if end <= passage.start:
        return []
    bars = plan.bars
    by_loudness = sorted(MOVING_STEMS, key=lambda s: (-_mean_level(bars, s, passage.start, end), s))
    lapping = by_loudness[0]
    lap = slow_lap(plan.bpm) if lapping == MUSIC else cruise_lap(plan.bpm)
    priority = ROLE_PRIORITY["break"]
    wishes = _wishes_of(_laps(lapping, passage.start, end, lap, priority, index), "break")
    others = [s for s in by_loudness if s != lapping]
    for start in range(passage.start, end, ROLE_PHRASE_BARS):
        length = _largest_length(min(BREAK_SCAT_BARS, end - start), 1)
        wishes += [Wish(priority, "break", (index, start), _scat(s, start, length)) for s in others if length]
    accent = [s for s in ACCENT_STEMS + (MUSIC,) if s != lapping]
    return wishes + [Wish(priority, "break", (index, "last"), _scat(s, passage.end - 1)) for s in accent]


def _entry_wishes(index, passage, plan):
    """A vocal coming in for VOCAL_ENTRY_BARS or longer: an orbit at the
    cruise where the floor is out, a rise over the groove, that long; then a
    one-bar scat now and then while it sings on. A shorter shot: a ping on
    beat 3 when the pings allow, else a one-bar scat."""
    if passage.stem != VOCALS:
        return []
    priority = ROLE_PRIORITY["entry"]
    start = passage.start
    if passage.end - start < VOCAL_ENTRY_BARS:
        return [Wish(priority, "entry", index, _ping(VOCALS, start)),
                Wish(priority, "entry", index, _scat(VOCALS, start))]
    if _floor_out(plan, start):
        wishes = _wishes_of(_laps(VOCALS, start, start + VOCAL_ENTRY_BARS, cruise_lap(plan.bpm), priority, index),
                            "entry")
    else:
        wishes = [Wish(priority, "entry", None, _event(VOCALS, start * BEATS_PER_BAR, "rise", VOCAL_ENTRY_BARS, False))]
    starts = _phrase_starts(plan, passage.start + VOCAL_ENTRY_BARS, passage.end)[::VOCAL_SCAT_EVERY_PHRASES]
    return wishes + [Wish(ROLE_PRIORITY["present"], "present", None, _scat(VOCALS, bar)) for bar in starts]


def _groove_wishes(index, passage, plan):
    """Under the groove, from its second phrase: the music laps slowly, one
    lap and a rest as long; the tops accent the last bar of every second
    phrase."""
    wishes = []
    lap = slow_lap(plan.bpm)
    starts = _phrase_starts(plan, passage.start + 1, passage.end)
    bar = starts[0] if starts else passage.end
    while bar < passage.end:
        for bars in lap_plan(passage.end - bar, lap)[:1]:
            wishes.append(Wish(ROLE_PRIORITY["groove"], "groove", None,
                               _event(MUSIC, bar * BEATS_PER_BAR, "orbit", bars, False)))
        bar += lap * (1 + MUSIC_REST_LAPS)
    for start in _phrase_starts(plan, passage.start + 1, passage.end + 1):
        if ((start - plan.offset) // ROLE_PHRASE_BARS) % TOPS_TURN_EVERY_PHRASES == 0:
            wishes.append(Wish(ROLE_PRIORITY["phrase"], "phrase", None, _scat(TOPS, start - 1)))
    return wishes


def _build_wishes(index, passage, plan):
    return _wishes_of(_build_candidates(index, passage, plan), "build")


def _highs_wishes(index, passage, plan):
    return _wishes_of(_highs_candidates(index, passage, plan), "highs")


def _gap_accent_bars(gap):
    beats = gap.end - gap.start
    return max(bars for bars in GAP_ACCENT_LENGTHS if bars * BEATS_PER_BAR <= beats + GAP_OVERRUN_BEATS)


def _accent_wishes(group, stem, beat, bars):
    """A CROSS if the pings allow, else a scat: one of the two."""
    return [Wish(ROLE_PRIORITY["gap"], "gap", group, _event(stem, beat, shape, bars, False))
            for shape in ("ping", "scat")]


def _gap_wishes(index, gap, plan):
    """For each mover heard in the gap: the tops accent it from its first
    beat; the vocals answer GAP_VOCAL_DELAY_BEATS later; the music, if it is
    not orbiting across the gap already, may start one slow lap there."""
    heard = [s for s in MOVING_STEMS if passages.audible_during(plan.bars, s, gap.start, gap.end)]
    wishes = []
    if TOPS in heard:
        wishes += _accent_wishes((index, TOPS), TOPS, gap.start, _gap_accent_bars(gap))
    if VOCALS in heard:
        wishes += _accent_wishes((index, VOCALS), VOCALS, gap.start + GAP_VOCAL_DELAY_BEATS, GAP_VOCAL_BARS)
    if MUSIC in heard:
        wishes.append(Wish(ROLE_PRIORITY["gap-orbit"], "gap-orbit", (index, MUSIC),
                           _event(MUSIC, gap.start, "orbit", slow_lap(plan.bpm), False)))
    return wishes


def _gap_group_too_close(wish, gaps, answered):
    """Another gap within GAP_GROUP_EVERY_BARS already got its answer."""
    if wish.kind not in GAP_KINDS:
        return False
    index = wish.group[0]
    return any(other != index and abs(gaps[other].start - gaps[index].start) < GAP_GROUP_EVERY_BARS * BEATS_PER_BAR
               for other in answered)


ROLE_MAKERS = {"build": _build_wishes, "highs": _highs_wishes, "break": _break_wishes, "entry": _entry_wishes,
               "groove": _groove_wishes,
               "dropout": lambda index, passage, plan: _turn_wishes(index, passage.start, "turn"),
               "fill": lambda index, passage, plan: _turn_wishes(index, passage.start, "turn")}


def _orbits_at_once(event, accepted):
    """The most orbits running together anywhere in `event`'s span, itself included."""
    if event["shape"] != "orbit":
        return 0
    start, end = _span(event)
    others = [_span(o) for o in accepted if o["shape"] == "orbit" and _overlap(o, event)]
    points = [start] + [s for s, _ in others if start < s < end]
    return 1 + max(sum(1 for s, e in others if s <= point < e) for point in points)


def _crowded(beat, accepted, window):
    """Would one more event at `beat` put more than DENSITY_MAX_PER_WINDOW
    starts into some window of `window` beats?"""
    beats = sorted([e["beat"] for e in accepted] + [beat])
    return any(sum(1 for b in beats if start <= b < start + window) > DENSITY_MAX_PER_WINDOW
               for start in beats if start <= beat < start + window)


def _fits_the_roles(wish, accepted, plan, length_beats, return_ones):
    event = wish.event
    if event["beat"] in return_ones or not _stem_may_move(event, wish.kind, plan.bars):
        return False
    if not _fits_its_stem(event, accepted, plan.bars, length_beats):
        return False
    if _orbits_at_once(event, accepted) > MAX_ORBITS_AT_ONCE:
        return False
    if event["shape"] == "ping" and any(o["shape"] == "ping" and abs(o["beat"] - event["beat"])
                                        < PING_SPACING_BARS * BEATS_PER_BAR for o in accepted):
        return False
    return not _crowded(event["beat"], accepted, DENSITY_WINDOW_SECONDS * plan.bpm / 60.0)


def _mostly_forward(events):
    """The second orbit of every REVERSE_EVERY_ORBITS turns back."""
    orbits = sorted((e for e in events if e["shape"] == "orbit"), key=lambda e: (e["beat"], e["stem"]))
    for number, event in enumerate(orbits):
        event["reversed"] = number % REVERSE_EVERY_ORBITS == 1


def events_from_roles(found, bars, length_beats, bpm, gaps=()):
    """RULES_V3's decision: the wishes of every passage and drum gap, the
    stronger reason first, each taken if it keeps the hard rules (v2's, and:
    at most MAX_ORBITS_AT_ONCE orbits together, pings PING_SPACING_BARS
    apart, the density cap, nothing on the one of a return, one gap's group
    in GAP_GROUP_EVERY_BARS)."""
    plan = RolePlan(found, bars, bpm, passages.phrase_offset(found, ROLE_PHRASE_BARS))
    wishes = []
    for index, passage in enumerate(found):
        maker = ROLE_MAKERS.get(passage.kind)
        if maker is not None:
            wishes.extend(maker(index, passage, plan))
    for index, gap in enumerate(gaps):
        wishes.extend(_gap_wishes(index, gap, plan))
    return_ones = {p.start * BEATS_PER_BAR for p in found if p.kind == "return"}
    accepted = []
    taken_groups = set()
    answered_gaps = set()
    for wish in sorted(wishes, key=lambda w: (w.priority, w.event["beat"] // BEATS_PER_BAR)):
        if wish.group is not None and (wish.kind, wish.group) in taken_groups:
            continue
        if _gap_group_too_close(wish, gaps, answered_gaps):
            continue
        if _fits_the_roles(wish, accepted, plan, length_beats, return_ones):
            accepted.append(wish.event)
            if wish.group is not None:
                taken_groups.add((wish.kind, wish.group))
            if wish.kind in GAP_KINDS:
                answered_gaps.add(wish.group[0])
    events = sorted(accepted, key=lambda e: (e["beat"], e["stem"]))
    _mostly_forward(events)
    return events


def RULES_V3(length_beats, bars, bpm):
    """Roles per slot from the passages and grooves of the track (see
    passages.find_all_passages): the tops accent the turns, the music laps
    slowly under the groove, a vocal rises or orbits where it comes in, a
    break moves two or three stems together, a build as in v2."""
    return events_from_roles(passages.find_all_passages(bars), bars, length_beats, bpm, passages.find_gaps(bars))


RULE_SETS = {"v1": RULES_V1, "v2": RULES_V2, "v3": RULES_V3}
NEEDS_ANALYSIS = {"v2", "v3"}
PASSAGE_FINDERS = {"v2": passages.find_passages, "v3": passages.find_all_passages}


def _seed(set_name, index):
    digest = hashlib.sha256(f"{set_name}|{index}".encode("utf-8")).digest()
    return str(int.from_bytes(digest[:8], "big"))


def _thin_out(events, kept_events):
    """Drops what would crowd a stem (under MIN_GAP_BEATS from an earlier
    event) or sit on a slot `kept_events` hold."""
    taken = list(kept_events)
    result = []
    for event in sorted(events, key=lambda e: (e["beat"], e["stem"])):
        crowded = any(other["stem"] == event["stem"]
                      and abs(other["beat"] - event["beat"]) < MIN_GAP_BEATS
                      for other in taken)
        if crowded:
            continue
        taken.append(event)
        result.append(event)
    return result


def prepare_set(first_stem_file, grid, length_beats, rules, kept_events=(), bars=None):
    """The prepared events for one set, seeds from the set's name and the
    event's index, so a rerun is byte-identical. `kept_events` (the hand
    events) keep their slots."""
    if grid.bpm <= 0:
        return []
    name = events_file_for(first_stem_file).name[:-len(EVENTS_SUFFIX)]
    candidates = rules(length_beats, bars, grid.bpm)
    for index, event in enumerate(candidates):
        event["seed"] = _seed(name, index)
    room = max(0, MAX_EVENTS - len(kept_events))
    return _thin_out(candidates, kept_events)[:room]


# --- how the app names files and keys its cache -----------------------------

def is_ni_stem_file(name):
    return name.lower().endswith(NI_SUFFIX)


def events_file_for(first_stem_file):
    """stemevents::fileFor: beside the first stem file, an NI file with its
    whole .stem.mp4 extension dropped."""
    path = Path(first_stem_file)
    base = path.name[:-len(NI_SUFFIX)] if is_ni_stem_file(path.name) else path.stem
    return path.with_name(base + EVENTS_SUFFIX)


def juce_hex_hash(text):
    """String::toHexString (String::hashCode64()): 101 * h + code point."""
    value = 0
    for character in text:
        value = (value * 101 + ord(character)) & 0xFFFFFFFFFFFFFFFF
    return format(value, "x")


def cache_key(files):
    """AnalysisCache::keyFor: name, size and modification time (ms) of every
    slot's file; an empty slot is an empty name, size 0 and time 0."""
    key = ""
    for file in files:
        if file is None:
            key += "|0|0;"
            continue
        info = Path(file).stat()
        key += f"{Path(file).name}|{info.st_size}|{(info.st_mtime_ns // 1_000_000_000) * 1000};"
    return juce_hex_hash(key)


class AnalysisCache:
    def __init__(self, path):
        self.by_key = {}
        try:
            root = ElementTree.parse(path).getroot()
        except (OSError, ElementTree.ParseError):
            return
        for element in root.iter("SET"):
            self.by_key[element.get("key")] = element

    def grid_of(self, key):
        """The corrected grid if there is one, else the analysed one. Only the
        exact key counts: names are not unique across folders, and a changed
        file is another track."""
        element = self.by_key.get(key)
        if element is None:
            return None
        bpm_name, beat_name = (("correctedBpm", "correctedFirstBeat")
                               if element.get("correctedBpm") is not None else ("bpm", "firstBeat"))
        try:
            grid = Grid(float(element.get(bpm_name)), float(element.get(beat_name)))
        except (TypeError, ValueError):
            return None
        return grid if grid.bpm > 0 else None


# --- finding the sets -------------------------------------------------------

def split_stem_name(base):
    """StemNames.cpp splitStemName: (prefix, suffix) or None."""
    split_at = max(base.rfind(c) for c in SEPARATORS)
    if split_at <= 0 or split_at == len(base) - 1:
        return None
    prefix = base[:split_at].rstrip(SEPARATORS)
    suffix = base[split_at + 1:]
    if not prefix:
        return None

    def is_number(text):
        return 0 < len(text) <= 2 and text.isdigit()

    def dash_between(start, end):
        return "-" in base[start:end]

    if not is_number(suffix) and dash_between(len(prefix), split_at + 1):
        number_at = max(prefix.rfind(c) for c in SEPARATORS)
        if number_at >= 0 and is_number(prefix[number_at + 1:]):
            title = prefix[:number_at].rstrip(SEPARATORS)
            if title and dash_between(len(title), number_at + 1):
                suffix = base[len(title):].lstrip(SEPARATORS)
                prefix = title
    return prefix, suffix


def numbered_slot(suffix):
    if len(suffix) < 3 or suffix[0] not in "1234" or suffix[1] not in " -_.":
        return None
    if all(c in " -_." for c in suffix[1:]):
        return None
    return int(suffix[0]) - 1


def natural_key(text):
    return [int(part) if part.isdigit() else part.lower() for part in re.split(r"(\d+)", text)]


def group_stem_files(base_names):
    """StemNames.cpp groupStemFiles: [(name, [index or None] * 4)]."""
    sets = []
    groups = {}
    for index, base in enumerate(base_names):
        split = split_stem_name(base)
        if split is None:
            sets.append((base, [index, None, None, None]))
        else:
            groups.setdefault(split[0], []).append((index, split[1]))
    for prefix, members in groups.items():
        if len(members) == 4:
            ordered = sorted(members, key=lambda m: natural_key(m[1]))
            sets.append((prefix, [m[0] for m in ordered]))
            continue
        slots = [None] * STEMS
        numbered = 2 <= len(members) <= 3
        for index, suffix in members if numbered else ():
            slot = numbered_slot(suffix)
            if slot is None or slots[slot] is not None:
                numbered = False
                break
            slots[slot] = index
        if numbered:
            sets.append((prefix, slots))
        else:
            sets.extend((base_names[index], [index, None, None, None]) for index, _ in members)
    return sets


class StemSet:
    def __init__(self, name, files):
        self.name = name
        self.files = files    # four paths or None; an NI set holds its file four times

    @property
    def first_file(self):
        return next(file for file in self.files if file is not None)


def _in_ignored_folder(file, library):
    return "originals" in file.parent.relative_to(library).parts


def find_sets(library):
    library = Path(library)
    by_folder = {}
    ni_files = []
    for file in sorted(library.rglob("*")):
        if not file.is_file() or _in_ignored_folder(file, library):
            continue
        if is_ni_stem_file(file.name):
            ni_files.append(file)
        elif file.suffix.lower() in AUDIO_EXTENSIONS:
            by_folder.setdefault(file.parent, []).append(file)

    sets = []
    for files in by_folder.values():
        for name, slots in group_stem_files([file.stem for file in files]):
            sets.append(StemSet(name, [files[i] if i is not None else None for i in slots]))
    for file in ni_files:
        sets.append(StemSet(file.name[:-len(NI_SUFFIX)], [file] * STEMS))
    return sorted(sets, key=lambda s: (str(s.first_file.parent), natural_key(s.name)))


# --- the track's length -----------------------------------------------------

def _wav_seconds(data, file_size):
    if data[:4] not in (b"RIFF", b"RF64") or data[8:12] != b"WAVE":
        return None
    rate = block_align = None
    at = 12
    while at + 8 <= len(data):
        chunk, size = data[at:at + 4], struct.unpack("<I", data[at + 4:at + 8])[0]
        if chunk == b"fmt " and at + 20 <= len(data):
            rate = struct.unpack("<I", data[at + 12:at + 16])[0]
            block_align = struct.unpack("<H", data[at + 20:at + 22])[0]
        if chunk == b"data":
            available = file_size - (at + 8)
            if size == STREAMED_WAV_SIZE or size > available:
                size = max(0, available)   # streamed or cut short: what the file holds
            return size / (rate * block_align) if rate and block_align else None
        at += 8 + size + (size & 1)
    return None


def _flac_seconds(data):
    if data[:4] != b"fLaC" or len(data) < 26:
        return None
    packed = int.from_bytes(data[18:26], "big")   # rate:20 channels:3 bits:5 samples:36
    rate, samples = packed >> 44, packed & ((1 << 36) - 1)
    return samples / rate if rate and samples else None


def _mvhd_seconds(data):
    at = 0
    path = [b"moov", b"mvhd"]
    end = len(data)
    while path and at + 8 <= end:
        size = struct.unpack(">I", data[at:at + 4])[0]
        kind = data[at + 4:at + 8]
        if size < 8:
            return None
        if kind == path[0]:
            path.pop(0)
            if not path:
                version = data[at + 8]
                if version == 1:
                    scale, duration = struct.unpack(">IQ", data[at + 28:at + 40])
                else:
                    scale, duration = struct.unpack(">II", data[at + 20:at + 28])
                return duration / scale if scale else None
            at += 8
            continue
        at += size
    return None


def track_seconds(file):
    """From the header, like the app's reader; None for a format not read."""
    file = Path(file)
    with open(file, "rb") as handle:
        if is_ni_stem_file(file.name):
            return _mvhd_seconds(_read_moov(handle))
        data = handle.read(1 << 16)
    return _wav_seconds(data, file.stat().st_size) or _flac_seconds(data)


def _read_moov(handle):
    """The bytes of the file's moov box (the audio is seeked past)."""
    handle.seek(0, os.SEEK_END)
    total = handle.tell()
    at = 0
    while at + 8 <= total:
        handle.seek(at)
        header = handle.read(16)
        size, kind = struct.unpack(">I", header[:4])[0], header[4:8]
        if size == 1:
            size = struct.unpack(">Q", header[8:16])[0]
        if size == 0:
            size = total - at
        if size < 8:
            break
        if kind == b"moov":
            handle.seek(at)
            return handle.read(size)
        at += size
    return b""


# --- reading and writing the events file -----------------------------------

def _valid_event(item):
    if not isinstance(item, dict) or any(name not in item for name in FIELD_ORDER):
        return False
    beat, seed, stem = item["beat"], item["seed"], item["stem"]
    if isinstance(beat, bool) or not isinstance(beat, (int, float)) or beat != beat or abs(beat) == float("inf"):
        return False
    if not isinstance(seed, str) or not seed.isascii() or not seed.isdigit() or len(seed) > 20 or int(seed) >= 2 ** 64:
        return False
    return (isinstance(stem, int) and not isinstance(stem, bool) and 0 <= stem < STEMS
            and item["shape"] in SHAPES and item["origin"] in ("hand", "prepared")
            and isinstance(item["bars"], (int, float)) and item["bars"] in BAR_LENGTHS
            and isinstance(item["reversed"], bool) and isinstance(item["active"], bool))


def read_events(path):
    """(status, events): missing | ok | damaged | newer."""
    if not path.exists():
        return "missing", []
    try:
        root = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return "damaged", []
    if not isinstance(root, dict) or "version" not in root or "events" not in root:
        return "damaged", []
    version = root["version"]
    if isinstance(version, bool) or not isinstance(version, int):
        return "damaged", []
    if version > FILE_VERSION:
        return "newer", []
    if version < 1 or not isinstance(root["events"], list):
        return "damaged", []
    if not all(_valid_event(item) for item in root["events"]):
        return "damaged", []   # the app would drop some; the tool leaves the file alone
    return "ok", root["events"]


def _ordered(event):
    return {name: event[name] for name in FIELD_ORDER}


def document_for(events):
    ordered = sorted(events, key=lambda e: (e["beat"], e["stem"]))
    return json.dumps({"version": FILE_VERSION, "events": [_ordered(e) for e in ordered]}, indent=1) + "\n"


def write_atomically(path, text):
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text(text, encoding="utf-8")
    os.replace(temporary, path)


# --- the tool ---------------------------------------------------------------

Options = namedtuple("Options", "rules dry_run replace explain ffmpeg")


def analysis_file_for(first_stem_file):
    """Beside the events file, with the same name."""
    events = events_file_for(first_stem_file)
    return events.with_name(events.name[:-len(EVENTS_SUFFIX)] + stem_analysis.ANALYSIS_SUFFIX)


def _content_of(stem_set, grid, length_beats, options, say):
    """passages.Bars of the set, from the stored analysis or decoded now (and
    stored, unless this is a dry run); None, reported, when it cannot be had."""
    try:
        windows, _ = stem_analysis.analyse(stem_set.files, analysis_file_for(stem_set.first_file),
                                           cache_key(stem_set.files), options.ffmpeg, write=not options.dry_run)
    except stem_analysis.AnalysisError as error:
        say(f"skipped {stem_set.name}: {error}; no analysis, nothing written (--rules v1 needs none)")
        return None
    return stem_analysis.bars_from_windows(windows, grid.bpm, grid.first_beat, int(length_beats // BEATS_PER_BAR))


def _bar_label(beat):
    bar, in_bar = divmod(beat, BEATS_PER_BAR)
    return f"bar {int(bar) + 1}" + (f" beat {in_bar + 1:g}" if in_bar else "")


def _passage_line(passage):
    where = (f"bar {passage.start + 1}" if passage.end - passage.start == 1
             else f"bars {passage.start + 1}-{passage.end}")
    stem = f" stem {passage.stem}" if passage.stem is not None else ""
    note = " (floor stem highs only)" if passage.kind == "highs" else ""
    return f"  {passage.kind:<7} {where}{stem}{note}"


def _event_line(event, bpm):
    length = f"{event['bars']:g} bar" + ("" if event["bars"] == 1 else "s")
    if event["shape"] == "orbit" and bpm:
        length += f" = {lap_seconds(event['bars'], bpm):.1f} s/lap"
    turn = ", reversed" if event["shape"] == "orbit" and event["reversed"] else ""
    return (f"  -> {event['shape']} stem {event['stem']} beat {event['beat']:g} "
            f"({_bar_label(event['beat'])}) {length}{turn}")


def explanation(found, events, bpm=None):
    """What the tool found (bars counted from 1, as a DJ counts) and what it
    makes of it, one line each."""
    lines = []
    if found is not None:
        lines = [_passage_line(p) for p in found] or ["  no passage found"]
    return lines + [_event_line(e, bpm) for e in events]


def prepare_library_set(stem_set, cache, options, say):
    file = events_file_for(stem_set.first_file)
    grid = cache.grid_of(cache_key(stem_set.files))
    if grid is None:
        say(f"skipped {stem_set.name}: no grid (not analysed in the app yet, or the file changed since)")
        return
    seconds = track_seconds(stem_set.first_file)
    if not seconds:
        say(f"skipped {stem_set.name}: length not readable from the header")
        return
    status, existing = read_events(file)
    if status in ("damaged", "newer"):
        say(f"skipped {stem_set.name}: {status} events file, left alone ({file.name})")
        return
    if existing and not options.replace:
        say(f"kept: {stem_set.name} already has events; use --replace to redo the prepared ones")
        return

    length_beats = (seconds - grid.first_beat) * grid.bpm / 60.0
    bars = found = None
    if options.rules in NEEDS_ANALYSIS:
        bars = _content_of(stem_set, grid, length_beats, options, say)
        if bars is None:
            return
        found = PASSAGE_FINDERS[options.rules](bars)
    hand = [e for e in existing if e["origin"] == "hand"]
    prepared = prepare_set(stem_set.first_file, grid, length_beats, RULE_SETS[options.rules], hand, bars)
    explain = options.explain or options.dry_run

    def report(line):
        say(line)
        for detail in explanation(found, prepared, grid.bpm) if explain else ():
            say(detail)

    if not prepared and not existing:
        report(f"{stem_set.name}: nothing to prepare")
        return
    text = document_for(hand + prepared)
    if status == "ok" and file.read_text(encoding="utf-8") == text:
        report(f"{stem_set.name}: unchanged ({len(hand)} hand, {len(prepared)} prepared)")
        return
    verb = "would write" if options.dry_run else "wrote"
    report(f"{stem_set.name}: {verb} {file.name} ({len(hand)} hand kept, {len(prepared)} prepared)")
    if not options.dry_run:
        write_atomically(file, text)


HELP = """\
Stems: the first slot is the floor (drums and bass, the low end) and stays
put; the other three (tops, music, vocals or the like) move. Names do not
matter, slots do. The one exception: where the floor stem plays only highs
(hats, no kick, no bass) inside a break, it may orbit, slowly.

Rules:
  v3 (default)  gives each moving slot a role, from the same analysis and
                passages as v2 and three more findings: a GROOVE (the floor
                playing, between the breaks), a DROPOUT (the floor out for one
                to three bars: a turn) and an ENTRY (a mover coming in after a
                rest, for as long as it stays). Phrases (8 bars) are counted
                from where the floor returns after a break, so a pickup bar
                before the grid's one does not shift them.
                Slot 1, the tops: a one-bar SCAT on the last bar of every
                second phrase (8 bars) of a groove and on each dropout or
                fill, now and then a PING on beat 3 there (at most one in 16
                bars, the vocal first, a tops ping in place of the tops'
                scat). Slot 2, the music: under the groove, from its second
                phrase, one slow lap (the tempo's slow end, 8 bars at 145
                BPM), then a rest as long. Slot 3, the vocals: where they come
                in for 4 bars or more, a 4-bar RISE over the groove or a 4-bar
                ORBIT where the floor is out, then a one-bar SCAT every second
                phrase while they sing; a shorter vocal shot gets a PING on
                beat 3 (or a one-bar SCAT). A break moves two or three stems
                together: the loudest mover laps, the next one scatters each
                phrase, another one accents the break's last bar. Builds and
                the floor's hats as in v2.
                Hard rules as in v2, and: at most two stems orbit at once, at
                most 6 events start in any 30 s, nothing starts on the one
                where the floor returns, orbits run forward except the second
                of every four. A steady track still gets the groove's roles.
  v2            moves what the music does. Each set's four stems are decoded
                once by ffmpeg (loudness and the band above 4 kHz, every
                0.1 s; stored as <name>.a3analysis.json beside the set so a
                rerun decodes nothing) and laid on the deck's beat grid. The
                tool then looks for passages, per bar: a SOLO (one mover
                carries the mix, the floor out), a BREAK (the floor far below
                its usual level), HIGHS (the floor stem plays only hats in a
                break), a BUILD (louder and brighter, bar after bar), the
                RETURN of the floor, and a FILL (an onset spike in the last
                bar of 8).
                Events: a solo orbits its stem and a break the loudest mover,
                lap after lap at the tempo's cruise; a build rises the rising
                mover into its end while another mover laps twice as fast; the
                floor's hats orbit at the slow end; a fill sends a mover
                across the room (CROSS, "ping" in the file) on beat 3; a
                return gets nothing. Only a stem you can hear moves; one
                orbit runs at a time; every lap turns the other way. A track
                with nothing to find gets no events at all.
  v1            counts bars only: an orbit every 16 bars, a CROSS before it.

Speed: an orbit's bars are its length and its speed (one lap in that many
bars). The cruise lap is 2, 4 or 8 bars, whichever lasts nearest 9 s at the
track's tempo (60 BPM: 2, 80-140 BPM: 4, 170 BPM: 8); a build laps twice as
fast (never under 2 bars), the floor's hats nearer 12 s. Laps never outlast
their passage; into its end a lap may get twice as fast once, never by any
other step. 0.5 and 1 bar are left to CROSS.

Needs ffmpeg for v2 and v3 (one decode at a time, at the lowest CPU priority). A set
that cannot be decoded is skipped and reported; nothing is written for it.
--dry-run writes nothing at all, not even the analysis (it reads one that is
there); it prints what was found and the events it would write, with the
seconds per lap. --explain prints the same during a real run. Bars in that
report count from 1.
"""


def main(argv):
    parser = argparse.ArgumentParser(prog="stemdeck-prepare-events",
                                     description="Write prepared event files for every set in a library.",
                                     epilog=HELP, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("library", type=Path)
    parser.add_argument("--dry-run", action="store_true",
                        help="write nothing (no events, no analysis), print what would be done and why")
    parser.add_argument("--explain", action="store_true", help="print the passages found and the events, per set")
    parser.add_argument("--replace", action="store_true",
                        help="redo the prepared events of a file that has events (hand events are kept)")
    parser.add_argument("--only", metavar="SET", help="only the set with this name")
    parser.add_argument("--rules", choices=sorted(RULE_SETS), default="v3", help="see below (default: v3)")
    parser.add_argument("--cache", type=Path, default=DEFAULT_CACHE, help="the app's analysis cache")
    parser.add_argument("--ffmpeg", default="ffmpeg", help="the ffmpeg to decode with (default: from PATH)")
    args = parser.parse_args(argv)

    if not args.library.is_dir():
        print(f"not a folder: {args.library}", file=sys.stderr)
        return 2
    cache = AnalysisCache(args.cache)
    sets = [s for s in find_sets(args.library) if args.only in (None, s.name)]
    if args.only and not sets:
        print(f"no set called {args.only}", file=sys.stderr)
        return 1
    options = Options(args.rules, args.dry_run, args.replace, args.explain, args.ffmpeg)
    for stem_set in sets:
        prepare_library_set(stem_set, cache, options, print)
    return 0
