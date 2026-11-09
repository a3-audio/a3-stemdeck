// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>

#include "../BareXServer.h"
#include "OneDeckAudio.h"
#include "AuxPots.h"
#include "KnobSetup.h"
#include "OneDeckView.h"
#include "Outputs.h"
#include "StemKnob.h"
#include "Theme.h"
#include "TwoDeckFixture.h"

#include <vector>

// A -bformat instance's audio (S4.4a): one DeckHost on the 43 one-deck
// channels, through the AudioSource the JACK client pulls.
namespace
{
using twodeck::rate;
using twodeck::block;
using twodeck::blocksFor;
constexpr int stems = StemDeckPlayer::numStems;

class OneDeck : public ::testing::Test
{
protected:
	void SetUp() override
	{
		ASSERT_TRUE (folder.createDirectory().wasOk());
		formats.registerBasicFormats();
		set.name = "noise";
		const std::array<const char*, stems> names { "drums", "bass", "vocals", "synth" };
		for (int s = 0; s < stems; ++s)
		{
			set.files[(size_t) s] = folder.getChildFile ("stem" + juce::String (s) + ".wav");
			set.stemNames[(size_t) s] = names[(size_t) s];
			twodeck::writeNoiseStem (set.files[(size_t) s], 31 + s);
		}
		ASSERT_EQ (audio.createSpatial(), juce::String());
		audio.prepareToPlay (block, rate);
		ASSERT_EQ (audio.host().load (set), juce::String());
		player().setLoop (0.5, 2.5);   // the noise is 3 s long; the tests play longer
	}

	void TearDown() override
	{
		pool.removeAllJobs (true, 5000);
		audio.releaseResources();
	}

	~OneDeck() override { folder.deleteRecursively(); }

	StemDeckPlayer& player() { return audio.host().player(); }
	DeckSpatial::Controls& settings() { return audio.host().spatialPorts().settings(); }

	void renderBlock()
	{
		out.clear();
		audio.getNextAudioBlock (juce::AudioSourceChannelInfo (&out, 0, block));
	}

	void renderBlocks (int count)
	{
		for (int i = 0; i < count; ++i)
			renderBlock();
	}

	float peak (int channel) const { return out.getMagnitude (channel, 0, block); }

	// The highest peak of `channel` over the next `seconds`.
	float peakOver (int channel, double seconds)
	{
		float highest = 0.0f;
		for (int i = 0; i < blocksFor (seconds); ++i)
		{
			renderBlock();
			highest = juce::jmax (highest, peak (channel));
		}
		return highest;
	}

	// Every sample of channels [first, first + count) is 0.0f, block after block.
	void expectExactlySilentFor (int first, int count, double seconds, const char* what)
	{
		for (int i = 0; i < blocksFor (seconds); ++i)
		{
			renderBlock();
			for (int ch = first; ch < first + count; ++ch)
				for (int n = 0; n < block; ++n)
					ASSERT_EQ (out.getSample (ch, n), 0.0f) << what << ": " << outputs::oneDeck::portName (ch) << " sample " << n;
		}
	}

	void setAllAux (float aux)
	{
		for (auto& send : settings().aux)
			send.store (aux);
	}

	void setAllStems (bool muted, bool cued, float gain)
	{
		for (int s = 0; s < stems; ++s)
		{
			player().setStemMuted (s, muted);
			player().setStemCue (s, cued);
			player().setStemGain (s, gain);
		}
	}

	juce::ScopedJuceInitialiser_GUI juceInitialiser;
	juce::File folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("stemdeck-onedeck", "");
	juce::AudioFormatManager formats;
	StemSet set;
	juce::ThreadPool pool;
	AnalysisCache cache { folder.getChildFile ("analysis.xml") };
	OneDeckAudio audio { formats, pool, cache };
	juce::AudioBuffer<float> out { outputs::oneDeck::channelCount(), block };
	const outputs::SpatialLayout layout = outputs::oneDeck::layout();
};
}

// R4 through the instance's own path: the GAIN knob at its bottom, MUTE,
// phones LEVEL 0 and either end of CUE<->MIX each leave their outputs
// exactly 0.0f once the ramps have settled.
TEST_F (OneDeck, VolumeMinimaAreExactSilence)
{
	audio.updatePhonesDecoder (spatial::PhonesMode::on, true, false);
	audio.setPhones (1.0f, 0.5f);
	setAllStems (false, true, 1.0f);
	player().play();
	renderBlocks (blocksFor (0.3));
	setAllAux (1.0f);
	renderBlocks (blocksFor (0.3));
	for (const auto ch : { layout.bformat, layout.aux, layout.cue, layout.phones })
		ASSERT_GT (peakOver (ch, 0.05), 0.0f) << "the deck plays: " << outputs::oneDeck::portName (ch);

	// GAIN at the knob's minimum: every one of the 43.
	setAllStems (false, true, juce::Decibels::decibelsToGain (stemKnobMinimumDb, stemKnobMinimumDb));
	renderBlocks (blocksFor (0.5));
	expectExactlySilentFor (0, layout.channels, 0.2, "GAIN at its minimum");

	// MUTE: the B-format and the AUX; the cue is pre-mute and plays on, in its
	// own 9 channels and in the phones.
	setAllStems (true, true, 1.0f);
	renderBlocks (blocksFor (0.5));
	EXPECT_GT (peakOver (layout.cue, 0.05), 0.0f) << "the cue is pre-mute";
	EXPECT_GT (peakOver (layout.phones, 0.05), 0.0f) << "the cue is pre-mute";
	expectExactlySilentFor (layout.bformat, outputs::bformatChannels, 0.2, "MUTE");
	expectExactlySilentFor (layout.aux, outputs::bformatChannels, 0.2, "MUTE");

	// CUE<->MIX at MIX: the cue's weight is exactly 0, and the muted deck's mix is silence.
	audio.setPhones (1.0f, 1.0f);
	renderBlocks (blocksFor (0.5));
	expectExactlySilentFor (layout.phones, 2, 0.2, "CUE<->MIX at MIX");

	// CUE<->MIX at CUE with nothing cued: the deck plays, the phones are silent.
	setAllStems (false, false, 1.0f);
	audio.setPhones (1.0f, 0.0f);
	renderBlocks (blocksFor (0.5));
	ASSERT_GT (peakOver (layout.bformat, 0.05), 0.0f);
	expectExactlySilentFor (layout.phones, 2, 0.2, "CUE<->MIX at CUE, nothing cued");

	// LEVEL 0: the phones, whatever else plays.
	setAllStems (false, true, 1.0f);
	audio.setPhones (0.0f, 0.5f);
	renderBlocks (blocksFor (0.5));
	ASSERT_GT (peakOver (layout.bformat, 0.05), 0.0f);
	expectExactlySilentFor (layout.phones, 2, 0.2, "LEVEL 0");

	// AUX 0: the send, settled, leaves the aux bus exactly 0.0f.
	setAllStems (false, true, 1.0f);
	setAllAux (0.0f);
	renderBlocks (blocksFor (0.5));
	expectExactlySilentFor (layout.aux, outputs::bformatChannels, 0.2, "AUX 0");
	ASSERT_GT (peakOver (layout.bformat, 0.05), 0.0f);
	expectExactlySilentFor (layout.phones, 2, 0.2, "phones LEVEL 0");
}

