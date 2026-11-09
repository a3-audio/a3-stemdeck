// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "MotionLite.h"
#include "OrbitSwipe.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>

class DeckMotion;

// A pad that also reads "hold, then swipe sideways" (OrbitSwipe). A swipe
// calls `onSwipe` and takes the click away; a tap (or a hold that never
// moves) clicks as any button does. Without `onSwipe` it is a plain button.
// It overrides the mouse handlers rather than listening from outside because
// Button clicks inside its own mouseUp, before any MouseListener hears it.
class SwipePad : public juce::TextButton
{
public:
	std::function<void (OrbitSwipe::Result)> onSwipe;
	std::function<double()> secondsNow = [] { return juce::Time::getMillisecondCounterHiRes() / 1000.0; };

	void mouseDown (const juce::MouseEvent&) override;
	void mouseDrag (const juce::MouseEvent&) override;
	void mouseUp (const juce::MouseEvent&) override;
	// True while the click this release would make belongs to a swipe: onClick asks.
	bool isSwipeClick() const { return swallowClick; }

private:
	OrbitSwipe swipe;
	bool swallowClick = false;
};

// One stem's one-shot keys, as both screens show them: ORBIT, PING, RISE,
// SCAT (idle plain, armed blinking on the deck's beat, running in the stem's
// colour with its beat count), the length keys and the length between them.
// The keys are children of whatever adds them and placed by it: the two-deck
// call-out lays them in a row, the one-deck stem column two by two. A press
// asks `takeShift` whether it starts now (SHIFT; the key lets go after one
// press) and goes through the deck's motion queue.
class OneShotKeys
{
public:
	OneShotKeys (DeckMotion&, int stem, std::function<bool()> takeShift);

	void addTo (juce::Component& parent);
	void setStemColour (juce::Colour);
	// Reads what the audio thread last did into the faces and the length.
	void refresh();

	// Every press on an idle pad (one that starts or arms a shot) as it went
	// into the motion queue, with its SHIFT and seed: the deck records it as an
	// event while 3D REC is on (DeckHost::recordPress). A press that stops or
	// takes back a shot, and one the full queue refused, are not reported.
	std::function<void (motionlite::Shape, bool shift, std::uint64_t seed)> onPressed;

	// True while a press only places an event and plays nothing (3D REC on,
	// the deck standing: DeckHost::placesPresses). Such a press never reaches
	// the motion queue, so nothing moves now and nothing is left armed for
	// when the deck plays; it goes to onPressed whatever the pad's state.
	std::function<bool()> placesOnly;

	// What a click on the key does, now. triggerClick() answers asynchronously,
	// after a SHIFT release in the same frame would have taken the shift away.
	void press (motionlite::Shape);

	juce::TextButton& pad (motionlite::Shape shape) { return pads[(size_t) shape]; }
	SwipePad& orbitPad() { return pads[(size_t) motionlite::Shape::orbit]; }
	juce::TextButton& lengthDown() { return shorter; }
	juce::TextButton& lengthUp() { return longer; }
	juce::Label& length() { return lengthLabel; }

	// ORBIT's face shows its direction: "ORBIT \u25ba" forward, "\u25c4 ORBIT" reversed.
	static juce::String shapeName (motionlite::Shape, bool reversed = false);
	static juce::String lengthName (double bars);   // "½ BAR", "1 BAR", "4 BARS"

private:
	void lookOfPad (motionlite::Shape);
	void setDirection (OrbitSwipe::Result);

	DeckMotion& motion;
	const int stem;
	std::function<bool()> takeShift;
	juce::Colour stemColour;

	std::array<SwipePad, 4> pads;
	juce::TextButton shorter, longer;
	juce::Label lengthLabel;

	JUCE_DECLARE_NON_COPYABLE (OneShotKeys)
};
