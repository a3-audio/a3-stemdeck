// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "OrbitSwipe.h"

#include <algorithm>
#include <cmath>

void OrbitSwipe::down (Pointer pointer, float x, float y)
{
	if (starts.empty())
		recognised = false;   // a press of its own
	const auto known = std::find_if (starts.begin(), starts.end(), [pointer] (const Start& s) { return s.pointer == pointer; });
	if (known != starts.end())
	{
		*known = { pointer, x, y };
		return;
	}
	starts.push_back ({ pointer, x, y });
}

OrbitSwipe::Result OrbitSwipe::drag (Pointer pointer, float x, float y, float padWidth, float padHeight)
{
	if (recognised || padWidth <= 0.0f || padHeight <= 0.0f)
		return Result::none;
	const auto start = std::find_if (starts.begin(), starts.end(), [pointer] (const Start& s) { return s.pointer == pointer; });
	if (start == starts.end())
		return Result::none;

	// Each axis in shares of the pad's size along it, so the further one wins.
	const auto across = (x - start->x) / padWidth;
	const auto along = (y - start->y) / padHeight;
	if (std::max (std::abs (across), std::abs (along)) < travelShare)
		return Result::none;

	recognised = true;
	if (std::abs (across) >= std::abs (along))
		return across > 0.0f ? Result::right : Result::left;
	return along < 0.0f ? Result::up : Result::down;
}

bool OrbitSwipe::up (Pointer pointer)
{
	const auto known = std::any_of (starts.begin(), starts.end(), [pointer] (const Start& s) { return s.pointer == pointer; });
	if (! known)
		return false;
	starts.clear();
	return std::exchange (recognised, false);
}

bool OrbitSwipe::swiped() const
{
	return recognised;
}
