// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>

#include "EventMarkerLayout.h"
#include "OverviewGesture.h"
#include "StemThumbnails.h"
#include "Theme.h"
#include "TwoDeckFixture.h"
#include "Waveforms.h"

#include <iostream>
#include <vector>

// What a finger does on the two waveforms. The rolling waveform shows a
// marker per planned event in its stem's lane; a tap on one selects it and
// leaves the playhead where it is, a drag scrubs. The overview has no
// markers: a tap jumps, only a deliberate drag sets a loop.
namespace
{
constexpr int waveWidth = 756, waveHeight = 113;          // the rig's rolling waveform
constexpr int overviewWidth = 756, overviewHeight = 44;   // the rig's overview: 6 % of the view
constexpr double bpm = 120.0;                             // a beat is 0.5 s; the noise is 3 s long
constexpr int markedStem = 2;
constexpr double markedBeat = 3.0;                        // 1.5 s

juce::MouseEvent mouseAt (juce::Component& component, juce::Point<float> where, juce::Point<float> downAt)
{
	const auto now = juce::Time::getCurrentTime();
	return { juce::Desktop::getInstance().getMainMouseSource(), where, juce::ModifierKeys::leftButtonModifier,
			 juce::MouseInputSource::defaultPressure, 0.0f, 0.0f, 0.0f, 0.0f, &component, &component, now, downAt, now, 1,
			 where != downAt };
}

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

class RollingMarkers : public twodeck::TwoDeckSpatial
{
protected:
	void SetUp() override
	{
		TwoDeckSpatial::SetUp();
		player.setBeatGrid ({ bpm, 0.0 });
		wave.setLookAndFeel (&look);
		wave.setSize (waveWidth, waveHeight);
		stemevents::Event event;
		event.stem = markedStem;
		event.beat = markedBeat;
		ASSERT_EQ (list.add (event), stemevents::EventList::Add::added);
		player.jumpTo (1.0);   // on a beat: the marker half a second right of the playhead
	}

	void TearDown() override
	{
		wave.setLookAndFeel (nullptr);
		TwoDeckSpatial::TearDown();
	}

	void press (juce::Point<float> at) { wave.mouseDown (mouseAt (wave, at, at)); }
	void dragTo (juce::Point<float> from, juce::Point<float> to) { wave.mouseDrag (mouseAt (wave, to, from)); }
	void release (juce::Point<float> from, juce::Point<float> to) { wave.mouseUp (mouseAt (wave, to, from)); }

	void tap (juce::Point<float> where)
	{
		press (where);
		release (where, where);
	}

	eventmarkers::Rect rectOf (const stemevents::Event& event) const
	{
		const auto rect = eventmarkers::markerRect (event, event.beat * 60.0 / bpm, wave.markerWindow());
		EXPECT_TRUE (rect.has_value()) << "the marker is in view";
		return rect.value_or (eventmarkers::Rect { 0.0f, 0.0f, 0.0f, 0.0f });
	}

	eventmarkers::Rect markerRect() const { return rectOf (list.events().front()); }

	juce::Point<float> markerCentre() const
	{
		const auto r = markerRect();
		return { r.x + r.w * 0.5f, r.y + r.h * 0.5f };
	}

	juce::Image snapshot() { return wave.createComponentSnapshot (wave.getLocalBounds()); }

	juce::ScopedJuceInitialiser_GUI juceInitialiser;
	DJLookAndFeel look;
	juce::AudioThumbnailCache thumbCache { 4 };
	StemThumbnails thumbnails { formats, thumbCache };
	ScrollingWaveform wave { player, thumbnails, 0 };
	stemevents::EventList list;
	std::vector<int> tapped;
};
}

TEST_F (RollingMarkers, TheMarkerStandsAtItsTimeInItsLane)
{
	const auto centre = markerCentre();
	const auto pixelsPerSecond = 1.0f / (float) wave.markerWindow().secondsPerPixel;
	EXPECT_NEAR (centre.x, (float) waveWidth * 0.5f + 0.5f * pixelsPerSecond, 1.0f) << "half a second after the playhead";
	const auto lane = (float) waveHeight / (float) StemSet::numStems;
	EXPECT_GT (centre.y, lane * (float) markedStem);
	EXPECT_LT (centre.y, lane * (float) (markedStem + 1));
}

TEST_F (RollingMarkers, AMarkerTapSelectsAndDoesNotScrub)
{
	wave.setEvents (&list, -1);
	wave.onMarkerTapped = [&] (int index) { tapped.push_back (index); };
	const auto before = player.getPosition();

	tap (markerCentre());

	EXPECT_EQ (tapped, std::vector<int> { 0 });
	EXPECT_DOUBLE_EQ (player.getPosition(), before);
	EXPECT_FALSE (player.hasLoop());
}

