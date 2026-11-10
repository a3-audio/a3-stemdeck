// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>

#include "EventScheduler.h"

#include <vector>

using namespace stemevents;

namespace
{
	Event at (double beat, bool active = true)
	{
		Event e;
		e.beat = beat;
		e.active = active;
		return e;
	}

	motionlite::Clock playingAt (double beat)
	{
		motionlite::Clock clock;
		clock.beat = beat;
		return clock;
	}

	motionlite::Clock loopedAt (double beat, std::uint32_t wraps, double loopIn, double loopOut)
	{
		auto clock = playingAt (beat);
		clock.wraps = wraps;
		clock.loopBeats = std::make_pair (loopIn, loopOut);
		return clock;
	}

	constexpr int capacity = 16;

	std::vector<int> fire (EventScheduler& scheduler, const motionlite::Clock& clock,
	                       const std::vector<Event>& events, int limit = capacity, double blockBeats = 0.0)
	{
		int out[capacity + 1];
		out[limit] = -99;
		const int found = scheduler.update (clock, events.data(), (int) events.size(), out, limit, blockBeats);
		EXPECT_EQ (out[limit], -99) << "wrote beyond capacity";
		return std::vector<int> (out, out + found);
	}

	using Fired = std::vector<int>;

	// One audio block that played `blockBeats` up to the clock's beat.
	std::vector<int> play (EventScheduler& scheduler, const motionlite::Clock& clock,
	                       const std::vector<Event>& events, double blockBeats)
	{
		return fire (scheduler, clock, events, capacity, blockBeats);
	}

	motionlite::Clock standingAt (double beat)
	{
		auto clock = playingAt (beat);
		clock.playing = false;
		return clock;
	}

	motionlite::Clock jumpedTo (double beat, std::uint32_t seeks)
	{
		auto clock = playingAt (beat);
		clock.seeks = seeks;
		return clock;
	}

	// A beat from float arithmetic that should be `beat` and came out a hair above.
	double justAbove (double beat)
	{
		return beat + 1.0e-12;
	}

	constexpr double aBlock = 0.1;   // beats
}

TEST (EventScheduler, FiresWhenTheBeatCrossesIt)
{
	const std::vector<Event> events { at (8.0), at (16.0) };
	EventScheduler scheduler;
	fire (scheduler, playingAt (7.9), events);
	EXPECT_EQ (fire (scheduler, playingAt (8.1), events), Fired ({ 0 }));
	EXPECT_EQ (fire (scheduler, playingAt (15.9), events), Fired());
	EXPECT_EQ (fire (scheduler, playingAt (16.05), events), Fired ({ 1 }));
}

TEST (EventScheduler, DoesNotFireTwice)
{
	const std::vector<Event> events { at (8.0) };
	EventScheduler scheduler;
	fire (scheduler, playingAt (7.9), events);
	EXPECT_EQ (fire (scheduler, playingAt (8.1), events), Fired ({ 0 }));
	EXPECT_EQ (fire (scheduler, playingAt (8.1), events), Fired());
	EXPECT_EQ (fire (scheduler, playingAt (8.3), events), Fired());
}

TEST (EventScheduler, HalfOpenWindowFiresAtTheFromBeat)
{
	const std::vector<Event> events { at (8.0) };
	EventScheduler scheduler;
	fire (scheduler, playingAt (7.9), events);
	EXPECT_EQ (fire (scheduler, playingAt (8.0), events), Fired()) << "the to-beat is outside the window";
	EXPECT_EQ (fire (scheduler, playingAt (8.1), events), Fired ({ 0 })) << "the from-beat is inside";
}

