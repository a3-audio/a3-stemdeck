// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>

#include "StereoMaster.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace
{
	constexpr int stemChannels = 8;   // 4 stems x L/R, as StemDeckPlayer renders them

	struct Stems
	{
		explicit Stems (int numSamples) : data ((size_t) stemChannels, std::vector<float> ((size_t) numSamples, 0.0f))
		{
			for (size_t c = 0; c < data.size(); ++c)
				pointers[c] = data[c].data();
		}
		void fill (int stem, float left, float right)
		{
			std::fill (data[(size_t) stem * 2].begin(), data[(size_t) stem * 2].end(), left);
			std::fill (data[(size_t) stem * 2 + 1].begin(), data[(size_t) stem * 2 + 1].end(), right);
		}

		std::vector<std::vector<float>> data;
		std::array<const float*, stemChannels> pointers {};
	};

	struct Master
	{
		explicit Master (int n) : left ((size_t) n, 99.0f), right ((size_t) n, 99.0f) {}
		std::vector<float> left, right;
	};
}

TEST (StereoMaster, TheTrimIsHalf)
{
	EXPECT_EQ (stereomaster::trim, 0.5f);
}

TEST (StereoMaster, LeftSumsTheLeftsAndRightTheRights)
{
	Stems stems (4);
	stems.fill (0, 0.1f, 0.0f);
	stems.fill (1, 0.2f, 0.0f);
	stems.fill (2, 0.0f, 0.4f);
	stems.fill (3, 0.0f, 0.2f);
	Master out (4);

	stereomaster::block (stems.pointers.data(), stemChannels, 4, 1.0f, out.left.data(), out.right.data());

	for (int i = 0; i < 4; ++i)
	{
		EXPECT_FLOAT_EQ (out.left[(size_t) i], 0.3f);
		EXPECT_FLOAT_EQ (out.right[(size_t) i], 0.6f);
	}
}

TEST (StereoMaster, TheTrimScalesTheSum)
{
	Stems stems (3);
	stems.fill (0, 0.5f, -0.5f);
	stems.fill (1, 0.25f, 0.25f);
	Master out (3);

	stereomaster::block (stems.pointers.data(), stemChannels, 3, stereomaster::trim, out.left.data(), out.right.data());

	EXPECT_FLOAT_EQ (out.left[0], 0.375f);
	EXPECT_FLOAT_EQ (out.right[0], -0.125f);
}

TEST (StereoMaster, MutedStemsAreSilentSoSilenceIsSilence)
{
	Stems stems (8);   // the player hands over zeros for a muted stem
	Master out (8);

	stereomaster::block (stems.pointers.data(), stemChannels, 8, stereomaster::trim, out.left.data(), out.right.data());

	for (int i = 0; i < 8; ++i)
	{
		EXPECT_EQ (out.left[(size_t) i], 0.0f) << "overwrites what was in the buffer";
		EXPECT_EQ (out.right[(size_t) i], 0.0f);
	}
}

TEST (StereoMaster, FourStemsOfHalfScaleDoNotClipAtTheTrim)
{
	Stems stems (16);
	for (int stem = 0; stem < 4; ++stem)
		stems.fill (stem, 0.5f, -0.5f);
	Master out (16);

	stereomaster::block (stems.pointers.data(), stemChannels, 16, stereomaster::trim, out.left.data(), out.right.data());

	for (int i = 0; i < 16; ++i)
	{
		EXPECT_LE (std::abs (out.left[(size_t) i]), 1.0f);
		EXPECT_LE (std::abs (out.right[(size_t) i]), 1.0f);
	}
}

TEST (StereoMaster, ZeroSamplesTouchNothing)
{
	Stems stems (1);
	Master out (1);
	stereomaster::block (stems.pointers.data(), stemChannels, 0, 1.0f, out.left.data(), out.right.data());
	EXPECT_EQ (out.left[0], 99.0f);
}
