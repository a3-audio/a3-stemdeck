// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>

#include "TrackEvents.h"

#include <string>
#include <vector>

// A deck's events per track: the file beside the set's first stem is loaded
// into the deck's motion, recorded presses are added, and the list is saved
// back a second after the last change.
using namespace stemevents;

namespace
{
using motionlite::Shape;

juce::File freshDir (const char* name)
{
	auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile (juce::String ("sd-track-events-") + name);
	dir.deleteRecursively();
	dir.createDirectory();
	return dir;
}

Event event (int stem, double beat, Shape shape = Shape::orbit)
{
	Event e;
	e.stem = stem;
	e.beat = beat;
	e.shape = shape;
	return e;
}

EventList listOf (std::initializer_list<Event> events)
{
	EventList list;
	for (const auto& e : events)
		list.add (e);
	return list;
}

// A first stem file with its events file beside it.
juce::File setWith (const juce::File& dir, const char* name, std::initializer_list<Event> events)
{
	const auto firstStem = dir.getChildFile (juce::String (name) + "_drums.flac");
	firstStem.replaceWithText ("audio");
	if (events.size() > 0)
		save (fileFor (firstStem), listOf (events));
	return firstStem;
}

// What the deck's motion was told, in order.
struct FakeMotion
{
	std::vector<std::string> calls;
	std::vector<Event> published;

	TrackEvents::Motion sink()
	{
		return { [this] { calls.push_back ("newTrack"); published.clear(); },
				 [this] (const std::vector<Event>& events) { calls.push_back ("setEvents"); published = events; } };
	}
};

class TrackEventsTest : public ::testing::Test
{
protected:
	FakeMotion motion;
	TrackEvents track { motion.sink() };
};
}

TEST_F (TrackEventsTest, LoadingATrackLoadsItsEvents)
{
	const auto first = setWith (freshDir ("load"), "Song", { event (0, 8.0), event (2, 16.0, Shape::rise) });
	track.load (first);

	ASSERT_EQ (track.list().events().size(), 2u);
	EXPECT_EQ (track.status(), Status::ok);
	ASSERT_EQ (motion.published.size(), 2u) << "and the deck's motion has them";
	EXPECT_DOUBLE_EQ (motion.published[1].beat, 16.0);
}

// DeckMotion::newTrack empties the bank, so it must come first, or it would
// throw away the list just published.
TEST_F (TrackEventsTest, TheMotionHearsNewTrackBeforeTheEvents)
{
	const auto first = setWith (freshDir ("order"), "Song", { event (0, 8.0) });
	track.load (first);
	EXPECT_EQ (motion.calls, (std::vector<std::string> { "newTrack", "setEvents" }));
}

TEST_F (TrackEventsTest, LoadingAnotherTrackReplacesThem)
{
	const auto dir = freshDir ("replace");
	track.load (setWith (dir, "One", { event (0, 8.0), event (1, 12.0) }));
	track.load (setWith (dir, "Two", { event (3, 4.0, Shape::scat) }));

	ASSERT_EQ (track.list().events().size(), 1u);
	EXPECT_EQ (track.list().events()[0].stem, 3);
	ASSERT_EQ (motion.published.size(), 1u);
	EXPECT_EQ (motion.published[0].shape, Shape::scat);

	track.load (setWith (dir, "Three", {}));
	EXPECT_TRUE (track.list().events().empty()) << "a set without a file has none";
	EXPECT_TRUE (motion.published.empty());
	EXPECT_EQ (track.status(), Status::none);
}

