#include "SurfaceLayout.h"

#include <algorithm>
#include <initializer_list>

namespace surface
{
	//==============================================================================
	bool Rect::contains (const Rect& o) const
	{
		return o.x >= x && o.y >= y && o.right() <= right() && o.bottom() <= bottom();
	}

	bool Rect::intersects (const Rect& o) const
	{
		return ! isEmpty() && ! o.isEmpty() && o.x < right() && x < o.right() && o.y < bottom() && y < o.bottom();
	}

	Rect Rect::reduced (int dx, int dy) const
	{
		return { x + dx, y + dy, std::max (0, w - 2 * dx), std::max (0, h - 2 * dy) };
	}

	Rect Rect::withSizeKeepingCentre (int width, int height) const
	{
		return { x + (w - width) / 2, y + (h - height) / 2, width, height };
	}

	Rect Rect::removeFromTop (int amount)
	{
		amount = std::clamp (amount, 0, std::max (0, h));
		const Rect slice { x, y, w, amount };
		y += amount;
		h -= amount;
		return slice;
	}

	Rect Rect::removeFromBottom (int amount)
	{
		amount = std::clamp (amount, 0, std::max (0, h));
		h -= amount;
		return { x, y + h, w, amount };
	}

	Rect Rect::removeFromLeft (int amount)
	{
		amount = std::clamp (amount, 0, std::max (0, w));
		const Rect slice { x, y, amount, h };
		x += amount;
		w -= amount;
		return slice;
	}

	Rect Rect::removeFromRight (int amount)
	{
		amount = std::clamp (amount, 0, std::max (0, w));
		w -= amount;
		return { x + w, y, amount, h };
	}

	//==============================================================================
	namespace
	{
		// A share of a length, in per mille.
		int share (int length, int perMille) { return length * perMille / 1000; }

		// Two keys side by side, a gap between them.
		void splitPair (Rect row, int gap, Rect& left, Rect& right)
		{
			left = row.removeFromLeft ((row.w - gap) / 2);
			row.removeFromLeft (gap);
			right = row;
		}

		// `count` keys of equal width across a row, a hairline apart.
		template <size_t count>
		void splitRow (Rect row, std::array<Rect, count>& keys)
		{
			const auto width = row.w / (int) count;
			for (auto& key : keys)
				key = row.removeFromLeft (width).reduced (hairline (width), 0);
		}

		// A deck column in thousandths of a key: the rows and the gaps between
		// them. The transport's two rows are the tallest keys (PLAY, CUE,
		// NEXT, PREV).
		constexpr int timesUnits = 550, gridUnits = 830, keyUnits = 1000, transportUnits = 1400, gapUnits = 120;
		constexpr int deckUnits = timesUnits + 2 * gridUnits + 3 * keyUnits + 2 * transportUnits
								+ 7 * gapUnits;   // 6 between rows, 1 inside the transport
		// Without the transport (Transport::elsewhere): its two rows, the gap
		// between them and the one above it.
		constexpr int keysOnlyUnits = deckUnits - 2 * transportUnits - 2 * gapUnits;

		// The one-deck foot row, in per mille of its width: the meters' card,
		// the loop block, the track search; CUE | PLAY take the rest.
		constexpr int footMetersShare = 170, loopBlockShare = 190, trackSearchShare = 130;
		// A meter slot: its bar of the slot's width, its caption of the card's height.
		constexpr int meterBarShare = 300, meterCaptionShare = 220;

		// The column's panel stands this far inside its bounds.
		int panelInset (Metrics m) { return m.line * 2 / 3; }

		// The event strips' row, in per mille of the window's height: two rows of
		// keys about as tall as SYNC and VINYL on the rig.
		constexpr int eventRowShare = 66;

		// The band across its width: a tile per deck at the edges and the
		// meters' tile in the middle, a gap, the deck line and a gap between.
		// The event strips under the band stand on the same columns.
		struct BandColumns
		{
			Rect deckA, meters, deckB;
		};

		BandColumns bandColumns (Rect area, Metrics m)
		{
			const auto metersWidth = share (area.w, 150);
			BandColumns c;
			c.meters = { area.x + (area.w - metersWidth) / 2, area.y, metersWidth, area.h };
			const auto offset = 2 * m.gap + m.line;
			c.deckA = { area.x, area.y, c.meters.x - offset - area.x, area.h };
			c.deckB = { c.meters.right() + offset, area.y, area.right() - c.meters.right() - offset, area.h };
			return c;
		}

		// `keys` across a row, of equal width, `gap` apart.
		void splitEvenly (Rect row, int gap, std::initializer_list<Rect*> keys)
		{
			const auto count = (int) keys.size();
			const auto width = (row.w - gap * (count - 1)) / count;
			for (auto* key : keys)
			{
				*key = row.removeFromLeft (width);
				row.removeFromLeft (gap);
			}
		}
	}

