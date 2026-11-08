// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "EventStrip.h"

#include "KnobSetup.h"
#include "SurfaceJuce.h"
#include "SurfaceLayout.h"
#include "Theme.h"
#include "Waveforms.h"

namespace
{
	// U+25C4 / U+25BA, as on ORBIT's face (OneShotKeys::shapeName).
	const juce::String back = juce::String::fromUTF8 ("\xe2\x97\x84 ");
	const juce::String forward = juce::String::fromUTF8 (" \xe2\x96\xba");

	constexpr double beatsPerBar = motionlite::beatsPerBar;
	constexpr float noteFontShare = 0.32f;   // of the note's slot height: two short lines fit

	double nudgeBeats (EventStrip::Nudge which)
	{
		switch (which)
		{
			case EventStrip::Nudge::barBack:     return -beatsPerBar;
			case EventStrip::Nudge::beatBack:    return -1.0;
			case EventStrip::Nudge::beatForward: return 1.0;
			case EventStrip::Nudge::barForward:  return beatsPerBar;
		}
		return 0.0;
	}

	void setUpKey (juce::TextButton& key, const juce::String& name, juce::Colour lit)
	{
		key.setButtonText (name);
		key.setColour (juce::TextButton::buttonOnColourId, lit);
		key.setColour (juce::TextButton::textColourOnId, lit.contrasting());
		knobsetup::untabbableKey (key);
	}
}

EventStrip::EventStrip (DeckHost& deckHost, int index, int rowCount)
	: host (deckHost), deckIndex (index), rows (rowCount)
{
	setUpKey (rec, "REC", Theme::mute);
	setUpKey (events, "EVENTS", Theme::deck (deckIndex));
	setUpKey (quantize, "QUANTIZE", Theme::deck (deckIndex));
	setUpKey (active, "ACTIVE", Theme::play);
	setUpKey (remove, "DELETE", Theme::mute);
	setUpKey (nudges[(size_t) Nudge::barBack], back + "1 BAR", Theme::loop);
	setUpKey (nudges[(size_t) Nudge::beatBack], back + "1 BEAT", Theme::loop);
	setUpKey (nudges[(size_t) Nudge::beatForward], "1 BEAT" + forward, Theme::loop);
	setUpKey (nudges[(size_t) Nudge::barForward], "1 BAR" + forward, Theme::loop);

	rec.onClick = [this] { toggleRecording(); };
	events.onClick = [this] { toggleEvents(); };
	quantize.onClick = [this] { toggleQuantize(); };
	active.onClick = [this] { toggleActive(); };
	remove.onClick = [this] { pressDelete(); };
	for (size_t n = 0; n < nudges.size(); ++n)
		nudges[n].onClick = [this, n] { nudge ((Nudge) n); };

	for (auto* key : { &rec, &events, &quantize, &active, &remove })
		addAndMakeVisible (key);
	for (auto& key : nudges)
		addAndMakeVisible (key);
	refresh();
}

EventStrip::~EventStrip()
{
	if (overview == nullptr)
		return;
	overview->onMarkerTapped = nullptr;
	overview->setEvents (nullptr, -1);
}

void EventStrip::showOn (OverviewWaveform& shownOn)
{
	overview = &shownOn;
	overview->onMarkerTapped = [this] (int index) { select (index); };
	showEvents();
}

//==============================================================================
void EventStrip::pressRecorded (int stem, motionlite::Shape shape, bool shift, std::uint64_t seed)
{
	if (host.recordPress (stem, shape, shift, seed) == TrackEvents::Record::occupied)
		refused();
	showEvents();
}

int EventStrip::selected() const
{
	if (! selection)
		return -1;
	return host.trackEvents().list().indexAt (selection->first, selection->second);
}

void EventStrip::select (int index)
{
	const auto& list = host.trackEvents().list().events();
	if (index < 0 || index >= (int) list.size())
		return selectSlot (std::nullopt);
	selectSlot (std::make_pair (list[(size_t) index].stem, list[(size_t) index].beat));
}

void EventStrip::selectSlot (std::optional<std::pair<int, double>> slot)
{
	if (slot != selection)
		deleteArm.drop();
	selection = slot;
	showLights();
	showEvents();
}

//==============================================================================
void EventStrip::toggleRecording()
{
	host.setRecording (! host.isRecording());
	showLights();
}

void EventStrip::toggleEvents()
{
	auto& motion = host.spatialPorts().motion();
	motion.setEventsEnabled (! motion.eventsEnabled());
	showLights();
}

void EventStrip::toggleQuantize()
{
	auto& player = host.player();
	player.setQuantize (! player.quantizeRequested());
	showLights();
}

