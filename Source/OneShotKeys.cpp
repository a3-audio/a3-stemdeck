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
	constexpr int shapeCount = 4;

	const std::array<const char*, shapeCount> padNames { "ORBIT", "PING", "RISE", "SCAT" };

	void setUpKey (juce::TextButton& key, const juce::String& text)
	{
		key.setButtonText (text);
		knobsetup::untabbableKey (key);
	}

	juce::String barsNumber (double bars)
	{
		return bars < 1.0 ? juce::String::fromUTF8 ("\xc2\xbd") : juce::String ((int) bars);
	}
}

void SwipePad::mouseDown (const juce::MouseEvent& e)
{
	swipe.down (secondsNow(), e.position.x);
	TextButton::mouseDown (e);
}

void SwipePad::mouseDrag (const juce::MouseEvent& e)
{
	swipe.drag (secondsNow(), e.position.x);
	TextButton::mouseDrag (e);
}

// The click happens inside TextButton::mouseUp (synchronously), so the flag
// is set before it and cleared after it, whether or not the click came.
void SwipePad::mouseUp (const juce::MouseEvent& e)
{
	const auto result = swipe.up ((float) getWidth());
	const auto swiped = onSwipe != nullptr && result != OrbitSwipe::Result::none;
	swallowClick = swiped;
	TextButton::mouseUp (e);
	swallowClick = false;
	if (swiped)
		onSwipe (result);
}

OneShotKeys::OneShotKeys (DeckMotion& deckMotion, int stemIndex, std::function<bool()> shift)
	: motion (deckMotion), stem (stemIndex), takeShift (std::move (shift))
{
	for (size_t p = 0; p < pads.size(); ++p)
	{
		setUpKey (pads[p], padNames[p]);
		pads[p].onClick = [this, p] { press ((motionlite::Shape) p); };
	}
	orbitPad().onClick = [this]
	{
		if (! orbitPad().isSwipeClick())
			press (motionlite::Shape::orbit);
	};
	orbitPad().onSwipe = [this] (OrbitSwipe::Result result) { setDirection (result); };
	setUpKey (shorter, juce::String::fromUTF8 ("\xe2\x97\x82"));
	setUpKey (longer, juce::String::fromUTF8 ("\xe2\x96\xb8"));
	shorter.onClick = [this] { motion.lengthDown (stem); };
	longer.onClick = [this] { motion.lengthUp (stem); };
	lengthLabel.setJustificationType (juce::Justification::centred);
}

void OneShotKeys::addTo (juce::Component& parent)
{
	for (auto& key : pads)
		parent.addAndMakeVisible (key);
	parent.addAndMakeVisible (shorter);
	parent.addAndMakeVisible (longer);
	parent.addAndMakeVisible (lengthLabel);
}

void OneShotKeys::setStemColour (juce::Colour colour)
{
	stemColour = colour;
	refresh();
}

juce::String OneShotKeys::shapeName (motionlite::Shape shape, bool reversed)
{
	const juce::String name (padNames[(size_t) shape]);
	if (shape != motionlite::Shape::orbit)
		return name;
	// U+25BA / U+25C4 sit in the same block as the length keys' triangles, in DejaVu Sans.
	return reversed ? juce::String::fromUTF8 ("\xe2\x97\x84 ") + name : name + juce::String::fromUTF8 (" \xe2\x96\xba");
}

juce::String OneShotKeys::lengthName (double bars)
{
	return barsNumber (bars) + (bars > 1.0 ? " BARS" : " BAR");
}

void OneShotKeys::press (motionlite::Shape shape)
{
	const auto now = takeShift != nullptr && takeShift();
	const auto seed = (std::uint64_t) juce::Random::getSystemRandom().nextInt64();
	motion.press (stem, shape, now, seed);
}

void OneShotKeys::setDirection (OrbitSwipe::Result result)
{
	if (result == OrbitSwipe::Result::none)
		return;
	motion.setReversed (stem, result == OrbitSwipe::Result::left);
	lookOfPad (motionlite::Shape::orbit);
}

void OneShotKeys::refresh()
{
	lengthLabel.setText (lengthName (motion.lengthBars (stem)), juce::dontSendNotification);
	for (int p = 0; p < shapeCount; ++p)
		lookOfPad ((motionlite::Shape) p);
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
