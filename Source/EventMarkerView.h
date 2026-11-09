// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "EventMarkerLayout.h"
#include "StemEvents.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

struct BeatGrid;

// The deck's planned events as markers on a waveform: the list to show, which
// one is selected, their times on the track, and how they are drawn. The
// rolling waveform and the overview each hold one, so both look the same; only
// the rolling one is touched (it uses `hit`).
class EventMarkerView
{
public:
	// The list is not owned and may be null. Any edit to it (record, nudge,
	// ACTIVE, delete) needs another setEvents.
	void setEvents (const stemevents::EventList* newEvents, int selected)
	{
		events = newEvents;
		selectedEvent = selected;
		secondsValid = false;
	}

	// The marker nearest to a fingertip at (x, y); -1 if none.
	int hit (const BeatGrid& grid, juce::Point<float> where, const eventmarkers::Window& window) const;

	// The stem's colour, its shape's letter on it (`lettered`); an inactive
	// event is dim, the selected one outlined in white and drawn last, so
	// nothing covers it. With many events the letters are left out (they would
	// only pile up), except on the selected one, and the corners are square: a
	// path per marker is what 512 of them cannot afford. Markers outside the
	// window are skipped; a grid-less deck has none.
	void draw (juce::Graphics& g, const BeatGrid& grid, const eventmarkers::Window& window, bool lettered) const;

private:
	// The events' track times; rebuilt only when the events or the grid changed.
	const std::vector<double>& seconds (const BeatGrid& grid) const;

	const stemevents::EventList* events = nullptr;
	int selectedEvent = -1;
	mutable std::vector<double> secondsCache;
	mutable double cachedBpm = 0.0, cachedFirstBeat = 0.0;
	mutable size_t cachedCount = 0;
	mutable bool secondsValid = false;
};
