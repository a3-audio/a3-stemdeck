#include <gtest/gtest.h>

#include "SurfaceLayout.h"

#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace
{
	using surface::Rect;

	// The rig's screen: a3nuc2's touchscreen, portrait.
	constexpr int rigWidth = 768, rigHeight = 1024;

	// Responsive: the same relations at other windows.
	const std::vector<std::pair<int, int>> windows { { rigWidth, rigHeight }, { 600, 800 }, { 1080, 1920 } };

	std::string sizeName (int w, int h) { return std::to_string (w) + "x" + std::to_string (h); }

	struct Placed
	{
		std::string name;
		Rect bounds;
		bool interactive;
	};

	struct Section
	{
		std::string name;
		Rect bounds;
		std::vector<Placed> controls;
	};

	std::string deckName (int deck) { return deck == 0 ? "A" : "B"; }

	// Every control the surface shows, in window coordinates, with the section
	// it belongs to.
	std::vector<Section> placeAll (int width, int height, std::array<bool, 2> gridOn)
	{
		const auto s = surface::sections (width, height);
		std::vector<Section> all;

		all.push_back ({ "waves", s.waves, { { "waveA", s.waveA, true }, { "waveB", s.waveB, true } } });

		for (int d = 0; d < 2; ++d)
		{
			const auto origin = s.deck[(size_t) d];
			const auto c = surface::deckColumn (Rect { 0, 0, origin.w, origin.h }, gridOn[(size_t) d], d, s.metrics);
			const auto at = [&origin] (const Rect& r) { return r.translated (origin.x, origin.y); };
			Section deck { "deck" + deckName (d), origin, {} };
			const auto add = [&] (const std::string& name, const Rect& r, bool interactive)
			{
				deck.controls.push_back ({ deck.name + "." + name, at (r), interactive });
			};
			add ("elapsed", c.elapsed, false);
			add ("remaining", c.remaining, false);
			add ("transportTile", c.transportTile, false);
			add ("previous", c.previous, true);
			add ("cue", c.cue, true);
			add ("play", c.play, true);
			add ("next", c.next, true);
			add ("loopOff", c.loopOff, true);
			add ("repeat", c.repeat, true);
			add ("sync", c.sync, true);
			add ("master", c.master, true);
			add ("vinyl", c.vinyl, true);
			add ("grid", c.grid, true);

			if (gridOn[(size_t) d])
			{
				for (size_t i = 0; i < c.gridNudge.size(); ++i)
					add ("nudge" + std::to_string (i), c.gridNudge[i], true);
				for (size_t i = 0; i < c.gridEdit.size(); ++i)
					add ("gridEdit" + std::to_string (i), c.gridEdit[i], true);
			}
			all.push_back (deck);
		}

		Section mixer { "mixer", s.mixer, {} };
		const auto strips = surface::mixerStrips (Rect { 0, 0, s.mixer.w, s.mixer.h });
		for (int d = 0; d < 2; ++d)
		{
			const auto stripOrigin = strips[(size_t) d].translated (s.mixer.x, s.mixer.y);
			const auto strip = surface::channelStrip (Rect { 0, 0, stripOrigin.w, stripOrigin.h }, d);
			const auto at = [&] (const Rect& r) { return r.translated (stripOrigin.x, stripOrigin.y); };
			const auto prefix = "strip" + deckName (d) + ".";

			for (size_t stem = 0; stem < strip.stems.size(); ++stem)
			{
				const auto& row = strip.stems[stem];
				const auto stemName = prefix + "stem" + std::to_string (stem) + ".";
				mixer.controls.push_back ({ stemName + "knob", at (row.knob), true });
				mixer.controls.push_back ({ stemName + "mute", at (row.mute), true });
				mixer.controls.push_back ({ stemName + "cue", at (row.cue), true });
				mixer.controls.push_back ({ stemName + "channel", at (row.channel), true });
				mixer.controls.push_back ({ stemName + "aux", at (row.aux), true });
			}
		}
		all.push_back (mixer);

		const auto b = surface::band (s.band, s.metrics);
		Section band { "band", s.band, { { "metersTile", b.metersTile, false }, { "meters", b.meters, false } } };
		for (size_t d = 0; d < 2; ++d)
		{
			const auto name = deckName ((int) d);
			const auto& deck = b.deck[d];
			band.controls.push_back ({ "tile" + name, deck.tile, false });
			band.controls.push_back ({ "pitch" + name, deck.pitch, true });
			band.controls.push_back ({ "pitchValue" + name, deck.pitchValue, false });
			band.controls.push_back ({ "range" + name, deck.range, true });
			band.controls.push_back ({ "keyLock" + name, deck.keyLock, true });
			band.controls.push_back ({ "bpm" + name, deck.bpm, false });
			band.controls.push_back ({ "bpmInfo" + name, deck.bpmInfo, false });
			band.controls.push_back ({ "title" + name, deck.title, false });
			band.controls.push_back ({ "stems" + name, deck.stems, false });
			band.controls.push_back ({ "overview" + name, deck.overview, true });
			band.controls.push_back ({ "fader" + name, deck.fader, true });
		}
		all.push_back (band);

		for (int d = 0; d < 2; ++d)
		{
			const auto origin = s.events[(size_t) d];
			const auto k = surface::eventStrip (Rect { 0, 0, origin.w, origin.h }, s.metrics, 2);
			Section strip { "events" + deckName (d), origin, {} };
			const auto add = [&] (const std::string& name, const Rect& r, bool interactive)
			{
				strip.controls.push_back ({ strip.name + "." + name, r.translated (origin.x, origin.y), interactive });
			};
			add ("rec", k.rec, true);
			add ("events", k.events, true);
			add ("quantize", k.quantize, true);
			add ("active", k.active, true);
			add ("delete", k.remove, true);
			for (size_t i = 0; i < k.nudge.size(); ++i)
				add ("nudge" + std::to_string (i), k.nudge[i], true);
			add ("note", k.note, false);
			all.push_back (strip);
		}

		all.push_back ({ "library", s.library, {} });
		return all;
	}

	void expectCleanSurface (int width, int height, std::array<bool, 2> gridOn)
	{
		const auto all = placeAll (width, height, gridOn);
		const Rect screen { 0, 0, width, height };
		const auto at = sizeName (width, height);
		std::vector<Placed> interactive;

		for (const auto& section : all)
		{
			EXPECT_TRUE (screen.contains (section.bounds)) << section.name << " leaves the screen at " << at;

			for (const auto& control : section.controls)
			{
				EXPECT_FALSE (control.bounds.isEmpty()) << control.name << " has no size at " << at;
				EXPECT_TRUE (section.bounds.contains (control.bounds)) << control.name << " leaves " << section.name << " at " << at;
				if (control.interactive)
					interactive.push_back (control);
			}
		}

		for (size_t i = 0; i < interactive.size(); ++i)
			for (size_t j = i + 1; j < interactive.size(); ++j)
				EXPECT_FALSE (interactive[i].bounds.intersects (interactive[j].bounds))
					<< interactive[i].name << " overlaps " << interactive[j].name << " at " << at;
	}
}

