#pragma once

#include "Buses.h"

#include <array>

// Where everything on the main surface sits (the "clean surface", 2026-10-08).
// Pure: no JUCE, so the geometry can be tested at the rig's 768 x 1024 -- and
// at any other size -- without a window. The components call these with
// their own bounds and apply the result (SurfaceJuce.h converts);
// MainComponent places the sections and the band, whose parts belong to the
// decks and the mixer.
//
// Responsive: every size and gap is a share of what it hangs on -- the
// window, a section, a tile -- never a pixel count.
namespace surface
{
	struct Rect
	{
		int x = 0, y = 0, w = 0, h = 0;

		int right() const { return x + w; }
		int bottom() const { return y + h; }
		bool isEmpty() const { return w <= 0 || h <= 0; }
		bool contains (const Rect& other) const;
		bool intersects (const Rect& other) const;   // sharing an edge is not overlapping
		Rect translated (int dx, int dy) const { return { x + dx, y + dy, w, h }; }
		Rect reduced (int dx, int dy) const;
		Rect reduced (int d) const { return reduced (d, d); }
		Rect withSizeKeepingCentre (int width, int height) const;

		// Cut a slice off one side and return it; clamped to what is there.
		Rect removeFromTop (int amount);
		Rect removeFromBottom (int amount);
		Rect removeFromLeft (int amount);
		Rect removeFromRight (int amount);

		bool operator== (const Rect& o) const { return x == o.x && y == o.y && w == o.w && h == o.h; }
		bool operator!= (const Rect& o) const { return ! (*this == o); }
	};

	// The two lengths every part of the surface shares, from the window's
	// width: the gap (between sections, around tiles, from a deck's line to a
	// tile) and the deck lines' width.
	struct Metrics
	{
		int gap = 0, line = 0;
	};
	Metrics metrics (int windowWidth);

	// The window: top bar; the two rolling waveforms, A over B, full width;
	// deck A | mixer | deck B; below them the band across the whole width;
	// the library.
	// Between the band and the library each deck's event strip (EventStrip.h),
	// under its tile in the band: taken from the
	// library's share, so the band and the overviews keep their room.
	struct Sections
	{
		Rect topBar, waves, waveA, waveB;
		std::array<Rect, 2> deck;
		Rect mixer, band, library;
		std::array<Rect, 2> events;
		Metrics metrics;
	};
	Sections sections (int width, int height);

	// The event strip, in its own coordinates: ten slots of one size -- REC,
	// EVENTS, QUANTIZE (the deck's switches), the four nudge keys, ACTIVE and
	// DELETE (the selected event's), and the note. One row on the one-deck
	// screen (switches, nudges, ACTIVE, DELETE, note); two on the two-deck
	// screen (switches, ACTIVE, DELETE over the nudges and the note). The
	// nudge keys always stand together, DELETE always beside ACTIVE.
	struct EventStripKeys
	{
		Rect rec, events, quantize, active, remove, note;
		std::array<Rect, 4> nudge;   // a bar back, a beat back, a beat on, a bar on
	};
	EventStripKeys eventStrip (Rect local, Metrics metrics, int rows);

	// The band under the decks and the mixer, in window coordinates: one
	// tile (card) per deck and one for the output meters between them, each
	// at the same gap from the decks' lines, which run between the tiles.
	// A deck's tile, from the outside in: the pitch column (the value above
	// the fader, the range key below it); the title and stems line above the
	// overview, the BPM and the original under it, KEY (key lock) before
	// them beside the range key; the volume fader (the stems' meters in its
	// slot). B mirrored.
	struct DeckTile
	{
		Rect tile;
		Rect pitchValue, pitch, keyLock, range;
		Rect title, stems, overview, bpm, bpmInfo;
		Rect fader;
	};
	struct Band
	{
		std::array<DeckTile, 2> deck;
		Rect metersTile, meters;   // the meters' bars and captions inside their tile
	};
	Band band (Rect area, Metrics metrics);

