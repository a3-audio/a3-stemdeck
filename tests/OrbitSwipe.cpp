// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>

#include "OrbitSwipe.h"

namespace
{
	constexpr float pad = 100.0f;

	// Finger down at x = 50, waits, then moves to `to` and lets go.
	OrbitSwipe::Result gesture (double holdFor, float to, float width = pad)
	{
		OrbitSwipe swipe;
		swipe.down (0.0, 50.0f);
		swipe.drag (holdFor, 50.0f);
		swipe.drag (holdFor + 0.1, to);
		return swipe.up (width);
	}
}

TEST (OrbitSwipe, ATapIsNone)
{
	OrbitSwipe swipe;
	swipe.down (0.0, 50.0f);
	EXPECT_EQ (swipe.up (pad), OrbitSwipe::Result::none);
}

TEST (OrbitSwipe, HoldThenSwipeRightIsRight)
{
	EXPECT_EQ (gesture (0.5, 100.0f), OrbitSwipe::Result::right);
}

TEST (OrbitSwipe, HoldThenSwipeLeftIsLeft)
{
	EXPECT_EQ (gesture (0.5, 0.0f), OrbitSwipe::Result::left);
}

TEST (OrbitSwipe, SwipeWithoutHoldIsNone)
{
	EXPECT_EQ (gesture (0.1, 100.0f), OrbitSwipe::Result::none);
}

TEST (OrbitSwipe, AnUnmovedFingerCountsAsHoldingBeforeTheSwipe)
{
	OrbitSwipe swipe;
	swipe.down (0.0, 50.0f);
	swipe.drag (0.1, 51.0f);   // jitter
	swipe.drag (0.6, 51.0f);
	swipe.drag (0.7, 100.0f);
	EXPECT_EQ (swipe.up (pad), OrbitSwipe::Result::right);
}

TEST (OrbitSwipe, ShortTravelIsNone)
{
	EXPECT_EQ (gesture (0.5, 80.0f), OrbitSwipe::Result::none);   // 30 px of 100
}

TEST (OrbitSwipe, TravelIsRelativeToThePadWidth)
{
	EXPECT_EQ (gesture (0.5, 80.0f, 60.0f), OrbitSwipe::Result::right);   // 30 px of 60
	EXPECT_EQ (gesture (0.5, 80.0f, 300.0f), OrbitSwipe::Result::none);   // 30 px of 300
}

TEST (OrbitSwipe, ReleaseBackNearTheStartIsNone)
{
	OrbitSwipe swipe;
	swipe.down (0.0, 50.0f);
	swipe.drag (0.5, 50.0f);
	swipe.drag (0.6, 100.0f);
	swipe.drag (0.7, 55.0f);
	EXPECT_EQ (swipe.up (pad), OrbitSwipe::Result::none);
}

TEST (OrbitSwipe, IsHoldAfterTheHoldTimeOnly)
{
	OrbitSwipe swipe;
	EXPECT_FALSE (swipe.isHold (5.0));   // nothing pressed
	swipe.down (1.0, 50.0f);
	EXPECT_FALSE (swipe.isHold (1.2));
	EXPECT_TRUE (swipe.isHold (1.4));
	swipe.up (pad);
	EXPECT_FALSE (swipe.isHold (2.0));
}
