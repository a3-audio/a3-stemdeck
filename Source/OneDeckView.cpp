// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "OneDeckView.h"

#include "KnobSetup.h"
#include "SurfaceJuce.h"
#include "SurfaceLayout.h"
#include "Theme.h"

namespace
{
	// The view, top to bottom, shares of its height: the rolling waveform, the
	// event strip, the overview (2026-11-09: under the strip), the foot row
	// (transport and meters); the deck panel and the stem columns take the rest.
	constexpr float waveShare = 0.15f, overviewShare = 0.06f, footShare = 0.15f;
	// The event strip's row under the rolling waveform: keys about a fingertip high.
	constexpr float eventStripShare = 0.065f;
	// The deck panel's card, a share of the width (the two-deck deck column's).
	constexpr float deckCardShare = 0.23f;
	// Inside the card, shares of its height: title, stems, the DeckPanel column, BPM, original.
	constexpr float titleLine = 0.05f, stemsLine = 0.04f, columnShare = 0.52f, bpmLine = 0.07f, bpmInfoLine = 0.04f;
	constexpr float pitchColumnShare = 0.32f;   // of the card's width: the fader beside value, range, KEY and SHIFT
	constexpr float captionFontShare = 0.8f;    // a meter's name, of its line
	constexpr float captionWidthShare = 0.34f;  // and at most this of its width: "MAIN" fits with room beside it

	juce::String meterName (outputs::oneDeck::MeteredBus bus)
	{
		switch (bus)
		{
			case outputs::oneDeck::MeteredBus::main: return "MAIN";
			case outputs::oneDeck::MeteredBus::aux:  return "AUX";
			case outputs::oneDeck::MeteredBus::cue:  return "CUE";
		}
		return {};
	}
}

//==============================================================================
OneDeckOutputs::OneDeckOutputs (OneDeckAudio& a) : audio (a)
{
	for (auto& meter : meters)
	{
		meter.setShowsClip (true);
		addAndMakeVisible (meter);
	}
}

void OneDeckOutputs::setMeterParameters (MeterParameters parameters)
{
	for (auto& meter : meters)
		meter.setParameters (parameters);
}

void OneDeckOutputs::refresh()
{
	// The buses are 16, 16 and 9 ambisonic channels, not stereo pairs: each bar is its bus's W.
	for (const auto bus : outputs::oneDeck::meteredBuses)
		meters[(size_t) bus].setLevel (audio.popPeak (outputs::oneDeck::wChannel (bus)));
}

void OneDeckOutputs::paint (juce::Graphics& g)
{
	const auto card = getLocalBounds().toFloat();
	g.setColour (Theme::panel);
	g.fillRoundedRectangle (card, Theme::corner (card));

	g.setColour (Theme::textDim);
	for (const auto bus : outputs::oneDeck::meteredBuses)
	{
		const auto& line = captions[(size_t) bus];
		const auto size = juce::jmin ((float) line.getHeight() * captionFontShare, (float) line.getWidth() * captionWidthShare);
		g.setFont (juce::FontOptions (size, juce::Font::bold));
		g.drawFittedText (meterName (bus), line, juce::Justification::centred, 1, 0.6f);
	}
}

void OneDeckOutputs::resized()
{
	const auto slots = surface::oneDeckMeterSlots (surface::fromJuce (getLocalBounds()), surface::metrics (getParentWidth()));
	for (size_t i = 0; i < slots.size(); ++i)
	{
		meters[i].setBounds (surface::toJuce (slots[i].bar));
		captions[i] = surface::toJuce (slots[i].caption);
	}
}

//==============================================================================
OneDeckView::OneDeckView (OneDeckAudio& a, juce::AudioFormatManager& formats, int index)
	: audio (a), deckIndex (index), thumbnails (formats, thumbCache), wave (a.host().player(), thumbnails, index),
	  panel (a.host().player(), thumbnails, index), events (a.host(), index, 1), outputRow (a)
{
	auto& host = audio.host();
	addAndMakeVisible (wave);
	addAndMakeVisible (panel);
	panel.addBandPartsTo (*this);
	panel.addTransportTo (*this);
	// Across instances on the link (S4.7): the owner makes the keys pressable
	// once it knows the truth has the link's ports.
	panel.setSyncAvailable (false);
	panel.onGridEdit = [this] (DeckPanel::GridAction action, double seconds)
	{
		audio.host().editGrid (action, seconds, std::nullopt);   // SHIFT GRID has no leader here
	};
	host.onAnalysing = [this] (bool analysing) { panel.setAnalysing (analysing); };

	shiftKey.setClickingTogglesState (true);
	knobsetup::untabbableKey (shiftKey);
	shiftKey.setColour (juce::TextButton::buttonOnColourId, Theme::deck (deckIndex));
	shiftKey.setTooltip ("The next pad starts now, not on the next bar");
	addAndMakeVisible (shiftKey);

	for (int s = 0; s < StemDeckPlayer::numStems; ++s)
	{
		auto& column = columns[(size_t) s];
		column = std::make_unique<StemColumn> (host.player(), host.spatialPorts(), s, [this] { return takeShift(); });
		column->oneShots().onPressed = [this, s] (motionlite::Shape shape, bool shift, std::uint64_t seed)
		{
			events.pressRecorded (s, shape, shift, seed);   // 3D REC on: the press is also an event
		};
		column->oneShots().placesOnly = [this] { return audio.host().placesPresses(); };
		addAndMakeVisible (*column);
	}
	addAndMakeVisible (outputRow);
	events.showOn (wave, panel.overviewWaveform());
	addAndMakeVisible (events);
}

