// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>

// DELETE needs two deliberate presses: there is no undo in the booth. The
// first press arms it, a second within two seconds confirms; a slower second
// press arms it again. Choosing another event drops the arm. Free of JUCE.
class DeleteArm
{
public:
	static constexpr double windowSeconds = 2.0;

	// Arms, or confirms when armed within the window.
	void press (double nowSeconds)
	{
		if (armed (nowSeconds))
		{
			armedAt.reset();
			confirmedNow = true;
			return;
		}
		armedAt = nowSeconds;
		confirmedNow = false;
	}

	bool armed (double nowSeconds) const
	{
		return armedAt.has_value() && nowSeconds >= *armedAt && nowSeconds - *armedAt <= windowSeconds;
	}

	// True after the confirming press, until the next press or drop.
	bool confirmed() const { return confirmedNow; }

	// The selection changed: nothing is armed for the new one.
	void drop()
	{
		armedAt.reset();
		confirmedNow = false;
	}

private:
	std::optional<double> armedAt;
	bool confirmedNow = false;
};
