// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>

#include "DeckHost.h"
#include "Outputs.h"
#include "PlayMode.h"
#include "StemEventsFile.h"
#include "StemDeckPlayer.h"

#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

// CONTINUE in the player (PlayMode.h): the next set, staged in memory beside
// the playing one, takes over at the end frame, on the audio thread. Two
// short FLAC sets, decoded as the library's are; the gap at the change is
// measured in frames, with key lock off and on.
namespace
{
constexpr double rate = 44100.0;
constexpr int block = 256;
constexpr double pi = 3.14159265358979323846;
constexpr double setSeconds = 1.5;
// A tone that never lingers at zero: at 441 Hz, 100 samples a period, no two
// samples in a row are below this.
constexpr float silent = 1.0e-4f;

void writeToneStem (const juce::File& file, double hz, float level, double seconds)
{
	const auto length = (int) std::lround (seconds * rate);
	juce::AudioBuffer<float> audio (2, length);
	for (int ch = 0; ch < 2; ++ch)
		for (int i = 0; i < length; ++i)
			audio.setSample (ch, i, level * (float) std::sin (2.0 * pi * hz * (i + 0.25) / rate));

	juce::FlacAudioFormat flac;
	std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
	auto writer = stream == nullptr ? nullptr
	                                : flac.createWriterFor (stream, juce::AudioFormatWriterOptions{}
	                                                                    .withSampleRate (rate)
	                                                                    .withNumChannels (2)
	                                                                    .withBitsPerSample (24));
	if (writer == nullptr || ! writer->writeFromAudioSampleBuffer (audio, 0, length))
		throw std::runtime_error ("cannot write " + file.getFullPathName().toStdString());
}

bool waitFor (const std::function<bool()>& condition, double seconds = 10.0)
{
	const auto until = std::chrono::steady_clock::now() + std::chrono::duration<double> (seconds);
	while (! condition())
	{
		if (std::chrono::steady_clock::now() > until)
			return false;
		std::this_thread::sleep_for (std::chrono::milliseconds (2));
	}
	return true;
}

// The longest run of silent frames in `x` within [from, to).
int longestSilence (const std::vector<float>& x, size_t from, size_t to)
{
	int longest = 0, run = 0;
	for (auto i = from; i < juce::jmin (to, x.size()); ++i)
	{
		run = std::abs (x[i]) < silent ? run + 1 : 0;
		longest = juce::jmax (longest, run);
	}
	return longest;
}

class PlayerContinue : public ::testing::Test
{
protected:
	void SetUp() override
	{
		ASSERT_TRUE (folder.createDirectory().wasOk());
		formats.registerBasicFormats();
		first = makeSet ("first", 441.0, 0.3f);
		second = makeSet ("second", 661.5, 0.2f);
		player.prepareToPlay (block, rate);
	}

	void TearDown() override { folder.deleteRecursively(); }

	StemSet makeSet (const juce::String& name, double hz, float level, double seconds = setSeconds)
	{
		StemSet set;
		set.name = name;
		for (int s = 0; s < StemSet::numStems; ++s)
		{
			set.files[(size_t) s] = folder.getChildFile (name + "-0" + juce::String (s + 1) + ".flac");
			set.stemNames[(size_t) s] = "stem" + juce::String (s + 1);
			writeToneStem (set.files[(size_t) s], hz, level, seconds);
		}
		set.lengthSeconds = seconds;
		return set;
	}

	void loadReady (const StemSet& set)
	{
		ASSERT_EQ (player.load (set), juce::String());
		ASSERT_TRUE (waitFor ([this] { return player.isReady(); }));
	}

	void stageReady (const StemSet& set)
	{
		ASSERT_EQ (player.stageNext (set), juce::String());
		ASSERT_TRUE (waitFor ([this] { return player.isStagedReady(); }));
	}

	// Plays `seconds`, stem 0's left side appended to `heard`.
	void play (double seconds)
	{
		juce::AudioBuffer<float> out (StemDeckPlayer::numOutputChannels, block);
		for (int i = 0; i < (int) std::ceil (seconds * rate / block); ++i)
		{
			out.clear();
			player.getNextAudioBlock (juce::AudioSourceChannelInfo (&out, 0, block));
			const auto* left = out.getReadPointer (0);
			heard.insert (heard.end(), left, left + block);
		}
	}