	int hairline (int size)
	{
		return std::max (1, size / 24);
	}

	Metrics metrics (int windowWidth)
	{
		return { windowWidth / 128, windowWidth / 256 };
	}

	int deckColumnHeight (int keyHeight, Metrics m)
	{
		// The rows, the margins at top and foot, the transport tile's padding.
		return keyHeight * deckUnits / 1000 + 4 * m.gap + 2 * panelInset (m);
	}

	Sections sections (int width, int height)
	{
		Sections s;
		s.metrics = metrics (width);
		const auto gap = s.metrics.gap;
		auto area = Rect { 0, 0, width, height }.reduced (gap);

		s.topBar = area.removeFromTop (share (height, 30));
		area.removeFromTop (gap);

		// The rolling waveforms, A over B, the whole width.
		const auto waveHeight = height / 9;
		s.waves = area.removeFromTop (waveHeight * 2 + gap / 2);
		auto waves = s.waves;
		s.waveA = waves.removeFromTop (waveHeight);
		waves.removeFromTop (gap / 2);
		s.waveB = waves;
		area.removeFromTop (gap);

		s.library = area.removeFromBottom (share (area.h, 300));
		area.removeFromBottom (gap);
		const auto eventRow = s.library.removeFromTop (share (height, eventRowShare));
		s.library.removeFromTop (gap);
		const auto columns = bandColumns (eventRow, s.metrics);
		s.events = { columns.deckA, columns.deckB };

		// Deck A | mixer | deck B, as tall as a deck's keys need; then the gap
		// the deck lines run in (a gap either side of them); the band takes
		// the rest.
		auto upper = area.removeFromTop (deckColumnHeight (share (height, 30), s.metrics));
		area.removeFromTop (2 * gap + s.metrics.line);
		s.band = area;

		const auto mixerWidth = share (width, 521);
		const auto deckWidth = (upper.w - mixerWidth) / 2;
		s.deck[0] = upper.removeFromLeft (deckWidth);
		s.deck[1] = upper.removeFromRight (deckWidth);
		s.mixer = upper;
		return s;
	}

	namespace
	{
		// One deck's tile, from its outer edge to its inner one (towards the
		// meters); `mirrored` for B, whose outer edge is on the right.
		DeckTile deckTile (Rect tile, Rect metersInner, Metrics m, int pitchWidth, int faderWidth, bool mirrored)
		{
			DeckTile d;
			d.tile = tile;
			const auto gap = m.gap;
			const auto outer = [mirrored] (Rect& r, int w) { return mirrored ? r.removeFromRight (w) : r.removeFromLeft (w); };
			const auto inner = [mirrored] (Rect& r, int w) { return mirrored ? r.removeFromLeft (w) : r.removeFromRight (w); };

			// Level with the meters' tile inside: the fader ends where the bars do.
			auto area = Rect { tile.x, metersInner.y, tile.w, metersInner.h }.reduced (gap, 0);
			const auto line = share (area.h, 150);

			auto pitchColumn = outer (area, pitchWidth);
			outer (area, gap);
			d.pitchValue = pitchColumn.removeFromTop (line);
			d.range = pitchColumn.removeFromBottom (line);
			pitchColumn.removeFromTop (gap);
			pitchColumn.removeFromBottom (gap);
			d.pitch = pitchColumn;

			d.fader = inner (area, faderWidth);
			d.fader.h -= meterCaptionHeight (metersInner.h);
			inner (area, gap);

			d.title = area.removeFromTop (share (tile.h, 110));
			d.stems = area.removeFromTop (share (tile.h, 70));
			area.removeFromTop (gap);
			// KEY (key lock) at the foot beside the range key, before the BPM:
			// the tempo's keys together, and the pitch fader keeps its travel.
			auto foot = area.removeFromBottom (share (tile.h, 200));
			d.keyLock = outer (foot, pitchWidth);
			outer (foot, gap);
			d.bpmInfo = foot.removeFromBottom (share (tile.h, 70));
			d.bpm = foot;
			area.removeFromBottom (gap);
			d.overview = area;
			return d;
		}
	}

	Band band (Rect area, Metrics m)
	{
		Band b;
		const auto gap = m.gap;

		// The meters' tile in the middle; a gap, the line, a gap either side;
		// the decks' tiles to the window's edges.
		const auto columns = bandColumns (area, m);
		b.metersTile = columns.meters;
		b.meters = b.metersTile.reduced (gap);

		const auto pitchWidth = share (area.w, 85);
		const auto faderWidth = share (area.w, 75);
		b.deck[0] = deckTile (columns.deckA, b.meters, m, pitchWidth, faderWidth, false);
		b.deck[1] = deckTile (columns.deckB, b.meters, m, pitchWidth, faderWidth, true);
		return b;
	}

