// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "EventMarkerView.h"
#include "StemSet.h"
#include "TempoAnalysis.h"
#include "Theme.h"

#include <array>

// A marker without a grid has no time: no grid, no markers.
const std::vector<double>& EventMarkerView::seconds (const BeatGrid& grid) const
{
	const auto count = events == nullptr ? size_t (0) : events->events().size();
	const auto unchanged = secondsValid && cachedBpm == grid.bpm && cachedFirstBeat == grid.firstBeat && cachedCount == count;
	if (unchanged)
		return secondsCache;

	secondsCache.clear();
	if (events != nullptr && grid.isValid())
		for (const auto& event : events->events())
			secondsCache.push_back (eventmarkers::eventSeconds (event, grid.firstBeat, grid.bpm));

	cachedBpm = grid.bpm;
	cachedFirstBeat = grid.firstBeat;
	cachedCount = count;
	secondsValid = true;
	return secondsCache;
}

int EventMarkerView::hit (const BeatGrid& grid, juce::Point<float> where, const eventmarkers::Window& window) const
{
	if (events == nullptr)
		return -1;

	return eventmarkers::hitTest (events->events(), seconds (grid), where.x, where.y, window);
}

void EventMarkerView::draw (juce::Graphics& g, const BeatGrid& grid, const eventmarkers::Window& window, bool lettered) const
{
	const auto& times = seconds (grid);
	if (times.empty())
		return;

	static const std::array<juce::String, eventmarkers::glyphCount> glyphs = [] {
		std::array<juce::String, eventmarkers::glyphCount> texts;
		for (int i = 0; i < eventmarkers::glyphCount; ++i)
			texts[(size_t) i] = juce::String::fromUTF8 (eventmarkers::glyphText (i));
		return texts;
	}();

	const auto laneHeight = window.height / (float) StemSet::numStems;
	const auto& list = events->events();
	const auto lettersFit = lettered && list.size() <= eventmarkers::maxLetteredMarkers;
	const juce::Font font (juce::FontOptions (laneHeight * eventmarkers::heightShare * 0.85f, juce::Font::bold));
	g.setFont (font);

	const auto drawOne = [&] (size_t i, bool selected)
	{
		const auto& event = list[i];
		const auto r = eventmarkers::markerRect (event, times[i], window);
		if (! r)
			return;

		const auto rect = juce::Rectangle<float> (r->x, r->y, r->w, r->h);
		const auto colour = Theme::stem (juce::jlimit (0, StemSet::numStems - 1, event.stem));

		g.setColour (event.active ? colour : colour.withAlpha (0.3f));
		if (lettersFit || selected)
			g.fillRoundedRectangle (rect, rect.getHeight() * 0.25f);
		else
			g.fillRect (rect);

		if (lettered && (lettersFit || selected))
		{
			g.setColour (event.active ? Theme::background : colour.withAlpha (0.7f));
			g.drawText (glyphs[(size_t) eventmarkers::glyphIndex (event)], rect, juce::Justification::centred, false);
		}

		if (! selected)
			return;

		g.setColour (juce::Colours::white);
		g.drawRoundedRectangle (rect.expanded (1.0f), rect.getHeight() * 0.25f, juce::jmax (1.0f, rect.getHeight() * 0.15f));
	};

	for (size_t i = 0; i < list.size() && i < times.size(); ++i)
		if ((int) i != selectedEvent)
			drawOne (i, false);

	if (selectedEvent >= 0 && (size_t) selectedEvent < list.size() && (size_t) selectedEvent < times.size())
		drawOne ((size_t) selectedEvent, true);
}