TEST_F (TrackEventsTest, ADamagedFileLoadsNothingAndIsNotOverwritten)
{
	const auto first = setWith (freshDir ("damaged"), "Song", {});
	const auto file = fileFor (first);
	file.replaceWithText ("{ not json");
	track.load (first);

	EXPECT_TRUE (track.list().events().empty()) << "the track plays plain";
	EXPECT_EQ (track.status(), Status::damaged);
	ASSERT_TRUE (track.note (true).has_value());
	EXPECT_TRUE (track.note (true)->contains ("damaged"));

	track.flush();
	track.saveIfDue (1.0e9);
	EXPECT_EQ (file.loadFileAsString(), "{ not json") << "never written without a change";

	// Nor after one: it may be a hand-edited file with one typo.
	track.setRecording (true);
	EXPECT_EQ (track.record (event (0, 4.0), 0.0), TrackEvents::Record::added) << "it plays in memory";
	EXPECT_TRUE (track.change ([] (EventList& list) { return list.setActive (0, false); }, 0.5));
	track.flush();
	EXPECT_EQ (file.loadFileAsString(), "{ not json");
	ASSERT_TRUE (track.note (true).has_value());
	EXPECT_TRUE (track.note (true)->contains ("damaged")) << "and still says why";
	track.load (setWith (file.getParentDirectory(), "Other", {}));   // a track change flushes too
	EXPECT_EQ (file.loadFileAsString(), "{ not json");
}

// A set without a grid must not hide a file it cannot read.
TEST_F (TrackEventsTest, TheFileStateComesBeforeNoGridInTheNote)
{
	const auto first = setWith (freshDir ("note-order"), "Song", {});
	fileFor (first).replaceWithText ("{ not json");
	track.load (first);
	ASSERT_TRUE (track.note (false).has_value());
	EXPECT_TRUE (track.note (false)->contains ("damaged"));
}

TEST_F (TrackEventsTest, ANewerFileIsNeverOverwrittenEvenAfterAChange)
{
	const auto first = setWith (freshDir ("newer"), "Song", {});
	const auto file = fileFor (first);
	const juce::String newer ("{\"version\": 99, \"events\": []}");
	file.replaceWithText (newer);
	track.load (first);
	EXPECT_EQ (track.status(), Status::newer);
	ASSERT_TRUE (track.note (true).has_value());
	EXPECT_TRUE (track.note (true)->contains ("newer"));

	track.setRecording (true);
	EXPECT_EQ (track.record (event (0, 4.0), 0.0), TrackEvents::Record::added) << "it plays in memory";
	track.flush();
	EXPECT_EQ (file.loadFileAsString(), newer);
	EXPECT_TRUE (track.note (true).has_value()) << "and says it is not kept";
}

TEST_F (TrackEventsTest, RecordingOverAnOccupiedSlotIsRefusedAndKeepsTheOldEvent)
{
	auto old = event (1, 8.0, Shape::ping);
	old.seed = 11;
	track.load (setWith (freshDir ("occupied"), "Song", { old }));
	track.setRecording (true);
	motion.calls.clear();

	auto again = event (1, 8.0, Shape::orbit);
	again.seed = 22;
	EXPECT_EQ (track.record (again, 0.0), TrackEvents::Record::occupied) << "the caller hears the refusal";
	ASSERT_EQ (track.list().events().size(), 1u);
	EXPECT_EQ (track.list().events()[0].shape, Shape::ping);
	EXPECT_EQ (track.list().events()[0].seed, 11u);
	EXPECT_TRUE (motion.calls.empty()) << "nothing new for the motion";
	EXPECT_FALSE (track.hasUnsaved());

	EXPECT_EQ (track.record (event (2, 8.0), 0.0), TrackEvents::Record::added) << "another stem's slot is free";
}

TEST_F (TrackEventsTest, ARecordedEventIsSavedBesideTheSet)
{
	const auto first = setWith (freshDir ("saved"), "Song", {});
	track.load (first);
	track.setRecording (true);

	ASSERT_EQ (track.record (event (0, 12.0, Shape::rise), 100.0), TrackEvents::Record::added);
	ASSERT_EQ (motion.published.size(), 1u) << "it plays at once";
	EXPECT_FALSE (fileFor (first).existsAsFile()) << "not yet: a second after the last change";

	EXPECT_FALSE (track.saveIfDue (100.5));
	EXPECT_TRUE (track.saveIfDue (101.0));
	const auto loaded = load (fileFor (first));
	EXPECT_EQ (loaded.status, Status::ok);
	ASSERT_EQ (loaded.list.events().size(), 1u);
	EXPECT_DOUBLE_EQ (loaded.list.events()[0].beat, 12.0);
	EXPECT_FALSE (track.hasUnsaved());
}

