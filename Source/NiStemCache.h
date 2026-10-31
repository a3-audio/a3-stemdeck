#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <functional>
#include <optional>

// The stems of an NI Stems file, decoded once by ffmpeg into FLACs under the
// cache root (NiStems.h); the .stem.mp4 itself is never changed.
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

class NiStemCache
{
public:
	explicit NiStemCache (juce::File root);

	// Where the four stems of `source` are, ready or not.
	std::array<juce::File, 4> stemFilesFor (const juce::File& source) const;
	bool isReady (const juce::File& source) const;

	// Decodes the stems (blocking: run it off the message thread). False when
	// ffmpeg is missing or fails, the file is no NI Stems file, or
	// `shouldAbort` says so; nothing half-made is left behind.
	bool prepare (const juce::File& source, const std::function<bool()>& shouldAbort) const;

private:
	juce::File root;
};
