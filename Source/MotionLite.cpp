// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "MotionLite.h"

#include <algorithm>
#include <cmath>

namespace motionlite
{
	namespace
	{
		constexpr double pi = 3.14159265358979323846;
		constexpr double assumedBpm = 120.0;
		constexpr double minBars = 0.5, maxBars = 8.0, defaultBars = 4.0;
		constexpr double returnBeats = 1.0;
		constexpr float pingSwing = 60.0f, riseTop = 60.0f, scatMaxElevation = 45.0f;

		float wrapDegrees (double degrees)
		{
			double wrapped = std::fmod (degrees + 180.0, 360.0);
			if (wrapped < 0.0)
				wrapped += 360.0;
			// A value a hair below +180 rounds to 180.0f as a float: that is -180.
			const auto azimuth = static_cast<float> (wrapped - 180.0);
			return azimuth >= 180.0f ? -180.0f : azimuth;
		}

		// Counter form of splitmix64: the n-th output of the stream from seed.
		std::uint64_t splitmix (std::uint64_t seed, std::uint64_t n)
		{
			std::uint64_t z = seed + (n + 1) * 0x9E3779B97F4A7C15ull;
			z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
			z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
			return z ^ (z >> 31);
		}

		double unit (std::uint64_t seed, std::uint64_t n)
		{
			return static_cast<double> (splitmix (seed, n) >> 11) * (1.0 / 9007199254740992.0);
		}

		spatial::Pose risePose (const spatial::Pose& home, double t, double length)
		{
			const double fall = std::min (4.0, length / 2.0), riseSpan = length - fall;
			const double r = t < riseSpan ? 0.5 - 0.5 * std::cos (pi * t / riseSpan)
			                              : 0.5 + 0.5 * std::cos (pi * (t - riseSpan) / fall);
			auto pose = home;
			pose.elevation = home.elevation + (riseTop - home.elevation) * static_cast<float> (r);
			return pose;
		}
	}

	spatial::Pose shapePose (Shape shape, const spatial::Pose& home, double t, double lengthBeats, std::uint64_t seed, bool reversed)
	{
		if (t >= lengthBeats || lengthBeats <= 0.0)
			return home;
		t = std::max (t, 0.0);
		const auto beat = static_cast<std::uint64_t> (std::floor (t));
		const bool lastBeat = static_cast<double> (beat) >= lengthBeats - 1.0;
		auto pose = home;

		switch (shape)
		{
			case Shape::orbit:
			{
				const double turn = 360.0 * t / lengthBeats;
				pose.azimuth = wrapDegrees (reversed ? home.azimuth - turn : home.azimuth + turn);
				return pose;
			}
			case Shape::ping:
				if (lastBeat)
					return home;
				pose.azimuth = wrapDegrees (home.azimuth + ((beat % 2 == 0) ? -pingSwing : pingSwing));
				return pose;
			case Shape::rise:
				return risePose (home, t, lengthBeats);
			case Shape::scat:
				if (lastBeat)
					return home;
				pose.azimuth = wrapDegrees (-180.0 + 360.0 * unit (seed, 2 * beat));
				pose.elevation = static_cast<float> (scatMaxElevation * unit (seed, 2 * beat + 1));
				return pose;
		}
		return home;
	}

	float envelope (double t, double lengthBeats)
	{
		return static_cast<float> (std::clamp (std::min (t, lengthBeats - t), 0.0, 1.0));
	}

	void StemMotion::setHome (spatial::Pose newHome) { home = newHome; }

	double validLength (double bars)
	{
		for (double valid = minBars; valid <= maxBars; valid *= 2.0)
			if (std::abs (bars - valid) < 1e-9)
				return bars;
		return defaultBars;
	}

	double shorterLength (double bars) { return std::max (minBars, bars / 2.0); }
	double longerLength (double bars) { return std::min (maxBars, bars * 2.0); }

	void StemMotion::lengthDown() { lengthBarsValue = shorterLength (lengthBarsValue); }
	void StemMotion::lengthUp() { lengthBarsValue = longerLength (lengthBarsValue); }

	void StemMotion::startEvent (Shape shape, bool reversed, double lengthBars, std::uint64_t seed)
	{
		Shot shot;
		shot.shape = shape;
		shot.seed = seed;
		shot.reversed = reversed;
		shot.lengthBeats = validLength (lengthBars) * beatsPerBar;
		running = shot;
		pending.reset();
	}

	void StemMotion::press (Shape shape, bool shift, std::uint64_t seed)
	{
		// The running pad stops; on its way home only SHIFT starts it afresh.
		const bool returning = running && running->shape == shape && running->returning;
		if (running && running->shape == shape && ! (returning && shift))
		{
			if (! returning)
				stopRunning();
			return;
		}
		if (! shift && pending && pending->shape == shape)
		{
			pending.reset();
			return;
		}

		Shot shot;
		shot.shape = shape;
		shot.seed = seed;
		shot.reversed = reversedValue;
		shot.lengthBeats = lengthBarsValue * beatsPerBar;
		if (shift)
		{
			running = shot;
			pending.reset();
			return;
		}
		pending = shot;
	}

	PadState StemMotion::state (Shape shape) const
	{
		if (running && running->shape == shape)
			return PadState::running;
		if (pending && pending->shape == shape)
			return PadState::armed;
		return PadState::idle;
	}

	std::optional<std::pair<int, int>> StemMotion::progress() const
	{
		if (! running)
			return std::nullopt;
		const int total = static_cast<int> (running->lengthBeats);
		const int now = running->returning ? total : static_cast<int> (std::floor (running->t)) + 1;
		return std::pair<int, int> { std::clamp (now, 1, total), total };
	}

	void StemMotion::startPending()
	{
		running = pending;
		pending.reset();
	}

	void StemMotion::stopRunning()
	{
		const auto now = frameNow();
		running->returning = true;
		running->returnT = 0.0;
		running->returnFrom = now.pose;
		running->returnEnvelope = now.envelope;
	}

	Frame StemMotion::frameNow() const
	{
		if (! running)
			return { home, 0.0f };

		if (! running->returning)
			return { shapePose (running->shape, home, running->t, running->lengthBeats, running->seed, running->reversed),
			         envelope (running->t, running->lengthBeats) };

		const auto fraction = static_cast<float> (std::clamp (running->returnT / returnBeats, 0.0, 1.0));
		const auto& from = running->returnFrom;
		spatial::Pose pose = home;
		const float turn = std::remainder (home.azimuth - from.azimuth, 360.0f);
		pose.azimuth = wrapDegrees (from.azimuth + turn * fraction);
		pose.elevation = from.elevation + (home.elevation - from.elevation) * fraction;
		return { pose, running->returnEnvelope * (1.0f - fraction) };
	}

	Frame StemMotion::advance (const Clock& clock, double blockSeconds)
	{
		if (running)
		{
			const bool finished = running->returning ? running->returnT >= returnBeats
			                                         : running->t >= running->lengthBeats;
			if (finished)
				running.reset();
		}

		if (pending)
		{
			const bool barCrossed = clock.beat && lastBeat && *clock.beat >= *lastBeat
				&& std::floor (*clock.beat / beatsPerBar) > std::floor (*lastBeat / beatsPerBar);
			if (! clock.beat || barCrossed)
				startPending();
		}
		lastBeat = clock.beat;

		const auto frame = frameNow();

		if (running && clock.playing)
		{
			const double bpm = clock.beat ? clock.bpm : assumedBpm;
			const double beats = std::max (0.0, blockSeconds * bpm * clock.rate / 60.0);
			(running->returning ? running->returnT : running->t) += beats;
		}
		return frame;
	}
}