	// The gap at the change: the longest silence around the first set's end.
	int gapAtTheChange() const
	{
		const auto end = (size_t) std::lround (setSeconds * rate);
		return longestSilence (heard, end - 4096, end + 4096);
	}

	juce::File folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("stemdeck-continue", "");
	juce::AudioFormatManager formats;
	StemDeckPlayer player { formats };
	StemSet first, second;
	std::vector<float> heard;
};
}

// Key lock off: the staged set's first frame follows the playing set's last.
TEST_F (PlayerContinue, HandsOverAtTheEndFrameWithoutAGap)
{
	loadReady (first);
	player.setContinue (true);
	stageReady (second);
	player.play();
	play (setSeconds + 0.5);

	EXPECT_EQ (player.handovers(), 1);
	EXPECT_TRUE (player.isPlaying());
	EXPECT_NEAR (player.getLength(), setSeconds, 1.0e-3);
	EXPECT_NEAR (player.getPosition(), 0.5, 0.05) << "the second set, from its start";
	EXPECT_FALSE (player.hasStaged());

	const auto gap = gapAtTheChange();
	std::cout << "[ measured ] gap at the change, key lock off: " << gap << " frames" << std::endl;
	RecordProperty ("gapFramesKeyLockOff", gap);
	EXPECT_EQ (gap, 0);
	EXPECT_EQ (longestSilence (heard, 0, heard.size()), gap) << "silent nowhere else either";

	// After the change the second set is heard: its level, not the first's.
	const auto after = (size_t) std::lround ((setSeconds + 0.2) * rate);
	float peak = 0.0f;
	for (auto i = after; i < after + 2048; ++i)
		peak = juce::jmax (peak, std::abs (heard[i]));
	EXPECT_NEAR (peak, 0.2f, 0.01f);
}

TEST_F (PlayerContinue, HandsOverUnderKeyLockWithAMeasuredGap)
{
	loadReady (first);
	player.setKeyLock (true);
	player.setContinue (true);
	stageReady (second);
	player.play();
	play (setSeconds + 0.5);

	EXPECT_EQ (player.handovers(), 1);
	EXPECT_TRUE (player.isPlaying());
	EXPECT_NEAR (player.getPosition(), 0.5, 0.1);

	const auto gap = gapAtTheChange();
	std::cout << "[ measured ] gap at the change, key lock on: " << gap << " frames" << std::endl;
	RecordProperty ("gapFramesKeyLockOn", gap);
	EXPECT_LE (gap, block) << "at most the block the stretchers ran out in";
}

// The old set's memory goes once the new one plays, freed off the audio thread.
TEST_F (PlayerContinue, TheOldSetIsFreedAfterTheChange)
{
	loadReady (first);
	const auto oneSet = player.decodedBytes();
	ASSERT_GT (oneSet, 0);
	player.setContinue (true);
	stageReady (second);
	EXPECT_EQ (player.decodedBytes(), 2 * oneSet) << "both, while staged";

	player.play();
	play (setSeconds + 0.1);
	ASSERT_EQ (player.handovers(), 1);
	EXPECT_EQ (player.decodedBytes(), 2 * oneSet) << "the audio thread frees nothing";
	player.releaseRetired();
	EXPECT_EQ (player.decodedBytes(), oneSet);
}

TEST_F (PlayerContinue, WithoutContinueTheStagedSetWaitsAndTheDeckStops)
{
	loadReady (first);
	stageReady (second);
	player.play();
	play (setSeconds + 0.3);

	EXPECT_EQ (player.handovers(), 0);
	EXPECT_FALSE (player.isPlaying());
	EXPECT_FALSE (player.takeRanOut()) << "not in CONTINUE: an ordinary end";
}