TEST_F (TrackEventsTest, SavingWaitsASecondAfterTheLastChange)
{
	const auto first = setWith (freshDir ("debounce"), "Song", {});
	track.load (first);
	track.setRecording (true);
	track.record (event (0, 4.0), 10.0);
	track.record (event (0, 8.0), 10.8);
	EXPECT_FALSE (track.saveIfDue (11.5)) << "the second press started the wait again";
	EXPECT_TRUE (track.saveIfDue (11.8));
	EXPECT_EQ (load (fileFor (first)).list.events().size(), 2u);
}

TEST_F (TrackEventsTest, ATrackChangeFlushesTheOldTracksEvents)
{
	const auto dir = freshDir ("flush-change");
	const auto one = setWith (dir, "One", {});
	track.load (one);
	track.setRecording (true);
	track.record (event (0, 4.0), 0.0);
	track.load (setWith (dir, "Two", {}));
	EXPECT_EQ (load (fileFor (one)).list.events().size(), 1u);
}

TEST_F (TrackEventsTest, QuittingFlushes)
{
	const auto first = setWith (freshDir ("flush-quit"), "Song", {});
	{
		FakeMotion other;
		TrackEvents quitting { other.sink() };
		quitting.load (first);
		quitting.setRecording (true);
		quitting.record (event (3, 4.0), 0.0);
	}
	EXPECT_EQ (load (fileFor (first)).list.events().size(), 1u);
}

TEST_F (TrackEventsTest, AnEmptiedListDeletesTheFile)
{
	const auto first = setWith (freshDir ("emptied"), "Song", { event (0, 8.0) });
	track.load (first);
	EXPECT_TRUE (track.change ([] (EventList& list) { return list.remove (0); }, 0.0));
	EXPECT_TRUE (motion.published.empty()) << "gone from the deck at once";
	track.flush();
	EXPECT_FALSE (fileFor (first).existsAsFile());
}

TEST_F (TrackEventsTest, WithoutRecordingAPressRecordsNothing)
{
	track.load (setWith (freshDir ("rec-off"), "Song", {}));
	EXPECT_EQ (track.record (event (0, 4.0), 0.0), TrackEvents::Record::notRecording);
	EXPECT_TRUE (track.list().events().empty());
}

TEST_F (TrackEventsTest, WithoutAGridRecordingDoesNothingAndSaysNoGrid)
{
	track.load (setWith (freshDir ("no-grid"), "Song", {}));
	track.setRecording (true);
	EXPECT_EQ (track.record (std::nullopt, 0.0), TrackEvents::Record::noGrid);
	EXPECT_TRUE (track.list().events().empty());
	EXPECT_FALSE (track.hasUnsaved());
	ASSERT_TRUE (track.note (false).has_value());
	EXPECT_EQ (*track.note (false), "no grid");
	EXPECT_FALSE (track.note (true).has_value()) << "with a grid and a good file, nothing to say";
}

TEST_F (TrackEventsTest, AHandOverWithoutASetEmptiesTheMotion)
{
	const auto first = setWith (freshDir ("unload"), "Song", { event (0, 8.0) });
	track.load (first);
	track.unload();
	EXPECT_TRUE (track.list().events().empty());
	ASSERT_FALSE (motion.calls.empty());
	EXPECT_EQ (motion.calls.back(), "newTrack");
	EXPECT_TRUE (fileFor (first).existsAsFile()) << "the file stays";
}
