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
StemDeck is never touched."""

import argparse
import hashlib
import json
import os
import re
import struct
import sys
import xml.etree.ElementTree as ElementTree
from collections import namedtuple
from pathlib import Path

Grid = namedtuple("Grid", "bpm first_beat")

FILE_VERSION = 1
MAX_EVENTS = 512
SLOT_TOLERANCE = 1e-6
BEATS_PER_BAR = 4
MIN_GAP_BEATS = 2.0           # per stem, from the pro-DJ density rule
STEMS = 4
FIELD_ORDER = ("stem", "beat", "shape", "bars", "reversed", "seed", "active", "origin")
SHAPES = ("orbit", "ping", "rise", "scat")
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
PING_BEAT_IN_BAR = 2          # beat 3 of the bar, counted from 0


def _event(stem, beat, shape, bars, reversed_):
    return {"stem": stem, "beat": float(beat), "shape": shape, "bars": bars,
            "reversed": reversed_, "active": True, "origin": "prepared"}


def RULES_V1(length_beats):
    """Candidate events (no seed yet) for a track of `length_beats` beats.

    From the second phrase on, an ORBIT of 4 bars on each phrase start, the
    stems taking turns and the direction alternating per phrase. On beat 3 of
    the last bar of every phrase a PING on a stem that is not about to orbit.
    Sparse on purpose: two events a phrase."""
    phrase_beats = PHRASE_BARS * BEATS_PER_BAR
    events = []
    phrase = 0
    while (phrase + 1) * phrase_beats <= length_beats:
        orbiting_next = phrase % STEMS
        ping_beat = (phrase + 1) * phrase_beats - BEATS_PER_BAR + PING_BEAT_IN_BAR
        events.append(_event((orbiting_next + 2) % STEMS, ping_beat, "ping", PING_BARS, False))
        next_start = (phrase + 1) * phrase_beats
        if next_start + ORBIT_BARS * BEATS_PER_BAR <= length_beats:
            events.append(_event(orbiting_next, next_start, "orbit", ORBIT_BARS, phrase % 2 == 1))
        phrase += 1
    return events


RULE_SETS = {"v1": RULES_V1}


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


def prepare_set(first_stem_file, grid, length_beats, rules, kept_events=()):
    """The prepared events for one set, seeds from the set's name and the
    event's index, so a rerun is byte-identical. `kept_events` (the hand
    events) keep their slots."""
    if grid.bpm <= 0:
        return []
    name = events_file_for(first_stem_file).name[:-len(EVENTS_SUFFIX)]
    candidates = rules(length_beats)
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

def prepare_library_set(stem_set, cache, rules, dry_run, say, replace=False):
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
    if existing and not replace:
        say(f"kept: {stem_set.name} already has events; use --replace to redo the prepared ones")
        return

    hand = [e for e in existing if e["origin"] == "hand"]
    length_beats = (seconds - grid.first_beat) * grid.bpm / 60.0
    prepared = prepare_set(stem_set.first_file, grid, length_beats, rules, hand)
    if not prepared and not existing:
        say(f"{stem_set.name}: nothing to prepare")
        return

    text = document_for(hand + prepared)
    if status == "ok" and file.read_text(encoding="utf-8") == text:
        say(f"{stem_set.name}: unchanged ({len(hand)} hand, {len(prepared)} prepared)")
        return
    verb = "would write" if dry_run else "wrote"
    say(f"{stem_set.name}: {verb} {file.name} ({len(hand)} hand kept, {len(prepared)} prepared)")
    if not dry_run:
        write_atomically(file, text)


def main(argv):
    parser = argparse.ArgumentParser(prog="stemdeck-prepare-events",
                                     description="Write prepared event files for every set in a library.")
    parser.add_argument("library", type=Path)
    parser.add_argument("--dry-run", action="store_true", help="write nothing, print what would be done")
    parser.add_argument("--replace", action="store_true",
                        help="redo the prepared events of a file that has events (hand events are kept)")
    parser.add_argument("--only", metavar="SET", help="only the set with this name")
    parser.add_argument("--rules", choices=sorted(RULE_SETS), default="v1")
    parser.add_argument("--cache", type=Path, default=DEFAULT_CACHE, help="the app's analysis cache")
    args = parser.parse_args(argv)

    if not args.library.is_dir():
        print(f"not a folder: {args.library}", file=sys.stderr)
        return 2
    cache = AnalysisCache(args.cache)
    sets = [s for s in find_sets(args.library) if args.only in (None, s.name)]
    if args.only and not sets:
        print(f"no set called {args.only}", file=sys.stderr)
        return 1
    for stem_set in sets:
        prepare_library_set(stem_set, cache, RULE_SETS[args.rules], args.dry_run, print, args.replace)
    return 0
