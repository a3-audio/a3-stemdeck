// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>

#include "StemEventsFile.h"

#include <utility>
#include <vector>

using namespace stemevents;

namespace
{
	juce::File freshDir (const char* name)
	{
		auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile (juce::String ("sd-events-") + name);
		dir.deleteRecursively();
		dir.createDirectory();
		return dir;
	}

	Event event (int stem, double beat, motionlite::Shape shape = motionlite::Shape::orbit)
	{
		Event e;
		e.stem = stem;
		e.beat = beat;
		e.shape = shape;
		return e;
	}

	bool same (const Event& a, const Event& b)
	{
		return a.stem == b.stem && a.beat == b.beat && a.shape == b.shape && a.lengthBars == b.lengthBars
			&& a.reversed == b.reversed && a.seed == b.seed && a.active == b.active && a.origin == b.origin;
	}
}

TEST (StemEventsFile, FileForSitsBesideTheFirstStemFileWithoutItsExtension)
{
	const juce::File dir ("/music/Artist/Album");
	EXPECT_EQ (fileFor (dir.getChildFile ("Song_bass.flac")), dir.getChildFile ("Song_bass.a3events.json"));
}

TEST (StemEventsFile, FileForDropsTheWholeNiStemsExtension)
{
	const juce::File dir ("/music/Artist");
	EXPECT_EQ (fileFor (dir.getChildFile ("Song.stem.mp4")), dir.getChildFile ("Song.a3events.json"));
}

TEST (StemEventsFile, RoundTripKeepsEveryField)
{
	const auto file = freshDir ("round").getChildFile ("a.a3events.json");
	Event first = event (2, -4.5, motionlite::Shape::scat);
	first.lengthBars = 8.0;
	first.reversed = true;
	first.seed = 18446744073709551615ULL;   // beyond 2^53: a JSON number would lose it
	first.active = false;
	first.origin = Origin::prepared;
	Event second = event (0, 16.0);
	second.seed = 9007199254740993ULL;

	EventList list;
	list.add (first);
	list.add (second);
	ASSERT_TRUE (save (file, list));

	const auto loaded = load (file);
	ASSERT_EQ (loaded.status, Status::ok);
	ASSERT_EQ (loaded.list.events().size(), 2u);
	EXPECT_TRUE (same (loaded.list.events()[0], first));
	EXPECT_TRUE (same (loaded.list.events()[1], second));
}

TEST (StemEventsFile, WritesTheDocumentedFormat)
{
	const auto file = freshDir ("format").getChildFile ("a.a3events.json");
	Event e = event (0, 16.0);
	e.seed = 123;
	EventList list;
	list.add (e);
	ASSERT_TRUE (save (file, list));

	const auto json = juce::JSON::parse (file.loadFileAsString());
	EXPECT_EQ ((int) json["version"], 1);
	const auto written = json["events"][0];
	EXPECT_EQ (written["seed"].toString(), "123");
	EXPECT_TRUE (written["seed"].isString());
	EXPECT_EQ (written["shape"].toString(), "orbit");
	EXPECT_EQ (written["origin"].toString(), "hand");
	EXPECT_FALSE (written.hasProperty ("time"));
}

TEST (StemEventsFile, MissingFileIsNone)
{
	const auto loaded = load (freshDir ("missing").getChildFile ("nope.a3events.json"));
	EXPECT_EQ (loaded.status, Status::none);
	EXPECT_TRUE (loaded.list.events().empty());
}

TEST (StemEventsFile, EmptyOrGarbageIsDamagedAndLeavesNoEvents)
{
	const auto dir = freshDir ("garbage");
	for (const char* text : { "", "not json", "[1,2,3]", "{\"version\":1}", "{\"version\":1,\"events\":5}", "{\"events\":[]}", "{\"version\":\"x\",\"events\":[]}" })
	{
		const auto file = dir.getChildFile ("g.a3events.json");
		file.replaceWithText (text);
		const auto loaded = load (file);
		EXPECT_EQ (loaded.status, Status::damaged) << text;
		EXPECT_TRUE (loaded.list.events().empty()) << text;
	}
}

TEST (StemEventsFile, NewerVersionIsNewerAndIsNeverOverwrittenBySave)
{
	const auto file = freshDir ("newer").getChildFile ("n.a3events.json");
	const juce::String future = "{\"version\":2,\"events\":[{\"whatever\":true}]}";
	file.replaceWithText (future);

	const auto loaded = load (file);
	EXPECT_EQ (loaded.status, Status::newer);
	EXPECT_TRUE (loaded.list.events().empty());

	EventList list;
	list.add (event (0, 1.0));
	EXPECT_FALSE (save (file, list));
	EXPECT_EQ (file.loadFileAsString(), future);
	EXPECT_FALSE (save (file, EventList()));   // not even the delete
	EXPECT_TRUE (file.existsAsFile());
}

