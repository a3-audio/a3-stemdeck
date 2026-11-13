#include "MixerPanel.h"

#include "KnobSetup.h"
#include "Theme.h"
#include "SurfaceJuce.h"
#include "StemGainKnob.h"
#include "StemSpatialPopup.h"
#include "DeckSpatialPorts.h"

namespace
{
	constexpr float minDb = stemKnobMinimumDb;
	constexpr int longPressMs = 600;   // a touch has no right button

	juce::String busName (int bus) { return juce::String (buses::name (bus)); }

	// A lit bus switch: the stem's own colour on 1-4, AUX its own.
	juce::Colour busColour (int bus, int stem)
	{
		return bus == buses::aux ? Theme::aux : Theme::stem (stem);
	}

	using stemgain::dbText;
}

//==============================================================================
OutputMeters::OutputMeters()
{
	for (int ch = 0; ch < numChannels; ++ch)
	{
		auto* meter = meters.add (new LevelMeter());
		meter->setShowsClip (true);
		addAndMakeVisible (meter);
	}
}

void OutputMeters::paint (juce::Graphics& g)
{
	// Its own tile in the band, like the decks'.
	const auto tile = getLocalBounds().toFloat();
	g.setColour (Theme::panel);
	g.fillRoundedRectangle (tile, Theme::corner (tile));

	// The bus names under their pairs, a share of the caption line.
	g.setFont (juce::FontOptions ((float) labelArea.getHeight() * 0.6f, juce::Font::bold));

	for (int bus = 0; bus < numBuses; ++bus)
	{
		const auto pair = meters[bus * 2]->getBounds().getUnion (meters[bus * 2 + 1]->getBounds());
		g.setColour (bus < buses::aux ? Theme::text : busColour (bus, 0));
		g.drawText (busName (bus), pair.withY (labelArea.getY()).withHeight (labelArea.getHeight()).expanded (6, 0),
					juce::Justification::centred);
	}
}

void OutputMeters::resized()
{
	// Bars top-aligned with the faders; captions below them, level with the
	// strips' empty bottom line.
	auto area = getLocalBounds().reduced (inset);
	labelArea = area.removeFromBottom (surface::meterCaptionHeight (area.getHeight()));

	const auto pairGap = area.getWidth() / 25;
	const auto meterWidth = (area.getWidth() - pairGap * (numBuses - 1)) / numChannels;
	auto x = area.getX() + (area.getWidth() - (meterWidth * numChannels + pairGap * (numBuses - 1))) / 2;

	for (int ch = 0; ch < numChannels; ++ch)
	{
		meters[ch]->setBounds (x, area.getY(), meterWidth - surface::hairline (meterWidth), area.getHeight());
		x += meterWidth + ((ch % 2 == 1) ? pairGap : 0);
	}
}