// Nothing staged (the list's last set, or a jump into the last moment): the
// deck stops and says it ran out, so its owner can load the next by hand.
TEST_F (PlayerContinue, WithNothingStagedTheDeckStopsAndSaysSo)
{
	loadReady (first);
	player.setContinue (true);
	player.play();
	play (setSeconds + 0.3);

	EXPECT_EQ (player.handovers(), 0);
	EXPECT_FALSE (player.isPlaying());
	EXPECT_TRUE (player.takeRanOut());
	EXPECT_FALSE (player.takeRanOut()) << "said once";
}

TEST_F (PlayerContinue, ALoopHolds)
{
	loadReady (first);
	player.setContinue (true);
	stageReady (second);
	player.setLoop (1.0, 1.4);
	player.play();
	play (3.0);

	EXPECT_EQ (player.handovers(), 0);
	EXPECT_TRUE (player.isPlaying());
	EXPECT_NEAR (player.getLength(), setSeconds, 1.0e-3);
}

TEST_F (PlayerContinue, RepeatWinsOverAStagedSet)
{
	loadReady (first);
	player.setRepeat (true);
	stageReady (second);
	player.play();
	play (setSeconds + 0.3);
	EXPECT_EQ (player.handovers(), 0);
	EXPECT_TRUE (player.isPlaying());
}

// A load by hand drops what was staged: the next set is the one after it.
TEST_F (PlayerContinue, ALoadDropsTheStagedSet)
{
	loadReady (first);
	stageReady (second);
	loadReady (first);
	EXPECT_FALSE (player.hasStaged());
}

// Preload timing: a set of four FLAC stems is in memory far within the lead.
TEST_F (PlayerContinue, StagingDecodesWellWithinTheLead)
{
	const auto longer = makeSet ("longer", 441.0, 0.3f, 30.0);
	loadReady (first);
	const auto started = std::chrono::steady_clock::now();
	ASSERT_EQ (player.stageNext (longer), juce::String());
	ASSERT_TRUE (waitFor ([this] { return player.isStagedReady(); }));
	const std::chrono::duration<double> took = std::chrono::steady_clock::now() - started;
	std::cout << "[ measured ] staging 4 x 30 s FLAC: " << took.count() << " s" << std::endl;
	EXPECT_LT (took.count(), playmode::stageLeadSeconds / 2);
}

//==============================================================================
// The deck: DeckHost stages the set and takes it on as loaded once the
// player has handed over -- its id, its grid, a new generation.
TEST_F (PlayerContinue, TheHostTakesTheHandedOverSetOn)
{
	juce::ThreadPool pool;
	AnalysisCache cache { folder.getChildFile ("analysis.xml") };
	cache.store (second, { 125.0, 0.1 });
	DeckHost host { formats, pool, cache };
	auto& deck = host.player();
	deck.prepareToPlay (block, rate);
	ASSERT_EQ (host.load (first), juce::String());
	ASSERT_TRUE (waitFor ([&deck] { return deck.isReady(); }));
	const auto generation = host.loadGeneration();

	deck.setContinue (true);
	ASSERT_EQ (host.stageNext (second), juce::String());
	EXPECT_EQ (host.stagedSetId(), second.files[0].getFullPathName());
	ASSERT_TRUE (waitFor ([&deck] { return deck.isStagedReady(); }));
	EXPECT_FALSE (host.followHandover()) << "nothing handed over yet";

	deck.play();
	juce::AudioBuffer<float> out (StemDeckPlayer::numOutputChannels, block);
	for (int i = 0; i < (int) ((setSeconds + 0.2) * rate / block); ++i)
		deck.getNextAudioBlock (juce::AudioSourceChannelInfo (&out, 0, block));
	EXPECT_EQ (deck.getBeatGrid().bpm, 125.0) << "the grid came with the set, at the change";

	EXPECT_TRUE (host.followHandover());
	EXPECT_FALSE (host.followHandover()) << "once";
	EXPECT_EQ (host.loadedSetId(), second.files[0].getFullPathName());
	EXPECT_EQ (host.loadGeneration(), generation + 1);
	EXPECT_TRUE (host.stagedSetId().isEmpty());
	pool.removeAllJobs (true, 5000);
}

