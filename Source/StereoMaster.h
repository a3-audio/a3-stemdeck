// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Output: Stereo -- the deck's stems as one master pair. Pure: no JUCE.
namespace stereomaster
{
	// Four stems at full scale add to four times full scale; the trim is
	// what keeps a typical mix of them inside the converter.
	constexpr float trim = 0.5f;

	// stemPairs: the stems' channels as StemDeckPlayer renders them (after the
	// stem knob and the mute), L0 R0 L1 R1 ...; numChannels of them (even).
	// Writes (not adds) numSamples to left and right.
	inline void block (const float* const* stemPairs, int numChannels, int numSamples, float gain, float* left, float* right)
	{
		for (int i = 0; i < numSamples; ++i)
		{
			float l = 0.0f, r = 0.0f;
			for (int c = 0; c + 1 < numChannels; c += 2)
			{
				l += stemPairs[c][i];
				r += stemPairs[c + 1][i];
			}
			left[i] = l * gain;
			right[i] = r * gain;
		}
	}
}
