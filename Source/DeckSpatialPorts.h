// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "DeckMotion.h"
#include "DeckSpatial.h"
#include "Outputs.h"

#include <memory>

class StemDeckPlayer;

// The deck's beat time for this block, as the one-shots and the events read it:
// the grid's beat at the playhead, tempo, rate, transport, the playhead's seeks
// and loop wraps, and the loop in beats. Audio thread, after the player's block.
motionlite::Clock deckClock (const StemDeckPlayer&);

// One deck's spatial ports: its DeckSpatial fed from the player's pre-mute
// block, written to the deck's output channels as the layout places them
// (Outputs.h). In two-deck mode (stage S2) 20 of them -- B-format, the
// binaural cue, and phones carrying that cue, both from the deck's one
// decoder (R2) -- and no AUX: the AUX bus stays the AUX. A -bformat instance
// has 43: B-format, the 16-channel AUX bus, the 9-channel cue and phones.
// Mute, cue and transport follow the player each block. Each stem's AUX, home
// and width, the deck 3D and the phones (level, CUE<->MIX, decoder on) are the
// settings, copied each block: nobody changes the deck-wide ones in two-deck
// mode, so there deck 3D stays 1 and StemDeck's phones decoder off. The
// one-shots (DeckMotion) move the played pose from the home in the player's
// beat time, every block.
class DeckSpatialPorts
{
public:
	explicit DeckSpatialPorts (outputs::SpatialLayout = outputs::twoDeckSpatialLayout());
	~DeckSpatialPorts();

	// Builds the IEM nodes; returns an error and renders silence if IEM refuses.
	juce::String create();
	bool prepare (double sampleRate, int blockSize);   // allocates; false and nothing done for a block of 0
	// Audio thread: writes the layout's channels, from firstChannel on, of `outputs`.
	void render (const StemDeckPlayer&, juce::AudioBuffer<float>& outputs, int firstChannel, int numSamples);
	DeckSpatial* deckSpatial() const { return spatial.get(); }
	DeckSpatial::Controls& settings() { return userSettings; }
	const DeckSpatial::Controls& settings() const { return userSettings; }
	DeckMotion& motion() { return stemMotion; }
	const DeckMotion& motion() const { return stemMotion; }

private:
	void followPlayer (const StemDeckPlayer&, int numSamples);
	void copySettings (bool withPose);
	void process (const float* const* stems, juce::AudioBuffer<float>& outputs, int firstChannel, int offset, int count);

	outputs::SpatialLayout layout;
	DeckSpatial::Controls userSettings;
	DeckMotion stemMotion;
	std::unique_ptr<DeckSpatial> spatial;
	double sampleRate = 44100.0;
	juce::AudioBuffer<float> silence;   // fed while the deck gives no pre-mute block, so tails and ramps run out
};