// R12: StemDeck's own phones decoder runs only with its ports connected, in
// automatic only while Core is not heard; `on` ignores Core, `off` never runs.
// With nothing cued and the phones at MIX, the phones carry sound exactly
// when it runs.
// PipeWire calls the sample-rate callback before a block size is known: the
// whole deck (player and engine) refuses a block of 0 and prepares at 128 later.
TEST_F (OneDeck, PrepareWithBlockSizeZeroIsRefused)
{
	EXPECT_FALSE (audio.host().prepare (rate, 0));
	audio.prepareToPlay (0, rate);   // the JACK path: must not divide by zero
	EXPECT_TRUE (audio.host().prepare (rate, block));
	setAllStems (false, false, 1.0f);
	player().play();
	renderBlocks (blocksFor (0.3));
	EXPECT_GT (peakOver (layout.bformat, 0.05), 0.0f);
}

TEST_F (OneDeck, PhonesDecoderFollowsR12)
{
	using spatial::PhonesMode;
	setAllStems (false, false, 1.0f);
	audio.setPhones (1.0f, 1.0f);
	player().play();

	for (const auto mode : { PhonesMode::automatic, PhonesMode::on, PhonesMode::off })
		for (const auto connected : { false, true })
			for (const auto coreHeard : { false, true })
			{
				const auto expected = spatial::phonesDecoderRuns (mode, connected, coreHeard);
				audio.updatePhonesDecoder (mode, connected, coreHeard);
				EXPECT_EQ (audio.phonesDecoderOn(), expected);
				renderBlocks (blocksFor (0.3));   // ramps, hold-over and the IR tail run out
				const auto runsBefore = audio.host().spatialPorts().deckSpatial()->decoderRuns().phones;
				const auto heard = peakOver (layout.phones, 0.1);
				const auto ran = audio.host().spatialPorts().deckSpatial()->decoderRuns().phones > runsBefore;
				EXPECT_EQ (heard > 0.0f, expected) << "mode " << (int) mode << ", connected " << connected << ", Core heard " << coreHeard;
				EXPECT_EQ (ran, expected) << "mode " << (int) mode << ", connected " << connected << ", Core heard " << coreHeard;
			}
}

// Core is heard while a /core/here came within the last five seconds (Core
// announces every two).
TEST (OneDeckCore, HeardWithinFiveSeconds)
{
	EXPECT_FALSE (spatial::coreHeard (std::nullopt, 100.0));
	EXPECT_TRUE (spatial::coreHeard (100.0, 100.0));
	EXPECT_TRUE (spatial::coreHeard (100.0, 104.9));
	EXPECT_FALSE (spatial::coreHeard (100.0, 105.1));
}

//==============================================================================
// The v4 screen (S4.4b): the deck part of a -bformat instance, on the
// fixture's deck. Components need JUCE's GUI; the binary runs under xvfb.
namespace
{
constexpr int screenWidth = 768, viewHeight = 760;   // the rig's width; the deck part of its 1024
// What OneDeckComponent at the rig's 768 x 1024 gives its view: the window less
// its gap all round, the top bar and the library.
constexpr int rigViewWidth = 756, rigViewHeight = 718;

// How much a snapshot varies: a screen that painted only its ground is flat.
double lumaVariance (const juce::Image& image)
{
	const juce::Image::BitmapData pixels (image, juce::Image::BitmapData::readOnly);
	double sum = 0.0, squares = 0.0;
	const auto count = (double) image.getWidth() * image.getHeight();
	for (int y = 0; y < image.getHeight(); ++y)
		for (int x = 0; x < image.getWidth(); ++x)
		{
			const auto colour = pixels.getPixelColour (x, y);
			const double luma = (colour.getRed() + colour.getGreen() + colour.getBlue()) / 3.0;
			sum += luma;
			squares += luma * luma;
		}
	const auto mean = sum / count;
	return squares / count - mean * mean;
}

int differingPixels (const juce::Image& a, const juce::Image& b)
{
	const juce::Image::BitmapData first (a, juce::Image::BitmapData::readOnly), second (b, juce::Image::BitmapData::readOnly);
	int differing = 0;
	for (int y = 0; y < a.getHeight(); ++y)
		for (int x = 0; x < a.getWidth(); ++x)
			differing += first.getPixelColour (x, y) != second.getPixelColour (x, y) ? 1 : 0;
	return differing;
}

juce::Image snapshotOf (OneDeckView& view)
{
	view.refresh();
	return view.createComponentSnapshot (view.getLocalBounds());
}
}

// Every state the screen shows, under the app's look and feel: each snapshot
// painted more than a ground, and each state changed the picture.
TEST_F (OneDeck, PaintsEveryState)
{
	DJLookAndFeel look;
	constexpr double minimumVariance = 50.0;
	{
		OneDeckAudio empty { formats, pool, cache };
		OneDeckView view (empty, formats, 0);
		view.setLookAndFeel (&look);
		view.setSize (screenWidth, viewHeight);
		const auto image = snapshotOf (view);
		EXPECT_GT (lumaVariance (image), minimumVariance) << "empty";
		view.setLookAndFeel (nullptr);
	}

	OneDeckView view (audio, formats, 0);
	view.setLookAndFeel (&look);
	view.setSize (screenWidth, viewHeight);
	std::vector<std::pair<std::string, juce::Image>> shots;
	const auto shoot = [&] (const char* state) { shots.emplace_back (state, snapshotOf (view)); };

	view.setSet (set);
	shoot ("loaded");

	player().setBeatGrid ({ 120.0, 0.0 });   // bar lines every 2 s; the deck stands at 0.5
	view.column (1).oneShots().pad (motionlite::Shape::ping).onClick();
	renderBlock();
	ASSERT_EQ (audio.host().spatialPorts().motion().stemState (1), motionlite::PadState::armed);
	shoot ("stem armed");

	view.shift().setToggleState (true, juce::dontSendNotification);
	view.column (2).oneShots().pad (motionlite::Shape::orbit).onClick();
	renderBlock();
	ASSERT_EQ (audio.host().spatialPorts().motion().stemState (2), motionlite::PadState::running);
	EXPECT_FALSE (view.shift().getToggleState()) << "SHIFT holds for one press";
	shoot ("running");

	view.column (2).cue().setToggleState (true, juce::sendNotificationSync);
	ASSERT_TRUE (player().isStemCued (2));
	shoot ("cued");

	view.column (3).mute().setToggleState (true, juce::sendNotificationSync);
	ASSERT_TRUE (player().isStemMuted (3));
	shoot ("muted");

	view.column (0).aux().setValue (0.6, juce::sendNotificationSync);
	ASSERT_FLOAT_EQ (settings().aux[0].load(), 0.6f);
	shoot ("aux sent");

	for (size_t i = 0; i < shots.size(); ++i)
	{
		const auto& [state, image] = shots[i];
		EXPECT_EQ (image.getWidth(), screenWidth) << state;
		EXPECT_GT (lumaVariance (image), minimumVariance) << state;
		if (i > 0)
			EXPECT_GT (differingPixels (shots[i - 1].second, image), 0) << state << " looks like " << shots[i - 1].first;
	}
	view.setLookAndFeel (nullptr);
}

