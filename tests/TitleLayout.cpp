// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>

#include "TitleLayout.h"

namespace
{
	// 10 px a code point (the ellipsis is three bytes but one glyph), no display.
	float tenPerGlyph (const std::string& text)
	{
		float glyphs = 0.0f;
		for (const unsigned char c : text)
			if ((c & 0xC0) != 0x80)
				glyphs += 1.0f;
		return glyphs * 10.0f;
	}

	titlelayout::Result lay (const std::string& title, float width)
	{
		return titlelayout::layout (title, width, tenPerGlyph);
	}
}

TEST (TitleLayout, ShortTitleStaysOnOneLineAtFullSize)
{
	const auto r = lay ("Artist - Title", 300.0f);
	ASSERT_EQ (r.lines.size(), 1u);
	EXPECT_EQ (r.lines[0], "Artist - Title");
	EXPECT_FLOAT_EQ (r.scale, 1.0f);
	EXPECT_FALSE (r.ellipsized);
}

TEST (TitleLayout, LongTitleWrapsAtAWordBeforeThePlayhead)
{
	// 33 glyphs = 330 px; 200 px of room.
	const auto r = lay ("Some Artist - A Rather Long Title", 200.0f);
	ASSERT_EQ (r.lines.size(), 2u);
	EXPECT_EQ (r.lines[0], "Some Artist - A");
	EXPECT_EQ (r.lines[1], "Rather Long Title");
	EXPECT_FLOAT_EQ (r.scale, 1.0f);
	EXPECT_FALSE (r.ellipsized);
	for (const auto& line : r.lines)
		EXPECT_LE (tenPerGlyph (line) * r.scale, 200.0f);
}

TEST (TitleLayout, ShrinksALittleWhenTwoLinesAlmostFit)
{
	// 40 glyphs in one word per half: 2 x 20 glyphs need 200 px, 190 given.
	const auto r = lay ("aaaaaaaaaaaaaaaaaaaa bbbbbbbbbbbbbbbbbbbb", 190.0f);
	ASSERT_EQ (r.lines.size(), 2u);
	EXPECT_LT (r.scale, 1.0f);
	EXPECT_GE (r.scale, titlelayout::smallestScale);
	EXPECT_FALSE (r.ellipsized);
	for (const auto& line : r.lines)
		EXPECT_LE (tenPerGlyph (line) * r.scale, 190.0f);
}

TEST (TitleLayout, VeryLongTitleEndsInAnEllipsisAsTheLastResort)
{
	const std::string title = "A Very Long Artist Name - An Even Longer Track Title That Cannot Possibly Fit";
	const auto r = lay (title, 200.0f);
	ASSERT_EQ (r.lines.size(), 2u);
	EXPECT_TRUE (r.ellipsized);
	EXPECT_FLOAT_EQ (r.scale, titlelayout::smallestScale);
	EXPECT_EQ (r.lines[1].substr (r.lines[1].size() - 3), "\xE2\x80\xA6");
	for (const auto& line : r.lines)
		EXPECT_LE (tenPerGlyph (line) * r.scale, 200.0f);
}

TEST (TitleLayout, AWordLongerThanTheRoomIsBrokenInsideTheWord)
{
	const auto r = lay ("Supercalifragilistic", 100.0f);
	ASSERT_EQ (r.lines.size(), 2u);
	EXPECT_EQ (r.lines[0] + r.lines[1], "Supercalifragilistic");
	for (const auto& line : r.lines)
		EXPECT_LE (tenPerGlyph (line) * r.scale, 100.0f);
}

TEST (TitleLayout, PlayheadAtTheFarLeftLeavesNoRoom)
{
	EXPECT_TRUE (lay ("Artist - Title", 0.0f).lines.empty());
	EXPECT_TRUE (lay ("Artist - Title", -12.0f).lines.empty());
	EXPECT_TRUE (lay ("Artist - Title", 5.0f).lines.empty());   // narrower than one glyph
}

TEST (TitleLayout, PlayheadFarToTheRightKeepsEvenALongTitleOnOneLine)
{
	const auto r = lay ("Some Artist - A Rather Long Title", 2000.0f);
	ASSERT_EQ (r.lines.size(), 1u);
	EXPECT_FLOAT_EQ (r.scale, 1.0f);
}

TEST (TitleLayout, EmptyTitleHasNoLines)
{
	EXPECT_TRUE (lay ("", 300.0f).lines.empty());
}

TEST (TitleLayout, MultibyteTitleIsNeverCutInsideACharacter)
{
	const auto r = lay ("\xC3\x84\xC3\x96\xC3\x9C \xC3\x84\xC3\x96\xC3\x9C \xC3\x84\xC3\x96\xC3\x9C \xC3\x84\xC3\x96\xC3\x9C \xC3\x84\xC3\x96\xC3\x9C \xC3\x84\xC3\x96\xC3\x9C \xC3\x84\xC3\x96\xC3\x9C", 90.0f);
	for (const auto& line : r.lines)
		for (size_t i = 0; i < line.size(); ++i)
			if ((static_cast<unsigned char> (line[i]) & 0xC0) == 0x80)
				EXPECT_GT (i, 0u);   // a continuation byte never starts a line
}
