// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <JuceHeader.h>
#include "DeckPanel.h"
#include "EventStrip.h"
#include "LevelMeter.h"
#include "OneDeckAudio.h"
#include "StemColumn.h"
#include "StemThumbnails.h"
#include "Waveforms.h"

#include <array>
#include <functional>
#include <memory>

// The one-deck screen's meters (2026-10-28): MAIN, AUX and CUE, one bar each,
// each its bus's W (ACN 0, outputs::oneDeck::wChannel) -- the level, not the
// field. Only what StemDeck sends; cue and level of the phones are the desk's.
class OneDeckOutputs : public juce::Component
{
public:
	explicit OneDeckOutputs (OneDeckAudio&);

	void refresh();   // the owner's timer
	void setMeterParameters (MeterParameters);

	const LevelMeter& meter (outputs::oneDeck::MeteredBus bus) const { return meters[(size_t) bus]; }

	void paint (juce::Graphics&) override;
	void resized() override;

private:
	OneDeckAudio& audio;
	std::array<LevelMeter, outputs::oneDeck::meteredBuses.size()> meters;
	std::array<juce::Rectangle<int>, outputs::oneDeck::meteredBuses.size()> captions;

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OneDeckOutputs)
};

//==============================================================================
// The deck part of the one-deck screen (v4 mockup, spec section 9): the
// rolling waveform with its four lanes and the overview; on the left the deck
// panel -- the two-deck DeckPanel (times, keys) with its band parts (title,
// stems, BPM, pitch fader, range, KEY) placed here -- and SHIFT, which makes
// the next pad press start now; the four stem columns; the foot row with the
// deck's transport (CUE | PLAY, the track search, the loop block) and the
// meters (surface::oneDeckFoot). Right under the overview the deck's event
// strip in one row (EventStrip.h); its markers stand on the rolling waveform.
// The top bar and the library are the window's.
class OneDeckView : public juce::Component
{
public:
	OneDeckView (OneDeckAudio&, juce::AudioFormatManager&, int deckIndex);
	~OneDeckView() override;

	// After the deck's host has loaded it.
	void setSet (const StemSet&);
	// The owner's timer.
	void refresh();

	// The deck panel's (tempo, range, key lock, vinyl, repeat) and each
	// stem's GAIN, MUTE and CUE; the rest is DeckHost's.
	void saveState (DeckSession&) const;
	void restoreState (const DeckSession&);

	void setMeterParameters (MeterParameters parameters) { outputRow.setMeterParameters (parameters); }

	DeckPanel& deckPanel() { return panel; }
	StemColumn& column (int stem) { return *columns[(size_t) stem]; }
	OneDeckOutputs& outputs() { return outputRow; }
	ScrollingWaveform& waveform() { return wave; }
	EventStrip& eventStrip() { return events; }
	juce::TextButton& shift() { return shiftKey; }

	void paint (juce::Graphics&) override;
	void resized() override;

private:
	bool takeShift();

	OneDeckAudio& audio;
	const int deckIndex;
	juce::AudioThumbnailCache thumbCache { 4 };
	StemThumbnails thumbnails;
	ScrollingWaveform wave;
	DeckPanel panel;
	juce::TextButton shiftKey { "SHIFT" };
	EventStrip events;   // after the waveform, which shows its markers
	std::array<std::unique_ptr<StemColumn>, StemDeckPlayer::numStems> columns;
	OneDeckOutputs outputRow;
	juce::Rectangle<int> deckCard;   // the deck panel's card, under DeckPanel and its band parts
	juce::Rectangle<int> transportTile;   // under the transport keys in the foot row

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OneDeckView)
};