TEST (SurfaceLayout, TheSectionsDoNotOverlap)
{
	for (const auto& [w, h] : windows)
	{
		const auto s = surface::sections (w, h);
		const std::vector<std::pair<const char*, Rect>> sections {
			{ "topBar", s.topBar }, { "waves", s.waves }, { "deckA", s.deck[0] }, { "deckB", s.deck[1] },
			{ "mixer", s.mixer }, { "band", s.band }, { "eventsA", s.events[0] }, { "eventsB", s.events[1] },
			{ "library", s.library } };

		for (size_t i = 0; i < sections.size(); ++i)
			for (size_t j = i + 1; j < sections.size(); ++j)
				EXPECT_FALSE (sections[i].second.intersects (sections[j].second))
					<< sections[i].first << " / " << sections[j].first << " at " << sizeName (w, h);
	}
}

TEST (SurfaceLayout, EveryControlInItsSectionNoTwoOverlapAtEverySize)
{
	for (const auto& [w, h] : windows)
		for (const auto grid : { std::array<bool, 2> { false, false }, { true, true }, { true, false }, { false, true } })
			expectCleanSurface (w, h, grid);
}

// The gap and the line are shares of the window, nothing fixed.
TEST (SurfaceLayout, GapAndLineGrowWithTheWindow)
{
	const auto small = surface::metrics (600), rig = surface::metrics (rigWidth), large = surface::metrics (1080);
	EXPECT_LT (small.gap, rig.gap);
	EXPECT_LT (rig.gap, large.gap);
	EXPECT_LE (small.line, rig.line);
	EXPECT_LT (rig.line, large.line);
	EXPECT_GT (small.line, 0);
}

TEST (SurfaceLayout, WaveformsFullWidthBandUnderDecksAndMixer)
{
	for (const auto& [w, h] : windows)
	{
		const auto s = surface::sections (w, h);
		EXPECT_EQ (s.waveA.x, s.waves.x);
		EXPECT_EQ (s.waveA.w, s.waves.w);
		EXPECT_EQ (s.band.x, s.deck[0].x);
		EXPECT_EQ (s.band.right(), s.deck[1].right());
		EXPECT_GT (s.band.y, s.mixer.bottom());
		EXPECT_EQ (s.deck[0].bottom(), s.mixer.bottom());
	}
}

