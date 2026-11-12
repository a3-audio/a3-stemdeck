// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <JuceHeader.h>
#include "DeckHost.h"
#include "Outputs.h"
#include "SpatialLaw.h"

#include <array>
#include <atomic>

// The audio of a -bformat instance (spec sections 1, 8): one deck, its 43
// outputs (outputs::oneDeck) and nothing else -- no buses, no mixer. Its
// JACK client pulls it as it is; without JACK only what fits the device
// plays (outputs::oneDeck::foldsInto).
// In Output: Stereo (outputs::OutputMode) the deck has no spatial engine and
// four outputs (outputs::oneDeckStereo): its stems summed to the master pair
// (stereomaster::block), and that pair again on the phones.
class OneDeckAudio : public juce::AudioSource
{
public:
	OneDeckAudio (juce::AudioFormatManager&, juce::ThreadPool& analysisPool, AnalysisCache&,
	              outputs::OutputMode = outputs::OutputMode::bformat);

	outputs::OutputMode outputMode() const { return mode; }
	int channelCount() const { return outputs::oneDeckChannelCount (mode); }

	DeckHost& host() { return deck; }
	const DeckHost& host() const { return deck; }
	juce::String createSpatial();   // stereo: none is built, no error

	// Message thread. R12: StemDeck's own phones decoder runs only where
	// spatial::phonesDecoderRuns says.
	void updatePhonesDecoder (spatial::PhonesMode, bool phonesConnected, bool coreHeard);
	bool phonesDecoderOn() const;
	// LEVEL 0..1 and CUE<->MIX 0 (cue) .. 1 (mix). No control on the screen
	// since 2026-10-28 (cue and level are the desk's): the phones decoder
	// plays at the defaults, 100 % and CUE, unless set here.
	void setPhones (float level, float mix);

	// The screen's meters: a channel's peak since the last call (any output).
	float popPeak (int channel);
	// The same for a bar of the screen (MAIN, AUX, CUE): the louder of its channels.
	float popBusPeak (outputs::oneDeck::MeteredBus);

	void prepareToPlay (int samplesPerBlockExpected, double sampleRate) override;
	void getNextAudioBlock (const juce::AudioSourceChannelInfo&) override;
	void releaseResources() override { deck.releaseResources(); }

private:
	void renderBFormat (int numSamples);
	void renderStereo (int numSamples);

	const outputs::OutputMode mode;
	DeckHost deck;
	juce::AudioBuffer<float> rendered;   // all the outputs of the mode
	std::array<std::atomic<float>, outputs::oneDeck::channels> peaks {};
};
