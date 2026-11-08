// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>

#include "../SyntheticStems.h"
#include "DeckSpatialPorts.h"
#include "StemDeckPlayer.h"

#include <algorithm>
#include <iterator>
#include <array>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <vector>

// A loop played as a CDJ plays it: the seam is a cut at the sample -- the
// loop's start heard exactly from its first frame, nothing from before it,
// at most a micro-fade of what is cut off behind the end -- with no click and
// no dip, and the playhead never runs past the loop's end, however many
// passes and whatever tempo, with key lock on or off.
namespace
{
constexpr double rate = 44100.0;
constexpr int block = 256;
// Four tones, each on a bin of Rubber Band's 2048-point analysis (a tone
// between two bins comes out of a phase vocoder beating by about 2 dB, seam
// or not), so a window of 4096 frames holds whole periods of every one and
// the mix reads a steady RMS -- also away from rate 1, where the stretcher
// swings a steady tone by 3 dB every 2048 frames. Across a seam the four
// meet in different phases, as music does: one of them upside down.
constexpr double binHz = 44100.0 / 2048.0;
constexpr std::array<int, 4> toneBins { 20, 27, 41, 53 };
constexpr float amplitude = 0.15f;   // each
constexpr int window = 4096, hop = 256;
constexpr double trackSeconds = 40.0;
constexpr double pi = 3.14159265358979323846;

// The four tones, mixed, at 44.1 kHz.
std::vector<float> toneMix()
{
	std::vector<float> x ((size_t) (trackSeconds * rate));
	for (size_t i = 0; i < x.size(); ++i)
		for (const auto bin : toneBins)
			x[i] += amplitude * (float) std::sin (2.0 * pi * binHz * bin * (double) i / rate);
	return x;
}

void writeStem (juce::AudioFormat& format, const juce::File& file, const std::vector<float>& samples, double fileRate, int bits)
{
	const auto length = (int) samples.size();
	juce::AudioBuffer<float> audio (2, length);
	for (int ch = 0; ch < 2; ++ch)
		audio.copyFrom (ch, 0, samples.data(), length);

	std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
	auto writer = stream == nullptr ? nullptr
	                                : format.createWriterFor (stream, juce::AudioFormatWriterOptions{}
	                                                                      .withSampleRate (fileRate)
	                                                                      .withNumChannels (2)
	                                                                      .withBitsPerSample (bits));
	if (writer == nullptr || ! writer->writeFromAudioSampleBuffer (audio, 0, length))
		throw std::runtime_error ("cannot write " + file.getFullPathName().toStdString());
}

struct Render
{
	std::vector<float> left;      // stem 0, left
	int wraps = 0;
	double furthest = 0.0;        // the playhead's furthest point
	double earliest = 1.0e9;      // and its earliest
};

struct Case
{
	double loopSeconds;
	double speed;
	bool keyLock;
};

std::string name (const Case& c)
{
	return "loop " + std::to_string (c.loopSeconds) + " s, speed " + std::to_string (c.speed) + (c.keyLock ? ", key lock" : "");
}

void PrintTo (const Case& c, std::ostream* os) { *os << name (c); }

class LoopFixture : public ::testing::Test
{
protected:
	void SetUp() override
	{
		ASSERT_TRUE (folder.createDirectory().wasOk());
		formats.registerBasicFormats();
	}

	void TearDown() override { folder.deleteRecursively(); }

	// WAV is memory-mapped by the player; FLAC, the library's format, is not.
	enum class Format { wav, flac };

	// The stems as files named `name`-N in the test's folder.
	StemSet write (const std::array<std::vector<float>, StemSet::numStems>& stems, double fileRate, Format format,
				   const juce::String& name = "stem")
	{
		juce::WavAudioFormat wav;
		juce::FlacAudioFormat flac;
		auto& writerFormat = format == Format::wav ? static_cast<juce::AudioFormat&> (wav) : flac;
		const auto extension = format == Format::wav ? ".wav" : ".flac";
		StemSet written;
		written.name = name;
		for (int s = 0; s < StemSet::numStems; ++s)
		{
			written.files[(size_t) s] = folder.getChildFile (name + juce::String (s) + extension);
			written.stemNames[(size_t) s] = "stem" + juce::String (s);
			writeStem (writerFormat, written.files[(size_t) s], stems[(size_t) s], fileRate, format == Format::wav ? 32 : 24);
		}
		return written;
	}

	// As a deck stands loaded before PLAY: decoded, unless told not to wait.
	void load (const std::array<std::vector<float>, StemSet::numStems>& stems, double fileRate, Format format = Format::wav,
			   bool waitForDecoding = true)
	{
		set = write (stems, fileRate, format);
		ASSERT_EQ (player.load (set), juce::String());
		player.prepareToPlay (block, rate);
		if (waitForDecoding)
			ASSERT_TRUE (waitUntilReady());
	}

