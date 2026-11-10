#include <gtest/gtest.h>

#include "Session.h"

namespace
{
	Session throughXml (const Session& session)
	{
		return Session::fromXml (*session.toXml());
	}
}

TEST (Session, KeyLockIsKeptPerDeck)
{
	Session session;
	session.decks[0].keyLock = true;
	session.decks[1].keyLock = false;

	const auto back = throughXml (session);
	EXPECT_TRUE (back.decks[0].keyLock);
	EXPECT_FALSE (back.decks[1].keyLock);

	session.decks[0].keyLock = false;
	session.decks[1].keyLock = true;
	const auto swapped = throughXml (session);
	EXPECT_FALSE (swapped.decks[0].keyLock);
	EXPECT_TRUE (swapped.decks[1].keyLock);
}

// A session written before key lock existed plays as it did: pitch follows tempo.
TEST (Session, AnOlderSessionHasKeyLockOff)
{
	auto xml = Session().toXml();
	for (auto* deck : xml->getChildWithTagNameIterator ("Deck"))
		deck->removeAttribute ("keyLock");

	for (const auto& deck : Session::fromXml (*xml).decks)
		EXPECT_FALSE (deck.keyLock);
}

TEST (Session, TheDeckControlsRoundTrip)
{
	Session session;
	auto& deck = session.decks[1];
	deck.tempo = 1.04;
	deck.tempoRange = 0.16;
	deck.vinyl = false;
	deck.sync = true;
	deck.repeat = true;

	const auto back = throughXml (session).decks[1];
	EXPECT_DOUBLE_EQ (back.tempo, 1.04);
	EXPECT_DOUBLE_EQ (back.tempoRange, 0.16);
	EXPECT_FALSE (back.vinyl);
	EXPECT_TRUE (back.sync);
	EXPECT_TRUE (back.repeat);
}

TEST (Session, CueIsKeptPerStem)
{
	Session session;
	session.decks[0].stems[1].cue = true;
	session.decks[1].stems[3].cue = true;
	session.decks[1].stems[3].muted = true;

	const auto back = throughXml (session);
	for (int d = 0; d < 2; ++d)
		for (int s = 0; s < 4; ++s)
			EXPECT_EQ (back.decks[(size_t) d].stems[(size_t) s].cue, (d == 0 && s == 1) || (d == 1 && s == 3));
	EXPECT_TRUE (back.decks[1].stems[3].muted);
}

TEST (Session, CueIsIndependentOfTheBuses)
{
	Session session;
	session.decks[0].stems[0].buses = 0b00001;
	session.decks[0].stems[0].cue = true;

	const auto back = throughXml (session).decks[0].stems[0];
	EXPECT_EQ (back.buses, 0b00001u);
	EXPECT_TRUE (back.cue);
}

// A session from the days of the CUE bus has it as bit 5 of the bus mask;
// that stays dropped -- the new cue is a field of its own, off in an older file.
TEST (Session, AnOldCueBusBitDoesNotTurnCueOn)
{
	auto xml = Session().toXml();
	for (auto* deck : xml->getChildWithTagNameIterator ("Deck"))
		for (auto* stem : deck->getChildWithTagNameIterator ("Stem"))
		{
			stem->setAttribute ("buses", 0b110001);
			stem->removeAttribute ("cue");
		}

	for (const auto& deck : Session::fromXml (*xml).decks)
		for (const auto& stem : deck.stems)
		{
			EXPECT_EQ (stem.buses, 0b010001u);
			EXPECT_FALSE (stem.cue);
		}
}

TEST (Session, SpatialStateRoundTrips)
{
	Session session;
	auto& stem = session.decks[1].stems[2];
	stem.azimuth = -120.5;
	stem.elevation = 33.0;
	stem.width = 200.0;

	const auto back = throughXml (session).decks[1].stems[2];
	EXPECT_DOUBLE_EQ (back.azimuth, -120.5);
	EXPECT_DOUBLE_EQ (back.elevation, 33.0);
	EXPECT_DOUBLE_EQ (back.width, 200.0);
}

