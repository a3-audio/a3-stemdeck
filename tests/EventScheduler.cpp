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
	                       const std::vector<Event>& events, int limit = capacity)
	{
		int out[capacity + 1];
		out[limit] = -99;
		const int found = scheduler.update (clock, events.data(), (int) events.size(), out, limit);
		EXPECT_EQ (out[limit], -99) << "wrote beyond capacity";
		return std::vector<int> (out, out + found);
	}

	using Fired = std::vector<int>;
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
	EXPECT_EQ (fire (scheduler, jumped, events), Fired()) << "a jump over the event";
	jumped.beat = 8.6;
	EXPECT_EQ (fire (scheduler, jumped, events), Fired());

	auto back = playingAt (7.95);
	back.seeks = 2;
	EXPECT_EQ (fire (scheduler, back, events), Fired());
	back.beat = 8.05;
	EXPECT_EQ (fire (scheduler, back, events), Fired ({ 0 })) << "from the new place it crosses again";

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