//==============================================================================
ChannelStrip::ChannelStrip (StemDeckPlayer& p, int index) : player (p), deckIndex (index)
{
	longPress.onFire = [this]
	{
		if (pressedStem >= 0)
			openSpatial (std::exchange (pressedStem, -1));
	};

	for (int s = 0; s < StemSet::numStems; ++s)
	{
		auto* knob = knobs.add (new juce::Slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox));
		stemgain::setUp (*knob);
		knob->setColour (juce::Slider::rotarySliderFillColourId, Theme::stem (s));
		knob->addMouseListener (this, false);   // right-click and long press, beside the knob's own drag
		knob->onValueChange = [this, s, knob]
		{
			player.setStemGain (s, stemgain::gainOf (knob->getValue()));
		};
		addAndMakeVisible (knob);

		auto* mute = muteButtons.add (new juce::TextButton ("M"));
		mute->setClickingTogglesState (true);
		mute->setColour (juce::TextButton::buttonOnColourId, Theme::mute);
		mute->setMouseClickGrabsKeyboardFocus (false);
		mute->onClick = [this, s, mute] { player.setStemMuted (s, mute->getToggleState()); };
		mute->setTooltip ("Stem stumm");
		addAndMakeVisible (mute);

		auto* cue = cueButtons.add (new juce::TextButton ("C"));
		cue->setClickingTogglesState (true);
		cue->setToggleState (player.isStemCued (s), juce::dontSendNotification);
		cue->setColour (juce::TextButton::buttonOnColourId, Theme::cue);
		cue->setMouseClickGrabsKeyboardFocus (false);
		cue->onClick = [this, s, cue] { player.setStemCue (s, cue->getToggleState()); };
		cue->setTooltip ("Stem to the cue outputs (pre-listen, before mute and fader)");
		addAndMakeVisible (cue);

		for (const auto bus : switchBuses (s))
		{
			const auto label = bus == buses::aux ? juce::String ("A") : juce::String (bus + 1);
			auto* button = busButtons.add (new juce::TextButton (label));
			button->setClickingTogglesState (true);
			button->setToggleState (player.isStemOnBus (s, bus), juce::dontSendNotification);
			button->setColour (juce::TextButton::buttonOnColourId, busColour (bus, s));
			button->setMouseClickGrabsKeyboardFocus (false);
			button->onClick = [this, s, bus, button]
			{
				if (onBusSwitch)
					onBusSwitch (s, bus, button->getToggleState());
				else
					showBuses (s);
			};
			button->setTooltip ("Stem to bus " + busName (bus) + " (post fader)");
			addAndMakeVisible (button);
		}

		stemMeters.add (new LevelMeter())->setBarColour (Theme::stem (s));
	}

	fader.setRange (minDb, 0.0, 0.1);
	fader.setSkewFactorFromMidPoint (-15.0);
	fader.setValue (0.0, juce::dontSendNotification);
	fader.setDoubleClickReturnValue (true, 0.0);
	fader.setPopupDisplayEnabled (true, true, nullptr);
	fader.textFromValueFunction = dbText;
	fader.setColour (juce::Slider::thumbColourId, Theme::deck (deckIndex));
	fader.setMouseClickGrabsKeyboardFocus (false);
	fader.onValueChange = [this]
	{
		player.setDeckGain (juce::Decibels::decibelsToGain ((float) fader.getValue(), minDb));
	};
	// The deck's four stems metered in the fader's slot, pre-fader: the meters
	// behind, the fader's ticks and cap drawn over them (2026-10-08).
	fader.getProperties().set (DJLookAndFeel::meterInSlot, true);
}

int ChannelStrip::knobIndexOf (const juce::Component* component) const
{
	for (int s = 0; s < knobs.size(); ++s)
		if (knobs[s] == component)
			return s;
	return -1;
}

void ChannelStrip::openSpatial (int stem)
{
	if (spatial == nullptr || stem < 0 || stem >= knobs.size())
		return;
	StemSpatialPopup::show (*knobs[stem], spatial->settings(), &spatial->motion(), stem,
							[this, stem] (motionlite::Shape shape, bool shift, std::uint64_t seed)
							{
								if (onOneShotPressed != nullptr)
									onOneShotPressed (stem, shape, shift, seed);
							},
							oneShotsPlaceOnly, oneShotsEvent != nullptr ? oneShotsEvent (stem) : OneShotKeys::EventLink {});
}

void ChannelStrip::mouseDown (const juce::MouseEvent& e)
{
	const auto stem = knobIndexOf (e.eventComponent);
	if (stem < 0 || spatial == nullptr)
		return;
	if (e.mods.isPopupMenu())
		return openSpatial (stem);

	pressedStem = stem;
	longPress.startTimer (longPressMs);
}

void ChannelStrip::mouseDrag (const juce::MouseEvent& e)
{
	if (e.mouseWasDraggedSinceMouseDown())
		longPress.stopTimer();
}

void ChannelStrip::mouseUp (const juce::MouseEvent&)
{
	longPress.stopTimer();
	pressedStem = -1;
}

void ChannelStrip::saveState (DeckSession& state) const
{
	for (int s = 0; s < StemSet::numStems; ++s)
	{
		auto& stem = state.stems[(size_t) s];
		stem.gainDb = knobs[s]->getValue();
		stem.muted = muteButtons[s]->getToggleState();
		stem.cue = cueButtons[s]->getToggleState();
		stem.buses = 0;
		const auto stemBuses = switchBuses (s);
		for (int k = 0; k < switchesPerStem; ++k)
			if (busButtons[s * switchesPerStem + k]->getToggleState())
				stem.buses |= 1u << stemBuses[(size_t) k];
	}
	state.faderDb = fader.getValue();
}

