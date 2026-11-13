// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "MotionLite.h"

#include <cstdint>
#include <vector>

// A deck's list of planned one-shot movements, kept sorted by beat, then stem.
// Free of JUCE; lives on the message thread.
namespace stemevents
{
	enum class Origin { hand, prepared };

	struct Event
	{
		int stem = 0;
		double beat = 0.0;
		motionlite::Shape shape = motionlite::Shape::orbit;
		double lengthBars = 4.0;
		bool reversed = false;
		double speed = motionlite::defaultSpeed;   // the shot's tempo step (motionlite::validSpeed)
		std::uint64_t seed = 0;
		bool active = true;
		Origin origin = Origin::hand;
	};

	constexpr int maxEvents = 512;

	// What the clip settings show of an event and change in it: the pads, the
	// length keys and the swipes. The slot (stem, beat), the seed and ACTIVE
	// are the event strip's.
	struct Shot
	{
		motionlite::Shape shape = motionlite::Shape::orbit;
		double lengthBars = 4.0;
		bool reversed = false;
		double speed = motionlite::defaultSpeed;
	};
	bool operator== (const Shot&, const Shot&);

	Shot shotOf (const Event&);

	// The clip settings' keys other than the pads: the length keys, a swipe
	// sideways (direction) and up or down (tempo step). Clamped as the stem's own.
	enum class ShotKey { shorter, longer, reverse, forward, faster, slower };
	Shot afterKey (Shot, ShotKey);

	class EventList
	{
	public:
		enum class Add { added, occupied, full };

		Add add (const Event&);
		bool remove (int index);
		// setActive and nudge are the DJ's edits: the event becomes a hand one,
		// which the preparation tool keeps (prepare_events --replace).
		bool setActive (int index, bool active);
		// False and unchanged if the event would land on an occupied slot.
		bool nudge (int index, double beats);
		// The clip settings' edit: the event's shot replaced; it becomes a hand
		// one. False and unchanged for a bad index, a length or tempo step outside
		// their sets, or a shot that is already the event's.
		bool setShot (int index, const Shot&);
		// A slot is one stem at one beat; -1 if none.
		int indexAt (int stem, double beat) const;
		// The stem's event whose shot holds `beat`: from its start (the
		// scheduler's start tolerance below it) up to, not including, its end
		// (lengthBars bars on); of two, the later start, which takes the stem
		// over. -1 if none. Active or not: the settings show either.
		int indexCovering (int stem, double beat) const;
		const std::vector<Event>& events() const { return list; }

	private:
		bool validIndex (int index) const;
		std::vector<Event>::iterator sortedPosition (const Event&);

		std::vector<Event> list;
	};

	// The event the clip settings show for a stem: while the deck stands, the
	// one at the playhead (indexCovering); playing, or without a grid (a beat
	// not finite), none, and they show the stem's pad defaults.
	int shownInSettings (const EventList&, int stem, bool standing, double playheadBeat);
}
