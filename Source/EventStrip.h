// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <JuceHeader.h>
#include "DeckHost.h"
#include "DeleteArm.h"
#include "MotionLite.h"

#include <cstdint>
#include <functional>
#include <optional>

class OverviewWaveform;

// A deck's events, as keys (spec Goals 4-7): REC, EVENTS and QUANTIZE, the
// deck's switches; the four nudge keys, ACTIVE and DELETE for the event
// selected on the overview (a tap on its marker); and a short note when
// something is off (no grid, a file the app will not touch, a slot taken, a
// full list), one note at a time.
// State is light, never text: a switch is lit when on, ACTIVE when the
// selected event plays, DELETE while it is armed. Without a grid REC, EVENTS
// and QUANTIZE are dark and not pressable; without a selection the event
// keys are. DELETE takes two presses (DeleteArm). Both screens show one per
// deck: the one-deck screen in one row under the overview, the two-deck
// screen in two under the deck's tile in the band (surface::eventStrip).
class EventStrip : public juce::Component
{
public:
	enum class Nudge { barBack, beatBack, beatForward, barForward };

	EventStrip (DeckHost&, int deckIndex, int rows);
	~EventStrip() override;

	// The overview the markers stand on: it shows the deck's events and the
	// selection, and a tap on a marker there selects it.
	void showOn (OverviewWaveform&);

	// A pad press that went into the deck's motion (OneShotKeys::onPressed):
	// an event while REC is on; a taken slot or a full list is refused and the note says so.
	void pressRecorded (int stem, motionlite::Shape, bool shift, std::uint64_t seed);

	// The selected event's index in the deck's list; -1 for none.
	void select (int index);
	int selected() const;

	// Reads the deck into the keys' lights and the note: the owner's timer.
	void refresh();

	// Empty when all is well; short and in capitals, to be seen, not read. One
	// at a time, the most pressing first: SLOT TAKEN, LIST FULL, NEWER FILE,
	// FILE DAMAGED, NO GRID, NOT SAVED.
	juce::String note() const;

	juce::TextButton& recKey() { return rec; }
	juce::TextButton& eventsKey() { return events; }
	juce::TextButton& quantizeKey() { return quantize; }
	juce::TextButton& nudgeKey (Nudge which) { return nudges[(size_t) which]; }
	juce::TextButton& activeKey() { return active; }
	juce::TextButton& deleteKey() { return remove; }

	std::function<double()> secondsNow = [] { return juce::Time::getMillisecondCounterHiRes() / 1000.0; };

	void paint (juce::Graphics&) override;
	void resized() override;

	static constexpr double refusedNoteSeconds = 3.0;   // how long "SLOT TAKEN" or "LIST FULL" stays

private:
	void toggleRecording();
	void toggleEvents();
	void toggleQuantize();
	void nudge (Nudge);
	void toggleActive();
	void pressDelete();
	void selectSlot (std::optional<std::pair<int, double>> slot);
	void refused (double& noteUntil);
	void showLights();
	void showEvents();

	DeckHost& host;
	const int deckIndex;
	const int rows;
	OverviewWaveform* overview = nullptr;

	// The selection is the event's slot (stem, beat), not its index: the
	// list is sorted, and a recording or a nudge moves the indices.
	std::optional<std::pair<int, double>> selection;
	int seenGeneration = -1;
	// What the overview was last given (showEvents).
	int shownGeneration = -2, shownSelected = -1;
	size_t shownCount = 0;
	DeleteArm deleteArm;
	double slotTakenUntil = -1.0, listFullUntil = -1.0;

	juce::TextButton rec, events, quantize, active, remove;
	std::array<juce::TextButton, 4> nudges;
	juce::Rectangle<int> noteArea;

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EventStrip)
};
