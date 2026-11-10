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
		// A slot is one stem at one beat; -1 if none.
		int indexAt (int stem, double beat) const;
		const std::vector<Event>& events() const { return list; }

	private:
		bool validIndex (int index) const;
		std::vector<Event>::iterator sortedPosition (const Event&);

		std::vector<Event> list;
	};
}
