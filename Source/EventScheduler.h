// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "MotionLite.h"
#include "StemEvents.h"

#include <cstdint>
#include <optional>
#include <utility>

// Decides which of a deck's events fire in one audio block: those the playhead
// plays over going forward. Where play starts counts as played over: PLAY on an
// event's beat, a jump that lands on it and plays on, a new track's first block.
// A jump that lands past it, a scrub and backward play never fire it. A start
// reaches startTolerance below its beat (a cue point read back as a beat can come
// out a hair above the event's); two playing blocks share their boundary exactly,
// so nothing fires twice. Runs on the audio thread: no allocation, no locks, no
// exceptions.
namespace stemevents
{
	class EventScheduler
	{
	public:
		// The slot tolerance (StemEvents.cpp): an event this close below a start is at it.
		static constexpr double startTolerance = 1e-6;

		// Writes the indices into `events` that fire in this block to `out`, in firing
		// order, and returns how many (at most capacity). `events` is sorted by beat.
		// `blockBeats`: how far the block plays at the clock's tempo and rate; a jump's
		// landing and a first block are reckoned back from the beat by it.
		int update (const motionlite::Clock&, const Event* events, int count, int* out, int capacity,
		            double blockBeats = 0.0);
		// Follows the playhead like update() but fires nothing: what it plays over waits
		// for the next update(), and fires there once. For events not live yet.
		void hold (const motionlite::Clock&, double blockBeats);
		// A new track: where play stood is forgotten; the next block starts afresh.
		void reset();

	private:
		struct Window { double from; double to; };
		struct Due { Window window; std::uint32_t wraps; };
		// A jump's block: the earliest beat it may have played from, and the beat it read.
		struct Landing { double earliest; double beat; };

		std::optional<Due> advance (const motionlite::Clock&, double blockBeats, bool live);
		void stand (double beat, bool live);
		static double landedAt (Landing, double beat, double played);
		static int collect (Window, const Event* events, int count, int* out, int found, int capacity);
		static int collectLoop (Window, std::pair<double, double> loop, std::uint32_t wraps,
		                        const Event* events, int count, int* out, int capacity);

		std::optional<double> from;      // where the next window starts; none: the next block sets it
		std::optional<Landing> landing;  // set by a jump's block, used by the block after
		std::optional<std::uint32_t> lastSeeks, lastWraps;   // none: the first block's are the base
	};
}