void ChannelStrip::restoreState (const DeckSession& state)
{
	// Through the controls, so the player follows exactly as on a click.
	for (int s = 0; s < StemSet::numStems; ++s)
	{
		const auto& stem = state.stems[(size_t) s];
		knobs[s]->setValue (stem.gainDb, juce::sendNotificationSync);
		muteButtons[s]->setToggleState (stem.muted, juce::sendNotificationSync);
		cueButtons[s]->setToggleState (stem.cue, juce::sendNotificationSync);
	}
	fader.setValue (state.faderDb, juce::sendNotificationSync);
}

std::array<int, ChannelStrip::switchesPerStem> ChannelStrip::switchBuses (int stem) const
{
	return { buses::channelOf (buses::stemIndex (deckIndex, stem)), buses::aux };
}

void ChannelStrip::showBuses (int stem)
{
	const auto stemBuses = switchBuses (stem);
	for (int k = 0; k < switchesPerStem; ++k)
		busButtons[stem * switchesPerStem + k]->setToggleState (player.isStemOnBus (stem, stemBuses[(size_t) k]), juce::dontSendNotification);
}

void ChannelStrip::nudgeStemGain (int stem, double detents)
{
	if (auto* knob = knobs[stem])
		knobsetup::nudge (*knob, detents);
}

void ChannelStrip::toggleMute (int stem)
{
	if (auto* b = muteButtons[stem])
		b->setToggleState (! b->getToggleState(), juce::sendNotificationSync);
}

void ChannelStrip::toggleCue (int stem)
{
	if (auto* b = cueButtons[stem])
		b->setToggleState (! b->getToggleState(), juce::sendNotificationSync);
}

void ChannelStrip::refresh()
{
	for (int s = 0; s < StemSet::numStems; ++s)
		stemMeters[s]->setLevel (player.popStemPeak (s));
}

void ChannelStrip::paint (juce::Graphics& g)
{
	const auto bounds = getLocalBounds().toFloat();
	g.setColour (Theme::panelRaised);
	g.fillRoundedRectangle (bounds, 6.0f);

	// The deck's bar with its name inside, the text a share of the bar.
	const auto bar = headerArea.toFloat().reduced (headerArea.getHeight() * 0.3f, headerArea.getHeight() * 0.1f);
	g.setColour (Theme::deck (deckIndex));
	g.fillRoundedRectangle (bar, bar.getHeight() * 0.2f);
	g.setColour (juce::Colours::black);
	g.setFont (juce::FontOptions (bar.getHeight() * 0.75f, juce::Font::bold));
	g.drawText ("DECK " + Theme::deckName (deckIndex), bar, juce::Justification::centred);
}

void ChannelStrip::resized()
{
	const auto layout = surface::channelStrip (surface::fromJuce (getLocalBounds()), deckIndex);
	headerArea = surface::toJuce (layout.header);

	for (int s = 0; s < StemSet::numStems; ++s)
	{
		const auto& row = layout.stems[(size_t) s];
		knobs[s]->setBounds (surface::toJuce (row.knob));
		muteButtons[s]->setBounds (surface::toJuce (row.mute));
		cueButtons[s]->setBounds (surface::toJuce (row.cue));
		busButtons[s * switchesPerStem + 0]->setBounds (surface::toJuce (row.channel));
		busButtons[s * switchesPerStem + 1]->setBounds (surface::toJuce (row.aux));
	}
}

void ChannelStrip::addFaderTo (juce::Component& parent)
{
	for (auto* meter : stemMeters)
		parent.addAndMakeVisible (meter);   // first: behind the fader
	parent.addAndMakeVisible (fader);
}

void ChannelStrip::setFaderBounds (juce::Rectangle<int> area)
{
	fader.setBounds (area);

	// The meters fill the slot over the fader's travel: from where the cap's
	// centre stands at full level to where it stands at the bottom.
	const auto travel = fader.getLocalBounds().toFloat().reduced (0.0f, DJLookAndFeel::faderCapHeight (fader) / 2.0f);
	// The four stem meters side by side in the slot, a hairline apart.
	auto slot = DJLookAndFeel::faderSlot (fader, travel).getSmallestIntegerContainer() + fader.getPosition();
	const auto width = slot.getWidth() / stemMeters.size();
	for (auto* meter : stemMeters)
		meter->setBounds (slot.removeFromLeft (width).withTrimmedRight (1));
}