TEST (EventScheduler, StoppedOrBackwardFiresNothing)
{
	const std::vector<Event> events { at (8.0) };

	EventScheduler stopped;
	fire (stopped, playingAt (7.9), events);
	auto paused = playingAt (8.1);
	paused.playing = false;
	EXPECT_EQ (fire (stopped, paused, events), Fired()) << "a jog over the event while paused";
	EXPECT_EQ (fire (stopped, playingAt (8.2), events), Fired()) << "play resumes after the event";

	EventScheduler backward;
	fire (backward, playingAt (8.1), events);
	auto reverse = playingAt (7.9);
	reverse.rate = -1.0;
	EXPECT_EQ (fire (backward, reverse, events), Fired());
	auto still = playingAt (8.1);
	still.rate = 0.0;
	EXPECT_EQ (fire (backward, still, events), Fired()) << "a zero rate is not forward play";

	EventScheduler bent;
	fire (bent, playingAt (7.9), events);
	auto pitchBend = playingAt (8.1);
	pitchBend.rate = 0.92;
	EXPECT_EQ (fire (bent, pitchBend, events), Fired ({ 0 })) << "a pitch bend still counts as forward";
}

TEST (EventScheduler, SeekFiresNothingAndRebases)
{
	const std::vector<Event> events { at (8.0) };
	EventScheduler scheduler;
	fire (scheduler, playingAt (7.9), events);

	auto jumped = playingAt (8.5);
	jumped.seeks = 1;
	EXPECT_EQ (play (scheduler, jumped, events, aBlock), Fired()) << "a jump over the event";
	jumped.beat = 8.6;
	EXPECT_EQ (play (scheduler, jumped, events, aBlock), Fired());

	auto back = playingAt (7.95);
	back.seeks = 2;
	EXPECT_EQ (play (scheduler, back, events, aBlock), Fired());
	back.beat = 8.05;
	EXPECT_EQ (play (scheduler, back, events, aBlock), Fired ({ 0 })) << "from the new place it crosses again";

	auto jumpAndWrap = loopedAt (8.2, 1, 6.0, 10.0);
	jumpAndWrap.seeks = 3;
	fire (scheduler, loopedAt (9.9, 0, 6.0, 10.0), events);
	EXPECT_EQ (fire (scheduler, jumpAndWrap, events), Fired()) << "a seek wins over a wrap";
}

TEST (EventScheduler, FirstBlockFiresNothing)
{
	const std::vector<Event> events { at (8.0) };
	EventScheduler scheduler;
	auto first = playingAt (8.0);
	first.seeks = 5;
	first.wraps = 3;
	EXPECT_EQ (fire (scheduler, first, events), Fired());
	auto second = first;
	second.beat = 8.1;
	EXPECT_EQ (fire (scheduler, second, events), Fired ({ 0 })) << "the counters of the first block are the base";
}

TEST (EventScheduler, NoGridFiresNothing)
{
	const std::vector<Event> events { at (8.0) };
	EventScheduler scheduler;
	motionlite::Clock noGrid;
	EXPECT_EQ (fire (scheduler, noGrid, events), Fired());
	EXPECT_EQ (fire (scheduler, noGrid, events), Fired());

	fire (scheduler, playingAt (7.9), events);
	EXPECT_EQ (fire (scheduler, noGrid, events), Fired());
	EXPECT_EQ (fire (scheduler, playingAt (8.1), events), Fired()) << "a grid that comes back is a new start";
}

TEST (EventScheduler, InactiveNeverFires)
{
	const std::vector<Event> events { at (8.0, false), at (8.05) };
	EventScheduler scheduler;
	fire (scheduler, playingAt (7.9), events);
	EXPECT_EQ (fire (scheduler, playingAt (8.1), events), Fired ({ 1 }));

	const std::vector<Event> looped { at (16.0, false), at (19.95, false) };
	fire (scheduler, loopedAt (19.9, 0, 16.0, 20.0), looped);
	EXPECT_EQ (fire (scheduler, loopedAt (16.3, 1, 16.0, 20.0), looped), Fired());
}

