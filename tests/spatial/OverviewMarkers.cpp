// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>

#include "EventMarkerLayout.h"
#include "StemThumbnails.h"
#include "Theme.h"
#include "TwoDeckFixture.h"
#include "Waveforms.h"

#include <iostream>
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

namespace
{
constexpr int denseCount = stemevents::maxEvents;

// 512 events in the track's first 6 beats: one lane, or round the four.
void fillDense (stemevents::EventList& list, bool oneLane)
{
	for (int i = 0; i < denseCount; ++i)
	{
		stemevents::Event event;
		event.stem = oneLane ? 1 : i % StemSet::numStems;
		event.beat = 0.01 * i + (oneLane ? 0.0 : 0.002 * (i % StemSet::numStems));
		ASSERT_EQ (list.add (event), stemevents::EventList::Add::added);
	}
}
}

// Paint cost: the overview repaints on every deck tick. 512 markers must not
// cost a text layout or an allocation each; the mean of 20 paints stays far
// under what that would take (it was ~1 ms per 100 text layouts and more).
TEST_F (OverviewMarkers, PaintsADenseLaneInTime)
{
	constexpr double generousBoundMs = 4.0;   // on top of a paint without markers
	const auto meanPaintMs = [&]
	{
		snapshot();
		const auto start = juce::Time::getMillisecondCounterHiRes();
		for (int i = 0; i < 20; ++i)
			snapshot();
		return (juce::Time::getMillisecondCounterHiRes() - start) / 20.0;
	};
	overview.setEvents (nullptr, -1);
	const auto baselineMs = meanPaintMs();
	std::cout << "[ INFO     ] no markers: mean paint " << baselineMs << " ms\n";
	for (const bool oneLane : { true, false })
	{
		list = {};
		fillDense (list, oneLane);
		ASSERT_EQ ((int) list.events().size(), denseCount);
		const int selected = 7;
		overview.setEvents (&list, selected);
		const auto meanMs = meanPaintMs() - baselineMs;
		std::cout << "[ INFO     ] " << (oneLane ? "one lane" : "four lanes") << ": extra paint " << meanMs << " ms\n";
		EXPECT_LT (meanMs, generousBoundMs) << (oneLane ? "one lane" : "four lanes");

		// Letter-less markers still render in their stem's colour; the selected one is outlined.
		const auto image = snapshot();
		const auto lane = (float) overviewHeight / (float) StemSet::numStems;
		const auto rectOf = [&] (int index)
		{
			const auto& event = list.events()[(size_t) index];
			return eventmarkers::markerRect (event, lane * (float) event.stem, lane, (float) overviewWidth,
											 event.beat * 60.0 / bpm, player.getLength());
		};
		const auto other = rectOf (300);
		const auto stemColour = Theme::stem (list.events()[300].stem);
		const auto fill = image.getPixelAt ((int) (other.x + other.w * 0.5f), (int) (other.y + other.h * 0.5f));
		EXPECT_NEAR (fill.getRed(), stemColour.getRed(), 24);
		EXPECT_NEAR (fill.getBlue(), stemColour.getBlue(), 24);

		const auto picked = rectOf (selected);
		const auto outline = image.getPixelAt ((int) (picked.x - 1.0f), (int) (picked.y + picked.h * 0.5f));
		EXPECT_GT (outline.getRed(), 200);
		EXPECT_GT (outline.getGreen(), 200);
		EXPECT_GT (outline.getBlue(), 200);
	}
}

TEST_F (OverviewMarkers, TheSecondsFollowTheGrid)
{
	overview.setEvents (&list, -1);
	const auto before = markerRect();
	overview.refresh();
	snapshot();
	player.setBeatGrid ({ 60.0, 0.0 });   // a beat is 1 s: the event at beat 3 moves from 1.5 s to 3 s
	const auto none = [&] { overview.setEvents (nullptr, -1); return snapshot(); }();
	overview.setEvents (&list, -1);
	const auto moved = snapshot();
	const auto r = eventmarkers::markerRect (list.events().front(), (float) overviewHeight / 4.0f * (float) markedStem,
											 (float) overviewHeight / 4.0f, (float) overviewWidth, 3.0, player.getLength());
	EXPECT_GT (r.x, before.x);
	EXPECT_FALSE (differenceBounds (none, moved).isEmpty());
	EXPECT_TRUE (juce::Rectangle<float> (r.x, r.y, r.w, r.h).expanded (3.0f).getSmallestIntegerContainer().contains (differenceBounds (none, moved)));
}
