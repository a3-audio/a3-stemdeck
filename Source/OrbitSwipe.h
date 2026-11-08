// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <vector>

// The ORBIT pad's direction gesture: hold, then swipe sideways. Pure: the pad
// feeds it times (seconds) and x positions and asks at release. Every size is
// a share of the pad's width, so the gesture scales with the screen.
class OrbitSwipe
{
public:
	enum class Result { none, left, right };

	static constexpr double holdSeconds = 0.35;       // still, before the swipe
	static constexpr float travelShare = 0.35f;       // of the pad width, at release
	static constexpr float stillShare = 0.1f;         // moving less than this is still holding

	void down (double timeSeconds, float x);
	void drag (double timeSeconds, float x);
	// right: held >= holdSeconds, then released >= travelShare * padWidth to the right of the start.
	Result up (float padWidth);
	// The pad's "armed" look: pressed for long enough that a swipe would count.
	bool isHold (double nowSeconds) const;

private:
	struct Sample { double time; float x; };

	bool pressed = false;
	double downTime = 0.0;
	float startX = 0.0f;
	std::vector<Sample> moves;
};