OneDeckView::~OneDeckView()
{
	audio.host().onAnalysing = nullptr;
}

// SHIFT holds for one press.
bool OneDeckView::takeShift()
{
	const auto now = shiftKey.getToggleState();
	shiftKey.setToggleState (false, juce::dontSendNotification);
	return now;
}

bool OneDeckView::spaceKey (bool down, bool typing)
{
	const auto pressed = down && ! spaceWasDown && ! typing;
	spaceWasDown = down;
	if (pressed)
		panel.playPressed();   // the PLAY key's own path: CUE's preview, the play mode, the panel's light
	return pressed;
}

void OneDeckView::setSet (const StemSet& set)
{
	thumbnails.setSet (set);
	panel.setSet (set);
	wave.setTitle (set.name);
	for (int s = 0; s < StemDeckPlayer::numStems; ++s)
		columns[(size_t) s]->setLoaded (set.hasStem (s));
}

void OneDeckView::refresh()
{
	wave.repaint();
	panel.refresh();
	events.refresh();
	for (auto& column : columns)
		column->refresh();
	outputRow.refresh();
}

void OneDeckView::saveState (DeckSession& deck) const
{
	panel.saveState (deck);
	for (size_t s = 0; s < columns.size(); ++s)
		columns[s]->saveState (deck.stems[s]);
}

void OneDeckView::restoreState (const DeckSession& deck)
{
	panel.restoreState (deck);
	for (size_t s = 0; s < columns.size(); ++s)
		columns[s]->restoreState (deck.stems[s]);
}

void OneDeckView::paint (juce::Graphics& g)
{
	g.fillAll (Theme::background);
	const auto card = deckCard.toFloat();
	g.setColour (Theme::panel);
	g.fillRoundedRectangle (card, Theme::corner (card));
	// The transport on its own tile, as in the two-deck column.
	const auto tile = transportTile.toFloat();
	g.setColour (Theme::panelRaised);
	g.fillRoundedRectangle (tile, Theme::corner (tile));
}

void OneDeckView::resized()
{
	const auto height = (float) getHeight();
	const auto share = [] (int whole, float part) { return juce::roundToInt ((float) whole * part); };
	const auto gap = surface::metrics (getWidth()).gap;
	auto area = getLocalBounds();

	wave.setBounds (area.removeFromTop (share (getHeight(), waveShare)));
	area.removeFromTop (gap / 2);
	events.setBounds (area.removeFromTop (share (getHeight(), eventStripShare)));
	area.removeFromTop (gap / 2);
	surface::DeckTile parts;
	parts.overview = surface::fromJuce (area.removeFromTop (share (getHeight(), overviewShare)));
	area.removeFromTop (gap);
	const auto foot = surface::oneDeckFoot (surface::fromJuce (area.removeFromBottom (juce::roundToInt (height * footShare))),
											surface::metrics (getWidth()));
	transportTile = surface::toJuce (foot.transport.tile);
	panel.setTransportBounds (foot.transport);
	outputRow.setBounds (surface::toJuce (foot.metersTile));
	area.removeFromBottom (gap);

	// The deck panel's card: title and stems, DeckPanel's column, BPM, then
	// the pitch fader beside its value, range, KEY and SHIFT.
	deckCard = area.removeFromLeft (share (getWidth(), deckCardShare));
	area.removeFromLeft (gap);
	auto card = deckCard.reduced (gap);
	const auto cardHeight = card.getHeight();
	parts.title = surface::fromJuce (card.removeFromTop (share (cardHeight, titleLine)));
	parts.stems = surface::fromJuce (card.removeFromTop (share (cardHeight, stemsLine)));
	panel.setBounds (card.removeFromTop (share (cardHeight, columnShare)).expanded (gap, 0));
	parts.bpm = surface::fromJuce (card.removeFromTop (share (cardHeight, bpmLine)));
	parts.bpmInfo = surface::fromJuce (card.removeFromTop (share (cardHeight, bpmInfoLine)));
	card.removeFromTop (gap);
	parts.pitch = surface::fromJuce (card.removeFromLeft (share (card.getWidth(), pitchColumnShare)));
	card.removeFromLeft (gap);
	const auto key = (card.getHeight() - 3 * gap) / 4;
	parts.pitchValue = surface::fromJuce (card.removeFromTop (key));
	card.removeFromTop (gap);
	parts.range = surface::fromJuce (card.removeFromTop (key));
	card.removeFromTop (gap);
	parts.keyLock = surface::fromJuce (card.removeFromTop (key));
	card.removeFromTop (gap);
	shiftKey.setBounds (card);
	panel.setBandBounds (parts);

	// The four stems share the rest.
	const auto columnWidth = (area.getWidth() - gap * (StemDeckPlayer::numStems - 1)) / StemDeckPlayer::numStems;
	for (auto& column : columns)
	{
		column->setBounds (area.removeFromLeft (columnWidth));
		area.removeFromLeft (gap);
	}
}
