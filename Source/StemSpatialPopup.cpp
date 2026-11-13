// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "StemSpatialPopup.h"

#include "DeckMotion.h"
#include "KnobSetup.h"

namespace
{
	constexpr int refreshHz = 30;
	// Shares of the popup's height: three slider rows, the tempo step, the pads, the length row.
	constexpr float sliderRowShare = 0.065f, tempoRowShare = 0.045f, padRowShare = 0.11f, lengthRowShare = 0.08f, gapShare = 0.015f;
	constexpr float tempoFontShare = 0.85f;   // of the tempo row

	void setUp (juce::Slider& slider, juce::Label& label, const juce::String& name, juce::Range<double> range, double step,
				double value, const juce::String& suffix, std::atomic<float>& target, juce::Component& parent)
	{
		slider.setSliderStyle (juce::Slider::LinearHorizontal);
		slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 24);
		slider.setRange (range.getStart(), range.getEnd(), step);
		slider.setTextValueSuffix (suffix);
		slider.setValue (value, juce::dontSendNotification);
		slider.setMouseClickGrabsKeyboardFocus (false);
		// After the value is set: opening the popup must not write anything.
		slider.onValueChange = [&slider, &target] { target.store ((float) slider.getValue()); };
		label.setText (name, juce::dontSendNotification);
		label.setJustificationType (juce::Justification::centredLeft);
		parent.addAndMakeVisible (label);
		parent.addAndMakeVisible (slider);
	}

}

StemSpatialPopup::StemSpatialPopup (DeckSpatial::Controls& c, int stem, DeckMotion* deckMotion)
	: controls (c), stemIndex ((size_t) juce::jlimit (0, DeckSpatial::stems - 1, stem)), motion (deckMotion),
	  stemColour (findColour (juce::Slider::rotarySliderFillColourId))
{
	const auto s = stemIndex;
	setUp (azimuthSlider, azimuthLabel, "AZ", { -180.0, 180.0 }, 1.0, controls.azimuth[s].load(), " deg", controls.azimuth[s], *this);
	setUp (elevationSlider, elevationLabel, "EL", { -90.0, 90.0 }, 1.0, controls.elevation[s].load(), " deg", controls.elevation[s], *this);
	setUp (widthSlider, widthLabel, "WIDTH", { 0.0, 360.0 }, 1.0, controls.width[s].load(), " deg", controls.width[s], *this);

	// Double-click returns to the value a fresh stem has.
	azimuthSlider.setDoubleClickReturnValue (true, 0.0);
	elevationSlider.setDoubleClickReturnValue (true, 0.0);
	widthSlider.setDoubleClickReturnValue (true, spatial::Pose{}.width);

	addAndMakeVisible (stemViewer);
	stemViewer.onHomeChanged = [this] (spatial::Pose pose) { homeDragged (pose); };
	for (auto* slider : { &azimuthSlider, &elevationSlider, &widthSlider })
		slider->onValueChange = [this, write = slider->onValueChange]
		{
			write();
			refresh();   // the viewer follows the sliders
		};

	if (motion != nullptr)
	{
		oneShots = std::make_unique<OneShotKeys> (*motion, (int) stemIndex, [this] { return takeShift(); });
		oneShots->addTo (*this);
		shiftKey.setButtonText ("SHIFT");
		knobsetup::untabbableKey (shiftKey);
		shiftKey.setClickingTogglesState (true);
		addAndMakeVisible (shiftKey);
	}
	setStemColour (stemColour);
	refresh();
	if (motion != nullptr)
		startTimerHz (refreshHz);
}

StemSpatialPopup::~StemSpatialPopup()
{
	stopTimer();
}

void StemSpatialPopup::setOnPressed (Pressed onPressed)
{
	if (oneShots != nullptr)
		oneShots->onPressed = std::move (onPressed);
}

void StemSpatialPopup::setPlacesOnly (PlacesOnly placesOnly)
{
	if (oneShots != nullptr)
		oneShots->placesOnly = std::move (placesOnly);
}

void StemSpatialPopup::setEvent (OneShotKeys::EventLink link)
{
	if (oneShots != nullptr)
		oneShots->event = std::move (link);
}

void StemSpatialPopup::setStemColour (juce::Colour colour)
{
	stemColour = colour;
	stemViewer.setColour (StemViewer::stemColourId, colour);
	shiftKey.setColour (juce::TextButton::buttonOnColourId, colour);
	if (oneShots != nullptr)
		oneShots->setStemColour (colour);
	refresh();
}

