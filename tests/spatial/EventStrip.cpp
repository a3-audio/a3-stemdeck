// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>

#include "EventMarkerLayout.h"
#include "EventStrip.h"
#include "OneDeckAudio.h"
#include "OneDeckView.h"
#include "StemThumbnails.h"
#include "Theme.h"
#include "TwoDeckFixture.h"
#include "Waveforms.h"

#include <string>
#include <utility>
#include <vector>

// The event strip on a real deck: a set loaded with a cached grid (120 BPM,
// a beat is 0.5 s, the noise is 3 s long), its rolling waveform, a clock the
// test turns.
namespace
{
constexpr double bpm = 120.0;
constexpr int waveWidth = 756, waveHeight = 113;   // the rig's rolling waveform

class EventStripTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		ASSERT_TRUE (folder.createDirectory().wasOk());
		formats.registerBasicFormats();
		set.name = "noise";
		for (int s = 0; s < StemDeckPlayer::numStems; ++s)
		{
			set.files[(size_t) s] = folder.getChildFile ("stem" + juce::String (s) + ".wav");
			set.stemNames[(size_t) s] = "stem" + juce::String (s);
			twodeck::writeNoiseStem (set.files[(size_t) s], 41 + s);
		}
		cache.store (set, { bpm, 0.0 });
		audio.prepareToPlay (twodeck::block, twodeck::rate);
		ASSERT_EQ (host().load (set), juce::String());
		ASSERT_TRUE (host().hasGrid());

		wave.setLookAndFeel (&look);
		wave.setSize (waveWidth, waveHeight);
		strip.secondsNow = [this] { return clock; };
		strip.showOn (wave, overview);
		strip.refresh();
	}

	void TearDown() override
	{
		wave.setLookAndFeel (nullptr);
		pool.removeAllJobs (true, 5000);
		audio.releaseResources();
	}

	~EventStripTest() override { folder.deleteRecursively(); }

	DeckHost& host() { return audio.host(); }
	const std::vector<stemevents::Event>& events() { return host().trackEvents().list().events(); }

	void addEvent (int stem, double beat)
	{
		stemevents::Event e;
		e.stem = stem;
		e.beat = beat;
		ASSERT_TRUE (host().changeEvents ([e] (stemevents::EventList& list) { return list.add (e) == stemevents::EventList::Add::added; }));
		strip.refresh();
	}

	juce::Rectangle<int> markerArea (const stemevents::Event& e)
	{
		const auto r = eventmarkers::markerRect (e, e.beat * 60.0 / bpm, wave.markerWindow());
		EXPECT_TRUE (r.has_value()) << "the marker is in view";
		const auto rect = r.value_or (eventmarkers::Rect { 0.0f, 0.0f, 0.0f, 0.0f });
		return juce::Rectangle<float> (rect.x, rect.y, rect.w, rect.h).getSmallestIntegerContainer();
	}

	juce::Image waveShot() { return wave.createComponentSnapshot (wave.getLocalBounds()); }

	juce::ScopedJuceInitialiser_GUI juceInitialiser;
	juce::File folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("stemdeck-strip", "");
	juce::AudioFormatManager formats;
	StemSet set;
	juce::ThreadPool pool;
	AnalysisCache cache { folder.getChildFile ("analysis.xml") };
	OneDeckAudio audio { formats, pool, cache };
	DJLookAndFeel look;
	juce::AudioThumbnailCache thumbCache { 4 };
	StemThumbnails thumbnails { formats, thumbCache };
	ScrollingWaveform wave { audio.host().player(), thumbnails, 0 };
	OverviewWaveform overview { audio.host().player(), thumbnails };
	EventStrip strip { audio.host(), 0, 1 };
	double clock = 100.0;
};

// Where two pictures differ, as the bounding box of the differing pixels.
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

double lumaVariance (const juce::Image& image)
{
	const juce::Image::BitmapData pixels (image, juce::Image::BitmapData::readOnly);
	double sum = 0.0, squares = 0.0;
	const auto count = (double) image.getWidth() * image.getHeight();
	for (int y = 0; y < image.getHeight(); ++y)
		for (int x = 0; x < image.getWidth(); ++x)
		{
			const auto colour = pixels.getPixelColour (x, y);
			const double luma = (colour.getRed() + colour.getGreen() + colour.getBlue()) / 3.0;
			sum += luma;
			squares += luma * luma;
		}
	const auto mean = sum / count;
	return squares / count - mean * mean;
}
}

