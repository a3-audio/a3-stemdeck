#include <gtest/gtest.h>

#include "NiStemInfo.h"

#include <juce_core/juce_core.h>

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
