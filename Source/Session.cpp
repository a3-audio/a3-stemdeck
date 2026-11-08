#include "Session.h"
#include "Buses.h"
#include "MotionLite.h"

#include <cmath>

namespace
{
	double wrapDegrees (double degrees)
	{
		if (! std::isfinite (degrees))
			return 0.0;
		const auto wrapped = std::fmod (degrees + 180.0, 360.0);
		return (wrapped < 0.0 ? wrapped + 360.0 : wrapped) - 180.0;
	}

	// Routing (buses, channel fader) exists only in the two-deck file.
	// `index` (two-deck file only; -1 writes none) goes first, as it always has.
	void writeDeck (juce::XmlElement& parent, const DeckSession& deck, bool withRouting, int index = -1)
	{
		auto* e = parent.createNewChildElement ("Deck");
		if (index >= 0)
			e->setAttribute ("index", index);
		e->setAttribute ("set", deck.setId);
		e->setAttribute ("position", deck.position);
		e->setAttribute ("playing", deck.playing);
		e->setAttribute ("cue", deck.cuePoint);
		e->setAttribute ("loopStart", deck.loop.getStart());
		e->setAttribute ("loopEnd", deck.loop.getEnd());
		e->setAttribute ("repeat", deck.repeat);
		if (! withRouting)
			e->setAttribute ("continue", deck.continuePlay);
		e->setAttribute ("tempo", deck.tempo);
		e->setAttribute ("tempoRange", deck.tempoRange);
		e->setAttribute ("vinyl", deck.vinyl);
		e->setAttribute ("sync", deck.sync);
		e->setAttribute ("keyLock", deck.keyLock);
		if (withRouting)
			e->setAttribute ("fader", deck.faderDb);
		// Last: the files written before it keep their attribute order.
		e->setAttribute ("quantize", deck.quantize);
		e->setAttribute ("events", deck.eventsEnabled);

		for (const auto& mix : deck.stems)
		{
			auto* stem = e->createNewChildElement ("Stem");
			stem->setAttribute ("gain", mix.gainDb);
			stem->setAttribute ("muted", mix.muted);
			if (withRouting)
				stem->setAttribute ("buses", (int) mix.buses);
			stem->setAttribute ("cue", mix.cue);
			stem->setAttribute ("azimuth", mix.azimuth);
			stem->setAttribute ("elevation", mix.elevation);
			stem->setAttribute ("width", mix.width);
			stem->setAttribute ("lengthBars", mix.lengthBars);
			if (! withRouting)
				stem->setAttribute ("aux", mix.aux);
			// Last: the files written before it keep their attribute order.
			stem->setAttribute ("reversed", mix.reversed);
		}
	}

	void writeLibraryAndJobs (juce::XmlElement& xml, const LibrarySession& library, const std::vector<QueuedStemJob>& stemJobs)
	{
		auto* lib = xml.createNewChildElement ("Library");
		lib->setAttribute ("sortColumn", library.sortColumn);
		lib->setAttribute ("sortForwards", library.sortForwards);
		lib->setAttribute ("search", library.search);
		lib->setAttribute ("selected", library.selectedSetId);
		lib->setAttribute ("folder", library.folder);
		lib->setAttribute ("showFolders", library.showFolders);

		auto* jobs = xml.createNewChildElement ("StemJobs");
		for (const auto& job : stemJobs)
		{
			auto* e = jobs->createNewChildElement ("Job");
			e->setAttribute ("input", job.input);
			e->setAttribute ("folder", job.folder);
			e->setAttribute ("track", job.track);
		}
	}

	void readDeck (const juce::XmlElement& e, DeckSession& deck, bool withRouting)
	{
		deck.setId = e.getStringAttribute ("set");
		deck.position = e.getDoubleAttribute ("position");
		deck.playing = e.getBoolAttribute ("playing");
		deck.cuePoint = e.getDoubleAttribute ("cue");
		deck.quantize = e.getBoolAttribute ("quantize", true);
		deck.eventsEnabled = e.getBoolAttribute ("events", true);
		deck.loop = { e.getDoubleAttribute ("loopStart"), e.getDoubleAttribute ("loopEnd") };
		deck.repeat = e.getBoolAttribute ("repeat");
		deck.continuePlay = ! withRouting && e.getBoolAttribute ("continue");
		deck.tempo = e.getDoubleAttribute ("tempo", 1.0);
		deck.tempoRange = e.getDoubleAttribute ("tempoRange", 0.08);
		deck.vinyl = e.getBoolAttribute ("vinyl", true);
		deck.sync = e.getBoolAttribute ("sync");
		deck.keyLock = e.getBoolAttribute ("keyLock");
		if (withRouting)
			deck.faderDb = e.getDoubleAttribute ("fader");

		size_t s = 0;
		for (auto* stem : e.getChildWithTagNameIterator ("Stem"))
		{
			if (s >= deck.stems.size())
				break;
			deck.stems[s].gainDb = stem->getDoubleAttribute ("gain");
			deck.stems[s].muted = stem->getBoolAttribute ("muted");
			// An older session's "phones" and CUE bus bit are not read back: the
			// cue send is its own attribute, absent (off) in a file without it.
			deck.stems[s].cue = stem->getBoolAttribute ("cue");
			// Absent in an older file: the defaults of StemMixState. Stored garbage is
			// pulled into IEM's ranges here, so the engine never sees it. An older
			// file's "threeD" (the stem's own 3D, gone 2026-10-29) is not read.
			const StemMixState defaults;
			deck.stems[s].azimuth = wrapDegrees (stem->getDoubleAttribute ("azimuth", defaults.azimuth));
			deck.stems[s].elevation = juce::jlimit (-90.0, 90.0, stem->getDoubleAttribute ("elevation", defaults.elevation));
			deck.stems[s].width = juce::jlimit (0.0, 360.0, stem->getDoubleAttribute ("width", defaults.width));
			deck.stems[s].lengthBars = motionlite::validLength (stem->getDoubleAttribute ("lengthBars", defaults.lengthBars));
			deck.stems[s].aux = juce::jlimit (0.0, 1.0, stem->getDoubleAttribute ("aux", defaults.aux));
			deck.stems[s].reversed = stem->getBoolAttribute ("reversed", defaults.reversed);
			if (withRouting)
				deck.stems[s].buses = buses::fromStored ((unsigned) stem->getIntAttribute ("buses", (int) buses::defaultMask ((int) s)));
			++s;
		}
	}

