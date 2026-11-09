// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cmath>

// A press on the overview: a tap jumps, a drag across it sets a loop. Pure.
namespace overviewgesture
{
	// A loop needs a drag over this share of the overview's width; less is a
	// tap. A finger on the touch screen wanders a few pixels on its own, and
	// on the whole track a few pixels are seconds -- a beat once quantized.
	constexpr float loopShare = 0.04f;

	inline bool isLoopDrag (float startX, float endX, float width)
	{
		return width > 0.0f && std::abs (endX - startX) >= width * loopShare;
	}
}
