// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "OneShotKeys.h"
#include "Session.h"
#include "StemViewer.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

class DeckSpatialPorts;
class StemDeckPlayer;

// One stem of the one-deck screen (v4, spec section 9), top to bottom: the
// knobs as they sit on the A³ Motion panel (AUX the pot top left, WIDTH the
// lower encoder under it, GAIN the upper encoder to the right, half-way
// between them), the viewer (StemViewer: home dragged, a shot shown), the
// one-shot pads two by two under the stem's tempo step, CUE and MUTE, the
// length. No name, number or
// state line (2026-10-29): the colour line on top is the stem, a running pad
// carries its beat count.
// GAIN, CUE and MUTE are the player's; AUX, WIDTH and the home are the
// spatial settings (DeckSpatialPorts::settings), never the played pose.
class StemColumn : public juce::Component
{
public:
	StemColumn (StemDeckPlayer&, DeckSpatialPorts&, int stem, std::function<bool()> takeShift);

	// Whether a file is on this slot; an empty one is drawn dim.
	void setLoaded (bool);
	// The owner's timer: the shot's state, the viewer, CUE and MUTE as the player has them.
	void refresh();

	// GAIN, MUTE and CUE for the session; the spatial values are DeckHost's.
	void saveState (StemMixState&) const;
	void restoreState (const StemMixState&);

	juce::Slider& gain() { return gainKnob; }
	juce::Slider& aux() { return auxKnob; }
	juce::Slider& width() { return widthKnob; }
	juce::TextButton& cue() { return cueKey; }
	juce::TextButton& mute() { return muteKey; }
	StemViewer& viewer() { return stemViewer; }
	OneShotKeys& oneShots() { return keys; }
	juce::String auxCaptionText() const { return auxCaption.getText(); }

	void paint (juce::Graphics&) override;
	void resized() override;

private:
	void homeDragged (spatial::Pose);
	void showAuxShare();   // the AUX caption carries its value
	void placeKnobs (juce::Rectangle<int>& area, int gap);

	StemDeckPlayer& player;
	DeckSpatialPorts& ports;
	const int stem;
	const juce::Colour colour;

	juce::Slider gainKnob, auxKnob, widthKnob;
	juce::Label gainCaption, auxCaption, widthCaption;
	StemViewer stemViewer;
	OneShotKeys keys;
	juce::TextButton cueKey { "CUE" }, muteKey { "MUTE" };

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StemColumn)
};
