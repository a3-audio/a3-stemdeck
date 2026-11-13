// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "OneShotKeys.h"

#include "DeckMotion.h"
#include "KnobSetup.h"

#include <cmath>

namespace
{
	// A pad's face: armed blinks on the deck's beat between these two, running is the stem's colour.
	constexpr float armedLit = 0.55f, armedDim = 0.2f, armedNoBeat = 0.35f;
	// The tempo step at x1 (the default) is there but quiet.
	constexpr float defaultTempoAlpha = 0.4f;
	constexpr int shapeCount = 4;

	const std::array<const char*, shapeCount> padNames { "ORBIT", "CROSS", "RISE", "SCAT" };   // CROSS is Shape::ping

	void setUpKey (juce::TextButton& key, const juce::String& text)
	{
		key.setButtonText (text);
		knobsetup::untabbableKey (key);
	}

	// What a swipe does to a shot: sideways the direction (left reversed), up faster, down slower.
	stemevents::ShotKey swipeKey (OrbitSwipe::Result result)
	{
		using Result = OrbitSwipe::Result;
		using Key = stemevents::ShotKey;
		switch (result)
		{
			case Result::left:  return Key::reverse;
			case Result::right: return Key::forward;
			case Result::up:    return Key::faster;
			case Result::down:
			case Result::none:  break;
		}
		return Key::slower;
	}

	juce::String barsNumber (double bars)
	{
		if (bars < 0.5)
			return juce::String::fromUTF8 ("\xc2\xbc");   // U+00BC, beside the half in DejaVu Sans
		return bars < 1.0 ? juce::String::fromUTF8 ("\xc2\xbd") : juce::String ((int) bars);
	}
}

OrbitSwipe::Pointer SwipePad::pointerOf (const juce::MouseEvent& e)
{
	return { (int) e.source.getType(), e.source.getIndex() };
}

void SwipePad::mouseDown (const juce::MouseEvent& e)
{
	if (! swipe.swiped())
		swallowClick = false;   // a press of its own, not the twin of a swiping one
	swipe.down (pointerOf (e), e.position.x, e.position.y);
	TextButton::mouseDown (e);
}

void SwipePad::mouseDrag (const juce::MouseEvent& e)
{
	const auto result = swipe.drag (pointerOf (e), e.position.x, e.position.y, (float) getWidth(), (float) getHeight());
	TextButton::mouseDrag (e);
	if (result == OrbitSwipe::Result::none || onSwipe == nullptr)
		return;
	swallowClick = true;
	onSwipe (result);
}

// The click happens inside TextButton::mouseUp (synchronously): a swiped
// press keeps the flag through it, and lets it go after.
void SwipePad::mouseUp (const juce::MouseEvent& e)
{
	const auto swiped = swipe.up (pointerOf (e));
	TextButton::mouseUp (e);
	if (swiped)
		swallowClick = false;
}

OneShotKeys::OneShotKeys (DeckMotion& deckMotion, int stemIndex, std::function<bool()> shift)
	: motion (deckMotion), stem (stemIndex), takeShift (std::move (shift))
{
	for (size_t p = 0; p < pads.size(); ++p)
	{
		setUpKey (pads[p], shapeName ((motionlite::Shape) p));
		pads[p].onClick = [this, p]
		{
			if (! pads[p].isSwipeClick())
				press ((motionlite::Shape) p);
		};
		pads[p].onSwipe = [this] (OrbitSwipe::Result result) { swiped (result); };
	}
	setUpKey (shorter, juce::String::fromUTF8 ("\xe2\x97\x82"));
	setUpKey (longer, juce::String::fromUTF8 ("\xe2\x96\xb8"));
	shorter.onClick = [this]
	{
		if (! editEvent (stemevents::ShotKey::shorter))
			motion.lengthDown (stem);
	};
	longer.onClick = [this]
	{
		if (! editEvent (stemevents::ShotKey::longer))
			motion.lengthUp (stem);
	};
	lengthLabel.setJustificationType (juce::Justification::centred);
	tempoLabel.setJustificationType (juce::Justification::centred);
	tempoLabel.setTooltip ("The stem's tempo: swipe a pad up (faster) or down (slower)");
}

void OneShotKeys::addTo (juce::Component& parent)
{
	for (auto& key : pads)
		parent.addAndMakeVisible (key);
	parent.addAndMakeVisible (shorter);
	parent.addAndMakeVisible (longer);
	parent.addAndMakeVisible (lengthLabel);
	parent.addAndMakeVisible (tempoLabel);
}

void OneShotKeys::setStemColour (juce::Colour colour)
{
	stemColour = colour;
	refresh();
}

juce::String OneShotKeys::shapeName (motionlite::Shape shape, bool reversed)
{
	if (shape != motionlite::Shape::orbit)
		return padNames[(size_t) shape];
	// The letter and the direction, as the orbit's markers on the waveform show it.
	return reversed ? "O >" : "< O";
}

juce::String OneShotKeys::lengthName (double bars)
{
	return barsNumber (bars) + (bars > 1.0 ? " BARS" : " BAR");
}

juce::String OneShotKeys::speedName (double speed)
{
	return "x" + barsNumber (motionlite::validSpeed (speed));   // the length's quarter and half
}

