// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>

#include "OrbitSwipe.h"

// A finger on a shape pad that travels is a swipe the moment it passes a
// share of the pad, sideways (direction) or up and down (tempo); no hold
// before it (2026-11-10). A press that never travels so far is a tap.
namespace
{
	using Result = OrbitSwipe::Result;
	constexpr float width = 100.0f, height = 50.0f;
	// Every touch reaches JUCE twice: as touch#0 and as X's emulated mouse#0.
	constexpr OrbitSwipe::Pointer touch { 1, 0 }, mouse { 0, 0 };

	// Down at the pad's middle, then moved by (dx, dy): what the move made of it.
	Result moved (float dx, float dy)
	{
		OrbitSwipe swipe;
		swipe.down (touch, 50.0f, 25.0f);
		return swipe.drag (touch, 50.0f + dx, 25.0f + dy, width, height);
	}
}

TEST (OrbitSwipe, ATapIsNoSwipe)
{
	OrbitSwipe swipe;
	swipe.down (touch, 50.0f, 25.0f);
	EXPECT_FALSE (swipe.swiped());
	EXPECT_FALSE (swipe.up (touch)) << "the press clicks";
}

TEST (OrbitSwipe, ATapThatJittersUnderTheShareIsNoSwipe)
{
	OrbitSwipe swipe;
	swipe.down (touch, 50.0f, 25.0f);
	EXPECT_EQ (swipe.drag (touch, 53.0f, 23.0f, width, height), Result::none);
	EXPECT_EQ (swipe.drag (touch, 47.0f, 27.0f, width, height), Result::none);
	EXPECT_FALSE (swipe.up (touch));
}

TEST (OrbitSwipe, ItIsRecognisedOnTheMoveThatPassesTheShareWithoutAHold)
{
	OrbitSwipe swipe;
	swipe.down (touch, 50.0f, 25.0f);
	EXPECT_EQ (swipe.drag (touch, 60.0f, 25.0f, width, height), Result::none) << "a tenth of the width";
	EXPECT_EQ (swipe.drag (touch, 71.0f, 25.0f, width, height), Result::right) << "past a fifth: now, not at the release";
	EXPECT_TRUE (swipe.swiped());
}

TEST (OrbitSwipe, SidewaysIsTheDirectionUpAndDownTheTempo)
{
	const auto share = OrbitSwipe::travelShare;
	EXPECT_EQ (moved (width * share * 1.05f, 0.0f), Result::right);
	EXPECT_EQ (moved (-width * share * 1.05f, 0.0f), Result::left);
	EXPECT_EQ (moved (0.0f, -height * share * 1.05f), Result::up) << "up the screen";
	EXPECT_EQ (moved (0.0f, height * share * 1.05f), Result::down);
	EXPECT_EQ (moved (width * share * 0.95f, height * share * 0.95f), Result::none) << "neither axis far enough";
}

// Each axis is measured against the pad's own size along it.
TEST (OrbitSwipe, TheDominantAxisWins)
{
	EXPECT_EQ (moved (30.0f, 12.0f), Result::right) << "0.3 of the width against 0.24 of the height";
	EXPECT_EQ (moved (22.0f, 14.0f), Result::down) << "0.22 of the width against 0.28 of the height";
	EXPECT_EQ (moved (-25.0f, -15.0f), Result::up);
}

TEST (OrbitSwipe, TheShareScalesWithThePad)
{
	OrbitSwipe small, large;
	small.down (touch, 10.0f, 10.0f);
	large.down (touch, 10.0f, 10.0f);
	EXPECT_EQ (small.drag (touch, 25.0f, 10.0f, 60.0f, 30.0f), Result::right) << "15 px of 60";
	EXPECT_EQ (large.drag (touch, 25.0f, 10.0f, 300.0f, 30.0f), Result::none) << "15 px of 300";
}

TEST (OrbitSwipe, OnePressMakesOneSwipe)
{
	OrbitSwipe swipe;
	swipe.down (touch, 50.0f, 25.0f);
	EXPECT_EQ (swipe.drag (touch, 80.0f, 25.0f, width, height), Result::right);
	EXPECT_EQ (swipe.drag (touch, 99.0f, 25.0f, width, height), Result::none) << "further: nothing more";
	EXPECT_EQ (swipe.drag (touch, 80.0f, 0.0f, width, height), Result::none) << "nor on the other axis";
	EXPECT_EQ (swipe.drag (touch, 10.0f, 25.0f, width, height), Result::none) << "nor back the other way";
	EXPECT_TRUE (swipe.up (touch)) << "the release is no click";
	EXPECT_FALSE (swipe.swiped());

	swipe.down (touch, 50.0f, 25.0f);
	EXPECT_EQ (swipe.drag (touch, 20.0f, 25.0f, width, height), Result::left) << "the next press swipes afresh";
}

// The twin of a finger (the emulated mouse) moves with it: the swipe comes once.
TEST (OrbitSwipe, TheTwinOfAFingerNeitherSwipesAgainNorClicks)
{
	OrbitSwipe swipe;
	swipe.down (touch, 50.0f, 25.0f);
	swipe.down (mouse, 50.0f, 25.0f);
	EXPECT_EQ (swipe.drag (touch, 75.0f, 25.0f, width, height), Result::right);
	EXPECT_EQ (swipe.drag (mouse, 75.0f, 25.0f, width, height), Result::none);
	EXPECT_TRUE (swipe.up (touch));
	EXPECT_FALSE (swipe.up (mouse)) << "the twin's release ends nothing";
	EXPECT_FALSE (swipe.swiped());
}

TEST (OrbitSwipe, TheTwinsOwnTravelCountsFromItsOwnStart)
{
	OrbitSwipe swipe;
	swipe.down (touch, 50.0f, 25.0f);
	swipe.down (mouse, 70.0f, 25.0f);   // an emulated pointer that lands elsewhere
	EXPECT_EQ (swipe.drag (mouse, 80.0f, 25.0f, width, height), Result::none) << "10 px from its own start";
	EXPECT_EQ (swipe.drag (mouse, 91.0f, 25.0f, width, height), Result::right);
}

TEST (OrbitSwipe, APointerThatNeverWentDownIsIgnored)
{
	OrbitSwipe swipe;
	EXPECT_EQ (swipe.drag (touch, 90.0f, 25.0f, width, height), Result::none);
	EXPECT_FALSE (swipe.up (touch));
	swipe.down (touch, 50.0f, 25.0f);
	EXPECT_EQ (swipe.drag ({ 1, 3 }, 90.0f, 25.0f, width, height), Result::none) << "another finger, never down here";
}

TEST (OrbitSwipe, ATwinsTapIsATap)
{
	OrbitSwipe swipe;
	swipe.down (touch, 50.0f, 25.0f);
	swipe.down (mouse, 50.0f, 25.0f);
	EXPECT_EQ (swipe.drag (mouse, 52.0f, 25.0f, width, height), Result::none);
	EXPECT_FALSE (swipe.up (mouse));
	EXPECT_FALSE (swipe.up (touch));
}
