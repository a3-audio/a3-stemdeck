// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "StemEvents.h"

#include <vector>

// Where an event's marker sits on the overview, and which marker a finger
// hits. Free of JUCE: sizes are fractions of the lane height and the width.
namespace eventmarkers
{
	struct Rect { float x, y, w, h; };

	constexpr int laneCount = 4;

	// The track time of an event: its beat counts from the grid's first beat.
	double eventSeconds (const stemevents::Event&, double firstBeat, double bpm);

	// The marker in its stem's lane (laneTop, laneHeight), centred on the
	// event's time, kept inside [0, width]. trackSeconds maps time to x.
	Rect markerRect (const stemevents::Event&, float laneTop, float laneHeight, float width, double seconds,
					 double trackSeconds);

	// A fingertip's reach around a marker's centre.
	float fingertipRadius (float width, float height);

	// The marker nearest to (x, y) within a fingertip; -1 if none.
	// seconds[i] is events[i]'s time on the track.
	int hitTest (const std::vector<stemevents::Event>& events, const std::vector<double>& seconds, float x, float y,
				 float width, float height, double trackSeconds);

	// The letter (UTF-8) that tells the shape: O, P, R, S; a reversed orbit
	// shows the arrow instead.
	const char* glyphFor (const stemevents::Event&);
}