// A beat on, then a bar back: the event moves by whole beats, stays selected
// through the new sort order, and the rolling waveform paints it where it went.
TEST_F (EventStripTest, NudgeMovesTheSelectedEvent)
{
	addEvent (2, 1.0);
	addEvent (1, 1.5);   // sorts after the moved one once it is at 2.0
	strip.select (0);
	const auto before = waveShot();
	const auto from = markerArea (events()[0]);

	strip.nudgeKey (EventStrip::Nudge::beatForward).onClick();
	ASSERT_EQ (events().size(), 2u);
	EXPECT_DOUBLE_EQ (events()[1].beat, 2.0);
	EXPECT_EQ (events()[1].stem, 2);
	EXPECT_EQ (strip.selected(), 1) << "the selection follows the event, not its old index";

	const auto changed = differenceBounds (before, waveShot());
	EXPECT_TRUE (changed.intersects (from)) << "the marker left its place";
	EXPECT_TRUE (changed.intersects (markerArea (events()[1]))) << "and stands where it went";

	strip.nudgeKey (EventStrip::Nudge::barBack).onClick();
	EXPECT_EQ (strip.selected(), 0);
	EXPECT_DOUBLE_EQ (events()[0].beat, -2.0) << "a bar is four beats; before the one is allowed";
	strip.nudgeKey (EventStrip::Nudge::barForward).onClick();
	strip.nudgeKey (EventStrip::Nudge::beatBack).onClick();
	ASSERT_GE (strip.selected(), 0);
	EXPECT_DOUBLE_EQ (events()[(size_t) strip.selected()].beat, 1.0);
	EXPECT_EQ (strip.note(), juce::String());
}

// The strip's edits are the DJ's: a prepared event nudged or switched is a hand one.
TEST_F (EventStripTest, AnEditOnTheStripMakesAPreparedEventAHandOne)
{
	stemevents::Event e;
	e.stem = 1;
	e.beat = 2.0;
	e.origin = stemevents::Origin::prepared;
	ASSERT_TRUE (host().changeEvents ([e] (stemevents::EventList& list) { return list.add (e) == stemevents::EventList::Add::added; }));
	e.stem = 2;
	ASSERT_TRUE (host().changeEvents ([e] (stemevents::EventList& list) { return list.add (e) == stemevents::EventList::Add::added; }));
	strip.refresh();

	strip.select (0);
	strip.nudgeKey (EventStrip::Nudge::beatForward).onClick();
	ASSERT_GE (strip.selected(), 0);
	EXPECT_EQ (events()[(size_t) strip.selected()].origin, stemevents::Origin::hand);

	const auto second = host().trackEvents().list().indexAt (2, 2.0);
	ASSERT_GE (second, 0);
	EXPECT_EQ (events()[(size_t) second].origin, stemevents::Origin::prepared);
	strip.select (second);
	strip.activeKey().onClick();
	EXPECT_EQ (events()[(size_t) strip.selected()].origin, stemevents::Origin::hand);
}

TEST_F (EventStripTest, ANudgeOntoATakenSlotIsRefusedAndSaysSo)
{
	addEvent (2, 1.0);
	addEvent (2, 2.0);
	strip.select (0);
	strip.nudgeKey (EventStrip::Nudge::beatForward).onClick();
	EXPECT_DOUBLE_EQ (events()[0].beat, 1.0) << "the slot is protected";
	EXPECT_DOUBLE_EQ (events()[1].beat, 2.0);
	EXPECT_EQ (strip.selected(), 0);
	EXPECT_EQ (strip.note(), juce::String ("SLOT TAKEN"));
	clock += EventStrip::refusedNoteSeconds + 0.1;
	EXPECT_EQ (strip.note(), juce::String());
}

TEST_F (EventStripTest, ActiveToggleDimsTheMarker)
{
	addEvent (1, 2.0);
	strip.select (0);
	strip.refresh();
	EXPECT_TRUE (strip.activeKey().getToggleState()) << "lit: the event plays";
	const auto lit = waveShot();

	strip.activeKey().onClick();
	EXPECT_FALSE (events()[0].active);
	EXPECT_FALSE (strip.activeKey().getToggleState());
	const auto dim = waveShot();

	const auto r = markerArea (events()[0]);
	const auto at = juce::Point<int> (r.getX() + 1, r.getCentreY());
	EXPECT_LT (dim.getPixelAt (at.x, at.y).getBrightness(), lit.getPixelAt (at.x, at.y).getBrightness());

	strip.activeKey().onClick();
	EXPECT_TRUE (events()[0].active) << "one tap back";
}