TEST (Session, AnOlderSessionHasDefaultSpatialState)
{
	auto xml = Session().toXml();
	for (auto* deck : xml->getChildWithTagNameIterator ("Deck"))
		for (auto* stem : deck->getChildWithTagNameIterator ("Stem"))
			for (const auto* name : { "azimuth", "elevation", "width" })
				stem->removeAttribute (name);

	const auto back = Session::fromXml (*xml);
	for (const auto& deck : back.decks)
		for (const auto& stem : deck.stems)
		{
			EXPECT_DOUBLE_EQ (stem.azimuth, 0.0);
			EXPECT_DOUBLE_EQ (stem.elevation, 0.0);
			EXPECT_DOUBLE_EQ (stem.width, 60.0);
		}
}

TEST (Session, GarbageSpatialValuesAreClamped)
{
	auto xml = Session().toXml();
	auto* deck = xml->getChildByName ("Deck");
	auto* first = deck->getChildByName ("Stem");
	first->setAttribute ("azimuth", 400.0);
	first->setAttribute ("elevation", 120.0);
	first->setAttribute ("width", -5.0);
	auto* second = first->getNextElementWithTagName ("Stem");
	second->setAttribute ("elevation", -120.0);
	second->setAttribute ("width", 400.0);

	const auto back = Session::fromXml (*xml).decks[0];
	EXPECT_DOUBLE_EQ (back.stems[0].azimuth, 40.0);
	EXPECT_DOUBLE_EQ (back.stems[0].elevation, 90.0);
	EXPECT_DOUBLE_EQ (back.stems[0].width, 0.0);
	EXPECT_DOUBLE_EQ (back.stems[1].elevation, -90.0);
	EXPECT_DOUBLE_EQ (back.stems[1].width, 360.0);
}

// The per-stem 3D went on 2026-10-29 (a stem's 3D is the deck's times its
// action's envelope): a session that stored one still loads, everything else
// in it as stored, and a saved session no longer writes one.
TEST (Session, AStoredStem3DIsIgnored)
{
	Session session;
	session.decks[0].stems[1].azimuth = 30.0;
	session.decks[0].stems[1].aux = 0.0;
	session.decks[0].stems[1].gainDb = -3.0;
	auto xml = session.toXml();
	for (auto* deck : xml->getChildWithTagNameIterator ("Deck"))
		for (auto* stem : deck->getChildWithTagNameIterator ("Stem"))
		{
			EXPECT_FALSE (stem->hasAttribute ("threeD")) << "not written any more";
			stem->setAttribute ("threeD", 0.8);   // as an older StemDeck wrote it
		}

	const auto back = Session::fromXml (*xml);
	EXPECT_DOUBLE_EQ (back.decks[0].stems[1].azimuth, 30.0);
	EXPECT_DOUBLE_EQ (back.decks[0].stems[1].gainDb, -3.0);
	EXPECT_FALSE (back.toXml()->getChildByName ("Deck")->getChildByName ("Stem")->hasAttribute ("threeD"))
		<< "read, not carried";
}

TEST (OneDeckSession, AStoredStem3DIsIgnored)
{
	OneDeckSession session;
	session.deck.stems[2].width = 90.0;
	session.deck.stems[2].aux = 0.4;
	auto xml = session.toXml();
	auto* deck = xml->getChildByName ("Deck");
	ASSERT_NE (deck, nullptr);
	for (auto* stem : deck->getChildWithTagNameIterator ("Stem"))
	{
		EXPECT_FALSE (stem->hasAttribute ("threeD"));
		stem->setAttribute ("threeD", 1.0);
	}

	const auto back = OneDeckSession::fromXml (*xml);
	EXPECT_DOUBLE_EQ (back.deck.stems[2].width, 90.0);
	EXPECT_DOUBLE_EQ (back.deck.stems[2].aux, 0.4);
}