TEST (StemEventsFile, UnknownShapeSkipsThatEventOnly)
{
	const auto file = freshDir ("shape").getChildFile ("s.a3events.json");
	file.replaceWithText ("{\"version\":1,\"events\":["
		"{\"stem\":0,\"beat\":1.0,\"shape\":\"orbit\",\"bars\":4,\"reversed\":false,\"seed\":\"1\",\"active\":true,\"origin\":\"hand\"},"
		"{\"stem\":1,\"beat\":2.0,\"shape\":\"wobble\",\"bars\":4,\"reversed\":false,\"seed\":\"2\",\"active\":true,\"origin\":\"hand\"},"
		"{\"stem\":2,\"beat\":3.0,\"shape\":\"rise\",\"bars\":2,\"reversed\":true,\"seed\":\"3\",\"active\":true,\"origin\":\"prepared\"}]}");

	const auto loaded = load (file);
	ASSERT_EQ (loaded.status, Status::ok);
	ASSERT_EQ (loaded.list.events().size(), 2u);
	EXPECT_EQ (loaded.list.events()[0].stem, 0);
	EXPECT_EQ (loaded.list.events()[1].shape, motionlite::Shape::rise);
}

TEST (StemEventsFile, EmptyListDeletesTheFile)
{
	const auto file = freshDir ("empty").getChildFile ("e.a3events.json");
	EventList list;
	list.add (event (0, 1.0));
	ASSERT_TRUE (save (file, list));
	ASSERT_TRUE (file.existsAsFile());

	EXPECT_TRUE (save (file, EventList()));
	EXPECT_FALSE (file.existsAsFile());
	EXPECT_TRUE (save (file, EventList()));   // nothing to delete is fine
}

TEST (StemEventsFile, SaveIsAtomic)
{
	const auto dir = freshDir ("atomic");
	EventList list;
	list.add (event (0, 1.0));

	// A target that cannot be written: no file, no leftover temporary beside it.
	const auto bad = dir.getChildFile ("no-such-folder").getChildFile ("x.a3events.json");
	EXPECT_FALSE (save (bad, list));
	EXPECT_EQ (dir.getNumberOfChildFiles (juce::File::findFilesAndDirectories), 0);

	// A rewrite replaces the whole file and leaves no temporary behind.
	const auto file = dir.getChildFile ("x.a3events.json");
	ASSERT_TRUE (save (file, list));
	list.add (event (1, 2.0));
	ASSERT_TRUE (save (file, list));
	EXPECT_EQ (load (file).list.events().size(), 2u);
	EXPECT_EQ (dir.getNumberOfChildFiles (juce::File::findFiles), 1);
}

TEST (StemEventsFile, NonFiniteBeatSkipsThatEventOnly)
{
	const auto file = freshDir ("finite").getChildFile ("f.a3events.json");
	file.replaceWithText ("{\"version\":1,\"events\":["
		"{\"stem\":0,\"beat\":1e999,\"shape\":\"orbit\",\"bars\":4,\"reversed\":false,\"seed\":\"1\",\"active\":true,\"origin\":\"hand\"},"
		"{\"stem\":1,\"beat\":-1e999,\"shape\":\"orbit\",\"bars\":4,\"reversed\":false,\"seed\":\"2\",\"active\":true,\"origin\":\"hand\"},"
		"{\"stem\":2,\"beat\":3.0,\"shape\":\"orbit\",\"bars\":4,\"reversed\":false,\"seed\":\"3\",\"active\":true,\"origin\":\"hand\"}]}");

	const auto loaded = load (file);
	ASSERT_EQ (loaded.status, Status::ok);
	ASSERT_EQ (loaded.list.events().size(), 1u);
	EXPECT_EQ (loaded.list.events()[0].stem, 2);
}

namespace
{
	// One event as the file writes it, with `field` replaced by `value` (raw
	// JSON), or left out when `value` is empty.
	juce::String eventJson (int stem, double beat, const juce::String& field = {}, const juce::String& value = {})
	{
		const std::vector<std::pair<juce::String, juce::String>> fields {
			{ "stem", juce::String (stem) }, { "beat", juce::String (beat, 3) }, { "shape", "\"orbit\"" }, { "bars", "2" },
			{ "reversed", "false" }, { "seed", "\"7\"" }, { "active", "true" }, { "origin", "\"hand\"" } };
		juce::StringArray parts;
		for (const auto& [name, text] : fields)
		{
			if (name != field)
				parts.add ("\"" + name + "\":" + text);
			else if (value.isNotEmpty())
				parts.add ("\"" + name + "\":" + value);
		}
		return "{" + parts.joinIntoString (",") + "}";
	}

	Loaded loadText (const char* dirName, const juce::String& text)
	{
		const auto file = freshDir (dirName).getChildFile ("t.a3events.json");
		file.replaceWithText (text);
		return load (file);
	}

	Loaded loadEvents (const char* dirName, const juce::StringArray& events)
	{
		return loadText (dirName, "{\"version\":1,\"events\":[" + events.joinIntoString (",") + "]}");
	}
}

