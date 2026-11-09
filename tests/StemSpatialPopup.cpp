// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>

#include "DeckMotion.h"
#include "StemSpatialPopup.h"

#include <cstdint>
#include <utility>
#include <vector>

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

// 3D REC hears each press with what the motion was given: shape, SHIFT, seed.
TEST (OneShotKeys, APressTellsTheRecorderWhatItPressed)
{
	juce::ScopedJuceInitialiser_GUI init;
	DeckMotion motion;
	bool shiftNext = true;
	OneShotKeys keys (motion, 2, [&shiftNext] { return std::exchange (shiftNext, false); });
	struct Heard { motionlite::Shape shape; bool shift; std::uint64_t seed; };
	std::vector<Heard> heard;
	keys.onPressed = [&heard] (motionlite::Shape shape, bool shift, std::uint64_t seed) { heard.push_back ({ shape, shift, seed }); };

	keys.press (motionlite::Shape::scat);
	keys.press (motionlite::Shape::ping);
	ASSERT_EQ (heard.size(), 2u);
	EXPECT_EQ (heard[0].shape, motionlite::Shape::scat);
	EXPECT_TRUE (heard[0].shift);
	EXPECT_EQ (heard[1].shape, motionlite::Shape::ping);
	EXPECT_FALSE (heard[1].shift) << "SHIFT lets go after one press";
	EXPECT_NE (heard[0].seed, heard[1].seed) << "each press its own seed";

	DeckSpatial::Controls controls, played;
	motion.advance ({ 5.0, 120.0, 1.0, true }, 0.0, controls, played);
	EXPECT_EQ (motion.padState (2, motionlite::Shape::scat), motionlite::PadState::running) << "and the press still played live";
}

// A press the full motion queue refused never plays: 3D REC must not keep it.
TEST (OneShotKeys, APressTheFullQueueRefusedIsNotRecorded)
{
	juce::ScopedJuceInitialiser_GUI init;
	DeckMotion motion;
	OneShotKeys keys (motion, 0, [] { return false; });
	int heard = 0;
	keys.onPressed = [&heard] (motionlite::Shape, bool, std::uint64_t) { ++heard; };

	for (int i = 0; i < DeckMotion::queueCapacity; ++i)
		ASSERT_TRUE (motion.lengthUp (1)) << "filling the queue, " << i;
	ASSERT_FALSE (motion.press (1, motionlite::Shape::ping, false, 1)) << "the queue is full";

	keys.press (motionlite::Shape::orbit);
	EXPECT_EQ (heard, 0);
}

// The two-deck popup hands each press on, so its deck can record it.
TEST (StemSpatialPopup, HandsEachPressOnForTheRecorder)
{
	juce::ScopedJuceInitialiser_GUI init;
	DeckSpatial::Controls controls;
	DeckMotion motion;
	StemSpatialPopup popup (controls, 3, &motion);
	popup.setSize (400, 400);
	std::vector<motionlite::Shape> heard;
	popup.setOnPressed ([&heard] (motionlite::Shape shape, bool, std::uint64_t) { heard.push_back (shape); });

	popup.pad (motionlite::Shape::rise).onClick();
	EXPECT_EQ (heard, std::vector<motionlite::Shape> { motionlite::Shape::rise });
}

