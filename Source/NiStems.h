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
// moov/udta/meta/ilst. JUCE decodes no AAC on Linux, so the stems are decoded
// once into a cache of FLACs (NiStemCache.h).

bool isNiStemFileName (const std::string& fileName);

// "PR_OhNo.stem.mp4" -> "PR_OhNo".
std::string niStemTitle (const std::string& fileName);

// The folder in the cache that holds one file's stems: a changed file (size or
// time) gets new ones. A hash, no '/'.
std::string niStemCacheFolderName (const std::string& path, long long size, long long modifiedMs);

// $XDG_CACHE_HOME/stemdeck/ni-stems, or ~/.cache/stemdeck/ni-stems when that is
// unset or not absolute (as dataFolder(), DataPaths.h).
std::string niStemCacheRoot (const std::string& home, const char* xdgCacheHome);

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

// ffmpeg's arguments to decode tracks 1-4 into the four files given, as the rig
// plays (44.1 kHz, 24 bit FLAC).
std::vector<std::string> niStemExtractArgs (const std::string& source, const std::array<std::string, 4>& stemFiles);
