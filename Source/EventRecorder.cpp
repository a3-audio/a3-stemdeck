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

		double pressBeat (bool shift, double beatNow, bool quantize)
		{
			if (! shift)
				return nextBarStart (beatNow);
			return quantize ? std::round (beatNow) : beatNow;
		}
	}

	std::optional<Event> eventForPress (int stem, motionlite::Shape shape, bool shift, double beatNow,
										double lengthBars, bool reversed, std::uint64_t seed, bool quantize)
	{
		if (! std::isfinite (beatNow))
			return std::nullopt;

		Event e;
		e.stem = stem;
		e.beat = pressBeat (shift, beatNow, quantize);
		e.shape = shape;
		e.lengthBars = motionlite::validLength (lengthBars);
		e.reversed = reversed;
		e.seed = seed;
		e.active = true;
		e.origin = Origin::hand;
		return e;
	}
}
