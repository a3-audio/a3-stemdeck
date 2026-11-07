// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "EventBank.h"

#include <algorithm>

EventBank::EventBank() = default;

void EventBank::publish (const std::vector<stemevents::Event>& events)
{
	Slot& slot = slots[(size_t) producerSlot];
	const int count = std::min ((int) events.size(), stemevents::maxEvents);
	std::copy_n (events.begin(), count, slot.events.begin());
	slot.count = count;
	slot.generation = ++nextGeneration;

	// release: the consumer that takes this slot sees the writes above.
	// acquire: the slot handed back may be one the consumer just let go of;
	// its reads of it must be finished before the next publish writes into it.
	const auto previous = middle.exchange ((std::uint8_t) (producerSlot | freshFlag), std::memory_order_acq_rel);
	producerSlot = previous & indexMask;
}

EventBank::View EventBank::acquire()
{
	// relaxed is enough here: it only decides whether to swap, and the swap itself acquires.
	if ((middle.load (std::memory_order_relaxed) & freshFlag) == 0)
		return viewOf (consumerSlot);

	// acquire: see the producer's writes into the slot taken. release: the reads of
	// the slot given back happen before the producer writes into it again.
	const auto previous = middle.exchange ((std::uint8_t) consumerSlot, std::memory_order_acq_rel);
	consumerSlot = previous & indexMask;
	return viewOf (consumerSlot);
}

EventBank::View EventBank::viewOf (int slotIndex) const
{
	const Slot& slot = slots[(size_t) slotIndex];
	return { slot.events.data(), slot.count, slot.generation };
}