	bool waitUntilReady()
	{
		for (int waited = 0; waited < 20000 && ! player.isReady(); waited += 5)
			juce::Thread::sleep (5);
		return player.isReady();
	}

	// `passes` passes of the loop, played from its start.
	Render play (double loopStart, double loopEnd, int passes)
	{
		Render r;
		juce::AudioBuffer<float> buffer (StemDeckPlayer::numOutputChannels, block);
		const auto seconds = (loopEnd - loopStart) * passes / player.getSpeed() + 0.5;
		const auto blocks = (int) (seconds * rate / block);
		auto last = player.getPosition();

		for (int b = 0; b < blocks; ++b)
		{
			buffer.clear();
			player.getNextAudioBlock ({ &buffer, 0, block });
			r.left.insert (r.left.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + block);

			const auto now = player.getPosition();
			if (now < last - 0.5 * (loopEnd - loopStart))
				++r.wraps;
			r.furthest = std::max (r.furthest, now);
			r.earliest = std::min (r.earliest, now);
			last = now;
		}
		return r;
	}

	juce::File folder { juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("stemdeck-loop", "") };
	juce::AudioFormatManager formats;
	StemSet set;
	StemDeckPlayer player { formats };
};

// Tones on all four stems, as a track has music on all four.
class PlayerLoop : public LoopFixture, public ::testing::WithParamInterface<Case>
{
protected:
	void SetUp() override
	{
		LoopFixture::SetUp();
		const auto tones = toneMix();
		load ({ tones, tones, tones, tones }, rate);
	}
};

double steepestStep (const std::vector<float>& x, size_t from)
{
	double steepest = 0.0;
	for (size_t i = std::max<size_t> (from, 1); i < x.size(); ++i)
		steepest = std::max (steepest, (double) std::abs (x[i] - x[i - 1]));
	return steepest;
}

double rmsDb (const std::vector<float>& x, size_t from)
{
	double sum = 0.0;
	for (size_t i = from; i < from + window; ++i)
		sum += (double) x[i] * x[i];
	return 10.0 * std::log10 (sum / window + 1e-30);
}
}

TEST_P (PlayerLoop, NoDipAndNeverPastItsEnd)
{
	const auto c = GetParam();
	player.setSpeed (c.speed);
	player.setKeyLock (c.keyLock);

	// Half a period off the lowest tone's: the seam joins it to itself upside
	// down, the hardest case for a cut and for the level.
	const auto period = rate / (binHz * toneBins[0]);
	auto loopSamples = std::round (c.loopSeconds * rate);
	loopSamples += std::round (std::fmod (1.5 * period - std::fmod (loopSamples, period), period));
	const auto start = 3.0 + 13.0 / rate;
	const auto end = start + loopSamples / rate;
	player.setLoop (start, end);
	player.play();
	constexpr int passes = 24;
	const auto r = play (start, end, passes);

	EXPECT_GE (r.wraps, passes - 1) << name (c);
	EXPECT_LE (r.furthest, end + 1.0e-6) << name (c) << ": the playhead ran past the loop's end";
	EXPECT_GE (r.earliest, start - 0.01) << name (c);
	EXPECT_TRUE (player.hasLoop());

	// From the second pass on: every window of the output (seams included)
	// within 0.5 dB of the body's level -- the median window. Away from rate
	// 1 the stretcher holds a steady tone about 0.6 dB under the file's own,
	// so no window may rise above the louder of the two.
	const auto settled = (size_t) (0.3 * rate);
	std::vector<double> levels;
	for (size_t i = settled; i + window < r.left.size(); i += hop)
		levels.push_back (rmsDb (r.left, i));
	ASSERT_FALSE (levels.empty());
	auto sorted = levels;
	std::nth_element (sorted.begin(), sorted.begin() + (long) sorted.size() / 2, sorted.end());
	const auto bodyDb = sorted[sorted.size() / 2];
	const auto [lowest, highest] = std::minmax_element (levels.begin(), levels.end());
	const auto toneDb = 10.0 * std::log10 ((double) toneBins.size() * amplitude * amplitude / 2.0);
	// With key lock away from rate 1 the stretcher's window straddles the
	// seam, where the four tones jump in phase, and its frames there cancel
	// a little: up to 1 dB over a 93 ms window on these steady tones, met
	// upside down (any fade at the seam only loses more). Music snapped to
	// the grid meets the seam on a beat, where Rubber Band resets its phases
	// anyway. Everywhere else: 0.5 dB.
	const auto allowedDip = c.keyLock && std::abs (c.speed - 1.0) > 1e-9 ? 1.0 : 0.5;
	EXPECT_GT (*lowest, bodyDb - allowedDip) << name (c) << ": a dip at the seam";
	EXPECT_LT (*highest, std::max (bodyDb, toneDb) + 0.5) << name (c) << ": a bump at the seam";
	EXPECT_NEAR (bodyDb, toneDb, 1.0) << name (c) << ": the tone's own level";
}