TEST (EventScheduler, LoopWrapFiresTailAndHead)
{
	// Loop 16..20 beats; the block starts at 19.9, wraps once and ends at 16.3.
	const std::vector<Event> events { at (15.0), at (16.2), at (18.0), at (19.95), at (20.5) };
	EventScheduler scheduler;
	fire (scheduler, loopedAt (19.9, 0, 16.0, 20.0), events);
	EXPECT_EQ (fire (scheduler, loopedAt (16.3, 1, 16.0, 20.0), events), Fired ({ 3, 1 }))
		<< "the tail [19.9, 20) first, then the head [16, 16.3)";
	EXPECT_EQ (fire (scheduler, loopedAt (16.5, 1, 16.0, 20.0), events), Fired());
}

TEST (EventScheduler, AWrapWithoutLoopBeatsFiresNothing)
{
	const std::vector<Event> events { at (16.2), at (19.95) };
	EventScheduler scheduler;
	fire (scheduler, playingAt (19.9), events);
	auto wrapped = playingAt (16.3);
	wrapped.wraps = 1;
	EXPECT_EQ (fire (scheduler, wrapped, events), Fired()) << "without the loop's ends a wrap is a jump";
}

TEST (EventScheduler, EventAtLoopInFiresEveryWrap)
{
	const std::vector<Event> events { at (16.0) };
	EventScheduler scheduler;
	fire (scheduler, loopedAt (15.9, 0, 16.0, 20.0), events);
	EXPECT_EQ (fire (scheduler, loopedAt (16.1, 0, 16.0, 20.0), events), Fired ({ 0 })) << "entering the loop";
	fire (scheduler, loopedAt (19.9, 0, 16.0, 20.0), events);
	EXPECT_EQ (fire (scheduler, loopedAt (16.1, 1, 16.0, 20.0), events), Fired ({ 0 })) << "first wrap";
	fire (scheduler, loopedAt (19.9, 1, 16.0, 20.0), events);
	EXPECT_EQ (fire (scheduler, loopedAt (16.1, 2, 16.0, 20.0), events), Fired ({ 0 })) << "second wrap";

	fire (scheduler, loopedAt (19.9, 2, 16.0, 20.0), events);
	EXPECT_EQ (fire (scheduler, loopedAt (16.0, 3, 16.0, 20.0), events), Fired()) << "a wrap that lands on loop-in";
	EXPECT_EQ (fire (scheduler, loopedAt (16.1, 3, 16.0, 20.0), events), Fired ({ 0 })) << "fires once, in the next block";
}

TEST (EventScheduler, EventAtLoopOutNeverFires)
{
	const std::vector<Event> events { at (20.0) };
	EventScheduler scheduler;
	fire (scheduler, loopedAt (19.9, 0, 16.0, 20.0), events);
	for (std::uint32_t wrap = 1; wrap <= 3; ++wrap)
	{
		EXPECT_EQ (fire (scheduler, loopedAt (16.1, wrap, 16.0, 20.0), events), Fired());
		EXPECT_EQ (fire (scheduler, loopedAt (19.9, wrap, 16.0, 20.0), events), Fired());
	}
	EXPECT_EQ (fire (scheduler, loopedAt (16.0, 5, 16.0, 20.0), events), Fired()) << "two wraps in one block";
}

TEST (EventScheduler, LoopShorterThanABlockFiresEachEventOncePerWrap)
{
	// Loop 16..16.5; the block goes 16.3 -> 16.5 | 16..16.5 | 16..16.5 | 16 -> 16.3: three wraps.
	const std::vector<Event> events { at (16.0), at (16.25), at (16.5) };
	EventScheduler scheduler;
	fire (scheduler, loopedAt (16.3, 0, 16.0, 16.5), events);
	EXPECT_EQ (fire (scheduler, loopedAt (16.3, 3, 16.0, 16.5), events), Fired ({ 0, 1, 0, 1, 0, 1 }));

	// The playhead enters the loop and wraps twice in one block: 15.9 -> 16.5 | 16..16.5 | 16 -> 16.1.
	EventScheduler entering;
	fire (entering, loopedAt (15.9, 0, 16.0, 16.5), events);
	EXPECT_EQ (fire (entering, loopedAt (16.1, 2, 16.0, 16.5), events), Fired ({ 0, 1, 0, 1, 0 }));
}

