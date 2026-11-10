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
	// not finite). The length and the speed (the stem's tempo step) are kept as
	// the one-shot would play them.
	std::optional<Event> eventForPress (int stem, motionlite::Shape, bool shift, double beatNow,
										double lengthBars, bool reversed, std::uint64_t seed, bool quantize,
										double speed = motionlite::defaultSpeed);

	// The event a press makes while the deck stands (3D REC in pause): placed at
	// the playhead, nothing plays. On the nearest beat when quantize is on;
	// off, at the playhead's exact beat (the file keeps a fractional beat, as
	// for a SHIFT press without quantize). SHIFT means nothing here. None
	// without a grid (beatPlayhead not finite).
	std::optional<Event> eventAtPlayhead (int stem, motionlite::Shape, double beatPlayhead, double lengthBars,
										  bool reversed, std::uint64_t seed, bool quantize,
										  double speed = motionlite::defaultSpeed);
}
