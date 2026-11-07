// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>

#include "DeckMotion.h"
#include "StemSpatialPopup.h"

// A popup is a component: it needs JUCE's GUI initialised, no X window.
TEST (StemSpatialPopup, SlidersWriteTheControls)
{
	juce::ScopedJuceInitialiser_GUI init;
	DeckSpatial::Controls controls;
	StemSpatialPopup popup (controls, 2);
	popup.setSize (400, 200);

	popup.azimuth().setValue (-90.0, juce::sendNotificationSync);
	popup.elevation().setValue (30.0, juce::sendNotificationSync);
	popup.width().setValue (200.0, juce::sendNotificationSync);

	EXPECT_FLOAT_EQ (controls.azimuth[2].load(), -90.0f);
	EXPECT_FLOAT_EQ (controls.elevation[2].load(), 30.0f);
	EXPECT_FLOAT_EQ (controls.width[2].load(), 200.0f);

	for (int other : { 0, 1, 3 })   // only that stem's
	{
		EXPECT_FLOAT_EQ (controls.azimuth[(size_t) other].load(), 0.0f);
		EXPECT_FLOAT_EQ (controls.width[(size_t) other].load(), 60.0f);
	}
}

// The stem's own 3D is gone (2026-10-29): direction and width only.
TEST (StemSpatialPopup, HasNo3DRow)
{
	juce::ScopedJuceInitialiser_GUI init;
	DeckSpatial::Controls controls;
	StemSpatialPopup popup (controls, 0);
	juce::StringArray rows;
	for (auto* child : popup.getChildren())
		if (auto* label = dynamic_cast<juce::Label*> (child))
			rows.add (label->getText());
	EXPECT_EQ (rows, juce::StringArray ({ "AZ", "EL", "WIDTH" }));
}

TEST (StemSpatialPopup, StartsFromTheControlsAndKeepsIEMsRanges)
{
	juce::ScopedJuceInitialiser_GUI init;
	DeckSpatial::Controls controls;
	controls.azimuth[1] = 45.0f;
	controls.width[1] = 120.0f;
	StemSpatialPopup popup (controls, 1);

	EXPECT_DOUBLE_EQ (popup.azimuth().getValue(), 45.0);
	EXPECT_DOUBLE_EQ (popup.width().getValue(), 120.0);
	EXPECT_DOUBLE_EQ (popup.azimuth().getMinimum(), -180.0);
	EXPECT_DOUBLE_EQ (popup.elevation().getMaximum(), 90.0);
	EXPECT_DOUBLE_EQ (popup.width().getMaximum(), 360.0);

	// Opening it writes nothing back.
	EXPECT_FLOAT_EQ (controls.azimuth[1].load(), 45.0f);
}

TEST (StemSpatialPopup, PaintsEveryState)
{
	juce::ScopedJuceInitialiser_GUI init;
	DeckSpatial::Controls controls;
	for (const auto width : { 0.0f, 360.0f })
	{
		controls.width[0] = width;
		StemSpatialPopup popup (controls, 0);
		popup.setSize (400, 200);
		const auto image = popup.createComponentSnapshot (popup.getLocalBounds());
		EXPECT_EQ (image.getWidth(), 400);
	}
}

// Stage S3: the pads, the length keys and SHIFT go through the deck's motion
// queue; the audio thread's next block takes them.
TEST (DeckMotion, SetLengthReachesTheStemAndReadsBackBeforeAnyBlock)
{
	juce::ScopedJuceInitialiser_GUI init;   // the queue's producer is the message thread
	DeckSpatial::Controls controls, played;
	DeckMotion motion;
	const motionlite::Clock clock { 5.0, 120.0, 1.0, true };

	EXPECT_TRUE (motion.setLength (2, 0.5));
	EXPECT_DOUBLE_EQ (motion.lengthBars (2), 0.5) << "readable at once, as a restore is gathered before the first block";
	EXPECT_DOUBLE_EQ (motion.lengthBars (1), 4.0);
	motion.advance (clock, 0.0, controls, played);
	EXPECT_DOUBLE_EQ (motion.lengthBars (2), 0.5);

	motion.lengthUp (2);
	EXPECT_DOUBLE_EQ (motion.lengthBars (2), 1.0);
	motion.advance (clock, 0.0, controls, played);
	EXPECT_DOUBLE_EQ (motion.lengthBars (2), 1.0) << "the audio thread agrees with the mirror";

	motion.setLength (2, 3.0);
	motion.advance (clock, 0.0, controls, played);
	EXPECT_DOUBLE_EQ (motion.lengthBars (2), 4.0);
	EXPECT_FALSE (motion.setLength (9, 2.0));
}

TEST (DeckMotion, ARestoredLengthIsTheOneTheNextShotUses)
{
	juce::ScopedJuceInitialiser_GUI init;   // the queue's producer is the message thread
	DeckSpatial::Controls controls, played;
	DeckMotion motion;
	motion.setLength (0, 1.0);
	motion.press (0, motionlite::Shape::orbit, true, 1);
	motion.advance ({ 5.0, 120.0, 1.0, true }, 0.0, controls, played);
	EXPECT_EQ (motion.progress (0), (std::pair<int, int> { 1, 4 }));   // 1 bar = 4 beats
}

