#pragma once

#include <JuceHeader.h>
#include "KeyboardPolicy.h"

// Runs KeyboardPolicy's commands against onboard, the on-screen keyboard: a
// text field taking focus shows it, focus leaving the text fields hides it a
// moment later, KEYS toggles it.
//
// Onboard is its own process, steered over D-Bus with dbus-send. The calls run
// on a worker thread, never on the message thread; when onboard does not
// answer it is started once, detached, and asked again. Without an onboard
// binary on the PATH nothing is started and KEYS only gives a hint.
// The desktop's focus listener is public: a test without an X display hands
// it the focus change a tap would have made.
class OnScreenKeyboard  : public juce::FocusChangeListener,
						  private juce::Timer
{
public:
	// What would go to onboard: show, hide or toggle.
	using Onboard = std::function<void (KeyboardPolicy::Command)>;

	OnScreenKeyboard();
	// Tests: the commands go to `onboard` instead; no worker, nothing started.
	OnScreenKeyboard (bool onboardAvailable, Onboard onboard);
	~OnScreenKeyboard() override;

	void keysPressed();
	bool isAvailable() const { return policy.isAvailable(); }

	// A short line for the status label: why KEYS did nothing.
	std::function<void (const juce::String&)> onHint;

	void globalFocusChanged (juce::Component* focusedComponent) override;

private:
	class Worker;

	void timerCallback() override;
	void run (KeyboardPolicy::Command command);
	void post (KeyboardPolicy::Command command);   // to the worker: show, hide or toggle
	void onboardFailed (const juce::String& why, bool stillAvailable);

	KeyboardPolicy policy;
	Onboard onboard;
	std::unique_ptr<Worker> worker;

	JUCE_DECLARE_WEAK_REFERENCEABLE (OnScreenKeyboard)
	JUCE_DECLARE_NON_COPYABLE (OnScreenKeyboard)
};
