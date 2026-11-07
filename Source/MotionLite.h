// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "SpatialLaw.h"

#include <cstdint>
#include <optional>
#include <utility>

// One-shot movements per stem (ORBIT, PING, RISE, SCAT), quantised to the next
// bar of the deck's beat time. Free of JUCE and of allocation, so it can run
// in the audio callback.
namespace motionlite
{
	enum class Shape { orbit, ping, rise, scat };
	enum class PadState { idle, armed, running };

	// beat = BeatGrid::beatsAt (position); nullopt = no grid.
	struct Clock { std::optional<double> beat; double bpm = 120.0; double rate = 1.0; bool playing = true; };

	struct Frame { spatial::Pose pose; float envelope = 0.0f; };

	constexpr double beatsPerBar = 4.0;

	// t and lengthBeats in beats; after the length the pose is home. reversed turns ORBIT
	// the other way round; the other shapes ignore it.
	spatial::Pose shapePose (Shape, const spatial::Pose& home, double t, double lengthBeats, std::uint64_t seed, bool reversed = false);

	// Rises and falls by one per beat at both ends, capped at 1.
	float envelope (double t, double lengthBeats);

	// One-shot lengths in bars: ½, 1, 2, 4, 8. Anything else reads as the default 4.
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
		// Starts now with its own length and direction. Never toggles: it replaces the
		// running shot (also of the same shape, also a returning one) and a pending press.
		void startEvent (Shape, bool reversed, double lengthBars, std::uint64_t seed);
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
		std::optional<Shot> running;
		std::optional<Shot> pending;
		std::optional<double> lastBeat;
	};
}