TEST_F (EventStripTest, RecKeyIsOffWithoutAGrid)
{
	host().player().setBeatGrid ({});
	strip.refresh();
	for (auto* key : { &strip.recKey(), &strip.eventsKey(), &strip.quantizeKey() })
	{
		EXPECT_FALSE (key->isEnabled()) << key->getButtonText();
		EXPECT_FALSE (key->getToggleState()) << key->getButtonText();
	}
	EXPECT_EQ (strip.note(), juce::String ("NO GRID"));
	strip.recKey().onClick();
	EXPECT_FALSE (host().isRecording());

	host().player().setBeatGrid ({ bpm, 0.0 });
	strip.refresh();
	EXPECT_TRUE (strip.recKey().isEnabled());
	EXPECT_TRUE (strip.quantizeKey().getToggleState()) << "QUANTIZE on by default, lit with a grid";
	EXPECT_TRUE (strip.eventsKey().getToggleState());
	strip.recKey().onClick();
	EXPECT_TRUE (host().isRecording());
	EXPECT_TRUE (strip.recKey().getToggleState());
	EXPECT_EQ (strip.note(), juce::String());

	host().player().setBeatGrid ({});
	strip.refresh();
	EXPECT_FALSE (host().isRecording()) << "a lost grid ends the recording";
}

TEST_F (EventStripTest, EventsAndQuantizeKeysSwitchTheDeck)
{
	strip.eventsKey().onClick();
	EXPECT_FALSE (host().spatialPorts().motion().eventsEnabled());
	EXPECT_FALSE (strip.eventsKey().getToggleState());
	strip.quantizeKey().onClick();
	EXPECT_FALSE (host().player().quantizeRequested());
	EXPECT_FALSE (strip.quantizeKey().getToggleState());

	DeckSession saved;
	host().gather (saved);
	EXPECT_FALSE (saved.eventsEnabled) << "kept in the session";
	EXPECT_FALSE (saved.quantize);
}

TEST_F (EventStripTest, NothingSelectedDisablesTheEventKeys)
{
	addEvent (0, 1.0);
	const auto eventKeys = [this]
	{
		return std::vector<juce::TextButton*> { &strip.nudgeKey (EventStrip::Nudge::barBack), &strip.nudgeKey (EventStrip::Nudge::beatBack),
												&strip.nudgeKey (EventStrip::Nudge::beatForward), &strip.nudgeKey (EventStrip::Nudge::barForward),
												&strip.activeKey(), &strip.deleteKey() };
	};
	for (auto* key : eventKeys())
		EXPECT_FALSE (key->isEnabled()) << key->getButtonText();
	strip.select (0);
	for (auto* key : eventKeys())
		EXPECT_TRUE (key->isEnabled()) << key->getButtonText();
}

// No undo in the booth: DELETE arms (lit), a second press within two seconds deletes.
TEST_F (EventStripTest, DeleteTakesTwoDeliberatePresses)
{
	addEvent (3, 1.0);
	addEvent (3, 3.0);
	strip.select (0);

	strip.deleteKey().onClick();
	EXPECT_EQ (events().size(), 2u) << "one press deletes nothing";
	EXPECT_TRUE (strip.deleteKey().getToggleState()) << "armed is lit";

	clock += 2.5;
	strip.refresh();
	EXPECT_FALSE (strip.deleteKey().getToggleState()) << "the arm lets go";
	strip.deleteKey().onClick();
	EXPECT_EQ (events().size(), 2u) << "a slow second press arms again";

	clock += 1.0;
	strip.deleteKey().onClick();
	ASSERT_EQ (events().size(), 1u);
	EXPECT_DOUBLE_EQ (events()[0].beat, 3.0) << "the selected one went";
	EXPECT_EQ (strip.selected(), -1);
	EXPECT_FALSE (strip.deleteKey().isEnabled());
}

// A tap on a marker selects it; another selection drops an armed DELETE.
TEST_F (EventStripTest, AMarkerTapSelectsAndDropsTheArm)
{
	addEvent (0, 1.0);
	addEvent (1, 4.0);
	ASSERT_NE (wave.onMarkerTapped, nullptr);
	wave.onMarkerTapped (0);
	EXPECT_EQ (strip.selected(), 0);
	strip.deleteKey().onClick();
	ASSERT_TRUE (strip.deleteKey().getToggleState());

	wave.onMarkerTapped (1);
	EXPECT_EQ (strip.selected(), 1);
	EXPECT_FALSE (strip.deleteKey().getToggleState());
	strip.deleteKey().onClick();
	EXPECT_EQ (events().size(), 2u) << "the first press on the new selection only arms";
}

