#include <gtest/gtest.h>

#include "NiStems.h"

#include <cstdint>
#include <string>

namespace
{
	std::string bigEndian32 (uint32_t value)
	{
		return { (char) (value >> 24), (char) (value >> 16), (char) (value >> 8), (char) value };
	}

	std::string box (const std::string& type, const std::string& payload)
	{
		return bigEndian32 ((uint32_t) (8 + payload.size())) + type + payload;
	}

	// An iTunes tag: the item box holds a 'data' box with type and locale before the text.
	std::string tag (const std::string& type, const std::string& text)
	{
		return box (type, box ("data", bigEndian32 (1) + bigEndian32 (0) + text));
	}

	const std::string json = R"({"version": 1, "stems": [{"color": "#fd4a4a", "name": "Drums"},)"
							 R"( {"color": "#ffff00", "name": "Bass"}, {"color": "#00e8e8", "name": "Chords"},)"
							 R"( {"color": "#ad65ff", "name": "Vox+FX"}]})";

	// The layout of NI's own files: ftyp, moov (tracks, then udta with meta/ilst and stem), mdat.
	std::string niFile (bool moovLast = false)
	{
		const auto ilst = box ("ilst", tag ("\xa9nam", "Oh No") + tag ("\xa9" "ART", "Native Instruments"));
		const auto meta = box ("meta", bigEndian32 (0) + box ("hdlr", std::string (25, '\0')) + ilst);
		const auto moov = box ("moov", box ("mvhd", std::string (100, '\0')) + box ("trak", std::string (40, 't'))
										   + box ("udta", meta + box ("stem", json)));
		const auto mdat = box ("mdat", std::string (1000, 'a'));
		const auto ftyp = box ("ftyp", "isom" + bigEndian32 (1));
		return moovLast ? ftyp + mdat + moov : ftyp + moov + mdat;
	}

	NiStemMetadata readFrom (const std::string& file)
	{
		return readNiStemMetadata ([&file] (uint64_t offset, size_t length)
								   { return offset >= file.size() ? std::string() : file.substr ((size_t) offset, length); },
								   file.size());
	}
}

TEST (NiStems, AStemMp4IsAnNiFileWhateverItsCase)
{
	EXPECT_TRUE (isNiStemFileName ("PR_OhNo.stem.mp4"));
	EXPECT_TRUE (isNiStemFileName ("Track.STEM.MP4"));
	EXPECT_FALSE (isNiStemFileName ("Track.mp4"));
	EXPECT_FALSE (isNiStemFileName ("Track.stem.flac"));
	EXPECT_FALSE (isNiStemFileName (".stem.mp4"));
}

TEST (NiStems, TheTitleIsTheNameWithoutStemMp4)
{
	EXPECT_EQ (niStemTitle ("PR_OhNo.stem.mp4"), "PR_OhNo");
	EXPECT_EQ (niStemTitle ("A.b.STEM.MP4"), "A.b");
}

TEST (NiStems, TheMetadataIsReadFromMoovBeforeOrAfterTheAudio)
{
	for (const bool moovLast : { false, true })
	{
		const auto metadata = readFrom (niFile (moovLast));
		ASSERT_TRUE (metadata.stemJson.has_value()) << moovLast;
		EXPECT_EQ (*metadata.stemJson, json);
		EXPECT_EQ (metadata.title, "Oh No");
		EXPECT_EQ (metadata.artist, "Native Instruments");
	}
}

TEST (NiStems, AFileWithoutStemBoxHasNoJson)
{
	const auto plain = box ("ftyp", "isom") + box ("moov", box ("mvhd", std::string (20, '\0'))) + box ("mdat", "x");
	EXPECT_FALSE (readFrom (plain).stemJson.has_value());
}

TEST (NiStems, ACutFileNeverReadsPastItsEnd)
{
	const auto file = niFile();
	for (size_t length : { (size_t) 0, (size_t) 7, (size_t) 30, file.size() / 2 })
		EXPECT_NO_THROW (readFrom (file.substr (0, length))) << length;
	auto lying = box ("ftyp", "isom") + bigEndian32 (0xffffff00u) + "moov";
	EXPECT_FALSE (readFrom (lying).stemJson.has_value());
}
