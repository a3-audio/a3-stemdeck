// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>

#include "MotionLite.h"

#include <array>
#include <cmath>

using namespace motionlite;

namespace
{
	const spatial::Pose home { 30.0f, 10.0f, 60.0f };

	// A block of one beat at 120 BPM lasts 0.5 s.
	constexpr double beatSeconds = 0.5;

	Clock at (double beat, bool playing = true) { return { beat, 120.0, 1.0, playing }; }
	Clock noGrid() { return { std::nullopt, 120.0, 1.0, true }; }

	StemMotion makeMotion()
	{
		StemMotion motion;
		motion.setHome (home);
		return motion;
	}

	// Presses at 5.0, runs to the bar line at 8.0 and returns the running motion.
	StemMotion started (Shape shape = Shape::orbit)
	{
		auto motion = makeMotion();
		motion.advance (at (5.0), 0.0);
		motion.press (shape, false, 1);
		motion.advance (at (7.9), 0.0);
		motion.advance (at (8.0), 0.0);
		return motion;
	}

	float wrapDiff (float a, float b)
	{
		return std::remainder (a - b, 360.0f);
	}
}

TEST (MotionLite, OrbitTurnsOnceAndEndsHome)
{
	const double L = 16.0;
	EXPECT_NEAR (wrapDiff (shapePose (Shape::orbit, home, L / 4, L, 0).azimuth, home.azimuth + 90.0f), 0.0f, 1e-3f);
	EXPECT_NEAR (wrapDiff (shapePose (Shape::orbit, home, L / 2, L, 0).azimuth, home.azimuth + 180.0f), 0.0f, 1e-3f);
	EXPECT_NEAR (wrapDiff (shapePose (Shape::orbit, home, L, L, 0).azimuth, home.azimuth), 0.0f, 1e-3f);
	EXPECT_FLOAT_EQ (shapePose (Shape::orbit, home, 3.0, L, 0).elevation, home.elevation);
	EXPECT_FLOAT_EQ (shapePose (Shape::orbit, home, 3.0, L, 0).width, home.width);
}

// PING shows as CROSS: each beat it jumps across the room to a point between
// two speakers of the default square rig (+-45, +-135): front, back, left,
// right, each a little off its midpoint by the seed, never near a speaker.
namespace
{
	const std::array<float, 4> squareRigSpeakers { 45.0f, -45.0f, -135.0f, 135.0f };
	const std::array<float, 4> crossForward { 0.0f, 180.0f, 90.0f, -90.0f };    // front, back, left, right
	const std::array<float, 4> crossBackward { 0.0f, -90.0f, 90.0f, 180.0f };   // front, right, left, back
	constexpr float crossJitterMax = 20.0f, speakerClearance = 20.0f;
	constexpr double crossLength = 16.0;

	float crossHit (std::uint64_t seed, int beat, bool reversed = false, const spatial::Pose& from = home)
	{
		return shapePose (Shape::ping, from, beat + 0.4, crossLength, seed, reversed).azimuth;
	}
}

TEST (MotionLite, CrossHitsTheMidpointsInTurnWhateverTheHome)
{
	const spatial::Pose elsewhere { -120.0f, 25.0f, 30.0f };
	for (std::uint64_t seed : { 0ull, 7ull, 123456789ull })
		for (int beat = 0; beat < 15; ++beat)
			for (const auto& from : { home, elsewhere })
			{
				const auto pose = shapePose (Shape::ping, from, beat + 0.4, crossLength, seed);
				EXPECT_LE (std::abs (wrapDiff (pose.azimuth, crossForward[(size_t) beat % 4])), crossJitterMax)
					<< "seed " << seed << " beat " << beat;
				EXPECT_FLOAT_EQ (pose.elevation, from.elevation);
				EXPECT_FLOAT_EQ (pose.width, from.width);
			}
}

