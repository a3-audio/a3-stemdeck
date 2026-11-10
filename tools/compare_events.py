#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Raphael Eismann
# SPDX-License-Identifier: GPL-3.0-or-later
"""Two event files side by side, on one beat grid: a yardstick for the
preparation rules against events a DJ placed by hand.

Per file: how many events per stem and shape, events per minute, the share
on a bar line and on an 8-bar phrase start, and (with --analysis) the share
inside a break. Then a coarse overlap: how many of A's events have an event
of B on the same stem, of the same shape (strict) or the same kind of move
(loose: scat and ping are both accents), within OVERLAP_BARS. B can be the
rules themselves, run on the analysis (--rules), so no audio is decoded.

A development tool, not part of the package. Bars count from 1 in the output."""

import argparse
import json
import sys
from collections import namedtuple
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import passages  # noqa: E402
import prepare_events  # noqa: E402
import stem_analysis  # noqa: E402

Grid = namedtuple("Grid", "bpm first_beat")

BEATS_PER_BAR = 4
PHRASE_BARS = 8
OVERLAP_BARS = 4
DENSITY_WINDOW_SECONDS = prepare_events.DENSITY_WINDOW_SECONDS
FAMILY = {"orbit": "orbit", "rise": "rise", "scat": "accent", "ping": "accent"}


def load_events(path):
    status, events = prepare_events.read_events(Path(path))
    if status != "ok":
        raise SystemExit(f"{path}: {status}")
    return events


def load_windows(path):
    """The analysis windows of a .a3analysis.json, whatever its key."""
    root = json.loads(Path(path).read_text(encoding="utf-8"))
    return [(stem["level"], stem["bright"]) for stem in root["stems"]]