	EventStripKeys eventStrip (Rect local, Metrics m, int rows)
	{
		EventStripKeys k;
		const auto gap = std::max (1, m.gap / 2);
		if (rows <= 1)
		{
			splitEvenly (local, gap, { &k.rec, &k.events, &k.quantize, &k.nudge[0], &k.nudge[1], &k.nudge[2], &k.nudge[3],
									   &k.active, &k.remove, &k.note });
			return k;
		}
		auto upper = local.removeFromTop ((local.h - gap) / 2);
		local.removeFromTop (gap);
		splitEvenly (upper, gap, { &k.rec, &k.events, &k.quantize, &k.active, &k.remove });
		splitEvenly (local, gap, { &k.nudge[0], &k.nudge[1], &k.nudge[2], &k.nudge[3], &k.note });
		return k;
	}

	Rect deckStripe (Rect deck, int deckIndex, Metrics m)
	{
		const auto panel = deck.reduced (panelInset (m));
		return { deckIndex == 0 ? panel.right() - m.line : panel.x, panel.y, m.line, panel.h };
	}

	DeckLine deckLine (const Sections& sections, const Band& band, int deckIndex)
	{
		DeckLine line;
		const auto m = sections.metrics;
		line.stripe = deckStripe (sections.deck[(size_t) deckIndex], deckIndex, m);

		// The top runs a gap above the band's tiles; the side a gap from the
		// deck's tile and from the meters' tile.
		const auto topY = sections.band.y - m.gap - m.line;
		line.drop = { line.stripe.x, line.stripe.bottom(), m.line, topY - line.stripe.bottom() };

		const auto sideX = deckIndex == 0 ? band.metersTile.x - m.gap - m.line : band.metersTile.right() + m.gap;
		line.side = { sideX, topY, m.line, sections.band.bottom() - topY };
		line.top = deckIndex == 0 ? Rect { line.stripe.x, topY, line.side.right() - line.stripe.x, m.line }
								  : Rect { sideX, topY, line.stripe.right() - sideX, m.line };
		return line;
	}

	DeckColumn deckColumn (Rect local, bool gridOn, int deckIndex, Metrics m, Transport transport)
	{
		DeckColumn c;
		const auto gap = m.gap;

		// Inside the panel: a gap from the stripe and from the outer edge.
		auto area = local.reduced (panelInset (m));
		if (deckIndex == 0)
			area.removeFromRight (m.line + gap);
		else
			area.removeFromLeft (m.line + gap);
		area = deckIndex == 0 ? Rect { area.x + gap, area.y, area.w - gap, area.h } : Rect { area.x, area.y, area.w - gap, area.h };
		area = area.reduced (0, gap);

		const auto hasTransport = transport == Transport::inColumn;
		const auto key = hasTransport ? (area.h - 2 * gap) * 1000 / deckUnits : area.h * 1000 / keysOnlyUnits;
		const auto units = [key] (int u) { return key * u / 1000; };
		const auto rowGap = units (gapUnits);

		// At the foot, within the DJ's reach and the biggest keys on the deck,
		// on their own tile: CUE | PLAY, and the track search over them, as
		// large -- a 2 x 2 block gives each key half the tile's width, where a
		// row of four would give a quarter.
		if (hasTransport)
		{
			c.transportTile = area.removeFromBottom (2 * units (transportUnits) + rowGap + 2 * gap);
			auto tile = c.transportTile.reduced (gap);
			splitPair (tile.removeFromBottom (units (transportUnits)), rowGap, c.cue, c.play);
			tile.removeFromBottom (rowGap);
			splitPair (tile, rowGap, c.previous, c.next);
			area.removeFromBottom (rowGap);
		}

		// Above: the keys, which stay where they are whatever GRID does.
		splitPair (area.removeFromBottom (units (keyUnits)), rowGap, c.vinyl, c.grid);
		area.removeFromBottom (rowGap);
		splitPair (area.removeFromBottom (units (keyUnits)), rowGap, c.sync, c.master);
		area.removeFromBottom (rowGap);
		// Without the transport LOOP OFF goes too (the foot row's LOOP does
		// its job), and the row is the play mode's: REPEAT | CONTINUE.
		if (hasTransport)
			splitPair (area.removeFromBottom (units (keyUnits)), rowGap, c.loopOff, c.repeat);
		else
			splitPair (area.removeFromBottom (units (keyUnits)), rowGap, c.repeat, c.continuePlay);
		area.removeFromBottom (rowGap);

		// Grid Adjust above the keys; with GRID off its room is the times'.
		auto times = area;
		splitRow (area.removeFromBottom (units (gridUnits)), c.gridEdit);
		area.removeFromBottom (rowGap);
		splitRow (area.removeFromBottom (units (gridUnits)), c.gridNudge);
		area.removeFromBottom (rowGap);

		if (gridOn)
		{
			times = area;
			c.elapsed = times.removeFromLeft (times.w / 2);
			c.remaining = times;
		}
		else
		{
			c.elapsed = times.removeFromTop (times.h / 2);
			c.remaining = times;
		}
		return c;
	}

