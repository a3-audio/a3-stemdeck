// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>

#include "EventMarkerLayout.h"

#include <algorithm>
#include <string>

namespace
{
	constexpr float width = 800.0f, height = 100.0f;
	constexpr float lane = height / 4.0f;
	constexpr double track = 200.0;

	stemevents::Event eventOn (int stem, double beat = 0.0)
	{
		stemevents::Event event;
		event.stem = stem;
		event.beat = beat;
		return event;
	}
}

TEST (EventMarkerLayout, MarkerSitsInItsStemsLane)
{
	for (int stem = 0; stem < 4; ++stem)
	{
		const auto top = lane * (float) stem;
		const auto rect = eventmarkers::markerRect (eventOn (stem), top, lane, width, 50.0, track);
		EXPECT_GE (rect.y, top);
		EXPECT_LE (rect.y + rect.h, top + lane);
		EXPECT_NEAR (rect.x + rect.w * 0.5f, 200.0f, 0.01f);   // 50 s of 200 s
	}
}

TEST (EventMarkerLayout, MarkerStaysInsideTheComponentAtTheEnds)
{
	const auto first = eventmarkers::markerRect (eventOn (0), 0.0f, lane, width, 0.0, track);
	const auto last = eventmarkers::markerRect (eventOn (0), 0.0f, lane, width, track, track);
	EXPECT_GE (first.x, 0.0f);
	EXPECT_LE (last.x + last.w, width);
}

TEST (EventMarkerLayout, EventTimeComesFromTheGrid)
{
	// 120 bpm: a beat is 0.5 s; the grid's first beat is at 1.25 s.
	EXPECT_DOUBLE_EQ (eventmarkers::eventSeconds (eventOn (0, 8.0), 1.25, 120.0), 5.25);
}

TEST (EventMarkerLayout, HitTestPicksTheNearest)
{
	const std::vector<stemevents::Event> events { eventOn (1), eventOn (1) };
	const std::vector<double> seconds { 50.0, 54.0 };   // 200 px and 216 px
	EXPECT_EQ (eventmarkers::hitTest (events, seconds, 203.0f, lane * 1.5f, width, height, track), 0);
	EXPECT_EQ (eventmarkers::hitTest (events, seconds, 213.0f, lane * 1.5f, width, height, track), 1);
}

TEST (EventMarkerLayout, HitTestMissesAnEmptyArea)
{
	const std::vector<stemevents::Event> events { eventOn (1) };
	const std::vector<double> seconds { 50.0 };
	EXPECT_EQ (eventmarkers::hitTest (events, seconds, 600.0f, lane * 1.5f, width, height, track), -1);
	EXPECT_EQ (eventmarkers::hitTest ({}, {}, 200.0f, lane * 1.5f, width, height, track), -1);
}

TEST (EventMarkerLayout, AFingertipIsWiderThanTheMarker)
{
	const std::vector<stemevents::Event> events { eventOn (1) };
	const std::vector<double> seconds { 50.0 };
	const auto reach = eventmarkers::fingertipRadius (width, height);
	const auto rect = eventmarkers::markerRect (events[0], lane, lane, width, 50.0, track);
	EXPECT_GT (reach * 2.0f, rect.w);   // the target is wider than the mark
	EXPECT_EQ (eventmarkers::hitTest (events, seconds, 200.0f + reach * 0.9f, lane * 1.5f, width, height, track), 0);
	EXPECT_EQ (eventmarkers::hitTest (events, seconds, 200.0f + reach * 1.1f, lane * 1.5f, width, height, track), -1);
}

TEST (EventMarkerLayout, EveryShapeHasItsOwnGlyph)
{
	std::vector<std::string> glyphs;
	for (const auto shape : { motionlite::Shape::orbit, motionlite::Shape::ping, motionlite::Shape::rise, motionlite::Shape::scat })
	{
		auto event = eventOn (0);
		event.shape = shape;
		glyphs.push_back (eventmarkers::glyphFor (event));
	}
	auto reversed = eventOn (0);
	reversed.reversed = true;
	glyphs.push_back (eventmarkers::glyphFor (reversed));
	std::sort (glyphs.begin(), glyphs.end());
	EXPECT_EQ (std::unique (glyphs.begin(), glyphs.end()), glyphs.end());
}
