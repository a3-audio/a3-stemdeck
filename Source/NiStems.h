#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

// NI Stems (*.stem.mp4), the parts that need no JUCE (2026-11-01).
//
// One MP4 with five audio tracks: track 0 the master, tracks 1-4 the stems in
// NI's order -- drums, bass, other, vocals by default, StemDeck's slot order --
// so track n plays on slot n. NI writes the stems' names and colours as JSON
// into the box moov/udta/stem, and title and artist as iTunes tags in
// moov/udta/meta/ilst. The audio is read by NiStemReader.h, the metadata here
// and in NiStemInfo.h.

bool isNiStemFileName (const std::string& fileName);

// "PR_OhNo.stem.mp4" -> "PR_OhNo".
std::string niStemTitle (const std::string& fileName);

struct NiStemMetadata
{
	std::optional<std::string> stemJson;   // moov/udta/stem, verbatim
	std::string title, artist;             // the iTunes tags; empty when missing
};

// Reads the bytes [offset, offset + length) of the file; fewer at its end.
using ReadAt = std::function<std::string (uint64_t offset, size_t length)>;

// Walks the box tree from the top, seeking past the audio: only moov is read.
// A cut or lying file gives what was found before the fault.
NiStemMetadata readNiStemMetadata (const ReadAt& read, uint64_t fileSize);