// 3D REC keeps only the presses that start or arm a shot: one that stops the
// running shape, or takes back an armed press, is no new event (it would
// restart the stem the DJ just stopped, or hit the taken slot).
TEST (OneShotKeys, OnlyAPressOnAnIdlePadIsRecorded)
{
	juce::ScopedJuceInitialiser_GUI init;
	DeckMotion motion;
	DeckSpatial::Controls controls, played;
	bool shiftNext = false;
	OneShotKeys keys (motion, 0, [&shiftNext] { return std::exchange (shiftNext, false); });
	std::vector<motionlite::Shape> heard;
	keys.onPressed = [&heard] (motionlite::Shape shape, bool, std::uint64_t) { heard.push_back (shape); };
	const auto blockAt = [&] (double beat) { motion.advance ({ beat, 120.0, 1.0, true }, 0.0, controls, played); };

	blockAt (5.0);
	keys.press (motionlite::Shape::orbit);
	blockAt (5.0);
	ASSERT_EQ (motion.padState (0, motionlite::Shape::orbit), motionlite::PadState::armed);
	EXPECT_EQ (heard.size(), 1u) << "the first press arms: recorded";

	keys.press (motionlite::Shape::orbit);
	blockAt (5.0);
	ASSERT_EQ (motion.padState (0, motionlite::Shape::orbit), motionlite::PadState::idle);
	EXPECT_EQ (heard.size(), 1u) << "taking the armed press back: not recorded";

	keys.press (motionlite::Shape::orbit);
	blockAt (7.9);
	blockAt (8.0);
	ASSERT_EQ (motion.padState (0, motionlite::Shape::orbit), motionlite::PadState::running);
	EXPECT_EQ (heard.size(), 2u);

	keys.press (motionlite::Shape::orbit);
	blockAt (8.0);
	EXPECT_EQ (heard.size(), 2u) << "stopping the running ORBIT: not recorded";

	shiftNext = true;
	keys.press (motionlite::Shape::ping);
	EXPECT_EQ (heard.size(), 3u) << "SHIFT on an idle pad: recorded";
	EXPECT_EQ (heard.back(), motionlite::Shape::ping);
}

// 3D REC in pause: a press only places an event. Nothing goes into the motion
// queue, so nothing plays now and nothing stays armed for when play is pressed.
TEST (OneShotKeys, APlacingPressReachesTheRecorderAndNotTheMotion)
{
	juce::ScopedJuceInitialiser_GUI init;
	DeckMotion motion;
	DeckSpatial::Controls controls, played;
	bool shiftNext = true;
	OneShotKeys keys (motion, 1, [&shiftNext] { return std::exchange (shiftNext, false); });
	keys.placesOnly = [] { return true; };
	std::vector<motionlite::Shape> heard;
	keys.onPressed = [&heard] (motionlite::Shape shape, bool, std::uint64_t) { heard.push_back (shape); };

	keys.press (motionlite::Shape::orbit);
	keys.press (motionlite::Shape::scat);
	EXPECT_EQ (heard, (std::vector<motionlite::Shape> { motionlite::Shape::orbit, motionlite::Shape::scat }));
	EXPECT_FALSE (shiftNext) << "SHIFT lets go after one press, as always";

	motion.advance ({ 7.5, 120.0, 1.0, false }, 0.0, controls, played);   // standing
	motion.advance ({ 7.9, 120.0, 1.0, true }, 0.0, controls, played);    // play pressed
	motion.advance ({ 8.1, 120.0, 1.0, true }, 0.0, controls, played);    // over the bar line
	EXPECT_EQ (motion.stemState (1), motionlite::PadState::idle) << "nothing armed, nothing running";
}

// The two-deck popup hands the deck's "only place" on to its pads.
TEST (StemSpatialPopup, APlacingPopupPressPlaysNothing)
{
	juce::ScopedJuceInitialiser_GUI init;
	DeckSpatial::Controls controls, played;
	DeckMotion motion;
	StemSpatialPopup popup (controls, 2, &motion);
	popup.setSize (400, 400);
	int heard = 0;
	popup.setOnPressed ([&heard] (motionlite::Shape, bool, std::uint64_t) { ++heard; });
	popup.setPlacesOnly ([] { return true; });

	popup.pad (motionlite::Shape::ping).onClick();
	motion.advance ({ 3.9, 120.0, 1.0, true }, 0.0, controls, played);
	motion.advance ({ 4.1, 120.0, 1.0, true }, 0.0, controls, played);
	EXPECT_EQ (heard, 1);
	EXPECT_EQ (motion.stemState (2), motionlite::PadState::idle);
}