// The app plays any length it does not know as 4 bars; one that is not even a
// positive number is read as 4 (the format page: "any other bars value plays as 4").
TEST (StemEventsFile, ABarsValueThatIsNoPositiveNumberReadsAsFour)
{
	const auto loaded = loadEvents ("bars", { eventJson (0, 1.0, "bars", "\"abc\""), eventJson (1, 1.0, "bars", "1e999"),
											  eventJson (2, 1.0, "bars", "0"), eventJson (3, 1.0, "bars", "-2"),
											  eventJson (0, 2.0, "bars", "true"), eventJson (1, 2.0, "bars", "8") });
	ASSERT_EQ (loaded.status, Status::ok);
	ASSERT_EQ (loaded.list.events().size(), 6u);
	for (const auto& e : loaded.list.events())
	{
		if (e.stem == 1 && e.beat == 2.0)
			EXPECT_DOUBLE_EQ (e.lengthBars, 8.0) << "a good one is kept";
		else
			EXPECT_DOUBLE_EQ (e.lengthBars, 4.0) << "stem " << e.stem << " beat " << e.beat;
	}
}

// "The app ignores events with a stem outside 0..3": that event only.
TEST (StemEventsFile, AStemOutside0To3SkipsThatEventOnly)
{
	const auto loaded = loadEvents ("stem", { eventJson (0, 1.0, "stem", "-1"), eventJson (0, 2.0, "stem", "4"),
											  eventJson (0, 3.0, "stem", "\"x\""), eventJson (0, 4.0, "stem", "1.5"),
											  eventJson (0, 5.0, "stem", "4294967296"), eventJson (3, 6.0) });
	ASSERT_EQ (loaded.status, Status::ok);
	ASSERT_EQ (loaded.list.events().size(), 1u);
	EXPECT_EQ (loaded.list.events()[0].stem, 3);
	EXPECT_DOUBLE_EQ (loaded.list.events()[0].beat, 6.0);
}

// The version is compared whole: 2^32 + 1 is newer, not version 1 cut short.
TEST (StemEventsFile, AVersionIsComparedBeforeAnyNarrowing)
{
	EXPECT_EQ (loadText ("big-version", "{\"version\":4294967297,\"events\":[]}").status, Status::newer);
	EXPECT_EQ (loadText ("negative-version", "{\"version\":-4294967295,\"events\":[]}").status, Status::damaged);
}

// The list's own rules hold on load: a doubled slot keeps the first, a list
// stops at 512 events; the file is still ok.
TEST (StemEventsFile, TheListsRejectionsHoldOnLoad)
{
	auto doubled = eventJson (2, 1.0);
	doubled = doubled.replace ("\"orbit\"", "\"ping\"");
	const auto twice = loadEvents ("doubled", { eventJson (2, 1.0), doubled });
	ASSERT_EQ (twice.status, Status::ok);
	ASSERT_EQ (twice.list.events().size(), 1u);
	EXPECT_EQ (twice.list.events()[0].shape, motionlite::Shape::orbit) << "the first one stays";

	juce::StringArray many;
	for (int i = 0; i < maxEvents + 1; ++i)
		many.add (eventJson (i % 4, (double) (i / 4)));
	const auto full = loadEvents ("many", many);
	ASSERT_EQ (full.status, Status::ok);
	EXPECT_EQ ((int) full.list.events().size(), maxEvents);
}

TEST (StemEventsFile, AMissingFieldSkipsThatEventOnly)
{
	for (const char* field : { "stem", "beat", "shape", "bars", "reversed", "seed", "active", "origin" })
	{
		const auto loaded = loadEvents ("missing-field", { eventJson (0, 1.0, field), eventJson (1, 2.0) });
		ASSERT_EQ (loaded.status, Status::ok) << field;
		ASSERT_EQ (loaded.list.events().size(), 1u) << field;
		EXPECT_EQ (loaded.list.events()[0].stem, 1) << field;
	}
}

TEST (StemEventsFile, ABadSeedSkipsThatEventOnly)
{
	const auto loaded = loadEvents ("seed", { eventJson (0, 1.0, "seed", "\"abc\""),
											  eventJson (1, 1.0, "seed", "\"18446744073709551616\""), eventJson (2, 1.0) });
	ASSERT_EQ (loaded.status, Status::ok);
	ASSERT_EQ (loaded.list.events().size(), 1u);
	EXPECT_EQ (loaded.list.events()[0].stem, 2);
}

TEST (StemEventsFile, AnUnknownOriginSkipsThatEventOnly)
{
	const auto loaded = loadEvents ("origin", { eventJson (0, 1.0, "origin", "\"robot\""), eventJson (1, 1.0) });
	ASSERT_EQ (loaded.status, Status::ok);
	ASSERT_EQ (loaded.list.events().size(), 1u);
	EXPECT_EQ (loaded.list.events()[0].stem, 1);
}