// Whole beats from where the event stands; a taken slot keeps it where it is.
void EventStrip::nudge (Nudge which)
{
	const auto index = selected();
	if (index < 0)
		return;
	const auto beats = nudgeBeats (which);
	if (! host.changeEvents ([index, beats] (stemevents::EventList& list) { return list.nudge (index, beats); }))
		return refused();
	selection->second += beats;
	showEvents();
}

void EventStrip::toggleActive()
{
	const auto index = selected();
	if (index < 0)
		return;
	const auto nowActive = host.trackEvents().list().events()[(size_t) index].active;
	host.changeEvents ([index, nowActive] (stemevents::EventList& list) { return list.setActive (index, ! nowActive); });
	showLights();
	showEvents();
}

void EventStrip::pressDelete()
{
	const auto index = selected();
	if (index < 0)
		return;
	deleteArm.press (secondsNow());
	if (deleteArm.confirmed())
	{
		host.changeEvents ([index] (stemevents::EventList& list) { return list.remove (index); });
		return selectSlot (std::nullopt);
	}
	showLights();
}

void EventStrip::refused()
{
	refusedUntil = secondsNow() + refusedNoteSeconds;
	repaint();
}

//==============================================================================
juce::String EventStrip::note() const
{
	if (! host.loadedSet())
		return {};
	if (secondsNow() < refusedUntil)
		return "SLOT TAKEN";
	const auto status = host.trackEvents().status();
	if (status == stemevents::Status::newer)
		return "NEWER FILE";
	if (status == stemevents::Status::damaged)
		return "FILE DAMAGED";
	if (! host.hasGrid())
		return "NO GRID";
	if (host.eventsNote())
		return "NOT SAVED";
	return {};
}

void EventStrip::refresh()
{
	// A new track: its events are others, so nothing stays selected or armed.
	if (host.loadGeneration() != seenGeneration)
	{
		seenGeneration = host.loadGeneration();
		selection.reset();
		deleteArm.drop();
	}
	if (selection && selected() < 0)
		selection.reset();
	// A grid lost (a set without one) ends the recording.
	if (host.isRecording() && ! host.hasGrid())
		host.setRecording (false);
	showLights();
	// Every edit here shows itself; a track change, or a list that grew or
	// shrank, is shown on the tick (the overview keeps its marker times
	// until told).
	if (shownGeneration != seenGeneration || shownCount != host.trackEvents().list().events().size()
		|| shownSelected != selected())
		showEvents();
	repaint (noteArea);
}

void EventStrip::showLights()
{
	const auto grid = host.hasGrid();
	const auto index = selected();
	const auto hasSelection = index >= 0;

	for (auto* key : { &rec, &events, &quantize })
		key->setEnabled (grid);
	rec.setToggleState (grid && host.isRecording(), juce::dontSendNotification);
	events.setToggleState (grid && host.spatialPorts().motion().eventsEnabled(), juce::dontSendNotification);
	quantize.setToggleState (host.player().quantize(), juce::dontSendNotification);

	for (auto* key : { &active, &remove })
		key->setEnabled (hasSelection);
	for (auto& key : nudges)
		key.setEnabled (hasSelection);
	active.setToggleState (hasSelection && host.trackEvents().list().events()[(size_t) index].active, juce::dontSendNotification);
	remove.setToggleState (hasSelection && deleteArm.armed (secondsNow()), juce::dontSendNotification);
}

void EventStrip::showEvents()
{
	shownGeneration = seenGeneration;
	shownCount = host.trackEvents().list().events().size();
	shownSelected = selected();
	if (overview != nullptr)
		overview->setEvents (&host.trackEvents().list(), shownSelected);
}

//==============================================================================
void EventStrip::paint (juce::Graphics& g)
{
	const auto text = note();
	if (text.isEmpty() || noteArea.isEmpty())
		return;
	g.setColour (Theme::cue);
	g.setFont (juce::FontOptions ((float) noteArea.getHeight() * noteFontShare, juce::Font::bold));
	g.drawFittedText (text, noteArea, juce::Justification::centred, 2, 0.7f);
}

void EventStrip::resized()
{
	const auto keys = surface::eventStrip (surface::fromJuce (getLocalBounds()), surface::metrics (getParentWidth()), rows);
	const auto place = [] (juce::Component& c, const surface::Rect& r) { c.setBounds (surface::toJuce (r)); };
	place (rec, keys.rec);
	place (events, keys.events);
	place (quantize, keys.quantize);
	place (active, keys.active);
	place (remove, keys.remove);
	for (size_t n = 0; n < nudges.size(); ++n)
		place (nudges[n], keys.nudge[n]);
	noteArea = surface::toJuce (keys.note);
}
