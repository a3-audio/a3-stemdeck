// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "StemEventsFile.h"

#include <juce_core/juce_core.h>

#include <functional>
#include <optional>
#include <vector>

// The events of the track a deck holds: loaded from the file beside its first
// stem, handed to the deck's motion, changed by recording (and later by the
// event strip), saved back. Message thread only.
//
// A damaged file, or one from a newer StemDeck, plays plain: nothing is loaded
// and the app never writes that file, not even after a change; recording and
// edits still work in memory for this track, and the note says why nothing is
// kept. The DJ (or the preparation tool) mends the file and reloads. Saving waits a
// second after the last change and is forced on a track change and on quit
// (the destructor), so a burst of presses writes once.
class TrackEvents
{
public:
	// Where the events go: the deck's motion. newTrack always comes before the
	// new track's setEvents (DeckMotion.h says why).
	struct Motion
	{
		std::function<void()> newTrack;
		std::function<void (const std::vector<stemevents::Event>&)> setEvents;
	};

	enum class Record { added, occupied, full, notRecording, noGrid };

	static constexpr double saveDelaySeconds = 1.0;

	explicit TrackEvents (Motion);
	~TrackEvents();

	// A new track: the old one's unsaved events are written first.
	void load (const juce::File& firstStemFile);
	// No track any more (a hand-over without a staged set): saved, then empty.
	void unload();

	void setRecording (bool on) { recording = on; }
	bool isRecording() const { return recording; }

	// A recorded press (EventRecorder.h's event; none without a grid). An
	// occupied slot is refused and keeps its event.
	Record record (const std::optional<stemevents::Event>&, double nowSeconds);
	// Any other change; `edit` returns whether it changed the list.
	bool change (const std::function<bool (stemevents::EventList&)>& edit, double nowSeconds);

	// Writes when a change is a second old; true if it wrote.
	bool saveIfDue (double nowSeconds);
	void flush();
	bool hasUnsaved() const { return dirty; }

	const stemevents::EventList& list() const { return events; }
	stemevents::Status status() const { return loadedStatus; }
	const juce::File& file() const { return eventsFile; }

	// A short line for the event strip: why events are off or not kept; none if all is well.
	std::optional<juce::String> note (bool hasGrid) const;

private:
	void publish();
	void changed (double nowSeconds);
	void save();

	Motion motion;
	juce::File eventsFile;
	stemevents::EventList events;
	stemevents::Status loadedStatus = stemevents::Status::none;
	bool recording = false;
	bool dirty = false;
	bool saveFailed = false;
	double changedAt = 0.0;
};