TEST (MotionLite, CrossNeverComesNearASpeaker)
{
	for (std::uint64_t seed = 0; seed < 200; ++seed)
		for (int beat = 0; beat < 15; ++beat)
			for (bool reversed : { false, true })
				for (float speaker : squareRigSpeakers)
					EXPECT_GE (std::abs (wrapDiff (crossHit (seed, beat, reversed), speaker)), speakerClearance)
						<< "seed " << seed << " beat " << beat;
}

TEST (MotionLite, CrossJumpsAcrossNeverToANeighbour)
{
	for (std::uint64_t seed = 0; seed < 50; ++seed)
		for (int beat = 0; beat + 1 < 15; ++beat)
			EXPECT_GE (std::abs (wrapDiff (crossHit (seed, beat + 1), crossHit (seed, beat))), 90.0f - 2.0f * crossJitterMax)
				<< "seed " << seed << " beat " << beat;
}

TEST (MotionLite, ReversedCrossRunsTheCycleBackwards)
{
	for (std::uint64_t seed : { 0ull, 7ull, 123456789ull })
		for (int beat = 0; beat < 15; ++beat)
		{
			const auto pose = shapePose (Shape::ping, home, beat + 0.4, crossLength, seed, true);
			EXPECT_LE (std::abs (wrapDiff (pose.azimuth, crossBackward[(size_t) beat % 4])), crossJitterMax)
				<< "seed " << seed << " beat " << beat;
			EXPECT_FLOAT_EQ (pose.elevation, home.elevation);
		}
}

TEST (MotionLite, CrossEndsHome)
{
	for (bool reversed : { false, true })
	{
		EXPECT_FLOAT_EQ (shapePose (Shape::ping, home, crossLength - 1.0, crossLength, 7, reversed).azimuth, home.azimuth);
		EXPECT_FLOAT_EQ (shapePose (Shape::ping, home, crossLength - 0.2, crossLength, 7, reversed).azimuth, home.azimuth);
		EXPECT_FLOAT_EQ (shapePose (Shape::ping, home, crossLength, crossLength, 7, reversed).azimuth, home.azimuth);
	}
}

TEST (MotionLite, CrossIsRepeatableBySeed)
{
	bool differsBySeed = false, differsByPass = false;
	for (int beat = 0; beat < 15; ++beat)
	{
		EXPECT_FLOAT_EQ (shapePose (Shape::ping, home, beat + 0.1, crossLength, 77).azimuth,
		                 shapePose (Shape::ping, home, beat + 0.9, crossLength, 77).azimuth) << "one target per beat";
		EXPECT_FLOAT_EQ (crossHit (77, beat), crossHit (77, beat)) << "the same seed plays the same cross";
		differsBySeed = differsBySeed || crossHit (77, beat) != crossHit (78, beat);
	}
	for (int beat = 0; beat + 4 < 15; ++beat)
		differsByPass = differsByPass || crossHit (77, beat) != crossHit (77, beat + 4);
	EXPECT_TRUE (differsBySeed) << "another seed, another cross";
	EXPECT_TRUE (differsByPass) << "the same point is not hit the same way on every pass";
}

TEST (MotionLite, RiseReachesSixtyAndComesBack)
{
	const double L = 16.0;   // F = 4, rise over [0, 12]
	EXPECT_NEAR (shapePose (Shape::rise, home, 0.0, L, 0).elevation, home.elevation, 1e-4f);
	EXPECT_NEAR (shapePose (Shape::rise, home, 6.0, L, 0).elevation, home.elevation + 0.5f * (60.0f - home.elevation), 1e-3f);
	EXPECT_NEAR (shapePose (Shape::rise, home, 12.0, L, 0).elevation, 60.0f, 1e-4f);
	EXPECT_NEAR (shapePose (Shape::rise, home, 14.0, L, 0).elevation, home.elevation + 0.5f * (60.0f - home.elevation), 1e-3f);
	EXPECT_NEAR (shapePose (Shape::rise, home, L, L, 0).elevation, home.elevation, 1e-4f);

	const double half = 2.0;   // F = 1, rise over [0, 1]
	EXPECT_NEAR (shapePose (Shape::rise, home, 1.0, half, 0).elevation, 60.0f, 1e-4f);
	EXPECT_NEAR (shapePose (Shape::rise, home, half, half, 0).elevation, home.elevation, 1e-4f);
	EXPECT_FLOAT_EQ (shapePose (Shape::rise, home, 5.0, L, 0).azimuth, home.azimuth);
}