	// The deck's colour line, in window coordinates, as one piece: the stripe
	// at the deck column's inner edge (the column paints it, deckStripe), its
	// drop into the gap above the band, the top along that gap, and the side
	// between the deck's tile and the meters' tile down to the band's foot.
	// Every tile keeps one gap from it.
	struct DeckLine
	{
		Rect stripe, drop, top, side;
	};
	Rect deckStripe (Rect deck, int deckIndex, Metrics metrics);   // in `deck`'s coordinates (window or local)
	DeckLine deckLine (const Sections& sections, const Band& band, int deckIndex);

	// A deck column above the band, in its own coordinates: the times, the
	// Grid Adjust rows, the keys, and at the foot the transport on its tile
	// (|< >| over CUE | PLAY, the biggest keys), one gap from the stripe.
	// No key moves when GRID toggles: with GRID off the times take the Grid
	// Adjust rows' room (stacked and bigger), with GRID on they share a row.
	struct DeckColumn
	{
		Rect elapsed, remaining;
		Rect transportTile, previous, cue, play, next;
		Rect loopOff, repeat, continuePlay, sync, master, vinyl, grid;
		std::array<Rect, 5> gridNudge;   // <1/2  <1  SET 1  1>  1/2>
		std::array<Rect, 5> gridEdit;    // SNAP  SHIFT  RESET  x2  1/2
	};
	// The transport in the column's foot (the two-deck screen), or placed by
	// the window, which leaves the column's room to its other keys (the
	// one-deck screen's foot row, oneDeckFoot); there LOOP OFF is the foot
	// row's LOOP, and its row is the play mode's: REPEAT | CONTINUE.
	enum class Transport { inColumn, elsewhere };
	DeckColumn deckColumn (Rect local, bool gridOn, int deckIndex, Metrics metrics, Transport transport = Transport::inColumn);

	// The one-deck screen's foot row, the transport band (2026-10-28), in its
	// own coordinates, left to right: on the transport's tile the loop block
	// (LOOP on top, LOOP IN | LOOP OUT under it), the track search (|< over
	// >|), and CUE | PLAY, as tall as the row and the biggest keys; then the
	// meters' card at the right, one bar each for MAIN, AUX and CUE.
	struct TransportKeys
	{
		Rect tile, previous, cue, play, next;
		Rect loop, loopIn, loopOut;   // the one-deck foot's only
	};
	struct OneDeckFoot
	{
		TransportKeys transport;
		Rect metersTile;
	};
	OneDeckFoot oneDeckFoot (Rect row, Metrics metrics);

	// Inside the meters' card (its own coordinates): MAIN, AUX, CUE left to
	// right, each a bar over its bus's name.
	struct MeterSlot
	{
		Rect bar, caption;
	};
	std::array<MeterSlot, 3> oneDeckMeterSlots (Rect tile, Metrics metrics);

	// How tall a deck column is for keys `keyHeight` high.
	int deckColumnHeight (int keyHeight, Metrics metrics);

	// One stem's row in a channel strip: its own channel and AUX (the only
	// buses it may use, Buses.h mayUse), the knob, the cue switch and the
	// mute. Deck B reads n, AUX, CUE, M, knob left to right (the knob on the
	// outer side); deck A is mirrored: knob, M, CUE, AUX, n. The rows lie tight under each other: a
	// matrix of keys.
	struct StemRow
	{
		Rect frame, knob, cue, channel, aux, mute;
	};

	// A channel strip above the band, in its own coordinates: the header and
	// the stem rows, which share its height.
	struct Strip
	{
		Rect header;
		std::array<StemRow, buses::stemsPerDeck> stems;
	};
	Strip channelStrip (Rect local, int deckIndex);

	// The mixer above the band, in its own coordinates: two strips.
	std::array<Rect, 2> mixerStrips (Rect local);

	// The output meters' caption line (the bus names) under the bars; the
	// volume faders end where the bars do.
	int meterCaptionHeight (int metersHeight);

	// A hairline between keys of `size`: a share of it, never nothing.
	int hairline (int size);
}