TEST (EventScheduler, ManyEventsInOneBlockAreBoundedByCapacity)
{
	std::vector<Event> events;
	for (int i = 0; i < 10; ++i)
		events.push_back (at (8.0 + 0.01 * i));
	EventScheduler scheduler;
	fire (scheduler, playingAt (7.9), events);
	EXPECT_EQ (fire (scheduler, playingAt (8.2), events, 4), Fired ({ 0, 1, 2, 3 }));

	const std::vector<Event> oneInLoop { at (16.0) };
	EventScheduler manyWraps;
	fire (manyWraps, loopedAt (16.3, 0, 16.0, 16.5), oneInLoop);
	EXPECT_EQ (fire (manyWraps, loopedAt (16.3, 1000000, 16.0, 16.5), oneInLoop, 4), Fired ({ 0, 0, 0, 0 }));

	const std::vector<Event> noneInLoop { at (8.0) };
	EventScheduler idleWraps;
	fire (idleWraps, loopedAt (16.3, 0, 16.0, 16.5), noneInLoop);
	EXPECT_EQ (fire (idleWraps, loopedAt (16.3, 4000000000u, 16.0, 16.5), noneInLoop), Fired());
}

TEST (EventScheduler, ResetForgetsLastBeat)
{
	const std::vector<Event> events { at (8.0), at (16.0) };
	EventScheduler scheduler;
	fire (scheduler, playingAt (7.9), events);
	scheduler.reset();
	EXPECT_EQ (fire (scheduler, playingAt (8.1), events), Fired()) << "a new track starts, it does not cross";
	EXPECT_EQ (fire (scheduler, playingAt (8.2), events), Fired());
	fire (scheduler, playingAt (15.9), events);
	EXPECT_EQ (fire (scheduler, playingAt (16.1), events), Fired ({ 1 }));
}

// Forward play whose beat goes down with no wrap and no seek counted (a
// grid edited under the playhead, a clock that stepped back): nothing fires,
// neither the events behind nor those in between.
TEST (EventScheduler, ForwardPlayWhoseBeatGoesDownFiresNothing)
{
	const std::vector<Event> events { at (6.0), at (8.0) };
	EventScheduler scheduler;
	fire (scheduler, playingAt (8.5), events);
	EXPECT_EQ (fire (scheduler, playingAt (5.5), events), Fired());
	EXPECT_EQ (fire (scheduler, playingAt (5.6), events), Fired()) << "and on from there, nothing crossed yet";
}

//==============================================================================
// Where playback starts. The rule: an event fires if the playhead plays from at
// or before its beat -- PLAY, a CUE preview, a jump that lands on it and plays
// on, a new track's first block. A jump that lands after it, and a scrub, never
// fire it. Between two playing blocks the window is exact, so nothing fires twice.

TEST (EventScheduler, PlayFromBeforeTheFirstBeatFiresTheEventOnIt)
{
	const std::vector<Event> events { at (0.0) };
	EventScheduler scheduler;
	fire (scheduler, standingAt (-0.6), events);   // 0.0 s, the grid's first beat 0.3 s in
	EXPECT_EQ (play (scheduler, playingAt (-0.5), events, aBlock), Fired());
	for (double beat = -0.4; beat < -0.05; beat += aBlock)
		EXPECT_EQ (play (scheduler, playingAt (beat), events, aBlock), Fired());
	EXPECT_EQ (play (scheduler, playingAt (0.05), events, aBlock), Fired ({ 0 }));
}

