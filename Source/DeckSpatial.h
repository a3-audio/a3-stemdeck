// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "DecoderGate.h"
#include "SpatialLaw.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>

namespace a3 { class IemNode; struct StereoPose; }
class SteadyBed;

// One deck's spatial engine (design spec §2, §5-§7): per stem a StereoEncoder
// fed the moving share g·x and the steady share (1-g)·x into the main bed, so
// the deck leaves as 16-channel B-format; the cued stems through a cue bed,
// leaving as 2nd-order B-format (M4: the first 9 ACN channels of the 3rd-order
// cue sum, so Core decodes it once); AUX as a second 16-channel bus of the stems'
// own sends (their encoder output plus their steady share through a third bed);
// phones for standalone use, through the deck's one BinauralDecoder.
// The encoders are fed pre-MUTE and MUTE applies to their output (ruling R5),
// so the cue stays pre-mute. Every gain ramps over 10 ms; every volume at its
// minimum is exact silence (R4). The decoder runs only while used (R12), and a
// standing deck runs no encoder and no bed at all (R14).
class DeckSpatial
{
public:
	static constexpr int stems = spatial::stemsPerDeck;

	struct Controls   // written from any thread, read once per block
	{
		Controls() { for (auto& w : width) w = spatial::Pose{}.width; }   // inline: the GUI tests build one without IEM
		std::array<std::atomic<float>, stems> azimuth {}, elevation {}, width {};   // width default 60
		std::array<std::atomic<float>, stems> aux {};   // each stem's send to the aux bus, 0..1
		std::array<std::atomic<bool>, stems> muted {}, cued {};
		std::atomic<float> deckThreeD { 1.0f }, phonesLevel { 1.0f }, phonesMix { 0.0f };
		std::atomic<bool> phonesDecoderOn { false };   // the app sets it from spatial::phonesDecoderRuns
		std::array<std::atomic<float>, stems> envelope {};   // a one-shot's 3D envelope, times deckThreeD; 0 = none, steady
		std::atomic<bool> playing { true };   // false, silent and settled, the deck stands (R14)
	};

	static constexpr int cueChannels = 9, auxChannels = 9;   // ACN 0..8: 2nd order

	// A null entry: that output is not rendered. binauralCue is two-deck mode's
	// (ruling R2): the phones decoder's output at cue 1, mix 0.
	struct Outputs
	{
		std::array<float*, spatial::bformatChannels> bformat {}, aux {};
		std::array<float*, cueChannels> cue {};
		std::array<float*, 2> binauralCue {}, phones {};
	};

	// Mean µs per process() call (S1.4 calls with 128-sample blocks) since resetTiming();
	// 0 for a node never called. Audio thread only. The phones decoder's figure and
	// sample count include its priming blocks after a reconnect (one per chunk, for
	// 2048 samples): real work on the audio thread, so it counts.
	struct Timing { double encoders, bed, cueBed, phonesDecoder; };
	// Samples the phones decoder, and the aux bed, ran since resetTiming(). Audio thread only.
	struct DecoderRuns { std::int64_t phones, auxBed; };

	static std::unique_ptr<DeckSpatial> create (juce::String& error);
	static int binauralDecodersCreated();   // in this process, by create(); for tests
	~DeckSpatial();

	// allocates; IEM nodes at blockSize, longer calls chunked. A block of 0 is refused: false, nothing changed.
	bool prepare (double sampleRate, int blockSize);
	// stemsPreMute: 8 channels, stem s at 2s and 2s+1, post GAIN, pre MUTE.
	void process (const float* const* stemsPreMute, const Outputs&, int numSamples);
	Controls& controls();

	Timing timing() const;
	DecoderRuns decoderRuns() const;
	void resetTiming();
	float movingGainNow (int stem) const;   // the encoder input gain where the ramp is now; audio thread only, for tests

private:
	struct Stem;
	using Ramp = juce::LinearSmoothedValue<float>;
	struct Clock { std::int64_t ticks = 0; };

	DeckSpatial();
	void readControls();
	void processChunk (const float* const* stemsPreMute, const Outputs&, int offset, int count);
	bool stands (const float* const* stemsPreMute, int offset, int count) const;
	void finishGlide (Stem&);
	void renderSilentMain (const Outputs&, int offset, int count);
	void encodeStem (Stem&, const float* left, const float* right, int count);
	a3::StereoPose poseOf (int stem) const;
	void renderMain (const Outputs&, int offset, int count);
	void renderAux (const Outputs&, int offset, int count);
	bool auxWanted() const;
	void renderCue (const Outputs&, int offset, int count, bool wanted);
	void renderPhones (const Outputs&, int offset, int count, bool cueInUse);
	void setPhonesTargets (bool binauralCue);
	void primePhonesDecoderOneBlock();
	bool cueWanted() const;

	Controls state;
	bool deckPlaying = true;   // state.playing, as read for this chunk
	std::array<std::unique_ptr<Stem>, stems> stemNodes;
	std::unique_ptr<SteadyBed> mainBed, cueBed, auxBed;
	std::unique_ptr<a3::IemNode> phonesDecoder;
	DecoderGate phonesGate, auxGate;
	spatial::PhonesWeights phonesSettings {};   // LEVEL and CUE<->MIX, as read for this chunk
	bool phonesDecoderOn = false;               // state.phonesDecoderOn, as read for this chunk
	bool phonesDecoding = false;       // the phones decoder ran in the last chunk
	bool phonesDecoderStale = false;   // it may hold a tail: stopped hard, before its hold-over ran out
	int phonesPrimeRemaining = 0;      // silence still to run through it before it decodes the deck again
	Ramp phonesCueWeight, phonesMixWeight;

	// Prepared at blockSize: 16-ch sums and decoder io, stereo bed inputs, per-sample gains.
	juce::AudioBuffer<float> mainSum, cueSum, auxSum, bedOut, auxBedOut, phonesDecoderIo;
	juce::AudioBuffer<float> bedIn, cueBedIn, auxBedIn, scratch;
	int blockSize = 0;

	Clock encoderClock, bedClock, cueBedClock, phonesDecoderClock;
	std::int64_t timedBlocks = 0, phonesDecoderSamples = 0, auxBedSamples = 0;
};