TEST (Session, LengthRoundTripsAndOldSessionsHaveFour)
{
	Session session;
	session.decks[1].stems[2].lengthBars = 0.5;
	session.decks[0].stems[0].lengthBars = 8.0;
	session.decks[0].stems[3].lengthBars = 0.25;
	const auto back = throughXml (session);
	EXPECT_DOUBLE_EQ (back.decks[1].stems[2].lengthBars, 0.5);
	EXPECT_DOUBLE_EQ (back.decks[0].stems[3].lengthBars, 0.25) << "a quarter bar";
	EXPECT_DOUBLE_EQ (back.decks[0].stems[0].lengthBars, 8.0);
	EXPECT_DOUBLE_EQ (back.decks[0].stems[1].lengthBars, 4.0);

	auto xml = Session().toXml();
	for (auto* deck : xml->getChildIterator())
		if (deck->hasTagName ("Deck"))
			for (auto* stem : deck->getChildWithTagNameIterator ("Stem"))
				stem->removeAttribute ("lengthBars");
	for (const auto& deck : Session::fromXml (*xml).decks)
		for (const auto& stem : deck.stems)
			EXPECT_DOUBLE_EQ (stem.lengthBars, 4.0);
}

TEST (Session, AStoredLengthOutsideTheValidSetReadsAsFour)
{
	auto xml = Session().toXml();
	auto* first = xml->getChildByName ("Deck")->getChildByName ("Stem");
	first->setAttribute ("lengthBars", 3.0);
	auto* second = first->getNextElementWithTagName ("Stem");
	second->setAttribute ("lengthBars", "garbage");
	auto* third = second->getNextElementWithTagName ("Stem");
	third->setAttribute ("lengthBars", 16.0);
	auto* fourth = third->getNextElementWithTagName ("Stem");
	fourth->setAttribute ("lengthBars", 2.0);

	const auto back = Session::fromXml (*xml).decks[0];
	EXPECT_DOUBLE_EQ (back.stems[0].lengthBars, 4.0);
	EXPECT_DOUBLE_EQ (back.stems[1].lengthBars, 4.0);
	EXPECT_DOUBLE_EQ (back.stems[2].lengthBars, 4.0);
	EXPECT_DOUBLE_EQ (back.stems[3].lengthBars, 2.0);
}

// ---- One deck per instance ----

namespace
{
	Session twoDeckSession()
	{
		Session s;
		s.decks[0].setId = "setA";
		s.decks[0].position = 12.5;
		s.decks[0].playing = true;
		s.decks[0].faderDb = -6.0;
		s.decks[0].stems[1].buses = 5u;
		s.decks[0].stems[1].azimuth = 45.0;
		s.decks[0].stems[1].lengthBars = 8.0;
		s.decks[0].stems[2].cue = true;
		s.decks[1].setId = "setB";
		s.decks[1].tempo = 1.04;
		s.decks[1].faderDb = -3.0;
		s.decks[1].stems[0].gainDb = -9.0;
		s.library.search = "dub";
		s.stemJobs.push_back ({ "in", "folder", "track" });
		return s;
	}

	juce::File freshDir (const char* name)
	{
		auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("sd-s42-" + juce::String (name));
		dir.deleteRecursively();
		dir.createDirectory();
		return dir;
	}
}

TEST (Session, SessionKeepsTheDirectionPerStem)
{
	auto session = twoDeckSession();
	session.decks[0].stems[1].reversed = true;
	session.decks[1].stems[3].reversed = true;

	const auto back = Session::fromXml (*session.toXml());
	for (size_t d = 0; d < 2; ++d)
		for (size_t s = 0; s < 4; ++s)
			EXPECT_EQ (back.decks[d].stems[s].reversed, session.decks[d].stems[s].reversed) << d << "/" << s;

	OneDeckSession one;
	one.deck.stems[2].reversed = true;
	const auto oneBack = OneDeckSession::fromXml (*one.toXml());
	EXPECT_TRUE (oneBack.deck.stems[2].reversed);
	EXPECT_FALSE (oneBack.deck.stems[1].reversed);
}