// Each deck's tile: the pitch column at the outer edge (value above the
// fader, range below), the title above the overview, BPM under it, the volume
// fader towards the meters; the meters' tile between the decks' tiles.
TEST (SurfaceLayout, OneTilePerDeckAndOneForTheMeters)
{
	for (const auto& [w, h] : windows)
	{
		const auto at = sizeName (w, h);
		const auto s = surface::sections (w, h);
		const auto b = surface::band (s.band, s.metrics);

		for (size_t d = 0; d < 2; ++d)
		{
			const auto& deck = b.deck[d];
			for (const auto& part : { deck.pitchValue, deck.pitch, deck.keyLock, deck.range, deck.title, deck.stems,
									  deck.overview, deck.bpm, deck.bpmInfo, deck.fader })
				EXPECT_TRUE (deck.tile.contains (part)) << at;
			EXPECT_FALSE (deck.tile.intersects (b.metersTile)) << at;

			EXPECT_LT (deck.pitchValue.bottom(), deck.pitch.y + 1) << "the value above the fader, " << at;
			EXPECT_LT (deck.pitch.bottom(), deck.range.y + 1) << "the range key below it, " << at;
			EXPECT_LT (deck.title.bottom(), deck.stems.y + 1) << at;
			EXPECT_LT (deck.stems.bottom(), deck.overview.y + 1) << "the title above the overview, " << at;
			EXPECT_LT (deck.overview.bottom(), deck.bpm.y + 1) << "BPM under it, " << at;
			EXPECT_LT (deck.bpm.bottom(), deck.bpmInfo.y + 1) << at;
			EXPECT_EQ (deck.fader.bottom(), b.meters.bottom() - surface::meterCaptionHeight (b.meters.h))
				<< "the fader ends where the meter bars do, " << at;
			EXPECT_EQ (deck.fader.y, b.meters.y) << at;
			EXPECT_EQ (deck.tile.y, b.metersTile.y) << at;
			EXPECT_EQ (deck.tile.h, b.metersTile.h) << at;
		}

		// Pitch at the outer edges, the faders towards the meters.
		EXPECT_LT (b.deck[0].pitch.right(), b.deck[0].overview.x);
		EXPECT_LT (b.deck[0].overview.right(), b.deck[0].fader.x);
		EXPECT_LT (b.deck[1].fader.right(), b.deck[1].overview.x);
		EXPECT_LT (b.deck[1].overview.right(), b.deck[1].pitch.x);
		EXPECT_LT (b.deck[0].tile.right(), b.metersTile.x);
		EXPECT_GT (b.deck[1].tile.x, b.metersTile.right());
		EXPECT_TRUE (b.metersTile.contains (b.meters));
	}
}

// The deck's colour line keeps its path -- stripe, drop, top, side -- and every
// tile stands one gap from it: the deck's tile, the meters' tile, the
// transport's tile in the column. The same gap everywhere, at every size.
TEST (SurfaceLayout, TilesStandOneGapFromTheDeckLines)
{
	for (const auto& [w, h] : windows)
	{
		const auto at = sizeName (w, h);
		const auto s = surface::sections (w, h);
		const auto m = s.metrics;
		const auto b = surface::band (s.band, m);

		for (int d = 0; d < 2; ++d)
		{
			const auto line = surface::deckLine (s, b, d);
			const auto& deck = s.deck[(size_t) d];
			const auto& tile = b.deck[(size_t) d].tile;

			EXPECT_EQ (line.stripe, surface::deckStripe (deck, d, m)) << "the column paints the same stripe";
			EXPECT_TRUE (deck.contains (line.stripe));
			EXPECT_EQ (line.drop.y, line.stripe.bottom()) << "continuous, " << at;
			EXPECT_EQ (line.drop.bottom(), line.top.y);
			EXPECT_EQ (line.side.y, line.top.y);
			EXPECT_EQ (line.side.bottom(), s.band.bottom()) << "down to the band's foot";
			EXPECT_FALSE (line.top.intersects (s.mixer));
			EXPECT_FALSE (line.top.intersects (deck));

			// One gap: above the tiles, between the line and either tile.
			EXPECT_EQ (tile.y - line.top.bottom(), m.gap) << at;
			EXPECT_EQ (b.metersTile.y - line.top.bottom(), m.gap) << at;
			if (d == 0)
			{
				EXPECT_EQ (line.side.x - tile.right(), m.gap) << at;
				EXPECT_EQ (b.metersTile.x - line.side.right(), m.gap) << at;
				EXPECT_EQ (line.top.x, line.stripe.x);
				EXPECT_EQ (line.top.right(), line.side.right());
			}
			else
			{
				EXPECT_EQ (tile.x - line.side.right(), m.gap) << at;
				EXPECT_EQ (line.side.x - b.metersTile.right(), m.gap) << at;
				EXPECT_EQ (line.top.right(), line.stripe.right());
				EXPECT_EQ (line.top.x, line.side.x);
			}

			// The transport's tile, one gap from the stripe.
			const auto column = surface::deckColumn (Rect { 0, 0, deck.w, deck.h }, false, d, m);
			const auto transport = column.transportTile.translated (deck.x, deck.y);
			if (d == 0)
				EXPECT_EQ (line.stripe.x - transport.right(), m.gap) << at;
			else
				EXPECT_EQ (transport.x - line.stripe.right(), m.gap) << at;
			for (const auto& key : { column.previous, column.next, column.cue, column.play })
				EXPECT_TRUE (column.transportTile.reduced (m.gap).contains (key)) << "a gap inside the tile, " << at;
		}
	}
}

// KEY (key lock) with the tempo's keys: at the foot of the tile, beside the
// range key, before the BPM -- the pitch fader keeps its whole travel.
TEST (SurfaceLayout, TheKeyLockKeyStandsWithTheTempoControls)
{
	for (const auto& [w, h] : windows)
	{
		const auto at = sizeName (w, h);
		const auto s = surface::sections (w, h);
		const auto b = surface::band (s.band, s.metrics);

		for (size_t d = 0; d < 2; ++d)
		{
			const auto& deck = b.deck[d];
			EXPECT_TRUE (deck.tile.contains (deck.keyLock)) << at;
			EXPECT_EQ (deck.keyLock.bottom(), deck.range.bottom()) << "level with the range key at the foot, " << at;
			EXPECT_GE (deck.keyLock.h, deck.range.h) << at;
			EXPECT_EQ (deck.keyLock.y, deck.bpm.y) << "beside the BPM, " << at;
			EXPECT_EQ (deck.keyLock.w, deck.pitch.w) << at;
		}

		// From the outside in: range key, KEY, BPM.
		EXPECT_LT (b.deck[0].range.right(), b.deck[0].keyLock.x) << at;
		EXPECT_LT (b.deck[0].keyLock.right(), b.deck[0].bpm.x) << at;
		EXPECT_LT (b.deck[1].bpm.right(), b.deck[1].keyLock.x) << at;
		EXPECT_LT (b.deck[1].keyLock.right(), b.deck[1].range.x) << at;
	}

	const auto s = surface::sections (rigWidth, rigHeight);
	const auto b = surface::band (s.band, s.metrics);
	EXPECT_GE (b.deck[0].keyLock.h, 26) << "a finger's key on the rig, as the range key";
	EXPECT_GE (b.deck[0].bpm.w, 90) << "room for 129.00";
}