INSTANTIATE_TEST_SUITE_P (LengthsTemposAndKeyLock, PlayerLoop,
	::testing::Values (Case { 0.25, 1.0, false }, Case { 0.5, 1.0, false }, Case { 1.0, 1.0, false },
					   Case { 0.5, 0.94, false }, Case { 0.5, 1.08, false },
					   Case { 0.25, 1.0, true }, Case { 0.5, 1.0, true }, Case { 1.0, 1.0, true },
					   Case { 0.5, 0.94, true }, Case { 0.5, 1.08, true }));

namespace
{
class PlayerLoopSeek : public PlayerLoop {};
}

// A jump from elsewhere while looping -- the sync's phase jump, a jog search
// -- lands in the loop, never behind its end: the loop the screen shows is
// the loop that plays.
TEST_P (PlayerLoopSeek, AJumpPastTheEndStaysInTheLoop)
{
	const auto c = GetParam();
	player.setSpeed (c.speed);
	player.setKeyLock (c.keyLock);
	const auto start = 3.0, end = start + c.loopSeconds;
	player.setLoop (start, end);
	player.play();
	play (start, end, 2);

	player.setPosition (end + 0.3 * c.loopSeconds);
	EXPECT_GE (player.getPosition(), start);
	EXPECT_LT (player.getPosition(), end);

	const auto r = play (start, end, 6);
	EXPECT_LE (r.furthest, end + 1.0e-6) << name (c);
	EXPECT_GE (r.wraps, 4) << name (c);
}

INSTANTIATE_TEST_SUITE_P (KeyLockOnAndOff, PlayerLoopSeek,
	::testing::Values (Case { 0.5, 1.0, false }, Case { 0.5, 1.03, true }));

// The deck's clock as the event scheduler reads it (EventScheduler.h): it
// relies on seeks and wraps that only count up, a beat that never passes the
// loop's out without a wrap counted in the same block, a seek counted by the
// time its position can be read, and no loop in beats without a loop and a grid.
namespace
{
class PlayerClock : public PlayerLoop
{
protected:
	static constexpr double loopIn = 3.0;   // beat 6 at 120 BPM from 0

	void SetUp() override
	{
		PlayerLoop::SetUp();
		player.setBeatGrid ({ 120.0, 0.0 });
	}

	void renderBlocks (int blocks)
	{
		juce::AudioBuffer<float> buffer (StemDeckPlayer::numOutputChannels, block);
		for (int b = 0; b < blocks; ++b)
			player.getNextAudioBlock ({ &buffer, 0, block });
	}
};
}

TEST_P (PlayerClock, ClockForABlockInsideALoopNeverPassesLoopOut)
{
	const auto c = GetParam();
	player.setSpeed (c.speed);
	player.setKeyLock (c.keyLock);
	player.setLoop (loopIn, loopIn + c.loopSeconds);
	player.play();

	const auto loop = player.loopBeats();
	ASSERT_TRUE (loop.has_value());
	const auto [in, out] = *loop;
	const auto loopLength = out - in;
	juce::AudioBuffer<float> buffer (StemDeckPlayer::numOutputChannels, block);
	auto last = deckClock (player);
	int passes = 0;
	std::uint32_t settledSeeks = 0;
	const int blocks = (int) (12 * c.loopSeconds / c.speed * rate / block);

	for (int b = 0; b < blocks; ++b)
	{
		player.getNextAudioBlock ({ &buffer, 0, block });
		const auto clock = deckClock (player);
		ASSERT_TRUE (clock.beat.has_value());
		ASSERT_EQ (clock.loopBeats, loop);
		EXPECT_LT (*clock.beat, out) << name (c) << ", block " << b;

		const bool wrappedBack = *clock.beat < *last.beat - 0.5 * loopLength;
		EXPECT_EQ (clock.wraps - last.wraps, wrappedBack ? 1u : 0u) << name (c) << ", block " << b;
		passes += wrappedBack ? 1 : 0;
		if (b == 100)
			settledSeeks = clock.seeks;
		if (b > 100)
			EXPECT_EQ (clock.seeks, settledSeeks) << name (c) << ": a loop pass is no seek";
		last = clock;
	}
	EXPECT_GE (passes, 10) << name (c);
	EXPECT_EQ (player.loopWraps(), last.wraps);
}