TEST (Session, AnOlderFileWithoutTheDirectionLoadsForward)
{
	auto xml = twoDeckSession().toXml();
	for (auto* deck : xml->getChildWithTagNameIterator ("Deck"))
		for (auto* stem : deck->getChildWithTagNameIterator ("Stem"))
			stem->removeAttribute ("reversed");
	const auto back = Session::fromXml (*xml);
	for (const auto& deck : back.decks)
		for (const auto& stem : deck.stems)
			EXPECT_FALSE (stem.reversed);
}

// The stem's tempo step (2026-11-10): kept per stem, x1 in an older file, and
// anything but the five steps read as x1.
TEST (Session, SessionKeepsTheSpeedPerStem)
{
	auto session = twoDeckSession();
	session.decks[0].stems[1].speed = 2.0;
	session.decks[1].stems[3].speed = 0.25;
	const auto back = Session::fromXml (*session.toXml());
	for (size_t d = 0; d < 2; ++d)
		for (size_t s = 0; s < 4; ++s)
			EXPECT_DOUBLE_EQ (back.decks[d].stems[s].speed, session.decks[d].stems[s].speed) << d << "/" << s;
	EXPECT_DOUBLE_EQ (back.decks[0].stems[0].speed, 1.0);

	OneDeckSession one;
	one.deck.stems[2].speed = 4.0;
	const auto oneBack = OneDeckSession::fromXml (*one.toXml());
	EXPECT_DOUBLE_EQ (oneBack.deck.stems[2].speed, 4.0);
	EXPECT_DOUBLE_EQ (oneBack.deck.stems[1].speed, 1.0);
}

TEST (Session, AnOlderFileWithoutTheSpeedLoadsX1AndGarbageReadsX1)
{
	auto session = twoDeckSession();
	session.decks[0].stems[0].speed = 2.0;
	auto xml = session.toXml();
	int n = 0;
	for (auto* deck : xml->getChildWithTagNameIterator ("Deck"))
		for (auto* stem : deck->getChildWithTagNameIterator ("Stem"))
		{
			const juce::StringArray stored { "", "3", "garbage", "-1", "0", "8", "0.5", "" };
			const auto text = stored[n++ % stored.size()];
			if (text.isEmpty())
				stem->removeAttribute ("speed");
			else
				stem->setAttribute ("speed", text);
		}
	const auto back = Session::fromXml (*xml);
	for (size_t d = 0; d < 2; ++d)
		for (size_t s = 0; s < 4; ++s)
			EXPECT_DOUBLE_EQ (back.decks[d].stems[s].speed, d == 1 && s == 2 ? 0.5 : 1.0) << d << "/" << s;
}

TEST (OneDeckSession, RoundTrips)
{
	OneDeckSession in;
	in.deck.setId = "x";
	in.deck.position = 3.25;
	in.deck.playing = true;
	in.deck.keyLock = true;
	in.deck.stems[3].gainDb = -4.5;
	in.deck.stems[3].muted = true;
	in.deck.stems[3].cue = true;
	in.deck.stems[3].azimuth = -90.0;
	in.deck.stems[3].elevation = 20.0;
	in.deck.stems[3].width = 120.0;
	in.deck.stems[3].lengthBars = 2.0;
	in.library.folder = "House";
	in.stemJobs.push_back ({ "a", "b", "c" });

	const auto out = OneDeckSession::fromXml (*in.toXml());
	EXPECT_EQ (out.deck.setId, "x");
	EXPECT_EQ (out.deck.position, 3.25);
	EXPECT_TRUE (out.deck.playing);
	EXPECT_TRUE (out.deck.keyLock);
	const auto& st = out.deck.stems[3];
	EXPECT_EQ (st.gainDb, -4.5);
	EXPECT_TRUE (st.muted);
	EXPECT_TRUE (st.cue);
	EXPECT_EQ (st.azimuth, -90.0);
	EXPECT_EQ (st.elevation, 20.0);
	EXPECT_EQ (st.width, 120.0);
	EXPECT_EQ (st.lengthBars, 2.0);
	EXPECT_EQ (out.library.folder, "House");
	ASSERT_EQ (out.stemJobs.size(), 1u);
	EXPECT_EQ (out.stemJobs[0].track, "c");
	EXPECT_FALSE (out.imported);
}