// The change moves the playhead from the old set's end to the new one's start:
// a jump, so the event scheduler takes no crossing from it.
TEST_F (PlayerContinue, TheChangeCountsAsASeek)
{
	loadReady (first);
	player.setContinue (true);
	stageReady (second);
	player.play();
	play (setSeconds - 0.2);
	ASSERT_EQ (player.handovers(), 0);
	const auto before = player.seeks();
	play (0.4);
	ASSERT_EQ (player.handovers(), 1);
	EXPECT_GT (player.seeks(), before);
}

//==============================================================================
// The track's events follow the set the deck holds: loaded with it, and after
// a CONTINUE change the new set's, with the motion's gate opened for it.
namespace
{
stemevents::Event eventAt (int stem, double beat, motionlite::Shape shape)
{
	stemevents::Event e;
	e.stem = stem;
	e.beat = beat;
	e.shape = shape;
	return e;
}

void saveEvents (const StemSet& set, std::initializer_list<stemevents::Event> events)
{
	stemevents::EventList list;
	for (const auto& e : events)
		list.add (e);
	ASSERT_TRUE (stemevents::save (stemevents::fileFor (set.firstFile()), list));
}
}

TEST_F (PlayerContinue, TheHostLoadsTheHandedOverSetsEvents)
{
	juce::ScopedJuceInitialiser_GUI juceInitialiser;
	juce::ThreadPool pool;
	AnalysisCache cache { folder.getChildFile ("analysis.xml") };
	cache.store (first, { 120.0, 0.0 });
	cache.store (second, { 120.0, 0.0 });
	saveEvents (first, { eventAt (1, 1.0, motionlite::Shape::ping) });
	saveEvents (second, { eventAt (3, 1.0, motionlite::Shape::rise) });

	DeckHost host { formats, pool, cache };
	auto& deck = host.player();
	deck.prepareToPlay (block, rate);
	ASSERT_EQ (host.load (first), juce::String());
	ASSERT_EQ (host.trackEvents().list().events().size(), 1u);
	EXPECT_EQ (host.trackEvents().list().events()[0].stem, 1) << "the loaded set's events";

	ASSERT_TRUE (waitFor ([&deck] { return deck.isReady(); }));
	deck.setContinue (true);
	ASSERT_EQ (host.stageNext (second), juce::String());
	ASSERT_TRUE (waitFor ([&deck] { return deck.isStagedReady(); }));
	deck.play();
	juce::AudioBuffer<float> out (StemDeckPlayer::numOutputChannels, block);
	for (int i = 0; i < (int) ((setSeconds + 0.2) * rate / block); ++i)
		deck.getNextAudioBlock (juce::AudioSourceChannelInfo (&out, 0, block));
	ASSERT_EQ (deck.handovers(), 1);

	ASSERT_TRUE (host.followHandover());
	ASSERT_EQ (host.trackEvents().list().events().size(), 1u);
	EXPECT_EQ (host.trackEvents().list().events()[0].stem, 3) << "the handed-over set's events";

	// The gate is open for track 1, with the new set's events behind it.
	auto& motion = host.spatialPorts().motion();
	DeckSpatial::Controls settings, played;
	motionlite::Clock clock;
	clock.track = 1;
	clock.beat = 0.5;
	motion.advance (clock, 0.0, settings, played);
	clock.beat = 1.5;
	motion.advance (clock, 0.25, settings, played);
	EXPECT_EQ (motion.padState (3, motionlite::Shape::rise), motionlite::PadState::running);
	EXPECT_EQ (motion.stemState (1), motionlite::PadState::idle);
	pool.removeAllJobs (true, 5000);
}

