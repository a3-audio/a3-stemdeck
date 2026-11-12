// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>

#include "../BareXServer.h"
#include "OneDeckAudio.h"
#include "OneDeckView.h"
#include "Outputs.h"
#include "StereoMaster.h"
#include "TwoDeckFixture.h"

// Output: Stereo -- the instance without a spatial engine: the deck's stems
// summed to master_L/R, the same pair on phones_L/R, nothing else.
namespace
{
using twodeck::rate;
using twodeck::block;
using twodeck::blocksFor;
using outputs::OutputMode;
constexpr int stems = StemDeckPlayer::numStems;
constexpr int master = outputs::oneDeckStereo::master, phones = outputs::oneDeckStereo::phones;

class OneDeckStereo : public ::testing::Test
{
protected:
	void SetUp() override
	{
		ASSERT_TRUE (folder.createDirectory().wasOk());
		formats.registerBasicFormats();
		set.name = "noise";
		for (int s = 0; s < stems; ++s)
		{
			set.files[(size_t) s] = folder.getChildFile ("stem" + juce::String (s) + ".wav");
			set.stemNames[(size_t) s] = "stem";
			twodeck::writeNoiseStem (set.files[(size_t) s], 31 + s);
		}
		EXPECT_EQ (audio.createSpatial(), juce::String()) << "no engine is built, so none can fail";
		audio.prepareToPlay (block, rate);
		ASSERT_EQ (audio.host().load (set), juce::String());
		player().setLoop (0.5, 2.5);   // the noise is 3 s long
	}

	void TearDown() override
	{
		pool.removeAllJobs (true, 5000);
		audio.releaseResources();
	}

	~OneDeckStereo() override { folder.deleteRecursively(); }

	StemDeckPlayer& player() { return audio.host().player(); }

	void renderBlock()
	{
		out.clear();
		audio.getNextAudioBlock (juce::AudioSourceChannelInfo (&out, 0, block));
	}

	void setAllStems (bool muted, float gain)
	{
		for (int s = 0; s < stems; ++s)
		{
			player().setStemMuted (s, muted);
			player().setStemGain (s, gain);
		}
	}

	juce::ScopedJuceInitialiser_GUI juceInitialiser;
	juce::File folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("stemdeck-onedeck-stereo", "");
	juce::AudioFormatManager formats;
	StemSet set;
	juce::ThreadPool pool;
	AnalysisCache cache { folder.getChildFile ("analysis.xml") };
	OneDeckAudio audio { formats, pool, cache, OutputMode::stereo };
	juce::AudioBuffer<float> out { outputs::oneDeckStereo::channelCount(), block };
};
}

TEST_F (OneDeckStereo, HasTheFourStereoChannels)
{
	EXPECT_EQ (audio.outputMode(), OutputMode::stereo);
	EXPECT_EQ (audio.channelCount(), 4);
}

TEST_F (OneDeckStereo, TheMasterCarriesTheDeckAndPhonesRepeatIt)
{
	setAllStems (false, 1.0f);
	player().play();
	float loudest = 0.0f;
	for (int i = 0; i < blocksFor (0.5); ++i)
	{
		renderBlock();
		loudest = juce::jmax (loudest, out.getMagnitude (master, 0, block), out.getMagnitude (master + 1, 0, block));
		for (int side = 0; side < 2; ++side)
			for (int n = 0; n < block; ++n)
				ASSERT_EQ (out.getSample (phones + side, n), out.getSample (master + side, n)) << "side " << side << " sample " << n;
	}
	EXPECT_GT (loudest, 0.0f) << "the deck plays on the master";
}

TEST_F (OneDeckStereo, TheMasterIsTheSumOfTheStemsAtTheTrim)
{
	// One stem alone at unity: no louder than the trim times full scale (the noise stem is within +-1).
	setAllStems (true, 1.0f);
	player().setStemMuted (0, false);
	player().play();
	for (int i = 0; i < blocksFor (0.3); ++i)
		renderBlock();   // the mute ramps settle
	float loudest = 0.0f;
	for (int i = 0; i < blocksFor (0.5); ++i)
	{
		renderBlock();
		loudest = juce::jmax (loudest, out.getMagnitude (master, 0, block), out.getMagnitude (master + 1, 0, block));
	}
	EXPECT_GT (loudest, 0.0f);
	EXPECT_LE (loudest, stereomaster::trim);
}

TEST_F (OneDeckStereo, MutedStemsLeaveExactSilence)
{
	setAllStems (true, 1.0f);
	player().play();
	for (int i = 0; i < blocksFor (0.3); ++i)
		renderBlock();
	for (int i = 0; i < blocksFor (0.3); ++i)
	{
		renderBlock();
		for (int ch = 0; ch < 4; ++ch)
			for (int n = 0; n < block; ++n)
				ASSERT_EQ (out.getSample (ch, n), 0.0f) << outputs::oneDeckPortName (OutputMode::stereo, ch) << " sample " << n;
	}
}

TEST_F (OneDeckStereo, PeaksAndTheMeterBarsWork)
{
	using outputs::oneDeck::MeteredBus;
	OneDeckView view (audio, formats, 0);
	setAllStems (false, 1.0f);
	player().play();
	for (int i = 0; i < blocksFor (0.3); ++i)
	{
		renderBlock();
		view.outputs().refresh();
	}
	EXPECT_GT (view.outputs().meter (MeteredBus::main).levelDb(), -60.0f) << "MAIN reads the master";
	EXPECT_LE (view.outputs().meter (MeteredBus::aux).levelDb(), -60.0f) << "no AUX in this mode";
	EXPECT_LE (view.outputs().meter (MeteredBus::cue).levelDb(), -60.0f) << "no cue in this mode";

	EXPECT_EQ (audio.popPeak (4), 0.0f) << "past the four: nothing, no crash";
	EXPECT_EQ (audio.popPeak (-1), 0.0f);
	EXPECT_EQ (audio.popBusPeak (MeteredBus::aux), 0.0f);
}