TEST (OneDeckSession, ImportTakesDeckAForAAndBForB)
{
	const auto old = twoDeckSession();
	const auto a = importFromTwoDeck (old, 'A');
	const auto b = importFromTwoDeck (old, 'B');

	EXPECT_EQ (a.deck.setId, "setA");
	EXPECT_EQ (a.deck.position, 12.5);
	EXPECT_TRUE (a.deck.playing);
	EXPECT_EQ (a.deck.stems[1].azimuth, 45.0);
	EXPECT_EQ (a.deck.stems[1].lengthBars, 8.0);
	EXPECT_TRUE (a.deck.stems[2].cue);
	EXPECT_EQ (b.deck.setId, "setB");
	EXPECT_EQ (b.deck.tempo, 1.04);
	EXPECT_EQ (b.deck.stems[0].gainDb, -9.0);
	EXPECT_EQ (a.library.search, "dub");
	EXPECT_EQ (a.stemJobs.size(), 1u);
	EXPECT_TRUE (a.imported);
	EXPECT_TRUE (b.imported);
}

// The two-deck session has decks A and B only: a letter beyond them starts
// with an empty deck, and takes the library and the stem jobs as A and B do.
TEST (OneDeckSession, ImportBeyondBTakesTheLibraryAndAnEmptyDeck)
{
	for (const char letter : { 'C', 'D', 'Z' })
	{
		const auto one = importFromTwoDeck (twoDeckSession(), letter);
		EXPECT_TRUE (one.deck.setId.isEmpty()) << letter;
		EXPECT_FALSE (one.deck.playing) << letter;
		EXPECT_EQ (one.library.search, "dub") << letter;
		EXPECT_EQ (one.stemJobs.size(), 1u) << letter;
		EXPECT_TRUE (one.imported) << letter;
	}
}

TEST (OneDeckSession, ImportDropsRouting)
{
	const auto a = importFromTwoDeck (twoDeckSession(), 'A');
	EXPECT_EQ (a.deck.faderDb, 0.0);
	for (const auto& stem : a.deck.stems)
		EXPECT_EQ (stem.buses, 0u);

	const auto xml = a.toXml();
	for (auto* stem : xml->getChildByName ("Deck")->getChildWithTagNameIterator ("Stem"))
		EXPECT_FALSE (stem->hasAttribute ("buses"));
	EXPECT_FALSE (xml->getChildByName ("Deck")->hasAttribute ("fader"));
}

// Review focus 4: an old file is imported once; afterwards the one-deck file is the truth.
TEST (OneDeckSession, ImportHappensOnceOnly)
{
	const auto dir = freshDir ("once");
	const DeckInstance instance { 'A' };
	const auto twoDeckFile = dir.getChildFile ("session.xml");
	const auto oneDeckFile = oneDeckSessionFile (dir.getChildFile ("settings.xml"), instance);
	ASSERT_TRUE (twoDeckFile.replaceWithText (twoDeckSession().toXml()->toString()));
	const auto before = twoDeckFile.loadFileAsString();

	auto first = loadOrImport (oneDeckFile, twoDeckFile, instance);
	EXPECT_TRUE (first.imported);
	EXPECT_EQ (first.deck.setId, "setA");

	first.deck.setId = "changedInOneDeckMode";
	ASSERT_TRUE (oneDeckFile.getParentDirectory().createDirectory());
	ASSERT_TRUE (oneDeckFile.replaceWithText (first.toXml()->toString()));

	const auto second = loadOrImport (oneDeckFile, twoDeckFile, instance);
	EXPECT_FALSE (second.imported);
	EXPECT_EQ (second.deck.setId, "changedInOneDeckMode");
	EXPECT_EQ (twoDeckFile.loadFileAsString(), before);   // two-deck mode still owns it

	dir.deleteRecursively();
}