TEST (EventScheduler, PlayFromExactlyOnTheEventFiresItOnce)
{
	const std::vector<Event> events { at (0.0) };
	EventScheduler scheduler;
	fire (scheduler, standingAt (0.0), events);
	EXPECT_EQ (play (scheduler, playingAt (0.1), events, aBlock), Fired ({ 0 }));
	EXPECT_EQ (play (scheduler, playingAt (0.2), events, aBlock), Fired());
	fire (scheduler, standingAt (0.2), events);
	EXPECT_EQ (play (scheduler, playingAt (0.3), events, aBlock), Fired()) << "a pause and PLAY again";
}

// A cue point at the first beat in seconds, read back as a beat, can come out
// a hair above the event's beat: it is still the event's beat.
TEST (EventScheduler, PlayFromACueThatRoundsAboveTheEventFiresIt)
{
	const std::vector<Event> events { at (0.0), at (8.0) };
	EventScheduler first;
	fire (first, standingAt (justAbove (0.0)), events);
	EXPECT_EQ (play (first, playingAt (0.1), events, aBlock), Fired ({ 0 }));

	EventScheduler bar3;
	fire (bar3, standingAt (justAbove (8.0)), events);
	EXPECT_EQ (play (bar3, playingAt (8.1), events, aBlock), Fired ({ 1 }));
}

TEST (EventScheduler, PlayFromOneBeatAfterDoesNotFire)
{
	const std::vector<Event> events { at (0.0) };
	EventScheduler scheduler;
	fire (scheduler, standingAt (1.0), events);
	EXPECT_EQ (play (scheduler, playingAt (1.1), events, aBlock), Fired());
	EXPECT_EQ (play (scheduler, playingAt (1.2), events, aBlock), Fired());
}

// The start's tolerance is for the start only: a block that ends a hair past an
// event fired it, and the next block, the pause after it and the PLAY again do not.
TEST (EventScheduler, TheStartsToleranceNeverFiresTwice)
{
	const std::vector<Event> events { at (8.0) };
	EventScheduler scheduler;
	fire (scheduler, playingAt (7.9), events);
	EXPECT_EQ (play (scheduler, playingAt (8.0 + 5.0e-7), events, aBlock), Fired ({ 0 }));
	EXPECT_EQ (play (scheduler, playingAt (8.1), events, aBlock), Fired());

	EventScheduler paused;
	fire (paused, playingAt (7.9), events);
	EXPECT_EQ (play (paused, playingAt (8.0 + 5.0e-7), events, aBlock), Fired ({ 0 }));
	fire (paused, standingAt (8.0 + 5.0e-7), events);
	EXPECT_EQ (play (paused, playingAt (8.1), events, aBlock), Fired()) << "PLAY where it stopped";

	EventScheduler stoppedShort;
	fire (stoppedShort, playingAt (7.9), events);
	EXPECT_EQ (play (stoppedShort, playingAt (8.0 - 5.0e-7), events, aBlock), Fired());
	fire (stoppedShort, standingAt (8.0 - 5.0e-7), events);
	EXPECT_EQ (play (stoppedShort, playingAt (8.1), events, aBlock), Fired ({ 0 })) << "once, after the PLAY";
	EXPECT_EQ (play (stoppedShort, playingAt (8.2), events, aBlock), Fired());
}

// Auto DJ starts a deck with a jump and PLAY at once (setPosition, play): the
// block that sees the jump already played from the landing.
TEST (EventScheduler, AJumpAndPlayOntoTheEventFiresIt)
{
	const std::vector<Event> events { at (0.0) };
	EventScheduler scheduler;
	fire (scheduler, standingAt (100.0), events);
	EXPECT_EQ (play (scheduler, jumpedTo (0.1, 1), events, aBlock), Fired()) << "the jump's block is never a crossing";
	EXPECT_EQ (play (scheduler, jumpedTo (0.2, 1), events, aBlock), Fired ({ 0 })) << "it played from the landing";
	EXPECT_EQ (play (scheduler, jumpedTo (0.3, 1), events, aBlock), Fired());
}