TEST_P (PlayerClock, SeekBlockCarriesThePostSeekBeat)
{
	const auto c = GetParam();
	player.setSpeed (c.speed);
	player.setKeyLock (c.keyLock);
	player.setPosition (1.0);
	player.play();
	renderBlocks (50);

	const auto before = deckClock (player);
	player.setPosition (10.0);
	const auto seeked = deckClock (player);
	EXPECT_NE (seeked.seeks, before.seeks);
	ASSERT_TRUE (seeked.beat.has_value());
	EXPECT_DOUBLE_EQ (*seeked.beat, 20.0) << name (c) << ": the counter and the beat it moved to";

	renderBlocks (1);
	const auto played = deckClock (player);
	EXPECT_GE (*played.beat, 20.0 - 0.01) << name (c);
	EXPECT_LT (*played.beat, 20.0 + 0.1) << name (c);

	player.setLoop (loopIn, loopIn + c.loopSeconds);
	EXPECT_NE (deckClock (player).seeks, played.seeks) << "setting a loop jumps to its in";
}

INSTANTIATE_TEST_SUITE_P (KeyLockOnAndOff, PlayerClock,
	::testing::Values (Case { 0.5, 1.0, false }, Case { 0.5, 1.03, true }));

TEST_F (PlayerClock, LoopBeatsAreEmptyWithoutLoopOrGrid)
{
	EXPECT_FALSE (player.loopBeats().has_value()) << "no loop";

	player.setLoop (loopIn, loopIn + 0.5);
	EXPECT_EQ (player.loopBeats(), (std::optional<std::pair<double, double>> { { 6.0, 7.0 } }));
	EXPECT_EQ (deckClock (player).loopBeats, player.loopBeats());

	player.setBeatGrid ({});
	EXPECT_TRUE (player.hasLoop());
	EXPECT_FALSE (player.loopBeats().has_value()) << "no grid";
	EXPECT_FALSE (deckClock (player).loopBeats.has_value());

	player.setBeatGrid ({ 120.0, 0.0 });
	player.clearLoop();
	EXPECT_FALSE (player.loopBeats().has_value()) << "a cleared loop";
}

TEST_F (PlayerClock, CountersNeverGoBackAcrossALoad)
{
	player.setLoop (loopIn, loopIn + 0.25);
	player.play();
	renderBlocks ((int) (2.0 * rate / block));
	const auto seeks = player.seeks(), wraps = player.loopWraps();
	ASSERT_GT (wraps, 0u);

	ASSERT_EQ (player.load (set), juce::String());
	ASSERT_TRUE (waitUntilReady());
	EXPECT_GT (player.seeks(), seeks) << "a load is a jump";
	EXPECT_GE (player.loopWraps(), wraps);
}

TEST_F (PlayerClock, AScratchingBlockCountsAsASeek)
{
	player.setPosition (5.0);
	player.play();
	renderBlocks (10);
	const auto seeks = player.seeks();

	player.beginScratch();
	player.scratchBy (-0.2);
	renderBlocks (1);
	EXPECT_NE (player.seeks(), seeks) << "a hand on the record is a jump, back or forth";
	player.endScratch();
}

// A grid edit or a late analysis moves the beat under a standing playhead.
TEST_F (PlayerClock, AGridChangeCountsAsASeek)
{
	player.setPosition (5.0);
	player.play();
	renderBlocks (10);
	const auto before = deckClock (player);

	player.setBeatGrid ({ 120.0, 0.25 });
	const auto after = deckClock (player);
	EXPECT_NE (after.seeks, before.seeks) << "the beat jumped without the playhead moving";
	ASSERT_TRUE (after.beat.has_value());
	EXPECT_LT (*after.beat, *before.beat);
}


namespace
{
// What each block of `blocks` cost: the stretchers' frames and the time.
struct BlockCost
{
	std::int64_t mostWork = 0;
	double slowest = 0.0, median = 0.0;
};

BlockCost measureBlocks (StemDeckPlayer& player, int blocks, int size)
{
	juce::AudioBuffer<float> buffer (StemDeckPlayer::numOutputChannels, size);
	BlockCost cost;
	std::vector<double> times;
	for (int b = 0; b < blocks; ++b)
	{
		const auto workBefore = player.stretcherWork();
		const auto start = juce::Time::getMillisecondCounterHiRes();
		player.getNextAudioBlock ({ &buffer, 0, size });
		times.push_back (juce::Time::getMillisecondCounterHiRes() - start);
		cost.mostWork = std::max (cost.mostWork, player.stretcherWork() - workBefore);
	}
	std::sort (times.begin(), times.end());
	cost.slowest = times.back();
	cost.median = times[times.size() / 2];
	return cost;
}
}