//==============================================================================
static_assert (StemSet::numStems == buses::stemsPerDeck);

MixerPanel::MixerPanel (StemDeckPlayer& playerA, StemDeckPlayer& playerB)
	: players { &playerA, &playerB }, stripA (playerA, 0), stripB (playerB, 1)
{
	for (int d = 0; d < buses::decks; ++d)
		strip (d).onBusSwitch = [this, d] (int stem, int bus, bool on) { switchBus (d, stem, bus, on); };
	addAndMakeVisible (stripA);
	addAndMakeVisible (stripB);
}

void MixerPanel::pressBus (int deck, int stem, int bus)
{
	if (deck < 0 || deck >= buses::decks || stem < 0 || stem >= buses::stemsPerDeck || bus < 0 || bus >= buses::count)
		return;
	switchBus (deck, stem, bus, ! players[(size_t) deck]->isStemOnBus (stem, bus));
}

void MixerPanel::addBandPartsTo (juce::Component& parent)
{
	stripA.addFaderTo (parent);
	stripB.addFaderTo (parent);
	parent.addAndMakeVisible (outputMeters);
}

void MixerPanel::setBandBounds (juce::Rectangle<int> faderA, juce::Rectangle<int> faderB, juce::Rectangle<int> metersTile, int inset)
{
	stripA.setFaderBounds (faderA);
	stripB.setFaderBounds (faderB);
	outputMeters.setInset (inset);
	outputMeters.setBounds (metersTile);
}

void MixerPanel::setMeterParameters (MeterParameters parameters)
{
	stripA.setMeterParameters (parameters);
	stripB.setMeterParameters (parameters);
	outputMeters.setMeterParameters (parameters);
}

void MixerPanel::refresh()
{
	stripA.refresh();
	stripB.refresh();
}

buses::Masks MixerPanel::busMasks() const
{
	buses::Masks masks {};
	for (int d = 0; d < buses::decks; ++d)
		for (int s = 0; s < buses::stemsPerDeck; ++s)
			masks[(size_t) buses::stemIndex (d, s)] = players[(size_t) d]->getStemBuses (s);
	return masks;
}

void MixerPanel::setBusMasks (const buses::Masks& masks, int answered)
{
	for (int d = 0; d < buses::decks; ++d)
		for (int s = 0; s < buses::stemsPerDeck; ++s)
		{
			players[(size_t) d]->setStemBuses (s, masks[(size_t) buses::stemIndex (d, s)]);
			strip (d).showBuses (s);
		}
	if (onBusesChanged)
		onBusesChanged (busMasks(), answered);
}

void MixerPanel::switchBus (int deck, int stem, int bus, bool on)
{
	const auto switched = buses::stemIndex (deck, stem);
	// Refused before anything is touched: a switch the rule forbids must not
	// reach the masks, the report or the Auto-DJ's "the DJ moved it".
	if (! buses::mayUse (switched, bus))
	{
		strip (deck).showBuses (stem);   // a clicked button toggles back
		return;
	}
	const auto after = buses::applySwitch (busMasks(), switched, bus, on, spare);
	// Every strip re-shown, also when nothing changed: a refused click (AUX
	// off) has already toggled its button and must toggle back.
	setBusMasks (after, switched);
	if (onDjSwitch)
		onDjSwitch (deck, stem, bus, on);
}

void MixerPanel::route (const std::vector<buses::Route>& routes)
{
	setBusMasks (buses::route (busMasks(), routes));
}

void MixerPanel::restoreBuses (const buses::Masks& stored)
{
	setBusMasks (buses::normalise (stored));
}

void MixerPanel::paint (juce::Graphics& g)
{
	g.setColour (Theme::panel);
	g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (2.0f), 6.0f);
}

void MixerPanel::resized()
{
	// The strips meet in the middle; their faders and the output meters are
	// in the band below (MainComponent).
	const auto strips = surface::mixerStrips (surface::fromJuce (getLocalBounds()));
	stripA.setBounds (surface::toJuce (strips[0]));
	stripB.setBounds (surface::toJuce (strips[1]));
}
