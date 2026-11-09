// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "DeckSpatial.h"
#include "OneShotKeys.h"
#include "StemViewer.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdint>
#include <functional>
#include <memory>

class DeckMotion;

// One stem's place in its deck's spatial engine: direction (azimuth,
// elevation) and width, three sliders writing DeckSpatial::Controls, and the
// viewer showing and dragging the same direction and width. With the deck's
// DeckMotion it also carries the one-shot pads (ORBIT, CROSS, RISE, SCAT; idle,
// armed, running), the length keys and SHIFT (the next press starts now, then
// SHIFT lets go). Shown in a CallOutBox pointing at the stem's row in the
// mixer. The ranges are IEM's StereoEncoder's; the session picks the values
// up on its next gather.
class StemSpatialPopup : public juce::Component, private juce::Timer
{
public:
	StemSpatialPopup (DeckSpatial::Controls&, int stem, DeckMotion* = nullptr);
	~StemSpatialPopup() override;

	juce::Slider& azimuth() { return azimuthSlider; }
	juce::Slider& elevation() { return elevationSlider; }
	juce::Slider& width() { return widthSlider; }
	StemViewer& viewer() { return stemViewer; }
	// With a DeckMotion only.
	juce::TextButton& pad (motionlite::Shape shape) { jassert (oneShots != nullptr); return oneShots->pad (shape); }
	SwipePad& orbitPad() { jassert (oneShots != nullptr); return oneShots->orbitPad(); }
	juce::TextButton& lengthDown() { jassert (oneShots != nullptr); return oneShots->lengthDown(); }
	juce::TextButton& lengthUp() { jassert (oneShots != nullptr); return oneShots->lengthUp(); }
	juce::TextButton& shift() { return shiftKey; }
	juce::String lengthText() const { jassert (oneShots != nullptr); return oneShots->length().getText(); }

	// Each pad press that went into the motion queue (OneShotKeys::onPressed):
	// the two-deck screen records it while its deck's 3D REC is on.
	using Pressed = std::function<void (motionlite::Shape, bool shift, std::uint64_t seed)>;
	void setOnPressed (Pressed);
	// While it says so a press only places an event (OneShotKeys::placesOnly).
	using PlacesOnly = std::function<bool()>;
	void setPlacesOnly (PlacesOnly);

	void setStemColour (juce::Colour);
	// Reads what the audio thread last did into the pads, the length and the
	// viewer; a timer calls it while the call-out is open.
	void refresh();

	// Opens the call-out at `anchor`, sized from the window it sits in; it is
	// dismissed by a click outside and deletes itself.
	static void show (juce::Component& anchor, DeckSpatial::Controls&, DeckMotion*, int stem, Pressed onPressed = nullptr,
					  PlacesOnly placesOnly = nullptr);

	void resized() override;

private:
	void timerCallback() override { refresh(); }
	bool takeShift();
	void homeDragged (spatial::Pose);
	spatial::Pose homeNow() const;

	DeckSpatial::Controls& controls;
	const size_t stemIndex;
	DeckMotion* motion;
	juce::Colour stemColour;

	juce::Slider azimuthSlider, elevationSlider, widthSlider;
	juce::Label azimuthLabel, elevationLabel, widthLabel;
	StemViewer stemViewer;
	std::unique_ptr<OneShotKeys> oneShots;   // with a DeckMotion
	juce::TextButton shiftKey;

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StemSpatialPopup)
};
