// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "EventMarkerLayout.h"

#include <algorithm>
#include <cmath>

namespace eventmarkers
{
	namespace
	{
		// A marker's width against its height.
		constexpr float widthToHeight = 1.3f;
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

	float fingertipRadius (float width, float height)
	{
		return std::max (width * fingertipShare, laneHeightOf (height) * fingertipLanes);
	}

	float xOf (const Window& window, double seconds)
	{
		return (float) ((seconds - window.startSeconds) / window.secondsPerPixel);
	}

	std::optional<Rect> markerRect (const stemevents::Event& event, double seconds, const Window& window)
	{
		const auto laneHeight = laneHeightOf (window.height);
		const auto h = laneHeight * heightShare;
		const auto w = h * widthToHeight;
		const auto x = xOf (window, seconds) - w * 0.5f;
		if (x + w <= 0.0f || x >= window.width)
			return std::nullopt;
		return Rect { x, laneTopOf (event, window.height) + (laneHeight - h) * 0.5f, w, h };
	}

	int hitTest (const std::vector<stemevents::Event>& events, const std::vector<double>& seconds, float x, float y,
				 const Window& window)
	{
		auto best = -1;
		auto bestDistance = fingertipRadius (window.width, window.height);

		for (size_t i = 0; i < events.size() && i < seconds.size(); ++i)
		{
			const auto rect = markerRect (events[i], seconds[i], window);
			if (! rect)
				continue;
			const auto distance = std::hypot (x - (rect->x + rect->w * 0.5f), y - (rect->y + rect->h * 0.5f));
			if (distance > bestDistance)
				continue;

			best = (int) i;
			bestDistance = distance;
		}

		return best;
	}

	bool staysATap (float dx, float dy, const Window& window)
	{
		return std::hypot (dx, dy) <= fingertipRadius (window.width, window.height);
	}

	int glyphIndex (const stemevents::Event& event)
	{
		using motionlite::Shape;
		switch (event.shape)
		{
			case Shape::orbit: return event.reversed ? 4 : 0;
			case Shape::ping:  return 1;
			case Shape::rise:  return 2;
			case Shape::scat:  return 3;
		}
		return 0;
	}

	const char* glyphText (int index)
	{
		static constexpr const char* glyphs[glyphCount] = { "O", "C", "R", "S", "\xE2\x97\x84" };   // C: CROSS, Shape::ping
		return glyphs[std::clamp (index, 0, glyphCount - 1)];
	}

	const char* glyphFor (const stemevents::Event& event)
	{
		return glyphText (glyphIndex (event));
	}
}
