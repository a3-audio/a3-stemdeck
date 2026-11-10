#pragma once

#include <juce_core/juce_core.h>
#include "Buses.h"
#include "DeckInstance.h"

#include <array>
#include <vector>

// What StemDeck remembers across a restart: both decks as they were, playing
// ones playing on from where they were, the mixer, the library view and the
// stem creator's queue. Written every couple of seconds and on quit to
// ~/.config/StemDeck/session.xml, so a crash or a power cut loses at most
// that much.

struct StemMixState
{
	double gainDb = 0.0;
	bool muted = false;
	unsigned buses = 0;     // bit per bus (Buses.h)
	bool cue = false;       // the stem's cue send, apart from the buses

	// The stem's place in the deck's spatial engine (DeckSpatial::Controls).
	double azimuth = 0.0, elevation = 0.0;        // home direction, degrees; azimuth -180..180, elevation -90..90
	double width = 60.0;                          // degrees, 0..360 (IEM StereoEncoder)
	double lengthBars = 4.0;                      // one-shot length: 0.25, 0.5, 1, 2, 4 or 8 bars
	double aux = 0.0;                             // send to the deck's AUX bus, 0..1; one-deck sessions only
	bool reversed = false;                        // ORBIT turns the other way, CROSS jumps backwards (hold and swipe on ORBIT)
};

struct DeckSession
{
	juce::String setId;     // the set's first stem file, relative to the library; empty: deck empty
	double position = 0.0;
	bool playing = false;
	double cuePoint = 0.0;
	bool quantize = true;       // QUANTIZE: jumps, cue and loop edges snap to the grid
	bool eventsEnabled = true;  // EVENTS: the track's prepared events fire
	juce::Range<double> loop;   // empty: no loop
	bool repeat = false;
	bool continuePlay = false;  // CONTINUE (PlayMode.h); one-deck sessions only

	double tempo = 1.0;         // the fader's ratio
	double tempoRange = 0.08;
	bool vinyl = true;
	bool sync = false;
	bool keyLock = false;       // tempo changes keep the pitch

	std::array<StemMixState, buses::stemsPerDeck> stems;
	double faderDb = 0.0;
};

struct LibrarySession
{
	int sortColumn = 0;         // 0: leave as it is
	bool sortForwards = true;
	juce::String search;
	juce::String selectedSetId;
	juce::String folder;        // the chosen folder, relative to the library; "" all
	bool showFolders = true;    // the folder tree beside the table
};

struct QueuedStemJob
{
	juce::String input, folder, track;
};

struct Session
{
	std::array<DeckSession, 2> decks;
	int masterDeck = -1;
	bool masterTurnedOff = false;
	bool autoDj = false;
	LibrarySession library;
	std::vector<QueuedStemJob> stemJobs;

	std::unique_ptr<juce::XmlElement> toXml() const;
	static Session fromXml (const juce::XmlElement& xml);

	static juce::File file (const juce::File& settingsFile) { return settingsFile.getSiblingFile ("session.xml"); }
	static std::optional<Session> load (const juce::File& file);
};

// The session of one `-bformat` instance: one deck, no routing (the deck's
// stems all go to the spatial engine), in ~/.config/StemDeck/<letter>/session.xml.
struct OneDeckSession
{
	DeckSession deck;
	LibrarySession library;
	std::vector<QueuedStemJob> stemJobs;
	bool imported = false;      // taken from the two-deck session this start; not stored

	std::unique_ptr<juce::XmlElement> toXml() const;
	static OneDeckSession fromXml (const juce::XmlElement& xml);
	static std::optional<OneDeckSession> load (const juce::File& file);
};

// A -> decks[0], B -> decks[1]. Buses and the channel fader are dropped; the
// stems' spatial state, the library view and the stem queue are kept.
OneDeckSession importFromTwoDeck (const Session& session, char letter);

juce::File oneDeckSessionFile (const juce::File& settingsFile, const DeckInstance& instance);

// The one-deck file wins when it exists; otherwise the two-deck file is read
// (never changed: two-deck mode still owns it) and the result is marked
// `imported`, so that the first save makes the one-deck file the truth. A
// one-deck file that is corrupt or has the wrong root counts as absent and
// falls through to the import (the start must not fail on it).
OneDeckSession loadOrImport (const juce::File& oneDeckFile, const juce::File& twoDeckFile, const DeckInstance& instance);
