#include <gtest/gtest.h>

#include "NiStemReader.h"
#include "StemReader.h"
#include "StemSet.h"

#include <JuceHeader.h>

#include <algorithm>
#include <cstdlib>
#include <iostream>

namespace
{
	bool haveFfmpeg()
	{
		juce::ChildProcess probe;
		return probe.start ("ffmpeg -version", 0) && probe.waitForProcessToFinish (10000) && probe.getExitCode() == 0;
	}

	// Laid out as NI's files: track t is a stereo sine at 100 * (t + 1) Hz, so track 0 (the master) is
	// 100 Hz and stem n (track n) is 100 * (n + 1) Hz. AAC, as NI sells them.
	bool makeNiLikeFile (const juce::File& file, int audioTracks, double seconds = 1.0)
	{
		juce::StringArray args { "ffmpeg", "-nostdin", "-loglevel", "error", "-y" };
		for (int t = 0; t < audioTracks; ++t)
			args.addArray ({ "-f", "lavfi", "-i",
							 "sine=frequency=" + juce::String (100 * (t + 1)) + ":duration=" + juce::String (seconds) + ":sample_rate=44100" });
		for (int t = 0; t < audioTracks; ++t)
			args.addArray ({ "-map", juce::String (t) });
		args.addArray ({ "-ac", "2", "-c:a", "aac", "-b:a", "256k", file.getFullPathName() });
		juce::ChildProcess make;
		return make.start (args, 0) && make.waitForProcessToFinish (60000) && make.getExitCode() == 0;
	}

	// Rising zero crossings per second of the left channel.
	double frequencyOf (juce::AudioFormatReader& reader, juce::int64 start, int numSamples)
	{
		juce::AudioBuffer<float> buffer (2, numSamples);
		EXPECT_TRUE (reader.read (&buffer, 0, numSamples, start, true, true));
		int crossings = 0;
		const auto* left = buffer.getReadPointer (0);
		for (int i = 1; i < numSamples; ++i)
			crossings += (left[i - 1] < 0.0f && left[i] >= 0.0f) ? 1 : 0;
		return crossings * reader.sampleRate / numSamples;
	}

	struct NiStemReaderTest : ::testing::Test
	{
		juce::TemporaryFile scratch { ".stem.mp4" };
		juce::File file;

		void SetUp() override
		{
			if (! haveFfmpeg())
				GTEST_SKIP() << "ffmpeg is not installed";
			file = scratch.getFile();
			ASSERT_TRUE (makeNiLikeFile (file, 5));
		}
	};
}

TEST_F (NiStemReaderTest, TheHeaderGivesRateChannelsAndLength)
{
	const auto reader = NiStemReader::open (file, 1);
	ASSERT_NE (reader, nullptr);
	EXPECT_DOUBLE_EQ (reader->sampleRate, 44100.0);
	EXPECT_EQ (reader->numChannels, 2u);
	EXPECT_TRUE (reader->usesFloatingPointData);
	// AAC adds a frame or two of priming and padding; the length is the track's, give or take those.
	EXPECT_NEAR ((double) reader->lengthInSamples, 44100.0, 2200.0);
}

TEST_F (NiStemReaderTest, EachTrackHasItsOwnSound)
{
	for (int track = 0; track <= 4; ++track)
	{
		const auto reader = NiStemReader::open (file, track);
		ASSERT_NE (reader, nullptr) << track;
		EXPECT_NEAR (frequencyOf (*reader, 4410, 22050), 100.0 * (track + 1), 6.0) << track;
	}
}

TEST_F (NiStemReaderTest, AReadAtAnOffsetIsTheSameAsTheWholeRead)
{
	const auto reader = NiStemReader::open (file, 2);
	ASSERT_NE (reader, nullptr);
	const auto length = (int) reader->lengthInSamples;
	juce::AudioBuffer<float> whole (2, length), part (2, 1000);
	ASSERT_TRUE (reader->read (&whole, 0, length, 0, true, true));

	const auto again = NiStemReader::open (file, 2);
	ASSERT_TRUE (again->read (&part, 0, 1000, 20000, true, true));
	for (int i = 0; i < 1000; ++i)
		ASSERT_FLOAT_EQ (part.getSample (1, i), whole.getSample (1, 20000 + i)) << i;
}

