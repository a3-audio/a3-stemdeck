// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ThumbnailLoad.h"

#include <gtest/gtest.h>

using thumbnailload::StemState;
using thumbnailload::Tracker;

namespace
{
	using States = std::array<StemState, thumbnailload::stems>;

	// Every stem `proportion` read, its peak so far `peak`; complete at 1.
	States allAt (double proportion, float peak)
	{
		States states;
		states.fill ({ proportion >= 1.0, proportion, peak });
		return states;
	}
}

TEST (ThumbnailLoad, TheGainStaysWhileLoadingAndIsSetOnceWhenComplete)
{
	Tracker tracker;
	tracker.reset();
	tracker.update (allAt (0.1, 0.1f));
	EXPECT_FLOAT_EQ (tracker.gain (0), 1.0f) << "a half-read track does not zoom the picture";
	tracker.update (allAt (0.6, 0.4f));
	EXPECT_FLOAT_EQ (tracker.gain (0), 1.0f);

	tracker.update (allAt (1.0, 0.5f));
	EXPECT_FLOAT_EQ (tracker.gain (0), thumbnailload::gainFor (0.5f)) << "complete: set from the whole track";

	tracker.update (allAt (1.0, 0.25f));
	EXPECT_FLOAT_EQ (tracker.gain (0), thumbnailload::gainFor (0.5f)) << "and only once";
}

TEST (ThumbnailLoad, EachStemGetsItsGainWhenItIsComplete)
{
	Tracker tracker;
	tracker.reset();
	auto states = allAt (0.5, 0.2f);
	states[2] = { true, 1.0, 0.5f };
	tracker.update (states);
	EXPECT_FLOAT_EQ (tracker.gain (2), thumbnailload::gainFor (0.5f));
	EXPECT_FLOAT_EQ (tracker.gain (0), 1.0f);
}

TEST (ThumbnailLoad, WhileLoadingItRedrawsOnlyAtStepsAndCompletions)
{
	Tracker tracker;
	tracker.reset();
	int redraws = 0;
	for (int step = 1; step < 100; ++step)
		redraws += tracker.update (allAt (step / 100.0, 0.3f)) ? 1 : 0;
	EXPECT_LE (redraws, (int) (1.0 / thumbnailload::progressStep)) << "not on each of 99 steps";
	EXPECT_GE (redraws, 1) << "the picture still fills in";

	EXPECT_TRUE (tracker.update (allAt (1.0, 0.3f))) << "complete: drawn";
	EXPECT_FALSE (tracker.update (allAt (1.0, 0.3f))) << "nothing new";
}

TEST (ThumbnailLoad, ANewSetStartsOverAtOne)
{
	Tracker tracker;
	tracker.reset();
	tracker.update (allAt (1.0, 0.25f));
	ASSERT_GT (tracker.gain (1), 1.0f);

	tracker.reset();
	EXPECT_FLOAT_EQ (tracker.gain (1), 1.0f);
	tracker.update (allAt (0.5, 0.5f));
	EXPECT_FLOAT_EQ (tracker.gain (1), 1.0f) << "the new set's gain waits for its own load";
	tracker.update (allAt (1.0, 0.5f));
	EXPECT_FLOAT_EQ (tracker.gain (1), thumbnailload::gainFor (0.5f));
}

TEST (ThumbnailLoad, TheGainFillsTheLaneWithinItsCap)
{
	EXPECT_FLOAT_EQ (thumbnailload::gainFor (0.0f), 1.0f) << "silence";
	EXPECT_FLOAT_EQ (thumbnailload::gainFor (1.0f), 1.0f) << "never below one";
	EXPECT_FLOAT_EQ (thumbnailload::gainFor (0.01f), thumbnailload::maxGain);
	EXPECT_FLOAT_EQ (thumbnailload::gainFor (0.5f), thumbnailload::fill / 0.5f);
}