// The rig runs 128 frames a block (2.9 ms at 44.1 kHz). A seam must cost no
// more than the blocks around it: with key lock, starting the stretchers
// at the loop's start inside the seam's block (03.0+236) took ~3 ms and
// every seam was an xrun. The work is counted (the stretchers' frames, robust on a busy
// machine), the time only bounded loosely.
TEST_F (PlayerLoopSeek, ASeamCostsNoMoreThanTheBlocksAroundIt)
{
	constexpr int rigBlock = 128;
	player.prepareToPlay (rigBlock, rate);
	player.setSpeed (1.03);
	player.setKeyLock (true);
	player.setPosition (3.0);
	player.play();
	measureBlocks (player, 1, rigBlock);   // the seek's own block: a whole start, as ever
	// From there on, as after every start, the four stems feed their
	// stretchers in the same blocks: the steady state to hold the seams to.
	const auto steady = measureBlocks (player, 2000, rigBlock);

	player.setLoop (3.0, 3.5);
	measureBlocks (player, 50, rigBlock);    // the loop's own setting is a seek
	const auto looping = measureBlocks (player, (int) (24 * 0.5 / 1.03 * rate / rigBlock), rigBlock);

	std::printf ("steady: %lld frames, %.3f ms slowest, %.3f ms median; looping: %lld frames, %.3f ms slowest, %.3f ms median\n",
			(long long) steady.mostWork, steady.slowest, steady.median, (long long) looping.mostWork, looping.slowest, looping.median);
	// The seam is in what the stretchers read: they never start again, so
	// no block of the loop does more than the steady state's.
	EXPECT_LE (looping.mostWork, steady.mostWork) << "no stretcher work at the seam";
	EXPECT_LT (looping.slowest, 2.0 * steady.slowest + 0.2) << "ms";
}


//==============================================================================
namespace
{
// Music on the grid: the synthetic stems (kick on the beat, hats off it,
// bass, a pad) at 120 BPM, the downbeat at 0.25 s, 8 kHz.
class PlayerLoopMusic : public LoopFixture, public ::testing::WithParamInterface<Case>
{
protected:
	void SetUp() override
	{
		LoopFixture::SetUp();
		const auto music = synthetic::fromScript ("FFFFFFFFFFFFFFFF");
		load ({ music.samples[0], music.samples[1], music.samples[2], music.samples[3] }, synthetic::sampleRate);
	}
};
}

// No click: around every seam no step between samples beyond what the
// music itself makes -- the same stretch played on without a loop.
TEST_P (PlayerLoopMusic, TheSeamClicksNoMoreThanTheMusic)
{
	const auto c = GetParam();
	player.setSpeed (c.speed);
	player.setKeyLock (c.keyLock);
	const auto start = synthetic::firstBeat + 4 * 4 * synthetic::beat;   // the fifth bar's downbeat
	const auto end = start + c.loopSeconds;
	constexpr int passes = 24;

	player.setPosition (start);
	player.play();
	const auto plain = play (start, end, passes);
	player.pause();

	player.setLoop (start, end);
	player.play();
	const auto looped = play (start, end, passes);
	EXPECT_GE (looped.wraps, passes - 1) << name (c);

	const auto settled = (size_t) (0.3 * rate);
	const auto own = steepestStep (plain.left, settled), atSeams = steepestStep (looped.left, settled);
	std::printf ("%s: steepest %.4f, without the loop %.4f\n", name (c).c_str(), atSeams, own);
	EXPECT_LT (atSeams, 1.25 * own) << name (c) << ": a click at the seam";
}

INSTANTIATE_TEST_SUITE_P (BeatsTemposAndKeyLock, PlayerLoopMusic,
	::testing::Values (Case { 0.5, 1.0, false }, Case { 1.0, 1.0, false }, Case { 2.0, 1.0, false },
					   Case { 1.0, 0.94, false }, Case { 1.0, 1.08, false },
					   Case { 0.5, 1.0, true }, Case { 1.0, 1.0, true }, Case { 2.0, 1.0, true },
					   Case { 1.0, 0.94, true }, Case { 1.0, 1.08, true }));

//==============================================================================
namespace
{
constexpr double markedStart = 3.0, markedLength = 0.5;
constexpr float marker = 0.9f;   // above anything the tones reach (0.6)

// Tones on stem 0, and in the 4096 frames before the loop's start a marker
// no tone reaches: if it is ever heard, the seam reached before the start.
class PlayerLoopExact : public LoopFixture
{
protected:
	void SetUp() override
	{
		LoopFixture::SetUp();
		tones = toneMix();
		const auto at = (size_t) std::llround (markedStart * rate);
		std::fill (tones.begin() + (long) at - 4096, tones.begin() + (long) at, marker);
		const std::vector<float> silence (tones.size(), 0.0f);
		load ({ tones, silence, silence, silence }, rate);
	}

	std::vector<float> tones;
};
}

