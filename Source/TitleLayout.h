// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>
#include <string>
#include <vector>

// How a deck's title is set in the rolling waveform's left half: it wraps at
// the playhead instead of running across it. Free of JUCE and of any display:
// the caller supplies the text's width at full size.
namespace titlelayout
{
	// Width in pixels of a UTF-8 string at scale 1.
	using Measure = std::function<float (const std::string&)>;

	struct Result
	{
		std::vector<std::string> lines;   // empty when nothing fits at all
		float scale = 1.0f;               // factor on the font size
		bool ellipsized = false;          // the last line ends in an ellipsis (U+2026)
	};

	constexpr int maxLines = 2;
	// The font shrinks this far (a little) before the text is cut.
	constexpr float smallestScale = 0.8f;

	// width: the room from the title's left edge to the playhead.
	Result layout (const std::string& title, float width, const Measure& measure);
}
