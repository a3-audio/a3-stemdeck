#include <gtest/gtest.h>

#include "GridEdit.h"

#include <cmath>

using GridEdit::Grid;

namespace
{
	constexpr Grid at120 { 120.0, 0.25 };   // a beat is 0.5 s, a bar 2 s
}

TEST (GridEdit, FirstBeatStaysWithinTheFirstBar)
{
	EXPECT_NEAR (GridEdit::normalised ({ 120.0, 5.1 }).firstBeat, 1.1, 1e-9) << "two bars earlier, same downbeat";
	EXPECT_NEAR (GridEdit::normalised ({ 120.0, -0.5 }).firstBeat, 1.5, 1e-9);
	EXPECT_NEAR (GridEdit::normalised ({ 120.0, 0.0 }).firstBeat, 0.0, 1e-9);
}

TEST (GridEdit, TheJogShiftsTheWholeGrid)
{
	EXPECT_NEAR (GridEdit::shift (at120, 0.01).firstBeat, 0.26, 1e-9);
	EXPECT_NEAR (GridEdit::shift (at120, -0.3).firstBeat, 1.95, 1e-9) << "past zero: a bar on, same downbeat";
	EXPECT_DOUBLE_EQ (GridEdit::shift (at120, 0.01).bpm, 120.0);
}

TEST (GridEdit, HalfABeatEachWay)
{
	EXPECT_NEAR (GridEdit::shiftHalfBeat (at120, true).firstBeat, 0.5, 1e-9);
	EXPECT_NEAR (GridEdit::shiftHalfBeat (at120, false).firstBeat, 0.0, 1e-9);
	EXPECT_NEAR (GridEdit::shiftHalfBeat ({ 120.0, 0.0 }, false).firstBeat, 1.75, 1e-9) << "before zero: a bar on";
}

TEST (GridEdit, SnapPutsTheDownbeatOnTheCue)
{
	const auto snapped = GridEdit::snapToCue (at120, 33.75);
	EXPECT_NEAR (snapped.firstBeat, 1.75, 1e-9) << "33.75 is 16 bars after 1.75";
	const auto beatsAtCue = (33.75 - snapped.firstBeat) / 0.5;
	EXPECT_NEAR (std::fmod (beatsAtCue, 4.0), 0.0, 1e-9) << "the cue is a 1";
}

TEST (GridEdit, ShiftGridTakesTheLeadersPhase)
{
	// At 10.1 s this deck is 0.1 s past a beat of its grid; the leader is
	// exactly on one. The grid moves 0.1 s later.
	const Grid grid { 120.0, 0.0 };
	const auto shifted = GridEdit::shiftToPhase (grid, 10.1, 7.0);   // own phase .2, leader .0
	const auto phase = (10.1 - shifted.firstBeat) / 0.5;
	EXPECT_NEAR (phase - std::round (phase), 0.0, 1e-9) << "now on a beat, like the leader";
	EXPECT_NEAR (std::abs (shifted.firstBeat - 0.1), 0.0, 1e-9) << "the short way: 0.1 s, not 0.4";
}

TEST (GridEdit, NoTempoNoEdit)
{
	const Grid none {};
	EXPECT_DOUBLE_EQ (GridEdit::shiftHalfBeat (none, true).firstBeat, 0.0);
	EXPECT_DOUBLE_EQ (GridEdit::shiftToPhase (none, 3.0, 1.2).firstBeat, 0.0);
}

// "SET 1": the bar's one wherever the playhead is -- between two beats of the
// old grid, which a cue point rarely is. The same arithmetic as SNAP.
TEST (GridEdit, TheDownbeatGoesToThePlayhead)
{
	const auto playhead = 12.34;
	const auto set = GridEdit::snapToCue (at120, playhead);
	EXPECT_NEAR (std::fmod ((playhead - set.firstBeat) / 0.5, 4.0), 0.0, 1e-9) << "the playhead is a 1";
	EXPECT_GE (set.firstBeat, 0.0);
	EXPECT_LT (set.firstBeat, 2.0) << "within the first bar";
	EXPECT_DOUBLE_EQ (set.bpm, 120.0);
}

// Loop in/out on the nearest beats of the grid (a beat is 0.5 s at 120, the
// first at 0.25).
TEST (GridEdit, ALoopSnapsToTheNearestBeats)
{
	const auto loop = GridEdit::snappedLoop (at120, 10.2, 12.1);
	EXPECT_NEAR (loop.start, 10.25, 1e-9);
	EXPECT_NEAR (loop.end, 12.25, 1e-9);
}

TEST (GridEdit, ALoopInsideOneBeatBecomesOneBeat)
{
	const auto loop = GridEdit::snappedLoop (at120, 10.2, 10.3);
	EXPECT_NEAR (loop.start, 10.25, 1e-9);
	EXPECT_NEAR (loop.end, 10.75, 1e-9);
}

TEST (GridEdit, WithoutATempoTheLoopStaysPut)
{
	const auto loop = GridEdit::snappedLoop ({ 0.0, 0.0 }, 10.2, 12.1);
	EXPECT_DOUBLE_EQ (loop.start, 10.2);
	EXPECT_DOUBLE_EQ (loop.end, 12.1);
}