// No key moves when GRID turns on: the finger that turned it on turns it off.
TEST (SurfaceLayout, DeckKeysStayPutWithGrid)
{
	for (const auto& [w, h] : windows)
		for (int d = 0; d < 2; ++d)
		{
			const auto s = surface::sections (w, h);
			const Rect column { 0, 0, s.deck[(size_t) d].w, s.deck[(size_t) d].h };
			const auto off = surface::deckColumn (column, false, d, s.metrics);
			const auto on = surface::deckColumn (column, true, d, s.metrics);

			for (const auto& [a, b] : { std::pair { off.previous, on.previous }, { off.cue, on.cue }, { off.play, on.play },
										{ off.next, on.next }, { off.loopOff, on.loopOff }, { off.repeat, on.repeat },
										{ off.sync, on.sync }, { off.master, on.master }, { off.vinyl, on.vinyl }, { off.grid, on.grid } })
				EXPECT_EQ (a, b) << sizeName (w, h);

			EXPECT_LT (on.remaining.bottom(), on.gridNudge[0].y + 1) << "Grid Adjust under the times";
			EXPECT_LT (on.gridEdit[0].bottom(), on.loopOff.y + 1) << "the keys under Grid Adjust";

			// With GRID off the times take the Grid Adjust rows' room: nothing empty.
			EXPECT_LE (off.elapsed.y, on.elapsed.y);
			EXPECT_GE (off.remaining.bottom(), on.gridEdit[0].bottom());
			EXPECT_GT (off.elapsed.h, on.elapsed.h) << "bigger when they have the room";
		}
}

// PLAY, CUE, NEXT and PREV: the biggest keys, at the bottom, within reach --
// a 2 x 2 block, CUE | PLAY at the foot, |< | >| over them.
TEST (SurfaceLayout, TheTransportIsTheBiggestAndLowest)
{
	for (const auto& [w, h] : windows)
	{
		const auto s = surface::sections (w, h);
		for (const auto gridOn : { false, true })
		{
			const auto c = surface::deckColumn (Rect { 0, 0, s.deck[0].w, s.deck[0].h }, gridOn, 0, s.metrics);
			std::vector<Rect> others { c.loopOff, c.repeat, c.sync, c.master, c.vinyl, c.grid };
			others.insert (others.end(), c.gridNudge.begin(), c.gridNudge.end());
			others.insert (others.end(), c.gridEdit.begin(), c.gridEdit.end());

			for (const auto& key : { c.previous, c.next, c.cue, c.play })
				for (const auto& other : others)
				{
					EXPECT_GT (key.h, other.h) << sizeName (w, h);
					EXPECT_GT (key.w * key.h, other.w * other.h) << "a larger target, " << sizeName (w, h);
					EXPECT_GT (key.y, other.y) << "below every other key";
				}

			EXPECT_EQ (c.cue.y, c.play.y);
			EXPECT_EQ (c.previous.y, c.next.y);
			EXPECT_LT (c.previous.bottom(), c.cue.y + 1) << "track search over CUE | PLAY";
			EXPECT_EQ (c.cue.x, c.previous.x);
			EXPECT_EQ (c.play.right(), c.next.right());
			EXPECT_GE (c.transportTile.bottom(), s.deck[0].h - s.deck[0].h / 20) << "at the column's foot";
		}
	}
}

TEST (SurfaceLayout, DeckKeysAreAtLeastFingerHighOnTheRig)
{
	const auto s = surface::sections (rigWidth, rigHeight);
	const auto c = surface::deckColumn (Rect { 0, 0, s.deck[0].w, s.deck[0].h }, true, 0, s.metrics);

	for (const auto& key : { c.cue, c.play, c.previous, c.next })
		EXPECT_GE (key.h, 40);
	for (const auto& key : { c.loopOff, c.sync, c.vinyl, c.grid })
		EXPECT_GE (key.h, 28);
	for (const auto& key : c.gridNudge)
		EXPECT_GE (key.h, 22);
	for (const auto& key : c.gridEdit)
		EXPECT_GE (key.h, 22);
	EXPECT_GE (surface::band (s.band, s.metrics).deck[0].range.h, 26);
}