	OneDeckFoot oneDeckFoot (Rect row, Metrics m)
	{
		OneDeckFoot foot;
		const auto width = row.w;
		foot.metersTile = row.removeFromRight (share (width, footMetersShare));
		row.removeFromRight (m.gap);

		auto& t = foot.transport;
		t.tile = row;
		auto keys = t.tile.reduced (m.gap);
		const auto halfHeight = (keys.h - m.gap) / 2;

		// Two rows of equal height in each block: over and under.
		auto loopBlock = keys.removeFromLeft (share (width, loopBlockShare));
		keys.removeFromLeft (m.gap);
		t.loop = loopBlock.removeFromTop (halfHeight);
		splitPair (loopBlock.removeFromBottom (halfHeight), m.gap, t.loopIn, t.loopOut);

		auto search = keys.removeFromLeft (share (width, trackSearchShare));
		keys.removeFromLeft (m.gap);
		t.previous = search.removeFromTop (halfHeight);
		t.next = search.removeFromBottom (halfHeight);

		keys.removeFromRight ((keys.w - m.gap) % 2);   // CUE and PLAY alike
		splitPair (keys, m.gap, t.cue, t.play);
		return foot;
	}

	std::array<MeterSlot, 3> oneDeckMeterSlots (Rect tile, Metrics m)
	{
		std::array<MeterSlot, 3> slots;
		auto inside = tile.reduced (m.gap);
		const auto captionHeight = share (tile.h, meterCaptionShare);
		const auto slotWidth = inside.w / (int) slots.size();
		for (auto& slot : slots)
		{
			auto column = inside.removeFromLeft (slotWidth);
			slot.caption = column.removeFromBottom (captionHeight);
			column.removeFromBottom (m.gap / 2);
			slot.bar = column.withSizeKeepingCentre (std::max (3, share (slotWidth, meterBarShare)), column.h);
		}
		return slots;
	}

	Strip channelStrip (Rect local, int deckIndex)
	{
		Strip strip;
		auto area = local.reduced (local.w / 40);
		strip.header = area.removeFromTop (share (local.h, 90));
		const auto gap = area.w / 56;
		area.removeFromTop (gap);

		// n, AUX, CUE, M, knob on deck B, mirrored on deck A: the knob sits
		// on the deck's outer side. Four rows tight
		// under each other: a matrix. The knob a seventh of the row, the four
		// keys share the rest and are as tall as the row, a hairline apart.
		const auto knobSize = area.w / 7;
		const auto keyWidth = (area.w - knobSize) / 4;
		const auto rows = (int) strip.stems.size();
		const auto rowHeight = area.h / rows;

		for (auto& row : strip.stems)
		{
			row.frame = area.removeFromTop (rowHeight);
			auto inner = row.frame;
			const auto keyHeightInset = hairline (rowHeight);

			const auto key = [&] (bool fromLeft)
			{
				auto slot = fromLeft ? inner.removeFromLeft (keyWidth) : inner.removeFromRight (keyWidth);
				return slot.reduced (hairline (keyWidth), keyHeightInset);
			};
			const auto knob = [&] (bool fromLeft)
			{
				auto slot = fromLeft ? inner.removeFromLeft (knobSize) : inner.removeFromRight (knobSize);
				return slot.withSizeKeepingCentre (knobSize, knobSize);
			};

			// Deck B reads left to right; deck A is the same row from the right.
			const auto fromLeft = deckIndex != 0;
			row.channel = key (fromLeft);
			row.aux = key (fromLeft);
			row.cue = key (fromLeft);
			row.mute = key (fromLeft);
			row.knob = knob (fromLeft);
		}
		return strip;
	}

	std::array<Rect, 2> mixerStrips (Rect local)
	{
		const auto pad = local.w / 50;
		auto area = local.reduced (pad);
		const auto gap = 2 * pad;   // the two decks' matrices clearly apart
		const auto stripWidth = (area.w - gap) / 2;
		return { area.removeFromLeft (stripWidth), area.removeFromRight (stripWidth) };
	}

	int meterCaptionHeight (int metersHeight)
	{
		return share (metersHeight, 90);
	}
}
