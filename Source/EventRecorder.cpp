// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "EventRecorder.h"

#include <cmath>

namespace stemevents
{
	namespace
	{
		// As MotionLite's live press: armed until floor (beat / 4) goes up.
		double nextBarStart (double beat)
		{
			return (std::floor (beat / motionlite::beatsPerBar) + 1.0) * motionlite::beatsPerBar;
		}

		double snappedIf (bool quantize, double beat)
		{
			return quantize ? std::round (beat) : beat;
		}

		double pressBeat (bool shift, double beatNow, bool quantize)
		{
			if (! shift)
				return nextBarStart (beatNow);
			return snappedIf (quantize, beatNow);
		}

		Event handEvent (int stem, motionlite::Shape shape, double beat, double lengthBars, bool reversed,
						 std::uint64_t seed, double speed)
		{
			Event e;
			e.stem = stem;
			e.beat = beat;
			e.shape = shape;
			e.lengthBars = motionlite::validLength (lengthBars);
			e.reversed = reversed;
			e.speed = motionlite::validSpeed (speed);
			e.seed = seed;
			e.active = true;
			e.origin = Origin::hand;
			return e;
		}
	}

	std::optional<Event> eventForPress (int stem, motionlite::Shape shape, bool shift, double beatNow,
										double lengthBars, bool reversed, std::uint64_t seed, bool quantize, double speed)
	{
		if (! std::isfinite (beatNow))
			return std::nullopt;
		return handEvent (stem, shape, pressBeat (shift, beatNow, quantize), lengthBars, reversed, seed, speed);
	}

	std::optional<Event> eventAtPlayhead (int stem, motionlite::Shape shape, double beatPlayhead, double lengthBars,
										  bool reversed, std::uint64_t seed, bool quantize, double speed)
	{
		if (! std::isfinite (beatPlayhead))
			return std::nullopt;
		return handEvent (stem, shape, snappedIf (quantize, beatPlayhead), lengthBars, reversed, seed, speed);
	}
}