// The mixer's stem rows: the knob half its old size, the stem's channel, AUX,
// cue and mute in one row, the four rows tight under each other -- a matrix
// of keys per deck, the two decks' matrices apart.
TEST (SurfaceLayout, StemRowsFormAMatrix)
{
	for (const auto& [w, h] : windows)
	{
		const auto s = surface::sections (w, h);
		const auto strips = surface::mixerStrips (Rect { 0, 0, s.mixer.w, s.mixer.h });
		const auto strip = surface::channelStrip (Rect { 0, 0, strips[0].w, strips[0].h }, 0);

		for (size_t r = 0; r < strip.stems.size(); ++r)
		{
			const auto& row = strip.stems[r];
			for (const auto& key : { row.cue, row.aux, row.channel })
			{
				EXPECT_EQ (key.y, row.mute.y);
				EXPECT_EQ (key.h, row.mute.h);
			}
			EXPECT_NE (row.mute.x, row.channel.x);
			EXPECT_TRUE (row.frame.contains (row.mute));

			if (r + 1 < strip.stems.size())
			{
				const auto& next = strip.stems[r + 1];
				EXPECT_EQ (row.frame.bottom(), next.frame.y) << "no gap between the rows";
				EXPECT_EQ (row.channel.x, next.channel.x) << "columns line up";
				EXPECT_EQ (row.aux.x, next.aux.x) << "columns line up";
				EXPECT_EQ (row.cue.x, next.cue.x) << "columns line up";
			}
		}
		EXPECT_GT (strips[1].x - strips[0].right(), 0) << "deck A's and deck B's matrices apart";
	}

	const auto s = surface::sections (rigWidth, rigHeight);
	const auto strips = surface::mixerStrips (Rect { 0, 0, s.mixer.w, s.mixer.h });
	const auto strip = surface::channelStrip (Rect { 0, 0, strips[0].w, strips[0].h }, 0);
	EXPECT_LE (strip.stems[0].knob.w, 28) << "half the 48 px knob";
	EXPECT_GE (strip.stems[0].channel.h, 30) << "the keys grew into the freed height";
}

// A stem reaches only its own channel and AUX (Buses.h mayUse), so its row is
// its channel, knob, AUX, cue and mute: deck B n, knob, AUX, CUE, M left to
// right, deck A mirrored (M, CUE, AUX, knob, n) -- the knob between the
// channel and the buses.
TEST (SurfaceLayout, AStemRowIsItsChannelKnobAuxCueAndMute)
{
	for (const auto& [w, h] : windows)
	{
		const auto s = surface::sections (w, h);
		const auto strips = surface::mixerStrips (Rect { 0, 0, s.mixer.w, s.mixer.h });
		for (int d = 0; d < 2; ++d)
		{
			const auto strip = surface::channelStrip (Rect { 0, 0, strips[(size_t) d].w, strips[(size_t) d].h }, d);
			for (const auto& row : strip.stems)
			{
				const std::array<Rect, 5> leftToRight = d == 0 ? std::array<Rect, 5> { row.knob, row.mute, row.cue, row.aux, row.channel }
															   : std::array<Rect, 5> { row.channel, row.aux, row.cue, row.mute, row.knob };
				for (size_t k = 0; k + 1 < leftToRight.size(); ++k)
					EXPECT_LE (leftToRight[k].right(), leftToRight[k + 1].x) << "deck " << d << ": part " << k << " left of part " << k + 1;

				const std::array<Rect, 5> all { row.knob, row.cue, row.aux, row.channel, row.mute };
				for (size_t a = 0; a < all.size(); ++a)
				{
					EXPECT_TRUE (row.frame.contains (all[a])) << "deck " << d << ": part " << a;
					for (size_t b = a + 1; b < all.size(); ++b)
						EXPECT_FALSE (all[a].intersects (all[b])) << "deck " << d << ": parts " << a << " and " << b;
				}
				EXPECT_EQ (row.cue.w, row.channel.w);
				EXPECT_EQ (row.aux.w, row.channel.w);
				EXPECT_EQ (row.mute.w, row.channel.w);
				EXPECT_EQ (row.knob.w, row.knob.h) << "the knob stays square";
			}
		}
	}
}

// With two switches instead of five the keys take the room: on the rig a
// channel key is more than a sixth of the row (before, about an eighth).
TEST (SurfaceLayout, AStemRowGivesItsKeysTheRoom)
{
	const auto s = surface::sections (rigWidth, rigHeight);
	const auto strips = surface::mixerStrips (Rect { 0, 0, s.mixer.w, s.mixer.h });
	for (int d = 0; d < 2; ++d)
	{
		const auto strip = surface::channelStrip (Rect { 0, 0, strips[(size_t) d].w, strips[(size_t) d].h }, d);
		for (const auto& row : strip.stems)
			EXPECT_GT (row.channel.w, row.frame.w / 6) << "deck " << d;
	}
}

TEST (SurfaceLayout, RectSlicing)
{
	Rect r { 10, 20, 100, 50 };
	EXPECT_EQ (r.removeFromTop (10), (Rect { 10, 20, 100, 10 }));
	EXPECT_EQ (r, (Rect { 10, 30, 100, 40 }));
	EXPECT_EQ (r.removeFromRight (30), (Rect { 80, 30, 30, 40 }));
	EXPECT_EQ (r.removeFromBottom (100), (Rect { 10, 30, 70, 40 })) << "clamped to what is there";
	EXPECT_TRUE (r.isEmpty());
	EXPECT_FALSE ((Rect { 0, 0, 10, 10 }).intersects (Rect { 10, 0, 10, 10 })) << "touching is not overlapping";
	EXPECT_TRUE ((Rect { 0, 0, 10, 10 }).contains (Rect { 2, 2, 8, 8 }));
}

