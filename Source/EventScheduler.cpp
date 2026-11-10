// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "EventScheduler.h"

#include <algorithm>
#include <cmath>

namespace stemevents
{
	namespace
	{
		bool forward (const motionlite::Clock& clock)
		{
			return clock.playing && clock.rate > 0.0;
		}

		// A block moves the read position by what it plays give or take this much of
		// it: the resampler reads a few frames ahead after a jump (flushBuffers).
		constexpr double blockSlack = 0.5;

		// Two blocks are one stretch of play if the second moved about as far as it plays.
		bool playedOn (double moved, double played)
		{
			return std::abs (moved - played) <= blockSlack * played + EventScheduler::startTolerance;
		}

		// The earliest beat a block that ended on `beat` can have played from.
		double reachedBack (double beat, double played)
		{
			return beat - (1.0 + blockSlack) * played;
		}
	}

	int EventScheduler::update (const motionlite::Clock& clock, const Event* events, int count, int* out, int capacity,
	                            double blockBeats)
	{
		const auto due = advance (clock, blockBeats, true);
		if (! due || capacity <= 0)
			return 0;
		if (due->wraps == 0)
			return collect (due->window, events, count, out, 0, capacity);
		return collectLoop (due->window, *clock.loopBeats, due->wraps, events, count, out, capacity);
	}

	void EventScheduler::hold (const motionlite::Clock& clock, double blockBeats)
	{
		advance (clock, blockBeats, false);
	}

	void EventScheduler::reset()
	{
		from.reset();
		landing.reset();
	}

	// One block on: the window it fires, if live and playing forward. The counters
	// of the very first block are the base; after that a moved seek counter is a jump.
	std::optional<EventScheduler::Due> EventScheduler::advance (const motionlite::Clock& clock, double blockBeats, bool live)
	{
		const bool jumped = lastSeeks && clock.seeks != *lastSeeks;
		const std::uint32_t wraps = lastWraps ? clock.wraps - *lastWraps : 0u;   // unsigned: survives the counter's overflow
		lastSeeks = clock.seeks;
		lastWraps = clock.wraps;

		if (! clock.beat)
		{
			reset();
			return std::nullopt;
		}

		const double beat = *clock.beat;
		const double played = forward (clock) ? std::max (0.0, blockBeats) : 0.0;
		// The counter moves before the position does: a jump's block may still read the
		// old place, so it fires nothing and the block after reads the landing. A jump
		// wins over a wrap; the hand on the record jumps every block and fires nothing.
		if (jumped)
		{
			landing = Landing { reachedBack (beat, played), beat };
			return std::nullopt;
		}
		if (landing)
			from = landedAt (*std::exchange (landing, std::nullopt), beat, played) - startTolerance;
		else if (! from)
			from = reachedBack (beat, played) - startTolerance;   // the first block played from one block back

		if (! forward (clock))
		{
			stand (beat, live);
			return std::nullopt;
		}
		if (wraps > 0 && (! clock.loopBeats || clock.loopBeats->second <= clock.loopBeats->first))
		{
			from = beat;   // a wrap without the loop's ends cannot be followed: a jump
			return std::nullopt;
		}
		if (! live)
		{
			if (wraps > 0)
				from = clock.loopBeats->first - startTolerance;   // held through a wrap: the loop's head is kept
			return std::nullopt;
		}

		const Due due { { *from, beat }, wraps };
		from = beat;
		return due;
	}

	// Standing or playing backwards: forward play starts from here, this beat
	// included -- unless the last window ended here and has counted it. Held, the
	// kept start stays, unless the playhead went back behind it.
	void EventScheduler::stand (double beat, bool live)
	{
		if (beat == *from)
			return;
		if (live || beat < *from)
			from = beat - startTolerance;
	}

	// The block after a jump reads the landing. If it played on from the jump's
	// block, that block played from the landing (the jump came before it) or read it
	// (the jump came during or after it): the earlier counts, so a landing on an
	// event fires it, at the cost of up to one and a half blocks' reach before the
	// landing (half a block for the resampler's read-ahead).
	// Otherwise the jump's block read the old place, and only this block played
	// from the landing.
	double EventScheduler::landedAt (Landing jump, double beat, double played)
	{
		const double moved = beat - jump.beat;
		if (playedOn (moved, played))
			return jump.earliest;
		if (moved == 0.0)
			return beat;   // playing, but standing: the stems still decode
		return reachedBack (beat, played);
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
	// Loop-in is a beat from seconds too: a pass reaches startTolerance below it.
	int EventScheduler::collectLoop (Window window, std::pair<double, double> loop, std::uint32_t wraps,
	                                 const Event* events, int count, int* out, int capacity)
	{
		const auto [loopIn, loopOut] = loop;
		const double passFrom = loopIn - startTolerance;
		int found = collect ({ window.from, loopOut }, events, count, out, 0, capacity);

		for (std::uint32_t pass = 1; pass < wraps && found < capacity; ++pass)
		{
			const int before = found;
			found = collect ({ passFrom, loopOut }, events, count, out, found, capacity);
			// A pass that finds nothing means every pass and the head find nothing: stop,
			// so a huge wrap count costs one pass.
			if (found == before)
				return found;
		}

		return collect ({ passFrom, window.to }, events, count, out, found, capacity);
	}
}
