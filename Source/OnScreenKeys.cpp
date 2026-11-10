// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "OnScreenKeys.h"

OnScreenKeys::OnScreenKeys()
{
	setUp();
}

OnScreenKeys::OnScreenKeys (bool onboardAvailable, OnScreenKeyboard::Onboard onboard)
	: onboardKeyboard (onboardAvailable, std::move (onboard))
{
	setUp();
}

void OnScreenKeys::setUp()
{
	keys.setTooltip (onboardKeyboard.isAvailable() ? "On-screen keyboard (onboard): show or hide"
												   : "On-screen keyboard: onboard is not installed");
	keys.onClick = [this] { onboardKeyboard.keysPressed(); };
	keys.setMouseClickGrabsKeyboardFocus (false);
	onboardKeyboard.onHint = [this] (const juce::String& hint)
	{
		keys.setTooltip (hint);
		if (onHint != nullptr)
			onHint (hint);
	};
}

void OnScreenKeys::addTo (juce::Component& parent)
{
	parent.addAndMakeVisible (keys);
}
