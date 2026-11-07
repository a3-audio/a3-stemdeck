// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "StemEvents.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

// A deck's events as the audio thread sees them: the message thread publishes
// a copy, the audio thread picks up the newest one once per block. A triple
// buffer: one slot the producer writes, one the consumer reads, one in the
// middle that the two swap with. No locks, no allocation after construction.
// Exactly one producer thread and one consumer thread.
class EventBank
{
public:
	struct View
	{
		const stemevents::Event* events;
		int count;
		std::uint32_t generation;
	};

	EventBank();

	// Message thread. Keeps the first maxEvents events, in the given order.
	void publish (const std::vector<stemevents::Event>&);

	// Audio thread. The newest published list; stays unchanged until the next acquire().
	View acquire();

private:
	struct Slot
	{
		std::array<stemevents::Event, stemevents::maxEvents> events;
		int count = 0;
		std::uint32_t generation = 0;
	};

	static constexpr std::uint8_t indexMask = 0x3;
	static constexpr std::uint8_t freshFlag = 0x4;

	View viewOf (int slotIndex) const;

	std::array<Slot, 3> slots;
	// Index of the middle slot, plus freshFlag while it holds a list the consumer has not taken.
	std::atomic<std::uint8_t> middle { 1 };
	static_assert (std::atomic<std::uint8_t>::is_always_lock_free);
	int producerSlot = 2;          // producer thread only
	int consumerSlot = 0;          // consumer thread only
	std::uint32_t nextGeneration = 0;   // producer thread only
};
