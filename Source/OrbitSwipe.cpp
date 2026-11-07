// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "OrbitSwipe.h"

#include <cmath>

void OrbitSwipe::down (double timeSeconds, float x)
{
	pressed = true;
	downTime = timeSeconds;
	startX = x;
	moves.clear();
	moves.push_back ({ timeSeconds, x });
}

void OrbitSwipe::drag (double timeSeconds, float x)
{
	if (! pressed)
		return;
	moves.push_back ({ timeSeconds, x });
}

OrbitSwipe::Result OrbitSwipe::up (float padWidth)
{
	if (! pressed || padWidth <= 0.0f)
		return Result::none;
	pressed = false;

	// The hold ends where the finger first leaves the start; a finger that
	// jitters a pixel while it waits has not started its swipe.
	double holdEnd = moves.back().time;
	for (const auto& move : moves)
		if (std::abs (move.x - startX) > stillShare * padWidth)
		{
			holdEnd = move.time;
			break;
		}
	if (holdEnd - downTime < holdSeconds)
		return Result::none;

	const auto travel = moves.back().x - startX;
	if (travel >= travelShare * padWidth)
		return Result::right;
	if (travel <= -travelShare * padWidth)
		return Result::left;
	return Result::none;
}

bool OrbitSwipe::isHold (double nowSeconds) const
{
	return pressed && nowSeconds - downTime >= holdSeconds;
}
