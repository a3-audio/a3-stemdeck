// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>

#include "EventRecorder.h"

// A recorded press is placed where the live press starts its motion: SHIFT at
// once, otherwise on the next bar's one.
namespace
{
using motionlite::Shape;
using stemevents::eventForPress;
}

TEST (EventRecorder, ShiftPressIsTheBeatNow)
{
	const auto e = eventForPress (1, Shape::ping, true, 9.3, 2.0, false, 7, false);
	ASSERT_TRUE (e.has_value());
	EXPECT_DOUBLE_EQ (e->beat, 9.3);
	EXPECT_EQ (e->stem, 1);
	EXPECT_EQ (e->shape, Shape::ping);
	EXPECT_DOUBLE_EQ (e->lengthBars, 2.0);
	EXPECT_TRUE (e->active);
	EXPECT_EQ (e->origin, stemevents::Origin::hand);
}

TEST (EventRecorder, ShiftPressSnapsWhenQuantizeIsOn)
{
	EXPECT_DOUBLE_EQ (eventForPress (0, Shape::orbit, true, 9.3, 4.0, false, 0, true)->beat, 9.0);
	EXPECT_DOUBLE_EQ (eventForPress (0, Shape::orbit, true, 9.6, 4.0, false, 0, true)->beat, 10.0);
	EXPECT_DOUBLE_EQ (eventForPress (0, Shape::orbit, true, -1.4, 4.0, false, 0, true)->beat, -1.0) << "before the one too";
}

TEST (EventRecorder, PlainPressIsTheNextBar)
{
	EXPECT_DOUBLE_EQ (eventForPress (2, Shape::rise, false, 9.3, 4.0, false, 0, false)->beat, 12.0);
	EXPECT_DOUBLE_EQ (eventForPress (2, Shape::rise, false, 9.3, 4.0, false, 0, true)->beat, 12.0) << "quantize changes nothing here";
	EXPECT_DOUBLE_EQ (eventForPress (2, Shape::rise, false, 8.0, 4.0, false, 0, false)->beat, 12.0)
		<< "exactly on a bar line: the next one, as the live press";
	EXPECT_DOUBLE_EQ (eventForPress (2, Shape::rise, false, -2.5, 4.0, false, 0, false)->beat, 0.0) << "before the one: the one";
}

TEST (EventRecorder, NoGridIsNoEvent)
{
	EXPECT_FALSE (eventForPress (0, Shape::orbit, false, stemevents::noBeat, 4.0, false, 0, false).has_value());
	EXPECT_FALSE (eventForPress (0, Shape::orbit, true, stemevents::noBeat, 4.0, false, 0, true).has_value());
}

TEST (EventRecorder, TheSeedIsTheLivePressSeed)
{
	const auto e = eventForPress (3, Shape::scat, false, 1.0, 1.0, true, 0xfeedbeefcafeULL, false);
	ASSERT_TRUE (e.has_value());
	EXPECT_EQ (e->seed, 0xfeedbeefcafeULL);
	EXPECT_TRUE (e->reversed) << "and the stem's direction";
}

TEST (EventRecorder, TheLengthIsTheOneTheShotPlays)
{
	EXPECT_DOUBLE_EQ (eventForPress (0, Shape::orbit, true, 1.0, 3.0, false, 0, false)->lengthBars, 4.0)
		<< "an invalid length reads as the default, as in MotionLite";
	EXPECT_DOUBLE_EQ (eventForPress (0, Shape::orbit, true, 1.0, 0.5, false, 0, false)->lengthBars, 0.5);
	EXPECT_DOUBLE_EQ (eventForPress (0, Shape::ping, true, 1.0, 0.25, false, 0, false)->lengthBars, 0.25) << "a quarter bar";
	EXPECT_DOUBLE_EQ (stemevents::eventAtPlayhead (0, Shape::scat, 4.0, 0.25, false, 0, true)->lengthBars, 0.25);
}

// 3D REC in pause: the press is placed at the playhead, not on the next bar.
TEST (EventRecorder, PausedPressIsThePlayheadsBeat)
{
	const auto e = stemevents::eventAtPlayhead (1, Shape::ping, 9.0, 2.0, false, 7, true);
	ASSERT_TRUE (e.has_value());
	EXPECT_DOUBLE_EQ (e->beat, 9.0) << "on a beat: that beat, not the next bar";
	EXPECT_EQ (e->stem, 1);
	EXPECT_EQ (e->shape, Shape::ping);
	EXPECT_DOUBLE_EQ (e->lengthBars, 2.0);
	EXPECT_EQ (e->seed, 7u);
	EXPECT_TRUE (e->active);
	EXPECT_EQ (e->origin, stemevents::Origin::hand);
	EXPECT_DOUBLE_EQ (stemevents::eventAtPlayhead (1, Shape::ping, 8.0, 2.0, false, 7, false)->beat, 8.0)
		<< "on a bar line: that bar line";
}

TEST (EventRecorder, PausedPressSnapsToTheNearestBeatWhenQuantizeIsOn)
{
	EXPECT_DOUBLE_EQ (stemevents::eventAtPlayhead (0, Shape::orbit, 9.3, 4.0, false, 0, true)->beat, 9.0);
	EXPECT_DOUBLE_EQ (stemevents::eventAtPlayhead (0, Shape::orbit, 9.6, 4.0, false, 0, true)->beat, 10.0);
	EXPECT_DOUBLE_EQ (stemevents::eventAtPlayhead (0, Shape::orbit, -0.4, 4.0, false, 0, true)->beat, 0.0) << "before the one";
}

TEST (EventRecorder, PausedPressWithoutQuantizeKeepsTheExactBeat)
{
	EXPECT_DOUBLE_EQ (stemevents::eventAtPlayhead (0, Shape::orbit, 9.3, 4.0, false, 0, false)->beat, 9.3);
}

TEST (EventRecorder, PausedPressKeepsTheStemsDirectionAndTheShotsLength)
{
	const auto e = stemevents::eventAtPlayhead (3, Shape::orbit, 4.0, 3.0, true, 0xfeedULL, true);
	ASSERT_TRUE (e.has_value());
	EXPECT_TRUE (e->reversed);
	EXPECT_DOUBLE_EQ (e->lengthBars, 4.0) << "an invalid length reads as the default, as in MotionLite";
	EXPECT_DOUBLE_EQ (stemevents::eventAtPlayhead (3, Shape::scat, 4.0, 0.5, false, 0, true)->lengthBars, 0.5);
}

TEST (EventRecorder, PausedPressWithoutAGridIsNoEvent)
{
	EXPECT_FALSE (stemevents::eventAtPlayhead (0, Shape::orbit, stemevents::noBeat, 4.0, false, 0, true).has_value());
	EXPECT_FALSE (stemevents::eventAtPlayhead (0, Shape::orbit, stemevents::noBeat, 4.0, false, 0, false).has_value());
}