void OneShotKeys::press (motionlite::Shape shape)
{
	const auto places = placesOnly != nullptr && placesOnly();
	if (! places && editEventShape (shape))
		return;
	const auto now = takeShift != nullptr && takeShift();
	const auto seed = (std::uint64_t) juce::Random::getSystemRandom().nextInt64();
	if (places)
	{
		if (onPressed != nullptr)
			onPressed (shape, now, seed);
		return;
	}
	// Only a press that starts or arms a shot is an event: one that stops the
	// running shape or takes back an armed press would restart the stem or
	// hit the taken slot. A press the full queue refused never plays, so it is
	// not recorded either.
	const bool startsAShot = motion.padState (stem, shape) == motionlite::PadState::idle;
	if (motion.press (stem, shape, now, seed) && startsAShot && onPressed != nullptr)
		onPressed (shape, now, seed);
}

void OneShotKeys::swiped (OrbitSwipe::Result result)
{
	using Result = OrbitSwipe::Result;
	if (result == Result::none || editEvent (swipeKey (result)))
		return;
	switch (result)
	{
		case Result::left:  setDirection (true); break;
		case Result::right: setDirection (false); break;
		case Result::up:    setSpeed (motionlite::fasterSpeed (motion.speed (stem))); break;
		case Result::down:  setSpeed (motionlite::slowerSpeed (motion.speed (stem))); break;
		case Result::none:  break;
	}
}

void OneShotKeys::setDirection (bool reversed)
{
	if (motion.reversed (stem) == reversed)
		return;
	motion.setReversed (stem, reversed);
	lookOfPad (motionlite::Shape::orbit);
}

void OneShotKeys::setSpeed (double speed)
{
	if (std::abs (motion.speed (stem) - speed) < 1e-9)
		return;
	motion.setSpeed (stem, speed);
	showTempo();
}

std::optional<stemevents::Shot> OneShotKeys::shownEvent() const
{
	if (event.shown == nullptr)
		return std::nullopt;
	return event.shown();
}

bool OneShotKeys::editEvent (stemevents::ShotKey key)
{
	const auto shot = shownEvent();
	if (! shot)
		return false;
	editEvent (stemevents::afterKey (*shot, key));
	return true;
}

bool OneShotKeys::editEventShape (motionlite::Shape shape)
{
	auto shot = shownEvent();
	if (! shot)
		return false;
	shot->shape = shape;
	editEvent (*shot);
	return true;
}

// A key at its end of the range changes nothing: setShot refuses, nothing is saved.
void OneShotKeys::editEvent (const stemevents::Shot& changed)
{
	if (event.edit != nullptr)
		event.edit (changed);
	refresh();
}

void OneShotKeys::refresh()
{
	const auto shot = shownEvent();
	lengthLabel.setText (lengthName (shot ? shot->lengthBars : motion.lengthBars (stem)), juce::dontSendNotification);
	showTempo();
	for (int p = 0; p < shapeCount; ++p)
	{
		if (shot)
			lookOfEventPad ((motionlite::Shape) p, *shot);
		else
			lookOfPad ((motionlite::Shape) p);
	}
}

void OneShotKeys::showTempo()
{
	const auto shot = shownEvent();
	const auto speed = shot ? shot->speed : motion.speed (stem);
	const auto ink = tempoLabel.getLookAndFeel().findColour (juce::Label::textColourId);
	tempoLabel.setText (speedName (speed), juce::dontSendNotification);
	const bool atDefault = std::abs (speed - motionlite::defaultSpeed) < 1e-9;
	tempoLabel.setColour (juce::Label::textColourId, atDefault ? ink.withMultipliedAlpha (defaultTempoAlpha) : ink);
}

// The event's face: its shape lit in the stem's colour, as a running shot is
// (what the event does is what the pad shows), the others plain. Not the
// stem's live states: the keys are the event's while it is shown.
void OneShotKeys::lookOfEventPad (motionlite::Shape shape, const stemevents::Shot& shot)
{
	auto& key = pads[(size_t) shape];
	const auto lit = shape == shot.shape;
	const auto plain = key.getLookAndFeel().findColour (juce::TextButton::buttonColourId);
	const auto text = key.getLookAndFeel().findColour (juce::TextButton::textColourOffId);
	key.setColour (juce::TextButton::buttonColourId, lit ? stemColour : plain);
	key.setColour (juce::TextButton::textColourOffId, lit ? stemColour.contrasting() : text);
	key.setButtonText (shapeName (shape, shot.reversed));
}

// Identity stays (name, place); the state is the face: idle plain, armed
// blinking on the deck's beat, running in the stem's colour with its beat count.
void OneShotKeys::lookOfPad (motionlite::Shape shape)
{
	auto& key = pads[(size_t) shape];
	const auto state = motion.padState (stem, shape);
	const auto plain = key.getLookAndFeel().findColour (juce::TextButton::buttonColourId);
	const auto text = key.getLookAndFeel().findColour (juce::TextButton::textColourOffId);
	auto name = shapeName (shape, motion.reversed (stem));

	auto face = plain;
	auto ink = text;
	if (state == motionlite::PadState::armed)
	{
		const auto beat = motion.beat();
		const auto alpha = ! beat ? armedNoBeat : (*beat - std::floor (*beat) < 0.5 ? armedLit : armedDim);
		face = plain.interpolatedWith (stemColour, alpha);
	}
	else if (state == motionlite::PadState::running)
	{
		face = stemColour;
		ink = stemColour.contrasting();
		if (const auto progress = motion.progress (stem))
			name << " " << progress->first << "/" << progress->second;
	}
	key.setColour (juce::TextButton::buttonColourId, face);
	key.setColour (juce::TextButton::textColourOffId, ink);
	key.setButtonText (name);
}
