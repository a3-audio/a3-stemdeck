// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>

#include "StemEvents.h"

using namespace stemevents;

namespace
{
	Event at (int stem, double beat)
	{
		Event e;
		e.stem = stem;
		e.beat = beat;
		return e;
	}
}

TEST (StemEvents, AddKeepsBeatOrder)
{
	EventList list;
	EXPECT_EQ (list.add (at (0, 8.0)), EventList::Add::added);
	EXPECT_EQ (list.add (at (1, 2.0)), EventList::Add::added);
	EXPECT_EQ (list.add (at (2, 4.0)), EventList::Add::added);
	ASSERT_EQ (list.events().size(), 3u);
	EXPECT_EQ (list.events()[0].beat, 2.0);
	EXPECT_EQ (list.events()[1].beat, 4.0);
	EXPECT_EQ (list.events()[2].beat, 8.0);
}

TEST (StemEvents, AddRefusesTheSameStemAndBeat)
{
	EventList list;
	list.add (at (1, 4.0));
	EXPECT_EQ (list.add (at (1, 4.0 + 1e-9)), EventList::Add::occupied);
	EXPECT_EQ (list.events().size(), 1u);
}

TEST (StemEvents, TheSameBeatOnAnotherStemIsFine)
{
	EventList list;
	list.add (at (2, 4.0));
	EXPECT_EQ (list.add (at (1, 4.0)), EventList::Add::added);
	ASSERT_EQ (list.events().size(), 2u);
	EXPECT_EQ (list.events()[0].stem, 1);
	EXPECT_EQ (list.events()[1].stem, 2);
}

TEST (StemEvents, AddRefusesBeyond512)
{
	EventList list;
	for (int i = 0; i < maxEvents; ++i)
		ASSERT_EQ (list.add (at (0, i)), EventList::Add::added);
	EXPECT_EQ (list.add (at (0, 1000.0)), EventList::Add::full);
	EXPECT_EQ (list.events().size(), static_cast<std::size_t> (maxEvents));
}

TEST (StemEvents, NudgeMovesAndKeepsOrder)
{
	EventList list;
	list.add (at (0, 4.0));
	list.add (at (1, 8.0));
	EXPECT_TRUE (list.nudge (0, 6.0));
	ASSERT_EQ (list.events().size(), 2u);
	EXPECT_EQ (list.events()[0].stem, 1);
	EXPECT_EQ (list.events()[1].stem, 0);
	EXPECT_EQ (list.events()[1].beat, 10.0);
}

TEST (StemEvents, NudgeOntoAnOccupiedSlotIsRefused)
{
	EventList list;
	list.add (at (0, 4.0));
	list.add (at (0, 8.0));
	EXPECT_FALSE (list.nudge (0, 4.0));
	EXPECT_EQ (list.events()[0].beat, 4.0);
	EXPECT_EQ (list.events()[1].beat, 8.0);
}

TEST (StemEvents, NegativeBeatsAreAllowed)
{
	EventList list;
	EXPECT_EQ (list.add (at (0, -4.0)), EventList::Add::added);
	EXPECT_EQ (list.add (at (0, 2.0)), EventList::Add::added);
	EXPECT_EQ (list.events()[0].beat, -4.0);
	EXPECT_EQ (list.indexAt (0, -4.0), 0);
	EXPECT_EQ (list.indexAt (0, 3.0), -1);
}

TEST (StemEvents, RemoveAndSetActiveRejectBadIndexes)
{
	EventList list;
	list.add (at (0, 4.0));
	EXPECT_FALSE (list.remove (-1));
	EXPECT_FALSE (list.remove (1));
	EXPECT_FALSE (list.setActive (-1, false));
	EXPECT_FALSE (list.setActive (1, false));
	EXPECT_FALSE (list.nudge (1, 1.0));
	EXPECT_TRUE (list.setActive (0, false));
	EXPECT_FALSE (list.events()[0].active);
	EXPECT_TRUE (list.remove (0));
	EXPECT_TRUE (list.events().empty());
}