// A finger wobbles: within a fingertip it is still a tap, and the view holds still.
TEST_F (RollingMarkers, AWobbleOnAMarkerIsStillATap)
{
	wave.setEvents (&list, -1);
	wave.onMarkerTapped = [&] (int index) { tapped.push_back (index); };
	const auto before = player.getPosition();
	const auto from = markerCentre();
	const auto wobble = from + juce::Point<float> (3.0f, -2.0f);

	press (from);
	dragTo (from, wobble);
	EXPECT_DOUBLE_EQ (player.getPosition(), before) << "no scrub while it is a tap";
	release (from, wobble);

	EXPECT_EQ (tapped, std::vector<int> { 0 });
	EXPECT_DOUBLE_EQ (player.getPosition(), before);
}

// A drag that began on a marker scrubs like any other and selects nothing.
TEST_F (RollingMarkers, ADragThatBeganOnAMarkerScrubs)
{
	wave.setEvents (&list, -1);
	wave.onMarkerTapped = [&] (int index) { tapped.push_back (index); };
	const auto before = player.getPosition();
	const auto from = markerCentre();
	const auto to = from + juce::Point<float> (60.0f, 0.0f);

	press (from);
	dragTo (from, to);
	release (from, to);

	EXPECT_TRUE (tapped.empty());
	EXPECT_NEAR (player.getPosition(), before - 60.0 * wave.markerWindow().secondsPerPixel, 0.02)
		<< "the track follows the finger";
}

// The rolling waveform has no tap action of its own: a tap beside the markers does nothing.
TEST_F (RollingMarkers, ATapElsewhereDoesNothing)
{
	wave.setEvents (&list, -1);
	wave.onMarkerTapped = [&] (int index) { tapped.push_back (index); };
	const auto before = player.getPosition();

	tap ({ (float) waveWidth * 0.1f, 5.0f });

	EXPECT_TRUE (tapped.empty());
	EXPECT_DOUBLE_EQ (player.getPosition(), before);
	EXPECT_FALSE (player.hasLoop());
}

TEST_F (RollingMarkers, WithoutAListNothingIsHit)
{
	wave.setEvents (nullptr, -1);
	wave.onMarkerTapped = [&] (int index) { tapped.push_back (index); };
	tap (markerCentre());
	EXPECT_TRUE (tapped.empty());
}

// Out of view, a marker is neither drawn nor hit; nothing is pushed to the edge.
TEST_F (RollingMarkers, AMarkerOutOfViewIsNotDrawnNorHit)
{
	stemevents::EventList far;
	stemevents::Event event;
	event.stem = markedStem;
	event.beat = 40.0;   // 20 s: far beyond the view
	ASSERT_EQ (far.add (event), stemevents::EventList::Add::added);
	ASSERT_FALSE (eventmarkers::markerRect (event, 20.0, wave.markerWindow()).has_value());

	wave.setEvents (nullptr, -1);
	const auto none = snapshot();
	wave.setEvents (&far, -1);
	EXPECT_TRUE (differenceBounds (none, snapshot()).isEmpty());

	wave.onMarkerTapped = [&] (int index) { tapped.push_back (index); };
	const auto lane = (float) waveHeight / (float) StemSet::numStems;
	tap ({ (float) waveWidth - 2.0f, lane * ((float) markedStem + 0.5f) });
	EXPECT_TRUE (tapped.empty());
}

