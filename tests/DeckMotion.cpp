// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>

#include "DeckMotion.h"

#include <juce_events/juce_events.h>

#include <cmath>
#include <vector>

// A deck's prepared events fire in its audio path: DeckMotion::advance runs the
// scheduler on the clock it is given and starts each due event on its stem.
namespace
{
using motionlite::PadState;
using motionlite::Shape;

constexpr double oneBeatAt120 = 0.5;   // seconds

motionlite::Clock at (double beat)
{
	motionlite::Clock clock;
	clock.beat = beat;
	return clock;
}

motionlite::Clock looped (double beat, std::uint32_t wraps, double loopIn, double loopOut)
{
	auto clock = at (beat);
	clock.wraps = wraps;
	clock.loopBeats = std::make_pair (loopIn, loopOut);
	return clock;
}

stemevents::Event event (int stem, double beat, Shape shape, double lengthBars = 4.0)
{
	stemevents::Event e;
	e.stem = stem;
	e.beat = beat;
	e.shape = shape;
	e.lengthBars = lengthBars;
	return e;
}

// The queue's and the bank's producer is the message thread: this one. No GUI.
struct MessageThreadHere
{
	MessageThreadHere() { juce::MessageManager::getInstance(); }
	~MessageThreadHere() { juce::MessageManager::deleteInstance(); }
};

class DeckMotionEvents : public ::testing::Test
{
protected:
	void advance (const motionlite::Clock& clock, double blockSeconds = 0.0)
	{
		motion.advance (clock, blockSeconds, settings, played);
	}

	MessageThreadHere messageThread;
	DeckSpatial::Controls settings, played;
	DeckMotion motion;
};
}

TEST_F (DeckMotionEvents, AnEventStartsItsStemsMotion)
{
	motion.setEvents ({ event (2, 8.0, Shape::rise) });
	advance (at (7.9));
	EXPECT_EQ (motion.stemState (2), PadState::idle);
	advance (at (8.1));
	EXPECT_EQ (motion.padState (2, Shape::rise), PadState::running) << "at its own beat, not the next bar";
}

// RISE narrows the stem on its way up: the played width follows, and is the
// home width again when the shot ends or another takes over.
TEST_F (DeckMotionEvents, ARisePlaysItsNarrowedWidthAndGivesTheHomeBack)
{
	settings.width[2] = 180.0f;
	motion.setEvents ({ event (2, 8.0, Shape::rise), event (2, 40.0, Shape::rise), event (2, 52.5, Shape::orbit) });
	advance (at (7.9));
	advance (at (8.1), 6.0 * oneBeatAt120 * 2.0);   // 12 beats on: the top
	advance (at (20.1), 4.0 * oneBeatAt120 * 2.0);
	const float top = motionlite::riseWidthFraction * 180.0f;
	EXPECT_NEAR (motion.posePlayed (2).width, top, 1e-2f);
	advance (at (24.1));
	EXPECT_FLOAT_EQ (played.width[2].load(), 180.0f) << "the shot is over";
	EXPECT_FLOAT_EQ (motion.posePlayed (2).width, 180.0f);

	advance (at (39.9));
	advance (at (40.1), 6.0 * oneBeatAt120 * 2.0);
	advance (at (52.1));
	EXPECT_NEAR (played.width[2].load(), top, 1e-2f);
	advance (at (52.6));
	EXPECT_EQ (motion.padState (2, Shape::orbit), PadState::running);
	EXPECT_FLOAT_EQ (played.width[2].load(), 180.0f) << "taken over, the width is home at once";
}

TEST_F (DeckMotionEvents, AnEventTakesOverALivePress)
{
	motion.press (0, Shape::orbit, true, 1);   // SHIFT: running at once
	advance (at (0.5));
	ASSERT_EQ (motion.padState (0, Shape::orbit), PadState::running);

	motion.setEvents ({ event (0, 1.0, Shape::ping) });
	advance (at (1.5));
	EXPECT_EQ (motion.padState (0, Shape::ping), PadState::running);
	EXPECT_EQ (motion.padState (0, Shape::orbit), PadState::idle);
}