// After every seam the loop's start is heard exactly, from its first frame
// and at full level; the only other thing there is what was cut off behind
// the end, fading out over StemDeckPlayer::seamFade frames. Plain path,
// rate 1: the output is the file's own frames, a fixed latency later.
TEST_F (PlayerLoopExact, TheLoopsStartIsHeardExactlyAndNothingBeforeIt)
{
	const auto start = (std::int64_t) std::llround (markedStart * rate);
	const auto length = (std::int64_t) std::llround (markedLength * rate);
	const auto end = start + length;
	player.setLoop ((double) start / rate, (double) end / rate);
	player.play();
	constexpr int passes = 6;
	const auto r = play (markedStart, markedStart + markedLength, passes);

	// What a CDJ plays: the loop's frames over and over; after each seam the
	// frames behind the end under them, fading out.
	const auto fade = StemDeckPlayer::seamFade;
	const auto expected = [&] (std::int64_t k)
	{
		const auto pass = k / length, i = k % length;
		auto v = tones[(size_t) (start + i)];
		if (pass > 0 && i < fade)
			v += 0.5f * (1.0f + std::cos ((float) pi * ((float) i + 0.5f) / (float) fade)) * tones[(size_t) (end + i)];
		return v;
	};

	// The fixed latency of the deck's resampler: where the output matches best.
	const auto errorAt = [&] (int lag, std::int64_t from, std::int64_t to)
	{
		double worst = 0.0;
		for (auto n = from; n < to && n < (std::int64_t) r.left.size(); ++n)
			worst = std::max (worst, (double) std::abs (r.left[(size_t) n] - expected (n - lag)));
		return worst;
	};
	int lag = 0;
	for (int candidate = 1; candidate < 64; ++candidate)
		if (errorAt (candidate, 1024, 4096) < errorAt (lag, 1024, 4096))
			lag = candidate;

	const auto worst = errorAt (lag, 1024, (std::int64_t) r.left.size());
	EXPECT_LT (worst, 1.0e-3) << "the loop as a CDJ plays it, latency " << lag;

	float loudest = 0.0f;
	for (const auto v : r.left)
		loudest = std::max (loudest, std::abs (v));
	EXPECT_LT (loudest, 0.8f) << "nothing from before the loop's start";
}

// The same with key lock: the stretcher changes every frame, but nothing
// from before the start may reach the ear.
TEST_F (PlayerLoopExact, NothingBeforeTheStartWithKeyLockEither)
{
	player.setSpeed (0.94);
	player.setKeyLock (true);
	player.setLoop (markedStart, markedStart + markedLength);
	player.play();
	const auto r = play (markedStart, markedStart + markedLength, 12);
	float loudest = 0.0f;
	for (size_t i = (size_t) (0.1 * rate); i < r.left.size(); ++i)
		loudest = std::max (loudest, std::abs (r.left[i]));
	EXPECT_LT (loudest, 0.8f) << "nothing from before the loop's start";
}


//==============================================================================
namespace
{
// Blocks as the audio device asks for them: no faster than real time, so
// the player's background threads get the time they get on the rig.
std::vector<float> playInRealTime (StemDeckPlayer& player, double seconds)
{
	std::vector<float> left;
	juce::AudioBuffer<float> buffer (StemDeckPlayer::numOutputChannels, block);
	const auto blocks = (int) (seconds * rate / block);
	const auto begin = juce::Time::getMillisecondCounterHiRes();
	for (int b = 0; b < blocks; ++b)
	{
		const auto due = begin + 1000.0 * b * block / rate;
		while (juce::Time::getMillisecondCounterHiRes() < due)
			juce::Thread::sleep (1);
		buffer.clear();
		player.getNextAudioBlock ({ &buffer, 0, block });
		left.insert (left.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + block);
	}
	return left;
}

size_t longestSilence (const std::vector<float>& x, size_t from)
{
	size_t longest = 0, run = 0;
	for (size_t i = from; i < x.size(); ++i)
	{
		run = x[i] == 0.0f ? run + 1 : 0;
		longest = std::max (longest, run);
	}
	return longest;
}

// The tones on all four stems, as FLAC.
class PlayerLoopFlac : public LoopFixture
{
protected:
	void SetUp() override
	{
		LoopFixture::SetUp();
		const auto tones = toneMix();
		load ({ tones, tones, tones, tones }, rate, Format::flac);
	}
};
}

// FLAC stems were read through JUCE's BufferingAudioReader, which keeps only
// what lies ahead of its last read and gives silence for anything it has not
// buffered: every jump back to the loop's start read zeros until its thread
// had decoded there again -- on the rig (03.0+240) 0 to 90 ms of silence at
// every seam, each stem coming back on its own: a click and a swell. As the
// keys do it: IN at 5 s, OUT a second later, then pass after pass.
TEST_F (PlayerLoopFlac, EveryPassIsTheLoopWithNoSilenceAtTheSeam)
{
	constexpr double in = 5.0, out = 6.0;
	player.setPosition (in - 1.0);
	juce::Thread::sleep (300);   // the deck stands loaded a while before PLAY
	player.play();
	playInRealTime (player, out - (in - 1.0));
	ASSERT_GT (player.getPosition(), out - 0.05);

	player.setLoop (in, out);
	const auto looped = playInRealTime (player, 3.5 * (out - in));

	EXPECT_LT (longestSilence (looped, 0), (size_t) 4) << "frames of silence: the loop's start not read";

	const auto length = (size_t) std::llround ((out - in) * rate);
	double worst = 0.0;
	for (size_t n = length + 64; n < looped.size(); ++n)
		worst = std::max (worst, (double) std::abs (looped[n] - looped[n - length]));
	EXPECT_LT (worst, 1.0e-6) << "every pass the same frames";
}