// For the maintainer's eye: with STEMDECK_SNAPSHOT_DIR set, the deck part at
// the rig's real size as PNGs -- loaded and stopped, then playing with a stem
// cued and sent to AUX, so the meters stand lit, then looping.
TEST_F (OneDeck, WritesTheRigSizedScreenForReview)
{
	const auto directory = juce::SystemStats::getEnvironmentVariable ("STEMDECK_SNAPSHOT_DIR", {});
	if (directory.isEmpty())
		GTEST_SKIP() << "STEMDECK_SNAPSHOT_DIR not set";

	DJLookAndFeel look;
	OneDeckView view (audio, formats, 0);
	view.setLookAndFeel (&look);
	view.setSize (rigViewWidth, rigViewHeight);
	view.setSet (set);
	player().setBeatGrid ({ 120.0, 0.0 });
	const auto write = [&] (const char* name)
	{
		const auto file = juce::File (directory).getChildFile (juce::String (name) + ".png");
		ASSERT_TRUE (file.getParentDirectory().createDirectory().wasOk());
		file.deleteFile();
		juce::FileOutputStream stream (file);
		ASSERT_TRUE (stream.openedOk());
		const auto image = snapshotOf (view);
		EXPECT_GT (lumaVariance (image), 50.0) << name;
		ASSERT_TRUE (juce::PNGImageFormat().writeImageToStream (image, stream));
	};
	const auto playFor = [&] (double seconds)
	{
		for (int i = 0; i < blocksFor (seconds); ++i)
		{
			renderBlock();
			view.refresh();
		}
	};
	player().clearLoop();
	write ("one-deck-stopped");

	setAllStems (false, false, 1.0f);
	view.column (1).cue().setToggleState (true, juce::sendNotificationSync);
	view.column (0).aux().setValue (0.8, juce::sendNotificationSync);
	player().play();
	playFor (0.3);
	write ("one-deck-playing");

	player().setLoop (0.5, 2.5);
	playFor (0.3);
	write ("one-deck-looping");
	view.setLookAndFeel (nullptr);
}

// GAIN is the player's stem gain (its bottom exact silence, R4), AUX, WIDTH
// and the viewer's home the spatial settings -- never the played pose, which
// is the audio thread's -- and CUE and MUTE the player's switches. Only the
// column's own stem.
TEST_F (OneDeck, StemColumnKnobsDriveTheControls)
{
	OneDeckView view (audio, formats, 0);
	view.setSize (screenWidth, viewHeight);
	auto& column = view.column (1);
	const auto& played = audio.host().spatialPorts().deckSpatial()->controls();
	const auto playedWidth = played.width[1].load();

	column.gain().setValue (-6.0, juce::sendNotificationSync);
	EXPECT_FLOAT_EQ (player().getStemGain (1), juce::Decibels::decibelsToGain (-6.0f));
	column.gain().setValue (column.gain().getMinimum(), juce::sendNotificationSync);
	EXPECT_EQ (player().getStemGain (1), 0.0f);
	EXPECT_EQ (column.gain().getMinimum(), (double) stemKnobMinimumDb);

	column.width().setValue (150.0, juce::sendNotificationSync);
	EXPECT_FLOAT_EQ (settings().width[1].load(), 150.0f);
	EXPECT_EQ (played.width[1].load(), playedWidth) << "the played pose is the audio thread's";

	ASSERT_TRUE (column.viewer().onHomeChanged != nullptr);
	column.viewer().onHomeChanged ({ 30.0f, 10.0f, 90.0f });
	EXPECT_FLOAT_EQ (settings().azimuth[1].load(), 30.0f);
	EXPECT_FLOAT_EQ (settings().elevation[1].load(), 10.0f);
	EXPECT_FLOAT_EQ (settings().width[1].load(), 90.0f);
	EXPECT_DOUBLE_EQ (column.width().getValue(), 90.0) << "the WIDTH knob follows the viewer";

	column.cue().setToggleState (true, juce::sendNotificationSync);
	column.mute().setToggleState (true, juce::sendNotificationSync);
	EXPECT_TRUE (player().isStemCued (1));
	EXPECT_TRUE (player().isStemMuted (1));

	for (int other : { 0, 2, 3 })
	{
		EXPECT_FLOAT_EQ (settings().width[(size_t) other].load(), 60.0f);
		EXPECT_FLOAT_EQ (player().getStemGain (other), 1.0f);
		EXPECT_FALSE (player().isStemCued (other));
		EXPECT_FALSE (player().isStemMuted (other));
	}
}

// The deck panel's pitch fader is the player's speed, and a restored session
// plays at its stored tempo (the S4.4a placeholder played at 1) with its
// stems' GAIN, MUTE and CUE; a save gives them back.
TEST_F (OneDeck, PitchDrivesTheSpeedAndTheSessionTempoIsRestored)
{
	OneDeckView view (audio, formats, 0);
	view.setSize (screenWidth, viewHeight);
	view.setSet (set);

	view.deckPanel().moveTempo (16.0);   // an eighth of the travel: +2 % at +-8 %
	EXPECT_NEAR (player().getSpeed(), 1.02, 1e-9);

	DeckSession session;
	session.tempo = 1.05;
	session.tempoRange = 0.08;
	session.stems[2].gainDb = -12.0;
	session.stems[2].muted = true;
	session.stems[3].cue = true;
	view.restoreState (session);
	EXPECT_NEAR (player().getSpeed(), 1.05, 1e-9);
	EXPECT_FLOAT_EQ (player().getStemGain (2), juce::Decibels::decibelsToGain (-12.0f));
	EXPECT_TRUE (player().isStemMuted (2));
	EXPECT_TRUE (player().isStemCued (3));

	DeckSession saved;
	view.saveState (saved);
	EXPECT_NEAR (saved.tempo, 1.05, 1e-9);
	EXPECT_NEAR (saved.stems[2].gainDb, -12.0, 1e-9);
	EXPECT_TRUE (saved.stems[2].muted);
	EXPECT_TRUE (saved.stems[3].cue);
	EXPECT_FALSE (saved.stems[0].cue);
}