TEST (MotionLite, ScatIsRepeatableBySeed)
{
	const double L = 16.0;
	for (double t = 0.0; t < L - 1.0; t += 1.0)
	{
		const auto a = shapePose (Shape::scat, home, t + 0.3, L, 77);
		const auto b = shapePose (Shape::scat, home, t + 0.9, L, 77);
		EXPECT_FLOAT_EQ (a.azimuth, b.azimuth);   // one value per beat
		EXPECT_FLOAT_EQ (a.elevation, b.elevation);
		EXPECT_GE (a.azimuth, -180.0f);
		EXPECT_LT (a.azimuth, 180.0f);
		EXPECT_GE (a.elevation, 0.0f);
		EXPECT_LE (a.elevation, 45.0f);
	}
	bool differs = false;
	for (double t = 0.0; t < L - 1.0; t += 1.0)
		differs = differs || shapePose (Shape::scat, home, t, L, 77).azimuth != shapePose (Shape::scat, home, t, L, 78).azimuth;
	EXPECT_TRUE (differs);
	EXPECT_NE (shapePose (Shape::scat, home, 0.0, L, 77).azimuth, shapePose (Shape::scat, home, 1.0, L, 77).azimuth);

	const auto last = shapePose (Shape::scat, home, L - 0.5, L, 77);
	EXPECT_FLOAT_EQ (last.azimuth, home.azimuth);
	EXPECT_FLOAT_EQ (last.elevation, home.elevation);
}

TEST (MotionLite, EnvelopeRisesAndFallsInOneBeat)
{
	EXPECT_FLOAT_EQ (envelope (0.0, 2.0), 0.0f);
	EXPECT_FLOAT_EQ (envelope (0.5, 2.0), 0.5f);
	EXPECT_FLOAT_EQ (envelope (1.0, 2.0), 1.0f);
	EXPECT_FLOAT_EQ (envelope (1.5, 2.0), 0.5f);
	EXPECT_FLOAT_EQ (envelope (2.0, 2.0), 0.0f);
	EXPECT_FLOAT_EQ (envelope (8.0, 16.0), 1.0f);
	EXPECT_FLOAT_EQ (envelope (-1.0, 16.0), 0.0f);
	EXPECT_FLOAT_EQ (envelope (20.0, 16.0), 0.0f);
}

TEST (MotionLite, ArmedStartsOnTheNextBarOnly)
{
	auto motion = makeMotion();
	motion.advance (at (5.0), 0.0);
	motion.press (Shape::orbit, false, 1);
	EXPECT_EQ (motion.state (Shape::orbit), PadState::armed);
	motion.advance (at (6.0), 0.0);
	motion.advance (at (7.9), 0.0);
	EXPECT_EQ (motion.state (Shape::orbit), PadState::armed);
	EXPECT_FALSE (motion.progress().has_value());
	motion.advance (at (8.0), 0.0);
	EXPECT_EQ (motion.state (Shape::orbit), PadState::running);
	EXPECT_EQ (motion.progress(), (std::pair<int, int> { 1, 16 }));
}

TEST (MotionLite, ABackwardJumpStartsNothing)
{
	auto motion = makeMotion();
	motion.advance (at (9.0), 0.0);
	motion.press (Shape::orbit, false, 1);
	motion.advance (at (3.9), 0.0);   // jump back over the bar line at 8
	EXPECT_EQ (motion.state (Shape::orbit), PadState::armed);
	motion.advance (at (4.0), 0.0);
	EXPECT_EQ (motion.state (Shape::orbit), PadState::running);
}