//==============================================================================
// Since 2026-10-29 compressed stems are decoded whole into memory when the set
// loads: any jump -- CUE, a seek, the jog's search, a click in the waveform,
// a loop's first jump and every seam -- lands on decoded frames. Before, a
// jump into what the buffered reader had not decoded read up to ~90 ms of
// zeros, the first one included.
TEST_F (PlayerLoopFlac, NoJumpStartsWithSilence)
{
	player.setPosition (1.0);
	player.play();
	const std::array<double, 5> jumps { 25.0, 11.7, 37.2, 2.4, 19.95 };
	for (const auto to : jumps)
	{
		player.setPosition (to);   // a seek, the jog's search, a waveform click
		const auto heard = playInRealTime (player, 0.25);
		EXPECT_LT (longestSilence (heard, 0), (size_t) 4) << "frames of silence after a jump to " << to << " s";
	}

	// CUE: back to the cue point while standing, then PLAY.
	player.pause();
	player.setCuePoint (31.3);
	player.setPosition (player.getCuePoint());
	player.play();
	EXPECT_LT (longestSilence (playInRealTime (player, 0.25), 0), (size_t) 4) << "frames of silence after CUE";
}

namespace
{
class PlayerLoopFlacLong : public PlayerLoopFlac, public ::testing::WithParamInterface<double> {};
}

// A loop longer than what was once held of its start (2 s): its first jump
// -- far from where the deck played -- and its first seam, read from memory.
TEST_P (PlayerLoopFlacLong, TheFirstJumpAndTheSeamAreNeverSilent)
{
	const auto length = GetParam();
	const auto start = 30.0 - length;
	player.setPosition (2.0);
	player.play();
	playInRealTime (player, 0.2);

	player.setLoop (start, start + length);
	const auto looped = playInRealTime (player, length + 0.5);
	EXPECT_LT (longestSilence (looped, 0), (size_t) 4) << "frames of silence in a " << length << " s loop";
	EXPECT_GE (player.getPosition(), start);
	EXPECT_LT (player.getPosition(), start + length);
}

INSTANTIATE_TEST_SUITE_P (ThreeAndEightSeconds, PlayerLoopFlacLong, ::testing::Values (3.0, 8.0));

namespace
{
class PlayerLoopFlacStart : public LoopFixture {};
}

// PLAY straight after loading, before the stems are decoded: the deck starts
// once they are, from where it stands -- never zeros in the middle of the music.
TEST_F (PlayerLoopFlacStart, PlayBeforeDecodingStartsWhenReady)
{
	const auto tones = toneMix();
	load ({ tones, tones, tones, tones }, rate, Format::flac, false);
	player.setPosition (12.0);
	player.play();
	const auto heard = playInRealTime (player, 1.0);
	EXPECT_TRUE (player.isReady());

	const auto first = (size_t) std::distance (heard.begin(), std::find_if (heard.begin(), heard.end(), [] (float v) { return v != 0.0f; }));
	ASSERT_LT (first, heard.size()) << "nothing heard";
	EXPECT_LT (longestSilence (heard, first), (size_t) 4) << "frames of silence once it started";
	EXPECT_NEAR (player.getPosition(), 12.0 + (double) (heard.size() - first) / rate, 0.05) << "it started where it stood";
}

// The decoded stems are the set's memory: a reload frees the previous set's,
// and the two are never held at once. A mapped WAV set holds none.
TEST_F (PlayerLoopFlacStart, AReloadFreesThePreviousSetsMemory)
{
	const auto bytesOf = [] (double seconds) { return (juce::int64) StemSet::numStems * 2 * (juce::int64) std::llround (seconds * rate) * (juce::int64) sizeof (float); };
	const auto tones = toneMix();
	load ({ tones, tones, tones, tones }, rate, Format::flac);
	EXPECT_EQ (player.decodedBytes(), bytesOf (trackSeconds));

	const std::vector<float> shorter (tones.begin(), tones.begin() + (long) (10.0 * rate));
	const auto second = write ({ shorter, shorter, shorter, shorter }, rate, Format::flac, "second");
	ASSERT_EQ (player.load (second), juce::String());
	juce::int64 most = player.decodedBytes();
	for (int waited = 0; waited < 20000 && ! player.isReady(); waited += 1)
	{
		most = std::max (most, player.decodedBytes());
		juce::Thread::sleep (1);
	}
	ASSERT_TRUE (player.isReady());
	EXPECT_EQ (player.decodedBytes(), bytesOf (10.0)) << "the first set's memory is freed";
	EXPECT_LE (most, bytesOf (trackSeconds)) << "never both sets at once";

	const auto mapped = write ({ shorter, shorter, shorter, shorter }, rate, Format::wav, "mapped");
	ASSERT_EQ (player.load (mapped), juce::String());
	EXPECT_TRUE (player.isReady()) << "a mapped set plays at once";
	EXPECT_EQ (player.decodedBytes(), 0);
}

