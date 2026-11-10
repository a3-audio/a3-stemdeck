// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "DeckMotion.h"

#include <juce_events/juce_events.h>

namespace
{
constexpr int shapeCount = 4;

bool validStem (int stem)
{
	return stem >= 0 && stem < DeckMotion::stems;
}

spatial::Pose homeOf (const DeckSpatial::Controls& settings, size_t stem)
{
	return { settings.azimuth[stem].load (std::memory_order_relaxed), settings.elevation[stem].load (std::memory_order_relaxed),
			 settings.width[stem].load (std::memory_order_relaxed) };
}
}

DeckMotion::DeckMotion() = default;

bool DeckMotion::press (int stem, motionlite::Shape shape, bool shift, std::uint64_t seed)
{
	return push ({ Kind::press, stem, shape, shift, seed });
}

bool DeckMotion::lengthDown (int stem)
{
	if (! push ({ Kind::lengthDown, stem }))
		return false;
	lengthMirror[(size_t) stem] = motionlite::shorterLength (lengthMirror[(size_t) stem]);
	return true;
}

bool DeckMotion::lengthUp (int stem)
{
	if (! push ({ Kind::lengthUp, stem }))
		return false;
	lengthMirror[(size_t) stem] = motionlite::longerLength (lengthMirror[(size_t) stem]);
	return true;
}

bool DeckMotion::setLength (int stem, double bars)
{
	Command command { Kind::setLength, stem };
	command.bars = bars;
	if (! push (command))
		return false;
	lengthMirror[(size_t) stem] = motionlite::validLength (bars);
	return true;
}

bool DeckMotion::setReversed (int stem, bool reversed)
{
	Command command { Kind::setReversed, stem };
	command.reversed = reversed;
	if (! push (command))
		return false;
	reversedMirror[(size_t) stem] = reversed;
	return true;
}

void DeckMotion::setEvents (const std::vector<stemevents::Event>& events)
{
	JUCE_ASSERT_MESSAGE_THREAD   // the bank has one producer
	eventBank.publish (events);
}

// The bank is emptied first, then the generation counted: an audio block that
// sees the new generation acquires the empty list or a newer one.
void DeckMotion::newTrack()
{
	JUCE_ASSERT_MESSAGE_THREAD
	eventBank.publish ({});
	trackGeneration.fetch_add (1);
}

// After setEvents: the release makes the bank's newest list and newTrack's
// generation visible to the block that sees this track ready.
void DeckMotion::trackReady (std::uint32_t track)
{
	JUCE_ASSERT_MESSAGE_THREAD
	readyTrack.store (track, std::memory_order_release);
}

bool DeckMotion::push (const Command& command)
{
	JUCE_ASSERT_MESSAGE_THREAD   // the queue has one producer
	if (! validStem (command.stem))
		return false;
	const auto scope = fifo.write (1);
	if (scope.blockSize1 + scope.blockSize2 != 1)
		return false;
	commands[(size_t) (scope.blockSize1 == 1 ? scope.startIndex1 : scope.startIndex2)] = command;
	return true;
}

void DeckMotion::dropPendingPresses()
{
	drain (false);
}

void DeckMotion::drain (bool presses)
{
	const auto scope = fifo.read (fifo.getNumReady());
	const auto run = [this, presses] (int start, int count)
	{
		for (int i = start; i < start + count; ++i)
		{
			const auto& command = commands[(size_t) i];
			auto& motion = motions[(size_t) command.stem];
			switch (command.kind)
			{
				case Kind::press: if (presses) motion.press (command.shape, command.shift, command.seed); break;
				case Kind::lengthDown: motion.lengthDown(); break;
				case Kind::lengthUp: motion.lengthUp(); break;
				case Kind::setLength: motion.setLengthBars (command.bars); break;
				case Kind::setReversed: motion.setReversed (command.reversed); break;
			}
		}
	};
	run (scope.startIndex1, scope.blockSize1);
	run (scope.startIndex2, scope.blockSize2);
}

void DeckMotion::advance (const motionlite::Clock& clock, double blockSeconds, const DeckSpatial::Controls& settings,
						  DeckSpatial::Controls& played)
{
	for (size_t s = 0; s < (size_t) stems; ++s)
		motions[s].setHome (homeOf (settings, s));
	drain();
	fireEvents (clock, blockSeconds);

	for (size_t s = 0; s < (size_t) stems; ++s)
	{
		const auto frame = motions[s].advance (clock, blockSeconds);
		played.azimuth[s].store (frame.pose.azimuth, std::memory_order_relaxed);
		played.elevation[s].store (frame.pose.elevation, std::memory_order_relaxed);
		played.width[s].store (frame.pose.width, std::memory_order_relaxed);   // RISE narrows it; the other shapes leave it home
		played.envelope[s].store (frame.envelope, std::memory_order_relaxed);
		publish ((int) s, frame.pose);
	}
	hasBeat.store (clock.beat.has_value(), std::memory_order_relaxed);
	lastBeat.store (clock.beat.value_or (0.0), std::memory_order_relaxed);
}

