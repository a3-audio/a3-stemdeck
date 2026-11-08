// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>

#include "../SyntheticStems.h"
#include "DeckHost.h"
#include "Outputs.h"

#include <stdexcept>

namespace
{
constexpr double rate = 44100.0;
constexpr int block = 128;

void writeStem (const juce::File& file, const std::vector<float>& samples)
{
	juce::AudioBuffer<float> audio (2, (int) samples.size());
	for (int ch = 0; ch < 2; ++ch)
		audio.copyFrom (ch, 0, samples.data(), (int) samples.size());

	juce::WavAudioFormat wav;
	std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
	auto writer = stream == nullptr ? nullptr
	                                : wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}
	                                                                   .withSampleRate (synthetic::sampleRate)
	                                                                   .withNumChannels (2)
	                                                                   .withBitsPerSample (24));
	if (writer == nullptr || ! writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples()))
		throw std::runtime_error ("cannot write " + file.getFullPathName().toStdString());
}

// Waits on the message thread, where the analysis reports, until `done`.
template <typename Done>
bool dispatchUntil (Done done, int timeoutMs = 60000)
{
	const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
	while (! done())
	{
		if (juce::Time::getMillisecondCounter() > end)
			return false;
		juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
	}
	return true;
}
}

// One deck as both screens use it: a set loaded, its grid found in the
// background (and cached), its sections measured, then played into its
// stem pairs and its spatial ports.
TEST (DeckHost, LoadsASyntheticSetAndPlays)
{
	juce::ScopedJuceInitialiser_GUI juceInitialiser;
	const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("stemdeck-host", "");
	ASSERT_TRUE (folder.createDirectory().wasOk());

	StemSet set;
	set.name = "synthetic";
	const auto stems = synthetic::fromScript ("FFFFkkkkFFFFqqqqFFFF");
	const std::array<const char*, StemSet::numStems> names { "drums", "bass", "other", "vocals" };
	for (int s = 0; s < StemSet::numStems; ++s)
	{
		set.files[(size_t) s] = folder.getChildFile ("synthetic-" + juce::String (s + 1) + ".wav");
		set.stemNames[(size_t) s] = names[(size_t) s];
		writeStem (set.files[(size_t) s], stems.samples[(size_t) s]);
	}

	juce::AudioFormatManager formats;
	formats.registerBasicFormats();
	AnalysisCache cache (folder.getChildFile ("analysis.xml"));
	juce::ThreadPool pool;
	{
		DeckHost host (formats, pool, cache);
		std::vector<bool> analysing;
		host.onAnalysing = [&] (bool now) { analysing.push_back (now); };

		ASSERT_EQ (host.createSpatial(), juce::String());
		host.prepare (rate, block);

		ASSERT_EQ (host.load (set), juce::String());
		EXPECT_TRUE (host.player().isLoaded());
		ASSERT_TRUE (host.loadedSet().has_value());
		EXPECT_EQ (host.loadedSetId(), set.files[0].getFullPathName());
		ASSERT_EQ (analysing, std::vector<bool> { true }) << "nothing cached: analysed";

		ASSERT_TRUE (dispatchUntil ([&] { return analysing.size() == 2; })) << "the analysis reports back";
		EXPECT_FALSE (analysing.back());
		const auto grid = host.player().getBeatGrid();
		ASSERT_TRUE (grid.isValid());
		EXPECT_NEAR (grid.bpm, synthetic::bpm, 1.0);
		EXPECT_TRUE (cache.find (set).has_value()) << "and the grid is cached";
		EXPECT_TRUE (dispatchUntil ([&] { return ! host.sectionBars().empty(); })) << "the sections follow";

		host.player().play();
		juce::AudioBuffer<float> spatial (outputs::channelCount(), block);
		float stemPeak = 0.0f, bformatPeak = 0.0f;
		for (int i = 0; i < (int) (0.5 * rate) / block; ++i)
		{
			const auto& stemPairs = host.render (spatial, outputs::spatialFirstChannel (0), block);
			stemPeak = juce::jmax (stemPeak, stemPairs.getMagnitude (0, block));
			bformatPeak = juce::jmax (bformatPeak, spatial.getMagnitude (outputs::spatialFirstChannel (0), 0, block));
		}
		EXPECT_GT (stemPeak, 0.0f) << "the stems play";
		EXPECT_GT (bformatPeak, 0.0f) << "into the deck's B-format too";
		EXPECT_GT (host.player().getPosition(), 0.4);

		pool.removeAllJobs (true, 5000);
	}
	folder.deleteRecursively();
}

// EVENTS is on from the start; the session switch (Task 11) may turn it off.
TEST (DeckHost, EventsAreOnFromTheStart)
{
	juce::ScopedJuceInitialiser_GUI juceInitialiser;
	const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("stemdeck-host", "");
	ASSERT_TRUE (folder.createDirectory().wasOk());
	juce::AudioFormatManager formats;
	AnalysisCache cache (folder.getChildFile ("analysis.xml"));
	juce::ThreadPool pool;
	{
		DeckHost host (formats, pool, cache);
		EXPECT_TRUE (host.spatialPorts().motion().eventsEnabled());
	}
	folder.deleteRecursively();
}

// A restore pushes each stem's length and direction through the motion's
// queue, which nothing drains before the audio starts: all of it must fit,
// twice over (one-deck restores once, but a second restore must not lose any).
TEST (DeckHost, ARestoreFitsTheMotionQueueBeforeTheAudioRuns)
{
	juce::ScopedJuceInitialiser_GUI juceInitialiser;
	const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("stemdeck-host", "");
	ASSERT_TRUE (folder.createDirectory().wasOk());
	juce::AudioFormatManager formats;
	AnalysisCache cache (folder.getChildFile ("analysis.xml"));
	juce::ThreadPool pool;
	{
		DeckHost host (formats, pool, cache);
		DeckSession restored;
		const std::array<double, 4> lengths { 0.5, 1.0, 2.0, 8.0 };
		for (size_t s = 0; s < restored.stems.size(); ++s)
		{
			restored.stems[s].lengthBars = lengths[s];
			restored.stems[s].reversed = s % 2 == 0;
		}
		host.restoreSpatial (restored);
		host.restoreSpatial (restored);

		DeckSession gathered;
		host.gather (gathered);
		for (size_t s = 0; s < restored.stems.size(); ++s)
		{
			EXPECT_DOUBLE_EQ (gathered.stems[s].lengthBars, lengths[s]) << "stem " << s;
			EXPECT_EQ (gathered.stems[s].reversed, s % 2 == 0) << "stem " << s;
		}
		EXPECT_TRUE (host.spatialPorts().motion().press (0, motionlite::Shape::orbit, false, 1)) << "room for a press after it";
	}
	folder.deleteRecursively();
}
