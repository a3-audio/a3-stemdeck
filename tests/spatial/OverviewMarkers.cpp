// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>

#include "EventMarkerLayout.h"
#include "StemThumbnails.h"
#include "Theme.h"
#include "TwoDeckFixture.h"
#include "Waveforms.h"

#include <vector>

// The overview shows a marker per planned event in its stem's lane, and a tap
// on one selects it instead of jumping the playhead.
namespace
{
constexpr int overviewWidth = 756, overviewHeight = 44;   // the rig's overview: 6 % of the view
constexpr double bpm = 120.0;                             // a beat is 0.5 s; the noise is 3 s long
constexpr int markedStem = 2;
constexpr double markedBeat = 3.0;                        // 1.5 s

class OverviewMarkers : public twodeck::TwoDeckSpatial
{
protected:
	void SetUp() override
	{
		TwoDeckSpatial::SetUp();
		player.setBeatGrid ({ bpm, 0.0 });
		overview.setLookAndFeel (&look);
		overview.setSize (overviewWidth, overviewHeight);
		stemevents::Event event;
		event.stem = markedStem;
		event.beat = markedBeat;
		ASSERT_EQ (list.add (event), stemevents::EventList::Add::added);
	}

	void TearDown() override
	{
		overview.setLookAndFeel (nullptr);
		TwoDeckSpatial::TearDown();
	}

	juce::MouseEvent mouseAt (juce::Point<float> where)
	{
		const auto now = juce::Time::getCurrentTime();
		return { juce::Desktop::getInstance().getMainMouseSource(), where, juce::ModifierKeys::leftButtonModifier,
				 juce::MouseInputSource::defaultPressure, 0.0f, 0.0f, 0.0f, 0.0f, &overview, &overview, now, where, now, 1, false };
	}

	void tap (juce::Point<float> where)
	{
		overview.mouseDown (mouseAt (where));
		overview.mouseUp (mouseAt (where));
	}

	eventmarkers::Rect markerRect() const
	{
		const auto lane = (float) overviewHeight / (float) StemSet::numStems;
		return eventmarkers::markerRect (list.events().front(), lane * (float) markedStem, lane, (float) overviewWidth,
										 markedBeat * 60.0 / bpm, player.getLength());
	}

	juce::Point<float> markerCentre() const
	{
		const auto r = markerRect();
		return { r.x + r.w * 0.5f, r.y + r.h * 0.5f };
	}

	juce::Image snapshot()
	{
		overview.refresh();
		return overview.createComponentSnapshot (overview.getLocalBounds());
	}

	juce::ScopedJuceInitialiser_GUI juceInitialiser;
	DJLookAndFeel look;
	juce::AudioThumbnailCache thumbCache { 4 };
	StemThumbnails thumbnails { formats, thumbCache };
	OverviewWaveform overview { player, thumbnails };
	stemevents::EventList list;
};

// Where the two pictures differ, as the bounding box of the differing pixels.
juce::Rectangle<int> differenceBounds (const juce::Image& a, const juce::Image& b)
{
	const juce::Image::BitmapData first (a, juce::Image::BitmapData::readOnly), second (b, juce::Image::BitmapData::readOnly);
	juce::Rectangle<int> bounds;
	for (int y = 0; y < a.getHeight(); ++y)
		for (int x = 0; x < a.getWidth(); ++x)
			if (first.getPixelColour (x, y) != second.getPixelColour (x, y))
				bounds = bounds.getUnion ({ x, y, 1, 1 });
	return bounds;
}
}

TEST_F (OverviewMarkers, AMarkerTapDoesNotJump)
{
	overview.setEvents (&list, -1);
	std::vector<int> tapped;
	overview.onMarkerTapped = [&] (int index) { tapped.push_back (index); };
	player.jumpTo (0.25);
	const auto before = player.getPosition();

	tap (markerCentre());

	EXPECT_EQ (tapped, std::vector<int> { 0 });
	EXPECT_DOUBLE_EQ (player.getPosition(), before);
	EXPECT_FALSE (player.hasLoop());
}

TEST_F (OverviewMarkers, ATapElsewhereStillJumps)
{
	overview.setEvents (&list, -1);
	int tapped = 0;
	overview.onMarkerTapped = [&] (int) { ++tapped; };

	tap ({ (float) overviewWidth * 0.1f, 5.0f });   // 0.3 s, away from the marker at 1.5 s

	EXPECT_EQ (tapped, 0);
	EXPECT_NEAR (player.getPosition(), 0.5, 0.01);   // QUANTIZE: 0.3 s lands on the beat at 0.5 s
}

TEST_F (OverviewMarkers, WithoutAListATapJumps)
{
	overview.setEvents (nullptr, -1);
	tap (markerCentre());
	EXPECT_NEAR (player.getPosition(), 1.5, 0.01);
}

// A page test must paint: each state is a picture, and each differs from the
// last in the marker's place only.
TEST_F (OverviewMarkers, PaintsEveryState)
{
	overview.setEvents (nullptr, -1);
	const auto none = snapshot();

	overview.setEvents (&list, -1);
	const auto normal = snapshot();

	overview.setEvents (&list, 0);
	const auto selected = snapshot();

	ASSERT_TRUE (list.setActive (0, false));
	overview.setEvents (&list, -1);
	const auto inactive = snapshot();

	const auto r = markerRect();
	const auto place = juce::Rectangle<float> (r.x, r.y, r.w, r.h).expanded (3.0f).getSmallestIntegerContainer();

	for (const auto* shot : { &normal, &selected, &inactive })
	{
		const auto changed = differenceBounds (none, *shot);
		EXPECT_FALSE (changed.isEmpty());
		EXPECT_TRUE (place.contains (changed)) << changed.toString() << " outside " << place.toString();
	}

	EXPECT_FALSE (differenceBounds (normal, selected).isEmpty());
	EXPECT_FALSE (differenceBounds (normal, inactive).isEmpty());
	EXPECT_FALSE (differenceBounds (selected, inactive).isEmpty());

	// In the stem's colour, and the inactive one dimmer than the active.
	const auto stemColour = Theme::stem (markedStem);
	const auto centre = markerCentre();
	const auto activePixel = normal.getPixelAt ((int) r.x + 1, (int) centre.y);
	const auto dimPixel = inactive.getPixelAt ((int) r.x + 1, (int) centre.y);
	EXPECT_NEAR (activePixel.getRed(), stemColour.getRed(), 24);
	EXPECT_LT (dimPixel.getBrightness(), activePixel.getBrightness());
}