TEST (MotionLite, ShiftStartsNow)
{
	auto motion = makeMotion();
	motion.advance (at (5.0), 0.0);
	motion.press (Shape::rise, true, 1);
	EXPECT_EQ (motion.state (Shape::rise), PadState::running);
	const auto frame = motion.advance (at (5.0), 0.0);
	EXPECT_NEAR (frame.pose.elevation, home.elevation, 1e-4f);
	motion.advance (at (5.5), 3.0 * beatSeconds);   // 3 beats pass after this block
	const auto later = motion.advance (at (7.0), 0.0);
	EXPECT_GT (later.pose.elevation, home.elevation);
}

TEST (MotionLite, NoGridStartsAtOnceAt120Bpm)
{
	auto motion = makeMotion();
	motion.press (Shape::orbit, false, 1);
	motion.advance (noGrid(), 0.0);
	EXPECT_EQ (motion.state (Shape::orbit), PadState::running);
	// 16 beats at 120 BPM = 8 s; after 2 s the orbit is a quarter round.
	for (int i = 0; i < 4; ++i)
		motion.advance (noGrid(), 0.5);
	const auto frame = motion.advance (noGrid(), 0.0);
	EXPECT_NEAR (wrapDiff (frame.pose.azimuth, home.azimuth + 90.0f), 0.0f, 1e-2f);
}

TEST (MotionLite, PausedFreezesARunningShot)
{
	auto motion = started();
	motion.advance (at (8.0, false), 10.0);
	motion.advance (at (8.0, false), 10.0);
	EXPECT_EQ (motion.progress(), (std::pair<int, int> { 1, 16 }));
	EXPECT_EQ (motion.state (Shape::orbit), PadState::running);
	motion.advance (at (8.0), 2.0);   // 4 beats
	EXPECT_EQ (motion.progress(), (std::pair<int, int> { 5, 16 }));
}

TEST (MotionLite, TheRateScalesTheShotsClock)
{
	auto motion = started();
	motion.advance ({ 8.0, 120.0, 0.5, true }, 4.0);   // 4 s * 2 beats/s * 0.5 = 4 beats
	EXPECT_EQ (motion.progress(), (std::pair<int, int> { 5, 16 }));
}

TEST (MotionLite, TheShotEndsAndGoesBackToIdle)
{
	auto motion = started();
	motion.advance (at (8.0), 8.0);   // exactly 16 beats
	const auto frame = motion.advance (at (24.0), 0.0);
	EXPECT_EQ (motion.state (Shape::orbit), PadState::idle);
	EXPECT_FLOAT_EQ (frame.pose.azimuth, home.azimuth);
	EXPECT_FLOAT_EQ (frame.envelope, 0.0f);
}

TEST (MotionLite, PressingTheRunningPadReturnsHomeInOneBeat)
{
	auto motion = started (Shape::orbit);
	motion.advance (at (8.0), 2.0);   // 4 beats in: az = home + 90
	motion.press (Shape::orbit, false, 2);
	EXPECT_EQ (motion.state (Shape::orbit), PadState::running);
	const auto first = motion.advance (at (12.0), 0.25);   // half a beat
	EXPECT_NEAR (wrapDiff (first.pose.azimuth, home.azimuth + 90.0f), 0.0f, 1e-2f);
	const auto mid = motion.advance (at (12.5), 0.25);
	EXPECT_NEAR (wrapDiff (mid.pose.azimuth, home.azimuth + 45.0f), 0.0f, 1e-2f);
	EXPECT_NEAR (mid.envelope, 0.5f, 1e-4f);
	const auto done = motion.advance (at (13.0), 0.0);
	EXPECT_EQ (motion.state (Shape::orbit), PadState::idle);
	EXPECT_NEAR (wrapDiff (done.pose.azimuth, home.azimuth), 0.0f, 1e-3f);
	EXPECT_FLOAT_EQ (done.envelope, 0.0f);
}

