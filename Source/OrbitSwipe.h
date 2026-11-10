// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <utility>
#include <vector>

// The swipe on a stem's shape pads (ORBIT, CROSS, RISE, SCAT; named for the
// first pad that had one): sideways sets the stem's direction, up and down
// its tempo step. No hold before it (2026-11-10): the swipe is recognised on
// the move that takes a finger past travelShare of the pad -- of its width
// sideways, of its height up and down; the axis the finger went further along,
// in those shares, wins. One swipe per press; a press that never travels so
// far is a tap. Pure: the pad feeds it pointers and positions.
//
// Every touch reaches JUCE twice, as touch#0 and as X's emulated mouse#0
// (feedback_every-touch-arrives-twice): a pointer is (source type, index), and
// all pointers down on the pad between the first down and the first up are
// one press, so the twin neither swipes a second time nor ends a later press.
class OrbitSwipe
{
public:
	enum class Result { none, left, right, up, down };
	using Pointer = std::pair<int, int>;

	// A first guess, to be tried on the rig at 600 and 768 px.
	static constexpr float travelShare = 0.2f;

	void down (Pointer, float x, float y);
	// The swipe, on the move that makes it; none before it, after it, and for a
	// pointer that is not down here.
	Result drag (Pointer, float x, float y, float padWidth, float padHeight);
	// Ends the press at its first pointer up: true when it swiped (its click is
	// no press). A pointer that is not down here ends nothing.
	bool up (Pointer);
	// The press under way has swiped.
	bool swiped() const;

private:
	struct Start
	{
		Pointer pointer;
		float x, y;
	};

	std::vector<Start> starts;
	bool recognised = false;
};