TEST (EventScheduler, AJumpWhilePlayingThatLandsOnTheEventFiresIt)
{
	const std::vector<Event> events { at (8.0) };
	EventScheduler scheduler;
	fire (scheduler, playingAt (3.9), events);
	play (scheduler, playingAt (4.0), events, aBlock);
	EXPECT_EQ (play (scheduler, jumpedTo (8.1, 1), events, aBlock), Fired());
	EXPECT_EQ (play (scheduler, jumpedTo (8.2, 1), events, aBlock), Fired ({ 0 }));
}

TEST (EventScheduler, AJumpJustBeforeTheEventFiresItWhenCrossed)
{
	const std::vector<Event> events { at (8.0) };
	EventScheduler scheduler;
	fire (scheduler, playingAt (3.9), events);
	play (scheduler, playingAt (4.0), events, aBlock);
	EXPECT_EQ (play (scheduler, jumpedTo (8.05, 1), events, aBlock), Fired()) << "landed at 7.95, crossed in this block";
	EXPECT_EQ (play (scheduler, jumpedTo (8.15, 1), events, aBlock), Fired ({ 0 }));
}

TEST (EventScheduler, AJumpThatLandsAfterTheEventDoesNotFireIt)
{
	const std::vector<Event> events { at (8.0) };
	EventScheduler scheduler;
	fire (scheduler, playingAt (3.9), events);
	play (scheduler, playingAt (4.0), events, aBlock);
	EXPECT_EQ (play (scheduler, jumpedTo (8.2, 1), events, aBlock), Fired()) << "landed at 8.1, a block after it";
	EXPECT_EQ (play (scheduler, jumpedTo (8.3, 1), events, aBlock), Fired());
	EXPECT_EQ (play (scheduler, jumpedTo (8.4, 1), events, aBlock), Fired());
}

// The counter can move before the position does: the jump's block may still
// show the old place. The block after shows the landing, and only from there.
TEST (EventScheduler, AJumpReadBeforeItLandedIsNoCrossing)
{
	const std::vector<Event> events { at (6.0), at (8.0) };
	EventScheduler scheduler;
	fire (scheduler, playingAt (1.9), events);
	play (scheduler, playingAt (2.0), events, aBlock);
	EXPECT_EQ (play (scheduler, jumpedTo (2.1, 1), events, aBlock), Fired()) << "the counter moved, the position not yet";
	EXPECT_EQ (play (scheduler, jumpedTo (8.05, 1), events, aBlock), Fired ({ 1 })) << "landed on 7.95; never 6";
	EXPECT_EQ (play (scheduler, jumpedTo (8.15, 1), events, aBlock), Fired());
}

// The hand on the record counts a jump in every block: nothing it moves over fires.
TEST (EventScheduler, AScrubFiresNothing)
{
	const std::vector<Event> events { at (8.0), at (8.25) };
	EventScheduler scheduler;
	fire (scheduler, playingAt (7.7), events);
	std::uint32_t seeks = 0;
	for (double beat = 7.8; beat < 8.45; beat += aBlock)
		EXPECT_EQ (play (scheduler, jumpedTo (beat, ++seeks), events, aBlock), Fired()) << beat;
}

// A new track's first block, or the first after the audio starts: it played
// from one block back, and an event there fires.
TEST (EventScheduler, TheFirstBlockFiresWhatItPlayedOver)
{
	const std::vector<Event> events { at (0.0) };
	EventScheduler fresh;
	EXPECT_EQ (play (fresh, playingAt (0.1), events, aBlock), Fired ({ 0 }));
	EXPECT_EQ (play (fresh, playingAt (0.2), events, aBlock), Fired());

	EventScheduler afterReset;
	fire (afterReset, playingAt (50.0), events);
	afterReset.reset();
	EXPECT_EQ (play (afterReset, playingAt (0.1), events, aBlock), Fired ({ 0 }));

	EventScheduler oneBlockLate;
	EXPECT_EQ (play (oneBlockLate, playingAt (0.25), events, aBlock), Fired()) << "it played from 0.15";
}