TEST (MotionLite, AnotherPadReplacesAtTheNextBar)
{
	auto motion = started (Shape::orbit);
	motion.advance (at (8.0), 1.0);   // beat 2 of the orbit
	motion.press (Shape::ping, false, 3);
	EXPECT_EQ (motion.state (Shape::orbit), PadState::running);
	EXPECT_EQ (motion.state (Shape::ping), PadState::armed);
	motion.advance (at (9.0), 0.0);
	motion.advance (at (11.9), 0.0);
	EXPECT_EQ (motion.state (Shape::ping), PadState::armed);
	motion.advance (at (12.0), 0.0);
	EXPECT_EQ (motion.state (Shape::orbit), PadState::idle);
	EXPECT_EQ (motion.state (Shape::ping), PadState::running);
}

TEST (MotionLite, ShiftWithAnotherPadReplacesNow)
{
	auto motion = started (Shape::orbit);
	motion.advance (at (8.0), 1.0);
	motion.press (Shape::ping, true, 3);
	EXPECT_EQ (motion.state (Shape::orbit), PadState::idle);
	EXPECT_EQ (motion.state (Shape::ping), PadState::running);
	EXPECT_EQ (motion.progress(), (std::pair<int, int> { 1, 16 }));
}

TEST (MotionLite, PressingAnArmedPadDisarms)
{
	auto motion = makeMotion();
	motion.advance (at (5.0), 0.0);
	motion.press (Shape::scat, false, 1);
	motion.press (Shape::scat, false, 1);
	EXPECT_EQ (motion.state (Shape::scat), PadState::idle);
	motion.advance (at (8.0), 0.0);
	EXPECT_EQ (motion.state (Shape::scat), PadState::idle);
}

TEST (MotionLite, LengthClampsAndAppliesToTheNextTrigger)
{
	StemMotion motion;
	EXPECT_DOUBLE_EQ (motion.lengthBars(), 4.0);
	motion.lengthUp();
	EXPECT_DOUBLE_EQ (motion.lengthBars(), 8.0);
	motion.lengthUp();
	EXPECT_DOUBLE_EQ (motion.lengthBars(), 8.0);
	for (int i = 0; i < 6; ++i)
		motion.lengthDown();
	EXPECT_DOUBLE_EQ (motion.lengthBars(), 0.5);

	auto running = started();
	running.lengthDown();
	EXPECT_EQ (running.progress(), (std::pair<int, int> { 1, 16 }));   // a running shot keeps its length
	running.press (Shape::orbit, false, 1);                            // stop
	running.advance (at (9.0), 2.0);
	running.press (Shape::rise, true, 1);
	EXPECT_EQ (running.progress(), (std::pair<int, int> { 1, 8 }));   // 2 bars next time
}

TEST (MotionLite, ThreeDRisesToTheDecksAndGoesBackToSteady)
{
	auto motion = started (Shape::rise);
	motion.advance (at (8.0), 4.0);   // 8 beats in after this block
	const auto mid = motion.advance (at (16.0), 4.0);
	EXPECT_FLOAT_EQ (mid.envelope, 1.0f);
	EXPECT_FLOAT_EQ (spatial::effectiveThreeD (0.8f, mid.envelope), 0.8f);
	const auto after = motion.advance (at (24.0), 0.0);
	EXPECT_FLOAT_EQ (after.envelope, 0.0f);
	EXPECT_EQ (spatial::effectiveThreeD (0.8f, after.envelope), 0.0f);
}

TEST (MotionLite, SetHomeMovesAnIdleStemAtOnceAndARunningShotFollows)
{
	auto motion = makeMotion();
	motion.setHome ({ -90.0f, 0.0f, 40.0f });
	EXPECT_FLOAT_EQ (motion.advance (at (1.0), 0.0).pose.azimuth, -90.0f);
	motion.press (Shape::rise, true, 1);
	motion.setHome ({ 10.0f, 20.0f, 40.0f });
	EXPECT_FLOAT_EQ (motion.advance (at (1.0), 0.0).pose.elevation, 20.0f);
}