TEST (OneDeckSession, NothingToImportGivesDefaults)
{
	const auto dir = freshDir ("none");
	const auto s = loadOrImport (dir.getChildFile ("A/session.xml"), dir.getChildFile ("session.xml"), DeckInstance { 'A' });
	EXPECT_FALSE (s.imported);
	EXPECT_TRUE (s.deck.setId.isEmpty());
	dir.deleteRecursively();
}

// Review focus 3: two instances on one machine never share a file.
TEST (OneDeckSession, TwoInstancesHaveTwoFiles)
{
	const juce::File settings ("/tmp/StemDeck/settings.xml");
	const auto a = oneDeckSessionFile (settings, DeckInstance { 'A' });
	const auto b = oneDeckSessionFile (settings, DeckInstance { 'B' });
	EXPECT_NE (a, b);
	EXPECT_EQ (a, juce::File ("/tmp/StemDeck/A/session.xml"));
	EXPECT_EQ (b, juce::File ("/tmp/StemDeck/B/session.xml"));
	EXPECT_NE (a, Session::file (settings));
}

// The two-deck file must read byte for byte as it always did: attribute order
// is part of that (index first), pinned here against the order main writes.
TEST (Session, TwoDeckXmlKeepsItsAttributeOrder)
{
	const auto xml = twoDeckSession().toXml();
	const juce::StringArray deckOrder { "index", "set", "position", "playing", "cue", "loopStart", "loopEnd",
		"repeat", "tempo", "tempoRange", "vinyl", "sync", "keyLock", "fader", "quantize", "events" };
	const juce::StringArray stemOrder { "gain", "muted", "buses", "cue", "azimuth", "elevation", "width", "lengthBars", "reversed", "speed" };
	const auto names = [] (const juce::XmlElement& e)
	{
		juce::StringArray out;
		for (int i = 0; i < e.getNumAttributes(); ++i)
			out.add (e.getAttributeName (i));
		return out;
	};

	int decks = 0;
	for (auto* deck : xml->getChildWithTagNameIterator ("Deck"))
	{
		++decks;
		EXPECT_EQ (names (*deck), deckOrder);
		for (auto* stem : deck->getChildWithTagNameIterator ("Stem"))
			EXPECT_EQ (names (*stem), stemOrder);
	}
	EXPECT_EQ (decks, 2);
}

// Documented fallback: an unreadable one-deck file does not stop the start.
TEST (OneDeckSession, ACorruptOneDeckFileFallsBackToTheImport)
{
	const auto dir = freshDir ("corrupt");
	const DeckInstance instance { 'B' };
	const auto twoDeckFile = dir.getChildFile ("session.xml");
	const auto oneDeckFile = oneDeckSessionFile (dir.getChildFile ("settings.xml"), instance);
	ASSERT_TRUE (twoDeckFile.replaceWithText (twoDeckSession().toXml()->toString()));
	ASSERT_TRUE (oneDeckFile.getParentDirectory().createDirectory());

	for (const char* junk : { "<<< not xml", "<WrongRoot/>" })
	{
		ASSERT_TRUE (oneDeckFile.replaceWithText (junk));
		const auto s = loadOrImport (oneDeckFile, twoDeckFile, instance);
		EXPECT_TRUE (s.imported);
		EXPECT_EQ (s.deck.setId, "setB");
	}
	dir.deleteRecursively();
}

// The stem's AUX send exists in a one-deck session only; the two-deck file
// stays as it was.
TEST (OneDeckSession, AuxRoundTrips)
{
	OneDeckSession session;
	session.deck.stems[2].aux = 0.37;

	const auto back = OneDeckSession::fromXml (*session.toXml());
	EXPECT_DOUBLE_EQ (back.deck.stems[2].aux, 0.37);
	EXPECT_DOUBLE_EQ (back.deck.stems[0].aux, 0.0);
}