// After the presses, so an event takes over a press of the same block. The
// scheduler runs with EVENTS off too, so its last beat follows the playhead and
// switching on fires nothing that passed meanwhile.
// The ready track is read first: if it matches, everything published before
// trackReady (newTrack's generation, the new list) is seen below. Until it
// matches the scheduler holds: it follows the playhead and keeps what the new
// track plays over for the first block with its events. That held start is the
// new track's, so the generation counted for its list does not reset it.
void DeckMotion::fireEvents (const motionlite::Clock& clock, double blockSeconds)
{
	const bool live = readyTrack.load (std::memory_order_acquire) == clock.track;
	const auto view = eventBank.acquire();
	// After the list: newTrack counts before the new list is published, so a block that sees that list sees its reset too.
	const auto generation = trackGeneration.load();
	if (generation != schedulerGeneration)
	{
		schedulerGeneration = generation;
		if (schedulerLive)
			scheduler.reset();
	}
	schedulerLive = live;

	const double blockBeats = blockSeconds * clock.bpm / 60.0 * clock.rate;
	if (! live)
	{
		scheduler.hold (clock, blockBeats);
		return;
	}
	const int due = scheduler.update (clock, view.events, view.count, dueEvents.data(), (int) dueEvents.size(), blockBeats);
	if (! eventsOn.load (std::memory_order_relaxed))
		return;

	std::array<const stemevents::Event*, stems> lastDue {};
	for (int i = 0; i < due; ++i)
	{
		const auto& event = view.events[dueEvents[(size_t) i]];
		if (validStem (event.stem))
			lastDue[(size_t) event.stem] = &event;
	}
	for (size_t s = 0; s < (size_t) stems; ++s)
		if (const auto* event = lastDue[s])
			motions[s].startEvent (event->shape, event->reversed, event->lengthBars, event->seed);
}

void DeckMotion::publish (int stem, const spatial::Pose& pose)
{
	const auto& motion = motions[(size_t) stem];
	auto& out = published[(size_t) stem];
	for (int shape = 0; shape < shapeCount; ++shape)
		out.pads[(size_t) shape].store ((std::uint8_t) motion.state ((motionlite::Shape) shape), std::memory_order_relaxed);
	out.azimuth.store (pose.azimuth, std::memory_order_relaxed);
	out.elevation.store (pose.elevation, std::memory_order_relaxed);
	out.width.store (pose.width, std::memory_order_relaxed);
	const auto progress = motion.progress();
	out.progressNow.store (progress ? progress->first : 0, std::memory_order_relaxed);
	out.progressTotal.store (progress ? progress->second : 0, std::memory_order_relaxed);
}

motionlite::PadState DeckMotion::padState (int stem, motionlite::Shape shape) const
{
	if (! validStem (stem))
		return motionlite::PadState::idle;
	return (motionlite::PadState) published[(size_t) stem].pads[(size_t) shape].load (std::memory_order_relaxed);
}

motionlite::PadState DeckMotion::stemState (int stem) const
{
	auto state = motionlite::PadState::idle;
	for (int shape = 0; shape < shapeCount; ++shape)
	{
		const auto pad = padState (stem, (motionlite::Shape) shape);
		if (pad == motionlite::PadState::running)
			return pad;
		if (pad == motionlite::PadState::armed)
			state = pad;
	}
	return state;
}

double DeckMotion::lengthBars (int stem) const
{
	return validStem (stem) ? lengthMirror[(size_t) stem].load (std::memory_order_relaxed) : 4.0;
}

bool DeckMotion::reversed (int stem) const
{
	return validStem (stem) && reversedMirror[(size_t) stem].load (std::memory_order_relaxed);
}

spatial::Pose DeckMotion::posePlayed (int stem) const
{
	if (! validStem (stem))
		return {};
	const auto& out = published[(size_t) stem];
	return { out.azimuth.load (std::memory_order_relaxed), out.elevation.load (std::memory_order_relaxed),
			 out.width.load (std::memory_order_relaxed) };
}

std::optional<std::pair<int, int>> DeckMotion::progress (int stem) const
{
	if (! validStem (stem))
		return std::nullopt;
	const auto& out = published[(size_t) stem];
	const int total = out.progressTotal.load (std::memory_order_relaxed);
	if (total <= 0)
		return std::nullopt;
	return std::pair<int, int> { out.progressNow.load (std::memory_order_relaxed), total };
}

std::optional<double> DeckMotion::beat() const
{
	if (! hasBeat.load (std::memory_order_relaxed))
		return std::nullopt;
	return lastBeat.load (std::memory_order_relaxed);
}