TEST (MotionLite, AShotStartedByASeekWhilePausedStaysFrozen)
{
	auto motion = makeMotion();
	motion.advance (at (5.0, false), 0.0);
	motion.press (Shape::orbit, false, 1);
	motion.advance (at (9.5, false), 0.0);   // a forward seek over the bar line at 8
	EXPECT_EQ (motion.state (Shape::orbit), PadState::running);
	motion.advance (at (9.5, false), 10.0);
	motion.advance (at (9.5, false), 10.0);
	EXPECT_EQ (motion.progress(), (std::pair<int, int> { 1, 16 }));
	EXPECT_EQ (motion.state (Shape::orbit), PadState::running);
}

TEST (MotionLite, SetLengthTakesOnlyTheValidSet)
{
	StemMotion motion;
	for (const double bars : { 0.5, 1.0, 2.0, 4.0, 8.0 })
	{
		motion.setLengthBars (bars);
		EXPECT_DOUBLE_EQ (motion.lengthBars(), bars);
	}
	motion.setLengthBars (2.0);
	motion.setLengthBars (3.0);
	EXPECT_DOUBLE_EQ (motion.lengthBars(), 4.0);
	motion.setLengthBars (1.0);
	motion.setLengthBars (std::nan (""));
	EXPECT_DOUBLE_EQ (motion.lengthBars(), 4.0);
}

TEST (MotionLite, ALengthChangeAfterThePressLeavesTheArmedShotAlone)
{
	auto motion = makeMotion();
	motion.advance (at (5.0), 0.0);
	motion.press (Shape::orbit, false, 1);
	motion.lengthDown();
	motion.advance (at (8.0), 0.0);
	EXPECT_EQ (motion.progress(), (std::pair<int, int> { 1, 16 }));
	EXPECT_DOUBLE_EQ (motion.lengthBars(), 2.0);
}

TEST (MotionLite, ARepeatPressOnTheReturningPadIsIgnored)
{
	auto motion = started (Shape::orbit);
	motion.advance (at (8.0), 2.0);
	motion.press (Shape::orbit, false, 2);   // stop: return
	motion.advance (at (12.0), 0.25);
	motion.press (Shape::orbit, false, 3);   // again, no SHIFT
	EXPECT_EQ (motion.state (Shape::orbit), PadState::running);
	EXPECT_EQ (motion.progress(), (std::pair<int, int> { 16, 16 }));
	motion.advance (at (12.5), 0.25);
	motion.advance (at (13.0), 0.0);
	EXPECT_EQ (motion.state (Shape::orbit), PadState::idle) << "the return still ends one beat after the stop";
}

TEST (MotionLite, ShiftOnTheReturningPadStartsAFreshShotNow)
{
	auto motion = started (Shape::orbit);
	motion.advance (at (8.0), 2.0);
	motion.press (Shape::orbit, false, 2);   // stop: return
	motion.advance (at (12.0), 0.25);
	motion.press (Shape::orbit, true, 3);
	EXPECT_EQ (motion.state (Shape::orbit), PadState::running);
	EXPECT_EQ (motion.progress(), (std::pair<int, int> { 1, 16 }));
	motion.advance (at (12.5), 1.0);   // two beats: a return would have ended
	motion.advance (at (14.5), 0.0);
	EXPECT_EQ (motion.state (Shape::orbit), PadState::running);
	EXPECT_EQ (motion.progress(), (std::pair<int, int> { 3, 16 }));
}