// The stem's three knobs sit as on the A³ Motion panel (2026-10-29): AUX
// (the pot) top left, WIDTH (the lower encoder) under it in the same column,
// GAIN (the upper encoder) offset to the right, its centre half-way between
// theirs. The block starts at the top: there is no number and no state line.
TEST_F (OneDeck, StemColumnKnobsSitAsOnTheDevice)
{
	for (const auto& [width, height] : { std::pair { screenWidth, viewHeight }, std::pair { rigViewWidth, rigViewHeight } })
	{
		OneDeckView view (audio, formats, 0);
		view.setSize (width, height);
		for (int stem = 0; stem < stems; ++stem)
		{
			auto& column = view.column (stem);
			const auto gain = column.gain().getBounds(), aux = column.aux().getBounds(), wide = column.width().getBounds();

			EXPECT_EQ (aux.getX(), wide.getX()) << "AUX over WIDTH, one column";
			EXPECT_LT (aux.getBottom(), wide.getY()) << "its caption between";
			EXPECT_GE (gain.getX(), aux.getRight()) << "GAIN to the right";
			EXPECT_NEAR (gain.getCentreY(), (aux.getCentreY() + wide.getCentreY()) / 2, 1) << "half-way between";
			for (const auto& knob : { aux, wide })
				EXPECT_EQ (knob.getWidth(), gain.getWidth()) << "stem " << stem;
			for (const auto& knob : { aux, wide })
				EXPECT_EQ (knob.getHeight(), gain.getHeight()) << "stem " << stem;
			for (const auto& knob : { gain, aux, wide })
				EXPECT_TRUE (column.getLocalBounds().contains (knob)) << "stem " << stem;
			EXPECT_LT (aux.getY(), column.getHeight() / 20) << "the block starts at the top";
			EXPECT_GE (column.viewer().getY(), wide.getBottom()) << "the block does not cover the viewer";
		}
	}
}

// AUX is the stem's send to the deck's AUX bus (0..1), written to the spatial
// settings; the knob follows what the settings hold when nobody touches it.
TEST_F (OneDeck, AuxKnobWritesTheStemsAux)
{
	OneDeckView view (audio, formats, 0);
	view.setSize (screenWidth, viewHeight);
	auto& column = view.column (1);
	EXPECT_EQ (column.aux().getMinimum(), 0.0);
	EXPECT_EQ (column.aux().getMaximum(), 1.0);

	column.aux().setValue (0.4, juce::sendNotificationSync);
	EXPECT_FLOAT_EQ (settings().aux[1].load(), 0.4f);
	EXPECT_EQ (column.auxCaptionText(), "AUX 40") << "the readout in percent";
	for (int other : { 0, 2, 3 })
		EXPECT_EQ (settings().aux[(size_t) other].load(), 0.0f);

	settings().aux[1].store (0.7f);
	column.refresh();
	EXPECT_DOUBLE_EQ (column.aux().getValue(), 0.7);
}

// PING shows as CROSS, one letter longer. The look narrows a label to its pad:
// on the rig (768 x 1024) CROSS keeps the full size, on a3motionV02 (600 x 1024,
// 42 px pads) it is narrowed (11.5 of 14.4 px), but on both screens, at rest
// and running with the longest count, never below ORBIT's face beside it.
TEST_F (OneDeck, CrossFitsItsPadOnBothScreens)
{
	DJLookAndFeel look;
	const auto fontHeight = [] (juce::TextButton& key) { return key.getLookAndFeel().getTextButtonFont (key, key.getHeight()).getHeight(); };
	for (const auto& [width, height] : { std::pair { rigViewWidth, rigViewHeight }, std::pair { 592, 726 } })
	{
		OneDeckView view (audio, formats, 0);
		view.setLookAndFeel (&look);
		view.setSize (width, height);
		const auto size = std::to_string (width) + "x" + std::to_string (height);
		for (int stem = 0; stem < stems; ++stem)
		{
			auto& keys = view.column (stem).oneShots();
			auto& cross = keys.pad (motionlite::Shape::ping);
			auto& orbit = keys.pad (motionlite::Shape::orbit);
			EXPECT_EQ (cross.getButtonText(), juce::String ("CROSS")) << size;
			EXPECT_GE (fontHeight (cross), fontHeight (orbit)) << "at rest, " << size;

			cross.setButtonText (OneShotKeys::shapeName (motionlite::Shape::ping) + " 32/32");
			orbit.setButtonText (OneShotKeys::shapeName (motionlite::Shape::orbit, true) + " 32/32");
			EXPECT_GE (fontHeight (cross), fontHeight (orbit)) << "running, " << size;
		}
		view.setLookAndFeel (nullptr);
	}
}

// The column's only words are its captions and the length (2026-10-29): no
// slot number, no state line ("home", "ORBIT 4 · 2/4", "... · armed"), no
// "· AUX" -- whatever the stem does.
TEST_F (OneDeck, StemColumnShowsNoNumberAndNoStateLine)
{
	OneDeckView view (audio, formats, 0);
	view.setSize (screenWidth, viewHeight);
	view.setSet (set);
	player().setBeatGrid ({ 120.0, 0.0 });
	auto& column = view.column (2);
	const auto words = [&column]
	{
		column.refresh();
		juce::StringArray out;
		for (auto* child : column.getChildren())
			if (auto* label = dynamic_cast<juce::Label*> (child))
				out.add (label->getText());
		out.sort (false);
		return out;
	};
	const auto expected = [] (const char* aux) { auto out = juce::StringArray ({ "GAIN", aux, "WIDTH", "4 BARS" }); out.sort (false); return out; };

	EXPECT_EQ (words(), expected ("AUX 0")) << "at rest";
	column.oneShots().pad (motionlite::Shape::ping).onClick();
	renderBlock();
	ASSERT_EQ (audio.host().spatialPorts().motion().stemState (2), motionlite::PadState::armed);
	EXPECT_EQ (words(), expected ("AUX 0")) << "armed";
	view.shift().setToggleState (true, juce::dontSendNotification);
	column.oneShots().pad (motionlite::Shape::orbit).onClick();
	renderBlock();
	ASSERT_EQ (audio.host().spatialPorts().motion().stemState (2), motionlite::PadState::running);
	column.aux().setValue (0.4, juce::sendNotificationSync);
	EXPECT_EQ (words(), expected ("AUX 40")) << "running and sent";
}