// Not in the suite (it writes 4 x 7 minutes of FLAC): how long a set of the
// library's size takes from load() to ready on this machine.
// --gtest_also_run_disabled_tests --gtest_filter='*SevenMinute*'
TEST_F (PlayerLoopFlacStart, DISABLED_LoadTimeOfASevenMinuteSet)
{
	constexpr double minutes = 7.0;
	juce::Random random (29);
	std::vector<float> music ((size_t) (minutes * 60.0 * rate));
	for (size_t i = 0; i < music.size(); ++i)
		music[i] = 0.2f * (random.nextFloat() - 0.5f) + 0.3f * (float) std::sin (2.0 * pi * 110.0 * (double) i / rate);
	const auto seven = write ({ music, music, music, music }, rate, Format::flac, "seven");

	const auto begin = juce::Time::getMillisecondCounterHiRes();
	ASSERT_EQ (player.load (seven), juce::String());
	const auto opened = juce::Time::getMillisecondCounterHiRes();
	ASSERT_TRUE (waitUntilReady());
	const auto ready = juce::Time::getMillisecondCounterHiRes();
	std::printf ("7-minute 4-stem FLAC set: load() %.0f ms, ready after %.0f ms, %.1f MB decoded\n",
			opened - begin, ready - begin, (double) player.decodedBytes() / 1.0e6);
}

// QUANTIZE: the loop's edges, the cue point and the user's jumps land on the
// grid's beats while it is on (120 BPM from 0: a beat every 0.5 s).
TEST_F (PlayerClock, LoopEdgesSnapOnlyWhenQuantizeIsOn)
{
	player.setLoop (3.1, 5.2);
	EXPECT_DOUBLE_EQ (player.getLoop().getStart(), 3.0);
	EXPECT_DOUBLE_EQ (player.getLoop().getEnd(), 5.0);

	player.clearLoop();
	player.setQuantize (false);
	player.setLoop (3.1, 5.2);
	EXPECT_NEAR (player.getLoop().getStart(), 3.1, 1e-4);
	EXPECT_NEAR (player.getLoop().getEnd(), 5.2, 1e-4);
}

TEST_F (PlayerClock, CueSnapsWhenOn)
{
	player.setCuePoint (7.3);
	EXPECT_DOUBLE_EQ (player.getCuePoint(), 7.5);
	player.setQuantize (false);
	player.setCuePoint (7.3);
	EXPECT_DOUBLE_EQ (player.getCuePoint(), 7.3);
}

// A grid whose first beat is more than half a beat in: a cue near the start
// rounds to the beat before it, before the track; it stays at the start.
TEST_F (PlayerClock, ASnappedCueIsNeverBeforeTheStart)
{
	player.setBeatGrid ({ 120.0, 0.4 });
	player.setCuePoint (0.1);
	EXPECT_DOUBLE_EQ (player.getCuePoint(), 0.0);
}

TEST_F (PlayerClock, JumpToSnapsWhenOn)
{
	const auto before = player.seeks();
	player.jumpTo (9.1);
	EXPECT_NEAR (player.getPosition(), 9.0, 1e-4);
	EXPECT_GT (player.seeks(), before) << "a jump is counted";
	player.setQuantize (false);
	player.jumpTo (9.1);
	EXPECT_NEAR (player.getPosition(), 9.1, 1e-4);
	player.setPosition (11.1);
	player.setQuantize (true);
	player.setPosition (12.1);
	EXPECT_NEAR (player.getPosition(), 12.1, 1e-4) << "setPosition stays raw";
}

TEST_F (PlayerClock, QuantizeIsOffWithoutAGrid)
{
	EXPECT_TRUE (player.quantize());
	player.setBeatGrid ({ 0.0, 0.0 });
	EXPECT_FALSE (player.quantize());
	player.setCuePoint (7.3);
	EXPECT_DOUBLE_EQ (player.getCuePoint(), 7.3);
	player.jumpTo (9.1);
	EXPECT_NEAR (player.getPosition(), 9.1, 1e-4);
	player.setBeatGrid ({ 120.0, 0.0 });
	EXPECT_TRUE (player.quantize()) << "the switch itself was never turned off";
}
