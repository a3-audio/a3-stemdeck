#pragma once

#include <JuceHeader.h>
#include <memory>

// One audio track of an MP4 -- an NI Stems file's master (track 0) or stem
// (tracks 1-4) -- decoded by FFmpeg's libavcodec: JUCE has no AAC decoder on
// Linux. AAC and ALAC alike (2026-11-01).
//
// Opening reads only the header (rate, channels, length), so a library scan
// stays quick; the track is decoded into RAM on the first read, on the thread
// that reads -- the player's decoding job or an analysis, never the message
// thread. Reads are then random access, as from a FLAC decoded into RAM.
class NiStemReader : public juce::AudioFormatReader
{
public:
	// `track` counts the file's audio tracks from 0. None if the file cannot
	// be opened or has no such track.
	static std::unique_ptr<NiStemReader> open (const juce::File& file, int track);

	bool readSamples (int* const* destChannels, int numDestChannels, int startOffsetInDestBuffer,
					  juce::int64 startSampleInFile, int numSamples) override;

private:
	NiStemReader (const juce::File& file, int streamIndex);
	bool decodeOnce();

	juce::File file;
	int streamIndex;
	bool decodeTried = false, decoded = false;
	juce::AudioBuffer<float> samples;   // the whole track, after the first read

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NiStemReader)
};