// The MAIN meter is the B-format's W, the AUX meter the aux bus's W and the
// cue meter the cue bus's W: playing lights MAIN, cued stems with no send the
// cue only, a send the AUX.
TEST_F (OneDeck, MetersReadMainAuxAndCue)
{
	using outputs::oneDeck::MeteredBus;
	OneDeckView view (audio, formats, 0);
	view.setSize (rigViewWidth, rigViewHeight);
	setAllStems (false, true, 1.0f);
	const auto settle = [&]
	{
		for (int i = 0; i < blocksFor (0.3); ++i)
		{
			renderBlock();
			view.outputs().refresh();
		}
	};
	settle();
	const auto& main = view.outputs().meter (MeteredBus::main);
	const auto& cue = view.outputs().meter (MeteredBus::cue);
	const auto& aux = view.outputs().meter (MeteredBus::aux);
	EXPECT_LE (main.levelDb(), -60.0f) << "stopped";

	player().play();
	settle();
	EXPECT_GT (main.levelDb(), -60.0f) << "the deck plays on MAIN";
	EXPECT_GT (cue.levelDb(), -60.0f) << "cued stems play in the cue";
	EXPECT_LE (aux.levelDb(), -60.0f) << "nothing is sent yet";

	setAllAux (1.0f);
	settle();
	EXPECT_GT (aux.levelDb(), -60.0f) << "the send lights the aux bus's W";
}

// Each meter takes its bus's W and nothing else: the higher orders' peaks are
// left where the audio thread put them.
TEST_F (OneDeck, MetersTakeOnlyW)
{
	OneDeckView view (audio, formats, 0);
	view.setSize (rigViewWidth, rigViewHeight);
	setAllStems (false, true, 1.0f);
	setAllAux (1.0f);
	player().play();
	renderBlocks (blocksFor (0.3));
	view.outputs().refresh();
	for (const auto bus : { layout.bformat, layout.aux, layout.cue })
	{
		EXPECT_EQ (audio.popPeak (bus), 0.0f) << outputs::oneDeck::portName (bus) << " was read by its meter";
		EXPECT_GT (audio.popPeak (bus + 1), 0.0f) << outputs::oneDeck::portName (bus + 1) << " is no meter's";
	}
}

// The transport in the foot row at the rig's size (2026-10-28): the view's
// children, CUE | PLAY at the left and far bigger than the column gave them,
// the meters' card beside them; every key inside the view, none on another.
TEST_F (OneDeck, TheTransportStandsInTheFootRow)
{
	OneDeckView view (audio, formats, 0);
	view.setSize (rigViewWidth, rigViewHeight);
	auto& panel = view.deckPanel();
	const auto foot = view.getLocalBounds().removeFromBottom (juce::roundToInt (rigViewHeight * 0.15f));

	std::vector<juce::Rectangle<int>> keys;
	for (auto* key : { &panel.previousKey(), &panel.cueKey(), &panel.playKey(), &panel.nextKey() })
	{
		EXPECT_EQ (key->getParentComponent(), &view);
		EXPECT_TRUE (key->isVisible());
		EXPECT_TRUE (foot.contains (key->getBounds())) << key->getButtonText();
		for (const auto& other : keys)
			EXPECT_FALSE (other.intersects (key->getBounds())) << key->getButtonText();
		keys.push_back (key->getBounds());
	}
	EXPECT_LT (panel.cueKey().getRight(), panel.playKey().getX());
	EXPECT_GE (panel.cueKey().getHeight(), 84) << "over 12 mm on the rig";
	EXPECT_GE (panel.previousKey().getHeight(), 40);

	const auto meters = view.outputs().getBounds();
	EXPECT_TRUE (foot.contains (meters));
	EXPECT_LT (panel.playKey().getRight(), meters.getX()) << "the meters right of the transport";
	for (const auto bus : outputs::oneDeck::meteredBuses)
		EXPECT_TRUE (view.outputs().getLocalBounds().contains (view.outputs().meter (bus).getBounds()));

	// The loop block at the left, the track search beside it, then CUE
	// ("den loop block doch links neben CUE", "prev/next buttons links neben
	// cue", 2026-10-28).
	for (auto* key : { &panel.loopKey(), &panel.loopInKey(), &panel.loopOutKey() })
	{
		EXPECT_EQ (key->getParentComponent(), &view);
		EXPECT_TRUE (key->isVisible());
		EXPECT_TRUE (foot.contains (key->getBounds())) << key->getButtonText();
		for (const auto& other : keys)
			EXPECT_FALSE (other.intersects (key->getBounds())) << key->getButtonText();
		keys.push_back (key->getBounds());
		EXPECT_GE (key->getHeight(), 40) << "over 6 mm on the rig";
	}
	EXPECT_EQ (panel.loopKey().getX(), foot.getX() + surface::metrics (rigViewWidth).gap) << "the loop block at the left";
	EXPECT_LT (panel.loopOutKey().getRight(), panel.previousKey().getX()) << "then the track search";
	EXPECT_LT (panel.previousKey().getRight(), panel.cueKey().getX()) << "then CUE";
	EXPECT_LT (panel.loopKey().getBottom(), panel.loopInKey().getY() + 1) << "LOOP on top";
	EXPECT_LT (panel.loopInKey().getRight(), panel.loopOutKey().getX()) << "IN | OUT";
	EXPECT_EQ (panel.loopKey().getButtonText(), "LOOP") << "its state is its light, not its text";

	// LOOP OFF left the deck column; REPEAT stayed in it.
	EXPECT_FALSE (panel.loopOffKey().isVisible());
	EXPECT_TRUE (panel.repeatKey().isVisible());
	EXPECT_EQ (panel.repeatKey().getParentComponent(), &panel);
	EXPECT_TRUE (panel.getLocalBounds().contains (panel.repeatKey().getBounds()));

	// GRID re-lays the column: the transport stays where the view put it.
	const auto cue = panel.cueKey().getBounds();
	panel.resized();
	EXPECT_EQ (panel.cueKey().getBounds(), cue);
}