// SNAP SHIFT RESET x2 1/2: five keys under the five nudge keys, so the column
// of each stays put and the two tempo keys sit at the right end.
TEST (SurfaceLayout, GridEditRowHasFiveKeysUnderTheNudgeKeys)
{
	const auto s = surface::sections (rigWidth, rigHeight);
	const auto c = surface::deckColumn (Rect { 0, 0, s.deck[0].w, s.deck[0].h }, true, 0, s.metrics);

	ASSERT_EQ (c.gridEdit.size(), 5u);
	for (size_t i = 0; i < c.gridEdit.size(); ++i)
	{
		EXPECT_EQ (c.gridEdit[i].x, c.gridNudge[i].x) << "key " << i;
		EXPECT_EQ (c.gridEdit[i].w, c.gridNudge[i].w) << "key " << i;
	}
}

//==============================================================================
// The one-deck screen (2026-10-28): its outputs row lost the phones and the
// 16 B-format bars, and the transport moved into the room -- the loop block
// (LOOP over IN | OUT) at the left, the track search stacked beside it, CUE |
// PLAY as tall as the row after them, the three meters' card at the right.
namespace
{
	// OneDeckView's foot row on the rig: 15 % of its 756 x 718.
	constexpr int rigFootWidth = 756, rigFootHeight = 108;
	const std::vector<std::pair<int, int>> footRows { { rigFootWidth, rigFootHeight }, { 588, 82 }, { 1068, 190 } };

	Rect footRow (int w, int h) { return Rect { 0, 0, w, h }; }
}

TEST (SurfaceLayout, OneDeckFootHasCueThenPlayAsTallAsTheRow)
{
	for (const auto& [w, h] : footRows)
	{
		const auto row = footRow (w, h);
		const auto foot = surface::oneDeckFoot (row, surface::metrics (w));
		const auto& t = foot.transport;

		EXPECT_TRUE (row.contains (t.tile)) << sizeName (w, h);
		EXPECT_TRUE (row.contains (foot.metersTile)) << sizeName (w, h);
		EXPECT_FALSE (t.tile.intersects (foot.metersTile)) << sizeName (w, h);
		EXPECT_LT (t.tile.right(), foot.metersTile.x) << "the meters at the right, " << sizeName (w, h);

		for (const auto& key : { t.cue, t.play, t.previous, t.next })
			EXPECT_TRUE (t.tile.contains (key)) << sizeName (w, h);
		EXPECT_LT (t.cue.right(), t.play.x) << "CUE before PLAY, as on a CDJ";
		EXPECT_EQ (t.cue.y, t.play.y);
		EXPECT_EQ (t.cue.w, t.play.w);
		EXPECT_EQ (t.cue.h, t.play.h);
		EXPECT_GE (t.cue.h * 10, h * 8) << "nearly the row's height, " << sizeName (w, h);

		EXPECT_LT (t.next.right(), t.cue.x) << "track search before CUE";
		EXPECT_EQ (t.previous.x, t.next.x);
		EXPECT_EQ (t.previous.w, t.next.w);
		EXPECT_LT (t.previous.bottom(), t.next.y + 1) << "|< over >|";
		EXPECT_FALSE (t.previous.intersects (t.next));
		EXPECT_LT (t.previous.w * t.previous.h, t.cue.w * t.cue.h) << "CUE and PLAY are the biggest";
	}
}

TEST (SurfaceLayout, OneDeckTransportIsFingerSizedOnTheRig)
{
	const auto foot = surface::oneDeckFoot (footRow (rigFootWidth, rigFootHeight), surface::metrics (rigFootWidth));
	const auto& t = foot.transport;
	// About 7 px a mm: CUE and PLAY over 12 mm high and wider than high, the
	// track search and the loop keys over 6 mm each way.
	for (const auto& key : { t.cue, t.play })
	{
		EXPECT_GE (key.h, 84);
		EXPECT_GE (key.w * 2, 3 * key.h) << "wide enough to hit without looking";
	}
	for (const auto& key : { t.previous, t.next, t.loop, t.loopIn, t.loopOut })
	{
		EXPECT_GE (key.h, 40);
		EXPECT_GE (key.w, 60);
	}
}

