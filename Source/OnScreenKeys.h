// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <JuceHeader.h>
#include "OnScreenKeyboard.h"

// The KEYS key and the on-screen keyboard it steers, as both screens have
// them: a text field taking the focus shows onboard, the focus leaving hides
// it, KEYS shows or hides it by hand. The key never takes the focus itself,
// or pressing it would leave the text field it is for.
class OnScreenKeys
{
public:
	OnScreenKeys();   // onboard, over D-Bus
	// Tests: what would go to onboard goes to `onboard` (OnScreenKeyboard's seam).
	OnScreenKeys (bool onboardAvailable, OnScreenKeyboard::Onboard onboard);

	void addTo (juce::Component& parent);
	juce::TextButton& key() { return keys; }
	OnScreenKeyboard& keyboard() { return onboardKeyboard; }

	// Why KEYS did nothing (no onboard, onboard not answering): for the status line.
	std::function<void (const juce::String&)> onHint;

private:
	void setUp();

	juce::TextButton keys { "KEYS" };
	OnScreenKeyboard onboardKeyboard;

	JUCE_DECLARE_NON_COPYABLE (OnScreenKeys)
};
