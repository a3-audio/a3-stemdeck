#pragma once

#include <JuceHeader.h>
#include "StemDeckPlayer.h"
#include "Buses.h"
#include "Session.h"
#include "LevelMeter.h"
#include "MeterBallistics.h"
#include "MotionLite.h"

class DeckSpatialPorts;

//==============================================================================
// Meters for all output channels: buses 1-4 and AUX, each L/R.
class OutputMeters : public juce::Component
{
public:
	static constexpr int numBuses = buses::count;
	static constexpr int numChannels = numBuses * 2;

	OutputMeters();

	void setLevel (int channel, float peak) { meters[channel]->setLevel (peak); }
	void setMeterParameters (MeterParameters parameters)
	{
		for (auto* meter : meters)
			meter->setParameters (parameters);
	}
	static juce::String busName (int bus) { return juce::String (buses::name (bus)); }

	// On its own tile: the bars and captions this far inside its bounds.
	void setInset (int newInset) { inset = newInset; resized(); }

	void paint (juce::Graphics& g) override;
	void resized() override;

private:
	juce::OwnedArray<LevelMeter> meters;
	juce::Rectangle<int> labelArea;
	int inset = 0;
};

//==============================================================================
// Channel strip of one deck: a knob per stem, with two switches, its own
// channel and AUX (one stem per bus, the rest on AUX, Buses.h), a cue switch and mute in one row; the
// four rows tight, a matrix of keys. The stems' names are on the overview's
// lanes, not here.
// Its channel fader, which shows the deck's level in its slot, stands in the
// band under the mixer: the strip owns it, the window places it.
class ChannelStrip : public juce::Component
{
public:
	ChannelStrip (StemDeckPlayer& player, int deckIndex);

	void toggleMute (int stem);
	void toggleCue (int stem);
	void refresh(); // meters, called by the main timer
	void setMeterParameters (MeterParameters parameters)
	{
		for (auto* meter : stemMeters)
			meter->setParameters (parameters);
	}

	// The bus switches as the player has them, after a change from elsewhere
	// (the rule, or Core, spec stemdeck-remote); nothing is sent back from here.
	void showBuses (int stem);
	// A click on one of the stem's bus switches, asking for `on`. The mixer
	// applies it under the rule; the strip does not touch the player itself.
	std::function<void (int stem, int bus, bool on)> onBusSwitch;

	// The channel fader, moved as by hand (the Auto-DJ: unity on a load).
	void setFaderDb (double db) { fader.setValue (db, juce::sendNotificationSync); }
	// ... and as a controller's slider does: 0..1 of its travel, with the
	// on-screen fader's own curve.
	void setFaderTravel (double t) { fader.setValue (fader.proportionOfLengthToValue (juce::jlimit (0.0, 1.0, t)), juce::sendNotificationSync); }
	double getFaderTravel() { return fader.valueToProportionOfLength (fader.getValue()); }
	bool isMuted (int stem) const { return muteButtons[stem]->getToggleState(); }
	// A controller's encoder on a stem's knob: `detents` steps of a fixed
	// share of its travel, on the knob's own curve.
	void nudgeStemGain (int stem, double detents);

	// Knobs, mutes, bus switches and fader, for the session. A restore
	// leaves the bus switches to MixerPanel::restoreBuses, which spans both
	// decks and is where every change is reported.
	void saveState (DeckSession& state) const;
	void restoreState (const DeckSession& state);

	// The fader and the stem meters behind it: made children of `parent`, placed by it.
	void addFaderTo (juce::Component& parent);
	void setFaderBounds (juce::Rectangle<int> area);

	// The deck's spatial engine, if it has one: a right-click or a long press
	// on a stem's knob then opens that stem's direction, width and
	// one-shots (StemSpatialPopup). Without one (IEM refused) nothing opens.
	void setSpatial (DeckSpatialPorts* engine) { spatial = engine; }
	void openSpatial (int stem);
	// Each pad press in that popup, with its stem: the screen records it while
	// the deck's 3D REC is on (EventStrip::pressRecorded).
	std::function<void (int stem, motionlite::Shape, bool shift, std::uint64_t seed)> onOneShotPressed;