// A page test must paint: the foot row at the rig's size, stopped and playing
// (PLAY lit, the meters up) -- each paints more than its ground, and PLAY's
// own pixels change.
TEST_F (OneDeck, PaintsTheFootRowInEachState)
{
	DJLookAndFeel look;
	OneDeckView view (audio, formats, 0);
	view.setLookAndFeel (&look);
	view.setSize (rigViewWidth, rigViewHeight);
	view.setSet (set);
	const auto foot = view.getLocalBounds().removeFromBottom (juce::roundToInt (rigViewHeight * 0.15f));
	const auto footOf = [&] { return snapshotOf (view).getClippedImage (foot); };

	setAllStems (false, true, 1.0f);
	const auto stopped = footOf();
	player().play();
	for (int i = 0; i < blocksFor (0.3); ++i)
	{
		renderBlock();
		view.refresh();
	}
	const auto playing = footOf();
	EXPECT_GT (lumaVariance (stopped), 50.0);
	EXPECT_GT (lumaVariance (playing), 50.0);
	EXPECT_GT (differingPixels (stopped, playing), 100) << "PLAY and the meters light";

	const auto playArea = view.deckPanel().playKey().getBounds().translated (-foot.getX(), -foot.getY());
	EXPECT_GT (differingPixels (stopped.getClippedImage (playArea), playing.getClippedImage (playArea)), 10) << "PLAY is lit";

	// Looping: LOOP, IN and OUT light; the in point waiting: IN alone.
	const auto areaOf = [&] (juce::Component& key) { return key.getBounds().translated (-foot.getX(), -foot.getY()); };
	auto& panel = view.deckPanel();
	player().clearLoop();
	const auto unlooped = footOf();
	player().setLoop (0.5, 2.5);
	const auto looping = footOf();
	EXPECT_GT (lumaVariance (looping), 50.0);
	for (auto* key : { &panel.loopKey(), &panel.loopInKey(), &panel.loopOutKey() })
		EXPECT_GT (differingPixels (unlooped.getClippedImage (areaOf (*key)), looping.getClippedImage (areaOf (*key))), 10)
			<< key->getButtonText() << " is lit while looping";

	player().clearLoop();
	panel.loopIn();
	const auto marked = footOf();
	EXPECT_GT (differingPixels (unlooped.getClippedImage (areaOf (panel.loopInKey())), marked.getClippedImage (areaOf (panel.loopInKey()))), 10)
		<< "IN is lit while its point waits for OUT";
	EXPECT_EQ (differingPixels (unlooped.getClippedImage (areaOf (panel.loopKey())), marked.getClippedImage (areaOf (panel.loopKey()))), 0)
		<< "LOOP stays dark";
	view.setLookAndFeel (nullptr);
}

// The loop keys on the screen loop the player itself (CDJ-style, DeckLoop.h).
TEST_F (OneDeck, TheLoopKeysLoopTheDeck)
{
	OneDeckView view (audio, formats, 0);
	view.setSize (rigViewWidth, rigViewHeight);
	auto& panel = view.deckPanel();
	player().clearLoop();
	player().setBeatGrid ({ 120.0, 0.0 });   // a beat every 0.5 s: the loop snaps to it
	player().setPosition (0.52);
	player().play();

	panel.loopInKey().onClick();
	EXPECT_FALSE (player().hasLoop());
	renderBlocks (blocksFor (1.0));
	panel.loopOutKey().onClick();
	ASSERT_TRUE (player().hasLoop());
	EXPECT_NEAR (player().getLoop().getStart(), 0.5, 1e-3) << "on the beat";
	EXPECT_NEAR (player().getLoop().getEnd(), 1.5, 1e-3);
	EXPECT_TRUE (player().isPlaying());

	panel.loopKey().onClick();
	EXPECT_FALSE (player().hasLoop()) << "LOOP exits";
	EXPECT_TRUE (player().isPlaying()) << "and plays on";
	panel.loopKey().onClick();
	ASSERT_TRUE (player().hasLoop()) << "LOOP again goes back in";
	EXPECT_NEAR (player().getLoop().getStart(), 0.5, 1e-3);
	EXPECT_NEAR (player().getPosition(), 0.5, 0.01);

	// OUT is a toggle: out of the loop, playing on; again, the loop from
	// the same IN to the new playhead, on the beat.
	panel.loopOutKey().onClick();
	EXPECT_FALSE (player().hasLoop()) << "OUT exits";
	EXPECT_TRUE (player().isPlaying());
	panel.loopKey().onClick();
	panel.loopKey().onClick();   // RELOOP and EXIT: LOOP keeps its role
	player().setPosition (2.48);
	panel.loopOutKey().onClick();
	ASSERT_TRUE (player().hasLoop()) << "OUT again closes a loop";
	EXPECT_NEAR (player().getLoop().getStart(), 0.5, 1e-3) << "from the same IN";
	EXPECT_NEAR (player().getLoop().getEnd(), 2.5, 1e-3) << "to the new playhead, on the beat";
	EXPECT_NEAR (player().getPosition(), 0.5, 0.01);
}

// The maintainer's case on the screen (2026-10-28): one finger holds CUE --
// which reaches JUCE twice, as touch#0 and X's emulated mouse#0 -- a second
// finger taps PLAY (touch#1), then the first lets go. The deck plays on from
// where it was, without going back to the cue point.
TEST_F (OneDeck, HoldingCueAndTappingPlayOnTheScreenPlaysOn)
{
	if (! barex::hasDisplay())
		GTEST_SKIP() << "no X display";
	barex::internWindowManagerAtoms();

	OneDeckView view (audio, formats, 0);
	view.setSize (rigViewWidth, rigViewHeight);
	view.addToDesktop (0);
	view.setVisible (true);
	auto* peer = view.getPeer();
	ASSERT_NE (peer, nullptr);
	auto& panel = view.deckPanel();

	player().clearLoop();
	player().setCuePoint (1.0);
	player().setPosition (1.0);

	using Source = juce::MouseInputSource::InputSourceType;
	juce::int64 time = 1000;
	const auto send = [&] (Source type, int index, juce::Component& key, bool down)
	{
		const auto where = view.getLocalPoint (&key, key.getLocalBounds().getCentre()).toFloat();
		const auto mods = down ? juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier) : juce::ModifierKeys();
		const auto pressure = type == Source::touch ? 1.0f : juce::MouseInputSource::defaultPressure;
		peer->handleMouseEvent (type, where, mods, pressure, juce::MouseInputSource::defaultOrientation, time += 2, {}, index);
		juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
	};

	send (Source::touch, 0, panel.cueKey(), true);
	send (Source::mouse, 0, panel.cueKey(), true);
	ASSERT_TRUE (player().isPlaying()) << "held on the cue point, CUE previews";
	renderBlocks (blocksFor (0.5));

	send (Source::touch, 1, panel.playKey(), true);
	send (Source::touch, 1, panel.playKey(), false);
	EXPECT_TRUE (player().isPlaying()) << "PLAY while CUE is held plays on";

	send (Source::touch, 0, panel.cueKey(), false);
	send (Source::mouse, 0, panel.cueKey(), false);
	EXPECT_TRUE (player().isPlaying()) << "letting go of CUE keeps it playing";
	EXPECT_GT (player().getPosition(), 1.4) << "no jump back to the cue point";
	EXPECT_DOUBLE_EQ (player().getCuePoint(), 1.0);

	// And without PLAY the preview ends where it began.
	player().pause();
	player().setPosition (1.0);
	send (Source::touch, 0, panel.cueKey(), true);
	send (Source::mouse, 0, panel.cueKey(), true);
	renderBlocks (blocksFor (0.3));
	send (Source::touch, 0, panel.cueKey(), false);
	send (Source::mouse, 0, panel.cueKey(), false);
	EXPECT_FALSE (player().isPlaying());
	EXPECT_NEAR (player().getPosition(), 1.0, 0.01);
	view.removeFromDesktop();
}

