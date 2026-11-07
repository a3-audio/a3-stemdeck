// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>

#include "EventBank.h"

#include <atomic>
#include <memory>
#include <thread>

using stemevents::Event;
using stemevents::maxEvents;

namespace
{
	std::vector<Event> eventsAtStem (int stem, int count)
	{
		std::vector<Event> list;
		for (int i = 0; i < count; ++i)
		{
			Event e;
			e.stem = stem;
			e.beat = (double) i;
			list.push_back (e);
		}
		return list;
	}

	// The stress test's lists are a function of their generation, so the reader
	// can tell a whole list from a torn one without being told what was sent.
	int stressCount (std::uint32_t generation)
	{
		return 1 + (int) (generation % (std::uint32_t) maxEvents);
	}

	std::uint64_t stressSeed (std::uint32_t generation, int index)
	{
		return (std::uint64_t) generation * 2654435761u + (std::uint64_t) index;
	}

	std::vector<Event> stressList (std::uint32_t generation)
	{
		std::vector<Event> list;
		for (int i = 0; i < stressCount (generation); ++i)
		{
			Event e;
			e.stem = (int) generation;
			e.beat = (double) i;
			e.seed = stressSeed (generation, i);
			list.push_back (e);
		}
		return list;
	}

	std::uint64_t checksum (const EventBank::View& view)
	{
		std::uint64_t sum = 0;
		for (int i = 0; i < view.count; ++i)
			sum += view.events[i].seed ^ ((std::uint64_t) view.events[i].stem << 32) ^ (std::uint64_t) view.events[i].beat;
		return sum;
	}

	std::uint64_t expectedChecksum (std::uint32_t generation)
	{
		std::uint64_t sum = 0;
		for (int i = 0; i < stressCount (generation); ++i)
			sum += stressSeed (generation, i) ^ ((std::uint64_t) generation << 32) ^ (std::uint64_t) i;
		return sum;
	}
}

TEST (EventBank, AcquireBeforePublishIsEmpty)
{
	auto bank = std::make_unique<EventBank>();
	const auto view = bank->acquire();
	EXPECT_EQ (view.count, 0);
	EXPECT_EQ (view.generation, 0u);
}

TEST (EventBank, AcquireSeesTheLastPublish)
{
	auto bank = std::make_unique<EventBank>();
	bank->publish (eventsAtStem (1, 3));
	bank->publish (eventsAtStem (2, 5));
	const auto view = bank->acquire();
	ASSERT_EQ (view.count, 5);
	for (int i = 0; i < view.count; ++i)
	{
		EXPECT_EQ (view.events[i].stem, 2);
		EXPECT_EQ (view.events[i].beat, (double) i);
	}
}

TEST (EventBank, AcquireWithoutNewPublishKeepsTheList)
{
	auto bank = std::make_unique<EventBank>();
	bank->publish (eventsAtStem (3, 4));
	const auto first = bank->acquire();
	const auto second = bank->acquire();
	EXPECT_EQ (second.events, first.events);
	EXPECT_EQ (second.count, 4);
	EXPECT_EQ (second.generation, first.generation);
}

TEST (EventBank, GenerationRisesOnPublish)
{
	auto bank = std::make_unique<EventBank>();
	bank->publish (eventsAtStem (0, 1));
	const auto first = bank->acquire().generation;
	bank->publish (eventsAtStem (0, 1));
	const auto second = bank->acquire().generation;
	EXPECT_GT (first, 0u);
	EXPECT_GT (second, first);
}

TEST (EventBank, PublishKeepsAtMostMaxEvents)
{
	auto bank = std::make_unique<EventBank>();
	auto list = eventsAtStem (1, maxEvents + 10);
	bank->publish (list);
	const auto view = bank->acquire();
	ASSERT_EQ (view.count, maxEvents);
	EXPECT_EQ (view.events[0].beat, 0.0);
	EXPECT_EQ (view.events[maxEvents - 1].beat, (double) (maxEvents - 1));
}

TEST (EventBank, ViewStaysStableWhilePublishing)
{
	auto bank = std::make_unique<EventBank>();
	bank->publish (eventsAtStem (1, 6));
	const auto view = bank->acquire();
	bank->publish (eventsAtStem (2, 7));
	bank->publish (eventsAtStem (3, 8));
	ASSERT_EQ (view.count, 6);
	for (int i = 0; i < view.count; ++i)
	{
		EXPECT_EQ (view.events[i].stem, 1);
		EXPECT_EQ (view.events[i].beat, (double) i);
	}
	const auto next = bank->acquire();
	ASSERT_EQ (next.count, 8);
	EXPECT_EQ (next.events[0].stem, 3);
}

TEST (EventBank, ReaderNeverSeesATornListUnderAWriter)
{
	constexpr std::uint32_t listCount = 20000;
	auto bank = std::make_unique<EventBank>();
	std::atomic<bool> writerDone { false };

	std::thread writer ([&]
	{
		for (std::uint32_t generation = 1; generation <= listCount; ++generation)
			bank->publish (stressList (generation));
		writerDone.store (true, std::memory_order_release);
	});

	int tornLists = 0;
	int backwardSteps = 0;
	int viewsChecked = 0;
	std::uint32_t lastGeneration = 0;
	auto checkView = [&] (const EventBank::View& view)
	{
		if (view.generation < lastGeneration)
			++backwardSteps;
		lastGeneration = view.generation;
		if (view.generation == 0)
			return;
		++viewsChecked;
		// Read twice: a producer writing into the held slot would change it in between.
		const bool whole = view.count == stressCount (view.generation)
			&& checksum (view) == expectedChecksum (view.generation);
		std::this_thread::yield();
		const bool stillWhole = checksum (view) == expectedChecksum (view.generation);
		if (! whole || ! stillWhole)
			++tornLists;
	};

	while (! writerDone.load (std::memory_order_acquire))
		checkView (bank->acquire());
	writer.join();
	checkView (bank->acquire());

	EXPECT_EQ (tornLists, 0);
	EXPECT_EQ (backwardSteps, 0);
	EXPECT_EQ (lastGeneration, listCount);
	EXPECT_GT (viewsChecked, 0);
}