	void mouseDown (const juce::MouseEvent&) override;
	void mouseDrag (const juce::MouseEvent&) override;
	void mouseUp (const juce::MouseEvent&) override;
	void paint (juce::Graphics& g) override;
	void resized() override;

private:
	int knobIndexOf (const juce::Component*) const;

	StemDeckPlayer& player;
	const int deckIndex;
	DeckSpatialPorts* spatial = nullptr;

	// A press held on a knob without moving it.
	struct LongPress : juce::Timer
	{
		std::function<void()> onFire;
		void timerCallback() override { stopTimer(); onFire(); }
	};
	LongPress longPress;
	int pressedStem = -1;

	// A stem's two bus switches: its own channel, then AUX -- the only buses
	// it may use (Buses.h mayUse).
	static constexpr int switchesPerStem = 2;
	std::array<int, switchesPerStem> switchBuses (int stem) const;

	juce::OwnedArray<juce::Slider> knobs;
	juce::OwnedArray<juce::TextButton> muteButtons, cueButtons, busButtons;   // busButtons: stem * switchesPerStem + k, bus switchBuses (stem)[k]
	juce::Slider fader { juce::Slider::LinearVertical, juce::Slider::NoTextBox };
	juce::OwnedArray<LevelMeter> stemMeters;   // behind the fader, one per stem

	juce::Rectangle<int> headerArea;

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChannelStrip)
};

//==============================================================================
class MixerPanel : public juce::Component
{
public:
	MixerPanel (StemDeckPlayer& playerA, StemDeckPlayer& playerB);

	ChannelStrip& strip (int deckIndex) { return deckIndex == 0 ? stripA : stripB; }
	const ChannelStrip& strip (int deckIndex) const { return deckIndex == 0 ? stripA : stripB; }
	void refresh();

	void setSpatial (int deckIndex, DeckSpatialPorts* engine) { strip (deckIndex).setSpatial (engine); }

	// One bus switch under the one-stem-per-bus rule (Buses.h), across both
	// decks: the players and the screen follow, and onBusesChanged answers
	// the switched stem even when the rule refused it.
	void switchBus (int deck, int stem, int bus, bool on);
	// Where a switch sends a stem it takes off its channel: AUX, or off
	// while the Auto-DJ plays (Buses.h).
	void setSpare (buses::Spare s) { spare = s; }
	// The Auto-DJ's routing (buses::route), applied as a switch is: the
	// players and the screen follow.
	void route (const std::vector<buses::Route>& routes);
	// A controller's key for one bus switch: exactly what a click on the
	// screen key does -- the switch toggles, the rule decides, Core hears.
	void pressBus (int deck, int stem, int bus);
	// A session's switches, as stored: brought into the rule (an older
	// session may break it, and the rule spans both decks), then applied.
	void restoreBuses (const buses::Masks& stored);
	// Every switch as the players have it, stem by stem (buses::stemIndex).
	buses::Masks busMasks() const;
	// After every change of any stem's switches, from any path -- a click,
	// the panel, Core, the Auto-DJ, a restore: the masks as they are now, and
	// the stem a switch asked for (-1: none), so a refused one is answered.
	// The one place the masks change is setBusMasks, and it calls this.
	std::function<void (const buses::Masks& now, int answered)> onBusesChanged;
	// Called for every switch a DJ made (switchBus: a click, the desk, the
	// panel), after it was applied -- not for the Auto-DJ's route().
	std::function<void (int deck, int stem, int bus, bool on)> onDjSwitch;

	// The volume faders and the output meters, in the band under the mixer:
	// made children of `parent`, placed by it.
	void addBandPartsTo (juce::Component& parent);
	// `metersTile` with the bars and captions `inset` inside it.
	void setBandBounds (juce::Rectangle<int> faderA, juce::Rectangle<int> faderB, juce::Rectangle<int> metersTile, int inset);

	void setOutputLevel (int channel, float peak) { outputMeters.setLevel (channel, peak); }
	// Every meter on it: both strips and the output meters.
	void setMeterParameters (MeterParameters parameters);

	void paint (juce::Graphics& g) override;
	void resized() override;

private:
	void setBusMasks (const buses::Masks& masks, int answered = -1);

	std::array<StemDeckPlayer*, buses::decks> players;
	buses::Spare spare = buses::Spare::toAux;
	ChannelStrip stripA, stripB;
	OutputMeters outputMeters;

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixerPanel)
};
