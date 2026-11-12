#pragma once

#include <JuceHeader.h>

#include "AutoDj.h"
#include "FolderChoice.h"
#include "KeyLock.h"
#include "Outputs.h"
#include "SpatialLaw.h"

#include <optional>

// StemDeck's settings: where the stem library lives, how long the Auto DJ
// fades, the key lock's engine, whether tips show, and the way to the audio device. The folder is chosen, never typed
// -- the rig's touch screen has no keyboard to type with (2026-09-30). OK and
// Cancel are its own buttons: i3 gives a floating window no close button, so
// the touch screen had no way out (2026-10-06). The audio device applies as
// it is chosen, in its own panel, as before.
class SettingsPanel : public juce::Component
{
public:
	struct Values
	{
		juce::File libraryFolder;
		AutoDj::MixLength autoDjFade;
		bool audioDeviceChoosable = true;
		juce::String audioDeviceNote;   // its tooltip, why not
		bool tipsShown = true;
		// A -bformat instance: its phones decoder (R12) in place of Auto DJ, which it has not.
		std::optional<spatial::PhonesMode> phonesMode;
		// A -bformat instance: whether this deck holds the A³ Motion panel (one instance does).
		std::optional<bool> panelHere;
		bool panelFollowsWorkspace = false;   // R9: the workspace decides, the toggle only shows
		keylock::Engine keyLockEngine = keylock::Engine::standard;
		// A -bformat instance: Output: Stereo / B-Format. A change restarts the instance.
		std::optional<outputs::OutputMode> outputMode;
	};

	struct Actions
	{
		std::function<void (const juce::File&)> onLibraryFolder;
		std::function<void (AutoDj::MixLength)> onAutoDjFade;
		std::function<void()> onAudioDevice;
		std::function<void (bool shown)> onTips;
		std::function<void (spatial::PhonesMode)> onPhonesMode;
		std::function<void (bool here)> onPanelHere;
		std::function<void (keylock::Engine)> onKeyLockEngine;
		std::function<void (outputs::OutputMode)> onOutputMode;
	};

	SettingsPanel (const Values& values, Actions actions);

	void resized() override;

private:
	void choose();
	void showFolder();
	void close();
	void ok();

	juce::Label libraryLabel { {}, "Stem library" };
	juce::Label libraryPath;
	juce::TextButton chooseButton { juce::String::fromUTF8 ("Choose\xe2\x80\xa6") };
	juce::Label libraryNote;

	juce::Label fadeLabel { {}, "AutoDJ overlap" };
	juce::ComboBox fadeBars, fadeSeconds;
	juce::Label fadeBarsUnit { {}, "bars" }, fadeSecondsUnit { {}, "s without beat grid" };
	AutoDj::MixLength fadeShown;

	juce::Label phonesLabel { {}, "Phones decoder" };
	juce::ComboBox phonesMode;
	std::optional<spatial::PhonesMode> phonesModeShown;

	juce::Label keyQualityLabel { {}, "KEY quality" };
	juce::ComboBox keyQuality;
	keylock::Engine keyQualityShown = keylock::Engine::standard;

	juce::Label outputLabel { {}, "Output" };
	juce::ComboBox outputMode;
	std::optional<outputs::OutputMode> outputModeShown;

	juce::ToggleButton panelButton { "Panel: this deck" };
	std::optional<bool> panelShown;

	juce::ToggleButton tipsButton { "Show tips (help text on the keys)" };
	bool tipsWereShown = true;

	juce::TextButton audioButton { juce::String::fromUTF8 ("Audio device\xe2\x80\xa6") };
	juce::TextButton okButton { "OK" };
	juce::TextButton cancelButton { "Cancel" };

	Actions actions;
	std::unique_ptr<juce::FileChooser> chooser;
	FolderChoice choice;
};