TEST_F (NiStemReaderTest, PastTheEndIsSilence)
{
	const auto reader = NiStemReader::open (file, 1);
	ASSERT_NE (reader, nullptr);
	juce::AudioBuffer<float> buffer (2, 500);
	buffer.applyGain (0.0f);
	buffer.setSample (0, 0, 1.0f);
	reader->read (&buffer, 0, 500, reader->lengthInSamples + 100, true, true);
	EXPECT_EQ (buffer.getMagnitude (0, 500), 0.0f);
}

TEST_F (NiStemReaderTest, AMissingTrackOrFileGivesNoReader)
{
	EXPECT_EQ (NiStemReader::open (file, 5), nullptr);
	EXPECT_EQ (NiStemReader::open (file.getSiblingFile ("missing.stem.mp4"), 1), nullptr);
}

TEST_F (NiStemReaderTest, AnNiSetPlaysTrackNPlusOneOnSlotN)
{
	StemSet set;
	set.niStems = true;
	set.files.fill (file);
	juce::AudioFormatManager formats;
	formats.registerBasicFormats();

	for (int slot = 0; slot < StemSet::numStems; ++slot)
	{
		std::unique_ptr<juce::AudioFormatReader> reader (createStemReader (set, slot, formats));
		ASSERT_NE (reader, nullptr) << slot;
		EXPECT_NEAR (frequencyOf (*reader, 4410, 22050), 100.0 * (slot + 2), 6.0) << slot;
	}
}

TEST (StemReader, AnyOtherSetIsReadByItsFormat)
{
	juce::TemporaryFile wav { ".wav" };
	{
		juce::WavAudioFormat format;
		std::unique_ptr<juce::OutputStream> stream = wav.getFile().createOutputStream();
		auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions{}
														  .withSampleRate (44100.0)
														  .withNumChannels (2)
														  .withBitsPerSample (24));
		ASSERT_NE (writer, nullptr);
		juce::AudioBuffer<float> silence (2, 441);
		silence.clear();
		ASSERT_TRUE (writer->writeFromAudioSampleBuffer (silence, 0, 441));
	}
	StemSet set;
	set.files[0] = wav.getFile();
	juce::AudioFormatManager formats;
	formats.registerBasicFormats();

	std::unique_ptr<juce::AudioFormatReader> reader (createStemReader (set, 0, formats));
	ASSERT_NE (reader, nullptr);
	EXPECT_EQ (reader->lengthInSamples, 441);
}

// A real NI Stems file, when one is given (STEMDECK_NI_STEMS_FILE): its folder scans
// into a set named from its tags, with NI's stem names, and every slot decodes.
TEST (NiStemReaderOnARealFile, ScansAndDecodesEveryStem)
{
	const auto* path = std::getenv ("STEMDECK_NI_STEMS_FILE");
	if (path == nullptr)
		GTEST_SKIP() << "STEMDECK_NI_STEMS_FILE not set";
	const juce::File file (path);
	juce::AudioFormatManager formats;
	formats.registerBasicFormats();

	const auto sets = StemSet::scanFolder (file.getParentDirectory(), formats);
	const auto set = std::find_if (sets.begin(), sets.end(), [&] (const StemSet& s) { return s.firstFile() == file; });
	ASSERT_NE (set, sets.end());
	EXPECT_TRUE (set->niStems);
	EXPECT_TRUE (set->isComplete());
	EXPECT_GT (set->lengthSeconds, 10.0);
	std::cout << "name: " << set->name << ", stems: " << set->stemNames[0] << " | " << set->stemNames[1] << " | "
			  << set->stemNames[2] << " | " << set->stemNames[3] << ", " << set->lengthSeconds << " s\n";

	for (int slot = 0; slot < StemSet::numStems; ++slot)
	{
		std::unique_ptr<juce::AudioFormatReader> reader (createStemReader (*set, slot, formats));
		ASSERT_NE (reader, nullptr) << slot;
		const auto length = (int) reader->lengthInSamples;
		juce::AudioBuffer<float> buffer (2, length);
		const auto started = juce::Time::getMillisecondCounterHiRes();
		ASSERT_TRUE (reader->read (&buffer, 0, length, 0, true, true)) << slot;
		const auto decodeMs = juce::Time::getMillisecondCounterHiRes() - started;
		const auto rms = buffer.getRMSLevel (0, 0, length);
		std::cout << "slot " << slot + 1 << ": rms " << rms << ", decoded in " << (int) decodeMs << " ms\n";
		EXPECT_GT (rms, 0.001f) << slot;
	}
}
