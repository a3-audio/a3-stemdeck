// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "TrackEvents.h"

TrackEvents::TrackEvents (Motion m) : motion (std::move (m)) {}

TrackEvents::~TrackEvents()
{
	flush();
}

void TrackEvents::load (const juce::File& firstStemFile)
{
	flush();
	eventsFile = stemevents::fileFor (firstStemFile);
	auto loaded = stemevents::load (eventsFile);
	events = std::move (loaded.list);
	loadedStatus = loaded.status;
	saveFailed = false;
	motion.newTrack();
	publish();
}

void TrackEvents::unload()
{
	flush();
	eventsFile = juce::File();
	events = {};
	loadedStatus = stemevents::Status::none;
	saveFailed = false;
	motion.newTrack();
}

TrackEvents::Record TrackEvents::record (const std::optional<stemevents::Event>& event, double nowSeconds)
{
	if (! recording)
		return Record::notRecording;
	if (! event)
		return Record::noGrid;

	switch (events.add (*event))
	{
		case stemevents::EventList::Add::occupied: return Record::occupied;
		case stemevents::EventList::Add::full: return Record::full;
		case stemevents::EventList::Add::added: break;
	}
	changed (nowSeconds);
	return Record::added;
}

bool TrackEvents::change (const std::function<bool (stemevents::EventList&)>& edit, double nowSeconds)
{
	if (! edit (events))
		return false;
	changed (nowSeconds);
	return true;
}

bool TrackEvents::saveIfDue (double nowSeconds)
{
	if (! dirty || nowSeconds < changedAt + saveDelaySeconds)
		return false;
	save();
	return true;
}

void TrackEvents::flush()
{
	if (dirty)
		save();
}

std::optional<juce::String> TrackEvents::note (bool hasGrid) const
{
	// The file's state first: a set without a grid must not hide a file it cannot read.
	if (loadedStatus == stemevents::Status::newer)
		return juce::String ("events file from a newer StemDeck: not loaded, not saved");
	if (loadedStatus == stemevents::Status::damaged)
		return juce::String ("events file damaged: not loaded, not saved");
	if (! hasGrid)
		return juce::String ("no grid");
	if (saveFailed)
		return juce::String ("events not saved");
	return std::nullopt;
}

void TrackEvents::publish()
{
	motion.setEvents (events.events());
}

void TrackEvents::changed (double nowSeconds)
{
	publish();
	dirty = true;
	changedAt = nowSeconds;
}

// A file loaded damaged or newer is never written: it may be a hand-edited or
// prepared file with one typo, or one a later StemDeck still understands. The
// events stay in memory for this track only.
void TrackEvents::save()
{
	dirty = false;
	if (eventsFile == juce::File())
		return;
	if (loadedStatus == stemevents::Status::damaged || loadedStatus == stemevents::Status::newer)
	{
		saveFailed = true;
		return;
	}
	saveFailed = ! stemevents::save (eventsFile, events);
}
