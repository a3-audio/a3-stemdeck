#pragma once

// Correcting a beat grid by hand, as a CDJ-3000's Grid Adjust mode does.
// Pure: no JUCE, testable. A grid is beats at firstBeat + n * 60 / bpm, and
// the bar counts from firstBeat: that beat is the downbeat (1 of 4).
//
// Every edit keeps the result's firstBeat in [0, one bar), moved by whole
// bars, so the downbeat it defines stays where it is.
namespace GridEdit
{
	constexpr int beatsPerBar = 4;

	struct Grid
	{
		double bpm = 0.0;
		double firstBeat = 0.0;
	};

	// firstBeat moved by whole bars into [0, one bar).
	Grid normalised (Grid grid);

	// The whole grid later (+) or earlier (-) by `seconds`: the jog wheel,
	// the mouse wheel, and <1/2 1/2> (half a beat).
	Grid shift (Grid grid, double seconds);
	Grid shiftHalfBeat (Grid grid, bool forwards);

	// 1< / >1: the bar's one a whole beat earlier or later, the beats where
	// they are -- for when the analysis picked the wrong one.
	Grid moveOne (Grid grid, bool later);

	// x2 / 1/2: the analysis found half or double time. The downbeat stays
	// where it is (firstBeat unchanged); a beat is inserted between every two,
	// or every second one dropped. Without a tempo there is nothing to change.
	Grid doubleTempo (Grid grid);
	Grid halveTempo (Grid grid);

	// SNAP GRID (CUE): the downbeat onto the cue point.
	Grid snapToCue (Grid grid, double cueSeconds);

	// A point on the grid's nearest beat (QUANTIZE); unchanged without a tempo.
	double snappedPoint (Grid grid, double seconds);

	// A loop's in and out on the grid's nearest beats: "the loop in/out must
	// snap to grid" (2026-09-30). Both on the same beat makes a one-beat loop
	// rather than none; without a tempo the points stay where they were.
	struct Loop { double start = 0.0, end = 0.0; };
	Loop snappedLoop (Grid grid, double startSeconds, double endSeconds);

	// SHIFT GRID: the grid moved so that, at `position`, this deck is at the
	// same point in its beat as the leader (`leaderBeatPhase`, the leader's
	// position in beats) -- what was aligned by ear with the jog wheel goes
	// into the grid. The smaller way round: at most half a beat.
	Grid shiftToPhase (Grid grid, double position, double leaderBeatPhase);
}