TEST (OneDeckSession, OldOneDeckSessionHasAuxZero)
{
	auto xml = OneDeckSession().toXml();
	for (auto* deck : xml->getChildWithTagNameIterator ("Deck"))
		for (auto* stem : deck->getChildWithTagNameIterator ("Stem"))
			stem->removeAttribute ("aux");

	for (const auto& stem : OneDeckSession::fromXml (*xml).deck.stems)
		EXPECT_DOUBLE_EQ (stem.aux, 0.0);
}

TEST (OneDeckSession, AStoredAuxOutsideZeroToOneIsClamped)
{
	auto xml = OneDeckSession().toXml();
	auto* stem = xml->getChildByName ("Deck")->getChildByName ("Stem");
	stem->setAttribute ("aux", 7.5);
	EXPECT_DOUBLE_EQ (OneDeckSession::fromXml (*xml).deck.stems[0].aux, 1.0);
	stem->setAttribute ("aux", -3.0);
	EXPECT_DOUBLE_EQ (OneDeckSession::fromXml (*xml).deck.stems[0].aux, 0.0);
}

TEST (Session, TwoDeckXmlWritesNoAux)
{
	Session session;
	session.decks[0].stems[0].aux = 0.5;
	const auto xml = session.toXml();
	for (auto* deck : xml->getChildWithTagNameIterator ("Deck"))
		for (auto* stem : deck->getChildWithTagNameIterator ("Stem"))
			EXPECT_FALSE (stem->hasAttribute ("aux"));
}

// CONTINUE is the one-deck screen's (PlayMode.h): kept in its session like
// REPEAT; the two-deck file, read byte for byte, never carries it.
TEST (OneDeckSession, ContinueIsKeptLikeRepeat)
{
	OneDeckSession session;
	session.deck.continuePlay = true;
	EXPECT_TRUE (OneDeckSession::fromXml (*session.toXml()).deck.continuePlay);

	session.deck.continuePlay = false;
	EXPECT_FALSE (OneDeckSession::fromXml (*session.toXml()).deck.continuePlay);

	Session twoDeck;
	twoDeck.decks[0].continuePlay = true;
	const auto twoDeckXml = twoDeck.toXml();   // alive for the loop: the range would outlive a temporary
	for (auto* deck : twoDeckXml->getChildWithTagNameIterator ("Deck"))
		EXPECT_FALSE (deck->hasAttribute ("continue"));
}

TEST (Session, SessionKeepsQuantize)
{
	Session session;
	EXPECT_TRUE (session.decks[0].quantize) << "on by default, as a CDJ's";
	session.decks[1].quantize = false;

	const auto back = throughXml (session);
	EXPECT_TRUE (back.decks[0].quantize);
	EXPECT_FALSE (back.decks[1].quantize);
}

TEST (Session, AnOlderSessionHasQuantizeOn)
{
	auto xml = Session().toXml();
	for (auto* deck : xml->getChildWithTagNameIterator ("Deck"))
		deck->removeAttribute ("quantize");

	for (const auto& deck : Session::fromXml (*xml).decks)
		EXPECT_TRUE (deck.quantize);
}

TEST (Session, SessionKeepsEventsEnabled)
{
	Session session;
	EXPECT_TRUE (session.decks[0].eventsEnabled) << "a prepared track plays its events";
	session.decks[1].eventsEnabled = false;

	const auto back = throughXml (session);
	EXPECT_TRUE (back.decks[0].eventsEnabled);
	EXPECT_FALSE (back.decks[1].eventsEnabled);

	OneDeckSession oneDeck;
	oneDeck.deck.eventsEnabled = false;
	EXPECT_FALSE (OneDeckSession::fromXml (*oneDeck.toXml()).deck.eventsEnabled);
}

TEST (Session, AnOlderSessionHasEventsOn)
{
	auto xml = Session().toXml();
	for (auto* deck : xml->getChildWithTagNameIterator ("Deck"))
		deck->removeAttribute ("events");

	for (const auto& deck : Session::fromXml (*xml).decks)
		EXPECT_TRUE (deck.eventsEnabled);
}
