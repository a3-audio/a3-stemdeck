// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>

#include "EventMarkerLayout.h"

#include <algorithm>
#include <string>
#include <utility>

namespace
{
	constexpr float width = 800.0f, height = 100.0f;
	constexpr float lane = height / 4.0f;

	stemevents::Event eventOn (int stem, double beat = 0.0)
	{
		stemevents::Event event;
		event.stem = stem;
		event.beat = beat;
		return event;
	}
}

TEST (EventMarkerLayout, EventTimeComesFromTheGrid)
{
	// 120 bpm: a beat is 0.5 s; the grid's first beat is at 1.25 s.
	EXPECT_DOUBLE_EQ (eventmarkers::eventSeconds (eventOn (0, 8.0), 1.25, 120.0), 5.25);
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

// A marker's letter is its pad's: PING shows as CROSS, so its marker is C.
TEST (EventMarkerLayout, AGlyphIsThePadsFirstLetter)
{
	const std::pair<motionlite::Shape, std::string> letters[] = {
		{ motionlite::Shape::orbit, "O" }, { motionlite::Shape::ping, "C" }, { motionlite::Shape::rise, "R" }, { motionlite::Shape::scat, "S" } };
	for (const auto& [shape, letter] : letters)
	{
		auto event = eventOn (0);
		event.shape = shape;
		EXPECT_EQ (eventmarkers::glyphFor (event), letter);
	}
}

//==============================================================================
// The rolling waveform: a window of the track that scrolls past a fixed
// centre playhead. Here 20 s across 800 px, the playhead at 50 s.
namespace
{
	constexpr eventmarkers::Window window { 40.0, 20.0 / 800.0, width, height };

	float centreX (const eventmarkers::Rect& r) { return r.x + r.w * 0.5f; }
	float centreY (const eventmarkers::Rect& r) { return r.y + r.h * 0.5f; }
}

TEST (EventMarkerLayout, InTheWindowAMarkerAtThePlayheadSitsAtTheCentre)
{
	const auto rect = eventmarkers::markerRect (eventOn (2), 50.0, window);
	ASSERT_TRUE (rect.has_value());
	EXPECT_NEAR (centreX (*rect), width * 0.5f, 0.01f);
	EXPECT_NEAR (eventmarkers::xOf (window, 50.0), width * 0.5f, 0.01f);
}

TEST (EventMarkerLayout, InTheWindowAMarkerSitsInItsStemsLaneAtItsTime)
{
	for (int stem = 0; stem < 4; ++stem)
	{
		const auto top = lane * (float) stem;
		const auto rect = eventmarkers::markerRect (eventOn (stem), 45.0, window);   // 5 s before the playhead: 200 px
		ASSERT_TRUE (rect.has_value()) << stem;
		EXPECT_GE (rect->y, top);
		EXPECT_LE (rect->y + rect->h, top + lane);
		EXPECT_NEAR (centreX (*rect), 200.0f, 0.01f);
	}
}

// Its size follows the lane, not the zoom.
TEST (EventMarkerLayout, InTheWindowTheMarkersSizeFollowsTheLane)
{
	const auto wide = eventmarkers::markerRect (eventOn (0), 50.0, window);
	const eventmarkers::Window zoomedIn { 48.0, 4.0 / 800.0, width, height };
	const auto close = eventmarkers::markerRect (eventOn (0), 50.0, zoomedIn);
	ASSERT_TRUE (wide && close);
	EXPECT_FLOAT_EQ (wide->w, close->w);
	EXPECT_FLOAT_EQ (wide->h, close->h);
	EXPECT_LT (wide->h, lane);
	EXPECT_GT (wide->h, lane * 0.5f);
}

TEST (EventMarkerLayout, OutsideTheWindowAMarkerIsNotDrawn)
{
	EXPECT_FALSE (eventmarkers::markerRect (eventOn (1), 30.0, window).has_value()) << "10 s before the window";
	EXPECT_FALSE (eventmarkers::markerRect (eventOn (1), 70.0, window).has_value()) << "10 s after it";
}

// At the edge it is cut, not pushed inside: its centre stays on its time.
TEST (EventMarkerLayout, AtTheWindowsEdgeAMarkerKeepsItsTime)
{
	const auto rect = eventmarkers::markerRect (eventOn (1), 40.0, window);
	ASSERT_TRUE (rect.has_value());
	EXPECT_NEAR (centreX (*rect), 0.0f, 0.01f);
	EXPECT_LT (rect->x, 0.0f);
}

TEST (EventMarkerLayout, InTheWindowHitTestPicksTheNearest)
{
	const std::vector<stemevents::Event> events { eventOn (1), eventOn (1) };
	const std::vector<double> seconds { 50.0, 50.4 };   // 400 px and 416 px
	EXPECT_EQ (eventmarkers::hitTest (events, seconds, 403.0f, lane * 1.5f, window), 0);
	EXPECT_EQ (eventmarkers::hitTest (events, seconds, 413.0f, lane * 1.5f, window), 1);
	EXPECT_EQ (eventmarkers::hitTest (events, seconds, 403.0f, lane * 3.5f, window), -1) << "another stem's lane";
	EXPECT_EQ (eventmarkers::hitTest (events, seconds, 700.0f, lane * 1.5f, window), -1);
}

// A marker outside the window is not there to hit, even where the overview
// would once have pushed it to the edge.
TEST (EventMarkerLayout, OutsideTheWindowAMarkerIsNotHit)
{
	const std::vector<stemevents::Event> events { eventOn (1), eventOn (1) };
	const std::vector<double> seconds { 30.0, 70.0 };
	EXPECT_EQ (eventmarkers::hitTest (events, seconds, 1.0f, lane * 1.5f, window), -1);
	EXPECT_EQ (eventmarkers::hitTest (events, seconds, width - 1.0f, lane * 1.5f, window), -1);
	EXPECT_EQ (eventmarkers::hitTest ({}, {}, 400.0f, lane * 1.5f, window), -1) << "no events";
}

TEST (EventMarkerLayout, InTheWindowAFingertipIsWiderThanTheMarker)
{
	const std::vector<stemevents::Event> events { eventOn (1) };
	const std::vector<double> seconds { 50.0 };
	const auto reach = eventmarkers::fingertipRadius (width, height);
	const auto rect = eventmarkers::markerRect (events[0], 50.0, window);
	ASSERT_TRUE (rect.has_value());
	EXPECT_GT (reach * 2.0f, rect->w);
	EXPECT_EQ (eventmarkers::hitTest (events, seconds, 400.0f + reach * 0.9f, centreY (*rect), window), 0);
	EXPECT_EQ (eventmarkers::hitTest (events, seconds, 400.0f + reach * 1.1f, centreY (*rect), window), -1);
}

// On a marker: still within a fingertip it is a tap (select), beyond it a drag (scrub).
TEST (EventMarkerLayout, APressOnAMarkerIsATapUntilItLeavesAFingertip)
{
	const auto reach = eventmarkers::fingertipRadius (width, height);
	EXPECT_TRUE (eventmarkers::staysATap (0.0f, 0.0f, window));
	EXPECT_TRUE (eventmarkers::staysATap (reach * 0.5f, -reach * 0.5f, window)) << "a finger's wobble";
	EXPECT_FALSE (eventmarkers::staysATap (reach * 1.2f, 0.0f, window)) << "a drag";
	EXPECT_FALSE (eventmarkers::staysATap (-reach * 1.2f, 0.0f, window)) << "either way";
	EXPECT_FALSE (eventmarkers::staysATap (0.0f, reach * 1.2f, window)) << "up or down too";
}
