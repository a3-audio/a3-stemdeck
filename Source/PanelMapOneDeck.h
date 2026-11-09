// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "MotionLite.h"
#include "PanelMap.h"

#include <array>
#include <optional>
#include <vector>

// What the A³ Motion panel means to a one-deck (-bformat) StemDeck, and what
// it shows (spec section 10, B16): the stems stand under their encoders. Pure:
// no JUCE, testable.
//
//   col 0:  row 0 CUE, row 1 PLAY, row 2 LOOP, row 3 LOOP IN, row 4 LOOP OUT
//           (the screen's loop block on end, DeckLoop.h), row 5 CONTINUE
//           (PlayMode.h; REPEAT stays the screen's)
//   col 9:  row 0 SHIFT (held), rows 1-5 dark
//   stem s (0-3): pad columns 2s+1 and 2s+2
//       row 2  ORBIT  CROSS
//       row 3  RISE   SCAT
//       row 4  CUE    MUTE
//       row 5  <      >      (the one-shot length)
//   stem s's encoders: the upper one is GAIN (its push mutes; twice quickly
//   it resets GAIN, the second push unmuting again), the lower is WIDTH
//   (its push does nothing). The pots are AUX of stems 1-4 (pot n is
//   stem n's send), absolute: the reading is the value at once, the deck
//   wins over the screen (2026-10-29: no pick-up).
namespace panel1
{
	using panel::Cell;
	using panel::Rgb;

	struct Control
	{
		enum class Kind { none, cue, play, shift, loop, loopIn, loopOut, continuePlay, orbit, ping, rise, scat, stemCue, stemMute, lengthDown, lengthUp };
		Kind kind = Kind::none;
		int stem = -1;
	};

	Control controlAt (Cell cell);
	// Where a control's key is; {-1, -1} for none.
	Cell cellOf (const Control& control);

	struct StemKnob { int stem = -1; bool width = false; };
	// encoder % 4 is the stem; the upper four (>= 4) turn GAIN, the lower WIDTH.
	StemKnob encoderStem (int encoder);

	struct Event
	{
		enum class Type { cueDown, cueUp, play, shiftDown, shiftUp, loop, loopIn, loopOut, continuePlay, shot, stemCue, stemMute, lengthDown, lengthUp, gain, gainPush, width, aux };
		Type type = Type::play;
		int stem = -1;
		motionlite::Shape shape = motionlite::Shape::orbit;   // shot
		double value = 0.0;                                   // gain, width: encoder detents, + clockwise; aux: the pot's reading, 0..1
	};

	// Turns polled frames into events: a key reports its press (CUE and SHIFT
	// their release too) once, not on every poll it is held.
	class InputDecoder
	{
	public:
		std::vector<Event> feed (const panel::Frame& frame);
		void reset() { tracker.reset(); potAt = {}; }

	private:
		panel::EdgeTracker tracker;
		std::array<std::optional<int>, panel::potCount> potAt {};
	};

	//==============================================================================
	struct StemLeds
	{
		std::array<motionlite::PadState, 4> pads {};   // by motionlite::Shape
		bool cued = false, muted = false;
		bool shortest = false, longest = false;         // the one-shot length is at an end of its range
	};

	// Everything the panel shows, as plain values: the screen's state is read
	// into this by the owner, never passed in as a component.
	struct LedState
	{
		std::array<StemLeds, 4> stems {};
		std::array<Rgb, 4> stemColours {};
		Rgb deckColour, cueColour, playColour, muteColour, loopColour, playModeColour;
		bool playing = false, atCue = false, shift = false;
		// The loop keys' lights, as the screen's: looping, the deck's own
		// state (StemDeckPlayer::hasLoop); the IN mark, DeckLoop's.
		bool looping = false, loopInMarked = false;
		bool continuing = false;   // CONTINUE on
	};

	// Every LED as wanted. The deck's own keys rest in the deck colour, a
	// stem's keys in that stem's colour. Armed pads blink with a period of 500 ms.
	panel::Leds leds (const LedState& state, double nowSeconds);
}
