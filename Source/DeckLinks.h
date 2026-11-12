// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <juce_core/juce_core.h>
#include "Outputs.h"
#include <a3-links/LinkMap.h>

#include <optional>
#include <string>
#include <vector>

// Which JACK links a one-deck instance makes itself (plan ruling R8), in the
// engine's io format and with the engine's link code (a3::links).
namespace decklinks
{
	std::vector<std::string> ownOutputs (outputs::OutputMode = outputs::OutputMode::bformat);   // the 43 one-deck ports, or the stereo four
	std::vector<std::string> ownInputs();    // rec_L, rec_R

	// P39: rec_L/R from the engine's REC bus (a3-engine:rec_L/R), the recorder's
	// links on the Core, where no STEMDECK_LINK_MAP is set.
	inline constexpr const char* installedCoreMap = "/usr/share/stemdeck/links-core.json";
}

// ~/.config/StemDeck/links-<X>.json; without it the file envMapFile names,
// with every "{X}" in a peer replaced by the letter; without either, the
// default map (decklinks::installedCoreMap), and with no default, no links.
// The first found replaces the others whole: a user file without rec_L/R
// leaves the recorder unlinked.
// nullopt + error: a file that is named or there but wrong or missing.
std::optional<a3::links::LinkMap> deckLinkMap (char letter, const std::string& home, const char* envMapFile,
											   const juce::File& defaultMapFile, juce::String& error,
											   outputs::OutputMode mode = outputs::OutputMode::bformat);

// The two-deck player's links: its rec_L/R from the default map, no links
// without one (a build that is not installed). STEMDECK_LINK_MAP and the
// user's links-<X>.json are not read: they are one-deck maps, naming ports
// (bformat_*, aux_*, cue_*) the two-deck player does not have.
std::optional<a3::links::LinkMap> recorderLinkMap (const juce::File& defaultMapFile, juce::String& error);
