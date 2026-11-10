// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "StemEvents.h"

#include <juce_core/juce_core.h>

// The events of a set, kept beside its stem files (format v1, JSON).
namespace stemevents
{
	constexpr int fileVersion = 1;

	// "<set's name>.a3events.json" next to the first stem file. The name drops
	// the stem file's extension; for an NI Stems file that is the whole
	// ".stem.mp4", so "Song.stem.mp4" gives "Song.a3events.json".
	juce::File fileFor (const juce::File& firstStemFile);

	enum class Status { none, ok, damaged, newer };

	struct Loaded
	{
		EventList list;
		Status status = Status::none;
	};

	// Never throws. Garbage is damaged, a newer version is newer; both leave
	// the list empty. An event with a missing field, an unknown shape or
	// origin, a bad seed or a stem outside 0..3 is skipped, the rest kept; a
	// bars value that is no positive number reads as 4. The optional "speed" (the
	// shot's tempo step: 0.25, 0.5, 1, 2 or 4) reads as 1 when missing or anything
	// else; save always writes it.
	Loaded load (const juce::File&);

	// Atomic. An empty list deletes the file. Refuses (false) to overwrite a
	// file of a newer version, which a later StemDeck would still understand.
	bool save (const juce::File&, const EventList&);
}
