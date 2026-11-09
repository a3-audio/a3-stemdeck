// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>

#include "OverviewGesture.h"

namespace
{
	constexpr float width = 756.0f;   // the rig's one-deck overview
}

// A finger that lands and lifts wanders a few pixels: that is a tap, not a loop.
TEST (OverviewGesture, AFingersJitterIsATap)
{
	EXPECT_FALSE (overviewgesture::isLoopDrag (300.0f, 300.0f, width));
	EXPECT_FALSE (overviewgesture::isLoopDrag (300.0f, 306.0f, width));
	EXPECT_FALSE (overviewgesture::isLoopDrag (300.0f, 288.0f, width));
}

TEST (OverviewGesture, ADeliberateDragIsALoop)
{
	EXPECT_TRUE (overviewgesture::isLoopDrag (300.0f, 300.0f + width * 0.1f, width));
	EXPECT_TRUE (overviewgesture::isLoopDrag (300.0f, 300.0f - width * 0.1f, width)) << "either way";
}

TEST (OverviewGesture, TheThresholdIsAShareOfTheWidth)
{
	const auto threshold = width * overviewgesture::loopShare;
	EXPECT_FALSE (overviewgesture::isLoopDrag (100.0f, 100.0f + threshold * 0.9f, width));
	EXPECT_TRUE (overviewgesture::isLoopDrag (100.0f, 100.0f + threshold * 1.1f, width));

	// The same 20 px: a loop on a narrow overview, a tap on a wide one.
	EXPECT_TRUE (overviewgesture::isLoopDrag (100.0f, 120.0f, 300.0f));
	EXPECT_FALSE (overviewgesture::isLoopDrag (100.0f, 120.0f, 1500.0f));
}

TEST (OverviewGesture, NoWidthNoLoop)
{
	EXPECT_FALSE (overviewgesture::isLoopDrag (0.0f, 50.0f, 0.0f));
}
