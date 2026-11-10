// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "StemColumn.h"

#include "DeckSpatialPorts.h"
#include "StemDeckPlayer.h"
#include "KnobSetup.h"
#include "StemGainKnob.h"
#include "Theme.h"

namespace
{
	// Shares of the column's height, top to bottom; the viewer takes what is left.
	// The knob block is two knob rows, each with its caption under it.
	constexpr float knobShare = 0.11f, captionShare = 0.035f;
	constexpr float keyRowShare = 0.08f, gapShare = 0.012f;
	// Text a share of the line it stands in.
	constexpr float lineFont = 0.85f;

	void setUpKnob (juce::Slider& knob, juce::Range<double> range, double step, double fresh, const juce::String& suffix)
	{
		knobsetup::rotaryKnob (knob, range.getStart(), range.getEnd(), step, fresh);
		knob.setTextValueSuffix (suffix);
	}

	void setUpSwitch (juce::TextButton& key, juce::Colour on, juce::Colour inkOn)
	{
		key.setClickingTogglesState (true);
		knobsetup::untabbableKey (key);
		key.setColour (juce::TextButton::buttonOnColourId, on);
		key.setColour (juce::TextButton::textColourOnId, inkOn);
	}

	// The knob follows a value it does not own, unless a finger is on it.
	void follow (juce::Slider& knob, float value)
	{
		if (! knob.isMouseButtonDown())
			knob.setValue (value, juce::dontSendNotification);
	}
}

StemColumn::StemColumn (StemDeckPlayer& p, DeckSpatialPorts& spatialPorts, int stemIndex, std::function<bool()> takeShift)
	: player (p), ports (spatialPorts), stem (stemIndex), colour (Theme::stem (stemIndex)),
	  keys (spatialPorts.motion(), stemIndex, std::move (takeShift))
{
	const auto s = (size_t) stem;
	auto& settings = ports.settings();

	stemgain::setUp (gainKnob);
	gainKnob.onValueChange = [this] { player.setStemGain (stem, stemgain::gainOf (gainKnob.getValue())); };
	setUpKnob (auxKnob, { 0.0, 1.0 }, 0.01, 0.0, {});
	auxKnob.setValue (settings.aux[s].load(), juce::dontSendNotification);
	auxKnob.onValueChange = [this, s]
	{
		ports.settings().aux[s].store ((float) auxKnob.getValue());
		showAuxShare();
	};
	setUpKnob (widthKnob, { 0.0, 360.0 }, 1.0, spatial::Pose{}.width, juce::String::fromUTF8 ("\xc2\xb0"));
	widthKnob.setValue (settings.width[s].load(), juce::dontSendNotification);
	widthKnob.onValueChange = [this, s] { ports.settings().width[s].store ((float) widthKnob.getValue()); };

	for (auto [knob, caption, text] : { std::tuple { &gainKnob, &gainCaption, "GAIN" }, std::tuple { &auxKnob, &auxCaption, "AUX" },
										std::tuple { &widthKnob, &widthCaption, "WIDTH" } })
	{
		knob->setColour (juce::Slider::rotarySliderFillColourId, colour);
		caption->setText (text, juce::dontSendNotification);
		caption->setColour (juce::Label::textColourId, Theme::textDim);
		caption->setJustificationType (juce::Justification::centred);
		caption->setMinimumHorizontalScale (0.6f);
		addAndMakeVisible (knob);
		addAndMakeVisible (caption);
	}
	showAuxShare();

	stemViewer.setColour (StemViewer::stemColourId, colour);
	stemViewer.onHomeChanged = [this] (spatial::Pose pose) { homeDragged (pose); };
	addAndMakeVisible (stemViewer);

	keys.addTo (*this);
	keys.setStemColour (colour);
	keys.length().setColour (juce::Label::textColourId, Theme::text);

	setUpSwitch (cueKey, Theme::cue, juce::Colours::black);
	setUpSwitch (muteKey, Theme::mute, Theme::text);
	cueKey.setTooltip ("This stem into the cue (pre-mute)");
	muteKey.setTooltip ("This stem off the B-format and the AUX; the cue keeps it");
	cueKey.onClick = [this] { player.setStemCue (stem, cueKey.getToggleState()); };
	muteKey.onClick = [this] { player.setStemMuted (stem, muteKey.getToggleState()); };
	addAndMakeVisible (cueKey);
	addAndMakeVisible (muteKey);

	refresh();
}

void StemColumn::setLoaded (bool loaded)
{
	setAlpha (loaded ? 1.0f : Theme::emptySlotAlpha);
}

void StemColumn::showAuxShare()
{
	auxCaption.setText ("AUX " + juce::String (juce::roundToInt (auxKnob.getValue() * 100.0)), juce::dontSendNotification);
}

void StemColumn::homeDragged (spatial::Pose pose)
{
	auto& settings = ports.settings();
	const auto s = (size_t) stem;
	settings.azimuth[s].store (pose.azimuth);
	settings.elevation[s].store (pose.elevation);
	settings.width[s].store (pose.width);
	widthKnob.setValue (pose.width, juce::dontSendNotification);
}

