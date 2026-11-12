// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "OneDeckAudio.h"

#include "AudioCpu.h"
#include "StereoMaster.h"

namespace
{
constexpr int maxBlock = 4096;   // as the deck's own buffers
}

OneDeckAudio::OneDeckAudio (juce::AudioFormatManager& formats, juce::ThreadPool& pool, AnalysisCache& cache, outputs::OutputMode outputMode)
	: mode (outputMode), deck (formats, pool, cache, outputs::oneDeck::layout())   // in stereo its ports never render
{
}

juce::String OneDeckAudio::createSpatial()
{
	return mode == outputs::OutputMode::stereo ? juce::String() : deck.createSpatial();
}

void OneDeckAudio::updatePhonesDecoder (spatial::PhonesMode mode, bool phonesConnected, bool coreHeard)
{
	deck.spatialPorts().settings().phonesDecoderOn.store (spatial::phonesDecoderRuns (mode, phonesConnected, coreHeard));
}

bool OneDeckAudio::phonesDecoderOn() const
{
	return deck.spatialPorts().settings().phonesDecoderOn.load();
}

void OneDeckAudio::setPhones (float level, float mix)
{
	auto& settings = deck.spatialPorts().settings();
	settings.phonesLevel.store (level);
	settings.phonesMix.store (mix);
}

float OneDeckAudio::popPeak (int channel)
{
	if (! juce::isPositiveAndBelow (channel, channelCount()))
		return 0.0f;
	return peaks[(size_t) channel].exchange (0.0f, std::memory_order_relaxed);
}

float OneDeckAudio::popBusPeak (outputs::oneDeck::MeteredBus bus)
{
	float loudest = 0.0f;
	for (const auto channel : outputs::oneDeckMeterChannels (mode, bus))
		loudest = juce::jmax (loudest, popPeak (channel));
	return loudest;
}

void OneDeckAudio::prepareToPlay (int samplesPerBlockExpected, double sampleRate)
{
	rendered.setSize (channelCount(), juce::jmax (samplesPerBlockExpected, maxBlock));
	deck.prepare (sampleRate, samplesPerBlockExpected);
}

void OneDeckAudio::renderBFormat (int numSamples)
{
	deck.render (rendered, 0, numSamples);
}

void OneDeckAudio::renderStereo (int numSamples)
{
	const auto& stems = deck.renderStems (numSamples);
	auto* master = rendered.getArrayOfWritePointers() + outputs::oneDeckStereo::master;
	stereomaster::block (stems.getArrayOfReadPointers(), stems.getNumChannels(), numSamples, stereomaster::trim, master[0], master[1]);

	// No cue in this mode: the phones hear what the master carries.
	const auto phones = outputs::oneDeckStereo::phones;
	for (int side = 0; side < 2; ++side)
		rendered.copyFrom (phones + side, 0, rendered, outputs::oneDeckStereo::master + side, 0, numSamples);
}

void OneDeckAudio::getNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill)
{
	pinAudioThreadToItsCpu();

	const auto numSamples = bufferToFill.numSamples;
	if (numSamples > rendered.getNumSamples())
		rendered.setSize (channelCount(), numSamples, false, false, true);

	if (mode == outputs::OutputMode::stereo)
		renderStereo (numSamples);
	else
		renderBFormat (numSamples);

	for (int ch = 0; ch < channelCount(); ++ch)
	{
		auto& peak = peaks[(size_t) ch];
		const auto now = rendered.getMagnitude (ch, 0, numSamples);
		if (now > peak.load (std::memory_order_relaxed))
			peak.store (now, std::memory_order_relaxed);   // one writer; a racing pop loses one block's peak at worst
	}

	auto& out = *bufferToFill.buffer;
	bufferToFill.clearActiveBufferRegion();
	for (int ch = 0; ch < channelCount(); ++ch)
		if (const auto target = outputs::oneDeckFoldsInto (mode, ch, out.getNumChannels()); target >= 0)
			out.addFrom (target, bufferToFill.startSample, rendered, ch, 0, numSamples);
}