TEST_F (DeckMotionEvents, EventsOffFiresNothing)
{
	motion.setEvents ({ event (1, 4.0, Shape::orbit), event (1, 6.0, Shape::scat) });
	motion.setEventsEnabled (false);
	EXPECT_FALSE (motion.eventsEnabled());
	advance (at (3.0));
	advance (at (5.0));
	EXPECT_EQ (motion.stemState (1), PadState::idle);

	motion.setEventsEnabled (true);
	advance (at (5.5));
	EXPECT_EQ (motion.stemState (1), PadState::idle) << "the one passed while off does not follow";
	advance (at (6.5));
	EXPECT_EQ (motion.padState (1, Shape::scat), PadState::running);
}

TEST_F (DeckMotionEvents, EventsAreForTheirOwnStemOnly)
{
	motion.setEvents ({ event (3, 2.0, Shape::orbit), event (7, 2.0, Shape::ping), event (-1, 2.0, Shape::rise) });
	advance (at (1.0));
	advance (at (3.0));
	EXPECT_EQ (motion.padState (3, Shape::orbit), PadState::running);
	for (int stem = 0; stem < 3; ++stem)
		EXPECT_EQ (motion.stemState (stem), PadState::idle) << stem;
}

TEST_F (DeckMotionEvents, TheLastDueEventOfAStemInABlockWins)
{
	motion.setEvents ({ event (0, 2.0, Shape::orbit), event (0, 2.5, Shape::rise) });
	advance (at (1.0));
	advance (at (3.0));
	EXPECT_EQ (motion.padState (0, Shape::rise), PadState::running);
}

TEST_F (DeckMotionEvents, NewTrackClearsThePreviousTracksEvents)
{
	motion.setEvents ({ event (0, 4.0, Shape::orbit) });
	advance (at (3.0));
	motion.newTrack();
	advance (at (3.5));
	advance (at (4.5));
	EXPECT_EQ (motion.stemState (0), PadState::idle);

	// The next track's first block only sets where it starts: no window from the old track's beat.
	motion.setEvents ({ event (0, 10.0, Shape::ping) });
	advance (at (9.0));
	motion.newTrack();
	motion.setEvents ({ event (0, 10.0, Shape::ping), event (0, 13.0, Shape::orbit) });
	advance (at (12.0));
	EXPECT_EQ (motion.stemState (0), PadState::idle) << "9 -> 12 spans two tracks";
	advance (at (12.5));
	EXPECT_EQ (motion.stemState (0), PadState::idle);
	advance (at (13.5));
	EXPECT_EQ (motion.padState (0, Shape::orbit), PadState::running) << "the new track's own events fire";
}

TEST_F (DeckMotionEvents, ALoopPassFiresAgain)
{
	// An event on the loop-in of a 4-beat loop: every wrap starts it afresh.
	motion.setEvents ({ event (0, 0.0, Shape::orbit) });
	advance (looped (3.0, 0, 0.0, 4.0));
	advance (looped (0.25, 1, 0.0, 4.0), oneBeatAt120);
	ASSERT_EQ (motion.padState (0, Shape::orbit), PadState::running);
	advance (looped (3.5, 1, 0.0, 4.0), 3 * oneBeatAt120);
	EXPECT_EQ (motion.progress (0), (std::pair<int, int> { 5, 16 }));

	advance (looped (0.25, 2, 0.0, 4.0));
	EXPECT_EQ (motion.progress (0), (std::pair<int, int> { 1, 16 })) << "restarted, not carried on";
}

TEST_F (DeckMotionEvents, DirectionReachesTheAudioThreadThroughTheFifo)
{
	DeckMotion forward;
	EXPECT_TRUE (motion.setReversed (1, true));
	EXPECT_TRUE (motion.reversed (1)) << "readable at once, as setLength";
	EXPECT_FALSE (motion.reversed (0));
	EXPECT_FALSE (motion.setReversed (4, true));

	for (auto* deck : { &motion, &forward })
	{
		deck->press (1, Shape::orbit, true, 1);
		deck->advance (at (0.0), oneBeatAt120, settings, played);
		deck->advance (at (1.0), oneBeatAt120, settings, played);
	}
	const auto turned = motion.posePlayed (1).azimuth, plain = forward.posePlayed (1).azimuth;
	EXPECT_NEAR (plain, 22.5f, 1.0e-3f);
	EXPECT_NEAR (turned, -22.5f, 1.0e-3f) << "the audio thread's stem turns the other way";
}

