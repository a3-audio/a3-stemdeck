// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>

#include "CuePlay.h"
#include "FakeTransportDeck.h"

// CUE and PLAY as on a CDJ-3000 (manual, "Cueing"): stopped off the cue
// point CUE sets it; on it, CUE plays for as long as it is held; playing, CUE
// goes back to it and stops. PLAY while CUE is held plays on from where the
// deck is, and letting go of CUE then changes nothing.
namespace
{
struct CuePlayTest : ::testing::Test
{
	FakeTransportDeck deck;
	CuePlay keys;

	void previewFromCue (double cue)
	{
		deck.cue = cue;
		deck.at = cue;
		keys.cueDown (deck);
		ASSERT_TRUE (deck.playing) << "held on the cue point, CUE previews";
		deck.at = cue + 1.5;   // the preview ran on
	}
};
}

TEST_F (CuePlayTest, StoppedOffTheCuePointCueSetsIt)
{
	deck.at = 12.0;
	keys.cueDown (deck);
	keys.cueUp (deck);
	EXPECT_DOUBLE_EQ (deck.cue, 12.0);
	EXPECT_FALSE (deck.playing);
}

TEST_F (CuePlayTest, HeldOnTheCuePointPreviewsAndReleaseReturns)
{
	previewFromCue (4.0);
	keys.cueUp (deck);
	EXPECT_FALSE (deck.playing);
	EXPECT_DOUBLE_EQ (deck.at, 4.0) << "back on the cue point";
}

TEST_F (CuePlayTest, PlayingCueGoesBackAndStops)
{
	deck.cue = 2.0;
	deck.at = 30.0;
	deck.playing = true;
	keys.cueDown (deck);
	EXPECT_FALSE (deck.playing);
	EXPECT_DOUBLE_EQ (deck.at, 2.0);
	keys.cueUp (deck);
	EXPECT_FALSE (deck.playing);
	EXPECT_DOUBLE_EQ (deck.at, 2.0);
}

// The maintainer's case (2026-10-28): "if i hold cue and press the play it
// should play then without gap. atm it stops."
TEST_F (CuePlayTest, PlayWhileCueHeldPlaysOnWithoutAGap)
{
	previewFromCue (4.0);
	keys.playPressed (deck);
	EXPECT_TRUE (deck.playing) << "PLAY does not toggle the preview off";
	EXPECT_DOUBLE_EQ (deck.at, 5.5) << "no jump";

	keys.cueUp (deck);
	EXPECT_TRUE (deck.playing) << "letting go of CUE keeps it playing";
	EXPECT_DOUBLE_EQ (deck.at, 5.5) << "and does not go back to the cue point";
	EXPECT_DOUBLE_EQ (deck.cue, 4.0) << "the cue point stays";
}

TEST_F (CuePlayTest, AfterPlayingOnTheNextCueGoesBack)
{
	previewFromCue (4.0);
	keys.playPressed (deck);
	keys.cueUp (deck);
	deck.at = 9.0;

	keys.cueDown (deck);
	EXPECT_FALSE (deck.playing);
	EXPECT_DOUBLE_EQ (deck.at, 4.0);
}

TEST_F (CuePlayTest, PlayWithoutCueToggles)
{
	keys.playPressed (deck);
	EXPECT_TRUE (deck.playing);
	keys.playPressed (deck);
	EXPECT_FALSE (deck.playing);
}

// Stopped off the cue point, CUE held has just set the cue: PLAY starts from
// there, and the release leaves it playing.
TEST_F (CuePlayTest, PlayWhileCueHeldStoppedStartsAndReleaseKeepsPlaying)
{
	deck.at = 7.0;
	keys.cueDown (deck);
	keys.playPressed (deck);
	EXPECT_TRUE (deck.playing);
	keys.cueUp (deck);
	EXPECT_TRUE (deck.playing);
	EXPECT_DOUBLE_EQ (deck.at, 7.0);
}

// Every touch reaches JUCE twice (touch#0, then X's emulated mouse#0), and
// the panel and the screen may both hold CUE: a second down while held, or an
// up while not held, is not a new press.
TEST_F (CuePlayTest, ADoubledEdgeIsOnePress)
{
	previewFromCue (4.0);
	keys.cueDown (deck);   // the emulated mouse's down
	EXPECT_TRUE (deck.playing);
	keys.playPressed (deck);
	keys.cueUp (deck);
	keys.cueUp (deck);     // the emulated mouse's up
	EXPECT_TRUE (deck.playing);
	EXPECT_DOUBLE_EQ (deck.at, 5.5);
	EXPECT_FALSE (keys.isCueHeld());
}

TEST_F (CuePlayTest, HeldIsReported)
{
	EXPECT_FALSE (keys.isCueHeld());
	keys.cueDown (deck);
	EXPECT_TRUE (keys.isCueHeld());
	keys.cueUp (deck);
	EXPECT_FALSE (keys.isCueHeld());
}

TEST_F (CuePlayTest, NothingLoadedNothingMoves)
{
	deck.loaded = false;
	deck.at = 3.0;
	keys.cueDown (deck);
	keys.cueUp (deck);
	EXPECT_DOUBLE_EQ (deck.cue, 0.0);
	EXPECT_FALSE (deck.playing);
}

// A new track ends a preview: the release after a load does not jump.
TEST_F (CuePlayTest, ResetForgetsThePreview)
{
	previewFromCue (4.0);
	keys.reset();
	keys.cueUp (deck);
	EXPECT_TRUE (deck.playing);
	EXPECT_DOUBLE_EQ (deck.at, 5.5);
}

// With QUANTIZE the cue set off the beat lands on the beat, and the playhead
// goes with it (as on a CDJ): the next CUE previews from there instead of
// setting the cue again, and PLAY starts from the cue.
TEST_F (CuePlayTest, AQuantizedCueTakesThePlayheadWithIt)
{
	deck.beat = 0.5;
	deck.at = 12.2;
	keys.cueDown (deck);
	keys.cueUp (deck);
	EXPECT_DOUBLE_EQ (deck.cue, 12.0);
	EXPECT_DOUBLE_EQ (deck.at, 12.0) << "on the cue point";
	EXPECT_FALSE (deck.playing);

	keys.cueDown (deck);
	EXPECT_TRUE (deck.playing) << "the next CUE previews";
	EXPECT_DOUBLE_EQ (deck.cue, 12.0);
	keys.cueUp (deck);
	EXPECT_DOUBLE_EQ (deck.at, 12.0);

	keys.playPressed (deck);
	EXPECT_TRUE (deck.playing);
	EXPECT_DOUBLE_EQ (deck.at, 12.0) << "PLAY starts from the cue";
}
