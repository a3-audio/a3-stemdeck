// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "MotionLite.h"
#include "OrbitSwipe.h"
#include "StemEvents.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <optional>

class DeckMotion;

// A pad that also reads a swipe (OrbitSwipe): the moment a finger travels far
// enough it calls `onSwipe`, and that press clicks no more, neither then nor
// at the release. A tap clicks as any button does, at the release. Without
// `onSwipe` it is a plain button. It overrides the mouse handlers rather than
// listening from outside because Button clicks inside its own mouseUp, before
// any MouseListener hears it.
class SwipePad : public juce::TextButton
{
public:
	std::function<void (OrbitSwipe::Result)> onSwipe;

	void mouseDown (const juce::MouseEvent&) override;
	void mouseDrag (const juce::MouseEvent&) override;
	void mouseUp (const juce::MouseEvent&) override;
	// True from the swipe to the end of its press: a click then belongs to the
	// swipe, and onClick asks.
	bool isSwipeClick() const { return swallowClick; }

private:
	static OrbitSwipe::Pointer pointerOf (const juce::MouseEvent&);

	OrbitSwipe swipe;
	bool swallowClick = false;
};

// One stem's one-shot keys, as both screens show them: ORBIT, CROSS, RISE,
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

	// The event the keys show and change instead of the stem's settings: the
	// stem's event at the playhead while the deck stands (EventStrip::shownShot).
	// While `shown` has one, the length keys and the swipes change it, a pad
	// picks its shape (a placing press still places), the faces show it: its
	// shape's pad lit in the stem's colour, its length, tempo step and direction.
	// `edit` is false when the list refused. Without one, the stem's settings.
	struct EventLink
	{
		std::function<std::optional<stemevents::Shot>()> shown;
		std::function<bool (const stemevents::Shot&)> edit;
	};
	EventLink event;

	// What a click on the key does, now. triggerClick() answers asynchronously,
	// after a SHIFT release in the same frame would have taken the shift away.
	// A pad presses at the release of a tap (Button's default), so a finger
	// that goes on to swipe has pressed nothing.
	void press (motionlite::Shape);
	// What a swipe on any shape pad does: sideways the stem's direction (left
	// reversed), up a tempo step faster, down one slower. Shown at once.
	void swiped (OrbitSwipe::Result);

	SwipePad& pad (motionlite::Shape shape) { return pads[(size_t) shape]; }
	SwipePad& orbitPad() { return pad (motionlite::Shape::orbit); }
	juce::TextButton& lengthDown() { return shorter; }
	juce::TextButton& lengthUp() { return longer; }
	juce::Label& length() { return lengthLabel; }
	// The stem's tempo step, "x1" dimmed at the default.
	juce::Label& tempo() { return tempoLabel; }

	// ORBIT's face is its letter and its direction: "< O" forward, "O >" reversed.
	static juce::String shapeName (motionlite::Shape, bool reversed = false);
	static juce::String lengthName (double bars);   // "¼ BAR", "½ BAR", "1 BAR", "4 BARS"
	static juce::String speedName (double speed);   // "x¼", "x½", "x1", "x2", "x4"

private:
	void lookOfPad (motionlite::Shape);
	void lookOfEventPad (motionlite::Shape, const stemevents::Shot&);
	void showTempo();
	std::optional<stemevents::Shot> shownEvent() const;
	// False without a shown event: the key is the stem's.
	bool editEvent (stemevents::ShotKey);
	bool editEventShape (motionlite::Shape);
	void editEvent (const stemevents::Shot&);
	void setDirection (bool reversed);
	void setSpeed (double speed);

	DeckMotion& motion;
	const int stem;
	std::function<bool()> takeShift;
	juce::Colour stemColour;

	std::array<SwipePad, 4> pads;
	juce::TextButton shorter, longer;
	juce::Label lengthLabel, tempoLabel;

	JUCE_DECLARE_NON_COPYABLE (OneShotKeys)
};