// The transport band from left to right (2026-10-28, "den loop block doch
// links neben CUE", then "prev/next buttons links neben cue"): the loop
// block, LOOP on top, LOOP IN | LOOP OUT under it; |< over >|; CUE; PLAY;
// the meters.
TEST (SurfaceLayout, OneDeckFootRunsLoopSearchCuePlayMetersFromTheLeft)
{
	for (const auto& [w, h] : footRows)
	{
		const auto foot = surface::oneDeckFoot (footRow (w, h), surface::metrics (w));
		const auto& t = foot.transport;
		const auto size = sizeName (w, h);

		for (const auto& key : { t.loop, t.loopIn, t.loopOut })
			EXPECT_TRUE (t.tile.contains (key)) << size;
		EXPECT_LT (t.play.right(), foot.metersTile.x) << "the meters at the far right, " << size;
		EXPECT_LE (t.loop.x, t.tile.x + surface::metrics (w).gap) << "the loop block at the far left, " << size;
		EXPECT_LT (t.loopIn.right(), t.loopOut.x) << "IN before OUT, " << size;
		EXPECT_EQ (t.loopIn.y, t.loopOut.y);
		EXPECT_LE (std::abs (t.loopIn.w - t.loopOut.w), 1) << "IN and OUT alike";
		EXPECT_EQ (t.loopIn.h, t.loopOut.h);
		EXPECT_LT (t.loop.bottom(), t.loopIn.y + 1) << "LOOP on top, " << size;
		EXPECT_EQ (t.loop.x, t.loopIn.x) << "LOOP spans IN and OUT";
		EXPECT_EQ (t.loop.right(), t.loopOut.right());
		EXPECT_EQ (t.loop.h, t.loopIn.h);
		EXPECT_EQ (t.loop.y, t.previous.y) << "the loop block lines up with the track search";
		EXPECT_EQ (t.loopIn.bottom(), t.next.bottom());

		EXPECT_LT (t.loopOut.right(), t.previous.x) << "the loop block left of the track search, " << size;
		EXPECT_LT (t.previous.right(), t.cue.x) << "the track search left of CUE, " << size;
		EXPECT_LT (t.cue.right(), t.play.x) << "CUE before PLAY, " << size;

		for (const auto& small : { t.previous, t.next, t.loop, t.loopIn, t.loopOut })
			EXPECT_LT (small.w * small.h, t.cue.w * t.cue.h) << "CUE and PLAY are the biggest, " << size;
		EXPECT_GE (t.loopIn.h, 34) << "a fingertip, " << size;
	}
}

// Shares, not pixels: the same row twice as large is the same picture twice as large.
TEST (SurfaceLayout, OneDeckFootScalesWithItsRow)
{
	const auto small = surface::oneDeckFoot (footRow (rigFootWidth, rigFootHeight), surface::metrics (rigFootWidth));
	const auto large = surface::oneDeckFoot (footRow (2 * rigFootWidth, 2 * rigFootHeight), surface::metrics (2 * rigFootWidth));
	// Within a hundredth of the row: the gaps round to whole pixels.
	const auto near = [] (int doubled, int single) { return std::abs (doubled - 2 * single) <= 2 * rigFootWidth / 100; };
	for (const auto& [a, b] : { std::pair { small.transport.cue, large.transport.cue }, { small.transport.loop, large.transport.loop },
								{ small.transport.loopOut, large.transport.loopOut }, { small.transport.next, large.transport.next },
								{ small.metersTile, large.metersTile } })
	{
		EXPECT_TRUE (near (b.x, a.x)) << b.x << " vs " << a.x;
		EXPECT_TRUE (near (b.w, a.w)) << b.w << " vs " << a.w;
		EXPECT_TRUE (near (b.h, a.h)) << b.h << " vs " << a.h;
	}
}

TEST (SurfaceLayout, OneDeckMetersAreMainAuxCueOneBarEach)
{
	for (const auto& [w, h] : footRows)
	{
		const auto foot = surface::oneDeckFoot (footRow (w, h), surface::metrics (w));
		const auto tile = foot.metersTile;
		const auto slots = surface::oneDeckMeterSlots (Rect { 0, 0, tile.w, tile.h }, surface::metrics (w));
		const Rect local { 0, 0, tile.w, tile.h };

		for (size_t i = 0; i < slots.size(); ++i)
		{
			const auto& s = slots[i];
			EXPECT_TRUE (local.contains (s.bar)) << i << " " << sizeName (w, h);
			EXPECT_TRUE (local.contains (s.caption)) << i << " " << sizeName (w, h);
			EXPECT_LT (s.bar.bottom(), s.caption.y + 1) << "the bus's name under its bar";
			EXPECT_GT (s.bar.h, s.caption.h * 2) << "the bar is the slot";
			EXPECT_GE (s.bar.w, 3) << "a bar, not a hairline";
			EXPECT_EQ (s.bar.w, slots[0].bar.w);
			EXPECT_EQ (s.bar.h, slots[0].bar.h);
			if (i > 0)
			{
				EXPECT_LT (slots[i - 1].bar.right(), s.bar.x) << "MAIN, AUX, CUE left to right";
				EXPECT_FALSE (slots[i - 1].caption.intersects (s.caption));
			}
		}
	}
}

// Without its transport the deck column gives the room to the keys left in
// it: each taller than in the two-deck column, none moved below another.
TEST (SurfaceLayout, DeckColumnWithoutTransportGivesTheRoomToTheKeys)
{
	for (const auto& [w, h] : windows)
	{
		const auto s = surface::sections (w, h);
		const Rect column { 0, 0, s.deck[0].w, s.deck[0].h };
		for (const auto gridOn : { false, true })
		{
			const auto with = surface::deckColumn (column, gridOn, 0, s.metrics);
			const auto without = surface::deckColumn (column, gridOn, 0, s.metrics, surface::Transport::elsewhere);

			for (const auto& key : { without.transportTile, without.previous, without.cue, without.play, without.next })
				EXPECT_TRUE (key.isEmpty()) << "nothing of the transport in the column";
			for (const auto& [a, b] : { std::pair { with.repeat, without.repeat }, { with.sync, without.sync }, { with.vinyl, without.vinyl },
										{ with.gridNudge[0], without.gridNudge[0] }, { with.gridEdit[0], without.gridEdit[0] } })
				EXPECT_GT (b.h, a.h) << sizeName (w, h);
			EXPECT_LT (without.repeat.bottom(), without.sync.y + 1);
			EXPECT_LT (without.sync.bottom(), without.vinyl.y + 1);
			// LOOP OFF went to the foot row's LOOP; its row is the play mode's,
			// REPEAT | CONTINUE, as SYNC | MASTER under it.
			EXPECT_TRUE (without.loopOff.isEmpty()) << sizeName (w, h);
			EXPECT_TRUE (with.continuePlay.isEmpty()) << "the two-deck column has no CONTINUE";
			EXPECT_EQ (without.repeat.x, without.sync.x) << sizeName (w, h);
			EXPECT_EQ (without.repeat.right(), without.sync.right()) << sizeName (w, h);
			EXPECT_EQ (without.continuePlay.x, without.master.x) << sizeName (w, h);
			EXPECT_EQ (without.continuePlay.right(), without.master.right()) << sizeName (w, h);
			EXPECT_EQ (without.continuePlay.y, without.repeat.y) << sizeName (w, h);
			EXPECT_EQ (without.continuePlay.h, without.repeat.h) << sizeName (w, h);
			for (const auto& key : { without.repeat, without.continuePlay, without.sync, without.master, without.vinyl, without.grid })
				EXPECT_TRUE (column.contains (key)) << sizeName (w, h);
			EXPECT_GE (without.vinyl.bottom(), column.h - column.h / 10) << "the keys at the column's foot";
		}
	}
}

