// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "MotionLite.h"
#include "StemEvents.h"

#include <cstdint>
#include <optional>
#include <utility>

// Decides which of a deck's events fire in one audio block: those the playhead
// crosses while playing forward. Runs on the audio thread: no allocation, no
// locks, no exceptions.
namespace stemevents
{
	class EventScheduler
	{
	public:
		// Writes the indices into `events` that fire in this block to `out`, in firing
		// order, and returns how many (at most capacity). `events` is sorted by beat.
		int update (const motionlite::Clock&, const Event* events, int count, int* out, int capacity);
		// A new track: the next block only sets the starting point.
		void reset();

	private:
		struct Window { double from; double to; };

		static int collect (Window, const Event* events, int count, int* out, int found, int capacity);
		static int collectLoop (double from, double to, std::pair<double, double> loop, std::uint32_t wraps,
		                        const Event* events, int count, int* out, int capacity);

		std::optional<double> lastBeat;
		std::uint32_t lastSeeks = 0;
		std::uint32_t lastWraps = 0;
	};
}
