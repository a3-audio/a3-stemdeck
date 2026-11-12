// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "DeckLinks.h"

#include "Outputs.h"

namespace
{
	std::string withLetter (const std::string& peer, char letter)
	{
		return juce::String (peer).replace ("{X}", juce::String::charToString (letter)).toStdString();
	}

	void substituteLetter (a3::links::LinkMap& map, char letter)
	{
		for (auto& [port, peers] : map.jack)
			for (auto& peer : peers)
				peer = withLetter (peer, letter);
		for (auto& [from, to] : map.directLinks)
		{
			from = withLetter (from, letter);
			to = withLetter (to, letter);
		}
	}

	// A stereo instance reads the map beside the named one that ends in -stereo.json
	// (links-aes67.json: links-aes67-stereo.json); the named one is B-format's.
	juce::String modeSuffix (outputs::OutputMode mode) { return mode == outputs::OutputMode::stereo ? "-stereo" : ""; }

	juce::File forMode (const juce::File& file, outputs::OutputMode mode)
	{
		return file.getSiblingFile (file.getFileNameWithoutExtension() + modeSuffix (mode) + file.getFileExtension());
	}

	std::optional<a3::links::LinkMap> parseFile (const juce::File& file, const std::vector<std::string>& ownOutputs, juce::String& error)
	{
		if (! file.existsAsFile())
		{
			error = "no link map at " + file.getFullPathName();
			return std::nullopt;
		}
		juce::var json;
		if (const auto result = juce::JSON::parse (file.loadFileAsString(), json); result.failed())
		{
			error = file.getFullPathName() + ": " + result.getErrorMessage();
			return std::nullopt;
		}
		auto map = a3::links::parseLinkMap (json, decklinks::ownInputs(), ownOutputs, error);
		if (! map)
			error = file.getFullPathName() + ": " + error;
		return map;
	}

	// Not installed (a build run from its tree): no links, as before P39.
	std::optional<a3::links::LinkMap> defaultMap (const juce::File& file, const std::vector<std::string>& ownOutputs, juce::String& error)
	{
		if (! file.existsAsFile())
			return a3::links::LinkMap {};
		return parseFile (file, ownOutputs, error);
	}
}

std::vector<std::string> decklinks::ownOutputs (outputs::OutputMode mode)
{
	std::vector<std::string> names;
	for (int channel = 0; channel < outputs::oneDeckChannelCount (mode); ++channel)
		names.push_back (outputs::oneDeckPortName (mode, channel));
	return names;
}

std::vector<std::string> decklinks::ownInputs() { return { "rec_L", "rec_R" }; }

std::optional<a3::links::LinkMap> deckLinkMap (char letter, const std::string& home, const char* envMapFile,
											   const juce::File& defaultMapFile, juce::String& error, outputs::OutputMode mode)
{
	error = {};
	const auto ownOutputs = decklinks::ownOutputs (mode);
	const auto userFile = juce::File (juce::String (home)).getChildFile (".config/StemDeck/links-" + juce::String::charToString (letter) + modeSuffix (mode) + ".json");
	if (userFile.existsAsFile())
		return parseFile (userFile, ownOutputs, error);

	if (envMapFile == nullptr || *envMapFile == '\0')
		return defaultMap (defaultMapFile, ownOutputs, error);

	auto map = parseFile (forMode (juce::File (juce::String (envMapFile)), mode), ownOutputs, error);
	if (map)
		substituteLetter (*map, letter);
	return map;
}

std::optional<a3::links::LinkMap> recorderLinkMap (const juce::File& defaultMapFile, juce::String& error)
{
	error = {};
	return defaultMap (defaultMapFile, {}, error);
}