//==============================================================================
// The pots are absolute (2026-10-29): a reading is the stem's AUX at once,
// whatever the knob held -- the deck wins, no pick-up, no mark on the knob.
TEST_F (OneDeck, APotSetsItsStemsAuxAtOnce)
{
	OneDeckView view (audio, formats, 0);
	view.setSize (screenWidth, viewHeight);
	AuxPots pots ([&view] (int stem) -> StemColumn& { return view.column (stem); });
	auto& knob = view.column (1).aux();
	knob.setValue (0.5, juce::sendNotificationSync);

	pots.reading (1, 0.2);
	EXPECT_DOUBLE_EQ (knob.getValue(), 0.2) << "no crossing needed";
	EXPECT_FLOAT_EQ (settings().aux[1].load(), 0.2f);
	pots.reading (1, 0.9);
	EXPECT_DOUBLE_EQ (knob.getValue(), 0.9);

	knob.setValue (0.3, juce::sendNotificationSync);   // a touch on the screen
	pots.reading (1, 0.95);
	EXPECT_DOUBLE_EQ (knob.getValue(), 0.95) << "the next reading wins over the touch";
	EXPECT_FLOAT_EQ (settings().aux[1].load(), 0.95f);

	for (int other : { 0, 2, 3 })
		EXPECT_EQ (settings().aux[(size_t) other].load(), 0.0f) << "only the pot's own stem";
	pots.reading (4, 0.5);
	pots.reading (-1, 0.5);
	for (int s = 0; s < stems; ++s)
		EXPECT_EQ (settings().aux[(size_t) s].load(), s == 1 ? 0.95f : 0.0f) << "a pot past the four is nothing";
}

// The panel's lower encoder turns WIDTH as the upper turns GAIN: a detent is
// 1/48 of the knob's travel (7.5 degrees), some two turns end to end.
TEST_F (OneDeck, AnEncoderDetentTurnsWidthByAFortyEighthOfItsTravel)
{
	OneDeckView view (audio, formats, 0);
	view.setSize (screenWidth, viewHeight);
	auto& width = view.column (2).width();
	width.setValue (0.0, juce::sendNotificationSync);
	knobsetup::nudge (width, 4.0);
	EXPECT_NEAR (settings().width[2].load(), 30.0f, 0.5f);
	knobsetup::nudge (width, 48.0);
	EXPECT_FLOAT_EQ (settings().width[2].load(), 360.0f) << "held at the end";
	knobsetup::nudge (width, -48.0);
	EXPECT_FLOAT_EQ (settings().width[2].load(), 0.0f);
}

// CONTINUE beside REPEAT in the deck column, one play mode: each key turns
// the other off, and the player follows. The session keeps it.
TEST_F (OneDeck, ContinueStandsBesideRepeatAndTheyAreExclusive)
{
	OneDeckView view (audio, formats, 0);
	view.setSize (rigViewWidth, rigViewHeight);
	auto& panel = view.deckPanel();
	auto& repeat = panel.repeatKey();
	auto& continuePlay = panel.continueKey();

	EXPECT_TRUE (continuePlay.isVisible());
	EXPECT_EQ (continuePlay.getParentComponent(), &panel);
	EXPECT_EQ (continuePlay.getY(), repeat.getY());
	EXPECT_EQ (continuePlay.getHeight(), repeat.getHeight());
	EXPECT_LT (repeat.getRight(), continuePlay.getX()) << "REPEAT | CONTINUE";
	EXPECT_EQ (continuePlay.getButtonText(), "CONTINUE") << "its state is its light, not its text";

	continuePlay.onClick();   // a tap, as the button runs it
	EXPECT_TRUE (continuePlay.getToggleState());
	EXPECT_TRUE (player().isContinuing());
	EXPECT_EQ (continuePlay.getButtonText(), "CONTINUE");

	repeat.onClick();   // a tap, as the button runs it
	EXPECT_TRUE (repeat.getToggleState());
	EXPECT_FALSE (continuePlay.getToggleState()) << "REPEAT turned CONTINUE off";
	EXPECT_TRUE (player().isRepeating());
	EXPECT_FALSE (player().isContinuing());

	panel.toggleContinue();   // the panel's key
	EXPECT_FALSE (repeat.getToggleState()) << "CONTINUE turned REPEAT off";
	EXPECT_FALSE (player().isRepeating());
	EXPECT_TRUE (player().isContinuing());

	DeckSession saved;
	view.saveState (saved);
	EXPECT_TRUE (saved.continuePlay);
	EXPECT_FALSE (saved.repeat);

	panel.toggleContinue();
	EXPECT_EQ (panel.playMode(), playmode::Mode::single);
	view.restoreState (saved);
	EXPECT_EQ (panel.playMode(), playmode::Mode::continuous);
	EXPECT_TRUE (player().isContinuing());
}