TEST (StemSpatialPopup, PadsPressThroughTheMotionQueue)
{
	juce::ScopedJuceInitialiser_GUI init;
	DeckSpatial::Controls controls, played;
	DeckMotion motion;
	StemSpatialPopup popup (controls, 1, &motion);
	popup.setSize (400, 400);
	const motionlite::Clock clock { 5.0, 120.0, 1.0, true };

	popup.lengthUp().onClick();
	popup.pad (motionlite::Shape::ping).onClick();
	motion.advance (clock, 0.0, controls, played);
	EXPECT_EQ (motion.padState (1, motionlite::Shape::ping), motionlite::PadState::armed);
	EXPECT_DOUBLE_EQ (motion.lengthBars (1), 8.0);
	EXPECT_EQ (motion.stemState (0), motionlite::PadState::idle) << "only that stem's";

	popup.shift().setToggleState (true, juce::dontSendNotification);
	popup.pad (motionlite::Shape::orbit).onClick();
	motion.advance (clock, 0.0, controls, played);
	EXPECT_EQ (motion.padState (1, motionlite::Shape::orbit), motionlite::PadState::running);

	popup.refresh();
	EXPECT_EQ (popup.lengthText(), juce::String ("8 BARS"));
	const auto image = popup.createComponentSnapshot (popup.getLocalBounds());
	EXPECT_EQ (image.getWidth(), 400);
}

namespace
{
	// A finger on the ORBIT pad at x (pad coordinates), as the pad sees it.
	juce::MouseEvent fingerAt (juce::Component& pad, float x, juce::ModifierKeys mods)
	{
		const auto source = juce::Desktop::getInstance().getMainMouseSource();
		const juce::Point<float> where { x, (float) pad.getHeight() * 0.5f };
		return juce::MouseEvent (source, where, mods, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &pad, &pad,
								 juce::Time::getCurrentTime(), where, juce::Time::getCurrentTime(), 1, false);
	}

	struct FakeClock { double seconds = 0.0; };

	// down at the pad's middle, wait `holdFor`, move to `toShare` of the width, release.
	void swipeOrbit (SwipePad& pad, FakeClock& clock, double holdFor, float toShare)
	{
		const auto down = juce::ModifierKeys::leftButtonModifier;
		const auto mid = (float) pad.getWidth() * 0.5f;
		pad.mouseDown (fingerAt (pad, mid, down));
		clock.seconds += holdFor;
		pad.mouseDrag (fingerAt (pad, mid, down));
		clock.seconds += 0.1;
		pad.mouseDrag (fingerAt (pad, (float) pad.getWidth() * toShare, down));
		pad.mouseUp (fingerAt (pad, (float) pad.getWidth() * toShare, {}));
	}
}

TEST (StemSpatialPopup, OrbitFaceShowsTheDirection)
{
	juce::ScopedJuceInitialiser_GUI init;
	DeckSpatial::Controls controls;
	DeckMotion motion;
	StemSpatialPopup popup (controls, 1, &motion);
	popup.setSize (400, 400);

	EXPECT_EQ (popup.pad (motionlite::Shape::orbit).getButtonText(), juce::String::fromUTF8 ("ORBIT \xe2\x96\xba"));
	const auto forward = popup.createComponentSnapshot (popup.getLocalBounds());

	motion.setReversed (1, true);
	popup.refresh();
	EXPECT_EQ (popup.pad (motionlite::Shape::orbit).getButtonText(), juce::String::fromUTF8 ("\xe2\x97\x84 ORBIT"));
	const auto reversed = popup.createComponentSnapshot (popup.getLocalBounds());

	EXPECT_EQ (forward.getWidth(), 400);
	EXPECT_EQ (reversed.getWidth(), 400);
	EXPECT_EQ (popup.pad (motionlite::Shape::ping).getButtonText(), "PING") << "only ORBIT shows a direction";
}

TEST (StemSpatialPopup, HoldAndSwipeSetsTheDirectionWithoutPressing)
{
	juce::ScopedJuceInitialiser_GUI init;
	DeckSpatial::Controls controls, played;
	DeckMotion motion;
	StemSpatialPopup popup (controls, 1, &motion);
	popup.setSize (400, 400);
	FakeClock clock;
	auto& orbit = popup.orbitPad();
	orbit.secondsNow = [&clock] { return clock.seconds; };

	swipeOrbit (orbit, clock, 0.5, 0.0f);   // to the left edge
	EXPECT_TRUE (motion.reversed (1));
	EXPECT_EQ (orbit.getButtonText(), juce::String::fromUTF8 ("\xe2\x97\x84 ORBIT"));

	swipeOrbit (orbit, clock, 0.5, 1.0f);   // and back to the right
	EXPECT_FALSE (motion.reversed (1));
	EXPECT_EQ (orbit.getButtonText(), juce::String::fromUTF8 ("ORBIT \xe2\x96\xba"));

	motion.advance ({ 5.0, 120.0, 1.0, true }, 0.0, controls, played);
	EXPECT_EQ (motion.padState (1, motionlite::Shape::orbit), motionlite::PadState::idle) << "a swipe presses nothing";
}

TEST (StemSpatialPopup, APlainTapStillPressesOrbit)
{
	juce::ScopedJuceInitialiser_GUI init;
	DeckSpatial::Controls controls, played;
	DeckMotion motion;
	StemSpatialPopup popup (controls, 1, &motion);
	popup.setSize (400, 400);
	const motionlite::Clock clock { 5.0, 120.0, 1.0, true };

	popup.shift().setToggleState (true, juce::dontSendNotification);
	popup.orbitPad().onClick();   // what Button::mouseUp calls on a tap
	motion.advance (clock, 0.0, controls, played);
	EXPECT_EQ (motion.padState (1, motionlite::Shape::orbit), motionlite::PadState::running);
	EXPECT_FALSE (motion.reversed (1));
}
