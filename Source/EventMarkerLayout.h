// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "StemEvents.h"

#include <cstddef>
#include <optional>
#include <vector>

// Where an event's marker sits on the rolling waveform, and which marker a
// finger hits. Free of JUCE: sizes are fractions of the lane height and the width.
namespace eventmarkers
{
	struct Rect { float x, y, w, h; };

	constexpr int laneCount = 4;

	// Share of the lane a marker fills.
	constexpr float heightShare = 0.9f;
	// More events than this and the markers go without letters: they overlap
	// too much to read, and a text layout each is what the waveform's timer
	// tick cannot afford.
	constexpr size_t maxLetteredMarkers = 64;

	// The track time of an event: its beat counts from the grid's first beat.
	double eventSeconds (const stemevents::Event&, double firstBeat, double bpm);

	// A fingertip's reach around a marker's centre.
	float fingertipRadius (float width, float height);

	// A view's time axis: x = (seconds - startSeconds) / secondsPerPixel over
	// [0, width]; height holds the four lanes. The rolling waveform's columns
	// (waveformColumns::Grid) in plain numbers.
	struct Window
	{
		double startSeconds;
		double secondsPerPixel;
		float width, height;
	};

	float xOf (const Window&, double seconds);

	// The marker in its stem's lane, centred on the event's time; false when
	// it lies wholly outside the window -- such a marker is neither drawn nor
	// hit. One at an edge is cut by it, not pushed inside.
	std::optional<Rect> markerRect (const stemevents::Event&, double seconds, const Window&);

	// The marker nearest to (x, y) within a fingertip; -1 if none. Only
	// markers in the window count; seconds[i] is events[i]'s time on the track.
	int hitTest (const std::vector<stemevents::Event>& events, const std::vector<double>& seconds, float x, float y,
				 const Window&);

	// A press that began on a marker is a tap while it stays within a
	// fingertip of where it began; further, it is a drag (the view scrubs).
	bool staysATap (float dx, float dy, const Window&);

	// The glyphs there are: O, P, R, S, and for a reversed orbit the arrow.
	constexpr int glyphCount = 5;
	int glyphIndex (const stemevents::Event&);
	// The glyph (UTF-8) of glyphIndex.
	const char* glyphText (int index);
	const char* glyphFor (const stemevents::Event&);
}