// A page test must paint: each state is a picture, and each differs from the
// last in the marker's place only.
TEST_F (RollingMarkers, PaintsEveryState)
{
	wave.setEvents (nullptr, -1);
	const auto none = snapshot();

	wave.setEvents (&list, -1);
	const auto normal = snapshot();

	wave.setEvents (&list, 0);
	const auto selected = snapshot();

	ASSERT_TRUE (list.setActive (0, false));
	wave.setEvents (&list, -1);
	const auto inactive = snapshot();

	const auto r = markerRect();
	const auto place = juce::Rectangle<float> (r.x, r.y, r.w, r.h).expanded (6.0f).getSmallestIntegerContainer();

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
	const auto activePixel = normal.getPixelAt ((int) r.x + 2, (int) centre.y);
	const auto dimPixel = inactive.getPixelAt ((int) r.x + 2, (int) centre.y);
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

// Paint cost: the rolling waveform repaints on every deck tick. 512 markers
// must not cost a text layout or an allocation each; the mean of 20 paints
// stays far under what that would take (~1 ms per 100 text layouts and more).
TEST_F (RollingMarkers, PaintsADenseLaneInTime)
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
	wave.setEvents (nullptr, -1);
	const auto baselineMs = meanPaintMs();
	std::cout << "[ INFO     ] no markers: mean paint " << baselineMs << " ms\n";
	for (const bool oneLane : { true, false })
	{
		list = {};
		fillDense (list, oneLane);
		ASSERT_EQ ((int) list.events().size(), denseCount);
		const int selected = 7;
		wave.setEvents (&list, selected);
		const auto meanMs = meanPaintMs() - baselineMs;
		std::cout << "[ INFO     ] " << (oneLane ? "one lane" : "four lanes") << ": extra paint " << meanMs << " ms\n";
		EXPECT_LT (meanMs, generousBoundMs) << (oneLane ? "one lane" : "four lanes");

		// Letter-less markers still render in their stem's colour; the selected one is outlined.
		const auto image = snapshot();
		const auto other = rectOf (list.events()[300]);
		const auto stemColour = Theme::stem (list.events()[300].stem);
		const auto fill = image.getPixelAt ((int) (other.x + other.w * 0.5f), (int) (other.y + other.h * 0.5f));
		EXPECT_NEAR (fill.getRed(), stemColour.getRed(), 24);
		EXPECT_NEAR (fill.getBlue(), stemColour.getBlue(), 24);

		const auto picked = rectOf (list.events()[(size_t) selected]);
		const auto outline = image.getPixelAt ((int) (picked.x - 1.0f), (int) (picked.y + picked.h * 0.5f));
		EXPECT_GT (outline.getRed(), 200);
		EXPECT_GT (outline.getGreen(), 200);
		EXPECT_GT (outline.getBlue(), 200);
	}
}

TEST_F (RollingMarkers, TheSecondsFollowTheGrid)
{
	wave.setEvents (&list, -1);
	const auto before = markerRect();
	snapshot();
	player.setBeatGrid ({ 60.0, 0.0 });   // a beat is 1 s: the event at beat 3 moves from 1.5 s to 3 s
	const auto none = [&] { wave.setEvents (nullptr, -1); return snapshot(); }();
	wave.setEvents (&list, -1);
	const auto moved = snapshot();
	const auto r = eventmarkers::markerRect (list.events().front(), 3.0, wave.markerWindow());
	ASSERT_TRUE (r.has_value());
	EXPECT_GT (r->x, before.x);
	EXPECT_FALSE (differenceBounds (none, moved).isEmpty());
	EXPECT_TRUE (juce::Rectangle<float> (r->x, r->y, r->w, r->h).expanded (6.0f).getSmallestIntegerContainer().contains (differenceBounds (none, moved)));
}

//==============================================================================
namespace
{
class OverviewTaps : public twodeck::TwoDeckSpatial
{
protected:
	void SetUp() override
	{
		TwoDeckSpatial::SetUp();
		player.setBeatGrid ({ bpm, 0.0 });
		overview.setLookAndFeel (&look);
		overview.setSize (overviewWidth, overviewHeight);
	}

	void TearDown() override
	{
		overview.setLookAndFeel (nullptr);
		TwoDeckSpatial::TearDown();
	}

	void gesture (float fromX, float toX)
	{
		const juce::Point<float> from { fromX, 5.0f }, to { toX, 5.0f };
		overview.mouseDown (mouseAt (overview, from, from));
		overview.mouseDrag (mouseAt (overview, to, from));
		overview.mouseUp (mouseAt (overview, to, from));
	}

	juce::ScopedJuceInitialiser_GUI juceInitialiser;
	DJLookAndFeel look;
	juce::AudioThumbnailCache thumbCache { 4 };
	StemThumbnails thumbnails { formats, thumbCache };
	OverviewWaveform overview { player, thumbnails };
};
}

TEST_F (OverviewTaps, ATapJumps)
{
	gesture ((float) overviewWidth * 0.5f, (float) overviewWidth * 0.5f);   // 1.5 s, on a beat
	EXPECT_NEAR (player.getPosition(), 1.5, 0.01);
	EXPECT_FALSE (player.hasLoop());
}

// A finger's jitter on the touch screen is a tap: it jumps and sets no loop.
TEST_F (OverviewTaps, AJitteryTapSetsNoLoop)
{
	gesture ((float) overviewWidth * 0.5f, (float) overviewWidth * 0.5f + 8.0f);
	EXPECT_FALSE (player.hasLoop());
	EXPECT_NEAR (player.getPosition(), 1.5, 0.01) << "QUANTIZE: the release lands on the beat at 1.5 s";
}

TEST_F (OverviewTaps, ADeliberateDragSetsALoop)
{
	const auto from = (float) overviewWidth * 0.25f;
	gesture (from, from + (float) overviewWidth * overviewgesture::loopShare * 4.0f);
	EXPECT_TRUE (player.hasLoop());
}
