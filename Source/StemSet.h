#pragma once

#include <JuceHeader.h>
#include <array>
#include <vector>

// A track of one to four stereo files on the deck's four slots; slot N is
// played on output pair N, an empty slot is silent. Files are grouped by their
// common name prefix (groupStemFiles, StemNames.h), e.g.
//   "Artist - Title-001.wav" ... "Artist - Title-004.wav"
//   "Artist - Title - DUB.wav", "... - KICK.wav", "... - PADS.wav", "... - PERC.wav"
//   "Title - 1 - drums.flac", "Title - 3 - other.flac" (slots 1 and 3)
//   "Any Stereo File.flac" (slot 1 alone)
//   "Title.stem.mp4" (NI Stems, all four slots; NiStems.h)
struct StemSet
{
	static constexpr int numStems = 4;

	juce::String name;
	// Where it sits in the library: stems/Artist/Album/<files> (LibraryPath.h).
	juce::String artist, album;
	// Its folder below the library folder, '/'-separated; "" straight in it.
	juce::String folder;
	std::array<juce::File, numStems> files;
	std::array<juce::String, numStems> stemNames;
	double lengthSeconds = 0.0; // from the first file's header
	// An NI Stems set: every slot is the same .stem.mp4, slot n its track n + 1
	// (createStemReader, StemReader.h); its first file names it, as any set's.
	bool niStems = false;

	// Inline: the test targets that build the deck views do not link StemSet.cpp.
	bool hasStem (int slot) const { return files[(size_t) slot].getFullPathName().isNotEmpty(); }
	int numLoaded() const
	{
		int count = 0;
		for (int s = 0; s < numStems; ++s)
			count += hasStem (s) ? 1 : 0;
		return count;
	}
	bool isComplete() const { return numLoaded() == numStems; }
	std::array<bool, numStems> loadedSlots() const
	{
		std::array<bool, numStems> loaded {};
		for (int s = 0; s < numStems; ++s)
			loaded[(size_t) s] = hasStem (s);
		return loaded;
	}
	// The set's first file, slot by slot: what identifies it.
	juce::File firstFile() const
	{
		for (int s = 0; s < numStems; ++s)
			if (hasStem (s))
				return files[(size_t) s];
		return {};
	}

	// Scans a folder (recursively) for sets of one to four files, sorted by
	// artist, album and name; artist and album come from the folders the set
	// lies in below `folder`.
	static std::vector<StemSet> scanFolder (const juce::File& folder, juce::AudioFormatManager& formats);
};