// 3D REC: a press is also an event of the track, where the live press starts.
TEST_F (PlayerContinue, TheHostRecordsAPressIntoTheTracksEvents)
{
	juce::ScopedJuceInitialiser_GUI juceInitialiser;
	juce::ThreadPool pool;
	AnalysisCache cache { folder.getChildFile ("analysis.xml") };
	cache.store (first, { 120.0, 0.0 });
	DeckHost host { formats, pool, cache };
	host.player().prepareToPlay (block, rate);
	ASSERT_EQ (host.load (first), juce::String());

	EXPECT_EQ (host.recordPress (0, motionlite::Shape::scat, false, 42), TrackEvents::Record::notRecording);
	EXPECT_FALSE (host.placesPresses());
	host.setRecording (true);
	ASSERT_TRUE (host.isRecording());
	EXPECT_TRUE (host.placesPresses()) << "recording, standing: a press only places";
	ASSERT_EQ (host.recordPress (0, motionlite::Shape::scat, false, 42), TrackEvents::Record::added);
	ASSERT_EQ (host.trackEvents().list().events().size(), 1u);
	const auto& recorded = host.trackEvents().list().events()[0];
	EXPECT_DOUBLE_EQ (recorded.beat, 0.0) << "standing: at the playhead (beat 0), not the next bar";
	EXPECT_EQ (recorded.seed, 42u);
	EXPECT_EQ (host.recordPress (0, motionlite::Shape::orbit, false, 7), TrackEvents::Record::occupied);
	EXPECT_FALSE (host.eventsNote().has_value());

	host.player().play();
	EXPECT_FALSE (host.placesPresses()) << "playing: the press plays live and is recorded";
	ASSERT_EQ (host.recordPress (1, motionlite::Shape::ping, false, 9), TrackEvents::Record::added);
	EXPECT_DOUBLE_EQ (host.trackEvents().list().events().back().beat, 4.0) << "playing near beat 0: the next bar, as before";
	host.player().pause();
	pool.removeAllJobs (true, 5000);
}

TEST_F (PlayerContinue, AHostWithoutAGridDoesNotRecordAndSaysSo)
{
	juce::ScopedJuceInitialiser_GUI juceInitialiser;
	juce::ThreadPool pool;
	AnalysisCache cache { folder.getChildFile ("analysis.xml") };
	DeckHost host { formats, pool, cache };
	host.player().prepareToPlay (block, rate);
	pool.removeAllJobs (true, 5000);
	ASSERT_EQ (host.load (first), juce::String());
	pool.removeAllJobs (true, 5000);   // the analysis would find a grid
	ASSERT_FALSE (host.player().getBeatGrid().isValid());

	host.setRecording (true);
	EXPECT_FALSE (host.isRecording()) << "3D REC stays off without a grid";
	EXPECT_EQ (host.recordPress (0, motionlite::Shape::orbit, true, 1), TrackEvents::Record::notRecording);
	ASSERT_TRUE (host.eventsNote().has_value());
	EXPECT_EQ (*host.eventsNote(), "no grid");
}

//==============================================================================
// Events at the very start of a track, through the real host: the player, its
// spatial ports, the motion and the events file.
namespace
{
// The host's audio block, as the audio callback runs it.
void renderHost (DeckHost& host, double seconds)
{
	juce::AudioBuffer<float> spatial (outputs::channelCount(), block);
	for (int i = 0; i < (int) std::ceil (seconds * rate / block); ++i)
		host.render (spatial, outputs::spatialFirstChannel (0), block);
}
}

// Auto DJ starts a deck at its first beat: a jump and PLAY in one go.
TEST_F (PlayerContinue, AJumpAndPlayOntoTheFirstBeatFiresItsEvent)
{
	juce::ScopedJuceInitialiser_GUI juceInitialiser;
	juce::ThreadPool pool;
	AnalysisCache cache { folder.getChildFile ("analysis.xml") };
	constexpr double firstBeat = 0.3;
	cache.store (first, { 120.0, firstBeat });
	saveEvents (first, { eventAt (0, 0.0, motionlite::Shape::orbit) });

	DeckHost host { formats, pool, cache };
	ASSERT_TRUE (host.createSpatial().isEmpty());
	ASSERT_TRUE (host.prepare (rate, block));
	ASSERT_EQ (host.load (first), juce::String());
	auto& deck = host.player();
	ASSERT_TRUE (waitFor ([&deck] { return deck.isReady(); }));
	deck.setPosition (1.0);
	renderHost (host, 0.05);   // standing, behind the event

	deck.setPosition (firstBeat);
	deck.play();
	renderHost (host, 0.05);
	EXPECT_EQ (host.spatialPorts().motion().padState (0, motionlite::Shape::orbit), motionlite::PadState::running);
	pool.removeAllJobs (true, 5000);
}