spatial::Pose StemSpatialPopup::homeNow() const
{
	return { controls.azimuth[stemIndex].load(), controls.elevation[stemIndex].load(), controls.width[stemIndex].load() };
}

void StemSpatialPopup::homeDragged (spatial::Pose pose)
{
	controls.azimuth[stemIndex].store (pose.azimuth);
	controls.elevation[stemIndex].store (pose.elevation);
	controls.width[stemIndex].store (pose.width);
	azimuthSlider.setValue (pose.azimuth, juce::dontSendNotification);
	elevationSlider.setValue (pose.elevation, juce::dontSendNotification);
	widthSlider.setValue (pose.width, juce::dontSendNotification);
}

// SHIFT holds for one press.
bool StemSpatialPopup::takeShift()
{
	const auto now = shiftKey.getToggleState();
	shiftKey.setToggleState (false, juce::dontSendNotification);
	return now;
}

void StemSpatialPopup::refresh()
{
	const auto home = homeNow();
	if (motion == nullptr)
	{
		stemViewer.setShown (home, home, motionlite::PadState::idle);
		return;
	}
	stemViewer.setShown (home, motion->posePlayed ((int) stemIndex), motion->stemState ((int) stemIndex));
	oneShots->refresh();
}

void StemSpatialPopup::resized()
{
	const auto height = (float) getHeight();
	const auto gap = juce::roundToInt (height * gapShare);
	auto area = getLocalBounds().reduced (gap * 2);
	const auto rowHeight = juce::roundToInt (height * sliderRowShare);
	const auto labelWidth = area.getWidth() / 5;

	for (auto [label, slider] : { std::pair { &azimuthLabel, &azimuthSlider }, std::pair { &elevationLabel, &elevationSlider },
								  std::pair { &widthLabel, &widthSlider } })
	{
		auto row = area.removeFromTop (rowHeight);
		label->setBounds (row.removeFromLeft (labelWidth));
		slider->setBounds (row);
	}

	if (motion != nullptr)
	{
		auto lengthRow = area.removeFromBottom (juce::roundToInt (height * lengthRowShare));
		area.removeFromBottom (gap);
		auto padRow = area.removeFromBottom (juce::roundToInt (height * padRowShare));
		const auto tempoRow = area.removeFromBottom (juce::roundToInt (height * tempoRowShare));
		oneShots->tempo().setBounds (tempoRow);
		oneShots->tempo().setFont (juce::FontOptions ((float) tempoRow.getHeight() * tempoFontShare));
		area.removeFromBottom (gap);

		constexpr int shapes = 4;
		const auto padWidth = padRow.getWidth() / shapes;
		for (int p = 0; p < shapes; ++p)
			oneShots->pad ((motionlite::Shape) p).setBounds (padRow.removeFromLeft (padWidth).reduced (gap / 2));
		const auto keyWidth = lengthRow.getWidth() / 5;
		shiftKey.setBounds (lengthRow.removeFromRight (keyWidth * 2).reduced (gap / 2));
		oneShots->lengthDown().setBounds (lengthRow.removeFromLeft (keyWidth).reduced (gap / 2));
		oneShots->lengthUp().setBounds (lengthRow.removeFromRight (keyWidth).reduced (gap / 2));
		oneShots->length().setBounds (lengthRow);
	}
	area.removeFromTop (gap);
	const auto side = juce::jmin (area.getWidth(), area.getHeight());
	stemViewer.setBounds (area.withSizeKeepingCentre (side, side));
}

void StemSpatialPopup::show (juce::Component& anchor, DeckSpatial::Controls& controls, DeckMotion* motion, int stem,
							  Pressed onPressed, PlacesOnly placesOnly)
{
	auto* top = anchor.getTopLevelComponent();
	const auto window = top->getLocalBounds();

	auto content = std::make_unique<StemSpatialPopup> (controls, stem, motion);
	content->setStemColour (anchor.findColour (juce::Slider::rotarySliderFillColourId));
	content->setOnPressed (std::move (onPressed));
	content->setPlacesOnly (std::move (placesOnly));
	// The viewer wants a square below the sliders: taller than wide, within the window.
	const auto width = window.getWidth() / 3;
	content->setSize (width, juce::jmin (window.getHeight() * 4 / 5, width * 2));

	auto& box = juce::CallOutBox::launchAsynchronously (std::move (content), top->getLocalArea (&anchor, anchor.getLocalBounds()), top);
	box.setLookAndFeel (&anchor.getLookAndFeel());
}
