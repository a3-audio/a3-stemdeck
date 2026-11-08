// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "TransportDeck.h"

#include <cmath>

// A deck in plain values: what the transport keys did, read back by the tests.
// Its grid, if any, snaps a loop's ends and the cue point to whole beats,
// as the player does with QUANTIZE on.
struct FakeTransportDeck : TransportDeck
{
	bool loaded = true, playing = false;
	double at = 0.0, cue = 0.0;
	bool looping = false;
	double in = 0.0, out = 0.0;
	double beat = 0.0;   // 0: no grid

	bool isLoaded() const override { return loaded; }
	bool isPlaying() const override { return playing; }
	void play() override { playing = loaded; }
	void pause() override { playing = false; }

	double position() const override { return at; }
	void setPosition (double seconds) override { at = seconds; }
	double cuePoint() const override { return cue; }
	void setCuePoint (double seconds) override { cue = snapped (seconds); }

	bool hasLoop() const override { return looping; }
	double loopStart() const override { return looping ? in : 0.0; }
	double loopEnd() const override { return looping ? out : 0.0; }
	void setLoop (double start, double end) override
	{
		in = snapped (start);
		out = snapped (end);
		if (out <= in)
			out = in + beat;
		looping = true;
		at = in;
	}
	void clearLoop() override { looping = false; }

	double snapped (double seconds) const { return beat > 0.0 ? std::round (seconds / beat) * beat : seconds; }
};
