// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "DeckSpatialPorts.h"

#include "Outputs.h"
#include "StemDeckPlayer.h"

namespace
{
constexpr int maxSilenceBlock = 4096;   // as the player's cue room
}

// The position and the grid before the counters: a seek counts before it
// moves either, so a moved position or grid always comes with its seek.
motionlite::Clock deckClock (const StemDeckPlayer& player)
{
	const auto position = player.getPosition();
	const auto grid = player.getBeatGrid();
	motionlite::Clock clock;
	clock.beat = grid.isValid() ? std::optional<double> (grid.beatsAt (position)) : std::nullopt;
	clock.bpm = grid.bpm;
	clock.rate = player.getEffectiveRate();
	clock.playing = player.isPlaying();
	clock.seeks = player.seeks();
	clock.wraps = player.loopWraps();
	clock.loopBeats = player.loopBeats();
	return clock;
}

DeckSpatialPorts::DeckSpatialPorts (outputs::SpatialLayout spatialLayout) : layout (spatialLayout) {}
DeckSpatialPorts::~DeckSpatialPorts() = default;

juce::String DeckSpatialPorts::create()
{
	juce::String error;
	spatial = DeckSpatial::create (error);
	if (spatial == nullptr)
		return error.isNotEmpty() ? error : juce::String ("the spatial engine could not be built");

	copySettings (true);
	return {};
}

bool DeckSpatialPorts::prepare (double newSampleRate, int blockSize)
{
	if (blockSize <= 0)
		return false;
	sampleRate = newSampleRate;
	stemMotion.dropPendingPresses();   // a press from before the stop must not fire now
	silence.setSize (StemDeckPlayer::numOutputChannels, juce::jmax (blockSize, maxSilenceBlock));
	silence.clear();
	if (spatial == nullptr)
		return true;
	copySettings (true);   // a fresh deck starts at the session's poses
	return spatial->prepare (sampleRate, blockSize);
}

// The pose is the one-shots' to write, block by block; only prepare, before
// any block, takes the home as it is.
void DeckSpatialPorts::copySettings (bool withPose)
{
	auto& controls = spatial->controls();
	const auto copy = [] (const std::atomic<float>& from, std::atomic<float>& to)
	{
		to.store (from.load (std::memory_order_relaxed), std::memory_order_relaxed);
	};
	copy (userSettings.deckThreeD, controls.deckThreeD);
	copy (userSettings.phonesLevel, controls.phonesLevel);
	copy (userSettings.phonesMix, controls.phonesMix);
	controls.phonesDecoderOn.store (userSettings.phonesDecoderOn.load (std::memory_order_relaxed), std::memory_order_relaxed);
	for (size_t s = 0; s < (size_t) DeckSpatial::stems; ++s)
	{
		copy (userSettings.aux[s], controls.aux[s]);
		if (! withPose)
			continue;
		copy (userSettings.azimuth[s], controls.azimuth[s]);
		copy (userSettings.elevation[s], controls.elevation[s]);
		copy (userSettings.width[s], controls.width[s]);
	}
}

// The one-shots run in the deck's beat time (R8): its grid at the playhead,
// its tempo and rate, frozen while it is paused.
void DeckSpatialPorts::followPlayer (const StemDeckPlayer& player, int numSamples)
{
	auto& controls = spatial->controls();
	copySettings (false);
	stemMotion.advance (deckClock (player), numSamples / sampleRate, userSettings, controls);
	for (int s = 0; s < DeckSpatial::stems; ++s)
	{
		controls.muted[(size_t) s].store (player.isStemMuted (s), std::memory_order_relaxed);
		controls.cued[(size_t) s].store (player.isStemCued (s), std::memory_order_relaxed);
	}
	controls.playing.store (player.isPlaying(), std::memory_order_relaxed);   // stopped and silent, it stands (R14)
}

void DeckSpatialPorts::render (const StemDeckPlayer& player, juce::AudioBuffer<float>& outputs, int firstChannel, int numSamples)
{
	jassert (firstChannel + layout.channels <= outputs.getNumChannels());
	if (spatial == nullptr)
	{
		for (int c = 0; c < layout.channels; ++c)
			outputs.clear (firstChannel + c, 0, numSamples);
		return;
	}

	followPlayer (player, numSamples);
	if (player.getPreMuteBlockSamples() == numSamples)
	{
		process (player.getPreMuteBlock().getArrayOfReadPointers(), outputs, firstChannel, 0, numSamples);
		return;
	}
	for (int offset = 0; offset < numSamples; offset += silence.getNumSamples())
		process (silence.getArrayOfReadPointers(), outputs, firstChannel, offset,
		         juce::jmin (silence.getNumSamples(), numSamples - offset));
}

void DeckSpatialPorts::process (const float* const* stems, juce::AudioBuffer<float>& outputs, int firstChannel, int offset, int count)
{
	const auto at = [&] (int within) { return outputs.getWritePointer (firstChannel + within) + offset; };
	DeckSpatial::Outputs ports;
	for (int c = 0; c < outputs::bformatChannels; ++c)
		ports.bformat[(size_t) c] = at (layout.bformat + c);
	if (layout.aux >= 0)
		for (int c = 0; c < outputs::bformatChannels; ++c)
			ports.aux[(size_t) c] = at (layout.aux + c);
	if (layout.binauralCue)
		for (int side = 0; side < 2; ++side)
			ports.binauralCue[(size_t) side] = at (layout.cue + side);
	else
		for (int c = 0; c < DeckSpatial::cueChannels; ++c)
			ports.cue[(size_t) c] = at (layout.cue + c);
	for (int side = 0; side < 2; ++side)
		ports.phones[(size_t) side] = at (layout.phones + side);
	spatial->process (stems, ports, count);
}