// "1 < / > 1": the beats sit right and only the bar's one is wrong, which is
// what the analysis gets wrong on most tracks (2026-10-08). The one moves a
// whole beat; every beat stays where it was.
TEST (GridEdit, TheOneMovesABeatAndTheBeatsStay)
{
	const auto later = GridEdit::moveOne (at120, true);
	EXPECT_NEAR (later.firstBeat, 0.75, 1e-9) << "the one a beat later";
	EXPECT_NEAR (GridEdit::moveOne (at120, false).firstBeat, 1.75, 1e-9) << "a beat earlier, past zero: a bar on";

	const auto phase = [] (Grid g) { return std::fmod (g.firstBeat, 60.0 / g.bpm); };
	EXPECT_NEAR (phase (later), phase (at120), 1e-9) << "the beats themselves do not move";
	EXPECT_DOUBLE_EQ (later.bpm, 120.0);
}

TEST (GridEdit, FourTimesTheOneComesBackToWhereItWas)
{
	auto grid = at120;
	for (int i = 0; i < 4; ++i)
	{
		grid = GridEdit::moveOne (grid, true);
		EXPECT_GE (grid.firstBeat, 0.0);
		EXPECT_LT (grid.firstBeat, 2.0) << "within the first bar";
	}
	EXPECT_NEAR (grid.firstBeat, at120.firstBeat, 1e-9);
}

TEST (GridEdit, NoTempoNoOneToMove)
{
	EXPECT_DOUBLE_EQ (GridEdit::moveOne (Grid {}, true).firstBeat, 0.0);
}

// x2 / 1/2: the analysis found half or double time (2026-10-16). The downbeat
// stays where it is; only the beats between change.
TEST (GridEdit, DoublingKeepsTheDownbeatAndPutsABeatBetween)
{
	const auto doubled = GridEdit::doubleTempo (at120);
	EXPECT_DOUBLE_EQ (doubled.bpm, 240.0);
	EXPECT_NEAR (doubled.firstBeat, at120.firstBeat, 1e-9) << "the downbeat has not moved";

	const auto beatAt = [] (Grid g, int n) { return g.firstBeat + n * 60.0 / g.bpm; };
	EXPECT_NEAR (beatAt (doubled, 1), (beatAt (at120, 0) + beatAt (at120, 1)) / 2, 1e-9) << "halfway between two old beats";
	EXPECT_NEAR (beatAt (doubled, 2), beatAt (at120, 1), 1e-9) << "the old beats are still beats";
}

TEST (GridEdit, HalvingKeepsTheDownbeatAndDropsEverySecondBeat)
{
	const auto halved = GridEdit::halveTempo (at120);
	EXPECT_DOUBLE_EQ (halved.bpm, 60.0);
	EXPECT_NEAR (halved.firstBeat, at120.firstBeat, 1e-9) << "the downbeat has not moved";

	const auto beatAt = [] (Grid g, int n) { return g.firstBeat + n * 60.0 / g.bpm; };
	EXPECT_NEAR (beatAt (halved, 1), beatAt (at120, 2), 1e-9) << "the old beat 2 is the new beat 1; the old beat 1 is gone";
}

TEST (GridEdit, DoubleThenHalveGivesTheGridBack)
{
	const auto back = GridEdit::halveTempo (GridEdit::doubleTempo (at120));
	EXPECT_DOUBLE_EQ (back.bpm, at120.bpm);
	EXPECT_NEAR (back.firstBeat, at120.firstBeat, 1e-9);
}

TEST (GridEdit, NoTempoNothingToDoubleOrHalve)
{
	EXPECT_DOUBLE_EQ (GridEdit::doubleTempo (Grid {}).bpm, 0.0);
	EXPECT_DOUBLE_EQ (GridEdit::halveTempo (Grid {}).bpm, 0.0);
}

TEST (GridEdit, TempoChangesKeepTheFirstBeatWithinOneBar)
{
	// firstBeat 1.9 s is inside the 2 s bar at 120; at 240 a bar is 1 s, at 60 it is 4 s.
	const Grid late { 120.0, 1.9 };
	const auto doubled = GridEdit::doubleTempo (late);
	EXPECT_GE (doubled.firstBeat, 0.0);
	EXPECT_LT (doubled.firstBeat, 4 * 60.0 / doubled.bpm) << "within the new, shorter bar";
	EXPECT_NEAR (doubled.firstBeat, 0.9, 1e-9) << "whole bars moved off: the same downbeat";

	const auto halved = GridEdit::halveTempo (late);
	EXPECT_GE (halved.firstBeat, 0.0);
	EXPECT_LT (halved.firstBeat, 4 * 60.0 / halved.bpm);
}

// QUANTIZE: any point, not only a loop's ends, goes to the nearest beat.
TEST (GridEdit, SnappedPointIsTheNearestBeat)
{
	const GridEdit::Grid at120 { 120.0, 0.25 };
	EXPECT_DOUBLE_EQ (GridEdit::snappedPoint (at120, 10.4), 10.25);
	EXPECT_DOUBLE_EQ (GridEdit::snappedPoint (at120, 10.6), 10.75);
}

TEST (GridEdit, SnappedPointWithoutBpmIsUnchanged)
{
	EXPECT_DOUBLE_EQ (GridEdit::snappedPoint ({ 0.0, 0.0 }, 10.4), 10.4);
}
