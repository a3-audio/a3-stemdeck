// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "EventScheduler.h"

namespace stemevents
{
	namespace
	{
		bool forward (const motionlite::Clock& clock)
		{
			return clock.playing && clock.rate > 0.0;
		}
	}

	int EventScheduler::update (const motionlite::Clock& clock, const Event* events, int count, int* out, int capacity)
	{
		const auto previousBeat = lastBeat;
		const bool seeked = clock.seeks != lastSeeks;
		const std::uint32_t wraps = clock.wraps - lastWraps;   // unsigned: survives the counter's overflow
		lastBeat = clock.beat;
		lastSeeks = clock.seeks;
		lastWraps = clock.wraps;

		if (! clock.beat || ! previousBeat || seeked || ! forward (clock) || capacity <= 0)
			return 0;

		const double from = *previousBeat;
		const double to = *clock.beat;
		if (wraps == 0)
			return collect ({ from, to }, events, count, out, 0, capacity);

		// A wrap without the loop's ends cannot be followed; treat it as a jump.
		if (! clock.loopBeats || clock.loopBeats->second <= clock.loopBeats->first)
			return 0;

		return collectLoop (from, to, *clock.loopBeats, wraps, events, count, out, capacity);
	}

	void EventScheduler::reset()
	{
		lastBeat.reset();
	}

	int EventScheduler::collect (Window window, const Event* events, int count, int* out, int found, int capacity)
	{
		for (int i = 0; i < count && found < capacity; ++i)
		{
			const auto& event = events[i];
			if (event.active && event.beat >= window.from && event.beat < window.to)
				out[found++] = i;
		}
		return found;
	}

	// The tail up to loop-out, wraps - 1 whole passes, then the head from loop-in. An
	// event at loop-in is in every pass and every head; one at loop-out is in none.
	int EventScheduler::collectLoop (double from, double to, std::pair<double, double> loop, std::uint32_t wraps,
	                                 const Event* events, int count, int* out, int capacity)
	{
		const auto [loopIn, loopOut] = loop;
		int found = collect ({ from, loopOut }, events, count, out, 0, capacity);

		for (std::uint32_t pass = 1; pass < wraps && found < capacity; ++pass)
		{
			const int before = found;
			found = collect ({ loopIn, loopOut }, events, count, out, found, capacity);
			// A pass that finds nothing means every pass and the head find nothing: stop,
			// so a huge wrap count costs one pass.
			if (found == before)
				return found;
		}

		return collect ({ loopIn, to }, events, count, out, found, capacity);
	}
}