void StemColumn::refresh()
{
	const auto& settings = ports.settings();
	const auto& motion = ports.motion();
	const auto s = (size_t) stem;
	const spatial::Pose home { settings.azimuth[s].load(), settings.elevation[s].load(), settings.width[s].load() };
	stemViewer.setShown (home, motion.posePlayed (stem), motion.stemState (stem));
	keys.refresh();

	follow (auxKnob, settings.aux[s].load());
	follow (widthKnob, settings.width[s].load());
	showAuxShare();
	cueKey.setToggleState (player.isStemCued (stem), juce::dontSendNotification);
	muteKey.setToggleState (player.isStemMuted (stem), juce::dontSendNotification);
}

void StemColumn::saveState (StemMixState& state) const
{
	state.gainDb = gainKnob.getValue();
	state.muted = player.isStemMuted (stem);
	state.cue = player.isStemCued (stem);
}

void StemColumn::restoreState (const StemMixState& state)
{
	gainKnob.setValue (state.gainDb, juce::dontSendNotification);
	player.setStemGain (stem, stemgain::gainOf (gainKnob.getValue()));
	player.setStemMuted (stem, state.muted);
	player.setStemCue (stem, state.cue);
	refresh();
}

void StemColumn::paint (juce::Graphics& g)
{
	// A card in the panel's grey; the stem's colour only as the line on top.
	const auto card = getLocalBounds().toFloat();
	juce::Path outline;
	outline.addRoundedRectangle (card, Theme::corner (card));
	g.setColour (Theme::panel);
	g.fillPath (outline);
	const juce::Graphics::ScopedSaveState clip (g);
	g.reduceClipRegion (outline);
	g.setColour (colour);
	g.fillRect (card.withHeight (juce::jmax (2.0f, (float) getHeight() * gapShare / 2.0f)));
}

void StemColumn::resized()
{
	const auto height = (float) getHeight();
	const auto row = [height] (float share) { return juce::roundToInt (height * share); };
	const auto gap = juce::jmax (1, row (gapShare));
	auto area = getLocalBounds().reduced (gap * 2, gap * 2);
	area.removeFromTop (gap);   // below the colour line

	placeKnobs (area, gap);
	area.removeFromTop (gap);

	// From the foot up: the length, CUE | MUTE, the pads two by two.
	const auto pair = [gap] (juce::Rectangle<int> r, juce::Component& left, juce::Component& right)
	{
		const auto half = (r.getWidth() - gap) / 2;
		left.setBounds (r.removeFromLeft (half));
		right.setBounds (r.removeFromRight (half));
	};
	auto lengthRow = area.removeFromBottom (row (keyRowShare));
	const auto arrowWidth = lengthRow.getWidth() / 4;
	keys.lengthDown().setBounds (lengthRow.removeFromLeft (arrowWidth));
	keys.lengthUp().setBounds (lengthRow.removeFromRight (arrowWidth));
	keys.length().setBounds (lengthRow);
	keys.length().setFont (juce::FontOptions ((float) lengthRow.getHeight() * lineFont * 0.6f));
	area.removeFromBottom (gap);
	pair (area.removeFromBottom (row (keyRowShare)), cueKey, muteKey);
	area.removeFromBottom (gap);
	pair (area.removeFromBottom (row (keyRowShare)), keys.pad (motionlite::Shape::rise), keys.pad (motionlite::Shape::scat));
	area.removeFromBottom (gap);
	pair (area.removeFromBottom (row (keyRowShare)), keys.pad (motionlite::Shape::orbit), keys.pad (motionlite::Shape::ping));
	// The stem's tempo step, a caption's line over the pads it belongs to.
	auto tempoRow = area.removeFromBottom (row (captionShare));
	keys.tempo().setBounds (tempoRow);
	keys.tempo().setFont (juce::FontOptions ((float) tempoRow.getHeight() * lineFont));
	area.removeFromBottom (gap);

	const auto side = juce::jmin (area.getWidth(), area.getHeight());
	stemViewer.setBounds (area.withSizeKeepingCentre (side, side));
}

// As on the panel: AUX (the pot) over WIDTH (the lower encoder) on the left,
// GAIN (the upper encoder) to the right, its centre half-way between theirs.
void StemColumn::placeKnobs (juce::Rectangle<int>& area, int gap)
{
	const auto height = (float) getHeight();
	const auto half = area.getWidth() / 2;
	// A knob is round: no taller than its half of the column is wide.
	const auto knobHeight = juce::jmin (juce::roundToInt (height * knobShare), half);
	const auto captionHeight = juce::roundToInt (height * captionShare);
	const auto cell = knobHeight + captionHeight;
	auto block = area.removeFromTop (2 * cell);
	auto left = block.removeFromLeft (half);
	auto right = block.withWidth (half).withTrimmedTop (cell / 2).withHeight (cell);

	const auto place = [gap] (juce::Slider& knob, juce::Label& caption, juce::Rectangle<int> r, int knobPart)
	{
		knob.setBounds (r.removeFromTop (knobPart).reduced (gap / 2, 0));
		caption.setBounds (r);
		caption.setFont (juce::FontOptions ((float) r.getHeight() * lineFont));
	};
	place (auxKnob, auxCaption, left.removeFromTop (cell), knobHeight);
	place (widthKnob, widthCaption, left, knobHeight);
	place (gainKnob, gainCaption, right, knobHeight);
}
