// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "SpatialLaw.h"

#include <cstdint>
#include <optional>
#include <utility>

// One-shot movements per stem (ORBIT, CROSS, RISE, SCAT), quantised to the next
// bar of the deck's beat time. Free of JUCE and of allocation, so it can run
// in the audio callback.
namespace motionlite
{
	// ping is what the user sees as CROSS; the name stays in code and in the events
	// file ("ping"), which a3-doc and tools/prepare_events.py read and write.
	enum class Shape { orbit, ping, rise, scat };
	enum class PadState { idle, armed, running };

	// beat = BeatGrid::beatsAt (position); nullopt = no grid. seeks and wraps count the
	// playhead's jumps, so the event scheduler can tell a jump from a crossing.
	struct Clock
	{
		std::optional<double> beat;
		double bpm = 120.0;
		double rate = 1.0;
		bool playing = true;
		std::uint32_t seeks = 0;
		std::uint32_t wraps = 0;
		std::optional<std::pair<double, double>> loopBeats;   // in, out
		// Which track the player holds: its hand-over count (StemDeckPlayer::handovers).
		std::uint32_t track = 0;
	};

	struct Frame { spatial::Pose pose; float envelope = 0.0f; };

	constexpr double beatsPerBar = 4.0;

	// RISE narrows the stem as it climbs, on the same curve as the elevation: at the
	// top its width (degrees) is this fraction of the home width, never below
	// riseMinWidth and never wider than home. A wide stem (the WIDTH knob at half
	// travel is 180) closes to a point overhead and opens again on the way down.
	constexpr float riseWidthFraction = 0.2f, riseMinWidth = 10.0f;

	// The tempo of a shot against the beat, a stem's step: x1/4, x1/2, x1, x2, x4.
	// x1 is the shot as it always played. Anything else reads as x1.
	constexpr double defaultSpeed = 1.0;
	double validSpeed (double speed);
	double fasterSpeed (double speed);   // doubled, clamped at x4
	double slowerSpeed (double speed);   // halved, clamped at x1/4

	// t and lengthBeats in beats; after the length the pose is home. reversed turns ORBIT
	// the other way round and runs CROSS's jumps backwards; RISE and SCAT ignore it.
	// speed (validSpeed) is the tempo: ORBIT turns speed laps in its length, CROSS and
	// SCAT hop speed times a beat (x1/2: each hit held two beats); the last beat still
	// goes home. RISE ignores it. Allocation-free; the same seed, t and speed give the same pose.
	spatial::Pose shapePose (Shape, const spatial::Pose& home, double t, double lengthBeats, std::uint64_t seed,
							 bool reversed = false, double speed = defaultSpeed);

	// Rises and falls by one per beat at both ends, capped at 1; a shot shorter
	// than two beats ramps over half its length each way.
	float envelope (double t, double lengthBeats);

	// One-shot lengths in bars: ¼, ½, 1, 2, 4, 8. Anything else reads as the default 4.
	double validLength (double bars);
	double shorterLength (double bars);   // halved, clamped
	double longerLength (double bars);    // doubled, clamped

	class StemMotion
	{
	public:
		void setHome (spatial::Pose);
		void press (Shape, bool shift, std::uint64_t seed);
		// The stem's direction; a press uses it.
		void setReversed (bool newReversed) { reversedValue = newReversed; }
		bool reversed() const { return reversedValue; }
		// The stem's tempo step (validSpeed); a press uses it.
		void setSpeed (double newSpeed);
		double speed() const { return speedValue; }
		// Starts now with its own length, direction and tempo. Never toggles: it replaces the
		// running shot (also of the same shape, also a returning one) and a pending press.
		void startEvent (Shape, bool reversed, double lengthBars, std::uint64_t seed, double speed = defaultSpeed);
		void lengthDown();
		void lengthUp();
		void setLengthBars (double bars) { lengthBarsValue = validLength (bars); }
		double lengthBars() const { return lengthBarsValue; }
		PadState state (Shape) const;
		std::optional<std::pair<int, int>> progress() const;
		Frame advance (const Clock&, double blockSeconds);

	private:
		struct Shot
		{
			Shape shape = Shape::orbit;
			std::uint64_t seed = 0;
			double lengthBeats = 16.0;
			bool reversed = false;
			double speed = defaultSpeed;
			double t = 0.0;
			bool returning = false;
			double returnT = 0.0;
			spatial::Pose returnFrom;
			float returnEnvelope = 0.0f;
		};

		void startPending();
		void stopRunning();
		Frame frameNow() const;

		spatial::Pose home;
		double lengthBarsValue = 4.0;
		bool reversedValue = false;
		double speedValue = defaultSpeed;
		std::optional<Shot> running;
		std::optional<Shot> pending;
		std::optional<double> lastBeat;
	};
}
