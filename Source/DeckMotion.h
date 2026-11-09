// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "DeckSpatial.h"
#include "EventBank.h"
#include "EventScheduler.h"
#include "MotionLite.h"

#include <juce_core/juce_core.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

// A deck's four one-shot movers (MotionLite.h) between the call-out and the
// audio thread. The call-out pushes presses and length keys into a lock-free
// single-producer queue; the audio thread drains it once per block, advances
// each stem in the deck's beat time and writes the played pose and 3D
// envelope. The home stays where the call-out and the session keep it (the
// settings); what the audio thread last did is published for the viewer.
// The track's prepared events reach the audio thread through an EventBank;
// each block the scheduler picks those the playhead crosses and they take
// over their stem (StemMotion::startEvent).
class DeckMotion
{
public:
	static constexpr int stems = spatial::stemsPerDeck;
	// Commands the queue holds before the audio thread drains it.
	static constexpr int queueCapacity = 63;

	DeckMotion();

	// Message thread, the one producer. False when the queue is full.
	bool press (int stem, motionlite::Shape, bool shift, std::uint64_t seed);
	bool lengthDown (int stem);
	bool lengthUp (int stem);
	bool setLength (int stem, double bars);   // a restored session; anything outside the valid set is 4
	bool setReversed (int stem, bool reversed);   // the stem's ORBIT (and CROSS) direction, for its presses

	// Message thread. The track's events, sorted by beat (EventList keeps them so).
	void setEvents (const std::vector<stemevents::Event>&);
	// A new track: its events are gone and the scheduler starts afresh at the next
	// block. Call it before the new track's setEvents.
	void newTrack();
	// The events published since newTrack are those of the player's track
	// `track` (Clock::track). Call it after setEvents. The player can change the
	// track on the audio thread (CONTINUE): until the message thread has
	// published that track's events and said so here, no event fires, so the
	// old track's never play on the new one.
	void trackReady (std::uint32_t track);
	// Any thread. Off, no event fires; on again, none of those passed meanwhile does.
	void setEventsEnabled (bool on) { eventsOn.store (on, std::memory_order_relaxed); }
	bool eventsEnabled() const { return eventsOn.load (std::memory_order_relaxed); }

	// The consumer's side, before the first block (DeckSpatialPorts::prepare):
	// presses queued while the audio stood are dropped, length changes kept.
	// Never while advance() can run.
	void dropPendingPresses();

	// Audio thread, once per block: settings give each stem's home and width;
	// played gets the pose, the width and the envelope. Allocation-free.
	void advance (const motionlite::Clock&, double blockSeconds, const DeckSpatial::Controls& settings,
				  DeckSpatial::Controls& played);

	// Any thread. The length is what the message thread last asked for, so a
	// session restored before the first block reads back as it was restored.
	// Everything else here is as of the last advance.
	motionlite::PadState padState (int stem, motionlite::Shape) const;
	motionlite::PadState stemState (int stem) const;   // running, else armed, else idle
	double lengthBars (int stem) const;
	bool reversed (int stem) const;   // as the message thread last asked, like lengthBars
	spatial::Pose posePlayed (int stem) const;
	std::optional<std::pair<int, int>> progress (int stem) const;   // beat now, beats in all
	std::optional<double> beat() const;                              // the deck's beat; none without a grid

private:
	enum class Kind : std::uint8_t { press, lengthDown, lengthUp, setLength, setReversed };
	struct Command
	{
		Kind kind = Kind::press;
		int stem = 0;
		motionlite::Shape shape = motionlite::Shape::orbit;
		bool shift = false;
		std::uint64_t seed = 0;
		double bars = 0.0;
		bool reversed = false;
	};

	struct Published
	{
		std::array<std::atomic<std::uint8_t>, 4> pads {};
		std::atomic<float> azimuth { 0.0f }, elevation { 0.0f }, width { 60.0f };
		std::atomic<int> progressNow { 0 }, progressTotal { 0 };
	};

	static constexpr int queueSize = queueCapacity + 1;   // an AbstractFifo keeps one slot free

	bool push (const Command&);
	void drain (bool presses = true);
	void publish (int stem, const spatial::Pose& played);
	void fireEvents (const motionlite::Clock&);
	std::uint32_t heldSeeks (std::uint32_t seeks);

	juce::AbstractFifo fifo { queueSize };
	std::array<Command, queueSize> commands {};
	std::array<motionlite::StemMotion, stems> motions {};
	std::array<Published, stems> published {};
	std::array<std::atomic<double>, stems> lengthMirror { 4.0, 4.0, 4.0, 4.0 };
	std::array<std::atomic<bool>, stems> reversedMirror { false, false, false, false };

	EventBank eventBank;
	std::atomic<bool> eventsOn { true };
	std::atomic<std::uint32_t> trackGeneration { 0 };   // newTrack() counts, advance() follows
	std::atomic<std::uint32_t> readyTrack { 0 };        // trackReady(), against Clock::track
	// Audio thread only:
	stemevents::EventScheduler scheduler;
	std::array<int, stemevents::maxEvents> dueEvents {};
	std::uint32_t schedulerGeneration = 0;
	std::uint32_t lastPlayerSeeks = 0, reportedSeeks = 0;
	bool seekHeld = false;
	std::atomic<double> lastBeat { 0.0 };
	std::atomic<bool> hasBeat { false };
};