def bars_of(windows, grid):
    seconds = max(len(level) for level, _ in windows) * stem_analysis.WINDOW_SECONDS
    length_beats = (seconds - grid.first_beat) * grid.bpm / 60.0
    count = int(length_beats // BEATS_PER_BAR)
    return stem_analysis.bars_from_windows(windows, grid.bpm, grid.first_beat, count), length_beats


def until(events, last_bar):
    """The events starting before bar `last_bar` ends (bars from 1)."""
    return [e for e in events if e["beat"] < last_bar * BEATS_PER_BAR]


def _on(beat, every_bars):
    return abs(beat / (every_bars * BEATS_PER_BAR) - round(beat / (every_bars * BEATS_PER_BAR))) < 1e-6


def _share(count, total):
    return count / total if total else 0.0


def peak(events, window_beats):
    """The most events starting in any window of `window_beats` beats."""
    beats = sorted(e["beat"] for e in events)
    return max((sum(1 for b in beats if start <= b < start + window_beats) for start in beats), default=0)


def stats(events, grid, minutes, breaks=(), bars=None):
    """Numbers of one file. `breaks`: (start, end) bars from 0, end exclusive;
    with the content (`bars`), how many events outlast their stem's sound, by
    the strict rule (v2) and by v3's (rests of up to two bars bridged, a
    one-bar lead before the first sound)."""
    total = len(events)
    result = {"events": total, "events/min": round(_share(total, minutes), 2),
              "peak in 30 s": peak(events, DENSITY_WINDOW_SECONDS * grid.bpm / 60.0),
              "on a bar line": _share(sum(1 for e in events if _on(e["beat"], 1)), total),
              "on a phrase start (8 bars)": _share(sum(1 for e in events if _on(e["beat"], PHRASE_BARS)), total),
              "in a break": _share(sum(1 for e in events
                                       if any(s <= e["beat"] / BEATS_PER_BAR < end for s, end in breaks)), total)}
    if bars is not None:
        result["overrun their sound"] = prepare_events.overruns(events, bars)
        result["overrun, rests bridged (v3)"] = prepare_events.overruns(events, bars,
                                                                       prepare_events.sounds_through_bridged)
    for stem in range(prepare_events.STEMS):
        for shape in prepare_events.SHAPES:
            count = sum(1 for e in events if e["stem"] == stem and e["shape"] == shape)
            if count:
                result[f"stem {stem} {shape}"] = count
    return result


def matched(a, b, strict=False):
    """How many of A's events have one of B's on the same stem, of the same
    shape (strict) or kind of move, starting within OVERLAP_BARS."""
    def same(x, y):
        kind = (x["shape"] == y["shape"]) if strict else FAMILY[x["shape"]] == FAMILY[y["shape"]]
        return (x["stem"] == y["stem"] and kind
                and abs(x["beat"] - y["beat"]) <= OVERLAP_BARS * BEATS_PER_BAR)
    return sum(1 for x in a if any(same(x, y) for y in b))


def _cell(value):
    return f"{value:.2f}" if isinstance(value, float) else str(value)


def table(name_a, name_b, stats_a, stats_b, a, b):
    keys = list(stats_a) + [k for k in stats_b if k not in stats_a]
    first = [k for k in keys if not k.startswith("stem ")]
    ordered = first + sorted(k for k in keys if k.startswith("stem "))
    width = max(len(k) for k in ordered + ["of A matched in B (strict)"]) + 2
    lines = [f"A: {name_a}", f"B: {name_b}", "", f"{'':<{width}}{'A':>8}{'B':>8}"]
    lines += [f"{key:<{width}}{_cell(stats_a.get(key, 0)):>8}{_cell(stats_b.get(key, 0)):>8}" for key in ordered]
    lines.append("")
    for strict in (False, True):
        label = "strict" if strict else "loose"
        lines.append(f"{f'of A matched in B ({label})':<{width}}{matched(a, b, strict)}/{len(a)}")
        lines.append(f"{f'of B matched in A ({label})':<{width}}{matched(b, a, strict)}/{len(b)}")
    return lines


def main(argv):
    parser = argparse.ArgumentParser(prog="compare_events", description=__doc__.split("\n\n")[0],
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("a", type=Path, help="an events file (the reference)")
    parser.add_argument("b", type=Path, nargs="?", help="an events file; leave out with --rules")
    parser.add_argument("--bpm", type=float, required=True)
    parser.add_argument("--first-beat", type=float, required=True, help="seconds")
    parser.add_argument("--analysis", type=Path, help="the set's .a3analysis.json: breaks, length, --rules")
    parser.add_argument("--rules", choices=sorted(prepare_events.RULE_SETS),
                        help="B is these rules run on --analysis")
    parser.add_argument("--length-seconds", type=float, help="the track's length (else from --analysis)")
    parser.add_argument("--until-bar", type=int, help="compare only events starting up to this bar (from 1)")
    args = parser.parse_args(argv)
    if (args.b is None) == (args.rules is None) or (args.rules and not args.analysis):
        parser.error("give B, or --rules with --analysis")

    grid = Grid(args.bpm, args.first_beat)
    a = load_events(args.a)
    breaks, length_beats, name_b, bars = [], None, str(args.b), None
    if args.analysis:
        bars, length_beats = bars_of(load_windows(args.analysis), grid)
        breaks = [(p.start, p.end) for p in passages.find_passages(bars) if p.kind == "break"]
    if args.rules:
        b = prepare_events.RULE_SETS[args.rules](length_beats, bars, grid.bpm)
        name_b = f"--rules {args.rules} on {args.analysis.name}"
    else:
        b = load_events(args.b)
    if args.length_seconds:
        length_beats = (args.length_seconds - grid.first_beat) * grid.bpm / 60.0
    if args.until_bar:
        a, b = until(a, args.until_bar), until(b, args.until_bar)
        length_beats = args.until_bar * BEATS_PER_BAR
    if length_beats is None:
        length_beats = max(e["beat"] + BEATS_PER_BAR * e["bars"] for e in a + b)
    minutes = length_beats * 60.0 / grid.bpm / 60.0
    lines = table(args.a.name, name_b, stats(a, grid, minutes, breaks, bars),
                  stats(b, grid, minutes, breaks, bars), a, b)
    print(f"{minutes:.2f} min compared, {grid.bpm:g} BPM; breaks (bars from 1): "
          + (", ".join(f"{s + 1}-{e}" for s, e in breaks) or "none given"))
    print("\n".join(lines))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