// A page test must paint: CONTINUE's own pixels change when it lights, and
// REPEAT's go dark; for the eye, the column at the rig's size in
// STEMDECK_SNAPSHOT_DIR when set.
TEST_F (OneDeck, PaintsTheContinueKeyLitAndDark)
{
	DJLookAndFeel look;
	OneDeckView view (audio, formats, 0);
	view.setLookAndFeel (&look);
	view.setSize (rigViewWidth, rigViewHeight);
	view.setSet (set);
	player().clearLoop();
	auto& panel = view.deckPanel();
	const auto areaOf = [&] (juce::Component& key) { return view.getLocalArea (&panel, key.getBounds()); };
	const auto keyRow = areaOf (panel.repeatKey()).getUnion (areaOf (panel.continueKey()));

	panel.setPlayMode (playmode::Mode::repeat);
	const auto repeating = snapshotOf (view);
	panel.setPlayMode (playmode::Mode::continuous);
	const auto continuing = snapshotOf (view);
	panel.setPlayMode (playmode::Mode::single);
	const auto single = snapshotOf (view);

	for (const auto* image : { &repeating, &continuing, &single })
		EXPECT_GT (lumaVariance (image->getClippedImage (keyRow)), 50.0);
	const auto continueArea = areaOf (panel.continueKey()), repeatArea = areaOf (panel.repeatKey());
	EXPECT_GT (differingPixels (single.getClippedImage (continueArea), continuing.getClippedImage (continueArea)), 100) << "CONTINUE is lit";
	EXPECT_GT (differingPixels (repeating.getClippedImage (repeatArea), continuing.getClippedImage (repeatArea)), 100) << "REPEAT went dark";
	EXPECT_EQ (differingPixels (single.getClippedImage (repeatArea), continuing.getClippedImage (repeatArea)), 0) << "REPEAT dark as in single";

	const auto directory = juce::SystemStats::getEnvironmentVariable ("STEMDECK_SNAPSHOT_DIR", {});
	if (directory.isNotEmpty())
	{
		const auto write = [&] (const char* name, const juce::Image& image)
		{
			const auto file = juce::File (directory).getChildFile (juce::String (name) + ".png");
			ASSERT_TRUE (file.getParentDirectory().createDirectory().wasOk());
			file.deleteFile();
			juce::FileOutputStream stream (file);
			ASSERT_TRUE (stream.openedOk());
			ASSERT_TRUE (juce::PNGImageFormat().writeImageToStream (image, stream));
		};
		write ("one-deck-continue-on", continuing);
		write ("one-deck-repeat-on", repeating);
		write ("one-deck-play-mode-off", single);
	}
	view.setLookAndFeel (nullptr);
}

//==============================================================================
// The slots are numbered, not named (2026-10-29): "anything can be in a
// track". An empty slot -- a set of fewer than four files -- is drawn dim.
namespace
{
// The mean luma of `area` in `image`.
double meanLuma (const juce::Image& image, juce::Rectangle<int> area)
{
	const juce::Image::BitmapData pixels (image, juce::Image::BitmapData::readOnly);
	area = area.getIntersection (image.getBounds());
	double sum = 0.0;
	for (int y = area.getY(); y < area.getBottom(); ++y)
		for (int x = area.getX(); x < area.getRight(); ++x)
		{
			const auto colour = pixels.getPixelColour (x, y);
			sum += (colour.getRed() + colour.getGreen() + colour.getBlue()) / 3.0;
		}
	return area.isEmpty() ? 0.0 : sum / (double) (area.getWidth() * area.getHeight());
}
}

TEST_F (OneDeck, EmptySlotsArePaintedDim)
{
	StemSet lone;
	lone.name = "lone";
	lone.files[0] = folder.getChildFile ("lone.wav");
	twodeck::writeNoiseStem (lone.files[0], 7);
	lone.stemNames[0] = "lone";

	DJLookAndFeel look;
	OneDeckView view (audio, formats, 0);
	view.setLookAndFeel (&look);
	view.setSize (rigViewWidth, rigViewHeight);
	view.setSet (set);
	const auto full = snapshotOf (view);

	ASSERT_EQ (audio.host().load (lone), juce::String());
	view.setSet (lone);
	const auto shortOfFour = snapshotOf (view);
	EXPECT_EQ (view.column (0).getAlpha(), 1.0f);
	for (int s = 1; s < stems; ++s)
	{
		EXPECT_LT (view.column (s).getAlpha(), 1.0f) << "slot " << s + 1 << " is empty";
		const auto knob = view.getLocalArea (&view.column (s), view.column (s).gain().getBounds());
		EXPECT_LT (meanLuma (shortOfFour, knob), meanLuma (full, knob)) << "slot " << s + 1 << " is drawn dim";
	}
	const auto first = view.getLocalArea (&view.column (0), view.column (0).gain().getBounds());
	EXPECT_EQ (differingPixels (full.getClippedImage (first), shortOfFour.getClippedImage (first)), 0) << "slot 1 as before";

	view.setSet (set);
	for (int s = 0; s < stems; ++s)
		EXPECT_EQ (view.column (s).getAlpha(), 1.0f) << "a full set lights slot " << s + 1 << " again";
	view.setLookAndFeel (nullptr);
}

// For the maintainer's eye: a set of one, two and four files at the rig's
// size in STEMDECK_SNAPSHOT_DIR.
TEST_F (OneDeck, WritesSetsOfOneTwoAndFourFilesForReview)
{
	const auto directory = juce::SystemStats::getEnvironmentVariable ("STEMDECK_SNAPSHOT_DIR", {});
	if (directory.isEmpty())
		GTEST_SKIP() << "STEMDECK_SNAPSHOT_DIR not set";

	StemSet one, two;
	one.name = "Lone Stereo Track";
	one.files[0] = folder.getChildFile ("Lone Stereo Track.wav");
	twodeck::writeNoiseStem (one.files[0], 3);
	two.name = "Two Files";
	two.files[0] = folder.getChildFile ("Two Files - 1 - drums.wav");
	two.files[1] = folder.getChildFile ("Two Files - 2 - rest.wav");
	twodeck::writeNoiseStem (two.files[0], 4);
	twodeck::writeNoiseStem (two.files[1], 5);

	DJLookAndFeel look;
	OneDeckView view (audio, formats, 0);
	view.setLookAndFeel (&look);
	view.setSize (rigViewWidth, rigViewHeight);
	for (const auto& [shown, file] : { std::pair { &one, "slots-1-file" }, std::pair { &two, "slots-2-files" }, std::pair { &set, "slots-4-files" } })
	{
		ASSERT_EQ (audio.host().load (*shown), juce::String());
		view.setSet (*shown);
		player().setBeatGrid ({ 120.0, 0.0 });
		const auto out = juce::File (directory).getChildFile (juce::String (file) + ".png");
		ASSERT_TRUE (out.getParentDirectory().createDirectory().wasOk());
		out.deleteFile();
		juce::FileOutputStream stream (out);
		ASSERT_TRUE (stream.openedOk());
		ASSERT_TRUE (juce::PNGImageFormat().writeImageToStream (snapshotOf (view), stream));
	}
	view.setLookAndFeel (nullptr);
}