// Float rounding next to the seam must land on -180, never on 180.
TEST (MotionLite, AnAzimuthIsNeverPlus180)
{
	const double L = 16.0;
	const spatial::Pose edge { std::nextafter (180.0f, 0.0f), 0.0f, 60.0f };
	const auto pose = shapePose (Shape::orbit, edge, 1.0e-5 * L / 360.0, L, 0);
	EXPECT_LT (pose.azimuth, 180.0f);
	EXPECT_GE (pose.azimuth, -180.0f);

	for (std::uint64_t seed = 0; seed < 20000; ++seed)
	{
		const auto scat = shapePose (Shape::scat, home, 0.5, L, seed).azimuth;
		ASSERT_LT (scat, 180.0f) << seed;
		ASSERT_GE (scat, -180.0f) << seed;
	}
}

TEST (MotionLite, ReversedOrbitMirrorsTheCircle)
{
	const double L = 16.0;
	for (double t : { 2.0, 4.0, 9.5 })
	{
		const auto pose = shapePose (Shape::orbit, home, t, L, 0, true);
		EXPECT_NEAR (wrapDiff (pose.azimuth, home.azimuth - static_cast<float> (360.0 * t / L)), 0.0f, 1e-3f);
		EXPECT_FLOAT_EQ (pose.elevation, home.elevation);
	}
}

TEST (MotionLite, ReversedIsIgnoredByRiseScat)
{
	for (auto shape : { Shape::rise, Shape::scat })
		for (double t : { 0.2, 1.7, 3.3 })
		{
			const auto forward = shapePose (shape, home, t, 8.0, 5, false);
			const auto backward = shapePose (shape, home, t, 8.0, 5, true);
			EXPECT_FLOAT_EQ (forward.azimuth, backward.azimuth);
			EXPECT_FLOAT_EQ (forward.elevation, backward.elevation);
		}
}

TEST (MotionLite, APressUsesTheStemsDirection)
{
	auto motion = makeMotion();
	motion.setReversed (true);
	EXPECT_TRUE (motion.reversed());
	motion.advance (at (5.0), 0.0);
	motion.press (Shape::orbit, true, 1);
	motion.advance (at (5.0), 0.0);
	const auto frame = motion.advance (at (5.0), 2 * beatSeconds);   // t = 2 beats of 16
	EXPECT_NEAR (wrapDiff (frame.pose.azimuth, home.azimuth), 0.0f, 1e-3f);
	const auto later = motion.advance (at (5.0), 0.0);
	EXPECT_NEAR (wrapDiff (later.pose.azimuth, home.azimuth - 45.0f), 0.0f, 1e-3f);
}

TEST (MotionLite, StartEventStartsAtOnceWithoutAPress)
{
	auto motion = makeMotion();
	motion.advance (at (5.0), 0.0);
	motion.startEvent (Shape::ping, false, 2.0, 1);
	motion.advance (at (5.0), 0.0);
	EXPECT_EQ (motion.state (Shape::ping), PadState::running);
}

TEST (MotionLite, StartEventNeverStopsARunningSameShape)
{
	auto motion = started (Shape::orbit);
	motion.advance (at (8.0), 4 * beatSeconds);   // 4 beats in
	motion.startEvent (Shape::orbit, false, 4.0, 1);
	EXPECT_EQ (motion.state (Shape::orbit), PadState::running);
	const auto frame = motion.advance (at (8.0), 0.0);
	EXPECT_NEAR (wrapDiff (frame.pose.azimuth, home.azimuth), 0.0f, 1e-3f);   // restarted at t = 0
	EXPECT_EQ (motion.progress()->first, 1);
}

TEST (MotionLite, StartEventReplacesAPendingPress)
{
	auto motion = makeMotion();
	motion.advance (at (5.0), 0.0);
	motion.press (Shape::scat, false, 1);
	EXPECT_EQ (motion.state (Shape::scat), PadState::armed);
	motion.startEvent (Shape::rise, false, 1.0, 1);
	EXPECT_EQ (motion.state (Shape::scat), PadState::idle);
	motion.advance (at (7.9), 0.0);
	motion.advance (at (8.0), 0.0);
	EXPECT_EQ (motion.state (Shape::scat), PadState::idle);
	EXPECT_EQ (motion.state (Shape::rise), PadState::running);
}

