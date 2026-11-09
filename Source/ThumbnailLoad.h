// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <cmath>

// How a deck's four waveform thumbnails come in while a set loads. Pure, no
// JUCE: StemThumbnails feeds it what its AudioThumbnails report and redraws
// only when it says so.
//
// A stem's display gain (the vertical zoom that makes its loudest peak fill
// the lane) is set once, when its thumbnail is complete: from a half-read
// track it would rescale the picture on every progress step. And while
// loading, the views redraw only when a stem completes or the load has gone
// on by another progressStep -- not on each of the thumbnails' many steps,
// each of which re-renders the whole overview.
namespace thumbnailload
{
	constexpr int stems = 4;
	constexpr float fill = 0.95f, maxGain = 4.0f;   // capped so near-silent stems don't show noise
	constexpr double progressStep = 0.25;            // of the whole set's load

	inline float gainFor (float peak)
	{
		return peak > 0.0f ? std::clamp (fill / peak, 1.0f, maxGain) : 1.0f;
	}

	// What one thumbnail reports: whether it is complete, how much of it is
	// read (0..1), and its loudest peak so far.
	struct StemState
	{
		bool complete = false;
		double proportion = 0.0;
		float peak = 0.0f;
	};

	class Tracker
	{
	public:
		// A new set: every gain 1, nothing complete.
		void reset()
		{
			gains.fill (1.0f);
			complete.fill (false);
			drawnProgress = 0.0;
		}

		// True when the views should redraw.
		bool update (const std::array<StemState, stems>& states)
		{
			auto redraw = false;
			auto progress = 0.0;
			for (size_t s = 0; s < states.size(); ++s)
			{
				progress += std::clamp (states[s].proportion, 0.0, 1.0) / stems;
				if (complete[s] || ! states[s].complete)
					continue;
				complete[s] = true;
				gains[s] = gainFor (states[s].peak);
				redraw = true;
			}

			if (progress >= drawnProgress + progressStep)
			{
				drawnProgress = progress;
				redraw = true;
			}
			return redraw;
		}

		float gain (int stem) const { return gains[(size_t) std::clamp (stem, 0, stems - 1)]; }

	private:
		std::array<float, stems> gains { 1.0f, 1.0f, 1.0f, 1.0f };
		std::array<bool, stems> complete {};
		double drawnProgress = 0.0;
	};
}
