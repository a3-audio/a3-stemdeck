#include <gtest/gtest.h>

#include "NiStemCache.h"

#include <juce_core/juce_core.h>

namespace
{
	bool haveFfmpeg()
	{
		juce::ChildProcess probe;
		return probe.start ("ffmpeg -version", 0) && probe.waitForProcessToFinish (10000) && probe.getExitCode() == 0;
	}

	// A file laid out like NI's: track 0 the master, tracks 1-4 the stems, AAC as NI sells it.
	bool makeNiLikeFile (const juce::File& file, int audioTracks)
	{
		juce::StringArray args { "ffmpeg", "-nostdin", "-loglevel", "error", "-y" };
		for (int t = 0; t < audioTracks; ++t)
			args.addArray ({ "-f", "lavfi", "-i", "sine=frequency=" + juce::String (100 * (t + 1)) + ":duration=1" });
		for (int t = 0; t < audioTracks; ++t)
			args.addArray ({ "-map", juce::String (t) });
		args.addArray ({ "-c:a", "aac", file.getFullPathName() });
		juce::ChildProcess make;
		return make.start (args, 0) && make.waitForProcessToFinish (60000) && make.getExitCode() == 0;
	}

	struct NiStemCacheTest : ::testing::Test
	{
		juce::TemporaryFile scratch;
		juce::File root, music;

		void SetUp() override
		{
			if (! haveFfmpeg())
				GTEST_SKIP() << "ffmpeg is not installed";
			ASSERT_TRUE (scratch.getFile().createDirectory());
			root = scratch.getFile().getChildFile ("cache");
			music = scratch.getFile().getChildFile ("music");
			ASSERT_TRUE (music.createDirectory());
		}

		void TearDown() override { scratch.getFile().deleteRecursively(); }
	};
}

TEST (NiStemInfo, NamesAndColoursComeFromNisJson)
{
	const auto info = parseNiStemJson (R"({"version": 1, "stems": [{"color": "#fd4a4a", "name": "Drums"},)"
									   R"( {"color": "#ffff00", "name": "Bass"}, {"color": "#00e8e8", "name": "Chords"},)"
									   R"( {"color": "#ad65ff", "name": "Vox+FX"}]})");
	ASSERT_TRUE (info.has_value());
	EXPECT_EQ (info->names[0], "Drums");
	EXPECT_EQ (info->names[2], "Chords");
	EXPECT_EQ (info->names[3], "Vox+FX");
	EXPECT_EQ (info->colours[1], 0xffffff00u);
}

TEST (NiStemInfo, BrokenJsonOrFewerThanFourStemsIsNoInfo)
{
	EXPECT_FALSE (parseNiStemJson ("{not json").has_value());
	EXPECT_FALSE (parseNiStemJson (R"({"stems": [{"name": "Drums"}]})").has_value());
	EXPECT_FALSE (parseNiStemJson (R"({"version": 1})").has_value());
}

TEST_F (NiStemCacheTest, PreparingWritesFourStemsAndNoPartFiles)
{
	const auto source = music.getChildFile ("Track.stem.mp4");
	ASSERT_TRUE (makeNiLikeFile (source, 5));
	const NiStemCache cache (root);

	EXPECT_FALSE (cache.isReady (source));
	ASSERT_TRUE (cache.prepare (source, [] { return false; }));
	EXPECT_TRUE (cache.isReady (source));
	for (const auto& stem : cache.stemFilesFor (source))
	{
		EXPECT_TRUE (stem.existsAsFile()) << stem.getFullPathName();
		EXPECT_TRUE (stem.isAChildOf (root));
	}
	EXPECT_EQ (root.findChildFiles (juce::File::findFiles, true, "*.part.flac").size(), 0);
	EXPECT_TRUE (source.existsAsFile()) << "the original is never touched";
}

TEST_F (NiStemCacheTest, AFileWithoutFiveTracksIsRefusedAndLeavesNothing)
{
	const auto source = music.getChildFile ("Plain.stem.mp4");
	ASSERT_TRUE (makeNiLikeFile (source, 1));
	const NiStemCache cache (root);

	EXPECT_FALSE (cache.prepare (source, [] { return false; }));
	EXPECT_FALSE (cache.isReady (source));
	EXPECT_EQ (root.findChildFiles (juce::File::findFiles, true, "*").size(), 0);
}

TEST_F (NiStemCacheTest, AChangedFileGetsFreshStems)
{
	const auto source = music.getChildFile ("Track.stem.mp4");
	ASSERT_TRUE (makeNiLikeFile (source, 5));
	const NiStemCache cache (root);
	ASSERT_TRUE (cache.prepare (source, [] { return false; }));

	ASSERT_TRUE (source.setLastModificationTime (source.getLastModificationTime() + juce::RelativeTime::seconds (60)));
	EXPECT_FALSE (cache.isReady (source));
}
