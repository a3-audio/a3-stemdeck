#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <optional>

// What an NI Stems file says about itself (NiStems.h): its name from the
// iTunes tags, its stems' names and colours from NI's JSON.
struct NiStemInfo
{
	std::array<juce::String, 4> names;
	std::array<juce::uint32, 4> colours {};   // ARGB
};

// NI's JSON from moov/udta/stem; none unless it names four stems.
std::optional<NiStemInfo> parseNiStemJson (const juce::String& json);

struct NiStemFileInfo
{
	juce::String setName;               // "Artist - Title" from the tags, else the file's title
	std::optional<NiStemInfo> stems;
};

NiStemFileInfo readNiStemFileInfo (const juce::File& source);