TEST_F (DeckMotionEvents, AnEventCarriesItsOwnDirection)
{
	auto reversedEvent = event (2, 1.0, Shape::orbit);
	reversedEvent.reversed = true;
	motion.setEvents ({ reversedEvent });
	advance (at (0.9));
	advance (at (1.1), oneBeatAt120);
	advance (at (2.1), oneBeatAt120);
	EXPECT_NEAR (motion.posePlayed (2).azimuth, -22.5f, 1.0e-3f);
	EXPECT_FALSE (motion.reversed (2)) << "the stem's own setting stays";
}

// The player's position and its seek counter are two atomics: a block may read
// the counter before the jump lands. The seek covers the next block too, so the
// jump is never read as a crossing.
TEST_F (DeckMotionEvents, ASeekStillCoversTheBlockAfterIt)
{
	motion.setEvents ({ event (0, 6.0, Shape::orbit) });
	advance (at (2.0));
	auto counted = at (2.1);   // the counter moved, the position not yet
	counted.seeks = 1;
	advance (counted);
	auto landed = at (8.0);
	landed.seeks = 1;
	advance (landed);
	EXPECT_EQ (motion.stemState (0), PadState::idle) << "2.1 -> 8 is a jump";

	motion.setEvents ({ event (0, 6.0, Shape::orbit), event (0, 8.2, Shape::ping) });
	landed.beat = 8.5;
	advance (landed);
	EXPECT_EQ (motion.padState (0, Shape::ping), PadState::running) << "from the landing on, it plays as ever";
}

TEST_F (DeckMotionEvents, PlayStartingOnAnEventsBeatFiresIt)
{
	motion.setEvents ({ event (0, 4.0, Shape::rise) });
	auto standing = at (4.0);
	standing.playing = false;
	advance (standing);
	advance (at (4.1));
	EXPECT_EQ (motion.padState (0, Shape::rise), PadState::running);
}

// CONTINUE changes the track on the audio thread; the message thread hears of
// it a timer tick later. In between, an old-track event whose beat the new
// track crosses must not fire: nothing fires until that track's events are in.
TEST_F (DeckMotionEvents, AnOldTracksEventDoesNotFireOnTheNextTrackBeforeItsEventsArrive)
{
	motion.setEvents ({ event (1, 2.0, Shape::ping) });   // the old track's
	advance (at (300.0));

	const auto nextTrack = [] (double beat)
	{
		auto clock = at (beat);
		clock.track = 1;   // handed over
		clock.seeks = 1;   // the change counts as a jump
		return clock;
	};
	advance (nextTrack (-0.5));
	advance (nextTrack (0.5), oneBeatAt120);
	advance (nextTrack (1.5), oneBeatAt120);
	advance (nextTrack (2.5), oneBeatAt120);
	EXPECT_EQ (motion.stemState (1), PadState::idle) << "the old track's beat 2 is not the new track's";

	motion.newTrack();
	motion.setEvents ({ event (3, 4.0, Shape::rise) });
	motion.trackReady (1);
	advance (nextTrack (3.5), oneBeatAt120);
	advance (nextTrack (4.5), oneBeatAt120);
	EXPECT_EQ (motion.padState (3, Shape::rise), PadState::running) << "the new track's own events fire";
	EXPECT_EQ (motion.stemState (1), PadState::idle);
}

// The gate opens only for the track it was told: events ready for an older
// track do not play on a newer one.
TEST_F (DeckMotionEvents, EventsReadyForAnOlderTrackDoNotPlayOnANewerOne)
{
	motion.setEvents ({ event (0, 4.0, Shape::orbit) });
	motion.trackReady (0);
	auto clock = at (3.5);
	clock.track = 2;
	advance (clock);
	clock.beat = 4.5;
	advance (clock);
	EXPECT_EQ (motion.stemState (0), PadState::idle);
}
