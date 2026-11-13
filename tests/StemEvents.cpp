// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>

#include "StemEvents.h"

#include <cmath>

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

//==============================================================================
// The clip settings show and edit the event at the playhead (deck standing).

namespace
{
	Event shot (int stem, double beat, double lengthBars)
	{
		Event e = at (stem, beat);
		e.lengthBars = lengthBars;
		return e;
	}
}

TEST (StemEvents, CoveringFindsTheEventFromItsStartToJustBeforeItsEnd)
{
	EventList list;
	list.add (shot (1, 8.0, 1.0));   // beats 8 to 12
	EXPECT_EQ (list.indexCovering (1, 8.0), 0);            // the start, exactly
	EXPECT_EQ (list.indexCovering (1, 8.0 - 1e-9), 0);     // a hair below, as the scheduler starts it
	EXPECT_EQ (list.indexCovering (1, 10.0), 0);           // the middle
	EXPECT_EQ (list.indexCovering (1, 11.999), 0);
	EXPECT_EQ (list.indexCovering (1, 12.0), -1);          // the end: the shot is home again
	EXPECT_EQ (list.indexCovering (1, 7.9), -1);
	EXPECT_EQ (list.indexCovering (2, 10.0), -1);          // another stem
}

TEST (StemEvents, CoveringPrefersTheLaterStartAsItTakesOverTheStem)
{
	EventList list;
	list.add (shot (0, 0.0, 8.0));   // beats 0 to 32
	list.add (shot (0, 4.0, 1.0));   // beats 4 to 8, replaces the first while it runs
	EXPECT_EQ (list.indexCovering (0, 2.0), 0);
	EXPECT_EQ (list.indexCovering (0, 4.0), 1);
	EXPECT_EQ (list.indexCovering (0, 6.0), 1);
	EXPECT_EQ (list.indexCovering (0, 8.0), 0);   // the second is over; the list knows no more
}

TEST (StemEvents, CoveringCountsQuarterBarShots)
{
	EventList list;
	list.add (shot (3, 16.0, 0.25));   // one beat
	EXPECT_EQ (list.indexCovering (3, 16.5), 0);
	EXPECT_EQ (list.indexCovering (3, 17.0), -1);
}

TEST (StemEvents, TheSettingsShowAnEventOnlyWhileTheDeckStands)
{
	EventList list;
	list.add (shot (2, 4.0, 1.0));
	EXPECT_EQ (shownInSettings (list, 2, true, 5.0), 0);
	EXPECT_EQ (shownInSettings (list, 2, false, 5.0), -1);   // playing: the pad defaults
	EXPECT_EQ (shownInSettings (list, 2, true, std::nan ("")), -1);   // no grid
	EXPECT_EQ (shownInSettings (list, 1, true, 5.0), -1);
}

TEST (StemEvents, ShotOfReadsTheEventsSettings)
{
	Event e = shot (1, 4.0, 0.5);
	e.shape = motionlite::Shape::scat;
	e.reversed = true;
	e.speed = 2.0;
	const auto s = shotOf (e);
	EXPECT_EQ (s.shape, motionlite::Shape::scat);
	EXPECT_EQ (s.lengthBars, 0.5);
	EXPECT_TRUE (s.reversed);
	EXPECT_EQ (s.speed, 2.0);
}

TEST (StemEvents, KeysStepTheShotWithinItsSets)
{
	const Shot plain;   // ORBIT, 4 bars, forward, x1
	EXPECT_EQ (afterKey (plain, ShotKey::shorter).lengthBars, 2.0);
	EXPECT_EQ (afterKey (plain, ShotKey::longer).lengthBars, 8.0);
	EXPECT_EQ (afterKey (afterKey (plain, ShotKey::longer), ShotKey::longer).lengthBars, 8.0);
	EXPECT_TRUE (afterKey (plain, ShotKey::reverse).reversed);
	EXPECT_FALSE (afterKey (afterKey (plain, ShotKey::reverse), ShotKey::forward).reversed);
	EXPECT_EQ (afterKey (plain, ShotKey::faster).speed, 2.0);
	EXPECT_EQ (afterKey (plain, ShotKey::slower).speed, 0.5);
	Shot quarter;
	quarter.lengthBars = 0.25;
	EXPECT_EQ (afterKey (quarter, ShotKey::shorter).lengthBars, 0.25);
	EXPECT_EQ (afterKey (plain, ShotKey::faster).shape, motionlite::Shape::orbit);   // the rest stays
}

TEST (StemEvents, SetShotChangesTheEventAndMakesItAHandOne)
{
	EventList list;
	Event e = shot (1, 4.0, 4.0);
	e.origin = Origin::prepared;
	e.seed = 77;
	list.add (e);
	Shot s;
	s.shape = motionlite::Shape::rise;
	s.lengthBars = 0.25;
	s.reversed = true;
	s.speed = 0.25;
	ASSERT_TRUE (list.setShot (0, s));
	const auto& changed = list.events()[0];
	EXPECT_EQ (changed.shape, motionlite::Shape::rise);
	EXPECT_EQ (changed.lengthBars, 0.25);
	EXPECT_TRUE (changed.reversed);
	EXPECT_EQ (changed.speed, 0.25);
	EXPECT_EQ (changed.origin, Origin::hand);
	EXPECT_EQ (changed.seed, 77u);   // the rest stays: slot, seed, active
	EXPECT_EQ (changed.beat, 4.0);
	EXPECT_TRUE (changed.active);
}

TEST (StemEvents, SetShotRefusesWhatTheSettingsCannotShow)
{
	EventList list;
	Event e = shot (1, 4.0, 4.0);
	e.origin = Origin::prepared;
	list.add (e);
	Shot s;
	s.lengthBars = 3.0;
	EXPECT_FALSE (list.setShot (0, s));   // no such length
	s.lengthBars = 4.0;
	s.speed = 3.0;
	EXPECT_FALSE (list.setShot (0, s));   // no such tempo step
	s.speed = 1.0;
	EXPECT_FALSE (list.setShot (0, s));   // nothing changes: the prepared event stays prepared
	EXPECT_FALSE (list.setShot (1, s));
	EXPECT_EQ (list.events()[0].origin, Origin::prepared);
	EXPECT_EQ (list.events()[0].lengthBars, 4.0);
}