// A press recorded on a taken slot keeps the old event and says so; the
// waveform is given the new list after a recording.
TEST_F (EventStripTest, ARecordedPressOnATakenSlotSaysSo)
{
	strip.recKey().onClick();
	ASSERT_TRUE (host().isRecording());
	const auto before = waveShot();

	strip.pressRecorded (1, motionlite::Shape::ping, false, 7);
	ASSERT_EQ (events().size(), 1u);
	EXPECT_EQ (strip.note(), juce::String());
	EXPECT_FALSE (differenceBounds (before, waveShot()).isEmpty()) << "the new marker is painted";

	strip.pressRecorded (1, motionlite::Shape::scat, false, 8);
	EXPECT_EQ (events().size(), 1u);
	EXPECT_EQ (events()[0].shape, motionlite::Shape::ping);
	EXPECT_EQ (strip.note(), juce::String ("SLOT TAKEN"));
}

// A press recorded into a full list (512 events) is refused and says so.
TEST_F (EventStripTest, ARecordedPressIntoAFullListSaysSo)
{
	ASSERT_TRUE (host().changeEvents ([] (stemevents::EventList& list)
	{
		for (int i = 0; i < stemevents::maxEvents; ++i)
		{
			stemevents::Event e;
			e.stem = i % StemSet::numStems;
			e.beat = 100.0 + (double) (i / StemSet::numStems);
			list.add (e);
		}
		return true;
	}));
	strip.recKey().onClick();
	strip.pressRecorded (2, motionlite::Shape::ping, false, 7);
	EXPECT_EQ ((int) events().size(), stemevents::maxEvents);
	EXPECT_EQ (strip.note(), juce::String ("LIST FULL"));
	clock += EventStrip::refusedNoteSeconds + 0.1;
	EXPECT_EQ (strip.note(), juce::String());
}

// One note at a time, the most pressing first:
// SLOT TAKEN > LIST FULL > NEWER FILE > FILE DAMAGED > NO GRID > NOT SAVED.
TEST_F (EventStripTest, NotesShowOneAtATimeInTheirOrder)
{
	const auto eventsFile = stemevents::fileFor (set.files[0]);
	const auto reload = [this]
	{
		ASSERT_EQ (host().load (set), juce::String());
		ASSERT_TRUE (host().hasGrid());
		strip.refresh();
	};

	// A newer file: the slot taken and the full list come before it.
	ASSERT_TRUE (eventsFile.replaceWithText ("{\"version\":2,\"events\":[]}"));
	reload();
	EXPECT_EQ (strip.note(), juce::String ("NEWER FILE"));
	strip.recKey().onClick();
	strip.pressRecorded (1, motionlite::Shape::ping, false, 7);
	ASSERT_EQ (events().size(), 1u) << "kept in memory";
	ASSERT_TRUE (host().changeEvents ([] (stemevents::EventList& list)
	{
		for (int i = 1; i < stemevents::maxEvents; ++i)
		{
			stemevents::Event e;
			e.stem = i % StemSet::numStems;
			e.beat = 100.0 + (double) (i / StemSet::numStems);
			list.add (e);
		}
		return true;
	}));
	ASSERT_EQ ((int) events().size(), stemevents::maxEvents);
	strip.pressRecorded (1, motionlite::Shape::scat, false, 8);   // the slot of the first press
	clock += 1.0;
	strip.pressRecorded (2, motionlite::Shape::scat, false, 9);   // a free slot, no room
	EXPECT_EQ (strip.note(), juce::String ("SLOT TAKEN"));
	clock += EventStrip::refusedNoteSeconds - 0.5;
	EXPECT_EQ (strip.note(), juce::String ("LIST FULL"));
	clock += 1.0;
	EXPECT_EQ (strip.note(), juce::String ("NEWER FILE"));
	host().player().setBeatGrid ({});
	strip.refresh();
	EXPECT_EQ (strip.note(), juce::String ("NEWER FILE")) << "before NO GRID";

	// A damaged file comes before a missing grid.
	ASSERT_TRUE (eventsFile.replaceWithText ("not json"));
	reload();
	host().player().setBeatGrid ({});
	strip.refresh();
	EXPECT_EQ (strip.note(), juce::String ("FILE DAMAGED"));

	// A file that cannot be written (a folder with something in it stands in
	// its place): NOT SAVED, after NO GRID.
	ASSERT_TRUE (eventsFile.deleteFile());
	ASSERT_TRUE (eventsFile.getChildFile ("keep").create().wasOk());
	reload();
	addEvent (0, 1.0);
	const auto until = juce::Time::getMillisecondCounter() + 10000;
	while (strip.note().isEmpty() && juce::Time::getMillisecondCounter() < until)
		juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
	EXPECT_EQ (strip.note(), juce::String ("NOT SAVED"));
	host().player().setBeatGrid ({});
	strip.refresh();
	EXPECT_EQ (strip.note(), juce::String ("NO GRID"));
}

