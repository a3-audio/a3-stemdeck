// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>

#include "../BareXServer.h"
#include "OnScreenKeys.h"
#include "StemLibrary.h"
#include "Theme.h"

#include <algorithm>
#include <vector>

// The one-deck screen's on-screen keyboard (2026-11-10, the rig has no real
// keyboard): a tap into the library's search shows onboard, KEYS toggles it
// and leaves the search its focus. Onboard itself is never started: the
// commands go to the test (OnScreenKeyboard's seam).
namespace
{
using Command = KeyboardPolicy::Command;

class OnScreenKeysTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		formats.registerBasicFormats();
		// Where OneDeckComponent has them at 768 x 1024: the top bar, the library at the foot.
		screen.setSize (768, 1024);
		keys.addTo (screen);
		keys.key().setBounds (500, 6, 60, 36);
		library.setOneDeck (Theme::deck (0));
		screen.addAndMakeVisible (library);
		library.setBounds (6, 772, 756, 246);
	}

	// The tap tests need a real window, hence an X server (xvfb-run).
	bool openWindow()
	{
		if (! barex::hasDisplay())
			return false;
		barex::internWindowManagerAtoms();
		screen.addToDesktop (0);
		screen.setVisible (true);
		juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
		return true;
	}

	void TearDown() override
	{
		if (screen.isOnDesktop())
			screen.removeFromDesktop();
	}

	juce::TextEditor* search()
	{
		for (auto* child : library.getChildren())
			if (auto* editor = dynamic_cast<juce::TextEditor*> (child))
				return editor;
		return nullptr;
	}

	// A finger on the rig: JUCE on X hands every touch on twice, as touch#0
	// and as the emulated mouse#0 (feedback_every-touch-arrives-twice).
	void tap (juce::Component& target)
	{
		auto* peer = screen.getPeer();
		ASSERT_NE (peer, nullptr);
		using Source = juce::MouseInputSource::InputSourceType;
		const auto where = screen.getLocalPoint (&target, target.getLocalBounds().getCentre()).toFloat();
		for (const auto down : { true, false })
			for (const auto type : { Source::touch, Source::mouse })
			{
				const auto mods = down ? juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier) : juce::ModifierKeys();
				const auto pressure = type == Source::touch ? 1.0f : juce::MouseInputSource::defaultPressure;
				peer->handleMouseEvent (type, where, mods, pressure, juce::MouseInputSource::defaultOrientation, time += 2, {}, 0);
			}
		juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
	}

	int count (Command command) const { return (int) std::count (sent.begin(), sent.end(), command); }

	juce::ScopedJuceInitialiser_GUI juceInitialiser;
	juce::AudioFormatManager formats;
	std::vector<Command> sent;
	juce::Component screen;
	OnScreenKeys keys { true, [this] (Command command) { sent.push_back (command); } };
	StemLibrary library { formats };
	juce::int64 time = 1000;
};
}

TEST_F (OnScreenKeysTest, KeysStandsOnTheScreenAndNeverTakesTheFocus)
{
	auto& key = keys.key();
	EXPECT_EQ (key.getParentComponent(), &screen);
	EXPECT_TRUE (key.isVisible());
	EXPECT_EQ (key.getButtonText(), juce::String ("KEYS"));
	EXPECT_FALSE (key.getMouseClickGrabsKeyboardFocus());
}

// What the tap below does to the keyboard, without a window: the desktop
// reports the search's focus, then the focus leaving.
TEST_F (OnScreenKeysTest, TheSearchTakingTheFocusShowsTheKeyboard)
{
	auto* field = search();
	ASSERT_NE (field, nullptr);
	keys.keyboard().globalFocusChanged (field);
	EXPECT_EQ (count (Command::show), 1);
	keys.keyboard().globalFocusChanged (&keys.key());
	EXPECT_EQ (count (Command::hide), 0) << "not at once: a hop between fields does not flash it away";
	juce::MessageManager::getInstance()->runDispatchLoopUntil (600);   // past the debounce
	EXPECT_EQ (count (Command::hide), 1);
}

TEST_F (OnScreenKeysTest, ATapIntoTheLibrarysSearchShowsTheKeyboard)
{
	if (! openWindow())
		GTEST_SKIP() << "no X display";
	auto* field = search();
	ASSERT_NE (field, nullptr);
	tap (*field);
	EXPECT_TRUE (field->hasKeyboardFocus (false)) << "the tap gives the search the focus";
	EXPECT_EQ (count (Command::show), 1) << "and the focus shows onboard";
}

TEST_F (OnScreenKeysTest, KeysTogglesAndLeavesTheSearchItsFocus)
{
	if (! openWindow())
		GTEST_SKIP() << "no X display";
	auto* field = search();
	ASSERT_NE (field, nullptr);
	tap (*field);
	tap (keys.key());
	EXPECT_EQ (count (Command::toggle), 1);
	EXPECT_TRUE (field->hasKeyboardFocus (false)) << "KEYS does not take the focus from the search";
}

TEST_F (OnScreenKeysTest, LeavingTheSearchHidesTheKeyboard)
{
	if (! openWindow())
		GTEST_SKIP() << "no X display";
	auto* field = search();
	ASSERT_NE (field, nullptr);
	tap (*field);
	ASSERT_EQ (count (Command::show), 1);
	field->giveAwayKeyboardFocus();
	juce::MessageManager::getInstance()->runDispatchLoopUntil (600);   // past the debounce
	EXPECT_EQ (count (Command::hide), 1);
}

TEST_F (OnScreenKeysTest, WithoutOnboardKeysSaysWhy)
{
	OnScreenKeys without { false, [this] (Command command) { sent.push_back (command); } };
	juce::String hint;
	without.onHint = [&hint] (const juce::String& text) { hint = text; };
	without.key().onClick();
	EXPECT_TRUE (hint.containsIgnoreCase ("onboard")) << hint;
	EXPECT_TRUE (sent.empty());
}