// A loop's ends are beats from seconds too: loop-in a hair above the event's beat.
TEST (EventScheduler, AnEventOnALoopInThatRoundsAboveFiresEveryWrap)
{
	const std::vector<Event> events { at (16.0) };
	const double loopIn = justAbove (16.0);
	EventScheduler scheduler;
	fire (scheduler, loopedAt (19.9, 0, loopIn, 20.0), events);
	EXPECT_EQ (fire (scheduler, loopedAt (16.1, 1, loopIn, 20.0), events), Fired ({ 0 })) << "first wrap";
	fire (scheduler, loopedAt (19.9, 1, loopIn, 20.0), events);
	EXPECT_EQ (fire (scheduler, loopedAt (16.1, 2, loopIn, 20.0), events), Fired ({ 0 })) << "second wrap";
}

TEST (EventScheduler, AJumpOntoLoopInFiresItAtTheLoopsFirstEntry)
{
	const std::vector<Event> events { at (16.0) };
	EventScheduler scheduler;
	fire (scheduler, loopedAt (30.0, 0, 16.0, 20.0), events);
	auto reloop = loopedAt (16.1, 0, 16.0, 20.0);
	reloop.seeks = 1;
	EXPECT_EQ (play (scheduler, reloop, events, aBlock), Fired());
	reloop.beat = 16.2;
	EXPECT_EQ (play (scheduler, reloop, events, aBlock), Fired ({ 0 }));
}

//==============================================================================
// Held: the events are not live yet (a CONTINUE change before the new track's
// list is in). The playhead is followed, nothing fires, and what it crossed
// fires once at the first update.
TEST (EventScheduler, AHeldStartFiresAtTheFirstUpdate)
{
	const std::vector<Event> events { at (0.0), at (0.25) };
	EventScheduler scheduler;
	fire (scheduler, playingAt (300.0), events);
	scheduler.hold (jumpedTo (-0.5, 1), aBlock);   // the change: a jump to the new track's start
	for (double beat = -0.4; beat < 0.15; beat += aBlock)
		scheduler.hold (jumpedTo (beat, 1), aBlock);
	EXPECT_EQ (play (scheduler, jumpedTo (0.2, 1), events, aBlock), Fired ({ 0 })) << "late, once";
	EXPECT_EQ (play (scheduler, jumpedTo (0.3, 1), events, aBlock), Fired ({ 1 }));
	EXPECT_EQ (play (scheduler, jumpedTo (0.4, 1), events, aBlock), Fired());
}

TEST (EventScheduler, AHeldJumpStillLandsWhereItLands)
{
	const std::vector<Event> events { at (2.0), at (8.0) };
	EventScheduler scheduler;
	fire (scheduler, playingAt (0.9), events);
	scheduler.hold (playingAt (1.0), aBlock);
	scheduler.hold (jumpedTo (8.2, 1), aBlock);
	scheduler.hold (jumpedTo (8.3, 1), aBlock);
	EXPECT_EQ (play (scheduler, jumpedTo (8.4, 1), events, aBlock), Fired()) << "landed a block after 8; 2 was jumped over";
}

// After a jump the resampler reads a few frames ahead: the jump's block moves
// the read position a little more than it plays. A landing on the event still fires it.
TEST (EventScheduler, AJumpsReadAheadStillFiresTheEventAtTheLanding)
{
	const std::vector<Event> events { at (0.0) };
	EventScheduler scheduler;
	fire (scheduler, standingAt (100.0), events);
	EXPECT_EQ (play (scheduler, jumpedTo (aBlock + 0.004, 1), events, aBlock), Fired());
	EXPECT_EQ (play (scheduler, jumpedTo (2 * aBlock + 0.004, 1), events, aBlock), Fired ({ 0 }));
}
