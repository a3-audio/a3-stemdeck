// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "EventMarkerLayout.h"

#include <algorithm>
#include <cmath>

namespace eventmarkers
{
	namespace
	{
		// Share of the lane the marker fills, and its width against its height.
		constexpr float heightShare = 0.9f, widthToHeight = 1.3f;
		// A fingertip: this share of the width, but never less than this many lanes.
		constexpr float fingertipShare = 0.025f, fingertipLanes = 0.75f;

		float laneHeightOf (float height) { return height / (float) laneCount; }
		float laneTopOf (const stemevents::Event& event, float height)
		{
			return laneHeightOf (height) * (float) std::clamp (event.stem, 0, laneCount - 1);
		}
	}

	double eventSeconds (const stemevents::Event& event, double firstBeat, double bpm)
	{
		return firstBeat + event.beat * 60.0 / bpm;
	}

	Rect markerRect (const stemevents::Event&, float laneTop, float laneHeight, float width, double seconds,
					 double trackSeconds)
	{
		const auto h = laneHeight * heightShare;
		const auto w = std::min (h * widthToHeight, width);
		const auto centre = trackSeconds > 0.0 ? (float) (seconds / trackSeconds) * width : 0.0f;
		const auto x = std::clamp (centre - w * 0.5f, 0.0f, width - w);
		return { x, laneTop + (laneHeight - h) * 0.5f, w, h };
	}

	float fingertipRadius (float width, float height)
	{
		return std::max (width * fingertipShare, laneHeightOf (height) * fingertipLanes);
	}

	int hitTest (const std::vector<stemevents::Event>& events, const std::vector<double>& seconds, float x, float y,
				 float width, float height, double trackSeconds)
	{
		const auto reach = fingertipRadius (width, height);
		auto best = -1;
		auto bestDistance = reach;

		for (size_t i = 0; i < events.size() && i < seconds.size(); ++i)
		{
			const auto rect = markerRect (events[i], laneTopOf (events[i], height), laneHeightOf (height), width, seconds[i],
										  trackSeconds);
			const auto distance = std::hypot (x - (rect.x + rect.w * 0.5f), y - (rect.y + rect.h * 0.5f));
			if (distance > bestDistance)
				continue;

			best = (int) i;
			bestDistance = distance;
		}

		return best;
	}

	const char* glyphFor (const stemevents::Event& event)
	{
		using motionlite::Shape;
		switch (event.shape)
		{
			case Shape::orbit: return event.reversed ? "\xE2\x97\x84" : "O";
			case Shape::ping:  return "P";
			case Shape::rise:  return "R";
			case Shape::scat:  return "S";
		}
		return "?";
	}
}
