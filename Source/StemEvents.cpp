// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "StemEvents.h"

#include <algorithm>
#include <cmath>

namespace stemevents
{
	namespace
	{
		// Beats come from float arithmetic on the grid; this is "the same slot".
		constexpr double slotTolerance = 1e-6;

		bool sortsBefore (const Event& a, const Event& b)
		{
			if (a.beat != b.beat)
				return a.beat < b.beat;
			return a.stem < b.stem;
		}
	}

	bool EventList::validIndex (int index) const
	{
		return index >= 0 && index < static_cast<int> (list.size());
	}

	std::vector<Event>::iterator EventList::sortedPosition (const Event& e)
	{
		return std::upper_bound (list.begin(), list.end(), e, sortsBefore);
	}

	int EventList::indexAt (int stem, double beat) const
	{
		for (int i = 0; i < static_cast<int> (list.size()); ++i)
			if (list[i].stem == stem && std::abs (list[i].beat - beat) <= slotTolerance)
				return i;
		return -1;
	}

	EventList::Add EventList::add (const Event& e)
	{
		if (indexAt (e.stem, e.beat) >= 0)
			return Add::occupied;
		if (static_cast<int> (list.size()) >= maxEvents)
			return Add::full;
		list.insert (sortedPosition (e), e);
		return Add::added;
	}

	bool EventList::remove (int index)
	{
		if (! validIndex (index))
			return false;
		list.erase (list.begin() + index);
		return true;
	}

	bool EventList::setActive (int index, bool active)
	{
		if (! validIndex (index))
			return false;
		list[index].active = active;
		list[index].origin = Origin::hand;
		return true;
	}

	bool EventList::nudge (int index, double beats)
	{
		if (! validIndex (index))
			return false;
		Event moved = list[index];
		moved.beat += beats;
		moved.origin = Origin::hand;
		const int target = indexAt (moved.stem, moved.beat);
		if (target >= 0 && target != index)
			return false;
		list.erase (list.begin() + index);
		list.insert (sortedPosition (moved), moved);
		return true;
	}

	bool EventList::setShot (int index, const Shot& shot)
	{
		if (! validIndex (index))
			return false;
		const auto legal = motionlite::validLength (shot.lengthBars) == shot.lengthBars
			&& motionlite::validSpeed (shot.speed) == shot.speed;
		if (! legal || shotOf (list[index]) == shot)
			return false;
		auto& e = list[index];
		e.shape = shot.shape;
		e.lengthBars = shot.lengthBars;
		e.reversed = shot.reversed;
		e.speed = shot.speed;
		e.origin = Origin::hand;
		return true;
	}

	// The list is sorted by beat: the last match is the latest start.
	int EventList::indexCovering (int stem, double beat) const
	{
		int found = -1;
		for (int i = 0; i < static_cast<int> (list.size()); ++i)
		{
			const auto& e = list[i];
			if (e.beat - slotTolerance > beat)
				break;
			const auto end = e.beat + e.lengthBars * motionlite::beatsPerBar;
			if (e.stem == stem && beat < end)
				found = i;
		}
		return found;
	}

	bool operator== (const Shot& a, const Shot& b)
	{
		return a.shape == b.shape && a.lengthBars == b.lengthBars && a.reversed == b.reversed && a.speed == b.speed;
	}

	Shot shotOf (const Event& e)
	{
		return { e.shape, e.lengthBars, e.reversed, e.speed };
	}

	Shot afterKey (Shot shot, ShotKey key)
	{
		switch (key)
		{
			case ShotKey::shorter: shot.lengthBars = motionlite::shorterLength (shot.lengthBars); break;
			case ShotKey::longer:  shot.lengthBars = motionlite::longerLength (shot.lengthBars); break;
			case ShotKey::reverse: shot.reversed = true; break;
			case ShotKey::forward: shot.reversed = false; break;
			case ShotKey::faster:  shot.speed = motionlite::fasterSpeed (shot.speed); break;
			case ShotKey::slower:  shot.speed = motionlite::slowerSpeed (shot.speed); break;
		}
		return shot;
	}

	int shownInSettings (const EventList& events, int stem, bool standing, double playheadBeat)
	{
		if (! standing || ! std::isfinite (playheadBeat))
			return -1;
		return events.indexCovering (stem, playheadBeat);
	}
}