//==============================================================================
// The event strip (EventStrip.h). The two screens it must fit: the rig's
// 768 x 1024 and a3motionV02's 600 x 1024.
namespace
{
	const std::vector<std::pair<int, int>> eventScreens { { rigWidth, rigHeight }, { 600, 1024 } };

	std::vector<Rect> stripKeys (const surface::EventStripKeys& k)
	{
		return { k.rec, k.events, k.quantize, k.nudge[0], k.nudge[1], k.nudge[2], k.nudge[3], k.active, k.remove };
	}
}

// Each deck's strip under its tile in the band (its markers are there), above
// the library; nothing it holds overlaps anything else on the surface.
TEST (SurfaceLayout, EventStripsStandUnderTheirDecksTilesAtBothScreens)
{
	for (const auto& [w, h] : eventScreens)
	{
		const auto at = sizeName (w, h);
		const auto s = surface::sections (w, h);
		const auto b = surface::band (s.band, s.metrics);
		for (size_t d = 0; d < 2; ++d)
		{
			const auto& strip = s.events[d];
			EXPECT_EQ (strip.x, b.deck[d].tile.x) << at;
			EXPECT_EQ (strip.w, b.deck[d].tile.w) << at;
			EXPECT_GT (strip.y, s.band.bottom()) << "under the band, " << at;
			EXPECT_LT (strip.bottom(), s.library.y) << "above the library, " << at;
			EXPECT_GT (s.library.h, h / 8) << "the library keeps a share, " << at;
		}
		for (const auto grid : { std::array<bool, 2> { false, false }, { true, true } })
			expectCleanSurface (w, h, grid);
	}
}

TEST (SurfaceLayout, EventStripKeysAreFingerSizedOnBothScreens)
{
	for (const auto& [w, h] : eventScreens)
	{
		const auto s = surface::sections (w, h);
		const auto k = surface::eventStrip (Rect { 0, 0, s.events[0].w, s.events[0].h }, s.metrics, 2);
		for (const auto& key : stripKeys (k))
		{
			EXPECT_GE (key.h, 28) << "as tall as SYNC, " << sizeName (w, h);
			EXPECT_GE (key.w, 40) << sizeName (w, h);
		}
	}
}

// One row (the one-deck screen) and two (the two-deck screen): ten slots
// alike, the nudge keys together, DELETE beside ACTIVE, the note last.
TEST (SurfaceLayout, EventStripRowsKeepTheirOrder)
{
	const auto m = surface::metrics (rigWidth);
	const auto one = surface::eventStrip (Rect { 0, 0, 756, 47 }, m, 1);
	const auto keys = stripKeys (one);
	for (size_t i = 1; i < keys.size(); ++i)
	{
		EXPECT_LT (keys[i - 1].right(), keys[i].x + 1) << "left to right, slot " << i;
		EXPECT_EQ (keys[i].y, keys[0].y);
		EXPECT_LE (std::abs (keys[i].w - keys[0].w), 1) << "one size, slot " << i;
	}
	EXPECT_LT (one.remove.right(), one.note.x + 1) << "the note last";
	EXPECT_EQ (one.rec.h, 47) << "as tall as its row";

	const auto two = surface::eventStrip (Rect { 0, 0, 306, 67 }, m, 2);
	for (const auto& upper : { two.rec, two.events, two.quantize, two.active, two.remove })
		for (const auto& lower : { two.nudge[0], two.nudge[1], two.nudge[2], two.nudge[3], two.note })
		{
			EXPECT_LT (upper.bottom(), lower.y) << "the switches, ACTIVE and DELETE over the nudges";
			EXPECT_FALSE (upper.intersects (lower));
		}
	EXPECT_LT (two.active.right(), two.remove.x + 1) << "DELETE beside ACTIVE";
	for (size_t i = 1; i < two.nudge.size(); ++i)
		EXPECT_LT (two.nudge[i - 1].right(), two.nudge[i].x + 1);
	EXPECT_LT (two.nudge[3].right(), two.note.x + 1);
	EXPECT_EQ (two.remove.right(), two.note.right()) << "the rows end alike";
}
