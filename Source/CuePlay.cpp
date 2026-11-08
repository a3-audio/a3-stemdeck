// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "CuePlay.h"

#include "CueLight.h"

#include <cmath>

void CuePlay::cueDown (TransportDeck& deck)
{
	if (held)
		return;
	held = true;

	if (! deck.isLoaded())
		return;

	if (deck.isPlaying())
	{
		deck.pause();
		deck.setPosition (deck.cuePoint());
		return;
	}
	if (std::abs (deck.position() - deck.cuePoint()) > cuelight::onCueTolerance)
	{
		// With QUANTIZE the cue lands on the beat; the playhead goes with it, as on a
		// CDJ, so the next CUE previews from there instead of setting it again.
		deck.setCuePoint (deck.position());
		deck.setPosition (deck.cuePoint());
		return;
	}
	previewing = true;
	deck.play();
}

void CuePlay::cueUp (TransportDeck& deck)
{
	if (! held)
		return;
	held = false;

	if (! previewing)
		return;
	previewing = false;
	deck.pause();
	deck.setPosition (deck.cuePoint());
}

void CuePlay::playPressed (TransportDeck& deck)
{
	// The preview becomes the playing: the release of CUE no longer returns.
	if (previewing)
	{
		previewing = false;
		return;
	}
	if (deck.isPlaying())
		deck.pause();
	else
		deck.play();
}