	void readLibraryAndJobs (const juce::XmlElement& xml, LibrarySession& library, std::vector<QueuedStemJob>& stemJobs)
	{
		if (auto* lib = xml.getChildByName ("Library"))
		{
			library.sortColumn = lib->getIntAttribute ("sortColumn");
			library.sortForwards = lib->getBoolAttribute ("sortForwards", true);
			library.search = lib->getStringAttribute ("search");
			library.selectedSetId = lib->getStringAttribute ("selected");
			library.folder = lib->getStringAttribute ("folder");
			library.showFolders = lib->getBoolAttribute ("showFolders", true);
		}

		if (auto* jobs = xml.getChildByName ("StemJobs"))
			for (auto* e : jobs->getChildWithTagNameIterator ("Job"))
				stemJobs.push_back ({ e->getStringAttribute ("input"), e->getStringAttribute ("folder"), e->getStringAttribute ("track") });
	}
}

std::unique_ptr<juce::XmlElement> Session::toXml() const
{
	auto xml = std::make_unique<juce::XmlElement> ("StemDeckSession");
	xml->setAttribute ("masterDeck", masterDeck);
	xml->setAttribute ("masterTurnedOff", masterTurnedOff);
	xml->setAttribute ("autoDj", autoDj);

	for (size_t d = 0; d < decks.size(); ++d)
		writeDeck (*xml, decks[d], true, (int) d);

	writeLibraryAndJobs (*xml, library, stemJobs);
	return xml;
}

Session Session::fromXml (const juce::XmlElement& xml)
{
	Session session;
	session.masterDeck = xml.getIntAttribute ("masterDeck", -1);
	session.masterTurnedOff = xml.getBoolAttribute ("masterTurnedOff");
	session.autoDj = xml.getBoolAttribute ("autoDj");

	for (auto* e : xml.getChildWithTagNameIterator ("Deck"))
	{
		const auto d = e->getIntAttribute ("index", -1);
		if (d >= 0 && d < (int) session.decks.size())
			readDeck (*e, session.decks[(size_t) d], true);
		}

	readLibraryAndJobs (xml, session.library, session.stemJobs);
	return session;
}

std::optional<Session> Session::load (const juce::File& file)
{
	if (auto xml = juce::XmlDocument::parse (file); xml != nullptr && xml->hasTagName ("StemDeckSession"))
		return fromXml (*xml);
	return std::nullopt;
}

std::unique_ptr<juce::XmlElement> OneDeckSession::toXml() const
{
	auto xml = std::make_unique<juce::XmlElement> ("StemDeckOneDeck");
	writeDeck (*xml, deck, false);
	writeLibraryAndJobs (*xml, library, stemJobs);
	return xml;
}

OneDeckSession OneDeckSession::fromXml (const juce::XmlElement& xml)
{
	OneDeckSession session;
	if (auto* e = xml.getChildByName ("Deck"))
		readDeck (*e, session.deck, false);
	readLibraryAndJobs (xml, session.library, session.stemJobs);
	return session;
}

std::optional<OneDeckSession> OneDeckSession::load (const juce::File& file)
{
	if (auto xml = juce::XmlDocument::parse (file); xml != nullptr && xml->hasTagName ("StemDeckOneDeck"))
		return fromXml (*xml);
	return std::nullopt;
}

OneDeckSession importFromTwoDeck (const Session& session, char letter)
{
	// The two-deck session holds decks A and B; a letter beyond them starts
	// with an empty deck and shares the library and the jobs (Ruling A8).
	OneDeckSession one;
	if (letter == 'A' || letter == 'B')
		one.deck = session.decks[letter == 'B' ? 1 : 0];
	one.deck.faderDb = 0.0;
	for (auto& stem : one.deck.stems)
		stem.buses = 0;
	one.library = session.library;
	one.stemJobs = session.stemJobs;
	one.imported = true;
	return one;
}

juce::File oneDeckSessionFile (const juce::File& settingsFile, const DeckInstance& instance)
{
	return settingsFile.getSiblingFile (instance.sessionDir()).getChildFile ("session.xml");
}

OneDeckSession loadOrImport (const juce::File& oneDeckFile, const juce::File& twoDeckFile, const DeckInstance& instance)
{
	if (auto own = OneDeckSession::load (oneDeckFile))
		return *own;
	if (auto old = Session::load (twoDeckFile))
		return importFromTwoDeck (*old, instance.letter);
	return {};
}