// A page test must paint: each state a picture, each different from the last.
TEST_F (EventStripTest, StripPaintsInEveryState)
{
	juce::Component window;
	window.setSize (768, 1024);
	window.addAndMakeVisible (strip);
	strip.setLookAndFeel (&look);
	addEvent (0, 1.0);
	addEvent (0, 2.0);

	strip.setSize (756, 47);   // the rig's one-deck row
	std::vector<std::pair<std::string, juce::Image>> shots;
	const auto shoot = [&] (const char* state)
	{
		strip.refresh();
		shots.emplace_back (state, strip.createComponentSnapshot (strip.getLocalBounds()));
	};

	shoot ("plain");
	strip.select (0);
	shoot ("selected");
	strip.deleteKey().onClick();
	shoot ("delete armed");
	strip.recKey().onClick();
	shoot ("recording");
	strip.activeKey().onClick();
	shoot ("inactive");
	strip.nudgeKey (EventStrip::Nudge::beatForward).onClick();
	shoot ("slot taken");
	host().player().setBeatGrid ({});
	shoot ("no grid");

	for (size_t i = 0; i < shots.size(); ++i)
	{
		EXPECT_GT (lumaVariance (shots[i].second), 50.0) << shots[i].first;
		if (i > 0)
			EXPECT_FALSE (differenceBounds (shots[i - 1].second, shots[i].second).isEmpty())
				<< shots[i].first << " looks like " << shots[i - 1].first;
	}
	strip.setLookAndFeel (nullptr);
	window.removeChildComponent (&strip);
}

//==============================================================================
// The one-deck screen: the strip right under the overview, in one row, on
// both screens (768 x 1024 on a3nuc1/2, 600 x 1024 on a3motionV02), touching
// none of the other parts; and a pad press while REC is on is an event.
TEST_F (EventStripTest, OneDeckStripFitsUnderTheOverviewOnBothScreens)
{
	// What OneDeckComponent gives its view at 768 x 1024 and at 600 x 1024.
	for (const auto& [width, height] : { std::pair { 756, 718 }, std::pair { 592, 726 } })
	{
		OneDeckView view (audio, formats, 0);
		view.setSize (width, height);
		const auto size = std::to_string (width) + "x" + std::to_string (height);
		auto& events = view.eventStrip();
		const auto stripBounds = events.getBounds();

		EXPECT_TRUE (view.getLocalBounds().contains (stripBounds)) << size;
		EXPECT_GE (stripBounds.getY(), view.deckPanel().overviewWaveform().getBottom()) << "under the overview, " << size;
		for (auto* child : view.getChildren())
			if (child != &events && child->isVisible())
				EXPECT_FALSE (child->getBounds().intersects (stripBounds)) << size << ": " << child->getName()
																			 << " " << child->getBounds().toString();

		for (auto* key : { &events.recKey(), &events.eventsKey(), &events.quantizeKey(), &events.activeKey(), &events.deleteKey(),
						   &events.nudgeKey (EventStrip::Nudge::barBack), &events.nudgeKey (EventStrip::Nudge::barForward) })
		{
			EXPECT_GE (key->getHeight(), 40) << "a fingertip, " << size;
			EXPECT_GE (key->getWidth(), 50) << size;
			EXPECT_TRUE (events.getLocalBounds().contains (key->getBounds())) << size;
		}
	}
}

TEST_F (EventStripTest, OneDeckPadPressWhileRecIsOnIsAnEvent)
{
	OneDeckView view (audio, formats, 0);
	view.setSize (756, 718);
	view.eventStrip().recKey().onClick();
	ASSERT_TRUE (host().isRecording());

	view.column (2).oneShots().press (motionlite::Shape::rise);
	ASSERT_EQ (events().size(), 1u);
	EXPECT_EQ (events()[0].stem, 2);
	EXPECT_EQ (events()[0].shape, motionlite::Shape::rise);
}
