// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "MotionLite.h"
#include "StemEvents.h"

#include <cstdint>
#include <limits>
#include <optional>

// What a live press becomes in a recording: an event where the press would
// have started its motion. Free of JUCE.
namespace stemevents
{
	// The beat of a deck without a grid: there is nowhere to put an event.
	constexpr double noBeat = std::numeric_limits<double>::quiet_NaN();

	// The event a press would make: SHIFT: the beat now (snapped when quantize is on);
	// otherwise the next bar's first beat, as the live press waits for it (a press
	// exactly on a bar line waits for the next one). None without a grid (beatNow
	// not finite). The length is kept as the one-shot would play it.
	std::optional<Event> eventForPress (int stem, motionlite::Shape, bool shift, double beatNow,
										double lengthBars, bool reversed, std::uint64_t seed, bool quantize);
}