TEST (MotionLite, StartEventUsesItsOwnLengthAndDirection)
{
	auto motion = makeMotion();
	motion.setLengthBars (8.0);
	motion.setReversed (false);
	motion.advance (at (5.0), 0.0);
	motion.startEvent (Shape::orbit, true, 1.0, 1);   // 4 beats
	EXPECT_EQ (motion.progress()->second, 4);
	motion.advance (at (5.0), 0.0);
	motion.advance (at (5.0), beatSeconds);   // 1 beat of 4
	const auto frame = motion.advance (at (5.0), 0.0);
	EXPECT_NEAR (wrapDiff (frame.pose.azimuth, home.azimuth - 90.0f), 0.0f, 1e-3f);
	EXPECT_DOUBLE_EQ (motion.lengthBars(), 8.0);
	EXPECT_FALSE (motion.reversed());
}

// Reversed, the orbit runs to negative turns: next to the seam it wraps to
// -180 .. 180 and never reads +180 itself.
TEST (MotionLite, AReversedOrbitWrapsAtTheSeamAndNeverReadsPlus180)
{
	const double L = 16.0;
	for (float homeAzimuth : { 0.0f, -180.0f, std::nextafter (-180.0f, 0.0f) })
	{
		const spatial::Pose start { homeAzimuth, 0.0f, 60.0f };
		for (double t : { 1.0e-9, 1.0e-5, 0.01, L / 2.0, L / 2.0 + 1.0e-9 })
		{
			const auto azimuth = shapePose (Shape::orbit, start, t, L, 0, true).azimuth;
			ASSERT_LT (azimuth, 180.0f) << homeAzimuth << " at " << t;
			ASSERT_GE (azimuth, -180.0f) << homeAzimuth << " at " << t;
			EXPECT_NEAR (wrapDiff (azimuth, homeAzimuth - static_cast<float> (360.0 * t / L)), 0.0f, 1e-3f)
				<< homeAzimuth << " at " << t;
		}
	}
	EXPECT_LT (shapePose (Shape::orbit, home, 1.0e-3, L, 0, true).azimuth, home.azimuth) << "turning the other way";
}

TEST (MotionLite, StartEventReplacesAReturningShotWithAFreshStart)
{
	auto motion = started (Shape::orbit);
	motion.advance (at (8.0), 2.0);
	motion.press (Shape::orbit, false, 2);   // stop: return
	motion.advance (at (12.0), 0.25);
	motion.startEvent (Shape::orbit, false, 4.0, 3);
	EXPECT_EQ (motion.state (Shape::orbit), PadState::running);
	EXPECT_EQ (motion.progress(), (std::pair<int, int> { 1, 16 }));
	motion.advance (at (12.5), 1.0);   // two beats: a return would have ended
	motion.advance (at (14.5), 0.0);
	EXPECT_EQ (motion.state (Shape::orbit), PadState::running);
	EXPECT_EQ (motion.progress(), (std::pair<int, int> { 3, 16 }));
}

// The direction is taken at the press: turning the stem round while the
// press waits for its bar leaves that shot as it was.
TEST (MotionLite, AnArmedPressKeepsTheDirectionItWasPressedWith)
{
	auto motion = makeMotion();
	motion.setReversed (false);
	motion.advance (at (5.0), 0.0);
	motion.press (Shape::orbit, false, 1);
	motion.setReversed (true);
	motion.advance (at (7.9), 0.0);
	motion.advance (at (8.0), 0.0);
	ASSERT_EQ (motion.state (Shape::orbit), PadState::running);
	motion.advance (at (8.0), 2 * beatSeconds);   // t = 2 beats of 16
	const auto frame = motion.advance (at (8.0), 0.0);
	EXPECT_NEAR (wrapDiff (frame.pose.azimuth, home.azimuth + 45.0f), 0.0f, 1e-3f) << "forward, as pressed";
}
